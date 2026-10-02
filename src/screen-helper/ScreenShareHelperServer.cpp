// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareHelperServer.h"

#include "ScreenShare.h"
#include "ScreenShareExternalProcess.h"
#include "ScreenShareProcessJob.h"
#include "ScreenShareProcessOwner.h"
#include "ScreenShareIPC.h"
#include "ScreenShareFrameTransport.h"
#include "ScreenShareRelayClient.h"
#include "ScreenShareSessionPlanner.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QJsonParseError>
#include <QtCore/QProcess>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUrl>
#include <QtCore/QUuid>
#include <QtNetwork/QLocalSocket>

namespace {
constexpr int IDLE_TIMEOUT_MSEC        = 30000;
constexpr int SELF_TEST_FILE_WAIT_MSEC = 5000;

struct ProcessOwnerIdentity {
	bool present = false;
	bool valid = true;
	qint64 processID = 0;
	QString identity;
	quint64 sequence = 0;
};

ProcessOwnerIdentity processOwnerFromPayload(const QJsonObject &payload) {
	ProcessOwnerIdentity owner;
	owner.present = payload.contains(QStringLiteral("owner_process_id"))
		|| payload.contains(QStringLiteral("owner_process_identity"))
		|| payload.contains(QStringLiteral("owner_operation_sequence"));
	if (!owner.present) return owner;
	bool validProcess = false;
	bool validIdentity = false;
	owner.processID = payload.value(QStringLiteral("owner_process_id")).toVariant().toLongLong(&validProcess);
	const quint64 identity = payload.value(QStringLiteral("owner_process_identity")).toString().toULongLong(&validIdentity, 16);
	owner.valid = validProcess && owner.processID > 0 && validIdentity && identity != 0;
	owner.identity = QString::number(identity, 16);
	if (payload.contains(QStringLiteral("owner_operation_sequence"))) {
		bool validSequence = false;
		owner.sequence = payload.value(QStringLiteral("owner_operation_sequence")).toString().toULongLong(&validSequence);
		owner.valid = owner.valid && validSequence && owner.sequence > 0;
	}
	return owner;
}

bool ownerMayReplace(const ProcessOwnerIdentity &owner, const ScreenShareHelperServer::ManagedSession &session) {
	return session.ownerProcessID == 0 || (owner.present && owner.processID == session.ownerProcessID
		&& owner.identity == session.ownerProcessIdentity);
}

QString streamIDFromPayload(const QJsonObject &payload) {
	return payload.value(QStringLiteral("stream_id")).toString().trimmed();
}

QString roleTokenFromPayload(const QJsonObject &payload) {
	const QString roleToken = payload.value(QStringLiteral("relay_role_token")).toString().trimmed();
	if (!roleToken.isEmpty()) {
		return roleToken;
	}

	switch (static_cast< MumbleProto::ScreenShareRelayRole >(payload.value(QStringLiteral("relay_role")).toInt())) {
		case MumbleProto::ScreenShareRelayRolePublisher:
			return QStringLiteral("publisher");
		case MumbleProto::ScreenShareRelayRoleViewer:
			return QStringLiteral("viewer");
		default:
			return QStringLiteral("unknown");
	}
}

QString codecTokenFromPayload(const QJsonObject &payload) {
	const QString codecToken = payload.value(QStringLiteral("codec_token")).toString().trimmed();
	if (!codecToken.isEmpty()) {
		return codecToken;
	}

	return Mumble::ScreenShare::codecToConfigToken(
		Mumble::ScreenShare::IPC::codecFromJson(payload.value(QStringLiteral("codec"))));
}

QString relaySchemeFromPayload(const QJsonObject &payload) {
	const QUrl relayUrl(payload.value(QStringLiteral("relay_url")).toString());
	const QString scheme = relayUrl.scheme().trimmed().toLower();
	return scheme.isEmpty() ? QStringLiteral("unknown") : scheme;
}

QByteArray serializeReply(const QJsonObject &reply) {
	QByteArray data = QJsonDocument(reply).toJson(QJsonDocument::Compact);
	data.append('\n');
	return data;
}

void appendWarnings(QJsonObject *payload, const QStringList &warnings) {
	if (!payload || warnings.isEmpty()) {
		return;
	}

	QJsonArray warningArray = payload->value(QStringLiteral("warnings")).toArray();
	for (const QString &warning : warnings) {
		if (!warning.trimmed().isEmpty()) {
			warningArray.push_back(warning);
		}
	}
	payload->insert(QStringLiteral("warnings"), warningArray);
}

QJsonArray stringListToJson(const QStringList &values) {
	QJsonArray array;
	for (const QString &value : values) {
		if (!value.trimmed().isEmpty()) {
			array.push_back(value.trimmed());
		}
	}
	return array;
}

bool replySucceeded(const QJsonObject &reply, QString *errorMessage = nullptr) {
	return Mumble::ScreenShare::IPC::replySucceeded(reply, errorMessage);
}

QJsonObject makeSelfTestPayload(const QString &streamID, const QString &relayUrl, const QString &relayRoomID,
								const MumbleProto::ScreenShareRelayRole relayRole) {
	const quint64 now = static_cast< quint64 >(QDateTime::currentMSecsSinceEpoch());

	QJsonObject payload;
	payload.insert(QStringLiteral("stream_id"), streamID);
	payload.insert(QStringLiteral("owner_session"), 1);
	payload.insert(QStringLiteral("scope"), static_cast< int >(MumbleProto::ScreenShareScopeChannel));
	payload.insert(QStringLiteral("scope_id"), 1);
	payload.insert(QStringLiteral("relay_url"), relayUrl);
	payload.insert(QStringLiteral("relay_room_id"), relayRoomID);
	payload.insert(QStringLiteral("internal_self_test"), true);
	payload.insert(QStringLiteral("relay_transport"), static_cast< int >(MumbleProto::ScreenShareRelayTransportDirect));
	payload.insert(QStringLiteral("relay_transport_token"), QStringLiteral("direct"));
	payload.insert(QStringLiteral("relay_role"), static_cast< int >(relayRole));
	payload.insert(QStringLiteral("relay_role_token"), Mumble::ScreenShare::relayRoleToConfigToken(relayRole));
	payload.insert(QStringLiteral("relay_token"), QStringLiteral("self-test-token"));
	payload.insert(QStringLiteral("relay_session_id"), QStringLiteral("self-test-session"));
	payload.insert(QStringLiteral("relay_token_expires_at"), QString::number(now + 60000));
	payload.insert(QStringLiteral("created_at"), QString::number(now));
	payload.insert(QStringLiteral("state"), static_cast< int >(MumbleProto::ScreenShareLifecycleStateActive));
	payload.insert(QStringLiteral("codec"), static_cast< int >(MumbleProto::ScreenShareCodecH264));
	payload.insert(QStringLiteral("codec_token"), QStringLiteral("h264"));
	payload.insert(QStringLiteral("width"), 1280);
	payload.insert(QStringLiteral("height"), 720);
	payload.insert(QStringLiteral("fps"), 30);
	payload.insert(QStringLiteral("bitrate_kbps"), 4500);
	payload.insert(QStringLiteral("min_bitrate_kbps"), 1200);
	payload.insert(QStringLiteral("max_bitrate_kbps"), 6000);
	payload.insert(QStringLiteral("quality_profile"), QStringLiteral("auto"));
	payload.insert(QStringLiteral("capture_source_id"), QStringLiteral("self-test"));
	payload.insert(QStringLiteral("prefer_hardware_encoding"), true);
	return payload;
}

} // namespace

