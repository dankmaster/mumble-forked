// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareExternalProcess.h"

#include "ScreenShare.h"
#include "ScreenShareIPC.h"
#include "ScreenShareWindowsNativeCapture.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonArray>
#include <QtCore/QJsonDocument>
#include <QtCore/QProcess>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QRegularExpression>
#include <QtCore/QStandardPaths>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>

#include <optional>

namespace {
constexpr int PROBE_TIMEOUT_MSEC = 5000;
constexpr int START_TIMEOUT_MSEC = 3000;
constexpr int START_SETTLE_MSEC  = 1000;

struct RelayEndpoint {
	bool valid = false;
	QString errorMessage;
	QString endpointUrl;
	QString outputFormat;
	QString localFilePath;
};

ScreenShareExternalProcess::LaunchResult startProcess(const QString &program, const QStringList &arguments,
													  QObject *parent, const QString &executionMode);

QString readMergedProcessOutput(QProcess &process) {
	return QString::fromUtf8(process.readAll());
}

bool envFlagEnabled(const char *name) {
	const QString value = qEnvironmentVariable(name).trimmed().toLower();
	return value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
		   || value == QLatin1String("on");
}

void appendUniqueExistingDirectory(QStringList &directories, const QString &path) {
	const QString cleanedPath = QDir::cleanPath(path);
	if (cleanedPath.isEmpty() || directories.contains(cleanedPath)) {
		return;
	}

	const QFileInfo info(cleanedPath);
	if (info.isDir()) {
		directories.append(info.absoluteFilePath());
	}
}

void appendUniquePath(QStringList &paths, const QString &path) {
	const QString cleanedPath = QDir::cleanPath(path);
	if (!cleanedPath.isEmpty() && !paths.contains(cleanedPath)) {
		paths.append(cleanedPath);
	}
}

QStringList bundledGStreamerExecutableDirectories() {
	QStringList directories;
	const QString appDir = QCoreApplication::applicationDirPath();
	if (appDir.isEmpty()) {
		return directories;
	}

	appendUniqueExistingDirectory(directories, appDir);
	appendUniqueExistingDirectory(directories, QDir(appDir).filePath(QStringLiteral("bin")));
	appendUniqueExistingDirectory(directories, QDir(appDir).filePath(QStringLiteral("gstreamer/bin")));
	return directories;
}

QString findExecutableInDirectories(const QStringList &candidates, const QStringList &directories) {
	for (const QString &directory : directories) {
		for (const QString &candidate : candidates) {
			const QFileInfo info(QDir(directory).filePath(candidate));
			if (info.isFile() && info.isExecutable()) {
				return info.absoluteFilePath();
			}
		}
	}

	return QString();
}

QStringList gstreamerRuntimeRootCandidates(const QString &program) {
	QStringList roots;
	const QFileInfo programInfo(program);
	if (!programInfo.exists()) {
		return roots;
	}

	const QDir executableDir = programInfo.absoluteDir();
	appendUniquePath(roots, executableDir.absolutePath());

	if (executableDir.dirName().compare(QStringLiteral("bin"), Qt::CaseInsensitive) == 0) {
		QDir runtimeRoot = executableDir;
		runtimeRoot.cdUp();
		appendUniquePath(roots, runtimeRoot.absolutePath());
	}

	return roots;
}

QString firstExistingDirectory(const QStringList &paths) {
	for (const QString &path : paths) {
		const QFileInfo info(path);
		if (info.isDir()) {
			return info.absoluteFilePath();
		}
	}

	return QString();
}

QString firstExistingFile(const QStringList &paths) {
	for (const QString &path : paths) {
		const QFileInfo info(path);
		if (info.isFile()) {
			return info.absoluteFilePath();
		}
	}

	return QString();
}

void prependEnvironmentPath(QProcessEnvironment &environment, const QString &key, const QString &path) {
	if (path.isEmpty()) {
		return;
	}

#ifdef Q_OS_WIN
	const QLatin1Char separator(';');
#else
	const QLatin1Char separator(':');
#endif
	const QString current = environment.value(key);
	environment.insert(key, current.isEmpty() ? path : path + separator + current);
}

QProcessEnvironment processEnvironmentForExternalProgram(const QString &program) {
	QProcessEnvironment environment = QProcessEnvironment::systemEnvironment();
	const QFileInfo programInfo(program);
	const QString programName = programInfo.fileName().toLower();
	if (!programName.startsWith(QLatin1String("gst-"))) {
		return environment;
	}

	prependEnvironmentPath(environment, QStringLiteral("PATH"), programInfo.absolutePath());

	QStringList pluginPathCandidates;
	QStringList scannerCandidates;
	for (const QString &runtimeRoot : gstreamerRuntimeRootCandidates(program)) {
		const QDir root(runtimeRoot);
		pluginPathCandidates << root.filePath(QStringLiteral("lib/gstreamer-1.0"));
		scannerCandidates << root.filePath(QStringLiteral("libexec/gstreamer-1.0/gst-plugin-scanner.exe"))
						  << root.filePath(QStringLiteral("libexec/gstreamer-1.0/gst-plugin-scanner"));
	}

	const QString pluginPath = firstExistingDirectory(pluginPathCandidates);
	if (!pluginPath.isEmpty()) {
		environment.insert(QStringLiteral("GST_PLUGIN_SYSTEM_PATH_1_0"), pluginPath);
		environment.insert(QStringLiteral("GST_PLUGIN_PATH"), pluginPath);
	}

	const QString scannerPath = firstExistingFile(scannerCandidates);
	if (!scannerPath.isEmpty()) {
		environment.insert(QStringLiteral("GST_PLUGIN_SCANNER"), scannerPath);
	}

	return environment;
}

QString runProbeCommand(const QString &program, const QStringList &arguments) {
	if (program.isEmpty()) {
		return QString();
	}

	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.setProcessEnvironment(processEnvironmentForExternalProgram(program));
	process.start(program, arguments);
	if (!process.waitForStarted(START_TIMEOUT_MSEC)) {
		return QString();
	}

	process.closeWriteChannel();
	if (!process.waitForFinished(PROBE_TIMEOUT_MSEC)) {
		process.kill();
		process.waitForFinished(500);
	}

	return QString::fromUtf8(process.readAll());
}

bool gstElementAvailable(const QString &gstInspectPath, const QString &elementName) {
	if (gstInspectPath.isEmpty() || elementName.trimmed().isEmpty()) {
		return false;
	}

	QProcess process;
	process.setProcessChannelMode(QProcess::MergedChannels);
	process.setProcessEnvironment(processEnvironmentForExternalProgram(gstInspectPath));
	process.start(gstInspectPath, { elementName });
	if (!process.waitForStarted(START_TIMEOUT_MSEC)) {
		return false;
	}

	process.closeWriteChannel();
	if (!process.waitForFinished(PROBE_TIMEOUT_MSEC)) {
		process.kill();
		process.waitForFinished(500);
		return false;
	}

	return process.exitStatus() == QProcess::NormalExit && process.exitCode() == 0;
}

void appendMissingGStreamerElement(ScreenShareExternalProcess::RuntimeSupport *support, const QString &elementName,
								   const bool available) {
	if (!support || available || elementName.trimmed().isEmpty()
		|| support->missingGStreamerElements.contains(elementName)) {
		return;
	}

	support->missingGStreamerElements.append(elementName);
}

QString sanitizeRoomToken(const QString &value) {
	QString sanitized = value.trimmed();
	sanitized.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]+")), QStringLiteral("-"));
	sanitized.remove(QRegularExpression(QStringLiteral("^-+")));
	sanitized.remove(QRegularExpression(QStringLiteral("-+$")));
	return sanitized.isEmpty() ? QStringLiteral("screen-share") : sanitized;
}

QString fileContainerFormat(const QString &path) {
	const QString suffix = QFileInfo(path).suffix().trimmed().toLower();
	if (suffix == QLatin1String("mp4")) {
		return QStringLiteral("mp4");
	}
	if (suffix == QLatin1String("flv")) {
		return QStringLiteral("flv");
	}
	if (suffix == QLatin1String("ts") || suffix == QLatin1String("mpegts")) {
		return QStringLiteral("mpegts");
	}

	return QStringLiteral("matroska");
}

QString appendPathSegment(const QString &basePath, const QString &segment) {
	QString path = basePath;
	if (path.isEmpty()) {
		path = QStringLiteral("/");
	}
	if (!path.endsWith(QLatin1Char('/'))) {
		path.append(QLatin1Char('/'));
	}
	path.append(segment);
	return path;
}

QString findExecutableAny(const QStringList &candidates) {
	for (const QString &candidate : candidates) {
		const QString resolved = QStandardPaths::findExecutable(candidate);
		if (!resolved.isEmpty()) {
			return resolved;
		}
	}

	return QString();
}

QString configuredExecutablePath(const char *envName) {
	const QString configuredPath = qEnvironmentVariable(envName).trimmed();
	if (configuredPath.isEmpty()) {
		return QString();
	}

	const QFileInfo configuredInfo(configuredPath);
	if (configuredInfo.isFile() && configuredInfo.isExecutable()) {
		return configuredInfo.absoluteFilePath();
	}

	return QStandardPaths::findExecutable(configuredPath);
}

QString preferredExecutablePath(const char *envName, const QStringList &candidates,
								const QStringList &additionalDirectories = {}) {
	const QString configuredPath = configuredExecutablePath(envName);
	if (!configuredPath.isEmpty()) {
		return configuredPath;
	}

	const QString bundledPath = findExecutableInDirectories(candidates, additionalDirectories);
	if (!bundledPath.isEmpty()) {
		return bundledPath;
	}

	return findExecutableAny(candidates);
}

#ifdef Q_OS_WIN
QString existingWindowsBrowserPath(const QStringList &relativePaths) {
	QStringList roots;
	for (const char *envName : { "ProgramFiles", "ProgramFiles(x86)", "LocalAppData" }) {
		const QString root = qEnvironmentVariable(envName).trimmed();
		if (!root.isEmpty() && !roots.contains(root)) {
			roots.append(root);
		}
	}

	for (const QString &root : roots) {
		for (const QString &relativePath : relativePaths) {
			const QString candidate = QDir(root).filePath(relativePath);
			if (QFileInfo(candidate).isExecutable()) {
				return candidate;
			}
		}
	}

	return QString();
}
#endif

QString preferredBrowserPath(const ScreenShareExternalProcess::RuntimeSupport &support, QString *browserID = nullptr) {
	if (support.edgeAvailable) {
		if (browserID) {
			*browserID = QStringLiteral("edge");
		}
		return support.edgePath;
	}
	if (support.chromeAvailable) {
		if (browserID) {
			*browserID = QStringLiteral("chrome");
		}
		return support.chromePath;
	}
	if (support.firefoxAvailable) {
		if (browserID) {
			*browserID = QStringLiteral("firefox");
		}
		return support.firefoxPath;
	}

	if (browserID) {
		browserID->clear();
	}
	return QString();
}

QString browserProfileDirectory(const QJsonObject &plan, const bool publish, const QString &browserID) {
	const QString streamID          = plan.value(QStringLiteral("stream_id")).toString().trimmed();
	const QString sanitizedStreamID = streamID.isEmpty() ? QStringLiteral("screen-share") : sanitizeRoomToken(streamID);
	QString tempBase                = QStandardPaths::writableLocation(QStandardPaths::TempLocation);
	if (tempBase.trimmed().isEmpty()) {
		tempBase = QDir::tempPath();
	}

	return QDir(tempBase).filePath(
		QStringLiteral("mumble-screen-share/%1/%2-%3")
			.arg(browserID, publish ? QStringLiteral("publish") : QStringLiteral("view"), sanitizedStreamID));
}

