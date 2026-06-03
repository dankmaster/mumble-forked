// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareManager.h"

#include "ClientUser.h"
#include "ExternalScreenShareWindowHost.h"
#include "Global.h"
#include "Log.h"
#ifdef Q_OS_WIN
#	include "win.h"
#endif
#include "ProtoUtils.h"
#include "QtUtils.h"
#include "ScreenShare.h"
#include "ServerHandler.h"

#include <algorithm>
#include <limits>
#include <optional>

#include <QtCore/QDateTime>
#include <QtCore/QProcessEnvironment>
#include <QtCore/QStringList>

namespace {
constexpr int kExternalRuntimeWatchdogIntervalMsec = 3000;
constexpr int kExternalRuntimeRestartLimit         = 2;

bool envFlagEnabled(const char *name) {
	const QString value = qEnvironmentVariable(name).trimmed().toLower();
	return value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
		   || value == QLatin1String("on");
}

std::optional< bool > envFlagOverride(const char *name) {
	if (!qEnvironmentVariableIsSet(name)) {
		return std::nullopt;
	}

	const QString value = qEnvironmentVariable(name).trimmed().toLower();
	if (value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
		|| value == QLatin1String("on")) {
		return true;
	}
	if (value == QLatin1String("0") || value == QLatin1String("false") || value == QLatin1String("no")
		|| value == QLatin1String("off")) {
		return false;
	}

	return envFlagEnabled(name);
}

QList< int > codecFallbackOrderFromState(const MumbleProto::ScreenShareState &msg) {
	QList< int > codecs;
	codecs.reserve(msg.codec_fallback_order_size());
	for (int i = 0; i < msg.codec_fallback_order_size(); ++i) {
		codecs.append(static_cast< int >(msg.codec_fallback_order(i)));
	}

	return Mumble::ScreenShare::sanitizeCodecList(codecs);
}

bool tokenExpired(const quint64 expiresAt, const qint64 skewMsec = 5000) {
	if (expiresAt == 0) {
		return false;
	}

	return static_cast< quint64 >(QDateTime::currentMSecsSinceEpoch() + skewMsec) >= expiresAt;
}

bool isWebRtcRelayTransport(const MumbleProto::ScreenShareRelayTransport transport) {
	return Mumble::ScreenShare::isWebRtcRelayTransport(transport);
}

bool helperRuntimeSupportsRelayTransport(const ScreenShareHelperClient::CapabilitySnapshot &capabilities,
										 const MumbleProto::ScreenShareRelayTransport transport) {
	return transport != MumbleProto::ScreenShareRelayTransportUnknown
		   && capabilities.runtimeRelayTransports.contains(static_cast< int >(transport));
}

bool helperRuntimeSupportsPublishing(const ScreenShareHelperClient::CapabilitySnapshot &capabilities,
									 const MumbleProto::ScreenShareRelayTransport transport) {
	if (!helperRuntimeSupportsRelayTransport(capabilities, transport)) {
		return false;
	}

	return !isWebRtcRelayTransport(transport) || capabilities.gstreamerLiveKitPublishAvailable;
}

bool helperRuntimeSupportsViewing(const ScreenShareHelperClient::CapabilitySnapshot &capabilities,
								  const MumbleProto::ScreenShareRelayTransport transport) {
	if (!helperRuntimeSupportsRelayTransport(capabilities, transport)) {
		return false;
	}

	return !isWebRtcRelayTransport(transport) || capabilities.gstreamerLiveKitViewAvailable;
}

#ifdef Q_OS_WIN
struct ExternalWindowSearch {
	DWORD processID = 0;
	HWND window     = nullptr;
};

BOOL CALLBACK enumExternalProcessWindows(HWND hwnd, LPARAM userData) {
	auto *search = reinterpret_cast< ExternalWindowSearch * >(userData);
	if (!search || !hwnd || !IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) {
		return TRUE;
	}

	DWORD windowProcessID = 0;
	GetWindowThreadProcessId(hwnd, &windowProcessID);
	if (windowProcessID != search->processID) {
		return TRUE;
	}

	RECT rect = {};
	if (!GetWindowRect(hwnd, &rect) || rect.right <= rect.left || rect.bottom <= rect.top) {
		return TRUE;
	}

	search->window = hwnd;
	return FALSE;
}

bool externalProcessIsRunning(const qint64 processID) {
	if (processID <= 0 || processID > std::numeric_limits< DWORD >::max()) {
		return false;
	}

	HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast< DWORD >(processID));
	if (!process) {
		return false;
	}

	const DWORD waitResult = WaitForSingleObject(process, 0);
	CloseHandle(process);
	return waitResult == WAIT_TIMEOUT;
}

HWND findExternalProcessWindow(const qint64 processID) {
	if (processID <= 0 || processID > std::numeric_limits< DWORD >::max()) {
		return nullptr;
	}

	ExternalWindowSearch search;
	search.processID = static_cast< DWORD >(processID);
	EnumWindows(enumExternalProcessWindows, reinterpret_cast< LPARAM >(&search));
	return search.window;
}

bool externalProcessHasWindow(const qint64 processID) {
	return findExternalProcessWindow(processID) != nullptr;
}

bool focusExternalProcessWindow(const qint64 processID) {
	const HWND window = findExternalProcessWindow(processID);
	if (!window) {
		return false;
	}

	if (IsIconic(window)) {
		ShowWindow(window, SW_RESTORE);
	} else {
		ShowWindow(window, SW_SHOWNORMAL);
	}
	SetForegroundWindow(window);
	return true;
}
#else
bool externalProcessIsRunning(const qint64 processID) {
	return processID > 0;
}

bool externalProcessHasWindow(const qint64 processID) {
	return externalProcessIsRunning(processID);
}

