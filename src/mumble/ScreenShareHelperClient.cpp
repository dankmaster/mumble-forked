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
#include <QtCore/QDeadlineTimer>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QFileInfo>
#include <QtCore/QFutureWatcher>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonParseError>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QStandardPaths>
#include <QtCore/QThread>
#include <QtConcurrent>
#include <QtNetwork/QLocalSocket>

namespace {
constexpr int HELPER_CONNECT_TIMEOUT_MSEC = 1000;
constexpr int HELPER_REQUEST_TIMEOUT_MSEC = 45000;
constexpr int HELPER_PICK_SOURCE_TIMEOUT_MSEC = 150000;
constexpr int HELPER_START_TIMEOUT_MSEC   = 20000;
constexpr qsizetype MAX_HELPER_REPLY_BYTES = 1024 * 1024;

QMutex g_cachedCapabilitiesMutex;
ScreenShareHelperClient::CapabilitySnapshot g_cachedCapabilities;
bool g_haveCachedCapabilities = false;

QProcessEnvironment helperRuntimeEnvironment(const QString &helperExecutable) {
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	const QDir appRoot = QFileInfo(helperExecutable).absoluteDir();
	const QString gstRoot = appRoot.filePath(QStringLiteral("gstreamer"));
	const QString gstBin = QDir(gstRoot).filePath(QStringLiteral("bin"));
	const QString gstPlugins = QDir(gstRoot).filePath(QStringLiteral("lib/gstreamer-1.0"));
#ifdef Q_OS_WIN
	const QString executableSuffix = QStringLiteral(".exe");
#else
	const QString executableSuffix;
#endif
	const QString gstScanner = QDir(gstRoot).filePath(
		QStringLiteral("libexec/gstreamer-1.0/gst-plugin-scanner") + executableSuffix);
	const QString gstLaunch = QDir(gstBin).filePath(QStringLiteral("gst-launch-1.0") + executableSuffix);
	const QString gstInspect = QDir(gstBin).filePath(QStringLiteral("gst-inspect-1.0") + executableSuffix);
	if (QFileInfo::exists(gstLaunch)) {
		environment.insert(QStringLiteral("PATH"), gstBin + QDir::listSeparator() + environment.value(QStringLiteral("PATH")));
		environment.insert(QStringLiteral("MUMBLE_SCREENSHARE_GST_LAUNCH_PATH"), gstLaunch);
		if (QFileInfo::exists(gstInspect)) {
			environment.insert(QStringLiteral("MUMBLE_SCREENSHARE_GST_INSPECT_PATH"), gstInspect);
		}
		environment.insert(QStringLiteral("GST_PLUGIN_PATH_1_0"), gstPlugins);
		environment.insert(QStringLiteral("GST_PLUGIN_SYSTEM_PATH_1_0"), gstPlugins);
		if (QFileInfo::exists(gstScanner)) {
			environment.insert(QStringLiteral("GST_PLUGIN_SCANNER"), gstScanner);
			environment.insert(QStringLiteral("GST_PLUGIN_SCANNER_1_0"), gstScanner);
		}
		QString stateRoot = Global::get().qdBasePath.absolutePath();
		if (stateRoot.trimmed().isEmpty()) stateRoot = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
		if (!stateRoot.trimmed().isEmpty()) {
			QDir().mkpath(stateRoot);
			environment.insert(QStringLiteral("GST_REGISTRY_1_0"), QDir(stateRoot).filePath(QStringLiteral("gstreamer-registry.bin")));
		}
	}
	return environment;
}

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
		case Mumble::ScreenShare::IPC::Command::PickSource:
			return QStringLiteral("pick-source");
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
		case Mumble::ScreenShare::IPC::Command::PickSource:
			return HELPER_PICK_SOURCE_TIMEOUT_MSEC;
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

qint64 activeProcessIDFromReply(const QJsonObject &reply) {
	const QJsonValue value = reply.value(QStringLiteral("payload"))
								 .toObject()
								 .value(QStringLiteral("active_process_id"));
	bool ok = false;
	const qint64 processID =
		value.isString() ? value.toString().toLongLong(&ok) : static_cast< qint64 >(value.toDouble(0));
	return ok || processID > 0 ? processID : 0;
}

QJsonObject runCapabilityProbeProcess(const QString &helperExecutable, QString *errorMessage) {
	qInfo().noquote()
		<< QStringLiteral("ScreenShareHelperClient: probing capabilities executable=%1 mode=process")
			   .arg(helperExecutable);

	QProcess process;
	process.setProcessChannelMode(QProcess::SeparateChannels);
	process.setProcessEnvironment(helperRuntimeEnvironment(helperExecutable));
	process.start(helperExecutable, { QStringLiteral("--print-capabilities-json") });
	if (!process.waitForStarted(HELPER_CONNECT_TIMEOUT_MSEC)) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Failed to start helper capability probe: %1").arg(process.errorString());
		}
		return {};
	}

	QByteArray stdoutBytes;
	QByteArray stderrBytes;
	auto drainProcessOutput = [&]() {
		stdoutBytes.append(process.readAllStandardOutput());
		stderrBytes.append(process.readAllStandardError());
		return stdoutBytes.size() <= MAX_HELPER_REPLY_BYTES && stderrBytes.size() <= MAX_HELPER_REPLY_BYTES;
	};
	QElapsedTimer deadline;
	deadline.start();
	while (process.state() != QProcess::NotRunning && deadline.elapsed() < HELPER_REQUEST_TIMEOUT_MSEC) {
		const int remaining = HELPER_REQUEST_TIMEOUT_MSEC - static_cast< int >(deadline.elapsed());
		process.waitForReadyRead(qMin(remaining, 250));
		if (!drainProcessOutput()) {
			process.kill();
			process.waitForFinished(500);
			if (errorMessage) {
				*errorMessage = QStringLiteral("Helper capability probe exceeded the reply size limit.");
			}
			return {};
		}
	}
	if (process.state() != QProcess::NotRunning) {
		process.kill();
		process.waitForFinished(500);
		if (errorMessage) {
			*errorMessage = QStringLiteral("Timed out waiting for helper capability probe.");
		}
		return {};
	}
	if (!drainProcessOutput()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Helper capability probe exceeded the reply size limit.");
		}
		return {};
	}
	stdoutBytes = stdoutBytes.trimmed();
	stderrBytes = stderrBytes.trimmed();
	if (process.exitStatus() != QProcess::NormalExit || process.exitCode() != 0) {
		if (errorMessage) {
			const QString detail =
				QString::fromUtf8(stderrBytes.isEmpty() ? stdoutBytes.left(512) : stderrBytes.left(512)).trimmed();
			*errorMessage = detail.isEmpty()
								? QStringLiteral("Helper capability probe exited with code %1.").arg(process.exitCode())
								: detail;
		}
		return {};
	}

	QByteArray jsonReply = stdoutBytes;
	const QList< QByteArray > lines = stdoutBytes.split('\n');
	for (const QByteArray &line : lines) {
		const QByteArray candidate = line.trimmed();
		if (candidate.startsWith('{') && candidate.endsWith('}')) {
			jsonReply = candidate;
			break;
		}
	}
	if (jsonReply.isEmpty()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Helper capability probe returned an empty reply.");
		}
		return {};
	}

	QJsonParseError parseError;
	const QJsonDocument replyDoc = QJsonDocument::fromJson(jsonReply, &parseError);
	if (parseError.error != QJsonParseError::NoError || !replyDoc.isObject()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Malformed helper capability probe reply.");
		}
		return {};
	}

	return replyDoc.object();
}
} // namespace