QString browserLaunchUrl(const QJsonObject &plan, QString *errorMessage, QStringList *warnings) {
	const QString relayUrl = Mumble::ScreenShare::normalizeRelayUrl(plan.value(QStringLiteral("relay_url")).toString());
	if (relayUrl.isEmpty()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Missing or invalid relay_url.");
		}
		return QString();
	}

	QUrl launchUrl(relayUrl);
	const QString originalScheme = launchUrl.scheme().trimmed().toLower();
	if (originalScheme == QLatin1String("wss")) {
		launchUrl.setScheme(QStringLiteral("https"));
		if (warnings) {
			warnings->append(QStringLiteral("Launching the WebRTC relay UI over https while preserving the original "
											"wss relay URL as signaling metadata."));
		}
	} else if (originalScheme == QLatin1String("ws")) {
		launchUrl.setScheme(QStringLiteral("http"));
		if (warnings) {
			warnings->append(QStringLiteral("Launching the WebRTC relay UI over http while preserving the original ws "
											"relay URL as signaling metadata."));
		}
	}

	if (launchUrl.scheme().trimmed().isEmpty() || launchUrl.host().trimmed().isEmpty()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Unable to derive a browser launch URL from relay_url.");
		}
		return QString();
	}

	QUrlQuery query(launchUrl);
	auto appendQuery = [&](const QString &key, const QString &value) {
		if (!value.trimmed().isEmpty()) {
			query.addQueryItem(key, value);
		}
	};

	appendQuery(QStringLiteral("mumble_screen_share"), QStringLiteral("1"));
	appendQuery(QStringLiteral("relay_url"), relayUrl);
	appendQuery(QStringLiteral("relay_room_id"), plan.value(QStringLiteral("relay_room_id")).toString());
	appendQuery(QStringLiteral("relay_session_id"), plan.value(QStringLiteral("relay_session_id")).toString());
	appendQuery(QStringLiteral("stream_id"), plan.value(QStringLiteral("stream_id")).toString());
	appendQuery(QStringLiteral("relay_role"), plan.value(QStringLiteral("relay_role_token")).toString());
	appendQuery(QStringLiteral("codec"), plan.value(QStringLiteral("codec_token")).toString());
	appendQuery(QStringLiteral("requested_codec"), plan.value(QStringLiteral("requested_codec_token")).toString());
	appendQuery(QStringLiteral("transport"), plan.value(QStringLiteral("relay_transport_token")).toString());
	appendQuery(QStringLiteral("width"), QString::number(qMax(0, plan.value(QStringLiteral("width")).toInt())));
	appendQuery(QStringLiteral("height"), QString::number(qMax(0, plan.value(QStringLiteral("height")).toInt())));
	appendQuery(QStringLiteral("fps"), QString::number(qMax(0, plan.value(QStringLiteral("fps")).toInt())));
	appendQuery(QStringLiteral("bitrate_kbps"),
				QString::number(qMax(0, plan.value(QStringLiteral("bitrate_kbps")).toInt())));
	const bool browserPublisherCaptureAudio =
		plan.value(QStringLiteral("relay_contract_mode")).toString() == QLatin1String("browser-webrtc-runtime")
		&& plan.value(QStringLiteral("relay_role_token")).toString() == QLatin1String("publisher");
	if (browserPublisherCaptureAudio) {
		appendQuery(QStringLiteral("capture_audio"), QStringLiteral("1"));
		appendQuery(QStringLiteral("system_audio"), QStringLiteral("include"));
		appendQuery(QStringLiteral("surface_switching"), QStringLiteral("include"));
		appendQuery(QStringLiteral("self_browser_surface"), QStringLiteral("exclude"));
	}
	launchUrl.setQuery(query);
	const QString relayToken = plan.value(QStringLiteral("relay_token")).toString().trimmed();
	if (!relayToken.isEmpty()) {
		QUrlQuery fragment;
		fragment.addQueryItem(QStringLiteral("relay_token"), relayToken);
		launchUrl.setFragment(fragment.toString(QUrl::FullyEncoded));
	}
	return launchUrl.toString(QUrl::FullyEncoded);
}

ScreenShareExternalProcess::LaunchResult
	startBrowserWebRtcSession(const ScreenShareExternalProcess::RuntimeSupport &support, const QJsonObject &plan,
							  QObject *parent, const bool publish) {
	ScreenShareExternalProcess::LaunchResult launch;
	if (!support.graphicalSessionAvailable) {
		launch.errorMessage =
			QStringLiteral("A graphical desktop session is required for the helper WebRTC browser runtime.");
		return launch;
	}

	QString browserID;
	const QString browserPath = preferredBrowserPath(support, &browserID);
	if (browserPath.isEmpty()) {
		launch.errorMessage = QStringLiteral("No supported browser runtime was found for WebRTC screen sharing.");
		return launch;
	}

	QString launchUrlError;
	QStringList warnings;
	const QString launchUrl = browserLaunchUrl(plan, &launchUrlError, &warnings);
	if (launchUrl.isEmpty()) {
		launch.errorMessage = launchUrlError;
		return launch;
	}

	const QString profileDir = browserProfileDirectory(plan, publish, browserID);
	QDir(profileDir).removeRecursively();
	QDir().mkpath(profileDir);

	QStringList arguments;
	if (browserID == QLatin1String("edge") || browserID == QLatin1String("chrome")) {
		arguments << QStringLiteral("--new-window") << QStringLiteral("--disable-session-crashed-bubble")
				  << QStringLiteral("--autoplay-policy=no-user-gesture-required")
				  << QStringLiteral("--user-data-dir=%1").arg(profileDir) << QStringLiteral("--app=%1").arg(launchUrl);
	} else if (browserID == QLatin1String("firefox")) {
		arguments << QStringLiteral("-new-instance") << QStringLiteral("-profile") << profileDir
				  << QStringLiteral("-new-window") << launchUrl;
	} else {
		launch.errorMessage = QStringLiteral("Unsupported browser runtime selected for WebRTC.");
		return launch;
	}

	launch = startProcess(browserPath, arguments, parent,
						  publish ? QStringLiteral("browser-webrtc-publish") : QStringLiteral("browser-webrtc-view"));
	if (!launch.started) {
		return launch;
	}

	launch.endpointUrl = launchUrl;
	launch.warnings    = warnings;
	if (publish) {
		launch.selectedCaptureSource = QStringLiteral("browser-webrtc-%1").arg(browserID);
		launch.selectedEncoder       = QStringLiteral("browser-webrtc");
	} else {
		launch.selectedRenderer = QStringLiteral("browser-webrtc-%1").arg(browserID);
	}

	return launch;
}

QString detectRenderNode() {
#ifdef Q_OS_LINUX
	const QDir driDir(QStringLiteral("/dev/dri"));
	const QStringList nodes =
		driDir.entryList(QStringList() << QStringLiteral("renderD*"), QDir::Files | QDir::System | QDir::Readable);
	if (!nodes.isEmpty()) {
		return driDir.absoluteFilePath(nodes.front());
	}
#endif
	return QString();
}

bool anyNvidiaDevicePresent() {
#ifdef Q_OS_LINUX
	if (QFileInfo::exists(QStringLiteral("/dev/nvidiactl"))) {
		return true;
	}

	const QDir devDir(QStringLiteral("/dev"));
	const QStringList nodes =
		devDir.entryList(QStringList() << QStringLiteral("nvidia[0-9]*"), QDir::System | QDir::Files | QDir::Readable);
	return !nodes.isEmpty();
#else
	return false;
#endif
}

bool hasWindowedViewerSurface() {
#ifdef Q_OS_WIN
	return true;
#else
	return !qEnvironmentVariable("DISPLAY").trimmed().isEmpty();
#endif
}

QStringList candidateBackendOrder(const ScreenShareExternalProcess::RuntimeSupport &support,
								  const MumbleProto::ScreenShareCodec codec, const QString &plannedBackend) {
	QStringList candidates;
	auto appendUnique = [&](const QString &candidate) {
		if (!candidate.isEmpty() && !candidates.contains(candidate)) {
			candidates.append(candidate);
		}
	};

	appendUnique(plannedBackend.trimmed().toLower());

	switch (codec) {
		case MumbleProto::ScreenShareCodecH264:
			if (support.h264NvencAvailable) {
				appendUnique(QStringLiteral("nvenc-h264"));
			}
			if (support.h264VaapiAvailable) {
				appendUnique(QStringLiteral("vaapi-h264"));
			}
			if (support.h264MfAvailable) {
				appendUnique(QStringLiteral("mf-h264"));
			}
			if (support.h264QsvAvailable) {
				appendUnique(QStringLiteral("qsv-h264"));
			}
			if (support.libx264Available) {
				appendUnique(QStringLiteral("libx264-h264"));
			}
			break;
		case MumbleProto::ScreenShareCodecAV1:
			if (support.av1NvencAvailable) {
				appendUnique(QStringLiteral("nvenc-av1"));
			}
			if (support.av1VaapiAvailable) {
				appendUnique(QStringLiteral("vaapi-av1"));
			}
			if (support.av1MfAvailable) {
				appendUnique(QStringLiteral("mf-av1"));
			}
			if (support.av1QsvAvailable) {
				appendUnique(QStringLiteral("qsv-av1"));
			}
			if (support.libSvtAv1Available) {
				appendUnique(QStringLiteral("libsvtav1-av1"));
			}
			break;
		case MumbleProto::ScreenShareCodecVP8:
			if (support.libVpxVp8Available) {
				appendUnique(QStringLiteral("libvpx-vp8"));
			}
			break;
		case MumbleProto::ScreenShareCodecVP9:
			if (support.libVpxVp9Available) {
				appendUnique(QStringLiteral("libvpx-vp9"));
			}
			break;
		case MumbleProto::ScreenShareCodecUnknown:
		default:
			break;
	}

	return candidates;
}