bool focusExternalProcessWindow(qint64) {
	return false;
}
#endif

QString normalizeScreenShareQualityProfile(const QString &profile) {
	const QString normalized = profile.trimmed().toLower();
	if (normalized == QLatin1String("sharp_text") || normalized == QLatin1String("sharp-text")
		|| normalized == QLatin1String("text")) {
		return QStringLiteral("sharp_text");
	}
	if (normalized == QLatin1String("smooth_motion") || normalized == QLatin1String("smooth-motion")
		|| normalized == QLatin1String("motion")) {
		return QStringLiteral("smooth_motion");
	}
	if (normalized == QLatin1String("data_saver") || normalized == QLatin1String("data-saver")
		|| normalized == QLatin1String("low")) {
		return QStringLiteral("data_saver");
	}

	return QStringLiteral("auto");
}

struct QualityBitratePolicy {
	unsigned int startKbps = 4000;
	unsigned int minKbps   = 1200;
	unsigned int maxKbps   = 6000;
};

QualityBitratePolicy bitratePolicyForProfile(const QString &profile) {
	const QString normalized = normalizeScreenShareQualityProfile(profile);
	if (normalized == QLatin1String("sharp_text")) {
		return { 4500, 1500, 7000 };
	}
	if (normalized == QLatin1String("smooth_motion")) {
		return { 5000, 1800, 7500 };
	}
	if (normalized == QLatin1String("data_saver")) {
		return { 1600, 700, 2500 };
	}

	return { 4000, 1200, 6000 };
}
} // namespace

ScreenShareManager::ScreenShareManager(QObject *parent) : QObject(parent) {
	m_helperClient = new ScreenShareHelperClient(this);
	connect(m_helperClient, &ScreenShareHelperClient::capabilitiesChanged, this,
			[this]() { logLocalShareAvailabilityDiagnostic(QStringLiteral("helper-capabilities")); });
	m_externalRuntimeWatchdogTimer.setInterval(kExternalRuntimeWatchdogIntervalMsec);
	m_externalRuntimeWatchdogTimer.setSingleShot(false);
	connect(&m_externalRuntimeWatchdogTimer, &QTimer::timeout, this,
			&ScreenShareManager::checkExternalRuntimeLiveness);
}

ScreenShareHelperClient::CapabilitySnapshot ScreenShareManager::detectAdvertisedCapabilities() {
	return ScreenShareHelperClient::detectLocalCapabilities();
}

ScreenShareHelperClient &ScreenShareManager::helperClient() {
	return *m_helperClient;
}

const ScreenShareHelperClient &ScreenShareManager::helperClient() const {
	return *m_helperClient;
}

void ScreenShareManager::logLocalShareAvailabilityDiagnostic(const QString &context) const {
	if (!Global::get().s.bScreenShareDiagnostics) {
		return;
	}

	const QString reason = localShareUnavailableReason();
	if (context == m_lastLoggedAvailabilityContext && reason == m_lastLoggedAvailabilityReason) {
		return;
	}

	m_lastLoggedAvailabilityContext = context;
	m_lastLoggedAvailabilityReason  = reason;

	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
	QStringList relayTransports;
	for (const int transport : capabilities.runtimeRelayTransports) {
		relayTransports << QString::number(transport);
	}
	const QString captureBackends =
		capabilities.captureBackends.isEmpty() ? QStringLiteral("-") : capabilities.captureBackends.join(QStringLiteral(","));

	qInfo().noquote()
		<< QStringLiteral("ScreenShareManager: availability context=%1 connected=%2 enabled=%3 helper_required=%4 "
						  "capture_supported=%5 view_supported=%6 helper_available=%7 relay_url=%8 "
						  "runtime_transports=[%9] in_app_relay=%10 capture_backend=%11 hw_encode=%12 "
						  "zero_copy=%13 roi=%14 damage=%15 queue_budget=%16 capture_backends=[%17] "
						  "gstreamer=%18 livekit_publish=%19 livekit_view=%20 reason=%21")
			   .arg(context.isEmpty() ? QStringLiteral("-") : context,
					Global::get().sh ? QStringLiteral("true") : QStringLiteral("false"),
					Global::get().bScreenShareEnabled ? QStringLiteral("true") : QStringLiteral("false"),
					Global::get().bScreenShareHelperRequired ? QStringLiteral("true") : QStringLiteral("false"),
					capabilities.captureSupported ? QStringLiteral("true") : QStringLiteral("false"),
					capabilities.viewSupported ? QStringLiteral("true") : QStringLiteral("false"),
					capabilities.helperAvailable ? QStringLiteral("true") : QStringLiteral("false"),
					Global::get().qsScreenShareRelayUrl.isEmpty() ? QStringLiteral("-")
																  : Global::get().qsScreenShareRelayUrl,
					relayTransports.join(QStringLiteral(",")),
					QStringLiteral("false"),
					capabilities.captureBackend.isEmpty() ? QStringLiteral("-") : capabilities.captureBackend,
					capabilities.hardwareEncodeSupported ? QStringLiteral("true") : QStringLiteral("false"),
					capabilities.zeroCopySupported ? QStringLiteral("true") : QStringLiteral("false"),
					capabilities.roiSupported ? QStringLiteral("true") : QStringLiteral("false"),
					capabilities.damageMetadataSupported ? QStringLiteral("true") : QStringLiteral("false"),
					QString::number(capabilities.queueBudgetFrames),
					captureBackends,
					capabilities.gstreamerAvailable ? QStringLiteral("true") : QStringLiteral("false"),
					capabilities.gstreamerLiveKitPublishAvailable ? QStringLiteral("true") : QStringLiteral("false"),
					capabilities.gstreamerLiveKitViewAvailable ? QStringLiteral("true") : QStringLiteral("false"),
					reason.isEmpty() ? QStringLiteral("available") : reason);
}