struct ScreenShareHelperServer::OwnerOperations {
	std::unique_ptr< ScreenShareProcessOwner > process;
	// Keep stop tombstones until this exact owner exits, including absent jobs.
	QHash< QString, QPair< quint64, bool > > latest;
};

ScreenShareHelperServer::ScreenShareHelperServer(QObject *parent)
	: QObject(parent), m_server(new QLocalServer(this)), m_capabilities(ScreenShareMediaSupport::probe()) {
	m_idleTimer.setInterval(IDLE_TIMEOUT_MSEC);
	m_idleTimer.setSingleShot(true);

	connect(m_server, &QLocalServer::newConnection, this, &ScreenShareHelperServer::handleNewConnection);
	connect(&m_idleTimer, &QTimer::timeout, this, &ScreenShareHelperServer::handleIdleTimeout);
}

ScreenShareHelperServer::~ScreenShareHelperServer() {
	stopAllSessions();
}

bool ScreenShareHelperServer::start(QString *errorMessage) {
	return start(Mumble::ScreenShare::IPC::socketBaseName(), errorMessage);
}

bool ScreenShareHelperServer::start(const QString &socketBaseName, QString *errorMessage) {
	const QString socketName = Mumble::ScreenShare::IPC::socketPath(socketBaseName);

	QLocalServer::removeServer(socketName);
#ifndef Q_OS_WIN
	QFile::remove(socketName);
#endif

	// SECURITY: restrict the local socket / named pipe to the current user account. Without this the
	// default DACL can let other accounts on the machine connect and drive the helper (start screen
	// capture, point it at a relay). UserAccessOption sets a user-only DACL on Windows and user-only
	// permissions on the socket file on Unix. (Peer authentication between same-user processes is a
	// separate, deferred hardening item - see SECURITY-FIXES.md.)
	m_server->setSocketOptions(QLocalServer::UserAccessOption);

	if (!m_server->listen(socketName)) {
		if (errorMessage) {
			*errorMessage = m_server->errorString();
		}

		return false;
	}

	qInfo().nospace() << "ScreenShareHelper: listening on " << socketName;
	refreshIdleTimer();
	return true;
}