RelayEndpoint materializeRelayEndpoint(const QJsonObject &plan,
									   const ScreenShareExternalProcess::RuntimeSupport &support) {
	RelayEndpoint endpoint;

	const QString relayUrl = Mumble::ScreenShare::normalizeRelayUrl(plan.value(QStringLiteral("relay_url")).toString());
	if (relayUrl.isEmpty()) {
		endpoint.errorMessage = QStringLiteral("Missing or invalid relay_url.");
		return endpoint;
	}

	const QString relayRoomID = sanitizeRoomToken(plan.value(QStringLiteral("relay_room_id")).toString());
	const QString relayToken  = plan.value(QStringLiteral("relay_token")).toString().trimmed();

	QUrl url(relayUrl);
	const QString scheme = url.scheme().trimmed().toLower();
	if (scheme == QLatin1String("file")) {
		if (!support.fileProtocolAvailable) {
			endpoint.errorMessage = QStringLiteral("ffmpeg on this host has no file protocol support.");
			return endpoint;
		}

		const QString originalPath = url.toLocalFile();
		if (originalPath.isEmpty()) {
			endpoint.errorMessage = QStringLiteral("The configured file relay path is empty.");
			return endpoint;
		}

		const QFileInfo originalInfo(originalPath);
		const bool treatAsDirectory =
			relayUrl.endsWith(QLatin1Char('/')) || (originalInfo.exists() && originalInfo.isDir());
		QString resolvedPath;
		if (treatAsDirectory) {
			resolvedPath = QDir(originalPath).filePath(relayRoomID + QStringLiteral(".mkv"));
		} else {
			const QString suffix    = originalInfo.suffix().trimmed().toLower();
			const QString extension = suffix.isEmpty() ? QStringLiteral("mkv") : suffix;
			const QString baseName  = originalInfo.completeBaseName().trimmed().isEmpty()
										 ? relayRoomID
										 : originalInfo.completeBaseName() + QLatin1Char('-') + relayRoomID;
			resolvedPath = originalInfo.dir().filePath(baseName + QLatin1Char('.') + extension);
		}

		const QFileInfo resolvedInfo(resolvedPath);
		QDir().mkpath(resolvedInfo.absolutePath());
		endpoint.valid         = true;
		endpoint.localFilePath = resolvedPath;
		endpoint.endpointUrl   = QUrl::fromLocalFile(resolvedPath).toString();
		endpoint.outputFormat  = fileContainerFormat(resolvedPath);
		return endpoint;
	}

	if (scheme != QLatin1String("rtmp") && scheme != QLatin1String("rtmps")) {
		endpoint.errorMessage =
			QStringLiteral("Relay scheme %1 is advertised by the server but this helper build cannot publish it yet.")
				.arg(scheme.toHtmlEscaped());
		return endpoint;
	}

	if (scheme == QLatin1String("rtmp") && !support.rtmpProtocolAvailable) {
		endpoint.errorMessage = QStringLiteral("ffmpeg on this host has no RTMP output support.");
		return endpoint;
	}
	if (scheme == QLatin1String("rtmps") && !support.rtmpsProtocolAvailable) {
		endpoint.errorMessage = QStringLiteral("ffmpeg on this host has no RTMPS output support.");
		return endpoint;
	}

	url.setPath(appendPathSegment(url.path(), relayRoomID));
	if (!relayToken.isEmpty()) {
		QUrlQuery query(url);
		query.addQueryItem(QStringLiteral("token"), relayToken);
		url.setQuery(query);
	}

	endpoint.valid        = true;
	endpoint.endpointUrl  = url.toString(QUrl::FullyEncoded);
	endpoint.outputFormat = QStringLiteral("flv");
	return endpoint;
}

bool selectCaptureSource(const ScreenShareExternalProcess::RuntimeSupport &support, QString *captureSource,
						 QString *errorMessage, QStringList *arguments, const QString &candidateBackend) {
	const QString forcedSource = qEnvironmentVariable("MUMBLE_SCREENSHARE_CAPTURE_SOURCE").trimmed().toLower();
	const QString configuredDisplay =
		qEnvironmentVariable("MUMBLE_SCREENSHARE_CAPTURE_DISPLAY", qEnvironmentVariable("DISPLAY")).trimmed();
	const bool useTestPattern = forcedSource == QLatin1String("test-pattern") || forcedSource == QLatin1String("lavfi")
								|| envFlagEnabled("MUMBLE_SCREENSHARE_TEST_PATTERN");
	const bool forceD3D11Capture = forcedSource == QLatin1String("d3d11") || forcedSource == QLatin1String("dda")
								   || forcedSource == QLatin1String("ddagrab")
								   || forcedSource == QLatin1String("native")
								   || forcedSource == QLatin1String("native-d3d11");
	const bool candidateCanConsumeD3D11Frames = candidateBackend.contains(QLatin1String("nvenc"));
	const bool preferD3D11Capture =
		forceD3D11Capture
		|| (forcedSource.isEmpty() && support.ddagrabAvailable && candidateCanConsumeD3D11Frames
			&& support.d3d11HardwareDeviceAvailable);

	if (useTestPattern) {
		if (!support.lavfiAvailable) {
			if (errorMessage) {
				*errorMessage = QStringLiteral("ffmpeg on this host has no lavfi input for test-pattern mode.");
			}
			return false;
		}

		if (captureSource) {
			*captureSource = QStringLiteral("lavfi-test-pattern");
		}
		if (arguments) {
			const int width  = qMax(1, arguments->takeFirst().toInt());
			const int height = qMax(1, arguments->takeFirst().toInt());
			const int fps    = qMax(1, arguments->takeFirst().toInt());
			arguments->clear();
			arguments->append(QStringLiteral("-re"));
			arguments->append(QStringLiteral("-f"));
			arguments->append(QStringLiteral("lavfi"));
			arguments->append(QStringLiteral("-i"));
			arguments->append(QStringLiteral("testsrc2=size=%1x%2:rate=%3").arg(width).arg(height).arg(fps));
		}
		return true;
	}

#ifdef Q_OS_WIN
	if (preferD3D11Capture) {
		if (!support.ddagrabAvailable) {
			if (errorMessage) {
				*errorMessage = QStringLiteral("ffmpeg on this host has no ddagrab input support.");
			}
			return false;
		}
		if (!support.d3d11HardwareDeviceAvailable) {
			if (errorMessage) {
				*errorMessage = QStringLiteral("No hardware D3D11 device is available for native capture.");
			}
			return false;
		}
		if (!candidateCanConsumeD3D11Frames) {
			if (errorMessage) {
				*errorMessage = QStringLiteral("The selected encoder backend cannot consume D3D11 capture frames.");
			}
			return false;
		}

		if (captureSource) {
			*captureSource = QStringLiteral("d3d11-desktop-duplication");
		}
		if (arguments) {
			const int width  = qMax(1, arguments->takeFirst().toInt());
			const int height = qMax(1, arguments->takeFirst().toInt());
			const int fps    = qMax(1, arguments->takeFirst().toInt());
			arguments->clear();
			arguments->append(QStringLiteral("-f"));
			arguments->append(QStringLiteral("lavfi"));
			arguments->append(QStringLiteral("-i"));
			arguments->append(
				QStringLiteral("ddagrab=framerate=%1:video_size=%2x%3:draw_mouse=1:output_fmt=bgra")
					.arg(fps)
					.arg(width)
					.arg(height));
		}
		return true;
	}

	if (forcedSource == QLatin1String("gdi") || forcedSource == QLatin1String("gdigrab")
		|| forcedSource == QLatin1String("desktop") || forcedSource.isEmpty()) {
		if (!support.gdigrabAvailable) {
			if (errorMessage) {
				*errorMessage = QStringLiteral("ffmpeg on this host has no gdigrab input support.");
			}
			return false;
		}

		if (captureSource) {
			*captureSource = QStringLiteral("gdigrab");
		}
		if (arguments) {
			const int width  = qMax(1, arguments->takeFirst().toInt());
			const int height = qMax(1, arguments->takeFirst().toInt());
			const int fps    = qMax(1, arguments->takeFirst().toInt());
			arguments->clear();
			arguments->append(QStringLiteral("-f"));
			arguments->append(QStringLiteral("gdigrab"));
			arguments->append(QStringLiteral("-draw_mouse"));
			arguments->append(QStringLiteral("1"));
			arguments->append(QStringLiteral("-framerate"));
			arguments->append(QString::number(fps));
			arguments->append(QStringLiteral("-video_size"));
			arguments->append(QStringLiteral("%1x%2").arg(width).arg(height));
			arguments->append(QStringLiteral("-i"));
			arguments->append(QStringLiteral("desktop"));
		}
		return true;
	}
#endif

	if (forcedSource == QLatin1String("x11") || forcedSource.isEmpty()) {
		if (!support.x11GrabAvailable) {
			if (errorMessage) {
				*errorMessage = QStringLiteral("ffmpeg on this host has no x11grab input support.");
			}
			return false;
		}
		if (configuredDisplay.isEmpty()) {
			if (errorMessage) {
				*errorMessage = QStringLiteral("No DISPLAY is available for live desktop capture. Set "
											   "MUMBLE_SCREENSHARE_TEST_PATTERN=1 for headless verification.");
			}
			return false;
		}

		if (captureSource) {
			*captureSource = QStringLiteral("x11grab");
		}
		if (arguments) {
			const int width  = qMax(1, arguments->takeFirst().toInt());
			const int height = qMax(1, arguments->takeFirst().toInt());
			const int fps    = qMax(1, arguments->takeFirst().toInt());
			arguments->clear();
			arguments->append(QStringLiteral("-f"));
			arguments->append(QStringLiteral("x11grab"));
			arguments->append(QStringLiteral("-draw_mouse"));
			arguments->append(QStringLiteral("1"));
			arguments->append(QStringLiteral("-framerate"));
			arguments->append(QString::number(fps));
			arguments->append(QStringLiteral("-video_size"));
			arguments->append(QStringLiteral("%1x%2").arg(width).arg(height));
			arguments->append(QStringLiteral("-i"));
			arguments->append(configuredDisplay + QStringLiteral("+0,0"));
		}
		return true;
	}

	if (errorMessage) {
		*errorMessage = QStringLiteral("Unsupported capture source override: %1").arg(forcedSource);
	}
	return false;
}