bool ScreenShareManager::canRequestLocalShare() const {
	return localShareUnavailableReason().isEmpty();
}

QString ScreenShareManager::localShareUnavailableReason() const {
	if (!Global::get().sh) {
		return tr("Connect to a server before starting screen sharing.");
	}

	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
	const MumbleProto::ScreenShareRelayTransport relayTransport =
		Mumble::ScreenShare::relayTransportFromUrl(Global::get().qsScreenShareRelayUrl);
	if (!Global::get().bScreenShareEnabled) {
		return tr("Screen sharing is disabled on this server.");
	}
	if (!Mumble::ScreenShare::isValidRelayUrl(Global::get().qsScreenShareRelayUrl)) {
		return tr("This server has no valid screen-share relay endpoint configured.");
	}
	if (!capabilities.captureSupported) {
		return tr("No supported local capture source is available.");
	}
	if (Global::get().bScreenShareHelperRequired && !capabilities.helperAvailable) {
		return tr("The local screen-share helper is required but unavailable.");
	}
	if (!helperRuntimeSupportsPublishing(capabilities, relayTransport)) {
		return isWebRtcRelayTransport(relayTransport)
				   ? tr("The bundled screen-share runtime is missing WebRTC publishing support.")
				   : tr("The bundled screen-share runtime does not support this server's relay transport.");
	}
	if (capabilities.supportedCodecs.isEmpty()) {
		return tr("The bundled screen-share runtime does not advertise an executable video codec.");
	}

	return QString();
}

bool ScreenShareManager::canViewSession(const QString &streamID) const {
	const auto it = m_sessions.constFind(streamID);
	return it != m_sessions.cend() && canViewSession(it.value());
}

bool ScreenShareManager::isPublishingSession(const QString &streamID) const {
	return m_activePublishSessions.contains(streamID);
}

bool ScreenShareManager::isViewingSession(const QString &streamID) const {
	return m_activeViewSessions.contains(streamID) || m_pausedExternalViewSessions.contains(streamID);
}

bool ScreenShareManager::hasDetachedWindow(const QString &streamID) const {
	if (ExternalScreenShareWindowHost *host = m_externalViewWindows.value(streamID, nullptr); host && host->isVisible()) {
		return true;
	}

	if (m_activePublishSessions.contains(streamID)
		&& externalProcessHasWindow(m_externalPublishProcessIDs.value(streamID))) {
		return true;
	}

	return m_activeViewSessions.contains(streamID) && externalProcessHasWindow(m_externalViewProcessIDs.value(streamID));
}

bool ScreenShareManager::hasRunningExternalRuntime(const QString &streamID) const {
	if (m_activePublishSessions.contains(streamID)
		&& externalProcessIsRunning(m_externalPublishProcessIDs.value(streamID))) {
		return true;
	}

	return m_activeViewSessions.contains(streamID)
		   && externalProcessIsRunning(m_externalViewProcessIDs.value(streamID));
}

bool ScreenShareManager::focusOrReopenDetachedWindow(const QString &streamID) {
	auto focusExternalViewHost = [](ExternalScreenShareWindowHost *host) {
		if (!host) {
			return false;
		}

		if (host->isMinimized()) {
			host->showNormal();
		} else {
			host->show();
		}
		host->raise();
		host->activateWindow();
		return true;
	};

	if (focusExternalViewHost(m_externalViewWindows.value(streamID, nullptr))) {
		return true;
	}

	const auto it = m_sessions.constFind(streamID);
	if (it == m_sessions.cend()) {
		return false;
	}

	if (m_activePublishSessions.contains(streamID)) {
		const qint64 processID = m_externalPublishProcessIDs.value(streamID);
		if (focusExternalProcessWindow(processID)) {
			return true;
		}
		if (externalProcessIsRunning(processID)) {
			return false;
		}

		m_activePublishSessions.remove(streamID);
		m_externalPublishProcessIDs.remove(streamID);
		updateExternalRuntimeWatchdog();
		emit sessionUpdated(streamID);
		return false;
	}
	if (m_pausedExternalViewSessions.contains(streamID)) {
		showExternalViewWindow(it.value(), 0);
		return true;
	}
	if (m_activeViewSessions.contains(streamID)) {
		const qint64 processID = m_externalViewProcessIDs.value(streamID);
		if (focusExternalProcessWindow(processID)) {
			return true;
		}
		if (externalProcessIsRunning(processID)) {
			return false;
		}

		m_activeViewSessions.remove(streamID);
		m_externalViewProcessIDs.remove(streamID);
		updateExternalRuntimeWatchdog();
		emit sessionUpdated(streamID);
		return false;
	}

	return false;
}

bool ScreenShareManager::isUsingFallbackRuntime(const QString &streamID) const {
	Q_UNUSED(streamID);
	return false;
}

bool ScreenShareManager::isUsingNativeGpuRuntime(const QString &streamID) const {
	if (!m_activePublishSessions.contains(streamID)) {
		return false;
	}

	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
	return capabilities.zeroCopySupported
		   && (capabilities.captureBackends.contains(QStringLiteral("gstreamer-d3d11-livekit"))
			   || capabilities.captureBackends.contains(QStringLiteral("d3d11-desktop-duplication"))
			   || capabilities.captureBackends.contains(QStringLiteral("windows-graphics-capture-d3d11")));
}

