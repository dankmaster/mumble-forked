// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareHelperClient.h"

#include "MumbleApplication.h"
#include "ScreenShare.h"
#include "ScreenShareManager.h"
#include "Global.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtNetwork/QLocalSocket>

namespace {
constexpr int HELPER_CONNECT_TIMEOUT_MSEC = 1000;
constexpr int HELPER_REQUEST_TIMEOUT_MSEC = 45000;
constexpr int HELPER_START_TIMEOUT_MSEC   = 20000;

QString helperSocketBaseName() {
	const QString explicitName = QProcessEnvironment::systemEnvironment().value(
		QLatin1String("MUMBLE_SCREENSHARE_HELPER_SOCKET"));
	if (!explicitName.trimmed().isEmpty()) {
		return Mumble::ScreenShare::IPC::sanitizeSocketBaseName(explicitName);
	}

	QString basePath = Global::get().qdBasePath.absolutePath().trimmed();
	if (basePath.isEmpty()) {
		basePath = QCoreApplication::applicationDirPath();
	}

	if (basePath.trimmed().isEmpty()) {
		return Mumble::ScreenShare::IPC::socketBaseName();
	}

	MumbleApplication *app = MumbleApplication::instance();
	QString appRoot        = app ? app->applicationVersionRootPath() : QCoreApplication::applicationDirPath();
	if (appRoot.trimmed().isEmpty()) {
		appRoot = QCoreApplication::applicationDirPath();
	}

	const QString socketIdentity =
		QDir::toNativeSeparators(basePath) + QLatin1Char('\n') + QDir::toNativeSeparators(appRoot);
	const QByteArray profileHash = QCryptographicHash::hash(socketIdentity.toUtf8(), QCryptographicHash::Sha256)
									   .toHex()
									   .left(16);
	return Mumble::ScreenShare::IPC::sanitizeSocketBaseName(
		QStringLiteral("%1-%2").arg(Mumble::ScreenShare::IPC::socketBaseName(), QString::fromLatin1(profileHash)));
}

QString helperSocketPath() {
	return Mumble::ScreenShare::IPC::socketPath(helperSocketBaseName());
}

QString streamIDForStopPayload(const QString &streamID) {
	return streamID.trimmed();
}

QList< int > relayTransportListFromJson(const QJsonValue &value) {
	QList< int > transports;
	const QJsonArray array = value.toArray();
	transports.reserve(array.size());
	for (const QJsonValue &entry : array) {
		const int transport = entry.toInt(static_cast< int >(MumbleProto::ScreenShareRelayTransportUnknown));
		switch (static_cast< MumbleProto::ScreenShareRelayTransport >(transport)) {
			case MumbleProto::ScreenShareRelayTransportDirect:
			case MumbleProto::ScreenShareRelayTransportWebRTC:
				if (!transports.contains(transport)) {
					transports.append(transport);
				}
				break;
			case MumbleProto::ScreenShareRelayTransportUnknown:
			default:
				break;
		}
	}

	return transports;
}

QStringList stringListFromJson(const QJsonValue &value) {
	QStringList values;
	const QJsonArray array = value.toArray();
	for (const QJsonValue &entry : array) {
		const QString token = entry.toString().trimmed();
		if (!token.isEmpty() && !values.contains(token)) {
			values.append(token);
		}
	}
	return values;
}

unsigned int nonNegativePayloadValue(const QJsonObject &payload, const char *key) {
	const int rawValue = payload.value(QLatin1String(key)).toInt();
	if (rawValue <= 0) {
		return 0;
	}

	return static_cast< unsigned int >(rawValue);
}

unsigned int limitFromPayload(const QJsonObject &payload, const char *key, const unsigned int hardMax) {
	return Mumble::ScreenShare::sanitizeLimit(nonNegativePayloadValue(payload, key), 0U, hardMax);
}

QString commandToken(const Mumble::ScreenShare::IPC::Command command) {
	switch (command) {
		case Mumble::ScreenShare::IPC::Command::QueryCapabilities:
			return QStringLiteral("query-capabilities");
		case Mumble::ScreenShare::IPC::Command::StartPublish:
			return QStringLiteral("start-publish");
		case Mumble::ScreenShare::IPC::Command::StopPublish:
			return QStringLiteral("stop-publish");
		case Mumble::ScreenShare::IPC::Command::StartView:
			return QStringLiteral("start-view");
		case Mumble::ScreenShare::IPC::Command::StopView:
			return QStringLiteral("stop-view");
	}

	return QStringLiteral("unknown");
}

int helperRequestTimeoutMsec(const Mumble::ScreenShare::IPC::Command command) {
	switch (command) {
		case Mumble::ScreenShare::IPC::Command::StartPublish:
		case Mumble::ScreenShare::IPC::Command::StartView:
			return HELPER_REQUEST_TIMEOUT_MSEC;
		case Mumble::ScreenShare::IPC::Command::QueryCapabilities:
		case Mumble::ScreenShare::IPC::Command::StopPublish:
		case Mumble::ScreenShare::IPC::Command::StopView:
			return HELPER_CONNECT_TIMEOUT_MSEC;
	}

	return HELPER_CONNECT_TIMEOUT_MSEC;
}

QString boolToken(const bool value) {
	return value ? QStringLiteral("true") : QStringLiteral("false");
}

QString intListToken(const QList< int > &values) {
	QStringList tokens;
	tokens.reserve(values.size());
	for (const int value : values) {
		tokens.append(QString::number(value));
	}
	return tokens.join(QLatin1Char(','));
}

QString socketErrorMessage(const QLocalSocket &socket, const QString &fallback) {
	const QString message = socket.errorString().trimmed();
	if (message.isEmpty() || message == QLatin1String("Unknown error")) {
		return fallback;
	}

	return message;
}
} // namespace