bool appendEncoderArguments(const ScreenShareExternalProcess::RuntimeSupport &support, const QJsonObject &plan,
							QString *selectedEncoder, QStringList *warnings, QStringList *arguments,
							QString *errorMessage, const QString &captureSource = QString()) {
	const MumbleProto::ScreenShareCodec codec =
		Mumble::ScreenShare::IPC::codecFromJson(plan.value(QStringLiteral("codec")));
	const QString plannedBackend = plan.value(QStringLiteral("planned_encoder_backend")).toString().trimmed().toLower();
	const int bitrateKbps        = qMax(2500, plan.value(QStringLiteral("bitrate_kbps")).toInt());
	const int fps                = qMax(1, plan.value(QStringLiteral("fps")).toInt());
	const int maxRateKbps        = qMax(bitrateKbps, bitrateKbps + (bitrateKbps / 8));
	const int bufferSizeKbps     = qMax(maxRateKbps, 2500);
	const QString renderNode     = detectRenderNode();
	const bool captureProducesD3D11Frames = captureSource == QLatin1String("d3d11-desktop-duplication");

	auto appendRateControl = [&](const QString &encoder) {
		arguments->append(QStringLiteral("-c:v"));
		arguments->append(encoder);
		arguments->append(QStringLiteral("-g"));
		arguments->append(QString::number(fps));
		arguments->append(QStringLiteral("-bf"));
		arguments->append(QStringLiteral("0"));
		arguments->append(QStringLiteral("-b:v"));
		arguments->append(QString::number(bitrateKbps) + QStringLiteral("k"));
		arguments->append(QStringLiteral("-maxrate"));
		arguments->append(QString::number(maxRateKbps) + QStringLiteral("k"));
		arguments->append(QStringLiteral("-bufsize"));
		arguments->append(QString::number(bufferSizeKbps) + QStringLiteral("k"));
	};

	switch (codec) {
		case MumbleProto::ScreenShareCodecH264:
			if (plannedBackend.contains(QLatin1String("nvenc")) && support.h264NvencAvailable) {
				appendRateControl(QStringLiteral("h264_nvenc"));
				arguments->append(QStringLiteral("-preset"));
				arguments->append(QStringLiteral("p5"));
				arguments->append(QStringLiteral("-tune"));
				arguments->append(QStringLiteral("ll"));
				if (!captureProducesD3D11Frames) {
					arguments->append(QStringLiteral("-pix_fmt"));
					arguments->append(QStringLiteral("yuv420p"));
				}
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("h264_nvenc");
				}
				return true;
			}
			if (plannedBackend.contains(QLatin1String("vaapi")) && support.h264VaapiAvailable
				&& !renderNode.isEmpty()) {
				arguments->append(QStringLiteral("-vaapi_device"));
				arguments->append(renderNode);
				arguments->append(QStringLiteral("-vf"));
				arguments->append(QStringLiteral("format=nv12,hwupload"));
				appendRateControl(QStringLiteral("h264_vaapi"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("h264_vaapi");
				}
				return true;
			}
			if (plannedBackend.contains(QLatin1String("mf")) && support.h264MfAvailable) {
				appendRateControl(QStringLiteral("h264_mf"));
				arguments->append(QStringLiteral("-pix_fmt"));
				arguments->append(QStringLiteral("nv12"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("h264_mf");
				}
				return true;
			}
			if (plannedBackend.contains(QLatin1String("qsv")) && support.h264QsvAvailable) {
				appendRateControl(QStringLiteral("h264_qsv"));
				arguments->append(QStringLiteral("-pix_fmt"));
				arguments->append(QStringLiteral("nv12"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("h264_qsv");
				}
				return true;
			}
			if (support.libx264Available) {
				if (warnings && plannedBackend.contains(QLatin1String("nvenc"))) {
					warnings->append(QStringLiteral("Falling back from NVENC to libx264 on this host."));
				} else if (warnings && plannedBackend.contains(QLatin1String("vaapi"))) {
					warnings->append(QStringLiteral("Falling back from VA-API to libx264 on this host."));
				} else if (warnings && plannedBackend.contains(QLatin1String("mf"))) {
					warnings->append(
						QStringLiteral("Falling back from Media Foundation H.264 to libx264 on this host."));
				} else if (warnings && plannedBackend.contains(QLatin1String("qsv"))) {
					warnings->append(
						QStringLiteral("Falling back from Intel Quick Sync H.264 to libx264 on this host."));
				}
				appendRateControl(QStringLiteral("libx264"));
				arguments->append(QStringLiteral("-preset"));
				arguments->append(QStringLiteral("veryfast"));
				arguments->append(QStringLiteral("-tune"));
				arguments->append(QStringLiteral("zerolatency"));
				arguments->append(QStringLiteral("-profile:v"));
				arguments->append(QStringLiteral("high"));
				arguments->append(QStringLiteral("-pix_fmt"));
				arguments->append(QStringLiteral("yuv420p"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("libx264");
				}
				return true;
			}
			break;
		case MumbleProto::ScreenShareCodecAV1:
			if (plannedBackend.contains(QLatin1String("nvenc")) && support.av1NvencAvailable) {
				appendRateControl(QStringLiteral("av1_nvenc"));
				arguments->append(QStringLiteral("-preset"));
				arguments->append(QStringLiteral("p5"));
				arguments->append(QStringLiteral("-tune"));
				arguments->append(QStringLiteral("ll"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("av1_nvenc");
				}
				return true;
			}
			if (plannedBackend.contains(QLatin1String("vaapi")) && support.av1VaapiAvailable && !renderNode.isEmpty()) {
				arguments->append(QStringLiteral("-vaapi_device"));
				arguments->append(renderNode);
				arguments->append(QStringLiteral("-vf"));
				arguments->append(QStringLiteral("format=nv12,hwupload"));
				appendRateControl(QStringLiteral("av1_vaapi"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("av1_vaapi");
				}
				return true;
			}
			if (plannedBackend.contains(QLatin1String("mf")) && support.av1MfAvailable) {
				appendRateControl(QStringLiteral("av1_mf"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("av1_mf");
				}
				return true;
			}
			if (plannedBackend.contains(QLatin1String("qsv")) && support.av1QsvAvailable) {
				appendRateControl(QStringLiteral("av1_qsv"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("av1_qsv");
				}
				return true;
			}
			if (support.libSvtAv1Available) {
				if (warnings
					&& (plannedBackend.contains(QLatin1String("nvenc"))
						|| plannedBackend.contains(QLatin1String("vaapi")))) {
					warnings->append(QStringLiteral("Falling back to libsvtav1 on this host."));
				} else if (warnings && plannedBackend.contains(QLatin1String("mf"))) {
					warnings->append(
						QStringLiteral("Falling back from Media Foundation AV1 to libsvtav1 on this host."));
				} else if (warnings && plannedBackend.contains(QLatin1String("qsv"))) {
					warnings->append(
						QStringLiteral("Falling back from Intel Quick Sync AV1 to libsvtav1 on this host."));
				}
				appendRateControl(QStringLiteral("libsvtav1"));
				arguments->append(QStringLiteral("-preset"));
				arguments->append(QStringLiteral("8"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("libsvtav1");
				}
				return true;
			}
			break;
		case MumbleProto::ScreenShareCodecVP8:
			if (support.libVpxVp8Available) {
				appendRateControl(QStringLiteral("libvpx"));
				arguments->append(QStringLiteral("-deadline"));
				arguments->append(QStringLiteral("realtime"));
				arguments->append(QStringLiteral("-cpu-used"));
				arguments->append(QStringLiteral("6"));
				arguments->append(QStringLiteral("-lag-in-frames"));
				arguments->append(QStringLiteral("0"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("libvpx");
				}
				return true;
			}
			break;
		case MumbleProto::ScreenShareCodecVP9:
			if (support.libVpxVp9Available) {
				appendRateControl(QStringLiteral("libvpx-vp9"));
				arguments->append(QStringLiteral("-deadline"));
				arguments->append(QStringLiteral("realtime"));
				arguments->append(QStringLiteral("-cpu-used"));
				arguments->append(QStringLiteral("4"));
				arguments->append(QStringLiteral("-lag-in-frames"));
				arguments->append(QStringLiteral("0"));
				arguments->append(QStringLiteral("-row-mt"));
				arguments->append(QStringLiteral("1"));
				if (selectedEncoder) {
					*selectedEncoder = QStringLiteral("libvpx-vp9");
				}
				return true;
			}
			break;
		case MumbleProto::ScreenShareCodecUnknown:
		default:
			break;
	}

	if (errorMessage) {
		*errorMessage = QStringLiteral("No executable ffmpeg encoder is available for codec %1.")
							.arg(Mumble::ScreenShare::codecToConfigToken(codec));
	}
	return false;
}

QString liveKitWebSocketUrlFromRelayUrl(const QJsonObject &plan, QString *errorMessage) {
	const QString relayUrl = Mumble::ScreenShare::normalizeRelayUrl(plan.value(QStringLiteral("relay_url")).toString());
	if (relayUrl.isEmpty()) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Missing or invalid relay_url.");
		}
		return QString();
	}

	QUrl url(relayUrl);
	const QString scheme = url.scheme().trimmed().toLower();
	if (scheme == QLatin1String("https")) {
		url.setScheme(QStringLiteral("wss"));
	} else if (scheme == QLatin1String("http")) {
		url.setScheme(QStringLiteral("ws"));
	} else if (scheme != QLatin1String("wss") && scheme != QLatin1String("ws")) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("GStreamer LiveKit sessions require a ws, wss, http, or https relay URL.");
		}
		return QString();
	}

	return url.toString(QUrl::FullyEncoded);
}

bool relayTokenLooksLikeJwt(const QString &token) {
	const QStringList parts = token.split(QLatin1Char('.'));
	return parts.size() == 3 && !parts.at(0).isEmpty() && !parts.at(1).isEmpty() && !parts.at(2).isEmpty();
}

QJsonObject liveKitJwtPayload(const QString &token) {
	const QStringList parts = token.split(QLatin1Char('.'));
	if (parts.size() < 2) {
		return {};
	}

	const QByteArray decoded =
		QByteArray::fromBase64(parts.at(1).toLatin1(),
							   QByteArray::Base64UrlEncoding | QByteArray::OmitTrailingEquals);
	QJsonParseError parseError;
	const QJsonDocument document = QJsonDocument::fromJson(decoded, &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		return {};
	}

	return document.object();
}

QJsonObject liveKitJwtMetadata(const QJsonObject &payload) {
	QJsonParseError parseError;
	const QJsonDocument document =
		QJsonDocument::fromJson(payload.value(QStringLiteral("metadata")).toString().toUtf8(), &parseError);
	if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
		return {};
	}

	return document.object();
}

QString liveKitPublisherIdentityFromPlan(const QJsonObject &plan) {
	const QJsonObject tokenPayload = liveKitJwtPayload(plan.value(QStringLiteral("relay_token")).toString().trimmed());
	const QJsonObject metadata     = liveKitJwtMetadata(tokenPayload);
	const int serverID             = metadata.value(QStringLiteral("mumble_server_id")).toInt(-1);
	const QString streamID =
		metadata.value(QStringLiteral("mumble_stream_id")).toString(plan.value(QStringLiteral("stream_id")).toString());
	const int ownerSession =
		metadata.value(QStringLiteral("mumble_owner_session")).toInt(plan.value(QStringLiteral("owner_session")).toInt());
	if (serverID < 0 || streamID.trimmed().isEmpty() || ownerSession <= 0) {
		return QString();
	}

	return QStringLiteral("mumble-%1-%2-%3-publisher").arg(serverID).arg(streamID).arg(ownerSession);
}

QString liveKitIdentityFromToken(const QJsonObject &plan, const QString &fallbackRole) {
	const QJsonObject tokenPayload = liveKitJwtPayload(plan.value(QStringLiteral("relay_token")).toString().trimmed());
	const QString subject         = tokenPayload.value(QStringLiteral("sub")).toString().trimmed();
	if (!subject.isEmpty()) {
		return subject;
	}

	const QString streamID = plan.value(QStringLiteral("stream_id")).toString(QStringLiteral("screen-share")).trimmed();
	return QStringLiteral("mumble-%1-%2").arg(fallbackRole, streamID.isEmpty() ? QStringLiteral("screen-share") : streamID);
}

QString gstCaptureApiToken() {
	const QString configured = qEnvironmentVariable("MUMBLE_SCREENSHARE_GST_CAPTURE_API").trimmed().toLower();
	if (configured == QLatin1String("dxgi") || configured == QLatin1String("wgc")) {
		return configured;
	}

#ifdef Q_OS_WIN
	return QStringLiteral("wgc");
#else
	return QString();
#endif
}

struct GStreamerEncoderSelection {
	bool valid = false;
	QString encoderElement;
	QString selectedEncoder;
	QString rawFormat;
	bool consumesD3D11Memory = false;
	QStringList properties;
};

GStreamerEncoderSelection selectGStreamerEncoder(const ScreenShareExternalProcess::RuntimeSupport &support,
												 const QJsonObject &plan, QString *errorMessage) {
	GStreamerEncoderSelection selection;
	const MumbleProto::ScreenShareCodec codec =
		Mumble::ScreenShare::IPC::codecFromJson(plan.value(QStringLiteral("codec")));
	if (codec != MumbleProto::ScreenShareCodecH264) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("The GStreamer LiveKit runtime currently supports H.264 only.");
		}
		return selection;
	}

	const QString plannedBackend = plan.value(QStringLiteral("planned_encoder_backend")).toString().trimmed().toLower();
	const int bitrateKbps        = qBound(300, plan.value(QStringLiteral("bitrate_kbps")).toInt(4000), 50000);
	const int maxBitrateKbps =
		qBound(bitrateKbps, plan.value(QStringLiteral("max_bitrate_kbps")).toInt(qMax(6000, bitrateKbps)), 50000);
	const int fps     = qMax(1, plan.value(QStringLiteral("fps")).toInt(30));
	const int gopSize = qMax(fps, fps * 2);

	auto appendCommonH264Properties = [&](QStringList *properties) {
		properties->append(QStringLiteral("bitrate=%1").arg(bitrateKbps));
		properties->append(QStringLiteral("max-bitrate=%1").arg(maxBitrateKbps));
	};

	if ((plannedBackend.contains(QLatin1String("nvd3d11")) || plannedBackend.contains(QLatin1String("gstreamer")))
		&& support.gstNvD3D11H264EncoderAvailable) {
		selection.valid              = true;
		selection.encoderElement     = QStringLiteral("nvd3d11h264enc");
		selection.selectedEncoder    = QStringLiteral("gstreamer-nvd3d11h264enc");
		selection.rawFormat          = QStringLiteral("NV12");
		selection.consumesD3D11Memory = true;
		appendCommonH264Properties(&selection.properties);
		selection.properties.append(QStringLiteral("gop-size=%1").arg(gopSize));
		selection.properties.append(QStringLiteral("bframes=0"));
		selection.properties.append(QStringLiteral("zerolatency=true"));
		return selection;
	}

	if ((plannedBackend.contains(QLatin1String("mf")) || plannedBackend.contains(QLatin1String("gstreamer")))
		&& support.gstMfH264EncoderAvailable) {
		selection.valid           = true;
		selection.encoderElement  = QStringLiteral("mfh264enc");
		selection.selectedEncoder = QStringLiteral("gstreamer-mfh264enc");
		selection.rawFormat       = QStringLiteral("NV12");
		appendCommonH264Properties(&selection.properties);
		selection.properties.append(QStringLiteral("gop-size=%1").arg(gopSize));
		selection.properties.append(QStringLiteral("bframes=0"));
		selection.properties.append(QStringLiteral("low-latency=true"));
		return selection;
	}

	if (support.gstX264EncoderAvailable) {
		selection.valid           = true;
		selection.encoderElement  = QStringLiteral("x264enc");
		selection.selectedEncoder = QStringLiteral("gstreamer-x264enc");
		selection.rawFormat       = QStringLiteral("I420");
		selection.properties.append(QStringLiteral("bitrate=%1").arg(bitrateKbps));
		selection.properties.append(QStringLiteral("speed-preset=ultrafast"));
		selection.properties.append(QStringLiteral("tune=zerolatency"));
		selection.properties.append(QStringLiteral("key-int-max=%1").arg(gopSize));
		selection.properties.append(QStringLiteral("bframes=0"));
		selection.properties.append(QStringLiteral("byte-stream=true"));
		return selection;
	}

	if (support.gstOpenH264EncoderAvailable) {
		selection.valid           = true;
		selection.encoderElement  = QStringLiteral("openh264enc");
		selection.selectedEncoder = QStringLiteral("gstreamer-openh264enc");
		selection.rawFormat       = QStringLiteral("I420");
		selection.properties.append(QStringLiteral("bitrate=%1").arg(bitrateKbps * 1000));
		selection.properties.append(QStringLiteral("gop-size=%1").arg(gopSize));
		return selection;
	}

	if (errorMessage) {
		*errorMessage = QStringLiteral("No GStreamer H.264 encoder is available.");
	}
	return selection;
}

void appendGStreamerLiveKitSignallerArguments(QStringList *arguments, const QJsonObject &plan,
											  const QString &wsUrl, const QString &identity,
											  const QString &participantName) {
	arguments->append(QStringLiteral("signaller::ws-url=%1").arg(wsUrl));
	arguments->append(QStringLiteral("signaller::auth-token=%1")
						  .arg(plan.value(QStringLiteral("relay_token")).toString().trimmed()));
	arguments->append(QStringLiteral("signaller::room-name=%1")
						  .arg(plan.value(QStringLiteral("relay_room_id")).toString().trimmed()));
	if (!identity.trimmed().isEmpty()) {
		arguments->append(QStringLiteral("signaller::identity=%1").arg(identity.trimmed()));
	}
	if (!participantName.trimmed().isEmpty()) {
		arguments->append(QStringLiteral("signaller::participant-name=%1").arg(participantName.trimmed()));
	}
}

ScreenShareExternalProcess::LaunchResult
	startGStreamerLiveKitPublish(const ScreenShareExternalProcess::RuntimeSupport &support, const QJsonObject &plan,
								 QObject *parent) {
	ScreenShareExternalProcess::LaunchResult launch;
	if (!support.gstLaunchAvailable) {
		launch.errorMessage = QStringLiteral("gst-launch-1.0 is not installed on this host.");
		return launch;
	}
	if (!support.gstreamerLiveKitPublishAvailable) {
		launch.errorMessage =
			QStringLiteral("The GStreamer LiveKit publish runtime is unavailable. Missing elements: %1")
				.arg(support.missingGStreamerElements.join(QStringLiteral(", ")));
		return launch;
	}

	QString wsUrlError;
	const QString wsUrl = liveKitWebSocketUrlFromRelayUrl(plan, &wsUrlError);
	if (wsUrl.isEmpty()) {
		launch.errorMessage = wsUrlError;
		return launch;
	}
	const QString relayToken = plan.value(QStringLiteral("relay_token")).toString().trimmed();
	if (!relayTokenLooksLikeJwt(relayToken)) {
		launch.errorMessage = QStringLiteral("GStreamer LiveKit publishing requires a LiveKit JWT relay token.");
		return launch;
	}

	QString encoderError;
	const GStreamerEncoderSelection encoder = selectGStreamerEncoder(support, plan, &encoderError);
	if (!encoder.valid) {
		launch.errorMessage = encoderError;
		return launch;
	}

	const int width  = qMax(1, plan.value(QStringLiteral("width")).toInt(1280));
	const int height = qMax(1, plan.value(QStringLiteral("height")).toInt(720));
	const int fps    = qMax(1, plan.value(QStringLiteral("fps")).toInt(30));
	const QString identity = liveKitIdentityFromToken(plan, QStringLiteral("publisher"));
	const QString participantName =
		QStringLiteral("Mumble screen share %1").arg(plan.value(QStringLiteral("stream_id")).toString());
	const bool useTestPattern = envFlagEnabled("MUMBLE_SCREENSHARE_TEST_PATTERN")
								|| qEnvironmentVariable("MUMBLE_SCREENSHARE_CAPTURE_SOURCE").trimmed().toLower()
									   == QLatin1String("test-pattern");

	QStringList arguments;
	arguments << QStringLiteral("-e");
	arguments << QStringLiteral("livekitwebrtcsink") << QStringLiteral("name=sink");
	appendGStreamerLiveKitSignallerArguments(&arguments, plan, wsUrl, identity, participantName);
	arguments << QStringLiteral("video-caps=video/x-h264");

	if (useTestPattern) {
		arguments << QStringLiteral("videotestsrc") << QStringLiteral("is-live=true")
				  << QStringLiteral("pattern=smpte") << QStringLiteral("!");
		arguments << QStringLiteral("video/x-raw,width=%1,height=%2,framerate=%3/1")
						 .arg(width)
						 .arg(height)
						 .arg(fps)
				  << QStringLiteral("!");
		arguments << QStringLiteral("videoconvert") << QStringLiteral("!") << QStringLiteral("videoscale")
				  << QStringLiteral("!");
		arguments << QStringLiteral("video/x-raw,format=%1,width=%2,height=%3,framerate=%4/1")
						 .arg(encoder.rawFormat)
						 .arg(width)
						 .arg(height)
						 .arg(fps)
				  << QStringLiteral("!");
	} else {
		arguments << QStringLiteral("d3d11screencapturesrc")
				  << QStringLiteral("capture-api=%1").arg(gstCaptureApiToken())
				  << QStringLiteral("show-cursor=true") << QStringLiteral("!");
		if (encoder.consumesD3D11Memory) {
			arguments << QStringLiteral("video/x-raw(memory:D3D11Memory),framerate=%1/1").arg(fps)
					  << QStringLiteral("!") << QStringLiteral("d3d11convert") << QStringLiteral("!");
			if (support.gstD3D11ScaleAvailable) {
				arguments << QStringLiteral("d3d11scale") << QStringLiteral("!");
			}
			arguments << QStringLiteral(
							 "video/x-raw(memory:D3D11Memory),format=%1,width=%2,height=%3,framerate=%4/1")
							 .arg(encoder.rawFormat)
							 .arg(width)
							 .arg(height)
							 .arg(fps)
					  << QStringLiteral("!");
		} else {
			arguments << QStringLiteral("d3d11download") << QStringLiteral("!") << QStringLiteral("videoconvert")
					  << QStringLiteral("!") << QStringLiteral("videoscale") << QStringLiteral("!");
			arguments << QStringLiteral("video/x-raw,format=%1,width=%2,height=%3,framerate=%4/1")
							 .arg(encoder.rawFormat)
							 .arg(width)
							 .arg(height)
							 .arg(fps)
					  << QStringLiteral("!");
		}
	}

	arguments << encoder.encoderElement;
	arguments.append(encoder.properties);
	arguments << QStringLiteral("!") << QStringLiteral("h264parse") << QStringLiteral("config-interval=-1")
			  << QStringLiteral("!") << QStringLiteral("queue") << QStringLiteral("leaky=downstream")
			  << QStringLiteral("max-size-buffers=2") << QStringLiteral("!") << QStringLiteral("sink.");

	launch = startProcess(support.gstLaunchPath, arguments, parent, QStringLiteral("gstreamer-livekit-publish"));
	if (!launch.started) {
		return launch;
	}

	launch.endpointUrl           = wsUrl;
	launch.selectedEncoder       = encoder.selectedEncoder;
	launch.selectedCaptureSource = useTestPattern ? QStringLiteral("gstreamer-test-pattern")
												   : QStringLiteral("gstreamer-d3d11-%1").arg(gstCaptureApiToken());
	return launch;
}

ScreenShareExternalProcess::LaunchResult
	startGStreamerLiveKitView(const ScreenShareExternalProcess::RuntimeSupport &support, const QJsonObject &plan,
							  QObject *parent) {
	ScreenShareExternalProcess::LaunchResult launch;
	if (!support.gstLaunchAvailable) {
		launch.errorMessage = QStringLiteral("gst-launch-1.0 is not installed on this host.");
		return launch;
	}
	if (!support.gstreamerLiveKitViewAvailable) {
		launch.errorMessage =
			QStringLiteral("The GStreamer LiveKit viewer runtime is unavailable. Missing elements: %1")
				.arg(support.missingGStreamerElements.join(QStringLiteral(", ")));
		return launch;
	}

	QString wsUrlError;
	const QString wsUrl = liveKitWebSocketUrlFromRelayUrl(plan, &wsUrlError);
	if (wsUrl.isEmpty()) {
		launch.errorMessage = wsUrlError;
		return launch;
	}
	const QString relayToken = plan.value(QStringLiteral("relay_token")).toString().trimmed();
	if (!relayTokenLooksLikeJwt(relayToken)) {
		launch.errorMessage = QStringLiteral("GStreamer LiveKit viewing requires a LiveKit JWT relay token.");
		return launch;
	}

	const QString identity = liveKitIdentityFromToken(plan, QStringLiteral("viewer"));
	const QString participantName =
		QStringLiteral("Mumble viewer %1").arg(plan.value(QStringLiteral("stream_id")).toString());
	const QString publisherIdentity = liveKitPublisherIdentityFromPlan(plan);

	QStringList arguments;
	arguments << QStringLiteral("-e");
	arguments << QStringLiteral("livekitwebrtcsrc") << QStringLiteral("name=src");
	appendGStreamerLiveKitSignallerArguments(&arguments, plan, wsUrl, identity, participantName);
	if (!publisherIdentity.isEmpty()) {
		arguments << QStringLiteral("signaller::producer-peer-id=%1").arg(publisherIdentity);
	}
	arguments << QStringLiteral("video-codecs=<H264>");
	arguments << QStringLiteral("src.") << QStringLiteral("!") << QStringLiteral("queue")
			  << QStringLiteral("leaky=downstream") << QStringLiteral("max-size-buffers=4") << QStringLiteral("!")
			  << QStringLiteral("decodebin") << QStringLiteral("!");
	if (support.gstD3D11VideoSinkAvailable) {
		arguments << QStringLiteral("d3d11videosink") << QStringLiteral("sync=false");
	} else if (support.gstAutoVideoSinkAvailable) {
		arguments << QStringLiteral("videoconvert") << QStringLiteral("!") << QStringLiteral("autovideosink")
				  << QStringLiteral("sync=false");
	} else {
		arguments << QStringLiteral("fakesink") << QStringLiteral("sync=false");
	}

	launch = startProcess(support.gstLaunchPath, arguments, parent, QStringLiteral("gstreamer-livekit-view"));
	if (!launch.started) {
		return launch;
	}

	launch.endpointUrl       = wsUrl;
	launch.selectedRenderer = support.gstD3D11VideoSinkAvailable ? QStringLiteral("gstreamer-d3d11videosink")
																 : (support.gstAutoVideoSinkAvailable
																		? QStringLiteral("gstreamer-autovideosink")
																		: QStringLiteral("gstreamer-fakesink"));
	return launch;
}

ScreenShareExternalProcess::LaunchResult startProcess(const QString &program, const QStringList &arguments,
													  QObject *parent, const QString &executionMode) {
	ScreenShareExternalProcess::LaunchResult launch;
	launch.program       = program;
	launch.executionMode = executionMode;

	QProcess *process = new QProcess(parent);
	process->setProcessChannelMode(QProcess::MergedChannels);
	process->setProcessEnvironment(processEnvironmentForExternalProgram(program));
	process->start(program, arguments);
	if (!process->waitForStarted(START_TIMEOUT_MSEC)) {
		launch.errorMessage = process->errorString();
		process->deleteLater();
		return launch;
	}

	if (process->waitForFinished(START_SETTLE_MSEC)) {
		const QString output = readMergedProcessOutput(*process).trimmed();
		launch.errorMessage =
			output.isEmpty() ? QStringLiteral("The external media process exited immediately.") : output;
		process->deleteLater();
		return launch;
	}

	launch.started = true;
	launch.process = process;
	return launch;
}
} // namespace

ScreenShareExternalProcess::RuntimeSupport ScreenShareExternalProcess::probeRuntimeSupport(const bool refresh) {
	static std::optional< RuntimeSupport > cachedSupport;
	if (!refresh && cachedSupport.has_value()) {
		return *cachedSupport;
	}

	RuntimeSupport support;
	support.ffmpegPath = preferredExecutablePath("MUMBLE_SCREENSHARE_FFMPEG_PATH",
												 QStringList{ QStringLiteral("ffmpeg"), QStringLiteral("ffmpeg.exe") });
	support.ffplayPath = preferredExecutablePath("MUMBLE_SCREENSHARE_FFPLAY_PATH",
												 QStringList{ QStringLiteral("ffplay"), QStringLiteral("ffplay.exe") });
	const QStringList bundledGStreamerDirs = bundledGStreamerExecutableDirectories();
	support.gstLaunchPath =
		preferredExecutablePath("MUMBLE_SCREENSHARE_GST_LAUNCH_PATH",
								QStringList{ QStringLiteral("gst-launch-1.0"), QStringLiteral("gst-launch-1.0.exe") },
								bundledGStreamerDirs);
	support.gstInspectPath =
		preferredExecutablePath("MUMBLE_SCREENSHARE_GST_INSPECT_PATH",
								QStringList{ QStringLiteral("gst-inspect-1.0"),
											 QStringLiteral("gst-inspect-1.0.exe") },
								bundledGStreamerDirs);
#ifdef Q_OS_WIN
	support.edgePath = findExecutableAny(QStringList{ QStringLiteral("msedge.exe") });
	if (support.edgePath.isEmpty()) {
		support.edgePath =
			existingWindowsBrowserPath(QStringList{ QStringLiteral("Microsoft/Edge/Application/msedge.exe"),
													QStringLiteral("Microsoft/Edge Beta/Application/msedge.exe") });
	}
	support.chromePath = findExecutableAny(QStringList{ QStringLiteral("chrome.exe") });
	if (support.chromePath.isEmpty()) {
		support.chromePath =
			existingWindowsBrowserPath(QStringList{ QStringLiteral("Google/Chrome/Application/chrome.exe"),
													QStringLiteral("Chromium/Application/chrome.exe") });
	}
	support.firefoxPath = findExecutableAny(QStringList{ QStringLiteral("firefox.exe") });
	if (support.firefoxPath.isEmpty()) {
		support.firefoxPath = existingWindowsBrowserPath(QStringList{ QStringLiteral("Mozilla Firefox/firefox.exe") });
	}
#else
	support.edgePath =
		findExecutableAny(QStringList{ QStringLiteral("microsoft-edge"), QStringLiteral("microsoft-edge-stable") });
	support.chromePath =
		findExecutableAny(QStringList{ QStringLiteral("google-chrome"), QStringLiteral("google-chrome-stable"),
									   QStringLiteral("chromium"), QStringLiteral("chromium-browser") });
	support.firefoxPath               = findExecutableAny(QStringList{ QStringLiteral("firefox") });
#endif
	support.ffmpegAvailable  = !support.ffmpegPath.isEmpty();
	support.ffplayAvailable  = !support.ffplayPath.isEmpty();
	support.gstLaunchAvailable  = !support.gstLaunchPath.isEmpty();
	support.gstInspectAvailable = !support.gstInspectPath.isEmpty();
	support.gstreamerAvailable  = support.gstLaunchAvailable && support.gstInspectAvailable;
	support.edgeAvailable    = !support.edgePath.isEmpty();
	support.chromeAvailable  = !support.chromePath.isEmpty();
	support.firefoxAvailable = !support.firefoxPath.isEmpty();
#ifdef Q_OS_WIN
	support.graphicalSessionAvailable = true;
#else
	support.graphicalSessionAvailable = !qEnvironmentVariable("DISPLAY").trimmed().isEmpty()
										|| !qEnvironmentVariable("WAYLAND_DISPLAY").trimmed().isEmpty();
#endif
	support.browserWebRtcAvailable = support.graphicalSessionAvailable
									 && (support.edgeAvailable || support.chromeAvailable || support.firefoxAvailable);
	support.x11DisplayAvailable     = !qEnvironmentVariable("DISPLAY").trimmed().isEmpty();
	support.windowedViewerAvailable = hasWindowedViewerSurface();
#ifdef Q_OS_WIN
	const ScreenShareWindowsNativeCapture::Capability nativeCapture = ScreenShareWindowsNativeCapture::probe();
	support.d3d11HardwareDeviceAvailable = nativeCapture.d3d11HardwareDeviceAvailable;
	support.windowsGraphicsCaptureAvailable = nativeCapture.graphicsCaptureSupported;
	support.windowsGraphicsCaptureFreeThreaded = nativeCapture.freeThreadedFramePoolSupported;
	support.windowsGraphicsCaptureDirtyRegions = nativeCapture.dirtyRegionMetadataSupported;
	support.windowsNativeCapturePipelineAvailable = nativeCapture.inProcessCapturePipelinePlanned;
#endif

	if (support.ffmpegAvailable) {
		const QString encoders =
			runProbeCommand(support.ffmpegPath, { QStringLiteral("-hide_banner"), QStringLiteral("-encoders") });
		const QString devices =
			runProbeCommand(support.ffmpegPath, { QStringLiteral("-hide_banner"), QStringLiteral("-devices") });
		const QString filters =
			runProbeCommand(support.ffmpegPath, { QStringLiteral("-hide_banner"), QStringLiteral("-filters") });
		const QString protocols =
			runProbeCommand(support.ffmpegPath, { QStringLiteral("-hide_banner"), QStringLiteral("-protocols") });

		support.gdigrabAvailable = devices.contains(QLatin1String("gdigrab"));
		support.x11GrabAvailable = devices.contains(QLatin1String("x11grab"));
		support.ddagrabAvailable = filters.contains(QLatin1String("ddagrab"));
		support.lavfiAvailable   = devices.contains(QLatin1String("lavfi"));
#ifdef Q_OS_LINUX
		const bool nvidiaDeviceAvailable = anyNvidiaDevicePresent();
#else
		const bool nvidiaDeviceAvailable = true;
#endif

		support.h264NvencAvailable     = encoders.contains(QLatin1String("h264_nvenc")) && nvidiaDeviceAvailable;
		support.h264VaapiAvailable     = encoders.contains(QLatin1String("h264_vaapi"));
		support.h264MfAvailable        = encoders.contains(QLatin1String("h264_mf"));
		support.h264QsvAvailable       = encoders.contains(QLatin1String("h264_qsv"));
		support.libx264Available       = encoders.contains(QLatin1String("libx264"));
		support.av1NvencAvailable      = encoders.contains(QLatin1String("av1_nvenc")) && nvidiaDeviceAvailable;
		support.av1VaapiAvailable      = encoders.contains(QLatin1String("av1_vaapi"));
		support.av1MfAvailable         = encoders.contains(QLatin1String("av1_mf"));
		support.av1QsvAvailable        = encoders.contains(QLatin1String("av1_qsv"));
		support.libSvtAv1Available     = encoders.contains(QLatin1String("libsvtav1"));
		support.libVpxVp8Available     = encoders.contains(QLatin1String("libvpx"));
		support.libVpxVp9Available     = encoders.contains(QLatin1String("libvpx-vp9"));
		support.fileProtocolAvailable  = protocols.contains(QLatin1String("file"));
		support.rtmpProtocolAvailable  = protocols.contains(QLatin1String("rtmp"));
		support.rtmpsProtocolAvailable = protocols.contains(QLatin1String("rtmps"));
	}

	if (support.gstreamerAvailable) {
		const QString versionOutput = runProbeCommand(support.gstLaunchPath, { QStringLiteral("--version") });
		const QStringList versionLines =
			versionOutput.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
		support.gstreamerVersion = versionLines.isEmpty() ? QString() : versionLines.first().trimmed();

		support.gstLiveKitWebRtcSinkAvailable = gstElementAvailable(support.gstInspectPath, QStringLiteral("livekitwebrtcsink"));
		support.gstLiveKitWebRtcSrcAvailable  = gstElementAvailable(support.gstInspectPath, QStringLiteral("livekitwebrtcsrc"));
		support.gstD3D11ScreenCaptureAvailable =
			gstElementAvailable(support.gstInspectPath, QStringLiteral("d3d11screencapturesrc"));
		support.gstD3D11ConvertAvailable = gstElementAvailable(support.gstInspectPath, QStringLiteral("d3d11convert"));
		support.gstD3D11ScaleAvailable   = gstElementAvailable(support.gstInspectPath, QStringLiteral("d3d11scale"));
		support.gstD3D11DownloadAvailable =
			gstElementAvailable(support.gstInspectPath, QStringLiteral("d3d11download"));
		support.gstNvD3D11H264EncoderAvailable =
			gstElementAvailable(support.gstInspectPath, QStringLiteral("nvd3d11h264enc"));
		support.gstMfH264EncoderAvailable =
			gstElementAvailable(support.gstInspectPath, QStringLiteral("mfh264enc"));
		support.gstX264EncoderAvailable = gstElementAvailable(support.gstInspectPath, QStringLiteral("x264enc"));
		support.gstOpenH264EncoderAvailable =
			gstElementAvailable(support.gstInspectPath, QStringLiteral("openh264enc"));
		support.gstH264ParseAvailable = gstElementAvailable(support.gstInspectPath, QStringLiteral("h264parse"));
		support.gstVideoTestSrcAvailable =
			gstElementAvailable(support.gstInspectPath, QStringLiteral("videotestsrc"));
		support.gstVideoConvertAvailable =
			gstElementAvailable(support.gstInspectPath, QStringLiteral("videoconvert"));
		support.gstVideoScaleAvailable =
			gstElementAvailable(support.gstInspectPath, QStringLiteral("videoscale"));
		support.gstDecodeBinAvailable = gstElementAvailable(support.gstInspectPath, QStringLiteral("decodebin"));
		support.gstAutoVideoSinkAvailable =
			gstElementAvailable(support.gstInspectPath, QStringLiteral("autovideosink"));
		support.gstD3D11VideoSinkAvailable =
			gstElementAvailable(support.gstInspectPath, QStringLiteral("d3d11videosink"));
		support.gstFakeSinkAvailable = gstElementAvailable(support.gstInspectPath, QStringLiteral("fakesink"));

		const bool gstSystemMemoryH264Available =
			support.gstD3D11DownloadAvailable && support.gstVideoConvertAvailable
			&& (support.gstMfH264EncoderAvailable || support.gstX264EncoderAvailable
				|| support.gstOpenH264EncoderAvailable);
		const bool gstH264EncoderAvailable =
			support.gstNvD3D11H264EncoderAvailable || gstSystemMemoryH264Available;
		const bool gstDesktopCaptureAvailable =
#ifdef Q_OS_WIN
			support.gstD3D11ScreenCaptureAvailable && support.gstD3D11ConvertAvailable
				&& support.gstD3D11ScaleAvailable;
#else
			support.gstVideoTestSrcAvailable && envFlagEnabled("MUMBLE_SCREENSHARE_TEST_PATTERN");
#endif
		const bool gstViewerSinkAvailable =
			support.gstD3D11VideoSinkAvailable || support.gstAutoVideoSinkAvailable || support.gstFakeSinkAvailable;
		support.gstreamerLiveKitPublishAvailable =
			support.gstLiveKitWebRtcSinkAvailable && support.gstH264ParseAvailable && gstH264EncoderAvailable
			&& gstDesktopCaptureAvailable;
		support.gstreamerLiveKitViewAvailable =
			support.gstLiveKitWebRtcSrcAvailable && support.gstDecodeBinAvailable && gstViewerSinkAvailable;

		appendMissingGStreamerElement(&support, QStringLiteral("livekitwebrtcsink"),
									  support.gstLiveKitWebRtcSinkAvailable);
		appendMissingGStreamerElement(&support, QStringLiteral("livekitwebrtcsrc"),
									  support.gstLiveKitWebRtcSrcAvailable);
		appendMissingGStreamerElement(&support, QStringLiteral("h264parse"), support.gstH264ParseAvailable);
#ifdef Q_OS_WIN
		appendMissingGStreamerElement(&support, QStringLiteral("d3d11screencapturesrc"),
									  support.gstD3D11ScreenCaptureAvailable);
		appendMissingGStreamerElement(&support, QStringLiteral("d3d11convert"),
									  support.gstD3D11ConvertAvailable);
		appendMissingGStreamerElement(&support, QStringLiteral("d3d11scale"), support.gstD3D11ScaleAvailable);
		if (!support.gstNvD3D11H264EncoderAvailable && !support.gstD3D11DownloadAvailable) {
			appendMissingGStreamerElement(&support, QStringLiteral("d3d11download"), false);
		}
#else
		appendMissingGStreamerElement(&support, QStringLiteral("videotestsrc"),
									  support.gstVideoTestSrcAvailable
										  || !envFlagEnabled("MUMBLE_SCREENSHARE_TEST_PATTERN"));
#endif
		if (!gstH264EncoderAvailable) {
			appendMissingGStreamerElement(&support, QStringLiteral("nvd3d11h264enc|mfh264enc|x264enc|openh264enc"),
										  false);
		}
		appendMissingGStreamerElement(&support, QStringLiteral("decodebin"), support.gstDecodeBinAvailable);
		if (!gstViewerSinkAvailable) {
			appendMissingGStreamerElement(&support, QStringLiteral("d3d11videosink|autovideosink|fakesink"), false);
		}
	}

	cachedSupport = support;
	return support;
}

QJsonObject ScreenShareExternalProcess::runtimeSupportToJson(const RuntimeSupport &support) {
	QJsonObject payload;
	payload.insert(QStringLiteral("ffmpeg_available"), support.ffmpegAvailable);
	payload.insert(QStringLiteral("ffplay_available"), support.ffplayAvailable);
	payload.insert(QStringLiteral("gst_launch_available"), support.gstLaunchAvailable);
	payload.insert(QStringLiteral("gst_inspect_available"), support.gstInspectAvailable);
	payload.insert(QStringLiteral("gstreamer_available"), support.gstreamerAvailable);
	payload.insert(QStringLiteral("gstreamer_version"), support.gstreamerVersion);
	payload.insert(QStringLiteral("gstreamer_livekit_publish_available"),
				   support.gstreamerLiveKitPublishAvailable);
	payload.insert(QStringLiteral("gstreamer_livekit_view_available"), support.gstreamerLiveKitViewAvailable);
	payload.insert(QStringLiteral("gst_livekitwebrtcsink_available"),
				   support.gstLiveKitWebRtcSinkAvailable);
	payload.insert(QStringLiteral("gst_livekitwebrtcsrc_available"), support.gstLiveKitWebRtcSrcAvailable);
	payload.insert(QStringLiteral("gst_d3d11screencapturesrc_available"),
				   support.gstD3D11ScreenCaptureAvailable);
	payload.insert(QStringLiteral("gst_d3d11convert_available"), support.gstD3D11ConvertAvailable);
	payload.insert(QStringLiteral("gst_d3d11scale_available"), support.gstD3D11ScaleAvailable);
	payload.insert(QStringLiteral("gst_d3d11download_available"), support.gstD3D11DownloadAvailable);
	payload.insert(QStringLiteral("gst_nvd3d11h264enc_available"),
				   support.gstNvD3D11H264EncoderAvailable);
	payload.insert(QStringLiteral("gst_mfh264enc_available"), support.gstMfH264EncoderAvailable);
	payload.insert(QStringLiteral("gst_x264enc_available"), support.gstX264EncoderAvailable);
	payload.insert(QStringLiteral("gst_openh264enc_available"), support.gstOpenH264EncoderAvailable);
	payload.insert(QStringLiteral("gst_h264parse_available"), support.gstH264ParseAvailable);
	payload.insert(QStringLiteral("gst_videotestsrc_available"), support.gstVideoTestSrcAvailable);
	payload.insert(QStringLiteral("gst_videoconvert_available"), support.gstVideoConvertAvailable);
	payload.insert(QStringLiteral("gst_videoscale_available"), support.gstVideoScaleAvailable);
	payload.insert(QStringLiteral("gst_decodebin_available"), support.gstDecodeBinAvailable);
	payload.insert(QStringLiteral("gst_autovideosink_available"), support.gstAutoVideoSinkAvailable);
	payload.insert(QStringLiteral("gst_d3d11videosink_available"), support.gstD3D11VideoSinkAvailable);
	payload.insert(QStringLiteral("gst_fakesink_available"), support.gstFakeSinkAvailable);
	{
		QJsonArray missingElements;
		for (const QString &element : support.missingGStreamerElements) {
			missingElements.push_back(element);
		}
		payload.insert(QStringLiteral("gstreamer_missing_elements"), missingElements);
	}
	payload.insert(QStringLiteral("browser_webrtc_available"), support.browserWebRtcAvailable);
	payload.insert(QStringLiteral("edge_available"), support.edgeAvailable);
	payload.insert(QStringLiteral("chrome_available"), support.chromeAvailable);
	payload.insert(QStringLiteral("firefox_available"), support.firefoxAvailable);
	payload.insert(QStringLiteral("graphical_session_available"), support.graphicalSessionAvailable);
	payload.insert(QStringLiteral("x11_display_available"), support.x11DisplayAvailable);
	payload.insert(QStringLiteral("x11grab_available"), support.x11GrabAvailable);
	payload.insert(QStringLiteral("gdigrab_available"), support.gdigrabAvailable);
	payload.insert(QStringLiteral("ddagrab_available"), support.ddagrabAvailable);
	payload.insert(QStringLiteral("lavfi_available"), support.lavfiAvailable);
	payload.insert(QStringLiteral("windowed_viewer_available"), support.windowedViewerAvailable);
	payload.insert(QStringLiteral("d3d11_hardware_device_available"), support.d3d11HardwareDeviceAvailable);
	payload.insert(QStringLiteral("windows_graphics_capture_available"), support.windowsGraphicsCaptureAvailable);
	payload.insert(QStringLiteral("windows_graphics_capture_free_threaded"),
				   support.windowsGraphicsCaptureFreeThreaded);
	payload.insert(QStringLiteral("windows_graphics_capture_dirty_regions"),
				   support.windowsGraphicsCaptureDirtyRegions);
	payload.insert(QStringLiteral("windows_native_capture_pipeline_available"),
				   support.windowsNativeCapturePipelineAvailable);
	payload.insert(QStringLiteral("h264_nvenc_available"), support.h264NvencAvailable);
	payload.insert(QStringLiteral("h264_vaapi_available"), support.h264VaapiAvailable);
	payload.insert(QStringLiteral("h264_mf_available"), support.h264MfAvailable);
	payload.insert(QStringLiteral("h264_qsv_available"), support.h264QsvAvailable);
	payload.insert(QStringLiteral("libx264_available"), support.libx264Available);
	payload.insert(QStringLiteral("av1_nvenc_available"), support.av1NvencAvailable);
	payload.insert(QStringLiteral("av1_vaapi_available"), support.av1VaapiAvailable);
	payload.insert(QStringLiteral("av1_mf_available"), support.av1MfAvailable);
	payload.insert(QStringLiteral("av1_qsv_available"), support.av1QsvAvailable);
	payload.insert(QStringLiteral("libsvtav1_available"), support.libSvtAv1Available);
	payload.insert(QStringLiteral("libvpx_vp8_available"), support.libVpxVp8Available);
	payload.insert(QStringLiteral("libvpx_vp9_available"), support.libVpxVp9Available);
	payload.insert(QStringLiteral("file_protocol_available"), support.fileProtocolAvailable);
	payload.insert(QStringLiteral("rtmp_protocol_available"), support.rtmpProtocolAvailable);
	payload.insert(QStringLiteral("rtmps_protocol_available"), support.rtmpsProtocolAvailable);

	QJsonArray publishSchemes;
	if (support.fileProtocolAvailable) {
		publishSchemes.push_back(QStringLiteral("file"));
	}
	if (support.rtmpProtocolAvailable) {
		publishSchemes.push_back(QStringLiteral("rtmp"));
	}
	if (support.rtmpsProtocolAvailable) {
		publishSchemes.push_back(QStringLiteral("rtmps"));
	}
	if (support.gstreamerLiveKitPublishAvailable || support.gstreamerLiveKitViewAvailable) {
		publishSchemes.push_back(QStringLiteral("http"));
		publishSchemes.push_back(QStringLiteral("https"));
		publishSchemes.push_back(QStringLiteral("ws"));
		publishSchemes.push_back(QStringLiteral("wss"));
	}
	payload.insert(QStringLiteral("publish_relay_schemes"), publishSchemes);
	payload.insert(QStringLiteral("view_relay_schemes"), publishSchemes);
	return payload;
}

ScreenShareExternalProcess::LaunchResult ScreenShareExternalProcess::startPublish(const QJsonObject &plan,
																				  QObject *parent) {
	LaunchResult launch;
	const RuntimeSupport support = probeRuntimeSupport();
	if (plan.value(QStringLiteral("relay_contract_mode")).toString() == QLatin1String("gstreamer-livekit-runtime")) {
		return startGStreamerLiveKitPublish(support, plan, parent);
	}
	if (plan.value(QStringLiteral("relay_contract_mode")).toString() == QLatin1String("browser-webrtc-runtime")) {
		return startBrowserWebRtcSession(support, plan, parent, true);
	}
	if (!plan.value(QStringLiteral("relay_runtime_executable")).toBool(true)) {
		launch.errorMessage =
			plan.value(QStringLiteral("relay_contract_description"))
				.toString(QStringLiteral("The negotiated relay transport is not executable in this helper build."));
		if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
			launch.started       = true;
			launch.usedStub      = true;
			launch.executionMode = QStringLiteral("stub");
			launch.warnings.append(launch.errorMessage);
		}
		return launch;
	}
	if (!support.ffmpegAvailable) {
		launch.errorMessage = QStringLiteral("ffmpeg is not installed on this host.");
		if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
			launch.started       = true;
			launch.usedStub      = true;
			launch.executionMode = QStringLiteral("stub");
			launch.warnings.append(QStringLiteral(
				"ffmpeg is unavailable; using helper stub mode because MUMBLE_SCREENSHARE_ALLOW_STUB=1."));
		}
		return launch;
	}

	const RelayEndpoint endpoint = materializeRelayEndpoint(plan, support);
	if (!endpoint.valid) {
		launch.errorMessage = endpoint.errorMessage;
		if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
			launch.started       = true;
			launch.usedStub      = true;
			launch.executionMode = QStringLiteral("stub");
			launch.warnings.append(endpoint.errorMessage);
		}
		return launch;
	}

	const int width  = qMax(1, plan.value(QStringLiteral("width")).toInt());
	const int height = qMax(1, plan.value(QStringLiteral("height")).toInt());
	const int fps    = qMax(1, plan.value(QStringLiteral("fps")).toInt());

	const MumbleProto::ScreenShareCodec codec =
		Mumble::ScreenShare::IPC::codecFromJson(plan.value(QStringLiteral("codec")));
	const QString plannedBackend        = plan.value(QStringLiteral("planned_encoder_backend")).toString();
	const QStringList candidateBackends = candidateBackendOrder(support, codec, plannedBackend);

	QString lastError;
	for (const QString &candidateBackend : candidateBackends) {
		QJsonObject attemptPlan = plan;
		attemptPlan.insert(QStringLiteral("planned_encoder_backend"), candidateBackend);

		QString captureSource;
		QString captureError;
		QStringList inputArguments{ QString::number(width), QString::number(height), QString::number(fps) };
		if (!selectCaptureSource(support, &captureSource, &captureError, &inputArguments, candidateBackend)) {
			lastError = captureError;
			continue;
		}

		QStringList arguments;
		arguments.append(QStringLiteral("-hide_banner"));
		arguments.append(QStringLiteral("-loglevel"));
		arguments.append(QStringLiteral("warning"));
		arguments.append(QStringLiteral("-nostdin"));
		arguments.append(QStringLiteral("-y"));
		arguments.append(inputArguments);
		arguments.append(QStringLiteral("-an"));

		QString selectedEncoder;
		QStringList attemptWarnings = launch.warnings;
		QString encoderError;
		if (!appendEncoderArguments(support, attemptPlan, &selectedEncoder, &attemptWarnings, &arguments,
									&encoderError, captureSource)) {
			lastError = encoderError;
			continue;
		}

		arguments.append(QStringLiteral("-f"));
		arguments.append(endpoint.outputFormat);
		arguments.append(endpoint.localFilePath.isEmpty() ? endpoint.endpointUrl : endpoint.localFilePath);

		LaunchResult attemptLaunch =
			startProcess(support.ffmpegPath, arguments, parent, QStringLiteral("ffmpeg-publish"));
		if (!attemptLaunch.started) {
			lastError = attemptLaunch.errorMessage;
			if (candidateBackend != candidateBackends.back()) {
				launch.warnings.append(
					QStringLiteral("Encoder backend %1 failed to start; trying the next available backend.")
						.arg(candidateBackend));
			}
			continue;
		}

		launch                       = attemptLaunch;
		launch.endpointUrl           = endpoint.endpointUrl;
		launch.selectedEncoder       = selectedEncoder;
		launch.selectedCaptureSource = captureSource;
		launch.warnings              = attemptWarnings;
		if (candidateBackend.trimmed().toLower() != plannedBackend.trimmed().toLower()) {
			launch.warnings.append(
				QStringLiteral("Fell back from %1 to %2 during helper startup.").arg(plannedBackend, candidateBackend));
		}
		return launch;
	}

	launch.errorMessage =
		lastError.isEmpty() ? QStringLiteral("No external ffmpeg encoder backend could be started.") : lastError;
	if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
		launch.started       = true;
		launch.usedStub      = true;
		launch.executionMode = QStringLiteral("stub");
		launch.warnings.append(launch.errorMessage);
	}
	return launch;
}

