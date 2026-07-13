// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

/// This file contains callback methods for receiving messages (based on Google's ProtoBuf) from the Server.
/// Further details on what exactly is contained in the "message objects" that are parameters to all functions
/// in this file, can be found in the src/Mumble.proto file.

#include "AudioInput.h"
#include "Channel.h"
#include "ChatFeature.h"
#include "ChatPerfTrace.h"
#include "Connection.h"
#include "Database.h"
#include "FeedbackReport.h"
#include "ForkFeature.h"
#include "Log.h"
#include "MainWindow.h"
#include "MumbleConstants.h"
#include "GlobalShortcut.h"
#include "ChannelListenerManager.h"
#include "PluginManager.h"
#include "QmlClientModels.h"
#include "QmlShellHost.h"
#include "ProtoUtils.h"
#include "ScreenShare.h"
#include "ScreenShareManager.h"
#include "ServerHandler.h"
#include "User.h"
#include "UserModel.h"
#include "Utils.h"
#include "VersionCheck.h"
#include "crypto/CryptState.h"
#include "crypto/CryptStateOCB2.h"
#include "Global.h"

#include <algorithm>
#include <QDateTime>
#include <QFile>
#include <QTextDocumentFragment>
#include <QTextStream>
#include <QUrl>

#define ACTOR_INIT                           \
	ClientUser *pSrc = nullptr;              \
	if (msg.has_actor())                     \
		pSrc = ClientUser::get(msg.actor()); \
	Q_UNUSED(pSrc);

#define VICTIM_INIT                                                                \
	ClientUser *pDst = ClientUser::get(msg.session());                             \
	if (!pDst) {                                                                   \
		qWarning("MainWindow: Message for nonexistent victim %d.", msg.session()); \
		return;                                                                    \
	}

#define SELF_INIT                                                                                       \
	ClientUser *pSelf = ClientUser::get(Global::get().uiSession);                                       \
	if (!pSelf) {                                                                                       \
		qWarning("MainWindow: Received message outside of session (sid %d).", Global::get().uiSession); \
		return;                                                                                         \
	}

namespace {
	QString normalizedChatAssetMime(const QString &mime) {
		return mime.section(QLatin1Char(';'), 0, 0).trimmed().toLower();
	}

	bool isPersistentChatPlayableMediaMime(const QString &mime) {
		const QString normalized = normalizedChatAssetMime(mime);
		return normalized == QLatin1String("image/gif") || normalized == QLatin1String("video/mp4")
			   || normalized == QLatin1String("video/webm");
	}

	QString persistentChatPlayableMediaKind(const QString &mime, MumbleProto::ChatAssetKind kind) {
		const QString normalized = normalizedChatAssetMime(mime);
		if (kind == MumbleProto::ChatAssetKindVideo || normalized.startsWith(QLatin1String("video/"))) {
			return QStringLiteral("video");
		}
		if (normalized == QLatin1String("image/gif")) {
			return QStringLiteral("gif");
		}
		if (kind == MumbleProto::ChatAssetKindImage || normalized.startsWith(QLatin1String("image/"))) {
			return QStringLiteral("image");
		}
		return QStringLiteral("media");
	}

	QString persistentChatPlayableMediaDataUrl(const QString &mime, const QByteArray &bytes) {
		const QString normalized = normalizedChatAssetMime(mime);
		if (!isPersistentChatPlayableMediaMime(normalized) || bytes.isEmpty()) {
			return QString();
		}

		return QString::fromLatin1("data:%1;base64,%2").arg(normalized, QString::fromLatin1(bytes.toBase64()));
	}

	QString normalizedPreviewHost(QString host) {
		host = host.trimmed().toLower();
		if (host.startsWith(QLatin1String("www."))) {
			host.remove(0, 4);
		}
		if (host.startsWith(QLatin1String("old."))) {
			host.remove(0, 4);
		}
		if (host.startsWith(QLatin1String("new."))) {
			host.remove(0, 4);
		}
		if (host.startsWith(QLatin1String("m."))) {
			host.remove(0, 2);
		}
		if (host.startsWith(QLatin1String("mobile."))) {
			host.remove(0, 7);
		}
		return host;
	}

	bool isSocialVideoPreviewUrl(const QString &url) {
		const QString host = normalizedPreviewHost(QUrl(url).host());
		return host == QLatin1String("reddit.com") || host == QLatin1String("redd.it")
			   || host == QLatin1String("v.redd.it") || host == QLatin1String("facebook.com")
			   || host == QLatin1String("fb.watch") || host == QLatin1String("instagram.com")
			   || host == QLatin1String("instagr.am");
	}

	QList< int > preferredScreenShareCodecsFromConfig(const MumbleProto::ServerConfig &msg) {
		QList< int > codecs;
		codecs.reserve(msg.preferred_screen_share_codecs_size());
		for (int i = 0; i < msg.preferred_screen_share_codecs_size(); ++i) {
			codecs.append(static_cast< int >(msg.preferred_screen_share_codecs(i)));
		}

		return Mumble::ScreenShare::sanitizeCodecList(codecs);
	}

	QString boolToken(const bool value) {
		return value ? QStringLiteral("true") : QStringLiteral("false");
	}

} // namespace

/// The authenticate message is being used by the client to send the authentication credentials to the server. Therefore
/// the server won't send this message type to the client which is why this implementation does nothing.
void MainWindow::msgAuthenticate(const MumbleProto::Authenticate &) {
}

/// This message is being received after this client has queried for the ban list (probably by using the BanEditor).
///
/// @param msg The message object containing information about the ban list
void MainWindow::msgBanList(const MumbleProto::BanList &msg) {
	openModernServerBanListDialog(msg);
}

/// This message is being received whenever the server rejects the connection of this client.
///
/// @param msg The message object containing the information about why the connection was rejected
void MainWindow::msgReject(const MumbleProto::Reject &msg) {
	rtLast = msg.type();

	QString reason;

	switch (rtLast) {
		case MumbleProto::Reject_RejectType_InvalidUsername:
			reason = tr("According to the server's configuration, your username is considered invalid.");
			break;
		case MumbleProto::Reject_RejectType_UsernameInUse:
			reason = tr("Username in use");
			break;
		case MumbleProto::Reject_RejectType_WrongUserPW:
			reason = tr("Wrong certificate or password");
			break;
		case MumbleProto::Reject_RejectType_WrongServerPW:
			reason = tr("Wrong password");
			break;
		case MumbleProto::Reject_RejectType_AuthenticatorFail:
			reason = tr("Your account information can not be verified currently. Please try again later");
			break;
		default:
			reason = u8(msg.reason()).toHtmlEscaped();
			break;
	}

	Global::get().l->log(Log::ServerDisconnected, tr("Server connection rejected: %1.").arg(reason));
	Global::get().l->setIgnore(Log::ServerDisconnected, 1);
}

/// This message is being received when the server has authenticated the user and finished synchronizing the server
/// state. The message contains the session ID (user ID) for this client that gets assigned to Global::uiSession. It
/// also contains information about the maximum bandwidth the user should use and the user's permissions in the root
/// channel. Furthermore the message may contain a welcome message that is logged to Mumble's console if present.
///
/// @param msg The message object with the respective information
void MainWindow::msgServerSync(const MumbleProto::ServerSync &msg) {
	const ClientUser *user = ClientUser::get(msg.session());
	const bool traceServerSync = qEnvironmentVariableIntValue("MUMBLE_CONNECT_TRACE") != 0;
	const auto appendServerSyncTrace = [traceServerSync, session = msg.session()](const QString &phase) {
		if (!traceServerSync) {
			return;
		}

		QFile traceFile(Global::get().qdBasePath.filePath(QLatin1String("shared-modern-connect-trace.log")));
		if (!traceFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
			return;
		}

		QTextStream stream(&traceFile);
		stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << QLatin1Char('Z')
			   << " ServerSync session=" << session << QLatin1Char(' ') << phase << Qt::endl;
	};
	appendServerSyncTrace(QStringLiteral("enter"));
	if (!user) {
		Global::get().l->log(Log::CriticalError, tr("Server sync protocol violation. No user profile received."));
		Global::get().sh->disconnect();
		return;
	}
	appendServerSyncTrace(QStringLiteral("user-found"));
	Global::get().uiSession = msg.session();
	Global::get().bScreenShareEnabled = false;
	Global::get().bScreenShareRecordingEnabled = false;
	Global::get().bScreenShareHelperRequired = true;
	Global::get().qlPreferredScreenShareCodecs.clear();
	Global::get().uiScreenShareMaxWidth = 0;
	Global::get().uiScreenShareMaxHeight = 0;
	Global::get().uiScreenShareMaxFps = 0;
	Global::get().qsScreenShareRelayUrl.clear();
	Global::get().bFeedbackEnabled       = false;
	Global::get().uiFeedbackMaxLogBytes  = Mumble::Feedback::DEFAULT_MAX_LOG_BYTES;
	Global::get().uiFeedbackMaxBodyBytes = Mumble::Feedback::DEFAULT_MAX_BODY_BYTES;
	Global::get().qsServerDisplayName.clear();
	Global::get().qsServerMonogram.clear();
	Global::get().qbaServerImage.clear();

	Global::get().sh->sendPing(); // Send initial ping to establish UDP connection
	appendServerSyncTrace(QStringLiteral("sent-ping"));

	Global::get().pPermissions = ChanACL::Permissions(static_cast< unsigned int >(msg.permissions()));
	Global::get().l->clearIgnore();
	if (msg.has_welcome_text()) {
		QString str = u8(msg.welcome_text());
		setPersistentChatWelcomeText(str);
	}
	pmModel->recheckLinks();
	appendServerSyncTrace(QStringLiteral("post-model-sync"));

	// Reset the mechanism for using and recycling target IDs for setting up
	// VoiceTargets
	qmTargetUse.clear();
	qmTargets.clear();
	const int uniqueTargetIDCount = 5;
	for (int i = 1; i < uniqueTargetIDCount + 1; ++i) {
		qmTargetUse.insert(i, i);
	}
	iTargetCounter = 100;
	appendServerSyncTrace(QStringLiteral("post-target-reset"));

	AudioInput::setMaxBandwidth(static_cast< int >(msg.max_bandwidth()));
	appendServerSyncTrace(QStringLiteral("post-bandwidth"));

	findDesiredChannel();
	appendServerSyncTrace(QStringLiteral("post-find-desired-channel"));

	QString host, uname, pw;
	unsigned short port;

	Global::get().sh->getConnectionInfo(host, port, uname, pw);

	QList< Shortcut > sc = Global::get().db->getShortcuts(Global::get().sh->serverDigest());
	if (!sc.isEmpty()) {
		Global::get().s.qlShortcuts << sc;
		GlobalShortcutEngine::engine->bNeedRemap = true;
	}
	appendServerSyncTrace(QStringLiteral("post-shortcuts"));


	connect(user, SIGNAL(talkingStateChanged()), this, SLOT(userStateChanged()));
	connect(user, SIGNAL(muteDeafStateChanged()), this, SLOT(userStateChanged()));
	connect(user, SIGNAL(prioritySpeakerStateChanged()), this, SLOT(userStateChanged()));
	connect(user, SIGNAL(recordingStateChanged()), this, SLOT(userStateChanged()));
	appendServerSyncTrace(QStringLiteral("post-self-signal-connect"));

	AudioInputPtr audioIn = Global::get().ai;
	if (audioIn) {
		audioIn->updateUserMuteDeafState(user);
		QObject::connect(user, &ClientUser::muteDeafStateChanged, audioIn.get(),
						 &AudioInput::onUserMuteDeafStateChanged);
	}
	appendServerSyncTrace(QStringLiteral("post-audio-update"));

	// Update QActions and menus
	if (qmServer && qmSelf && qmConfig) {
		refreshServerActions();
		refreshSelfActions();
		if (qmChannel && qmUser) {
			qmChannel_aboutToShow();
			qmUser_aboutToShow();
		}
		refreshConfigActions();
	}
	appendServerSyncTrace(QStringLiteral("post-menu-update"));


	Global::get().sh->setServerSynchronized(true);
	mumble::chatperf::fullBootstrapMonitor().enterSteadyState();
	updateChatBar();
	warmupPersistentChatHistory();
	appendServerSyncTrace(QStringLiteral("exit"));

	emit serverSynchronized();
}