void ScreenShareHelperServer::handleNewConnection() {
	while (QLocalSocket *socket = m_server->nextPendingConnection()) {
		m_socketBuffers.insert(socket, QByteArray());
		connect(socket, &QLocalSocket::readyRead, this, &ScreenShareHelperServer::handleSocketReadyRead);
		connect(socket, &QLocalSocket::disconnected, this, &ScreenShareHelperServer::handleSocketDisconnected);
	}

	refreshIdleTimer();
}

void ScreenShareHelperServer::handleSocketReadyRead() {
	QLocalSocket *socket = qobject_cast< QLocalSocket * >(sender());
	if (!socket || !m_socketBuffers.contains(socket)) {
		return;
	}
	if (socket->state() != QLocalSocket::ConnectedState) return;

	QByteArray &buffer = m_socketBuffers[socket];
	buffer.append(socket->readAll());

	const qsizetype newlineIndex = buffer.indexOf('\n');
	if (newlineIndex < 0) {
		return;
	}

	const QByteArray rawRequest = buffer.left(newlineIndex).trimmed();
	// Consume only the framed request line; preserve any trailing bytes instead of discarding the
	// whole buffer, so a fragmented or pipelined send is not silently truncated.
	buffer.remove(0, newlineIndex + 1);

	const QPointer< QLocalSocket > guardedSocket(socket);
	const auto reply = [guardedSocket](const QJsonObject &result) {
		if (!guardedSocket || guardedSocket->state() != QLocalSocket::ConnectedState) return;
		guardedSocket->write(serializeReply(result));
		guardedSocket->flush();
		guardedSocket->disconnectFromServer();
	};
	if (rawRequest.isEmpty()) { reply(Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("Empty request."))); return; }
	QJsonParseError parseError;
	const QJsonDocument requestDoc = QJsonDocument::fromJson(rawRequest, &parseError);
	if (parseError.error != QJsonParseError::NoError || !requestDoc.isObject()) {
		reply(Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("Malformed JSON request."))); return;
	}
	// One request per connection, with a deferred response for starting/stopping jobs.
	disconnect(socket, &QLocalSocket::readyRead, this, &ScreenShareHelperServer::handleSocketReadyRead);
	dispatchRequest(requestDoc.object(), reply, socket);
}

void ScreenShareHelperServer::handleSocketDisconnected() {
	QLocalSocket *socket = qobject_cast< QLocalSocket * >(sender());
	if (!socket) {
		return;
	}

	// Successful start replies close their socket too. Only an unfinished start is cancelled.
	for (auto *sessions : { &m_publishSessions, &m_viewSessions }) {
		const auto snapshot = *sessions;
		for (auto it = snapshot.cbegin(); it != snapshot.cend(); ++it) {
			if (it->requestSocket == socket && it->job && it->job->state() != ScreenShareProcessJob::State::Running)
				stopSession(*sessions, it.key());
		}
	}
	m_socketBuffers.remove(socket);
	socket->deleteLater();
	refreshIdleTimer();
}

void ScreenShareHelperServer::handleIdleTimeout() {
	if (!m_publishSessions.isEmpty() || !m_viewSessions.isEmpty()) {
		refreshIdleTimer();
		return;
	}

	qInfo("ScreenShareHelper: idle timeout reached, shutting down.");
	QCoreApplication::quit();
}