ScreenShareHelperClient::ScreenShareHelperClient(QObject *parent)
	: QObject(parent), m_capabilities(detectLocalCapabilities()) {
}

QString ScreenShareHelperClient::defaultHelperExecutablePath() {
#ifdef Q_OS_WIN
	const QString helperName = QStringLiteral("mumble-screen-helper.exe");
#else
	const QString helperName = QStringLiteral("mumble-screen-helper");
#endif

	MumbleApplication *app = MumbleApplication::instance();
	const QString basePath = app ? app->applicationVersionRootPath() : QCoreApplication::applicationDirPath();
	return QDir(basePath).filePath(helperName);
}

QString ScreenShareHelperClient::diagnosticsLogPath() {
	QString basePath = Global::get().qdBasePath.absolutePath();
	if (basePath.trimmed().isEmpty()) {
		basePath = QStandardPaths::writableLocation(QStandardPaths::AppLocalDataLocation);
	}
	if (basePath.trimmed().isEmpty()) {
		basePath = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	}
	if (basePath.trimmed().isEmpty()) {
		basePath = QDir::homePath();
	}

	return QDir(basePath).filePath(QStringLiteral("screen-share-helper.log"));
}

QStringList ScreenShareHelperClient::helperLaunchArguments() {
	QStringList arguments{ QStringLiteral("--socket-name"), helperSocketBaseName() };
	if (!Global::get().s.bScreenShareDiagnostics) {
		return arguments;
	}

	const QString logPath = diagnosticsLogPath();
	const QFileInfo logInfo(logPath);
	if (!logInfo.absoluteDir().exists()) {
		logInfo.absoluteDir().mkpath(QStringLiteral("."));
	}

	arguments << QStringLiteral("--diagnostics-log-file") << logPath;
	return arguments;
}