/// This message is being received when the server informs this client about server configuration details. This contains
/// things like the maximum bandwidth, the welcome text, whether HTML in messages is allowed, information about message
/// lengths as well as the maximum amount of users that may be connected to this server.
///
/// @param msg The message object
void MainWindow::msgServerConfig(const MumbleProto::ServerConfig &msg) {
	bool persistentGlobalChanged = false;
	bool screenShareConfigChanged = false;
	bool modernLayoutCompatibleAdvertised = false;
	if (msg.has_welcome_text()) {
		QString str = u8(msg.welcome_text());
		setPersistentChatWelcomeText(str);
	}
	if (msg.has_max_bandwidth())
		AudioInput::setMaxBandwidth(static_cast< int >(msg.max_bandwidth()));
	if (msg.has_allow_html())
		Global::get().bAllowHTML = msg.allow_html();
	if (msg.has_message_length())
		Global::get().uiMessageLength = msg.message_length();
	if (msg.has_image_message_length())
		Global::get().uiImageLength = msg.image_message_length();
	if (msg.has_max_users())
		Global::get().uiMaxUsers = msg.max_users();
	if (msg.has_recording_allowed()) {
		Global::get().mw->enableRecording(msg.recording_allowed());
	}
	if (msg.has_persistent_global_chat_enabled()) {
		markPersistentChatAvailable(false);
		const bool enabled = msg.persistent_global_chat_enabled();
		persistentGlobalChanged = Global::get().bPersistentGlobalChatEnabled != enabled;
		Global::get().bPersistentGlobalChatEnabled = enabled;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_persistent_chat_protocol_version()) {
		Global::get().uiPersistentChatProtocolVersion = msg.persistent_chat_protocol_version();
		modernLayoutCompatibleAdvertised              = true;
	}
	if (msg.supported_chat_features_size() > 0) {
		Global::get().qlSupportedChatFeatures = Mumble::ChatFeatures::featuresFromServerConfig(msg);
		if (Mumble::ChatFeatures::contains(Global::get().qlSupportedChatFeatures,
										   MumbleProto::ChatFeaturePersistentHistory)) {
			markPersistentChatAvailable(false);
		}
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.supported_fork_features_size() > 0 || msg.has_fork_extension_protocol_version()) {
		Global::get().qlSupportedForkFeatures      = Mumble::ForkFeatures::featuresFromServerConfig(msg);
		Global::get().uiForkExtensionProtocolVersion =
			msg.has_fork_extension_protocol_version() ? msg.fork_extension_protocol_version() : 0;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_enabled()) {
		Global::get().bScreenShareEnabled = msg.screen_share_enabled();
		screenShareConfigChanged          = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_recording_enabled()) {
		Global::get().bScreenShareRecordingEnabled = msg.screen_share_recording_enabled();
		screenShareConfigChanged                   = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_helper_required()) {
		Global::get().bScreenShareHelperRequired = msg.screen_share_helper_required();
		screenShareConfigChanged                 = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.preferred_screen_share_codecs_size() > 0) {
		Global::get().qlPreferredScreenShareCodecs = preferredScreenShareCodecsFromConfig(msg);
		screenShareConfigChanged                   = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_max_width()) {
		Global::get().uiScreenShareMaxWidth =
			Mumble::ScreenShare::sanitizeLimit(msg.screen_share_max_width(), 0, Mumble::ScreenShare::HARD_MAX_WIDTH);
		screenShareConfigChanged = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_max_height()) {
		Global::get().uiScreenShareMaxHeight =
			Mumble::ScreenShare::sanitizeLimit(msg.screen_share_max_height(), 0, Mumble::ScreenShare::HARD_MAX_HEIGHT);
		screenShareConfigChanged = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_max_fps()) {
		Global::get().uiScreenShareMaxFps =
			Mumble::ScreenShare::sanitizeLimit(msg.screen_share_max_fps(), 0, Mumble::ScreenShare::HARD_MAX_FPS);
		screenShareConfigChanged = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_screen_share_relay_url()) {
		Global::get().qsScreenShareRelayUrl =
			Mumble::ScreenShare::normalizeRelayUrl(u8(msg.screen_share_relay_url()));
		screenShareConfigChanged = true;
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_stonks_enabled()) {
		Global::get().bStonksEnabled = msg.stonks_enabled();
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_stonks_text_channel_id()) {
		Global::get().uiStonksTextChannelID = msg.stonks_text_channel_id();
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_stonks_social_announcements_enabled()) {
		Global::get().bStonksSocialAnnouncementsEnabled = msg.stonks_social_announcements_enabled();
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_feedback_enabled()) {
		Global::get().bFeedbackEnabled = msg.feedback_enabled();
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_feedback_max_log_bytes()) {
		Global::get().uiFeedbackMaxLogBytes =
			qMax(1u, msg.feedback_max_log_bytes());
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_feedback_max_body_bytes()) {
		Global::get().uiFeedbackMaxBodyBytes =
			qMax(1u, msg.feedback_max_body_bytes());
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_server_display_name()) {
		Global::get().qsServerDisplayName = u8(msg.server_display_name()).trimmed().left(128);
		modernLayoutCompatibleAdvertised  = true;
	}
	if (msg.has_server_monogram()) {
		Global::get().qsServerMonogram = u8(msg.server_monogram()).trimmed().left(12);
		modernLayoutCompatibleAdvertised = true;
	}
	if (msg.has_server_image()) {
		Global::get().qbaServerImage = blob(msg.server_image());
		modernLayoutCompatibleAdvertised = true;
	}
	if (screenShareConfigChanged && Global::get().s.bScreenShareDiagnostics) {
		qInfo().noquote()
			<< QStringLiteral("MainWindow: received screen-share ServerConfig enabled=%1 recording=%2 helper_required=%3 "
							  "max=%4x%5@%6 relay_url=%7")
				   .arg(boolToken(Global::get().bScreenShareEnabled), boolToken(Global::get().bScreenShareRecordingEnabled),
						boolToken(Global::get().bScreenShareHelperRequired),
						QString::number(Global::get().uiScreenShareMaxWidth),
						QString::number(Global::get().uiScreenShareMaxHeight),
						QString::number(Global::get().uiScreenShareMaxFps),
						Global::get().qsScreenShareRelayUrl.isEmpty() ? QStringLiteral("-")
																	  : Global::get().qsScreenShareRelayUrl);
		if (m_screenShareManager) {
			m_screenShareManager->logLocalShareAvailabilityDiagnostic(QStringLiteral("server-config"));
		}
	}
	if (modernLayoutCompatibleAdvertised) {
		m_modernLayoutCompatibleServer = true;
	}
	if (modernLayoutCompatibleAdvertised && Settings::LayoutModern != m_activeShellLayout) {
		refreshShellLayout();
	}
	if (persistentGlobalChanged) {
		rebuildPersistentChatChannelList();
		updateChatBar();
		const PersistentChatTarget target = currentPersistentChatTarget();
		if (target.scope == MumbleProto::ServerGlobal || target.scope == MumbleProto::Aggregate) {
			refreshPersistentChatView(true);
		}
	}
	scheduleQmlRoomStateUpdate();
	publishQmlActiveScopeState();
}