void ScreenShareHelperServer::dispatchRequest(const QJsonObject &request, ReplyCallback callback, QLocalSocket *socket) {
	const int version = request.value(QStringLiteral("version")).toInt();
	if (version < Mumble::ScreenShare::IPC::MINIMUM_PROTOCOL_VERSION
		|| version > Mumble::ScreenShare::IPC::PROTOCOL_VERSION) {
		callback(Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("Unsupported protocol version."))); return;
	}
	const auto command = Mumble::ScreenShare::IPC::commandFromName(request.value(QStringLiteral("command")).toString());
	if (!command) { callback(Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("Unknown command."))); return; }
	QJsonObject payload = request.value(QStringLiteral("payload")).toObject();
	if (version < 2) payload.insert(QStringLiteral("native_frame_requested"), false);
	switch (*command) {
		case Mumble::ScreenShare::IPC::Command::QueryCapabilities: callback(handleQueryCapabilities()); break;
		case Mumble::ScreenShare::IPC::Command::StartPublish: startSession(payload, true, callback, socket); break;
		case Mumble::ScreenShare::IPC::Command::StartView: startSession(payload, false, callback, socket); break;
		case Mumble::ScreenShare::IPC::Command::StopPublish: stopSessionRequest(payload, true, callback); break;
		case Mumble::ScreenShare::IPC::Command::StopView: stopSessionRequest(payload, false, callback); break;
	}
}

QJsonObject ScreenShareHelperServer::capabilityPayload() const {
	QJsonObject payload;
	payload.insert(QStringLiteral("supports_signaling"), true);
	payload.insert(QStringLiteral("helper_available"), true);
	payload.insert(QStringLiteral("capture_supported"), m_capabilities.captureSupported);
	payload.insert(QStringLiteral("capture_permission_granted"), m_capabilities.capturePermissionGranted);
	payload.insert(QStringLiteral("capture_permission_request_required"),
				   m_capabilities.capturePermissionRequestRequired);
	payload.insert(QStringLiteral("view_supported"), m_capabilities.viewSupported);
	payload.insert(QStringLiteral("hardware_encoding_preferred"), m_capabilities.hardwareEncodingPreferred);
	payload.insert(QStringLiteral("hardware_encode_supported"), m_capabilities.hardwareEncodeSupported);
	payload.insert(QStringLiteral("hardware_decode_supported"), m_capabilities.hardwareDecodeSupported);
	payload.insert(QStringLiteral("zero_copy_supported"), m_capabilities.zeroCopySupported);
	payload.insert(QStringLiteral("roi_supported"), m_capabilities.roiSupported);
	payload.insert(QStringLiteral("damage_metadata_supported"), m_capabilities.damageMetadataSupported);
	payload.insert(QStringLiteral("gstreamer_available"), m_capabilities.gstreamerAvailable);
	payload.insert(QStringLiteral("gstreamer_livekit_publish_available"),
				   m_capabilities.gstreamerLiveKitPublishAvailable);
	payload.insert(QStringLiteral("gstreamer_livekit_view_available"),
				   m_capabilities.gstreamerLiveKitViewAvailable);
	payload.insert(QStringLiteral("gstreamer_version"), m_capabilities.gstreamerVersion);
	payload.insert(QStringLiteral("gstreamer_missing_elements"),
				   stringListToJson(m_capabilities.missingGStreamerElements));
	payload.insert(QStringLiteral("supported_codecs"),
				   Mumble::ScreenShare::IPC::codecListToJson(m_capabilities.supportedCodecs));
	payload.insert(QStringLiteral("max_width"), static_cast< int >(m_capabilities.maxWidth));
	payload.insert(QStringLiteral("max_height"), static_cast< int >(m_capabilities.maxHeight));
	payload.insert(QStringLiteral("max_fps"), static_cast< int >(m_capabilities.maxFps));
	payload.insert(QStringLiteral("capture_backend"), m_capabilities.captureBackend);
	payload.insert(QStringLiteral("capture_backends"), stringListToJson(m_capabilities.captureBackends));
	payload.insert(QStringLiteral("supported_ingest_protocols"), stringListToJson(m_capabilities.ingestProtocols));
	payload.insert(QStringLiteral("drm_playback_supported"), false);
	payload.insert(QStringLiteral("drm_systems"), stringListToJson(m_capabilities.drmSystems));
	payload.insert(QStringLiteral("queue_budget_frames"), static_cast< int >(m_capabilities.queueBudgetFrames));
	payload.insert(QStringLiteral("native_frame_transport_version"), 2);
	payload.insert(QStringLiteral("native_frame_transport"), QStringLiteral("shared-memory-bgra-ring"));
	payload.insert(QStringLiteral("native_frame_slot_count"),
				   static_cast< int >(Mumble::ScreenShare::FrameTransport::SlotCount));
	payload.insert(QStringLiteral("native_frame_production_feed_available"), true);
	payload.insert(QStringLiteral("status"), m_capabilities.statusMessage);
	payload.insert(QStringLiteral("mode"), QStringLiteral("external-process"));
	payload.insert(QStringLiteral("encoder_backends"), ScreenShareSessionPlanner::advertisedEncoderBackends());
	payload.insert(QStringLiteral("relay_contracts"), ScreenShareRelayClient::advertisedContracts());
	payload.insert(QStringLiteral("runtime_relay_transports"), ScreenShareRelayClient::runtimeRelayTransports());
	payload.insert(QStringLiteral("signaling_relay_transports"), ScreenShareRelayClient::signalingRelayTransports());
	payload.insert(QStringLiteral("runtime_support"),
				   ScreenShareExternalProcess::runtimeSupportToJson(ScreenShareExternalProcess::probeRuntimeSupport()));
	return payload;
}