void ScreenShareManager::requestStartChannelShare(unsigned int channelID, const ScreenShareStartOptions &options) {
	if (!Global::get().sh) {
		return;
	}
	logLocalShareAvailabilityDiagnostic(QStringLiteral("request-start"));
	if (!Mumble::ScreenShare::isValidRelayUrl(Global::get().qsScreenShareRelayUrl)) {
		if (Global::get().l) {
			Global::get().l->log(
				Log::Warning,
				tr("Screen sharing is unavailable because the server has no valid relay endpoint configured."));
		}
		return;
	}
	if (!canRequestLocalShare()) {
		return;
	}

	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
	auto clampRequestedLimit = [](const unsigned int requested, const unsigned int capabilityLimit) {
		if (requested > 0 && capabilityLimit > 0) {
			return std::min(requested, capabilityLimit);
		}

		return requested > 0 ? requested : capabilityLimit;
	};
	if (channelID == 0) {
		const ClientUser *self = ClientUser::get(Global::get().uiSession);
		if (self && self->cChannel) {
			channelID = self->cChannel->iId;
		}
	}

	MumbleProto::ScreenShareCreate msg;
	msg.set_scope(MumbleProto::ScreenShareScopeChannel);
	if (channelID != 0) {
		msg.set_scope_id(channelID);
	}
	QList< int > availableCodecs = capabilities.supportedCodecs;
	if (availableCodecs.isEmpty()) {
		if (Global::get().l) {
			Global::get().l->log(Log::Warning,
								 tr("Unable to start screen sharing: no executable video codec is available."));
		}
		return;
	}
	const QList< int > preferredCodecs = Global::get().qlPreferredScreenShareCodecs.isEmpty()
											 ? availableCodecs
											 : Global::get().qlPreferredScreenShareCodecs;
	const MumbleProto::ScreenShareCodec codec =
		Mumble::ScreenShare::selectPreferredCodec(preferredCodecs, availableCodecs);
	for (const int availableCodec : availableCodecs) {
		msg.add_requested_codecs(static_cast< MumbleProto::ScreenShareCodec >(availableCodec));
	}

	const unsigned int requestedWidth =
		options.requestedWidth > 0 ? options.requestedWidth : Global::get().uiScreenShareMaxWidth;
	const unsigned int requestedHeight =
		options.requestedHeight > 0 ? options.requestedHeight : Global::get().uiScreenShareMaxHeight;
	const unsigned int requestedFps = options.requestedFps > 0 ? options.requestedFps : Global::get().uiScreenShareMaxFps;
	unsigned int targetWidth        = clampRequestedLimit(requestedWidth, capabilities.maxWidth);
	unsigned int targetHeight       = clampRequestedLimit(requestedHeight, capabilities.maxHeight);
	unsigned int targetFps          = clampRequestedLimit(requestedFps, capabilities.maxFps);

	const QString requestedQualityProfile = options.qualityProfile.trimmed().isEmpty()
												? qEnvironmentVariable("MUMBLE_SCREENSHARE_QUALITY_PROFILE",
																	   QStringLiteral("auto"))
												: options.qualityProfile;
	const QString qualityProfile = normalizeScreenShareQualityProfile(requestedQualityProfile);
	if (qualityProfile == QLatin1String("data_saver")) {
		targetWidth  = std::min(targetWidth, 960U);
		targetHeight = std::min(targetHeight, 540U);
		targetFps    = std::min(targetFps, 20U);
	} else if (qualityProfile == QLatin1String("sharp_text")) {
		targetFps = std::min(targetFps, 30U);
	}
	const QualityBitratePolicy bitratePolicy = bitratePolicyForProfile(qualityProfile);
	const unsigned int defaultBitrate =
		Mumble::ScreenShare::defaultBitrateKbps(codec, targetWidth, targetHeight, targetFps);
	const unsigned int requestedBitrate = qMax(defaultBitrate, bitratePolicy.startKbps);

	msg.set_requested_width(targetWidth);
	msg.set_requested_height(targetHeight);
	msg.set_requested_fps(targetFps);
	msg.set_requested_bitrate_kbps(requestedBitrate);
	msg.set_quality_profile(u8(qualityProfile));
	const QString requestedCaptureSource =
		options.captureSourceID.trimmed().isEmpty()
			? qEnvironmentVariable("MUMBLE_SCREENSHARE_SOURCE_ID", QStringLiteral("primary-monitor")).trimmed()
			: options.captureSourceID.trimmed();
	msg.set_capture_source_id(
		u8(requestedCaptureSource.isEmpty() ? QStringLiteral("primary-monitor") : requestedCaptureSource));
	if (options.captureAudio) {
		msg.set_capture_audio(true);
		const QString requestedAudioSource = options.audioSourceID.trimmed();
		if (!requestedAudioSource.isEmpty()) {
			msg.set_audio_source_id(u8(requestedAudioSource));
		}
	}
	msg.set_requested_min_bitrate_kbps(std::min(bitratePolicy.minKbps, requestedBitrate));
	msg.set_requested_max_bitrate_kbps(qMax(bitratePolicy.maxKbps, requestedBitrate));
	msg.set_prefer_hardware_encoding(true);

	Global::get().sh->sendMessage(msg);
}

void ScreenShareManager::requestStartViewing(const QString &streamID) {
	const auto it = m_sessions.constFind(streamID);
	if (it == m_sessions.cend()) {
		return;
	}

	if (!canViewSession(it.value())) {
		if (Global::get().l) {
			Global::get().l->log(
				Log::Warning,
				tr("Screen share %1 is not viewable on this client right now.").arg(streamID.toHtmlEscaped()));
		}
		return;
	}

	startLocalViewSession(it.value());
	emit sessionUpdated(streamID);
}

void ScreenShareManager::requestStopViewing(const QString &streamID) {
	const bool knownSession = m_sessions.contains(streamID);
	stopLocalViewSession(streamID);
	if (knownSession) {
		emit sessionUpdated(streamID);
	}
}

void ScreenShareManager::requestStopShare(const QString &streamID) {
	if (!Global::get().sh || streamID.isEmpty()) {
		return;
	}

	MumbleProto::ScreenShareStop msg;
	msg.set_stream_id(u8(streamID));
	Global::get().sh->sendMessage(msg);
}