ScreenShareHelperClient::CapabilitySnapshot ScreenShareHelperClient::detectLocalCapabilities() {
	CapabilitySnapshot snapshot;
	snapshot.helperExecutable = defaultHelperExecutablePath();

	const QFileInfo helperInfo(snapshot.helperExecutable);
	snapshot.helperAvailable = helperInfo.exists() && helperInfo.isFile() && helperInfo.isExecutable();
	if (!snapshot.helperAvailable) {
		return snapshot;
	}

	QString errorMessage;
	const QJsonObject reply = sendRequest(Mumble::ScreenShare::IPC::Command::QueryCapabilities, QJsonObject(),
										  snapshot.helperExecutable, &errorMessage, true);
	if (reply.isEmpty()) {
		qWarning().noquote() << QStringLiteral("ScreenShareHelperClient: capability probe failed for %1: %2")
									.arg(snapshot.helperExecutable, errorMessage);
		return snapshot;
	}
	if (!Mumble::ScreenShare::IPC::replySucceeded(reply, &errorMessage)) {
		qWarning().noquote()
			<< QStringLiteral("ScreenShareHelperClient: helper rejected capability probe: %1").arg(errorMessage);
		return snapshot;
	}

	snapshot = capabilitySnapshotFromPayload(reply.value(QStringLiteral("payload")).toObject(), snapshot.helperExecutable);
	qInfo().noquote() << QStringLiteral(
							 "ScreenShareHelperClient: capability probe succeeded executable=%1 capture_supported=%2 "
							 "view_supported=%3 gstreamer=%4 livekit_publish=%5 livekit_view=%6 runtime_transports=[%7]")
							 .arg(snapshot.helperExecutable, boolToken(snapshot.captureSupported),
								  boolToken(snapshot.viewSupported), boolToken(snapshot.gstreamerAvailable),
								  boolToken(snapshot.gstreamerLiveKitPublishAvailable),
								  boolToken(snapshot.gstreamerLiveKitViewAvailable),
								  intListToken(snapshot.runtimeRelayTransports));
	return snapshot;
}

ScreenShareHelperClient::CapabilitySnapshot
	ScreenShareHelperClient::capabilitySnapshotFromPayload(const QJsonObject &payload,
														   const QString &helperExecutable) {
	CapabilitySnapshot snapshot;
	snapshot.helperExecutable        = helperExecutable;
	snapshot.supportsSignaling       = payload.value(QStringLiteral("supports_signaling")).toBool(true);
	snapshot.helperAvailable         = payload.value(QStringLiteral("helper_available")).toBool(true);
	snapshot.captureSupported        = payload.value(QStringLiteral("capture_supported")).toBool(false);
	snapshot.viewSupported           = payload.value(QStringLiteral("view_supported")).toBool(false);
	snapshot.hardwareEncodeSupported = payload.value(QStringLiteral("hardware_encode_supported")).toBool(false);
	snapshot.hardwareDecodeSupported = payload.value(QStringLiteral("hardware_decode_supported")).toBool(false);
	snapshot.zeroCopySupported       = payload.value(QStringLiteral("zero_copy_supported")).toBool(false);
	snapshot.roiSupported            = payload.value(QStringLiteral("roi_supported")).toBool(false);
	snapshot.damageMetadataSupported = payload.value(QStringLiteral("damage_metadata_supported")).toBool(false);
	snapshot.gstreamerAvailable      = payload.value(QStringLiteral("gstreamer_available")).toBool(false);
	snapshot.gstreamerLiveKitPublishAvailable =
		payload.value(QStringLiteral("gstreamer_livekit_publish_available")).toBool(false);
	snapshot.gstreamerLiveKitViewAvailable =
		payload.value(QStringLiteral("gstreamer_livekit_view_available")).toBool(false);
	snapshot.supportedCodecs =
		Mumble::ScreenShare::IPC::codecListFromJson(payload.value(QStringLiteral("supported_codecs")));
	snapshot.runtimeRelayTransports =
		relayTransportListFromJson(payload.value(QStringLiteral("runtime_relay_transports")));
	snapshot.maxWidth                 = limitFromPayload(payload, "max_width", Mumble::ScreenShare::HARD_MAX_WIDTH);
	snapshot.maxHeight                = limitFromPayload(payload, "max_height", Mumble::ScreenShare::HARD_MAX_HEIGHT);
	snapshot.maxFps                   = limitFromPayload(payload, "max_fps", Mumble::ScreenShare::HARD_MAX_FPS);
	snapshot.captureBackend           = payload.value(QStringLiteral("capture_backend")).toString().trimmed();
	snapshot.captureBackends          = stringListFromJson(payload.value(QStringLiteral("capture_backends")));
	snapshot.gstreamerVersion         = payload.value(QStringLiteral("gstreamer_version")).toString().trimmed();
	snapshot.missingGStreamerElements =
		stringListFromJson(payload.value(QStringLiteral("gstreamer_missing_elements")));
	snapshot.supportedIngestProtocols = stringListFromJson(payload.value(QStringLiteral("supported_ingest_protocols")));
	snapshot.drmSystems               = stringListFromJson(payload.value(QStringLiteral("drm_systems")));
	snapshot.queueBudgetFrames = limitFromPayload(payload, "queue_budget_frames", Mumble::ScreenShare::HARD_MAX_FPS);

	if (snapshot.captureBackends.isEmpty() && !snapshot.captureBackend.isEmpty()) {
		snapshot.captureBackends.append(snapshot.captureBackend);
	}

	return snapshot;
}