QJsonObject ScreenShareHelperServer::handleQueryCapabilities() const {
	return Mumble::ScreenShare::IPC::makeSuccessReply(capabilityPayload());
}

void ScreenShareHelperServer::startSession(const QJsonObject &payload, bool publish, ReplyCallback callback,
										   QLocalSocket *socket) {
	const auto owner = processOwnerFromPayload(payload);
	if (!owner.valid) {
		callback(Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("Invalid client process ownership."))); return;
	}
	const auto plan = publish ? ScreenShareSessionPlanner::planPublish(payload, m_capabilities)
							  : ScreenShareSessionPlanner::planView(payload, m_capabilities);
	if (!plan.valid) { callback(Mumble::ScreenShare::IPC::makeErrorReply(plan.errorMessage)); return; }
	const QString streamID = streamIDFromPayload(plan.payload);
	const QString label = publish ? QStringLiteral("publish") : QStringLiteral("view");
	auto &sessions = publish ? m_publishSessions : m_viewSessions;
	if (!ownerMayReplace(owner, sessions.value(streamID))) {
		callback(Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("The session belongs to another client process."))); return;
	}
	QString operationError;
	if (owner.present && !acceptOwnerOperation(owner.processID, owner.identity, streamID,
		publish, false, owner.sequence, &operationError)) {
		callback(Mumble::ScreenShare::IPC::makeErrorReply(operationError)); return;
	}
	auto *job = new ScreenShareProcessJob([plan, publish](int attempt, QObject *processParent) {
		QJsonObject attemptPlan = plan.payload;
		attemptPlan.insert(QStringLiteral("_launch_attempt"), attempt);
		return publish ? ScreenShareExternalProcess::preparePublish(attemptPlan, processParent)
					   : ScreenShareExternalProcess::prepareView(attemptPlan, processParent);
	}, this);
	QString ownerError;
	if (owner.present && !job->bindOwnerProcess(owner.processID, owner.identity, &ownerError)) {
		delete job;
		callback(Mumble::ScreenShare::IPC::makeErrorReply(ownerError)); return;
	}
	const auto previous = sessions.take(streamID);
	const bool waitForPrevious = previous.job && previous.job->state() != ScreenShareProcessJob::State::Terminal;
	ManagedSession session;
	session.payload = plan.payload;
	session.job = job;
	session.requestSocket = socket;
	session.ownerProcessID = owner.processID;
	session.ownerProcessIdentity = owner.identity;
	// Registration precedes both process preparation and start, including replacement shutdown.
	sessions.insert(streamID, session);
	job->terminated = [this, job, streamID, publish]() {
		auto &current = publish ? m_publishSessions : m_viewSessions;
		if (current.value(streamID).job == job) current.remove(streamID);
		job->deleteLater();
		refreshIdleTimer();
	};
	job->start([this, job, streamID, publish, label, callback](bool success, const QString &error) {
		auto &current = publish ? m_publishSessions : m_viewSessions;
		if (!success || current.value(streamID).job != job) {
			callback(Mumble::ScreenShare::IPC::makeErrorReply(error.isEmpty()
				? QStringLiteral("Screen sharing startup was superseded.") : error));
			return;
		}
		auto &active = current[streamID];
		const auto &launch = job->launch();
		const QString mode = label + (launch.usedStub ? QStringLiteral("-stub") : QStringLiteral("-live"));
		active.payload.insert(QStringLiteral("mode"), mode);
		active.payload.insert(QStringLiteral("execution_mode"), launch.executionMode);
		active.payload.insert(QStringLiteral("active_endpoint_url"), launch.endpointUrl);
		active.payload.insert(QStringLiteral("active_encoder_backend"), launch.selectedEncoder);
		active.payload.insert(QStringLiteral("active_capture_source"), launch.selectedCaptureSource);
		active.payload.insert(QStringLiteral("active_renderer_backend"), launch.selectedRenderer);
		active.payload.insert(QStringLiteral("active_process_id"), QString::number(launch.processID));
		active.payload.insert(QStringLiteral("actual_fps"), active.payload.value(QStringLiteral("fps")));
		active.payload.insert(QStringLiteral("actual_bitrate_kbps"), active.payload.value(QStringLiteral("bitrate_kbps")));
		active.payload.insert(QStringLiteral("dropped_frames"), 0);
		active.payload.insert(QStringLiteral("adaptive_downgrade_reason"), QString());
		active.payload.insert(QStringLiteral("native_frame_feed_available"), !job->frameKey().isEmpty());
		active.payload.insert(QStringLiteral("native_frame_shared_memory_key"), job->frameKey());
		active.payload.insert(QStringLiteral("native_frame_generation"), QString::number(job->frameGeneration()));
		appendWarnings(&active.payload, launch.warnings);
		logSessionPlanSummary(active.payload, label, QStringLiteral("ready"));
		logPayloadWarnings(active.payload, label, streamID);
		attachProcessLogging(streamID, publish, label);
		// Clearing request ownership before the success reply distinguishes normal socket close from cancellation.
		active.requestSocket.clear();
		callback(Mumble::ScreenShare::IPC::makeSuccessReply(active.payload));
	}, waitForPrevious);
	if (waitForPrevious) {
		auto *oldJob = previous.job.data();
		const auto terminated = oldJob->terminated;
		const QPointer< ScreenShareProcessJob > replacement(job);
		oldJob->terminated = [terminated, replacement]() {
			if (terminated) terminated();
			if (replacement) replacement->resumeStart();
		};
		oldJob->cancel();
	}
	refreshIdleTimer();
}