const QHash< QString, ScreenShareSession > &ScreenShareManager::sessions() const {
	return m_sessions;
}

bool ScreenShareManager::hasSession(const QString &streamID) const {
	return m_sessions.contains(streamID);
}

void ScreenShareManager::resetState() {
	for (const QString &streamID : m_activePublishSessions) {
		m_helperClient->stopPublish(streamID);
	}
	for (const QString &streamID : m_activeViewSessions) {
		m_helperClient->stopView(streamID);
	}
	for (ExternalScreenShareWindowHost *host : m_externalViewWindows.values()) {
		if (host) {
			host->closeFromManager();
		}
	}
	m_externalViewWindows.clear();
	m_externalViewAudioMuted.clear();
	m_pausedExternalViewSessions.clear();

	m_activePublishSessions.clear();
	m_activeViewSessions.clear();
	m_externalPublishProcessIDs.clear();
	m_externalViewProcessIDs.clear();
	m_externalPublishRestartAttempts.clear();
	m_externalViewRestartAttempts.clear();
	m_announcedViewableSessions.clear();
	m_sessions.clear();
	m_lastLoggedAvailabilityContext.clear();
	m_lastLoggedAvailabilityReason.clear();
	updateExternalRuntimeWatchdog();
}

ScreenShareSession ScreenShareManager::sessionFromState(const MumbleProto::ScreenShareState &msg) const {
	ScreenShareSession session;
	if (msg.has_stream_id()) {
		session.streamID = u8(msg.stream_id());
	}
	if (msg.has_owner_session()) {
		session.ownerSession = msg.owner_session();
	}
	if (msg.has_scope()) {
		session.scope = msg.scope();
	}
	if (msg.has_scope_id()) {
		session.scopeID = msg.scope_id();
	}
	if (msg.has_relay_url()) {
		session.relayUrl = u8(msg.relay_url());
	}
	if (msg.has_relay_room_id()) {
		session.relayRoomID = u8(msg.relay_room_id());
	}
	if (msg.has_relay_token()) {
		session.relayToken = u8(msg.relay_token());
	}
	if (msg.has_relay_session_id()) {
		session.relaySessionID = u8(msg.relay_session_id());
	}
	if (msg.has_relay_transport()) {
		session.relayTransport = msg.relay_transport();
	}
	if (msg.has_relay_role()) {
		session.relayRole = msg.relay_role();
	}
	if (msg.has_relay_token_expires_at()) {
		session.relayTokenExpiresAt = msg.relay_token_expires_at();
	}
	if (session.relayTransport == MumbleProto::ScreenShareRelayTransportUnknown
		&& Mumble::ScreenShare::isValidRelayUrl(session.relayUrl)) {
		session.relayTransport = Mumble::ScreenShare::relayTransportFromUrl(session.relayUrl);
	}
	if (!msg.has_relay_role()) {
		session.relayRole = session.ownerSession == Global::get().uiSession ? MumbleProto::ScreenShareRelayRolePublisher
																			: MumbleProto::ScreenShareRelayRoleViewer;
	}
	if (msg.has_created_at()) {
		session.createdAt = msg.created_at();
	}
	if (msg.has_state()) {
		session.state = msg.state();
	}
	if (msg.has_codec()) {
		session.codec = msg.codec();
	}
	session.codecFallbackOrder = codecFallbackOrderFromState(msg);
	if (msg.has_width()) {
		session.width = msg.width();
	}
	if (msg.has_height()) {
		session.height = msg.height();
	}
	if (msg.has_fps()) {
		session.fps = msg.fps();
	}
	if (msg.has_bitrate_kbps()) {
		session.bitrateKbps = msg.bitrate_kbps();
	}
	if (msg.has_quality_profile()) {
		session.qualityProfile = normalizeScreenShareQualityProfile(u8(msg.quality_profile()));
	}
	if (msg.has_capture_source_id()) {
		session.captureSourceID = u8(msg.capture_source_id()).trimmed();
	}
	if (msg.has_capture_audio()) {
		session.captureAudio = msg.capture_audio();
	}
	if (msg.has_audio_source_id()) {
		session.audioSourceID = u8(msg.audio_source_id()).trimmed();
	}
	if (msg.has_min_bitrate_kbps()) {
		session.minBitrateKbps = msg.min_bitrate_kbps();
	}
	if (msg.has_max_bitrate_kbps()) {
		session.maxBitrateKbps = msg.max_bitrate_kbps();
	}

	return session;
}

bool ScreenShareManager::canPublishSession(const ScreenShareSession &session) const {
	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
	if (!Global::get().bScreenShareEnabled || !Global::get().sh) {
		return false;
	}
	if (session.ownerSession != Global::get().uiSession
		|| session.state != MumbleProto::ScreenShareLifecycleStateActive) {
		return false;
	}
	if (!Mumble::ScreenShare::isValidRelayUrl(session.relayUrl) || session.relayRoomID.trimmed().isEmpty()) {
		return false;
	}
	if (!session.relayToken.isEmpty() && tokenExpired(session.relayTokenExpiresAt)) {
		return false;
	}
	if (Mumble::ScreenShare::relayTransportRequiresSignaling(session.relayTransport)
		&& (session.relaySessionID.trimmed().isEmpty() || session.relayToken.trimmed().isEmpty()
			|| session.relayRole != MumbleProto::ScreenShareRelayRolePublisher)) {
		return false;
	}
	if (!Mumble::ScreenShare::isValidCodec(session.codec)) {
		return false;
	}
	if (!capabilities.captureSupported) {
		return false;
	}
	if (Global::get().bScreenShareHelperRequired && !capabilities.helperAvailable) {
		return false;
	}
	if (!helperRuntimeSupportsPublishing(capabilities, session.relayTransport)) {
		return false;
	}
	return capabilities.supportedCodecs.contains(static_cast< int >(session.codec));
}