/// This message is being received when the server denied the permission to perform a requested action. This function
/// basically informs the user about this denial by printing a message to Mumble's console.
///
/// @param msg The message object containing further details as to why and what Permission has been denied
void MainWindow::msgPermissionDenied(const MumbleProto::PermissionDenied &msg) {
	switch (msg.type()) {
		case MumbleProto::PermissionDenied_DenyType_Permission: {
			VICTIM_INIT;
			SELF_INIT;
			Channel *c = Channel::get(msg.channel_id());
			if (!c)
				return;
			ChanACL::Permissions permission = static_cast< ChanACL::Permissions >(msg.permission());
			QString pname                   = ChanACL::permName(permission);

			if ((permission == ChanACL::Perm::Enter) && c->hasEnterRestrictions.load()) {
				Global::get().l->log(
					Log::PermissionDenied,
					tr("Unable to %1 into %2 - Adding the respective access (password) token might grant you access.")
						.arg(Log::msgColor(pname, Log::Privilege))
						.arg(Log::formatChannel(c)));
			} else {
				QString text;
				if (pDst == pSelf)
					text = tr("You were denied %1 privileges in %2.").arg(Log::msgColor(pname, Log::Privilege))
							   .arg(Log::formatChannel(c));
				else
					text = tr("%3 was denied %1 privileges in %2.").arg(Log::msgColor(pname, Log::Privilege))
							   .arg(Log::formatChannel(c))
							   .arg(Log::formatClientUser(pDst, Log::Target));
				if (msg.has_reason() && !u8(msg.reason()).trimmed().isEmpty()) {
					text += QLatin1Char(' ') + u8(msg.reason()).toHtmlEscaped();
				}
				Global::get().l->log(Log::PermissionDenied, text);
			}
		} break;
		case MumbleProto::PermissionDenied_DenyType_SuperUser: {
			Global::get().l->log(Log::PermissionDenied, tr("Denied: Cannot modify SuperUser."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_ChannelName: {
			Global::get().l->log(
				Log::PermissionDenied,
				tr("Denied: According to the server's configuration, the channel name is considered invalid."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_TextTooLong: {
			Global::get().l->log(Log::PermissionDenied, tr("Denied: Text message too long."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_H9K: {
			if (Global::get().bHappyEaster) {
				bool bold             = Global::get().s.bDeaf;
				bool bold2            = Global::get().s.bTTS;
				Global::get().s.bDeaf = false;
				Global::get().s.bTTS  = true;
				quint32 oflags        = Global::get().s.qmMessages.value(Log::PermissionDenied);
				Global::get().s.qmMessages[Log::PermissionDenied] =
					(oflags | Settings::LogTTS) & static_cast< unsigned int >(~Settings::LogSoundfile);
				Global::get().l->log(Log::PermissionDenied, QString::fromUtf8(Global::get().ccHappyEaster + 39)
																.arg(Global::get().s.qsUsername.toHtmlEscaped()));
				Global::get().s.qmMessages[Log::PermissionDenied] = oflags;
				Global::get().s.bDeaf                             = bold;
				Global::get().s.bTTS                              = bold2;
				qApp->setWindowIcon(QIcon(QString::fromUtf8(Global::get().ccHappyEaster)));
				qWarning() << "Happy Easter";
			}
		} break;
		case MumbleProto::PermissionDenied_DenyType_TemporaryChannel: {
			Global::get().l->log(Log::PermissionDenied, tr("Denied: Operation not permitted in temporary channel."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_MissingCertificate: {
			VICTIM_INIT;
			SELF_INIT;
			if (pDst == pSelf)
				Global::get().l->log(Log::PermissionDenied, tr("You need a certificate to perform this operation."));
			else
				Global::get().l->log(
					Log::PermissionDenied,
					tr("%1 does not have a certificate.").arg(Log::formatClientUser(pDst, Log::Target)));
		} break;
		case MumbleProto::PermissionDenied_DenyType_UserName: {
			if (msg.has_name())
				Global::get().l->log(
					Log::PermissionDenied,
					tr("According to the server's configuration, the username %1 is considered invalid.")
						.arg(u8(msg.name()).toHtmlEscaped()));
			else
				Global::get().l->log(
					Log::PermissionDenied,
					tr("According to the server's configuration, the username is considered invalid."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_ChannelFull: {
			Global::get().l->log(Log::PermissionDenied, tr("Channel is full."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_NestingLimit: {
			Global::get().l->log(Log::PermissionDenied, tr("Channel nesting limit reached."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_ChannelCountLimit: {
			Global::get().l->log(Log::PermissionDenied,
								 tr("Channel count limit reached. Need to delete channels before creating new ones."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_ChannelListenerLimit: {
			Global::get().l->log(Log::PermissionDenied, tr("No more listeners allowed in this channel."));
		} break;
		case MumbleProto::PermissionDenied_DenyType_UserListenerLimit: {
			Global::get().l->log(Log::PermissionDenied,
								 tr("You are not allowed to listen to more channels than you currently are."));
		} break;
		default: {
			if (msg.has_reason())
				Global::get().l->log(Log::PermissionDenied, tr("Denied: %1.").arg(u8(msg.reason()).toHtmlEscaped()));
			else
				Global::get().l->log(Log::PermissionDenied, tr("Permission denied."));
		} break;
	}
}

/// This message is not used (anymore). Thus the implementation does nothing
void MainWindow::msgUDPTunnel(const MumbleProto::UDPTunnel &) {
}

/// This message is being received when the server informs this client about changed users. This might be because there
/// is a new user or because an existing user changed somehow (this includes things like a changed ID, changed name,
/// changed priority speaker status, changed channel, etc.). This function will match the local user representation
/// (UserModel) to these changes.
///
/// @param msg The message object containing the respective information
void MainWindow::msgUserState(const MumbleProto::UserState &msg) {
	ACTOR_INIT;
	ClientUser *pSelf = ClientUser::get(Global::get().uiSession);
	ClientUser *pDst  = ClientUser::get(msg.session());
	Channel *channel  = nullptr;
	const bool traceUserState = qEnvironmentVariableIntValue("MUMBLE_CONNECT_TRACE") != 0;
	const auto appendUserStateTrace = [traceUserState, session = msg.session()](const QString &phase) {
		if (!traceUserState) {
			return;
		}

		QFile traceFile(Global::get().qdBasePath.filePath(QLatin1String("shared-modern-connect-trace.log")));
		if (!traceFile.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
			return;
		}

		QTextStream stream(&traceFile);
		stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << QLatin1Char('Z')
			   << " UserState session=" << session << QLatin1Char(' ') << phase << Qt::endl;
	};
	appendUserStateTrace(QStringLiteral("enter"));
	bool createdUser                             = false;
	bool renamedUser                             = false;
	bool movedChannels                           = false;

	if (msg.has_channel_id()) {
		channel = Channel::get(msg.channel_id());
		if (!channel) {
			qWarning("msgUserState(): unknown channel.");
			channel = Channel::get(Mumble::ROOT_CHANNEL_ID);
		}
	}
	appendUserStateTrace(QStringLiteral("channel-resolved"));

	// User just connected
	if (!pDst) {
		if (!msg.has_name()) {
			return;
		}
		createdUser = true;

		pDst = pmModel->addUser(msg.session(), u8(msg.name()));

		appendUserStateTrace(QStringLiteral("connected-signals"));

		if (channel && channel != pDst->cChannel) {
			pmModel->moveUser(pDst, channel);
		}
		appendUserStateTrace(QStringLiteral("initial-channel-applied"));

		if (msg.has_hash()) {
			pmModel->setHash(pDst, u8(msg.hash()));
		}
		appendUserStateTrace(QStringLiteral("initial-hash-applied"));
		appendUserStateTrace(QStringLiteral("created"));

		if (pSelf) {
			if (pDst->cChannel == pSelf->cChannel) {
				Global::get().l->log(
					Log::ChannelJoinConnect,
					tr("%1 connected and entered channel.").arg(Log::formatClientUser(pDst, Log::Source)));
			} else {
				Global::get().l->log(Log::UserJoin, tr("%1 connected.").arg(Log::formatClientUser(pDst, Log::Source)));
			}
		}
	}
	appendUserStateTrace(QStringLiteral("post-create"));

	if (msg.has_user_id()) {
		pmModel->setUserId(pDst, static_cast< int >(msg.user_id()));
	}
	appendUserStateTrace(QStringLiteral("post-user-id"));

	if (channel) {
		Channel *oldChannel = pDst->cChannel;
		if (channel != oldChannel) {
			movedChannels = true;
			pmModel->moveUser(pDst, channel);

			if (pSelf) {
				if (pDst == pSelf) {
					qsDesiredChannel = channel->getPath();
				}

				if (pDst == pSelf) {
					if (pSrc == pSelf) {
						Global::get().l->log(Log::SelfChannelJoin,
											 tr("You joined %1.").arg(Log::formatChannel(channel)));
					} else {
						Global::get().l->log(Log::SelfChannelJoinOther,
											 tr("You were moved to %1 by %2.")
												 .arg(Log::formatChannel(channel))
												 .arg(Log::formatClientUser(pSrc, Log::Source)));
					}
				} else if (pSrc == pSelf) {
					if (channel == pSelf->cChannel) {
						Global::get().l->log(Log::ChannelJoin, tr("You moved %1 to %2.")
																   .arg(Log::formatClientUser(pDst, Log::Target))
																   .arg(Log::formatChannel(channel)));
					} else {
						Global::get().l->log(Log::ChannelLeave, tr("You moved %1 to %2.")
																	.arg(Log::formatClientUser(pDst, Log::Target))
																	.arg(Log::formatChannel(channel)));
					}
				} else if ((channel == pSelf->cChannel) || (oldChannel == pSelf->cChannel)) {
					if (pDst == pSrc) {
						if (channel == pSelf->cChannel) {
							Global::get().l->log(
								Log::ChannelJoin,
								tr("%1 entered channel.").arg(Log::formatClientUser(pDst, Log::Target)));
						} else {
							Global::get().l->log(Log::ChannelLeave, tr("%1 moved to %2.")
																		.arg(Log::formatClientUser(pDst, Log::Target))
																		.arg(Log::formatChannel(channel)));
						}
					} else {
						if (channel == pSelf->cChannel) {
							Global::get().l->log(Log::ChannelJoin, tr("%1 moved in from %2 by %3.")
																	   .arg(Log::formatClientUser(pDst, Log::Target))
																	   .arg(Log::formatChannel(oldChannel))
																	   .arg(Log::formatClientUser(pSrc, Log::Source)));
						} else {
							Global::get().l->log(Log::ChannelLeave, tr("%1 moved to %2 by %3.")
																		.arg(Log::formatClientUser(pDst, Log::Target))
																		.arg(Log::formatChannel(channel))
																		.arg(Log::formatClientUser(pSrc, Log::Source)));
						}
					}
				}

				if ((channel == pSelf->cChannel) && pDst->bRecording) {
					Global::get().l->log(Log::Recording,
										 tr("%1 is recording").arg(Log::formatClientUser(pDst, Log::Target)));
				}
			}
		}
	}
	appendUserStateTrace(QStringLiteral("post-channel"));

	// Handle channel listening
	for (int i = 0; i < msg.listening_channel_add_size(); i++) {
		Channel *c = Channel::get(msg.listening_channel_add(i));

		if (!c) {
			qWarning("msgUserState(): Invalid channel ID encountered");
			continue;
		}

		if (Global::get().channelListenerManager->isListening(pDst->uiSession, c->iId)) {
			// We are already listening to this channel
			continue;
		}

		Global::get().channelListenerManager->addListener(pDst->uiSession, c->iId);
		emit userAddedChannelListener(pDst, c);

		QString logMsg;
		if (pDst == pSelf) {
			logMsg = tr("You started listening to %1").arg(Log::formatChannel(c));
		} else if (pSelf && pSelf->cChannel == c) {
			logMsg = tr("%1 started listening to your channel").arg(Log::formatClientUser(pDst, Log::Target));
		}

		if (!logMsg.isEmpty()) {
			Global::get().l->log(Log::ChannelListeningAdd, logMsg);
		}
	}
	for (int i = 0; i < msg.listening_channel_remove_size(); i++) {
		Channel *c = Channel::get(msg.listening_channel_remove(i));

		if (!c) {
			qWarning("msgUserState(): Invalid channel ID encountered");
			continue;
		}

		Global::get().channelListenerManager->removeListener(pDst->uiSession, c->iId);
		emit userRemovedChannelListener(pDst, c);

		QString logMsg;
		if (pDst == pSelf) {
			logMsg = tr("You stopped listening to %1").arg(Log::formatChannel(c));
		} else if (pSelf && pSelf->cChannel == c) {
			logMsg = tr("%1 stopped listening to your channel").arg(Log::formatClientUser(pDst, Log::Target));
		}

		if (!logMsg.isEmpty()) {
			Global::get().l->log(Log::ChannelListeningRemove, logMsg);
		}
	}
	for (int i = 0; i < msg.listening_volume_adjustment_size(); i++) {
		unsigned int channelID = msg.listening_volume_adjustment(i).listening_channel();
		float adjustment       = msg.listening_volume_adjustment(i).volume_adjustment();

		const Channel *listenedChannel = Channel::get(channelID);
		if (listenedChannel && pSelf && pSelf->uiSession == pDst->uiSession) {
			Global::get().channelListenerManager->setListenerVolumeAdjustment(pDst->uiSession, listenedChannel->iId,
																			  VolumeAdjustment::fromFactor(adjustment));
		} else if (!listenedChannel) {
			qWarning("msgUserState(): Invalid channel ID encountered in volume adjustment");
		}
	}
	appendUserStateTrace(QStringLiteral("post-listening"));

	if (msg.has_name()) {
		QString oldName = pDst->qsName;
		QString newName = u8(msg.name());
		pmModel->renameUser(pDst, newName);
		renamedUser = !oldName.isNull() && oldName != newName;
		if (!oldName.isNull() && oldName != newName) {
			if (pSrc != pDst) {
				Global::get().l->log(Log::UserRenamed, tr("%1 renamed to %2 by %3.")
														   .arg(Log::formatClientUser(pDst, Log::Target, oldName))
														   .arg(Log::formatClientUser(pDst, Log::Target))
														   .arg(Log::formatClientUser(pSrc, Log::Source)));
			} else {
				Global::get().l->log(Log::UserRenamed, tr("%1 renamed to %2.")
														   .arg(Log::formatClientUser(pDst, Log::Target, oldName),
																Log::formatClientUser(pDst, Log::Target)));
			}
		}
	}
	appendUserStateTrace(QStringLiteral("post-name"));

	if (!pDst->qsHash.isEmpty()) {
		const QString &name = Global::get().db->getFriend(pDst->qsHash);
		if (!name.isEmpty()) {
			pmModel->setFriendName(pDst, name);
		}
		if (Global::get().db->isLocalMuted(pDst->qsHash))
			pDst->setLocalMute(true);
		if (Global::get().db->isLocalIgnored(pDst->qsHash))
			pDst->setLocalIgnore(true);
		if (Global::get().db->isLocalIgnoredTTS(pDst->qsHash))
			pDst->setLocalIgnoreTTS(true);
		const std::optional< bool > remoteSpeechCleanup = Global::get().db->getUserRemoteSpeechCleanup(pDst->qsHash);
		if (remoteSpeechCleanup.has_value()) {
			pDst->setRemoteSpeechCleanupOverride(remoteSpeechCleanup);
		}
		pDst->setLocalVolumeAdjustment(Global::get().db->getUserLocalVolume(pDst->qsHash));
		pDst->setLocalNickname(Global::get().db->getUserLocalNickname(pDst->qsHash));
	}
	appendUserStateTrace(QStringLiteral("post-local-flags"));

	if (msg.has_self_deaf() || msg.has_self_mute()) {
		if (msg.has_self_mute()) {
			pDst->setSelfMute(msg.self_mute());
		}

		if (msg.has_self_deaf()) {
			pDst->setSelfDeaf(msg.self_deaf());
		}

		if (pSelf && pDst != pSelf && pDst->cChannel == pSelf->cChannel) {
			if (pDst->bSelfMute && pDst->bSelfDeaf) {
				Global::get().l->log(Log::OtherSelfMute,
									 tr("%1 is now muted and deafened.").arg(Log::formatClientUser(pDst, Log::Target)));
			} else if (pDst->bSelfMute) {
				Global::get().l->log(Log::OtherSelfMute,
									 tr("%1 is now muted.").arg(Log::formatClientUser(pDst, Log::Target)));
			} else {
				Global::get().l->log(Log::OtherSelfMute,
									 tr("%1 is now unmuted.").arg(Log::formatClientUser(pDst, Log::Target)));
			}
		}
	}
	appendUserStateTrace(QStringLiteral("post-self-mute"));

	if (msg.has_recording()) {
		pDst->setRecording(msg.recording());

		// Do nothing during initial sync
		if (pSelf) {
			if (pDst == pSelf) {
				if (pDst->bRecording) {
					Global::get().l->log(Log::Recording, tr("Recording started"));
				} else {
					Global::get().l->log(Log::Recording, tr("Recording stopped"));
				}
			} else if (pDst->cChannel->allLinks().contains(pSelf->cChannel)) {
				if (pDst->bRecording) {
					Global::get().l->log(Log::Recording,
										 tr("%1 started recording.").arg(Log::formatClientUser(pDst, Log::Source)));
				} else {
					Global::get().l->log(Log::Recording,
										 tr("%1 stopped recording.").arg(Log::formatClientUser(pDst, Log::Source)));
				}
			}
		}
	}
	appendUserStateTrace(QStringLiteral("post-recording"));

	if (msg.has_priority_speaker()) {
		if (pSelf
			&& ((pDst->cChannel == pSelf->cChannel) || (pDst->cChannel->allLinks().contains(pSelf->cChannel))
				|| (pSrc == pSelf))) {
			if ((pSrc == pDst) && (pSrc == pSelf)) {
				if (pDst->bPrioritySpeaker) {
					Global::get().l->log(Log::YouMuted, tr("You revoked your priority speaker status."));
				} else {
					Global::get().l->log(Log::YouMuted, tr("You assumed priority speaker status."));
				}
			} else if ((pSrc != pSelf) && (pDst == pSelf)) {
				if (pDst->bPrioritySpeaker) {
					Global::get().l->log(
						Log::YouMutedOther,
						tr("%1 revoked your priority speaker status.").arg(Log::formatClientUser(pSrc, Log::Source)));
				} else {
					Global::get().l->log(
						Log::YouMutedOther,
						tr("%1 gave you priority speaker status.").arg(Log::formatClientUser(pSrc, Log::Source)));
				}
			} else if ((pSrc == pSelf) && (pSrc != pDst)) {
				if (pDst->bPrioritySpeaker) {
					Global::get().l->log(Log::YouMutedOther, tr("You revoked priority speaker status for %1.")
																 .arg(Log::formatClientUser(pDst, Log::Target)));
				} else {
					Global::get().l->log(
						Log::YouMutedOther,
						tr("You gave priority speaker status to %1.").arg(Log::formatClientUser(pDst, Log::Target)));
				}
			} else if ((pSrc == pDst) && (pSrc != pSelf)) {
				if (pDst->bPrioritySpeaker) {
					Global::get().l->log(
						Log::OtherMutedOther,
						tr("%1 revoked own priority speaker status.").arg(Log::formatClientUser(pSrc, Log::Source)));
				} else {
					Global::get().l->log(
						Log::OtherMutedOther,
						tr("%1 assumed priority speaker status.").arg(Log::formatClientUser(pSrc, Log::Source)));
				}
			} else if ((pSrc != pSelf) && (pDst != pSelf)) {
				if (pDst->bPrioritySpeaker) {
					Global::get().l->log(Log::OtherMutedOther, tr("%1 revoked priority speaker status for %2.")
																   .arg(Log::formatClientUser(pSrc, Log::Source),
																		Log::formatClientUser(pDst, Log::Target)));
				} else if (!pDst->bPrioritySpeaker) {
					Global::get().l->log(Log::OtherMutedOther, tr("%1 gave priority speaker status to %2.")
																   .arg(Log::formatClientUser(pSrc, Log::Source),
																		Log::formatClientUser(pDst, Log::Target)));
				}
			}
		}

		pDst->setPrioritySpeaker(msg.priority_speaker());
	}
	appendUserStateTrace(QStringLiteral("post-priority"));

	if (msg.has_deaf() || msg.has_mute() || msg.has_suppress()) {
		if (msg.has_mute())
			pDst->setMute(msg.mute());
		if (msg.has_deaf())
			pDst->setDeaf(msg.deaf());
		if (msg.has_suppress())
			pDst->setSuppress(msg.suppress());

		if (pSelf
			&& ((pDst->cChannel == pSelf->cChannel) || (pDst->cChannel->allLinks().contains(pSelf->cChannel))
				|| (pSrc == pSelf))) {
			if (pDst == pSelf) {
				if (msg.has_mute() && msg.has_deaf() && pDst->bMute && pDst->bDeaf) {
					Global::get().l->log(
						Log::YouMuted,
						tr("You were muted and deafened by %1.").arg(Log::formatClientUser(pSrc, Log::Source)));
				} else if (msg.has_mute() && msg.has_deaf() && !pDst->bMute && !pDst->bDeaf) {
					Global::get().l->log(
						Log::YouMuted,
						tr("You were unmuted and undeafened by %1.").arg(Log::formatClientUser(pSrc, Log::Source)));
				} else {
					if (msg.has_mute()) {
						if (pDst->bMute)
							Global::get().l->log(
								Log::YouMuted,
								tr("You were muted by %1.").arg(Log::formatClientUser(pSrc, Log::Source)));
						else
							Global::get().l->log(
								Log::YouMuted,
								tr("You were unmuted by %1.").arg(Log::formatClientUser(pSrc, Log::Source)));
					}

					if (msg.has_deaf()) {
						if (!pDst->bDeaf)
							Global::get().l->log(
								Log::YouMuted,
								tr("You were undeafened by %1.").arg(Log::formatClientUser(pSrc, Log::Source)));
					}
				}

				if (msg.has_suppress()) {
					if (pDst->bSuppress)
						Global::get().l->log(Log::YouMuted, tr("You were suppressed."));
					else {
						if (msg.has_channel_id())
							Global::get().l->log(Log::YouMuted, tr("You were unsuppressed."));
						else
							Global::get().l->log(
								Log::YouMuted,
								tr("You were unsuppressed by %1.").arg(Log::formatClientUser(pSrc, Log::Source)));
					}
				}
			} else if (pSrc == pSelf) {
				if (msg.has_mute() && msg.has_deaf() && pDst->bMute && pDst->bDeaf) {
					Global::get().l->log(
						Log::YouMutedOther,
						tr("You muted and deafened %1.").arg(Log::formatClientUser(pDst, Log::Target)));
				} else if (msg.has_mute() && msg.has_deaf() && !pDst->bMute && !pDst->bDeaf) {
					Global::get().l->log(
						Log::YouMutedOther,
						tr("You unmuted and undeafened %1.").arg(Log::formatClientUser(pDst, Log::Target)));
				} else {
					if (msg.has_mute()) {
						if (pDst->bMute)
							Global::get().l->log(Log::YouMutedOther,
												 tr("You muted %1.").arg(Log::formatClientUser(pDst, Log::Target)));
						else
							Global::get().l->log(Log::YouMutedOther,
												 tr("You unmuted %1.").arg(Log::formatClientUser(pDst, Log::Target)));
					}

					if (msg.has_deaf()) {
						if (!pDst->bDeaf)
							Global::get().l->log(
								Log::YouMutedOther,
								tr("You undeafened %1.").arg(Log::formatClientUser(pDst, Log::Target)));
					}
				}

				if (msg.has_suppress()) {
					if (!msg.has_channel_id()) {
						if (pDst->bSuppress)
							Global::get().l->log(
								Log::YouMutedOther,
								tr("You suppressed %1.").arg(Log::formatClientUser(pDst, Log::Target)));
						else
							Global::get().l->log(
								Log::YouMutedOther,
								tr("You unsuppressed %1.").arg(Log::formatClientUser(pDst, Log::Target)));
					}
				}
			} else {
				if (msg.has_mute() && msg.has_deaf() && pDst->bMute && pDst->bDeaf) {
					Global::get().l->log(Log::OtherMutedOther, tr("%1 muted and deafened by %2.")
																   .arg(Log::formatClientUser(pDst, Log::Target),
																		Log::formatClientUser(pSrc, Log::Source)));
				} else if (msg.has_mute() && msg.has_deaf() && !pDst->bMute && !pDst->bDeaf) {
					Global::get().l->log(Log::OtherMutedOther, tr("%1 unmuted and undeafened by %2.")
																   .arg(Log::formatClientUser(pDst, Log::Target),
																		Log::formatClientUser(pSrc, Log::Source)));
				} else {
					if (msg.has_mute()) {
						if (pDst->bMute)
							Global::get().l->log(Log::OtherMutedOther,
												 tr("%1 muted by %2.")
													 .arg(Log::formatClientUser(pDst, Log::Target),
														  Log::formatClientUser(pSrc, Log::Source)));
						else
							Global::get().l->log(Log::OtherMutedOther,
												 tr("%1 unmuted by %2.")
													 .arg(Log::formatClientUser(pDst, Log::Target),
														  Log::formatClientUser(pSrc, Log::Source)));
					}

					if (msg.has_deaf()) {
						if (!pDst->bDeaf)
							Global::get().l->log(Log::OtherMutedOther,
												 tr("%1 undeafened by %2.")
													 .arg(Log::formatClientUser(pDst, Log::Target),
														  Log::formatClientUser(pSrc, Log::Source)));
					}
				}

				if (msg.has_suppress()) {
					if (!msg.has_channel_id()) {
						if (pDst->bSuppress)
							Global::get().l->log(Log::OtherMutedOther,
												 tr("%1 suppressed by %2.")
													 .arg(Log::formatClientUser(pDst, Log::Target),
														  Log::formatClientUser(pSrc, Log::Source)));
						else
							Global::get().l->log(Log::OtherMutedOther,
												 tr("%1 unsuppressed by %2.")
													 .arg(Log::formatClientUser(pDst, Log::Target),
														  Log::formatClientUser(pSrc, Log::Source)));
					}
				}
			}
		}
	}
	appendUserStateTrace(QStringLiteral("post-server-mute"));

	const bool textureChanged = msg.has_texture_hash() || msg.has_texture();
	const bool commentChanged = msg.has_comment_hash() || msg.has_comment();

	if (msg.has_texture_hash()) {
		handleUserTextureHash(pDst, blob(msg.texture_hash()));
	}
	if (msg.has_texture()) {
		handleUserTextureBlob(pDst, blob(msg.texture()));
	}
	if (msg.has_comment_hash())
		pmModel->setCommentHash(pDst, blob(msg.comment_hash()));
	if (msg.has_comment()) {
		pmModel->setComment(pDst, u8(msg.comment()));
	}
	if (commentChanged) {
		clearUserCommentRequest(pDst->uiSession);
	}
	appendUserStateTrace(QStringLiteral("post-comment"));

	{
		const PersistentChatTarget activeTarget = currentPersistentChatTarget();
		const bool activeDirectMessageAffected =
			activeTarget.directMessage && activeTarget.user && activeTarget.user->uiSession == pDst->uiSession;
		const bool rebuildConversationList = createdUser || renamedUser || (movedChannels && pDst == pSelf);

		if (rebuildConversationList) {
			rebuildPersistentChatChannelList();
		}
		if (rebuildConversationList || movedChannels || activeDirectMessageAffected) {
			updateMenuPermissions();
			if (!rebuildConversationList) {
				publishQmlParticipantState(pDst);
			}
		}
		if (textureChanged || commentChanged) {
			publishQmlParticipantState(pDst);
		}
	}
	appendUserStateTrace(QStringLiteral("exit"));
}

/// This message is being received when a user was removed. This might be because the user disconnected or because
/// of a kick/ban. The affected user might be the local user.
/// This function will update the local user representation (UserModel) to match these removals and potentially inform
/// the local user about a kick/ban.
///
/// @param msg The message object containing further information
void MainWindow::msgUserRemove(const MumbleProto::UserRemove &msg) {
	VICTIM_INIT;
	ACTOR_INIT;
	SELF_INIT;

	QString reason = u8(msg.reason()).toHtmlEscaped();

	if (pDst == pSelf) {
		bRetryServer = false;
		if (msg.ban())
			Global::get().l->log(Log::YouKicked, tr("You were kicked and banned from the server by %1: %2.")
													 .arg(Log::formatClientUser(pSrc, Log::Source))
													 .arg(reason));
		else
			Global::get().l->log(Log::YouKicked, tr("You were kicked from the server by %1: %2.")
													 .arg(Log::formatClientUser(pSrc, Log::Source))
													 .arg(reason));
	} else if (pSrc) {
		if (msg.ban())
			Global::get().l->log((pSrc == pSelf) ? Log::YouKicked : Log::UserKicked,
								 tr("%3 was kicked and banned from the server by %1: %2.")
									 .arg(Log::formatClientUser(pSrc, Log::Source))
									 .arg(reason)
									 .arg(Log::formatClientUser(pDst, Log::Target)));
		else
			Global::get().l->log((pSrc == pSelf) ? Log::YouKicked : Log::UserKicked,
								 tr("%3 was kicked from the server by %1: %2.")
									 .arg(Log::formatClientUser(pSrc, Log::Source))
									 .arg(reason)
									 .arg(Log::formatClientUser(pDst, Log::Target)));
	} else {
		if (pDst->cChannel == pSelf->cChannel || pDst->cChannel->allLinks().contains(pSelf->cChannel)) {
			Global::get().l->log(Log::ChannelLeaveDisconnect,
								 tr("%1 left channel and disconnected.").arg(Log::formatClientUser(pDst, Log::Source)));
		} else {
			Global::get().l->log(Log::UserLeave, tr("%1 disconnected.").arg(Log::formatClientUser(pDst, Log::Source)));
		}
	}

	if (pDst != pSelf) {
		clearUserTextureRequest(pDst->uiSession);
		pmModel->removeUser(pDst);
		rebuildPersistentChatChannelList();
		updateMenuPermissions();
	}
}

/// This message is being received when the server informs the local client about channel properties (either during
/// connection/login to the server or whenever these properties changed).
///
/// @param msg The message object containing the details about the channel properties
void MainWindow::msgChannelState(const MumbleProto::ChannelState &msg) {
	if (!msg.has_channel_id())
		return;

	Channel *c = Channel::get(msg.channel_id());
	Channel *p = msg.has_parent() ? Channel::get(msg.parent()) : nullptr;

	if (!c) {
		// Addresses channel does not exist so create it
		if (p && msg.has_name()) {
			c = pmModel->addChannel(msg.channel_id(), p, u8(msg.name()));
			if (!c) {
				qWarning("Server attempted to create an invalid or duplicate channel");
				return;
			}
			c->bTemporary = msg.temporary();
			p             = nullptr; // No need to move it later

			ServerHandlerPtr sh = Global::get().sh;
			if (sh) {
				c->m_filterMode = Global::get().db->getChannelFilterMode(sh->serverDigest(), c->iId);
			}

		} else {
			qWarning("Server attempted state change on nonexistent channel");
			return;
		}
	}

	if (p) {
		// Channel move
		Channel *pp = p;
		while (pp) {
			if (pp == c) {
				qWarning("Server asked to move a channel into itself or one of its children");
				return;
			}

			pp = pp->cParent;
		}
		pmModel->moveChannel(c, p);
	}

	if (msg.has_name()) {
		pmModel->renameChannel(c, u8(msg.name()));
	}

	if (msg.has_description_hash()) {
		pmModel->setCommentHash(c, blob(msg.description_hash()));
	}
	if (msg.has_description()) {
		pmModel->setComment(c, u8(msg.description()));
	}

	if (msg.has_position()) {
		pmModel->repositionChannel(c, msg.position());
	}

	if (msg.links_size()) {
		QList< Channel * > ql;
		pmModel->unlinkAll(c);
		for (int i = 0; i < msg.links_size(); ++i) {
			Channel *l = Channel::get(msg.links(i));
			if (l)
				ql << l;
		}
		if (!ql.isEmpty()) {
			pmModel->linkChannels(c, ql);
		}
	}
	if (msg.links_remove_size()) {
		QList< Channel * > ql;
		for (int i = 0; i < msg.links_remove_size(); ++i) {
			Channel *l = Channel::get(msg.links_remove(i));
			if (l)
				ql << l;
		}
		if (!ql.isEmpty()) {
			pmModel->unlinkChannels(c, ql);
		}
	}
	if (msg.links_add_size()) {
		QList< Channel * > ql;
		for (int i = 0; i < msg.links_add_size(); ++i) {
			Channel *l = Channel::get(msg.links_add(i));
			if (l)
				ql << l;
		}
		if (!ql.isEmpty()) {
			pmModel->linkChannels(c, ql);
		}
	}

	if (msg.has_max_users()) {
		c->uiMaxUsers = msg.max_users();
	}

	bool forceUpdateTree = false;

	if (msg.has_is_enter_restricted()) {
		c->hasEnterRestrictions.store(msg.is_enter_restricted());
		forceUpdateTree = true;
	}

	if (msg.has_can_enter()) {
		c->localUserCanEnter.store(msg.can_enter());
		forceUpdateTree = true;
	}

	emit channelStateChanged(c, forceUpdateTree);
}

void MainWindow::msgChannelRemove(const MumbleProto::ChannelRemove &msg) {
	Channel *c = Channel::get(msg.channel_id());
	if (c && (c->iId != 0)) {
		c->clearFilterMode();
		if (!pmModel->removeChannel(c, true)) {
			Global::get().l->log(Log::CriticalError,
								 tr("Protocol violation. Server sent remove for occupied channel."));
			Global::get().sh->disconnect();
			return;
		}
	}
}

/// This message is being received because the local client received a text message that should be displayed to
/// the user - which is what this function does.
///
/// @param msg The message object that contains information about the received text message
void MainWindow::msgTextMessage(const MumbleProto::TextMessage &msg) {
	ACTOR_INIT;
	QString target;

	// Silently drop the message if this user is set to "ignore"
	if (pSrc && pSrc->bLocalIgnore)
		return;

	const QString &plainName = pSrc ? pSrc->qsName : tr("Server", "message from");
	const QString &name      = pSrc ? Log::formatClientUser(pSrc, Log::Source) : tr("Server", "message from");
	bool privateMessage      = false;

	if (msg.tree_id_size() > 0) {
		target += tr("Tree");
	} else if (msg.channel_id_size() > 0) {
		target += tr("Channel");
	} else if (msg.session_size() > 0) {
		target += tr("Private");
		privateMessage = true;
	}

	// If NoScope or NoAuthor is selected generate a new string to pass to TTS
	const QString overrideTTS = [&]() {
		if (!Global::get().s.bTTSNoScope && !Global::get().s.bTTSNoAuthor) {
			return QString();
		}
		const QString plainMessage = QTextDocumentFragment::fromHtml(u8(msg.message())).toPlainText();
		if (Global::get().s.bTTSNoScope && Global::get().s.bTTSNoAuthor) {
			return plainMessage;
		}
		const QString prefixTTS = Global::get().s.bTTSNoScope ? plainName : target;
		return tr("%1: %2").arg(prefixTTS).arg(plainMessage);
	}();

	const QString prefixMessage = target.isEmpty() ? name : tr("(%1) %2").arg(target).arg(name);

	Global::get().l->log(privateMessage ? Log::PrivateTextMessage : Log::TextMessage,
						 tr("%1: %2").arg(prefixMessage).arg(u8(msg.message())), tr("Message from %1").arg(plainName),
						 false, overrideTTS, pSrc ? pSrc->bLocalIgnoreTTS : false);

	if (true && privateMessage && pSrc) {
		appendModernDirectMessage(pSrc->uiSession, u8(msg.message()), false);
	}
}

/// This message is being received when the server informs the client about the access control list (ACL) for
/// a channel or multiple channels. It seems like this message will only be received after having queried it.
///
/// @param msg The message object holding the ACL and further details
void MainWindow::msgACL(const MumbleProto::ACL &msg) {
	openModernAclDialog(msg);
}

/// This message is being received when the server informs the local client about user information. This message will
/// only be received after being explicitly queried by the local client.
///
/// @param msg The message object with the respective information
void MainWindow::msgQueryUsers(const MumbleProto::QueryUsers &msg) {
	if (handleModernAclQueryUsers(msg)) {
		return;
	}
}

/// Pings are a method to check the server-client connection. This implementation does nothing.
void MainWindow::msgPing(const MumbleProto::Ping &) {
}

void MainWindow::msgCryptSetup(const MumbleProto::CryptSetup &msg) {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (serverHandler) {
		serverHandler->applyCryptSetup(msg);
	}
}

/// This messages is only sent by the client if it wants to instantiate a context action. Thus this implementation
/// does nothing.
void MainWindow::msgContextAction(const MumbleProto::ContextAction &) {
}

/// This message is being received if the server wants to instruct the client to add or remove a given context action.
///
/// @param msg The message object with further details about the respective context action
///
/// @see MainWindow::removeContextAction
void MainWindow::msgContextActionModify(const MumbleProto::ContextActionModify &msg) {
	if (msg.has_operation() && msg.operation() == MumbleProto::ContextActionModify_Operation_Remove) {
		removeContextAction(msg);
		updateMenuPermissions();
		return;
	}

	if (msg.has_operation() && msg.operation() != MumbleProto::ContextActionModify_Operation_Add)
		return;

	QAction *a = new QAction(u8(msg.text()), Global::get().mw);
	a->setData(u8(msg.action()));
	connect(a, SIGNAL(triggered()), this, SLOT(context_triggered()));
	unsigned int ctx = msg.context();
	if (ctx & MumbleProto::ContextActionModify_Context_Server)
		qlServerActions.append(a);
	if (ctx & MumbleProto::ContextActionModify_Context_User)
		qlUserActions.append(a);
	if (ctx & MumbleProto::ContextActionModify_Context_Channel)
		qlChannelActions.append(a);
	updateMenuPermissions();
}

/// Helper method for removing a context action.
///
/// @param msg The message object instructing the deletion of the action with further information about it
///
/// @see MainWindow::msgContextActionModify
void MainWindow::removeContextAction(const MumbleProto::ContextActionModify &msg) {
	QString action = u8(msg.action());

	QSet< QAction * > qs;
	qs += QSet< QAction * >(qlServerActions.begin(), qlServerActions.end());
	qs += QSet< QAction * >(qlChannelActions.begin(), qlChannelActions.end());
	qs += QSet< QAction * >(qlUserActions.begin(), qlUserActions.end());

	for (QAction *a : qs) {
		if (a->data() == action) {
			qlServerActions.removeOne(a);
			qlChannelActions.removeOne(a);
			qlUserActions.removeOne(a);
			delete a;
		}
	}
}

/// This message is being received in order to set the version information of this client.
///
/// @param msg The message object with the respective information
void MainWindow::msgVersion(const MumbleProto::Version &msg) {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (msg.supported_chat_features_size() > 0
		|| (msg.has_supports_persistent_chat() && msg.supports_persistent_chat())) {
		Global::get().qlSupportedChatFeatures = Mumble::ChatFeatures::featuresFromVersion(msg);
		if (Mumble::ChatFeatures::contains(Global::get().qlSupportedChatFeatures,
										   MumbleProto::ChatFeaturePersistentHistory)) {
			Global::get().uiPersistentChatProtocolVersion = Mumble::ChatFeatures::CURRENT_PROTOCOL_VERSION;
			markPersistentChatAvailable(false);
			m_modernLayoutCompatibleServer = true;
		}
	}
	if (msg.supported_fork_features_size() > 0 || msg.has_fork_extension_protocol_version()) {
		Global::get().qlSupportedForkFeatures      = Mumble::ForkFeatures::featuresFromVersion(msg);
		Global::get().uiForkExtensionProtocolVersion =
			msg.has_fork_extension_protocol_version() ? msg.fork_extension_protocol_version() : 0;
		m_modernLayoutCompatibleServer = true;
	}

	if (serverHandler) {
		serverHandler->setServerIdentityDetails(msg.has_release() ? u8(msg.release()) : QString(),
											msg.has_os() ? u8(msg.os()) : QString(),
											msg.has_os() && msg.has_os_version() ? u8(msg.os_version()) : QString());
	}
}

/// This message is being received if the client has queried for the list of all users.
///
/// @param msg The message object containing the user list
void MainWindow::msgUserList(const MumbleProto::UserList &msg) {
	if (handleModernAclUserList(msg)) {
		return;
	}
	openModernServerUserListDialog(msg);
}

/// This message is only sent by the client in order to register/clear whisper targets. Therefore
/// this implementation does nothing.
void MainWindow::msgVoiceTarget(const MumbleProto::VoiceTarget &) {
}

/// This message is being received as an answer to the request for certain permissions or if the
/// server wants the client to resync all channel permissions.
///
/// @param msg The message object containing the respective information
void MainWindow::msgPermissionQuery(const MumbleProto::PermissionQuery &msg) {
	const auto selectedVoiceChannel = selectedModernVoiceChannel();
	Channel *current = selectedVoiceChannel ? Channel::get(*selectedVoiceChannel) : nullptr;
	const PersistentChatTarget activeChatTarget = currentPersistentChatTarget();
	const bool activeChatTargetMatchesPermissionChannel =
		activeChatTarget.valid && !activeChatTarget.serverLog && !activeChatTarget.directMessage
		&& !activeChatTarget.ephemeralTextPath && activeChatTarget.channel
		&& activeChatTarget.channel->iId == msg.channel_id();
	const ChanACL::Permissions previousActiveChatPermissions =
		activeChatTargetMatchesPermissionChannel
			? static_cast< ChanACL::Permissions >(activeChatTarget.channel->uiPermissions)
			: ChanACL::None;

	if (msg.flush()) {
		for (Channel *c : Channel::c_qhChannels) {
			c->uiPermissions = 0;
		}

		// We always need the permissions of the current focus channel
		if (current && current->iId != msg.channel_id()) {
			Global::get().sh->requestChannelPermissions(current->iId);

			current->uiPermissions = ChanACL::All;
		}
	}
	Channel *c = Channel::get(msg.channel_id());
	if (c) {
		c->uiPermissions = msg.permissions();
		if (c->iId == 0)
			Global::get().pPermissions = static_cast< ChanACL::Permissions >(c->uiPermissions);
		if (c == current) updateMenuPermissions();
		scheduleQmlRoomStateUpdate();
		if (activeChatTargetMatchesPermissionChannel && activeChatTarget.channel == c
			&& previousActiveChatPermissions != static_cast< ChanACL::Permissions >(c->uiPermissions)) {
			refreshPersistentChatView(true);
		}
	}
}

/// This message is being received in order for the server to instruct this client which codec it should use.
///
/// @param msg The message object
void MainWindow::msgCodecVersion(const MumbleProto::CodecVersion &msg) {
	if (!msg.opus()) {
		Global::get().l->log(Log::CriticalError, tr("Server instructed us to use an audio codec different from Opus, "
													"which is no longer supported. Disconnecting..."));

		Global::get().sh->disconnect();
	}
}

/// This message is being received in order to communicate user stats from the server to the client.
///
/// @param msg The message object containing the stats
void MainWindow::msgUserStats(const MumbleProto::UserStats &msg) {
	if (msg.has_session()) {
		const std::optional< unsigned int > idleSeconds =
			msg.has_idlesecs() ? std::optional< unsigned int >(msg.idlesecs()) : std::nullopt;
		const auto previousIdleSeconds = userIdleSeconds(msg.session());
		if (idleSeconds) {
			m_userIdleSeconds.insert(msg.session(), *idleSeconds);
		} else {
			m_userIdleSeconds.remove(msg.session());
		}

		if (idleSeconds != previousIdleSeconds && pmModel) {
			if (ClientUser *user = ClientUser::get(msg.session()); user) {
				const QModelIndex idx = pmModel->index(user);
				if (idx.isValid()) {
					emit pmModel->dataChanged(idx, idx);
				}
			}
		}
	}

	if (msg.stats_only()) {
		return;
	}

	if (!m_pendingUserInformationSessions.remove(msg.session())) {
		return;
	}

	openModernUserInformationDialog(msg);
}

/// This message is only ever sent by the client in order to request binary data that otherwise
/// wouldn't be included in the normal messages (e.Global::get(). big images). Thus this implementation does
/// nothing.
void MainWindow::msgRequestBlob(const MumbleProto::RequestBlob &) {
}

/// This message is being received when the server wants to inform the client about suggested client configurations
/// made by the server administrator. These suggestions will be logged to Mumble's console (if unmet).
///
/// @param msg The message object containing the suggestions
void MainWindow::msgSuggestConfig(const MumbleProto::SuggestConfig &msg) {
	Version::full_t requestedVersion = MumbleProto::getSuggestedVersion(msg);
	if (requestedVersion <= Version::get()) {
		requestedVersion = Version::UNKNOWN;
	}
	if (requestedVersion != Version::UNKNOWN) {
		Global::get().l->log(
			Log::Warning, tr("The server requests minimum client version %1").arg(Version::toString(requestedVersion)));
	}
	if (msg.has_positional() && (msg.positional() != Global::get().s.doPositionalAudio())) {
		if (msg.positional())
			Global::get().l->log(Log::Warning, tr("The server requests positional audio be enabled."));
		else
			Global::get().l->log(Log::Warning, tr("The server requests positional audio be disabled."));
	}
	if (msg.has_push_to_talk() && (msg.push_to_talk() != (Global::get().s.atTransmit == Settings::PushToTalk))) {
		if (msg.push_to_talk())
			Global::get().l->log(Log::Warning, tr("The server requests Push-to-Talk be enabled."));
		else
			Global::get().l->log(Log::Warning, tr("The server requests Push-to-Talk be disabled."));
	}
}

void MainWindow::msgPluginDataTransmission(const MumbleProto::PluginDataTransmission &msg) {
	// Another client's plugin has sent us some data. Verify the necessary parts are there and delegate it to the
	// PluginManager

	if (!msg.has_sendersession() || !msg.has_data() || !msg.has_dataid()) {
		// if the message contains no sender session, no data or no ID for the data, it is of no use to us and we
		// discard it
		return;
	}

	const ClientUser *sender   = ClientUser::get(msg.sendersession());
	const std::string &msgData = msg.data();

	if (sender) {
		static_assert(sizeof(unsigned char) == sizeof(uint8_t), "Unsigned char does not have expected 8bit size");
		// As long as above assertion is true, we are only casting away the sign, which is fine
		Global::get().pluginManager->on_receiveData(sender, reinterpret_cast< const uint8_t * >(msgData.c_str()),
													msgData.size(), msg.dataid().c_str());
	}
}

void MainWindow::msgChatSend(const MumbleProto::ChatSend &) {
}

void MainWindow::msgChatMessage(const MumbleProto::ChatMessage &msg) {
	markPersistentChatAvailable();
	m_persistentChatLiveMessageKeys.insert(persistentChatMessageIdentityKey(msg));
	handlePersistentChatMessage(msg);
}

void MainWindow::msgChatMessageDelete(const MumbleProto::ChatMessageDelete &) {
}

void MainWindow::msgChatHistoryRequest(const MumbleProto::ChatHistoryRequest &) {
}

void MainWindow::msgChatHistoryWarmupRequest(const MumbleProto::ChatHistoryWarmupRequest &) {
}

void MainWindow::msgChatHistoryResponse(const MumbleProto::ChatHistoryResponse &msg) {
	markPersistentChatAvailable();
	handlePersistentChatHistory(msg);
}

void MainWindow::msgChatReadStateUpdate(const MumbleProto::ChatReadStateUpdate &msg) {
	markPersistentChatAvailable();
	handlePersistentChatReadState(msg);
}

void MainWindow::msgChatAssetUploadInit(const MumbleProto::ChatAssetUploadInit &) {
}

void MainWindow::msgChatAssetUploadChunk(const MumbleProto::ChatAssetUploadChunk &) {
}

void MainWindow::msgChatAssetUploadCommit(const MumbleProto::ChatAssetUploadCommit &) {
}

void MainWindow::msgChatAssetState(const MumbleProto::ChatAssetState &) {
}

void MainWindow::msgChatAssetRequest(const MumbleProto::ChatAssetRequest &) {
}

void MainWindow::msgChatAssetChunk(const MumbleProto::ChatAssetChunk &msg) {
	mumble::chatperf::ScopedDuration trace("chat.asset_chunk");
	if (!msg.has_asset_id() || msg.asset_id() == 0) {
		return;
	}

	auto it = m_persistentChatAssetDownloads.find(msg.asset_id());
	if (it == m_persistentChatAssetDownloads.end()) {
		PersistentChatAssetDownload download;
		download.assetID = msg.asset_id();
		it = m_persistentChatAssetDownloads.insert(msg.asset_id(), download);
	}

	if (msg.has_offset() && msg.offset() != static_cast< quint64 >(it->bytes.size())) {
		return;
	}

	if (msg.has_total_size()) {
		it->totalSize = msg.total_size();
	}
	if (msg.has_mime()) {
		it->mime = normalizedChatAssetMime(u8(msg.mime()));
	}
	if (msg.has_kind()) {
		it->kind = msg.kind();
	}
	if (msg.has_data() && !msg.data().empty()) {
		it->bytes.append(msg.data().data(), static_cast< int >(msg.data().size()));
	}
	it->nextOffset = static_cast< quint64 >(it->bytes.size());

	const bool complete = (msg.has_eof() && msg.eof())
						  || (it->totalSize > 0 && static_cast< quint64 >(it->bytes.size()) >= it->totalSize);
	if (!complete) {
		if (Global::get().sh && Global::get().sh->isRunning()) {
			MumbleProto::ChatAssetRequest request;
			request.set_asset_id(msg.asset_id());
			request.set_offset(it->nextOffset);
			request.set_max_bytes(262144);
			Global::get().sh->sendMessage(request);
		}
		return;
	}

	mumble::chatperf::recordValue("chat.asset_chunk.bytes", it->bytes.size());
	mumble::chatperf::recordValue("chat.asset_chunk.preview_keys", it->previewKeys.size());
	QImage image;
	{
		mumble::chatperf::ScopedDuration decodeTrace("chat.asset_chunk.decode");
		image.loadFromData(it->bytes);
	}
	const bool playableMedia = isPersistentChatPlayableMediaMime(it->mime);
	const QString mediaDataUrl =
		playableMedia ? persistentChatPlayableMediaDataUrl(it->mime, it->bytes) : QString();
	for (const QString &previewKey : it->previewKeys) {
		auto previewIt = m_persistentChatPreviews.find(previewKey);
		if (previewIt == m_persistentChatPreviews.end()) {
			continue;
		}

		previewIt->thumbnailFinished = true;
		const QString incomingKind = persistentChatPlayableMediaKind(it->mime, it->kind);
		const bool decorativeSocialGif =
			incomingKind == QLatin1String("gif") && isSocialVideoPreviewUrl(previewIt->canonicalUrl);
		if (!mediaDataUrl.isEmpty()) {
			const bool keepExistingVideo =
				previewIt->mediaKind == QLatin1String("video") && incomingKind != QLatin1String("video")
				&& previewIt->mediaDataUrl.startsWith(QLatin1String("https://"), Qt::CaseInsensitive);
			if (!keepExistingVideo && !decorativeSocialGif) {
				previewIt->mediaDataUrl = mediaDataUrl;
				previewIt->mediaMime    = it->mime;
				previewIt->mediaKind    = incomingKind;
				previewIt->autoplay     = false;
			}
			previewIt->failed = false;
			if (!image.isNull()) {
				previewIt->thumbnailImage = image;
			}
		} else if (!image.isNull()) {
			previewIt->thumbnailImage = image;
			previewIt->failed         = false;
		} else {
			previewIt->failed = true;
			ensurePersistentChatPreviewSiteSnapshot(previewKey);
		}
		ensurePersistentChatPreviewSiteSnapshot(previewKey);
		storePersistentChatPreviewDiskCache(previewKey);
	}

	m_persistentChatAssetDownloads.erase(it);
}

void MainWindow::msgChatEmbedState(const MumbleProto::ChatEmbedState &msg) {
	handlePersistentChatEmbedState(msg);
}

void MainWindow::msgChatEmbedAssistRequest(const MumbleProto::ChatEmbedAssistRequest &msg) {
	handleChatEmbedAssistRequest(msg);
}

void MainWindow::msgChatEmbedAssistResult(const MumbleProto::ChatEmbedAssistResult &) {
}

void MainWindow::msgChatReactionToggle(const MumbleProto::ChatReactionToggle &) {
}

void MainWindow::msgChatReactionState(const MumbleProto::ChatReactionState &msg) {
	handlePersistentChatReactionState(msg);
}

void MainWindow::msgChatHistoryGrantSync(const MumbleProto::ChatHistoryGrantSync &) {
}

void MainWindow::msgWatchTogetherSync(const MumbleProto::WatchTogetherSync &msg) {
	if (!m_qmlShellHost || !m_qmlShellHost->window() || !msg.has_session_id()) return;

	MediaSessionBackend *media = m_qmlShellHost->mediaSession();
	if (!media) return;

	QString provider = QStringLiteral("direct");
	switch (msg.source_kind()) {
		case MumbleProto::WatchTogetherSourceYouTube: provider = QStringLiteral("youtube"); break;
		default: break;
	}
	QString event = QStringLiteral("state");
	switch (msg.event()) {
		case MumbleProto::WatchTogetherEventStart: event = QStringLiteral("start"); break;
		case MumbleProto::WatchTogetherEventJoin: event = QStringLiteral("join"); break;
		case MumbleProto::WatchTogetherEventLeave: event = QStringLiteral("leave"); break;
		case MumbleProto::WatchTogetherEventEnd: event = QStringLiteral("end"); break;
		case MumbleProto::WatchTogetherEventHostTransfer: event = QStringLiteral("host-transfer"); break;
		case MumbleProto::WatchTogetherEventState:
		case MumbleProto::WatchTogetherEventStateRequest:
		default: break;
	}
	QVariantList participants;
	participants.reserve(msg.participant_sessions_size());
	for (const unsigned int participant : msg.participant_sessions())
		participants.push_back(QVariant::fromValue(static_cast< qulonglong >(participant)));
	media->applySharedState(
		u8(msg.session_id()), msg.has_source_url() ? QUrl(u8(msg.source_url())) : QUrl(), provider,
		msg.has_title() ? u8(msg.title()) : QString(), msg.has_scope_id() ? msg.scope_id() : 0,
		msg.has_actor_session() ? msg.actor_session() : 0, msg.has_host_session() ? msg.host_session() : 0,
		participants, event, msg.position_seconds(), msg.paused(), msg.has_updated_at() ? msg.updated_at() : 0,
		Global::get().uiSession);
}

void MainWindow::msgStonksRequest(const MumbleProto::StonksRequest &) {
}

void MainWindow::msgStonksAction(const MumbleProto::StonksAction &) {
}

void MainWindow::msgStonksState(const MumbleProto::StonksState &msg) {
	handleStonksState(msg);
}

void MainWindow::msgFeedbackReport(const MumbleProto::FeedbackReport &) {
}

void MainWindow::msgFeedbackReportState(const MumbleProto::FeedbackReportState &msg) {
	handleFeedbackReportState(msg);
}

void MainWindow::msgScreenShareCreate(const MumbleProto::ScreenShareCreate &) {
}

void MainWindow::msgScreenShareState(const MumbleProto::ScreenShareState &msg) {
	if (m_screenShareManager) {
		m_screenShareManager->handleScreenShareState(msg);
	}
}

void MainWindow::msgScreenShareOffer(const MumbleProto::ScreenShareOffer &msg) {
	if (m_screenShareManager) {
		m_screenShareManager->handleScreenShareOffer(msg);
	}
}

void MainWindow::msgScreenShareAnswer(const MumbleProto::ScreenShareAnswer &msg) {
	if (m_screenShareManager) {
		m_screenShareManager->handleScreenShareAnswer(msg);
	}
}

void MainWindow::msgScreenShareIceCandidate(const MumbleProto::ScreenShareIceCandidate &msg) {
	if (m_screenShareManager) {
		m_screenShareManager->handleScreenShareIceCandidate(msg);
	}
}

void MainWindow::msgScreenShareStop(const MumbleProto::ScreenShareStop &msg) {
	if (m_screenShareManager) {
		m_screenShareManager->handleScreenShareStop(msg);
	}
}

void MainWindow::msgTextChannelSync(const MumbleProto::TextChannelSync &msg) {
	markPersistentChatAvailable(false);
	handlePersistentTextChannelSync(msg);
}

#undef ACTOR_INIT
#undef VICTIM_INIT
#undef SELF_INIT