void ScreenShareHelperServer::stopSessionRequest(const QJsonObject &payload, bool publish, ReplyCallback callback) {
	const QString streamID = streamIDFromPayload(payload);
	if (streamID.isEmpty()) { callback(Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("Stop requires stream_id."))); return; }
	const auto owner = processOwnerFromPayload(payload);
	auto &sessions = publish ? m_publishSessions : m_viewSessions;
	if (!owner.valid || !ownerMayReplace(owner, sessions.value(streamID))) {
		callback(Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("The session belongs to another client process."))); return;
	}
	QString operationError;
	if (owner.present && !acceptOwnerOperation(owner.processID, owner.identity, streamID,
		publish, true, owner.sequence, &operationError)) {
		callback(Mumble::ScreenShare::IPC::makeErrorReply(operationError)); return;
	}
	stopSession(sessions, streamID, [callback, streamID]() {
		callback(Mumble::ScreenShare::IPC::makeSuccessReply(QJsonObject{ { QStringLiteral("stream_id"), streamID } }));
	});
	refreshIdleTimer();
}

bool ScreenShareHelperServer::acceptOwnerOperation(qint64 processID, const QString &identity,
	const QString &streamID, bool publish, bool stop, quint64 sequence, QString *error) {
	const QString ownerKey = QString::number(processID) + QLatin1Char(':') + identity;
	auto operations = m_ownerOperations.value(ownerKey);
	if (!operations) {
		operations = std::make_shared< OwnerOperations >();
		operations->process = std::make_unique< ScreenShareProcessOwner >(this);
		if (!operations->process->watch(processID, identity, [this, ownerKey]() {
			QTimer::singleShot(0, this, [this, ownerKey]() { m_ownerOperations.remove(ownerKey); });
		}, error)) return false;
		m_ownerOperations.insert(ownerKey, operations);
	}
	const QString streamKey = (publish ? QStringLiteral("publish:") : QStringLiteral("view:")) + streamID;
	const auto latest = operations->latest.value(streamKey);
	if (sequence < latest.first || (sequence != 0 && sequence == latest.first && latest.second && !stop)) {
		if (error) *error = QStringLiteral("The screen sharing operation was superseded.");
		return false;
	}
	// A same-sequence start retry can negotiate a legacy/native fallback. A
	// stop tombstone cannot be retried as a start, and a lower sequence can
	// neither resurrect a cancelled job nor stop its newer replacement.
	if (sequence != 0) operations->latest.insert(streamKey, qMakePair(sequence, stop));
	return true;
}