bool ScreenShareManager::canViewSession(const ScreenShareSession &session) const {
	const ScreenShareHelperClient::CapabilitySnapshot &capabilities = m_helperClient->capabilities();
	if (!Global::get().bScreenShareEnabled || session.ownerSession == Global::get().uiSession
		|| session.state != MumbleProto::ScreenShareLifecycleStateActive) {
		return false;
	}
	if (!Mumble::ScreenShare::isValidRelayUrl(session.relayUrl) || session.relayRoomID.trimmed().isEmpty()) {
		return false;
	}
	if (!session.relayToken.isEmpty() && tokenExpired(session.relayTokenExpiresAt)) {
		return false;
	}
	if (Mumble::ScreenShare::relayTransportRequiresSignaling(session.relayTransport)
		&& (session.relaySessionID.trimmed().isEmpty() || session.relayToken.trimmed().isEmpty()
			|| session.relayRole != MumbleProto::ScreenShareRelayRoleViewer)) {
		return false;
	}
	if (!Mumble::ScreenShare::isValidCodec(session.codec)) {
		return false;
	}
	if (!capabilities.viewSupported) {
		return false;
	}
	if (Global::get().bScreenShareHelperRequired && !capabilities.helperAvailable) {
		return false;
	}
	if (!helperRuntimeSupportsViewing(capabilities, session.relayTransport)) {
		return false;
	}
	if (!capabilities.supportedCodecs.contains(static_cast< int >(session.codec))) {
		return false;
	}

	const ClientUser *self = ClientUser::get(Global::get().uiSession);
	if (!self || !self->cChannel) {
		return false;
	}

	return session.scope == MumbleProto::ScreenShareScopeChannel && session.scopeID != 0
		   && self->cChannel->iId == session.scopeID;
}

bool ScreenShareManager::shouldAutoViewSession(const ScreenShareSession &session) const {
	Q_UNUSED(session);

	if (const std::optional< bool > override = envFlagOverride("MUMBLE_SCREENSHARE_AUTOVIEW"); override.has_value()) {
		return override.value();
	}
	if (const std::optional< bool > override = envFlagOverride("MUMBLE_SCREENSHARE_AUTO_VIEW"); override.has_value()) {
		return override.value();
	}

	return Global::get().s.bScreenShareAutoOpenCurrentRoom;
}

bool ScreenShareManager::restartExternalViewSession(const ScreenShareSession &session) {
	QString errorMessage;
	qint64 processID = 0;

	if (m_helperClient->startView(session, &errorMessage, &processID)) {
		m_activeViewSessions.insert(session.streamID);
		m_pausedExternalViewSessions.remove(session.streamID);
		m_externalViewProcessIDs.insert(session.streamID, processID);
		showExternalViewWindow(session, processID);
		updateExternalRuntimeWatchdog();
		if (Global::get().l) {
			Global::get().l->log(Log::Information,
								 tr("Using the external screen-share runtime for %1.")
									 .arg(session.streamID.toHtmlEscaped()));
		}
		return true;
	}

	if (Global::get().l) {
		Global::get().l->log(Log::Warning, tr("Unable to start screen-share viewer for %1: %2")
											   .arg(session.streamID.toHtmlEscaped(),
													errorMessage.isEmpty() ? tr("unknown error")
																		   : errorMessage.toHtmlEscaped()));
	}
	return false;
}

void ScreenShareManager::showExternalViewWindow(const ScreenShareSession &session, const qint64 processID) {
	ExternalScreenShareWindowHost *host = m_externalViewWindows.value(session.streamID, nullptr);
	if (!host) {
		host = new ExternalScreenShareWindowHost(session);
		m_externalViewWindows.insert(session.streamID, host);

		connect(host, &ExternalScreenShareWindowHost::stopRequested, this, &ScreenShareManager::requestStopViewing);
		connect(host, &ExternalScreenShareWindowHost::closeRequested, this, &ScreenShareManager::requestStopViewing);
		connect(host, &ExternalScreenShareWindowHost::pauseToggled, this,
				&ScreenShareManager::setExternalViewPaused);
		connect(host, &ExternalScreenShareWindowHost::audioMuteToggled, this,
				&ScreenShareManager::setExternalViewAudioMuted);
		connect(host, &QObject::destroyed, this, [this, streamID = session.streamID, host]() {
			if (m_externalViewWindows.value(streamID, nullptr) == host) {
				m_externalViewWindows.remove(streamID);
			}
		});
	} else {
		host->updateSession(session);
	}

	host->setProcessID(processID);
	host->setAudioMuted(m_externalViewAudioMuted.contains(session.streamID));
	host->setPaused(m_pausedExternalViewSessions.contains(session.streamID));
	if (host->isMinimized()) {
		host->showNormal();
	} else {
		host->show();
	}
	host->raise();
	host->activateWindow();
}

void ScreenShareManager::startLocalPublishSession(const ScreenShareSession &session) {
	if (m_activePublishSessions.contains(session.streamID)) {
		return;
	}

	QString errorMessage;
	qint64 processID = 0;
	if (m_helperClient->startPublish(session, &errorMessage, &processID)) {
		m_activePublishSessions.insert(session.streamID);
		m_externalPublishProcessIDs.insert(session.streamID, processID);
		updateExternalRuntimeWatchdog();
		if (Global::get().l) {
			Global::get().l->log(
				Log::Information,
				tr("Using the external screen-share runtime for %1.").arg(session.streamID.toHtmlEscaped()));
		}
		return;
	}

	if (Global::get().l) {
		Global::get().l->log(Log::Warning, tr("Unable to start local screen-share helper for %1: %2")
											   .arg(session.streamID.toHtmlEscaped(), errorMessage.toHtmlEscaped()));
	}
	requestStopShare(session.streamID);
}