ScreenShareHelperClient::ScreenShareHelperClient(QObject *parent)
	: QObject(parent), m_capabilities(advertisedCapabilities()) {
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

ScreenShareHelperClient::CapabilitySnapshot ScreenShareHelperClient::initialCapabilitySnapshot() {
	CapabilitySnapshot snapshot;
	snapshot.helperExecutable = defaultHelperExecutablePath();

	const QFileInfo helperInfo(snapshot.helperExecutable);
	snapshot.helperAvailable = helperInfo.exists() && helperInfo.isFile() && helperInfo.isExecutable();
	snapshot.probeComplete   = !snapshot.helperAvailable;
	return snapshot;
}

ScreenShareHelperClient::CapabilitySnapshot ScreenShareHelperClient::advertisedCapabilities() {
	QMutexLocker locker(&g_cachedCapabilitiesMutex);
	if (g_haveCachedCapabilities) {
		return g_cachedCapabilities;
	}

	locker.unlock();
	return initialCapabilitySnapshot();
}

ScreenShareHelperClient::CapabilitySnapshot ScreenShareHelperClient::detectLocalCapabilities() {
	CapabilitySnapshot snapshot = initialCapabilitySnapshot();
	snapshot.probeComplete      = true;
	if (!snapshot.helperAvailable) {
		cacheAdvertisedCapabilities(snapshot);
		return snapshot;
	}

	QString errorMessage;
	const QJsonObject reply = runCapabilityProbeProcess(snapshot.helperExecutable, &errorMessage);
	if (reply.isEmpty()) {
		qWarning().noquote() << QStringLiteral("ScreenShareHelperClient: capability probe failed for %1: %2")
									.arg(snapshot.helperExecutable, errorMessage);
		cacheAdvertisedCapabilities(snapshot);
		return snapshot;
	}
	if (!Mumble::ScreenShare::IPC::replySucceeded(reply, &errorMessage)) {
		qWarning().noquote()
			<< QStringLiteral("ScreenShareHelperClient: helper rejected capability probe: %1").arg(errorMessage);
		cacheAdvertisedCapabilities(snapshot);
		return snapshot;
	}

	snapshot = capabilitySnapshotFromPayload(reply.value(QStringLiteral("payload")).toObject(), snapshot.helperExecutable);
	snapshot.probeComplete = true;
	qInfo().noquote() << QStringLiteral(
							 "ScreenShareHelperClient: capability probe succeeded executable=%1 capture_supported=%2 "
							 "view_supported=%3 gstreamer=%4 livekit_publish=%5 livekit_view=%6 max=%7x%8@%9 "
							 "runtime_transports=[%10]")
							 .arg(snapshot.helperExecutable)
							 .arg(boolToken(snapshot.captureSupported))
							 .arg(boolToken(snapshot.viewSupported))
							 .arg(boolToken(snapshot.gstreamerAvailable))
							 .arg(boolToken(snapshot.gstreamerLiveKitPublishAvailable))
							 .arg(boolToken(snapshot.gstreamerLiveKitViewAvailable))
							 .arg(snapshot.maxWidth)
							 .arg(snapshot.maxHeight)
							 .arg(snapshot.maxFps)
							 .arg(intListToken(snapshot.runtimeRelayTransports));
	cacheAdvertisedCapabilities(snapshot);
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
	QProcess process;
	process.setProgram(helperExecutable);
	process.setArguments(helperLaunchArguments());
	process.setProcessEnvironment(helperRuntimeEnvironment(helperExecutable));
	if (!process.startDetached()) {
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
												 const bool launchIfNeeded, const int protocolVersion) {
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
		QJsonDocument(Mumble::ScreenShare::IPC::makeRequest(command, payload, protocolVersion)).toJson(QJsonDocument::Compact)
		+ QByteArray(1, '\n');
	const int requestTimeoutMsec = helperRequestTimeoutMsec(command);
	// Helper discovery and process startup use HELPER_START_TIMEOUT_MSEC above. Once connected, this single
	// deadline covers both writing the request and receiving its complete newline-terminated reply.
	QDeadlineTimer requestDeadline(requestTimeoutMsec, Qt::PreciseTimer);
	if (socket.write(requestData) < 0) {
		if (errorMessage) {
			*errorMessage =
				socketErrorMessage(socket, QStringLiteral("Failed to send the screen-share helper request."));
		}
		return {};
	}
	const qint64 writeTimeRemaining = requestDeadline.remainingTime();
	if (writeTimeRemaining <= 0 || !socket.waitForBytesWritten(static_cast< int >(writeTimeRemaining))) {
		if (errorMessage) {
			*errorMessage =
				socketErrorMessage(socket, QStringLiteral("Timed out sending the screen-share helper request."));
		}
		return {};
	}

	QByteArray replyBytes;
	while (!replyBytes.contains('\n')) {
		const qint64 remaining = requestDeadline.remainingTime();
		if (remaining <= 0 || !socket.waitForReadyRead(static_cast< int >(remaining))) {
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
		if (replyBytes.size() > MAX_HELPER_REPLY_BYTES) {
			if (errorMessage) {
				*errorMessage = QStringLiteral("Screen-share helper reply exceeded the size limit.");
			}
			return {};
		}
	}
	if (replyBytes.size() > MAX_HELPER_REPLY_BYTES) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Screen-share helper reply exceeded the size limit.");
		}
		return {};
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
	payload.insert(QStringLiteral("capture_audio"), session.captureAudio);
	payload.insert(QStringLiteral("audio_source_id"), session.captureAudio ? session.audioSourceID : QString());
	payload.insert(QStringLiteral("publisher_process_id"), QString::number(QCoreApplication::applicationPid()));
	payload.insert(QStringLiteral("min_bitrate_kbps"), static_cast< int >(session.minBitrateKbps));
	payload.insert(QStringLiteral("max_bitrate_kbps"), static_cast< int >(session.maxBitrateKbps));
	payload.insert(QStringLiteral("codec_preference"),
				   Mumble::ScreenShare::codecPreferenceString(session.codecFallbackOrder));
	payload.insert(QStringLiteral("prefer_hardware_encoding"), true);
	return payload;
}

void ScreenShareHelperClient::applyAdvertisedCapabilities(MumbleProto::Version &msg) {
	const CapabilitySnapshot snapshot = advertisedCapabilities();

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

bool ScreenShareHelperClient::startPublish(const ScreenShareSession &session, QString *errorMessage,
										   qint64 *processID) {
	if (processID) {
		*processID = 0;
	}
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

	if (processID) {
		*processID = activeProcessIDFromReply(reply);
	}
	qInfo().noquote()
		<< QStringLiteral("ScreenShareHelperClient: start-publish stream=%1 accepted").arg(session.streamID);
	return true;
}

ScreenShareHelperClient::PortalPickResult ScreenShareHelperClient::pickSource(const QString &helperExecutable,
																			  QString *errorMessage) {
	PortalPickResult result;
	QString localError;
	const QJsonObject reply =
		sendRequest(Mumble::ScreenShare::IPC::Command::PickSource, QJsonObject(), helperExecutable, &localError, true);
	if (reply.isEmpty() || !Mumble::ScreenShare::IPC::replySucceeded(reply, &localError)) {
		if (errorMessage) {
			*errorMessage = localError;
		}
		qWarning().noquote() << QStringLiteral("ScreenShareHelperClient: pick-source failed: %1")
									.arg(localError.isEmpty() ? QStringLiteral("unknown error") : localError);
		return result;
	}

	const QJsonObject payload = reply.value(QStringLiteral("payload")).toObject();
	result.nodeId   = static_cast< quint32 >(qMax(0, payload.value(QStringLiteral("node_id")).toInt()));
	result.width    = static_cast< quint32 >(qMax(0, payload.value(QStringLiteral("width")).toInt()));
	result.height   = static_cast< quint32 >(qMax(0, payload.value(QStringLiteral("height")).toInt()));
	result.sourceType = payload.value(QStringLiteral("source_type")).toString().trimmed();
	result.valid = result.nodeId > 0;
	if (!result.valid && errorMessage) {
		*errorMessage = QStringLiteral("The portal did not return a usable capture source.");
	}
	qInfo().noquote()
		<< QStringLiteral("ScreenShareHelperClient: pick-source accepted node=%1 size=%2x%3 type=%4")
			   .arg(result.nodeId)
			   .arg(result.width)
			   .arg(result.height)
			   .arg(result.sourceType.isEmpty() ? QStringLiteral("-") : result.sourceType);
	return result;
}

bool ScreenShareHelperClient::stopPublish(const QString &streamID, QString *errorMessage) {
	QJsonObject payload;
	payload.insert(QStringLiteral("stream_id"), streamIDForStopPayload(streamID));
	const QJsonObject reply = sendRequest(Mumble::ScreenShare::IPC::Command::StopPublish, payload,
										  m_capabilities.helperExecutable, errorMessage, false);
	return !reply.isEmpty() && Mumble::ScreenShare::IPC::replySucceeded(reply, errorMessage);
}

bool ScreenShareHelperClient::startView(const ScreenShareSession &session, QString *errorMessage,
										qint64 *processID, NativeFrameTransport *frameTransport) {
	if (processID) {
		*processID = 0;
	}
	if (frameTransport) *frameTransport = {};
	QString localError;
	QJsonObject viewPayload = payloadFromSession(session);
	viewPayload.insert(QStringLiteral("native_frame_requested"), true);
	QJsonObject reply = sendRequest(Mumble::ScreenShare::IPC::Command::StartView, viewPayload,
										  m_capabilities.helperExecutable, &localError, true);
	bool started = !reply.isEmpty() && Mumble::ScreenShare::IPC::replySucceeded(reply, &localError);
	if (!started) {
		viewPayload.insert(QStringLiteral("native_frame_requested"), false);
		QString fallbackError;
		QJsonObject fallbackReply = sendRequest(Mumble::ScreenShare::IPC::Command::StartView, viewPayload,
													m_capabilities.helperExecutable, &fallbackError, true);
		if (fallbackReply.isEmpty() || !Mumble::ScreenShare::IPC::replySucceeded(fallbackReply, &fallbackError)) {
			fallbackReply = sendRequest(Mumble::ScreenShare::IPC::Command::StartView, viewPayload,
											  m_capabilities.helperExecutable, &fallbackError, true,
											  Mumble::ScreenShare::IPC::MINIMUM_PROTOCOL_VERSION);
		}
		if (!fallbackReply.isEmpty() && Mumble::ScreenShare::IPC::replySucceeded(fallbackReply, &fallbackError)) {
			reply = std::move(fallbackReply);
			started = true;
			localError.clear();
		} else if (!fallbackError.isEmpty()) {
			localError = fallbackError;
		}
	}
	if (!started) {
		if (errorMessage) {
			*errorMessage = localError;
		}
		qWarning().noquote() << QStringLiteral("ScreenShareHelperClient: start-view stream=%1 failed: %2")
									.arg(session.streamID,
										 localError.isEmpty() ? QStringLiteral("unknown error") : localError);
		return false;
	}

	if (processID) {
		*processID = activeProcessIDFromReply(reply);
	}
	if (frameTransport) {
		const QJsonObject payload = reply.value(QStringLiteral("payload")).toObject();
		frameTransport->sharedMemoryKey = payload.value(QStringLiteral("native_frame_shared_memory_key")).toString();
		frameTransport->generation = payload.value(QStringLiteral("native_frame_generation")).toVariant().toULongLong();
		frameTransport->feedAvailable = payload.value(QStringLiteral("native_frame_feed_available")).toBool();
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
	if (m_capabilityRefreshInProgress) {
		return;
	}

	m_capabilityRefreshInProgress = true;
	auto *watcher = new QFutureWatcher< CapabilitySnapshot >(this);
	connect(watcher, &QFutureWatcher< CapabilitySnapshot >::finished, this, [this, watcher]() {
		const CapabilitySnapshot snapshot = watcher->result();
		watcher->deleteLater();

		m_capabilityRefreshInProgress = false;
		m_capabilities                = snapshot;
		cacheAdvertisedCapabilities(snapshot);
		emit capabilitiesChanged();
	});
	watcher->setFuture(QtConcurrent::run([]() { return ScreenShareHelperClient::detectLocalCapabilities(); }));
}

void ScreenShareHelperClient::cacheAdvertisedCapabilities(const CapabilitySnapshot &snapshot) {
	QMutexLocker locker(&g_cachedCapabilitiesMutex);
	g_cachedCapabilities     = snapshot;
	g_haveCachedCapabilities = true;
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