bool ScreenShareHelperClient::ensureHelperRunning(const QString &helperExecutable, QString *errorMessage) {
	const QString socketPath = helperSocketPath();
	qInfo().noquote() << QStringLiteral("ScreenShareHelperClient: launching helper executable %1 with socket %2")
							 .arg(helperExecutable, socketPath);
	if (!QProcess::startDetached(helperExecutable, helperLaunchArguments())) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Failed to launch helper executable %1.").arg(helperExecutable);
		}
		qWarning().noquote()
			<< QStringLiteral("ScreenShareHelperClient: helper launch failed for %1").arg(helperExecutable);
		return false;
	}

	return true;
}

QJsonObject ScreenShareHelperClient::sendRequest(Mumble::ScreenShare::IPC::Command command, const QJsonObject &payload,
												 const QString &helperExecutable, QString *errorMessage,
												 const bool launchIfNeeded) {
	const QString socketPath = helperSocketPath();
	const QString streamID   = payload.value(QStringLiteral("stream_id")).toString().trimmed();
	qInfo().noquote()
		<< QStringLiteral("ScreenShareHelperClient: sending %1 stream=%2 executable=%3 socket=%4 launch_if_needed=%5")
			   .arg(commandToken(command), streamID.isEmpty() ? QStringLiteral("-") : streamID, helperExecutable,
					socketPath, launchIfNeeded ? QStringLiteral("true") : QStringLiteral("false"));
	const QFileInfo helperInfo(helperExecutable);
	if (!helperInfo.exists() || !helperInfo.isFile() || !helperInfo.isExecutable()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Helper executable is missing: %1").arg(helperExecutable);
		}
		return {};
	}

	QLocalSocket socket;
	auto connectToHelper = [&socket, &socketPath](const int timeoutMsec) {
		socket.abort();
		socket.connectToServer(socketPath);
		return socket.waitForConnected(timeoutMsec);
	};

	if (!connectToHelper(150)) {
		if (!launchIfNeeded || !ensureHelperRunning(helperExecutable, errorMessage)) {
			return {};
		}

		QElapsedTimer timer;
		timer.start();
		bool connected = false;
		while (timer.elapsed() < HELPER_START_TIMEOUT_MSEC) {
			if (connectToHelper(150)) {
				connected = true;
				qInfo().noquote()
					<< QStringLiteral("ScreenShareHelperClient: helper started successfully at socket %1").arg(socketPath);
				break;
			}

			QThread::msleep(50);
		}

		if (!connected) {
			if (errorMessage) {
				*errorMessage = socketErrorMessage(
					socket, QStringLiteral("Timed out connecting to the screen-share helper socket."));
			}
			return {};
		}
	}

	const QByteArray requestData =
		QJsonDocument(Mumble::ScreenShare::IPC::makeRequest(command, payload)).toJson(QJsonDocument::Compact)
		+ QByteArray(1, '\n');
	const int requestTimeoutMsec = helperRequestTimeoutMsec(command);
	if (socket.write(requestData) < 0) {
		if (errorMessage) {
			*errorMessage =
				socketErrorMessage(socket, QStringLiteral("Failed to send the screen-share helper request."));
		}
		return {};
	}
	if (!socket.waitForBytesWritten(requestTimeoutMsec)) {
		if (errorMessage) {
			*errorMessage =
				socketErrorMessage(socket, QStringLiteral("Timed out sending the screen-share helper request."));
		}
		return {};
	}

	QByteArray replyBytes;
	while (!replyBytes.contains('\n')) {
		if (!socket.waitForReadyRead(requestTimeoutMsec)) {
			if (socket.state() == QLocalSocket::UnconnectedState) {
				replyBytes.append(socket.readAll());
				break;
			}
			if (errorMessage) {
				*errorMessage = socketErrorMessage(
					socket, QStringLiteral("Timed out waiting for a reply from the screen-share helper."));
			}
			return {};
		}

		replyBytes.append(socket.readAll());
	}

	const qsizetype newlineIndex = replyBytes.indexOf('\n');
	const QByteArray jsonReply   = (newlineIndex >= 0 ? replyBytes.left(newlineIndex) : replyBytes).trimmed();
	if (jsonReply.isEmpty()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Helper returned an empty reply.");
		}
		return {};
	}

	QJsonParseError parseError;
	const QJsonDocument replyDoc = QJsonDocument::fromJson(jsonReply, &parseError);
	if (parseError.error != QJsonParseError::NoError || !replyDoc.isObject()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Malformed helper reply.");
		}
		return {};
	}

	const QJsonObject reply = replyDoc.object();
	logReplyWarnings(reply, command, streamID);
	return reply;
}