void ScreenShareManager::startLocalViewSession(const ScreenShareSession &session) {
	if (m_activeViewSessions.contains(session.streamID)) {
		focusOrReopenDetachedWindow(session.streamID);
		return;
	}
	if (m_pausedExternalViewSessions.contains(session.streamID)) {
		setExternalViewPaused(session.streamID, false);
		return;
	}

	QString errorMessage;
	qint64 processID = 0;
	if (m_helperClient->startView(session, &errorMessage, &processID)) {
		m_activeViewSessions.insert(session.streamID);
		m_externalViewProcessIDs.insert(session.streamID, processID);
		showExternalViewWindow(session, processID);
		updateExternalRuntimeWatchdog();
		if (Global::get().l) {
			Global::get().l->log(
				Log::Information,
				tr("Using the external screen-share runtime for %1.").arg(session.streamID.toHtmlEscaped()));
		}
		return;
	}

	if (Global::get().l) {
		Global::get().l->log(Log::Warning, tr("Unable to start screen-share viewer for %1: %2")
											   .arg(session.streamID.toHtmlEscaped(), errorMessage.toHtmlEscaped()));
	}
}

void ScreenShareManager::stopLocalPublishSession(const QString &streamID) {
	if (m_activePublishSessions.remove(streamID)) {
		m_externalPublishProcessIDs.remove(streamID);
		m_helperClient->stopPublish(streamID);
	}
	m_externalPublishProcessIDs.remove(streamID);
	m_externalPublishRestartAttempts.remove(streamID);
	updateExternalRuntimeWatchdog();
}

void ScreenShareManager::stopLocalViewSession(const QString &streamID) {
	const auto hostIt = m_externalViewWindows.find(streamID);
	if (hostIt != m_externalViewWindows.end()) {
		ExternalScreenShareWindowHost *host = hostIt.value();
		m_externalViewWindows.erase(hostIt);
		if (host) {
			host->closeFromManager();
		}
	}
	m_externalViewAudioMuted.remove(streamID);
	m_pausedExternalViewSessions.remove(streamID);

	if (m_activeViewSessions.remove(streamID)) {
		m_externalViewProcessIDs.remove(streamID);
		m_helperClient->stopView(streamID);
	}
	m_externalViewProcessIDs.remove(streamID);
	m_externalViewRestartAttempts.remove(streamID);
	updateExternalRuntimeWatchdog();
}

void ScreenShareManager::updateExternalRuntimeWatchdog() {
	if (m_activePublishSessions.isEmpty() && m_activeViewSessions.isEmpty()) {
		m_externalRuntimeWatchdogTimer.stop();
		return;
	}

	if (!m_externalRuntimeWatchdogTimer.isActive()) {
		m_externalRuntimeWatchdogTimer.start();
	}
}

void ScreenShareManager::checkExternalRuntimeLiveness() {
	const QSet< QString > activePublishSessions = m_activePublishSessions;
	for (const QString &streamID : activePublishSessions) {
		const qint64 processID = m_externalPublishProcessIDs.value(streamID, 0);
		if (externalProcessIsRunning(processID)) {
			continue;
		}

		m_activePublishSessions.remove(streamID);
		m_externalPublishProcessIDs.remove(streamID);
		m_helperClient->stopPublish(streamID);

		const int restartAttempt = m_externalPublishRestartAttempts.value(streamID, 0) + 1;
		m_externalPublishRestartAttempts.insert(streamID, restartAttempt);
		const auto sessionIt = m_sessions.constFind(streamID);
		const bool canRestart = sessionIt != m_sessions.cend() && restartAttempt <= kExternalRuntimeRestartLimit
								&& canPublishSession(sessionIt.value());

		if (Global::get().l) {
			if (canRestart) {
				Global::get().l->log(Log::Warning,
									 tr("Screen-share publisher runtime for %1 exited unexpectedly; restarting "
										"(%2/%3).")
										 .arg(streamID.toHtmlEscaped(), QString::number(restartAttempt),
											  QString::number(kExternalRuntimeRestartLimit)));
			} else {
				Global::get().l->log(Log::Warning,
									 tr("Screen-share publisher runtime for %1 exited unexpectedly; ending the "
										"share.")
										 .arg(streamID.toHtmlEscaped()));
			}
		}

		if (canRestart) {
			startLocalPublishSession(sessionIt.value());
		} else {
			requestStopShare(streamID);
		}

		emit sessionUpdated(streamID);
	}

	const QSet< QString > activeViewSessions = m_activeViewSessions;
	for (const QString &streamID : activeViewSessions) {
		const qint64 processID = m_externalViewProcessIDs.value(streamID, 0);
		if (externalProcessIsRunning(processID)) {
			continue;
		}

		m_activeViewSessions.remove(streamID);
		m_externalViewProcessIDs.remove(streamID);
		m_helperClient->stopView(streamID);

		const int restartAttempt = m_externalViewRestartAttempts.value(streamID, 0) + 1;
		m_externalViewRestartAttempts.insert(streamID, restartAttempt);
		const auto sessionIt = m_sessions.constFind(streamID);
		const bool canRestart = sessionIt != m_sessions.cend() && restartAttempt <= kExternalRuntimeRestartLimit
								&& canViewSession(sessionIt.value());

		if (Global::get().l) {
			if (canRestart) {
				Global::get().l->log(Log::Warning,
									 tr("Screen-share viewer runtime for %1 exited unexpectedly; restarting "
										"(%2/%3).")
										 .arg(streamID.toHtmlEscaped(), QString::number(restartAttempt),
											  QString::number(kExternalRuntimeRestartLimit)));
			} else {
				Global::get().l->log(Log::Warning,
									 tr("Screen-share viewer runtime for %1 exited unexpectedly; closing the "
										"viewer.")
										 .arg(streamID.toHtmlEscaped()));
			}
		}

		if (canRestart) {
			if (!restartExternalViewSession(sessionIt.value())) {
				stopLocalViewSession(streamID);
			}
		} else {
			stopLocalViewSession(streamID);
		}

		emit sessionUpdated(streamID);
	}

	updateExternalRuntimeWatchdog();
}