ScreenShareExternalProcess::LaunchResult ScreenShareExternalProcess::startView(const QJsonObject &plan,
																			   QObject *parent) {
	LaunchResult launch;
	const RuntimeSupport support = probeRuntimeSupport();
	if (plan.value(QStringLiteral("relay_contract_mode")).toString() == QLatin1String("gstreamer-livekit-runtime")) {
		return startGStreamerLiveKitView(support, plan, parent);
	}
	if (plan.value(QStringLiteral("relay_contract_mode")).toString() == QLatin1String("browser-webrtc-runtime")) {
		return startBrowserWebRtcSession(support, plan, parent, false);
	}
	if (!plan.value(QStringLiteral("relay_runtime_executable")).toBool(true)) {
		launch.errorMessage =
			plan.value(QStringLiteral("relay_contract_description"))
				.toString(QStringLiteral("The negotiated relay transport is not executable in this helper build."));
		if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
			launch.started       = true;
			launch.usedStub      = true;
			launch.executionMode = QStringLiteral("stub");
			launch.warnings.append(launch.errorMessage);
		}
		return launch;
	}
	const bool headlessView = !support.graphicalSessionAvailable || envFlagEnabled("MUMBLE_SCREENSHARE_HEADLESS_VIEW");
	if (headlessView ? !support.ffmpegAvailable : !support.ffplayAvailable) {
		launch.errorMessage = headlessView
								  ? QStringLiteral("ffmpeg is not installed on this host for headless viewer mode.")
								  : QStringLiteral("ffplay is not installed on this host.");
		if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
			launch.started       = true;
			launch.usedStub      = true;
			launch.executionMode = QStringLiteral("stub");
			launch.warnings.append(
				headlessView
					? QStringLiteral("ffmpeg is unavailable for headless viewer mode; using helper stub mode because "
									 "MUMBLE_SCREENSHARE_ALLOW_STUB=1.")
					: QStringLiteral(
						"ffplay is unavailable; using helper stub mode because MUMBLE_SCREENSHARE_ALLOW_STUB=1."));
		}
		return launch;
	}

	const RelayEndpoint endpoint = materializeRelayEndpoint(plan, support);
	if (!endpoint.valid) {
		launch.errorMessage = endpoint.errorMessage;
		if (envFlagEnabled("MUMBLE_SCREENSHARE_ALLOW_STUB")) {
			launch.started       = true;
			launch.usedStub      = true;
			launch.executionMode = QStringLiteral("stub");
			launch.warnings.append(endpoint.errorMessage);
		}
		return launch;
	}

	QStringList arguments;
	const QString inputLocation = endpoint.localFilePath.isEmpty() ? endpoint.endpointUrl : endpoint.localFilePath;
	const QString program       = headlessView ? support.ffmpegPath : support.ffplayPath;
	const QString executionMode = headlessView ? QStringLiteral("ffmpeg-view") : QStringLiteral("ffplay-view");

	if (headlessView) {
		arguments.append(QStringLiteral("-hide_banner"));
		arguments.append(QStringLiteral("-loglevel"));
		arguments.append(QStringLiteral("warning"));
		arguments.append(QStringLiteral("-nostdin"));
		if (!endpoint.localFilePath.isEmpty()) {
			arguments.append(QStringLiteral("-re"));
		}
		arguments.append(QStringLiteral("-i"));
		arguments.append(inputLocation);
		arguments.append(QStringLiteral("-an"));
		arguments.append(QStringLiteral("-f"));
		arguments.append(QStringLiteral("null"));
		arguments.append(QStringLiteral("-"));
	} else {
		arguments.append(QStringLiteral("-hide_banner"));
		arguments.append(QStringLiteral("-loglevel"));
		arguments.append(QStringLiteral("warning"));
		arguments.append(QStringLiteral("-fflags"));
		arguments.append(QStringLiteral("nobuffer"));
		arguments.append(QStringLiteral("-flags"));
		arguments.append(QStringLiteral("low_delay"));
		arguments.append(QStringLiteral("-framedrop"));
		arguments.append(inputLocation);
	}

	launch = startProcess(program, arguments, parent, executionMode);
	if (!launch.started) {
		return launch;
	}

	launch.endpointUrl = endpoint.localFilePath.isEmpty() ? endpoint.endpointUrl
														  : QUrl::fromLocalFile(endpoint.localFilePath).toString();
	launch.selectedRenderer = headlessView ? QStringLiteral("ffmpeg-null-view") : QStringLiteral("ffplay");
	return launch;
}

void ScreenShareExternalProcess::stop(QProcess *process, const int timeoutMsec) {
	if (!process) {
		return;
	}

	if (process->state() == QProcess::NotRunning) {
		process->deleteLater();
		return;
	}

	process->terminate();
	if (!process->waitForFinished(timeoutMsec)) {
		process->kill();
		process->waitForFinished(500);
	}
	process->deleteLater();
}