QJsonObject ScreenShareHelperClient::payloadFromSession(const ScreenShareSession &session) {
	QJsonObject payload;
	payload.insert(QStringLiteral("stream_id"), session.streamID);
	payload.insert(QStringLiteral("owner_session"), static_cast< int >(session.ownerSession));
	payload.insert(QStringLiteral("scope"), static_cast< int >(session.scope));
	payload.insert(QStringLiteral("scope_id"), static_cast< int >(session.scopeID));
	payload.insert(QStringLiteral("relay_url"), session.relayUrl);
	payload.insert(QStringLiteral("relay_room_id"), session.relayRoomID);
	payload.insert(QStringLiteral("relay_token"), session.relayToken);
	payload.insert(QStringLiteral("relay_session_id"), session.relaySessionID);
	payload.insert(QStringLiteral("relay_transport"), static_cast< int >(session.relayTransport));
	payload.insert(QStringLiteral("relay_transport_token"),
				   Mumble::ScreenShare::relayTransportToConfigToken(session.relayTransport));
	payload.insert(QStringLiteral("relay_role"), static_cast< int >(session.relayRole));
	payload.insert(QStringLiteral("relay_role_token"), Mumble::ScreenShare::relayRoleToConfigToken(session.relayRole));
	payload.insert(QStringLiteral("relay_token_expires_at"), QString::number(session.relayTokenExpiresAt));
	payload.insert(QStringLiteral("created_at"), QString::number(session.createdAt));
	payload.insert(QStringLiteral("state"), static_cast< int >(session.state));
	payload.insert(QStringLiteral("codec"), static_cast< int >(session.codec));
	payload.insert(QStringLiteral("codec_fallback_order"),
				   Mumble::ScreenShare::IPC::codecListToJson(session.codecFallbackOrder));
	payload.insert(QStringLiteral("width"), static_cast< int >(session.width));
	payload.insert(QStringLiteral("height"), static_cast< int >(session.height));
	payload.insert(QStringLiteral("fps"), static_cast< int >(session.fps));
	payload.insert(QStringLiteral("bitrate_kbps"), static_cast< int >(session.bitrateKbps));
	payload.insert(QStringLiteral("quality_profile"),
				   session.qualityProfile.trimmed().isEmpty() ? QStringLiteral("auto") : session.qualityProfile);
	payload.insert(QStringLiteral("capture_source_id"), session.captureSourceID);
	payload.insert(QStringLiteral("min_bitrate_kbps"), static_cast< int >(session.minBitrateKbps));
	payload.insert(QStringLiteral("max_bitrate_kbps"), static_cast< int >(session.maxBitrateKbps));
	payload.insert(QStringLiteral("codec_preference"),
				   Mumble::ScreenShare::codecPreferenceString(session.codecFallbackOrder));
	payload.insert(QStringLiteral("prefer_hardware_encoding"), true);
	return payload;
}

void ScreenShareHelperClient::applyAdvertisedCapabilities(MumbleProto::Version &msg) {
	const CapabilitySnapshot snapshot = detectLocalCapabilities();

	msg.set_supports_screen_share_signaling(snapshot.supportsSignaling);
	msg.set_supports_screen_share_capture(snapshot.captureSupported);
	msg.set_supports_screen_share_view(snapshot.viewSupported);

	for (const int codec : snapshot.supportedCodecs) {
		msg.add_supported_screen_share_codecs(static_cast< MumbleProto::ScreenShareCodec >(codec));
	}

	if (snapshot.maxWidth > 0) {
		msg.set_max_screen_share_width(snapshot.maxWidth);
	}
	if (snapshot.maxHeight > 0) {
		msg.set_max_screen_share_height(snapshot.maxHeight);
	}
	if (snapshot.maxFps > 0) {
		msg.set_max_screen_share_fps(snapshot.maxFps);
	}
}