void ScreenShareHelperServer::runSelfTest(ReplyCallback callback) {
	// Each test owns its temporary relay directory. Poll with timers so media and IPC continue to run.
	const auto directory = std::make_shared< QTemporaryDir >(QDir::tempPath() + QStringLiteral("/mumble-screen-self-test-XXXXXX"));
	if (!directory->isValid()) { callback(Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("Unable to create self-test relay."))); return; }
	const QString streamID = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const QString relayUrl = QUrl::fromLocalFile(directory->path() + QDir::separator()).toString();
	const auto payload = makeSelfTestPayload(streamID, relayUrl, streamID, MumbleProto::ScreenShareRelayRolePublisher);
	startSession(payload, true, [this, directory, streamID, relayUrl, callback](const QJsonObject &publishReply) {
		if (!replySucceeded(publishReply)) { callback(publishReply); return; }
		const auto result = publishReply.value(QStringLiteral("payload")).toObject();
		const QString path = QUrl(result.value(QStringLiteral("active_endpoint_url")).toString()).toLocalFile();
		auto *poll = new QTimer(this);
		const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + SELF_TEST_FILE_WAIT_MSEC;
		connect(poll, &QTimer::timeout, this, [this, directory, streamID, relayUrl, callback, result, path, poll, deadline]() {
			const qint64 size = QFileInfo(path).size();
			if (size <= 0 && QDateTime::currentMSecsSinceEpoch() < deadline) return;
			poll->stop(); poll->deleteLater();
			stopSession(m_publishSessions, streamID, [this, directory, streamID, relayUrl, callback, result, path, size]() {
				if (size <= 0) { callback(Mumble::ScreenShare::IPC::makeErrorReply(QStringLiteral("Self-test relay remained empty."))); return; }
				const auto viewer = makeSelfTestPayload(streamID, relayUrl, streamID, MumbleProto::ScreenShareRelayRoleViewer);
				startSession(viewer, false, [this, directory, streamID, callback, result, path, size](const QJsonObject &viewReply) {
					if (!replySucceeded(viewReply)) { callback(viewReply); return; }
					QTimer::singleShot(500, this, [this, directory, streamID, callback, result, path, size, viewReply]() {
						stopSession(m_viewSessions, streamID, [directory, streamID, callback, result, path, size, viewReply]() {
							// Preserve the self-test artifact and its existing JSON contract for diagnostics.
							directory->setAutoRemove(false);
							callback(Mumble::ScreenShare::IPC::makeSuccessReply(QJsonObject{
								{ QStringLiteral("stream_id"), streamID }, { QStringLiteral("relay_room_id"), streamID },
								{ QStringLiteral("relay_url"), QUrl::fromLocalFile(directory->path() + QDir::separator()).toString() },
								{ QStringLiteral("output_file"), path }, { QStringLiteral("output_size"), QString::number(size) },
								{ QStringLiteral("publish_reply"), result },
								{ QStringLiteral("view_reply"), viewReply.value(QStringLiteral("payload")).toObject() },
								{ QStringLiteral("runtime_support"), ScreenShareExternalProcess::runtimeSupportToJson(
									ScreenShareExternalProcess::probeRuntimeSupport()) } }));
						});
					});
				}, nullptr);
			});
		});
		poll->start(100);
	}, nullptr);
}

void ScreenShareHelperServer::stopAllSessions() {
	const QStringList publishIDs = m_publishSessions.keys();
	for (const QString &streamID : publishIDs) {
		stopSession(m_publishSessions, streamID);
	}

	const QStringList viewIDs = m_viewSessions.keys();
	for (const QString &streamID : viewIDs) {
		stopSession(m_viewSessions, streamID);
	}
}

void ScreenShareHelperServer::stopSession(QHash< QString, ManagedSession > &sessions, const QString &streamID,
										std::function< void() > stopped) {
	// Retain stopping jobs until termination. A replacement must wait for the old
	// capture/decoder to exit instead of briefly running both processes together.
	const auto session = sessions.value(streamID);
	if (!session.job) { if (stopped) stopped(); return; }
	auto *job = session.job.data();
	if (job->state() == ScreenShareProcessJob::State::Terminal) { if (stopped) stopped(); return; }
	const auto previous = job->terminated;
	job->terminated = [previous, stopped]() { if (previous) previous(); if (stopped) stopped(); };
	job->cancel();
}