void ScreenShareManager::setExternalViewAudioMuted(const QString &streamID, const bool muted) {
	const auto it = m_sessions.constFind(streamID);
	if (it == m_sessions.cend()) {
		return;
	}

	if (muted) {
		m_externalViewAudioMuted.insert(streamID);
	} else {
		m_externalViewAudioMuted.remove(streamID);
	}

	ExternalScreenShareWindowHost *host = m_externalViewWindows.value(streamID, nullptr);
	if (host) {
		host->setAudioMuted(muted);
	}

	emit sessionUpdated(streamID);
}

void ScreenShareManager::setExternalViewPaused(const QString &streamID, const bool paused) {
	const auto it = m_sessions.constFind(streamID);
	if (it == m_sessions.cend()) {
		return;
	}

	ExternalScreenShareWindowHost *host = m_externalViewWindows.value(streamID, nullptr);
	if (paused) {
		m_pausedExternalViewSessions.insert(streamID);
		if (m_activeViewSessions.remove(streamID)) {
			m_helperClient->stopView(streamID);
		}
		m_externalViewProcessIDs.remove(streamID);
		updateExternalRuntimeWatchdog();
		if (host) {
			host->setProcessID(0);
			host->setPaused(true);
		}
		emit sessionUpdated(streamID);
		return;
	}

	m_pausedExternalViewSessions.remove(streamID);
	if (host) {
		host->setPaused(false);
	}
	if (!m_activeViewSessions.contains(streamID)) {
		if (!canViewSession(it.value()) || !restartExternalViewSession(it.value())) {
			requestStopViewing(streamID);
			return;
		}
	}
	emit sessionUpdated(streamID);
}

void ScreenShareManager::logRemoteViewAvailability(const ScreenShareSession &session) {
	if (m_announcedViewableSessions.contains(session.streamID) || !Global::get().l) {
		return;
	}

	m_announcedViewableSessions.insert(session.streamID);
	const ClientUser *owner  = ClientUser::get(session.ownerSession);
	const QString ownerLabel = owner ? owner->qsName.toHtmlEscaped()
									 : tr("session %1").arg(QString::number(session.ownerSession).toHtmlEscaped());
	Global::get().l->log(Log::Information,
						 tr("Screen share %1 from %2 is available in this channel. Enable auto-open in Settings > "
							"Screen Sharing or set MUMBLE_SCREENSHARE_AUTOVIEW=1 to open it automatically.")
							 .arg(session.streamID.toHtmlEscaped(), ownerLabel));
}

void ScreenShareManager::handleScreenShareState(const MumbleProto::ScreenShareState &msg) {
	if (!msg.has_stream_id()) {
		return;
	}

	const ScreenShareSession session = sessionFromState(msg);
	m_sessions.insert(session.streamID, session);
	if (ExternalScreenShareWindowHost *host = m_externalViewWindows.value(session.streamID, nullptr)) {
		host->updateSession(session);
	}

	if (canPublishSession(session)) {
		startLocalPublishSession(session);
	} else {
		stopLocalPublishSession(session.streamID);
	}

	if (canViewSession(session)) {
		if (m_activeViewSessions.contains(session.streamID)
			|| m_pausedExternalViewSessions.contains(session.streamID)) {
			m_announcedViewableSessions.remove(session.streamID);
		} else if (shouldAutoViewSession(session)) {
			startLocalViewSession(session);
			m_announcedViewableSessions.remove(session.streamID);
		} else {
			logRemoteViewAvailability(session);
		}
	} else {
		stopLocalViewSession(session.streamID);
		m_announcedViewableSessions.remove(session.streamID);
	}

	emit sessionUpdated(session.streamID);
}

void ScreenShareManager::handleScreenShareOffer(const MumbleProto::ScreenShareOffer &) {
}

void ScreenShareManager::handleScreenShareAnswer(const MumbleProto::ScreenShareAnswer &) {
}

void ScreenShareManager::handleScreenShareIceCandidate(const MumbleProto::ScreenShareIceCandidate &) {
}

void ScreenShareManager::handleScreenShareStop(const MumbleProto::ScreenShareStop &msg) {
	if (!msg.has_stream_id()) {
		return;
	}

	const QString streamID = u8(msg.stream_id());
	const QString reason   = msg.has_reason() ? u8(msg.reason()) : QString();
	m_announcedViewableSessions.remove(streamID);
	stopLocalHelperSessions(streamID);
	if (!m_sessions.contains(streamID)) {
		if (!reason.isEmpty() && Global::get().l) {
			Global::get().l->log(Log::Information,
								 tr("Screen share %1 ended: %2").arg(streamID.toHtmlEscaped(), reason.toHtmlEscaped()));
		}
		return;
	}

	m_sessions.remove(streamID);
	if (!reason.isEmpty() && Global::get().l) {
		Global::get().l->log(Log::Information,
							 tr("Screen share %1 ended: %2").arg(streamID.toHtmlEscaped(), reason.toHtmlEscaped()));
	}
	emit sessionStopped(streamID);
}

void ScreenShareManager::stopLocalHelperSessions(const QString &streamID) {
	stopLocalPublishSession(streamID);
	stopLocalViewSession(streamID);
}