const ScreenShareHelperClient::CapabilitySnapshot &ScreenShareHelperClient::capabilities() const {
	return m_capabilities;
}

bool ScreenShareHelperClient::startPublish(const ScreenShareSession &session, QString *errorMessage) {
	QString localError;
	const QJsonObject reply = sendRequest(Mumble::ScreenShare::IPC::Command::StartPublish, payloadFromSession(session),
										  m_capabilities.helperExecutable, &localError, true);
	const bool started = !reply.isEmpty() && Mumble::ScreenShare::IPC::replySucceeded(reply, &localError);
	if (!started) {
		if (errorMessage) {
			*errorMessage = localError;
		}
		qWarning().noquote() << QStringLiteral("ScreenShareHelperClient: start-publish stream=%1 failed: %2")
									.arg(session.streamID,
										 localError.isEmpty() ? QStringLiteral("unknown error") : localError);
		return false;
	}

	qInfo().noquote()
		<< QStringLiteral("ScreenShareHelperClient: start-publish stream=%1 accepted").arg(session.streamID);
	return true;
}

bool ScreenShareHelperClient::stopPublish(const QString &streamID, QString *errorMessage) {
	QJsonObject payload;
	payload.insert(QStringLiteral("stream_id"), streamIDForStopPayload(streamID));
	const QJsonObject reply = sendRequest(Mumble::ScreenShare::IPC::Command::StopPublish, payload,
										  m_capabilities.helperExecutable, errorMessage, false);
	return !reply.isEmpty() && Mumble::ScreenShare::IPC::replySucceeded(reply, errorMessage);
}

bool ScreenShareHelperClient::startView(const ScreenShareSession &session, QString *errorMessage) {
	QString localError;
	const QJsonObject reply = sendRequest(Mumble::ScreenShare::IPC::Command::StartView, payloadFromSession(session),
										  m_capabilities.helperExecutable, &localError, true);
	const bool started = !reply.isEmpty() && Mumble::ScreenShare::IPC::replySucceeded(reply, &localError);
	if (!started) {
		if (errorMessage) {
			*errorMessage = localError;
		}
		qWarning().noquote() << QStringLiteral("ScreenShareHelperClient: start-view stream=%1 failed: %2")
									.arg(session.streamID,
										 localError.isEmpty() ? QStringLiteral("unknown error") : localError);
		return false;
	}

	qInfo().noquote()
		<< QStringLiteral("ScreenShareHelperClient: start-view stream=%1 accepted").arg(session.streamID);
	return true;
}

bool ScreenShareHelperClient::stopView(const QString &streamID, QString *errorMessage) {
	QJsonObject payload;
	payload.insert(QStringLiteral("stream_id"), streamIDForStopPayload(streamID));
	const QJsonObject reply = sendRequest(Mumble::ScreenShare::IPC::Command::StopView, payload,
										  m_capabilities.helperExecutable, errorMessage, false);
	return !reply.isEmpty() && Mumble::ScreenShare::IPC::replySucceeded(reply, errorMessage);
}

void ScreenShareHelperClient::refreshCapabilities() {
	m_capabilities = detectLocalCapabilities();
	emit capabilitiesChanged();
}

void ScreenShareHelperClient::logReplyWarnings(const QJsonObject &reply, Mumble::ScreenShare::IPC::Command command,
											   const QString &streamID) {
	const QJsonArray warnings =
		reply.value(QStringLiteral("payload")).toObject().value(QStringLiteral("warnings")).toArray();
	for (const QJsonValue &warningValue : warnings) {
		const QString warning = warningValue.toString().trimmed();
		if (!warning.isEmpty()) {
			qWarning().noquote() << QStringLiteral("ScreenShareHelperClient: %1 stream=%2 warning=%3")
										.arg(commandToken(command), streamID.isEmpty() ? QStringLiteral("-") : streamID,
											 warning);
		}
	}
}