void ScreenShareHelperServer::logSessionPlanSummary(const QJsonObject &payload, const QString &label,
													const QString &phase) const {
	const QString streamID       = payload.value(QStringLiteral("stream_id")).toString().trimmed();
	const QString role           = roleTokenFromPayload(payload);
	const QString relayScheme    = relaySchemeFromPayload(payload);
	const QString codec          = codecTokenFromPayload(payload);
	const QString plannedBackend = payload.value(QStringLiteral("planned_encoder_backend")).toString().trimmed();
	const QString actualBackend  = payload.value(QStringLiteral("active_encoder_backend"))
									  .toString(payload.value(QStringLiteral("active_renderer_backend")).toString())
									  .trimmed();
	const QString plannedCapture = payload.value(QStringLiteral("capture_backend")).toString().trimmed();
	const QString captureSource = payload.value(QStringLiteral("active_capture_source")).toString().trimmed();
	const bool captureAudio = payload.value(QStringLiteral("capture_audio")).toBool(false);
	const QString audioSource = payload.value(QStringLiteral("audio_source_id")).toString().trimmed();
	const QString executionMode = payload.value(QStringLiteral("execution_mode")).toString().trimmed();

	qInfo().noquote() << QStringLiteral("ScreenShareHelper[%1:%2]: %3 summary role=%4 relay_scheme=%5 codec=%6 "
										"planned_backend=%7 actual_backend=%8 planned_capture=%9 capture_source=%10 "
										"capture_audio=%11 audio_source=%12 execution_mode=%13")
							 .arg(label, streamID, phase, role, relayScheme, codec,
								  plannedBackend.isEmpty() ? QStringLiteral("-") : plannedBackend,
								  actualBackend.isEmpty() ? QStringLiteral("-") : actualBackend,
								  plannedCapture.isEmpty() ? QStringLiteral("-") : plannedCapture,
								  captureSource.isEmpty() ? QStringLiteral("-") : captureSource,
								  captureAudio ? QStringLiteral("true") : QStringLiteral("false"),
								  audioSource.isEmpty() ? QStringLiteral("-") : audioSource,
								  executionMode.isEmpty() ? QStringLiteral("-") : executionMode);
}

void ScreenShareHelperServer::logPayloadWarnings(const QJsonObject &payload, const QString &label,
												 const QString &streamID) {
	const QJsonArray warnings = payload.value(QStringLiteral("warnings")).toArray();
	for (const QJsonValue &warningValue : warnings) {
		const QString warning = warningValue.toString().trimmed();
		if (!warning.isEmpty()) {
			qWarning().noquote()
				<< QStringLiteral("ScreenShareHelper[%1:%2]: warning: %3").arg(label, streamID, warning);
		}
	}
}

void ScreenShareHelperServer::attachProcessLogging(const QString &streamID, const bool publish, const QString &label) {
	QHash< QString, ManagedSession > &sessions = publish ? m_publishSessions : m_viewSessions;
	if (!sessions.contains(streamID) || !sessions.value(streamID).job || !sessions.value(streamID).job->launch().process) {
		return;
	}

	QProcess *process = sessions.value(streamID).job->launch().process;
	const bool binaryStdout = sessions.value(streamID).payload.value(QStringLiteral("native_frame_feed_available")).toBool();
	if (binaryStdout) process->setReadChannel(QProcess::StandardError);
	const auto logOutput = [process, streamID, label, binaryStdout]() {
		const QString output = QString::fromUtf8(binaryStdout ? process->readAllStandardError() : process->readAll()).trimmed();
		if (!output.isEmpty()) {
			qInfo().noquote() << QStringLiteral("ScreenShareHelper[%1:%2]: %3").arg(label, streamID, output);
		}
	};
	if (binaryStdout) connect(process, &QProcess::readyReadStandardError, this, logOutput);
	else connect(process, &QProcess::readyRead, this, logOutput);

	connect(process, &QProcess::errorOccurred, this, [streamID, label](QProcess::ProcessError error) {
		qWarning().noquote() << QStringLiteral("ScreenShareHelper[%1:%2]: process error %3")
									.arg(label, streamID)
									.arg(static_cast< int >(error));
	});

}

void ScreenShareHelperServer::refreshIdleTimer() {
	if (!m_publishSessions.isEmpty() || !m_viewSessions.isEmpty()) {
		m_idleTimer.stop();
		return;
	}

	m_idleTimer.start();
}
