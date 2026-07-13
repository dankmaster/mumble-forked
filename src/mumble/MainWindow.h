// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MAINWINDOW_H_
#define MUMBLE_MUMBLE_MAINWINDOW_H_

#include <QtCore/QVariantMap>

#include <QtCore/QHash>
#include <QtCore/QEvent>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtCore/QPointer>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtCore/QVariant>
#include <QtCore/QtGlobal>
#include <QtGui/QImage>
#include <QtGui/QAction>
#include <QtNetwork/QAbstractSocket>
#include <QtCore/QObject>

#include "ACL.h"
#include "ConnectionFailTypes.h"
#include "Log.h"
#include "ModernShellMenuSerializer.h"
#include "Mumble.pb.h"
#include "MumbleProtocol.h"
#include "PersistentChatState.h"
#include "PluginInstallService.h"
#include "QtUtils.h"
#include "Settings.h"
#include "UnresolvedServerAddress.h"
#include "Usage.h"

#include <memory>
#include <atomic>
#include <optional>
#include <stack>
#include <vector>

#define MB_QEVENT (QEvent::User + 939)
#define OU_QEVENT (QEvent::User + 940)

class ServerHandler;
class GlobalShortcut;
class TextToSpeech;
class UserModel;
class Channel;
class ClientUser;
class ClientActionRegistry;
class ClientActionList;
class ScreenShareManager;
struct ScreenShareStartOptions;
struct PersistentChatPreviewSpec;
class PersistentChatGateway;
class PersistentChatController;
class ModernDialogController;
class QmlShellHost;
class QmlSelectionState;
class PluginCancellationGate;
#	if defined(MUMBLE_HAS_MODERN_UI_AUTOMATION)
class ModernUiAutomationServer;
#	endif
struct ModernConnectPingState;
class QCloseEvent;
class QObject;
class QFrame;
class QModelIndex;
class QTimer;
class QHostAddress;
class QNetworkReply;
class QNetworkRequest;
class QSslCertificate;
class QSslError;
class QToolButton;
class QUrl;

namespace Search {
class SearchDialog;
}

class ListenerVolumeController;

struct ShortcutTarget;
struct FavoriteServer;
struct UnresolvedServerAddress;

struct ContextMenuTarget {
	ClientUser *user = nullptr;
	Channel *channel = nullptr;
};

struct PendingFeedbackSubmission {
	MumbleProto::FeedbackReportKind kind = MumbleProto::FeedbackReportBug;
	QString issueTitle;
	QString issueBody;
	QUrl fallbackUrl;
	bool fromModernShell = false;
};

class MessageBoxEvent : public QEvent {
public:
	QString msg;
	MessageBoxEvent(QString msg);
};

class OpenURLEvent : public QEvent {
public:
	QUrl url;
	OpenURLEvent(QUrl url);
};

class MainWindow : public QObject {
	friend class UserModel;
#	if defined(MUMBLE_HAS_MODERN_UI_AUTOMATION)
	friend class ModernUiAutomationServer;
#	endif

private:
	Q_OBJECT
	Q_DISABLE_COPY(MainWindow)
public:
	void show();
	void hide();
	void raise();
	void activateWindow();
	void close();
	bool isActiveWindow() const;
	bool isVisible() const;
	bool isMinimized() const;
	QFont font() const;
	WId winId() const;
	Qt::WindowStates windowState() const;
	void setWindowState(Qt::WindowStates state);
	Qt::WindowFlags windowFlags() const;
	void setWindowFlags(Qt::WindowFlags flags);
	void setWindowTitle(const QString &title);
	bool isServerLogViewVisible() const;
	bool shouldMirrorServerLogToNativeWidget() const;
	void setServerLogMaximumBlockCount(int maxBlocks);
	void queueModernStartupSetup(bool showAudioSetup, bool showCertificateSetup);
	void openModernPluginUpdateDialog(const QVariantList &updates);
	std::optional< unsigned int > selectedModernUserSession() const;
	std::optional< unsigned int > selectedModernVoiceChannel() const;
	QmlShellHost *qmlShellHost() const;
	void selectModernUserSession(unsigned int session);
	void selectModernVoiceChannel(unsigned int channelID);
	UserModel *pmModel;
	ClientActionList *qmUser;
	ClientActionList *qmChannel;
	ClientActionList *qmListener;
	ClientActionList *qmDeveloper;
	ClientActionList *qmConfig = nullptr, *qmHelp = nullptr, *qmServer = nullptr, *qmSelf = nullptr;
	QAction *qaQuit = nullptr, *qaServerConnect = nullptr, *qaServerDisconnect = nullptr;
	QAction *qaServerAddToFavorites = nullptr, *qaServerBanList = nullptr, *qaServerInformation = nullptr;
	QAction *qaUserKick = nullptr, *qaUserMute = nullptr, *qaUserBan = nullptr, *qaUserDeaf = nullptr;
	QAction *qaUserLocalIgnore = nullptr, *qaUserLocalMute = nullptr, *qaUserTextMessage = nullptr;
	QAction *qaUserLocalNickname = nullptr, *qaChannelAdd = nullptr, *qaChannelRemove = nullptr;
	QAction *qaChannelACL = nullptr, *qaChannelLink = nullptr, *qaChannelUnlink = nullptr;
	QAction *qaChannelUnlinkAll = nullptr, *qaAudioReset = nullptr, *qaAudioMute = nullptr;
	QAction *qaAudioDeaf = nullptr, *qaAudioTTS = nullptr, *qaAudioStats = nullptr, *qaAudioUnlink = nullptr;
	QAction *qaConfigDialog = nullptr, *qaFilterToggle = nullptr, *qaAudioWizard = nullptr;
	QAction *qaDeveloperConsole = nullptr, *qaHelpWhatsThis = nullptr, *qaHelpFeedback = nullptr;
	QAction *qaHelpAbout = nullptr, *qaHelpAboutSpeex = nullptr, *qaHelpAboutQt = nullptr;
	QAction *qaHelpVersionCheck = nullptr, *qaChannelSendMessage = nullptr, *qaChannelCopyURL = nullptr;
	QAction *qaConfigMinimal = nullptr, *qaConfigCert = nullptr;
	QAction *qaUserRegister = nullptr, *qaUserFriendAdd = nullptr, *qaUserFriendRemove = nullptr;
	QAction *qaUserFriendUpdate = nullptr, *qaServerUserList = nullptr, *qaServerTexture = nullptr;
	QAction *qaServerTokens = nullptr, *qaServerTextureRemove = nullptr, *qaUserCommentReset = nullptr;
	QAction *qaUserTextureReset = nullptr, *qaChannelJoin = nullptr, *qaChannelHide = nullptr;
	QAction *qaChannelPin = nullptr, *qaUserCommentView = nullptr, *qaUserInformation = nullptr;
	QAction *qaSelfComment = nullptr, *qaSelfRegister = nullptr, *qaUserPrioritySpeaker = nullptr;
	QAction *qaSelfPrioritySpeaker = nullptr, *qaRecording = nullptr, *qaChannelListen = nullptr;
	QAction *qaUserJoin = nullptr, *qaUserMove = nullptr, *qaUserLocalIgnoreTTS = nullptr;
	QAction *qaSearch = nullptr, *qaMoveBack = nullptr;
	QIcon qiIcon, qiIconMutePushToMute, qiIconMuteSelf, qiIconMuteServer, qiIconDeafSelf, qiIconDeafServer,
		qiIconMuteSuppressed;
	QIcon qiTalkingOn, qiTalkingWhisper, qiTalkingShout, qiTalkingOff;
	QIcon m_iconInformation;

	/// "Action" for when there are no actions available
	QAction *qaEmpty;

	GlobalShortcut *gsPushTalk, *gsResetAudio, *gsMuteSelf, *gsDeafSelf;
	GlobalShortcut *gsUnlink, *gsPushMute, *gsJoinChannel;
	GlobalShortcut *gsMinimal, *gsVolumeUp, *gsVolumeDown, *gsWhisper, *gsLinkChannel, *gsListenChannel;
	GlobalShortcut *gsCycleTransmitMode, *gsToggleMainWindowVisibility, *gsTransmitModePushToTalk,
		*gsTransmitModeContinuous, *gsTransmitModeVAD;
	GlobalShortcut *gsSendTextMessage, *gsSendClipboardTextMessage;
	GlobalShortcut *gsToggleSearch;
	GlobalShortcut *gsServerConnect, *gsServerDisconnect, *gsServerInformation, *gsServerTokens;
	GlobalShortcut *gsServerUserList, *gsServerBanList;
	GlobalShortcut *gsSelfPrioritySpeaker;
	GlobalShortcut *gsRecording;
	GlobalShortcut *gsSelfComment, *gsServerTexture, *gsServerTextureRemove;
	GlobalShortcut *gsSelfRegister, *gsAudioStats;
	GlobalShortcut *gsConfigDialog, *gsAudioWizard, *gsConfigCert;
	GlobalShortcut *gsAudioTTS;
	GlobalShortcut *gsHelpAbout, *gsHelpAboutQt, *gsHelpVersionCheck;
	GlobalShortcut *gsTogglePositionalAudio;
	GlobalShortcut *gsMoveBack;
	GlobalShortcut *gsCycleListenerAttenuationMode, *gsListenerAttenuationUp, *gsListenerAttenuationDown;
	GlobalShortcut *gsAdaptivePush;


	MumbleProto::Reject_RejectType rtLast;
	bool bRetryServer;
	QString qsDesiredChannel;
	struct PendingServerConnection {
		QString host;
		unsigned short port = 0;
		QString username;
		QString password;
		QString serverName;
		QString desiredChannel;
	};
	std::optional< PendingServerConnection > m_pendingServerConnection;
	std::shared_ptr< ServerHandler > m_retiringServerHandler;
	std::vector< std::shared_ptr< ServerHandler > > m_retiredServerHandlers;
	bool m_serverHandlerFinalizeScheduled = false;
	bool m_serverHandlerPruneScheduled    = false;
	int m_serverHandlerPruneAttempts      = 0;

	bool forceQuit;
	/// Restart the client after shutdown
	bool restartOnQuit;

	/// Cached copy of the image currently targeted by the log/persistent-chat image actions.
	QImage m_selectedLogImage;
	std::unique_ptr< ScreenShareManager > m_screenShareManager;
	std::unique_ptr< ClientActionRegistry > m_clientActionRegistry;

	QPointer< Channel > cContextChannel;
	QPointer< ClientUser > cuContextUser;

	QPoint qpContextPosition;

	void recheckTTS();
	void msgBox(QString msg);
	void setOnTop(bool top);
	void updateAudioToolTips();
	void focusNextMainWidget();
	void refreshShellLayout();
	void applyShellLayout();
	void scheduleQmlShellStateSync();
	void scheduleQmlShellStateSyncImmediate();
	void scheduleQmlShellStateSyncInternal(bool immediate);
	void runQmlShellStateSync();
	void syncQmlShellState();
	QmlSelectionState *qmlSelectionState() const;
	void applyQmlRoomState(const QVariantMap &state);
	void applyQmlDirectMessagesState(const QVariantMap &state);
	void ensureModernUiAutomationServer();
	void beginNativeWindowMoveOrResize();
	void endNativeWindowMoveOrResize();
	void updateServerNavigatorChrome();
	void syncServerNavigatorUserMenu();
	void positionServerNavigatorUserMenu();
	void initializePersistentChatBackend();
	void publishPersistentChatSnapshot();
	void syncPersistentChatGatewayHandler();
	void warmupPersistentChatHistory();
	QList< PersistentChatScopeKey > persistentChatWarmupScopes() const;

	struct PersistentChatTarget {
		bool valid                   = false;
		bool directMessage           = false;
		bool ephemeralTextPath          = false;
		bool readOnly                = false;
		bool serverLog               = false;
		ClientUser *user             = nullptr;
		Channel *channel             = nullptr;
		MumbleProto::ChatScope scope = MumbleProto::Channel;
		unsigned int scopeID         = 0;
		QString label;
		QString description;
		QString statusMessage;
	};

	struct PersistentTextChannel {
		unsigned int textChannelID = 0;
		unsigned int aclChannelID  = 0;
		unsigned int position      = 0;
		QString name;
		QString description;
	};

	enum class RoomCreateType : unsigned char { Voice, Text };
	enum class UserTextureRequestReason : unsigned char {
		UserState,
		Navigator,
		ModernShell,
		PersistentChat,
		UserInformation
	};

	bool ensureUserTextureAvailable(ClientUser *user, UserTextureRequestReason reason);
	bool ensureUserCommentAvailable(ClientUser *user);
	bool normalizeUserTextureForDisplay(ClientUser *user, bool clearHashOnFailure = false);

	struct PersistentChatPreviewMediaItem {
		QString url;
		QString mime;
		QString kind;
		QString imageProviderUrl;
		bool imageProviderRequested = false;
		bool imageProviderFinished  = false;
		bool imageProviderAnimated  = false;
	};

	struct PersistentChatPreview {
		QString canonicalUrl;
		QString title;
		QString subtitle;
		QString description;
		QImage thumbnailImage;
		QString mediaDataUrl;
		QString mediaAudioDataUrl;
		QString mediaAudioMime;
		QString mediaMime;
		QString mediaKind;
		QString thumbnailProviderUrl;
		QString thumbnailRequestSource;
		QString mediaImageProviderUrl;
		QString mediaImageProviderSource;
		std::vector< PersistentChatPreviewMediaItem > mediaItems;
		QVariantMap metadata;
		QString openLabel;
		unsigned int previewAssetID = 0;
		bool autoplay               = false;
		bool metadataFinished       = false;
		bool thumbnailFinished      = false;
		bool failed                 = false;
		bool siteSnapshotRequested  = false;
		bool siteSnapshotFinished   = false;
		bool remoteMediaRequested   = false;
		bool remoteMediaFinished    = false;
		bool mediaImageProviderRequested = false;
		bool mediaImageProviderFinished  = false;
		bool mediaImageProviderAnimated  = false;
	};

	struct PersistentChatAssetDownload {
		unsigned int assetID = 0;
		quint64 nextOffset   = 0;
		quint64 totalSize    = 0;
		QString mime;
		MumbleProto::ChatAssetKind kind = MumbleProto::ChatAssetKindUnknown;
		QByteArray bytes;
		QSet< QString > previewKeys;
	};

	struct PendingChatEmbedAssist {
		quint64 leaseID = 0;
		unsigned int messageID = 0;
		QString canonicalUrl;
		QString urlHash;
		quint64 leaseExpiresAt = 0;
		unsigned int maxThumbnailBytes = 512 * 1024;
		QString title;
		QString description;
		QString siteName;
		QImage thumbnailImage;
		bool completed = false;
	};

	struct ModernDirectMessageEntry {
		quint64 localID = 0;
		unsigned int peerSession  = 0;
		unsigned int actorSession = 0;
		QString actorName;
		QString messageHtml;
		QString plainText;
		qint64 createdAtMs = 0;
		bool outgoing      = false;
		bool persistent    = false;
		unsigned int threadID  = 0;
		unsigned int messageID = 0;
	};

	struct ModernDirectMessageConversation {
		unsigned int peerSession = 0;
		unsigned int peerUserID  = 0;
		QString label;
		QString subtitle;
		qint64 lastActivityAtMs = 0;
		unsigned int unreadCount = 0;
		bool open                = false;
		bool persistentHistory   = false;
		bool historyLoading      = false;
		bool historyLoaded       = false;
		QString historyError;
		unsigned int lastReadMessageID = 0;
		unsigned int lastMessageID     = 0;
		QSet< QString > persistentMessageKeys;
		std::vector< ModernDirectMessageEntry > messages;
	};

	enum class ModernShellMessageBuildMode : unsigned char { Full, FastFirstPaint };

	bool hasPersistentChatCapabilities() const;
	PersistentChatTarget ephemeralChatTarget() const;
	PersistentChatTarget currentPersistentChatTarget() const;
	void refreshPersistentChatView(bool forceReload = false);
	void requestOlderPersistentChatHistory();
	void setPersistentChatWelcomeText(const QString &message);
	void updatePersistentChatWelcome();
	void clearPersistentChatView(const QString &message, const QString &title = QString(),
								 const QStringList &hints = QStringList());
	void markPersistentChatAvailable(bool refreshUi = true);
	void clearPersistentChatReplyTarget(bool refreshChatBar);
	void setPersistentChatReplyTarget(const std::optional< MumbleProto::ChatMessage > &message);
	std::optional< MumbleProto::ChatEmbedRef >
		persistentChatPrimaryEmbed(const MumbleProto::ChatMessage &message) const;
	QString persistentChatMessageIdentityKey(const MumbleProto::ChatMessage &message) const;
	std::optional< QString > persistentChatPreviewKey(const MumbleProto::ChatMessage &message) const;
	PersistentChatPreviewSpec persistentChatPreviewSpec(const QString &previewKey) const;
	QString persistentChatScopeLabel(MumbleProto::ChatScope scope, unsigned int scopeID) const;
	void rememberPersistentChatPreviewInputs(const MumbleProto::ChatMessage &message);
	void warmupPersistentChatPreviews(const MumbleProto::ChatMessage &message);
	void warmupPersistentChatPreviews(const MumbleProto::ChatHistoryResponse &response);
	void queuePersistentChatPreviewRequest(const QString &previewKey);
	void flushPersistentChatPreviewRequests();
	void ensurePersistentChatPreview(const QString &previewKey);
	void ensurePersistentChatPreviewAssetDownload(unsigned int assetID, const QString &previewKey);
	void ensurePersistentChatPreviewSiteSnapshot(const QString &previewKey);
	void restorePersistentChatPreviewDiskCache(const QString &previewKey);
	bool refreshRestoredPersistentChatPreview(const QString &previewKey);
	void storePersistentChatPreviewDiskCache(const QString &previewKey);
	void handleChatEmbedAssistRequest(const MumbleProto::ChatEmbedAssistRequest &msg);
	void requestChatEmbedAssistPage(quint64 leaseID, const QUrl &url, int redirectCount = 0);
	void requestChatEmbedAssistImage(quint64 leaseID, const QUrl &url, int redirectCount = 0);
	QNetworkReply *startChatEmbedAssistGet(const QNetworkRequest &request, quint64 leaseID);
	void cancelChatEmbedAssistNetworkRequests(std::optional< quint64 > leaseID = std::nullopt,
										 bool invalidateGeneration = false);
	void finishChatEmbedAssist(quint64 leaseID, const QString &errorCode = QString());
	void cancelChatEmbedAssistForState(const MumbleProto::ChatEmbedState &msg);
	bool applyYahooFinanceQuotePreviewFallback(PersistentChatPreview &preview, const QUrl &url) const;
	bool requestPersistentChatFinancePreview(const QString &previewKey, const QUrl &previewUrl);
	bool requestPersistentChatInstagramMetadataPreview(const QString &previewKey, const QUrl &previewUrl);
	bool requestPersistentChatXPostPreview(const QString &previewKey, const QUrl &previewUrl);
	void requestPersistentChatXPostReplyContext(const QString &previewKey, const QString &statusId, int remaining,
												QVariantList directChain = QVariantList());
	bool requestPersistentChatGitHubPreview(const QString &previewKey, const QUrl &previewUrl);
	bool requestPersistentChatWebhallenProductPreview(const QString &previewKey, const QUrl &previewUrl);
	bool requestPersistentChatRichProviderPreview(const QString &previewKey, const QUrl &previewUrl);
	bool requestPersistentChatSteamAppPreview(const QString &previewKey, const QUrl &previewUrl);
	bool requestPersistentChatSteamReviewPreview(const QString &previewKey, const QString &appId);
	bool requestPersistentChatOEmbedPreview(const QString &previewKey, const QUrl &previewUrl);
	bool requestPersistentChatRedditVideoPreview(const QString &previewKey, const QUrl &previewUrl);
	bool requestPersistentChatRedditDashManifestPreview(const QString &previewKey, const QString &videoId,
														const QString &metadataFailureText = QString());
	bool requestPersistentChatRedditVideoAudioPreview(const QString &previewKey, const QUrl &dashManifestUrl);
	bool requestPersistentChatRemotePlayableMediaCache(const QString &previewKey, const QUrl &mediaUrl,
													   const QString &suggestedMime = QString(),
													   const QUrl &audioUrl = QUrl(),
													   const QString &suggestedAudioMime = QString());
	bool requestPersistentChatPreviewPosterImage(const QString &previewKey, const QUrl &posterUrl,
												 const QString &suggestedMime = QString(), int redirectCount = 0);
	bool applyPersistentChatRemotePlayableMedia(PersistentChatPreview &preview, const QUrl &mediaUrl,
												const QString &suggestedMime = QString());
	bool applyPersistentChatRemoteAudioMedia(PersistentChatPreview &preview, const QUrl &audioUrl,
											 const QString &suggestedMime = QString());
	void applyPersistentChatListingMediaItems(PersistentChatPreview &preview);
	void ensurePersistentChatPreviewImageProviders(const QString &previewKey);
	void requestPersistentChatPreviewImageProvider(const QString &previewKey, int mediaItemIndex,
											 const QString &sourceIdentity, const QUrl &requestUrl,
											 const QString &suggestedMime = QString(), int redirectCount = 0);
	void registerPersistentChatPreviewDataImageProvider(const QString &previewKey, int mediaItemIndex,
												 const QString &sourceIdentity, const QString &mime);
	void completePersistentChatPreviewImageProvider(const QString &previewKey, int mediaItemIndex,
												  const QString &sourceIdentity, const QString &providerUrl,
												  bool animated);
	QNetworkReply *startPersistentChatPreviewGet(const QNetworkRequest &request, const QString &previewKey);
	QNetworkReply *startPersistentChatPreviewPost(const QNetworkRequest &request, const QByteArray &body,
												 const QString &previewKey);
	void cancelPersistentChatPreviewNetworkRequests(const QString &previewKey = QString());
	void removePersistentChatPreview(const QString &previewKey);
	void publishPersistentChatPreviewUpdate(const QString &previewKey);
	int persistentChatPreviewContentWidth(int leftPadding) const;
	QString persistentChatPreviewHtml(const QString &previewKey, int availableWidth) const;
	void setPersistentChatTargetUsesVoiceTree(bool useVoiceTree);
	void rebuildPersistentChatChannelList();
	void handlePersistentTextChannelSync(const MumbleProto::TextChannelSync &msg);
	void updatePersistentChatScopeSelectorLabels();
	Channel *currentVoiceChannel() const;
	Channel *selectedVoiceTreeChannel() const;
	void focusPersistentChatVoiceChannel(Channel *channel);
	std::size_t cachedPersistentChatUnreadCount(MumbleProto::ChatScope scope, unsigned int scopeID) const;
	void setCachedPersistentChatUnreadCount(MumbleProto::ChatScope scope, unsigned int scopeID,
											unsigned int lastReadMessageID, std::size_t unreadCount);
	std::size_t totalCachedPersistentChatUnreadCount() const;
	bool navigateToPersistentChatScope(MumbleProto::ChatScope scope, unsigned int scopeID, bool forceReload = false,
									   bool useVoiceTree = false);
	ChanACL::Permissions channelPermissions(Channel *channel, bool requestPermissions = true) const;
	bool canEditChannelACL(Channel *channel) const;
	bool canCreateVoiceRoom(Channel *channel) const;
	bool canCreateAnyVoiceRoom() const;
	bool voiceRoomCreationForcesTemporary(Channel *channel) const;
	bool canCreateTextRoom() const;
	void createRoom(RoomCreateType preferredType, Channel *preferredVoiceParent = nullptr);
	bool canManagePersistentTextChannels() const;
	bool canEditPersistentTextChannelACL(const PersistentTextChannel &textChannel) const;
	std::optional< PersistentTextChannel > selectedPersistentTextChannel() const;
	void openServerSettingsDialog();
	void createPersistentTextChannel();
	void editPersistentTextChannel();
	void editPersistentTextChannel(unsigned int textChannelID);
	void removePersistentTextChannel();
	void removePersistentTextChannel(unsigned int textChannelID);
	void editPersistentTextChannelACL();
	void editPersistentTextChannelACL(unsigned int textChannelID);
	void setDefaultPersistentTextChannel();
	void setDefaultPersistentTextChannel(unsigned int textChannelID);
	void updatePersistentTextChannelControls();
	void openImageDialog(const QImage &image);
	void openModernImageViewerDialog(const QImage &image);
	void openModernScreenShareStatusDialog(const QString &streamID, const QString &sourceLabel,
										   const QString &qualityLabel, const QString &audioLabel);
	QString registerPersistentChatInlineDataImageSource(const QString &source);
	QUrl persistentChatInlineDataImageOpenUrl(const QString &token) const;
	QUrl persistentChatInlineDataImageResourceUrl(const QString &token) const;
	QString persistentChatInlineDataImageThumbnailSourceForToken(const QString &token, const QImage &previewImage);
	QImage persistentChatInlineDataImageFromSource(const QString &source) const;
	QImage persistentChatInlineDataImageFromUrl(const QUrl &url) const;
	void warmupPersistentChatInlineDataImages(const MumbleProto::ChatMessage &message);
	void queuePersistentChatInlineDataImageWarmup(const QString &source, const QString &messageKey = QString());
	void flushPersistentChatInlineDataImageWarmups();
	void setPersistentChatContentMode(bool showServerLog, bool preserveScrollPosition = false,
									  bool showComposer = false);
	void renderEphemeralLogView(bool preserveScrollPosition = false);
	void renderServerLogView(bool preserveScrollPosition = false);
	void renderPersistentChatView(const QString &statusMessage = QString(), bool scrollToBottom = true,
								  bool preserveScrollPosition = false);
	bool canMarkPersistentChatRead(bool willScrollToBottom = false) const;
	std::size_t persistentChatUnreadCount() const;
	void handlePersistentChatMessage(const MumbleProto::ChatMessage &msg);
	void handlePersistentChatHistory(const MumbleProto::ChatHistoryResponse &msg);
	void handlePersistentChatReadState(const MumbleProto::ChatReadStateUpdate &msg);
	void handlePersistentChatEmbedState(const MumbleProto::ChatEmbedState &msg);
	void handlePersistentChatReactionState(const MumbleProto::ChatReactionState &msg);
	bool canViewPersistentChatHistory(const PersistentChatTarget &target, bool requestPermissions) const;
	bool canSendToPersistentChatTarget(const PersistentChatTarget &target, bool requestPermissions) const;
	bool canDeletePersistentChatMessages(const PersistentChatTarget &target, bool requestPermissions) const;
	bool persistentChatTargetSupportsMessageDelete(const PersistentChatTarget &target) const;
	bool isOwnPersistentChatMessage(const MumbleProto::ChatMessage &message) const;
	const MumbleProto::ChatMessage *deletablePersistentChatMessage(unsigned int messageID,
																   PersistentChatTarget *target = nullptr) const;
	bool executePersistentChatMessageDelete(const PersistentChatTarget &target, const MumbleProto::ChatMessage &message);
	bool deletePersistentChatMessage(unsigned int messageID);
	void syncPersistentChatInputState(bool baseEnabled);
	bool attachPersistentChatClipboardImage();
	void attachPersistentChatImage(const QImage &image);
	void attachPersistentChatImages(const QList< QUrl > &urls);
	bool attachPersistentChatImageData(const QString &dataUrl);
	void openPersistentChatImagePicker();
	void clearPersistentChatChannelSelection();
	bool markPersistentChatRead(bool rerender = true, bool willScrollToBottom = false);
	void updatePersistentChatChrome(const PersistentChatTarget &target);
	void updatePersistentChatSendButton();
	bool tryConnectFromUpdateResumeState();
	void loadPendingUpdateResumeState();
	void applyPendingUpdateResumeState();
	QVariantMap buildModernShellMessageState(const MumbleProto::ChatMessage &message,
											 const PersistentChatTarget &target, bool canReply, bool canReact,
											 bool canDeleteMessages,
											 ModernShellMessageBuildMode buildMode = ModernShellMessageBuildMode::Full);
	QVariantList buildModernShellMessageStates(
		const PersistentChatTarget &target, std::size_t beginIndex = 0,
		ModernShellMessageBuildMode buildMode = ModernShellMessageBuildMode::Full);
	QVariantMap buildModernShellVoiceRoomScreenShareState(const Channel *channel) const;
	QVariantMap buildQmlActiveScopeState(const PersistentChatTarget &target);
	QVariantMap buildModernShellServerLogActiveScopeState(const PersistentChatTarget &target);
	QVariantMap buildQmlParticipantState(const ClientUser *user, const Channel *contextChannel,
													  const ClientUser *directMessagePeer, int avatarSize,
													  bool includeAvatar);
	QVariantMap buildQmlListenerState(const ClientUser *user, const Channel *channel, int avatarSize,
											   bool includeAvatar);
	std::optional< QVariantMap > buildCurrentQmlParticipantState(const ClientUser *user);
	QVariantList buildQmlChannelParticipantStates(const Channel *channel, int avatarSize,
														   bool includeAvatar);
	QVariantMap buildQmlRoomState();
	QVariantList buildQmlScopeActions(const QString &scopeToken, const QString &kind);
	void flushQmlRoomStateUpdates();
	void publishQmlChatMessage(const MumbleProto::ChatMessage &message);
	void publishPersistentChatInlineDataImageUpdate(const QString &token);
	void publishPersistentChatInlineDataImageProviderUpdate(const QString &token);
	void publishQmlActiveScopeState();
	void scheduleQmlRoomStateUpdate();
	void publishModernShellServerLogUpdate(const PersistentChatTarget &target);
	void publishModernShellServerLogReset(const PersistentChatTarget &target);
	void clearModernShellMessageDtoCache(const char *reason);
	void evictModernShellMessageDtoCacheForMessage(const MumbleProto::ChatMessage &message);
	void publishModernShellPreviewUpdateForKey(const QString &previewKey);
	QString modernShellAvatarDataUrlForTextureHash(const QByteArray &textureHash, int avatarSize);
	QString modernShellActorAvatarDataUrl(const MumbleProto::ChatMessage &message, const QString &actorIdentityKey,
										 const ClientUser *messageUser, int avatarSize);
	QString modernShellMessageDtoCacheKey(const MumbleProto::ChatMessage &message, const PersistentChatTarget &target,
										  bool canReply, bool canReact, bool canDeleteMessages) const;
	QVariantMap modernShellPreviewStateForKey(const QString &previewKey) const;
	QVariantMap buildModernShellCachedMessageState(const MumbleProto::ChatMessage &message,
												   const PersistentChatTarget &target, bool canReply, bool canReact,
												   bool canDeleteMessages,
												   ModernShellMessageBuildMode buildMode = ModernShellMessageBuildMode::Full);
	QString modernShellScopeTokenForTarget(const PersistentChatTarget &target) const;
	QString modernShellMessageTimelineMode(std::size_t beginIndex) const;
	void handleModernShellPreviewHydrationRequest(const QString &scopeToken, const QVariantList &messageIds,
												  bool highPriority);
	void flushModernShellPreviewHydrationQueue();
	QVariantMap buildModernShellDirectMessagesState() const;
	QVariantMap buildModernShellDirectMessageConversationState(const ModernDirectMessageConversation &conversation,
															   bool includeMessages) const;
	QVariantMap buildModernShellDirectMessageEntryState(const ModernDirectMessageEntry &entry) const;
	std::optional< unsigned int > persistentUserIDForClientUser(const ClientUser *user) const;
	ClientUser *clientUserByPersistentUserID(unsigned int userID) const;
	QString modernDirectMessagePersistentHistoryUnavailableReason(const ClientUser *peer) const;
	bool modernDirectMessagePersistentHistoryAvailable(const ClientUser *peer) const;
	bool openModernDirectMessage(unsigned int session, bool markRead = true);
	bool closeModernDirectMessage(unsigned int session);
	bool markModernDirectMessageRead(unsigned int session);
	void appendModernDirectMessage(unsigned int peerSession, const QString &messageHtml, bool outgoing);
	bool appendModernPersistentDirectMessage(const MumbleProto::ChatMessage &message, bool markReadIfOpen,
											 bool publishState = true);
	bool mergeModernDirectMessageHistory(const MumbleProto::ChatHistoryResponse &response);
	bool setModernDirectMessageMode(unsigned int session, const QString &mode);
	void requestModernDirectMessageHistory(unsigned int session);
	void warmupModernDirectMessageHistory();
	bool sendModernDirectMessage(unsigned int session, const QString &message);
	void publishQmlDirectMessagesState();
	bool handleModernShellScopeSelection(const QString &scopeToken);
	bool handleModernShellScopeRailSelection(const QString &scopeToken, const QString &railKind);
	bool handleModernShellVoiceJoin(const QString &scopeToken);
	bool handleModernShellParticipantSelection(unsigned int session, bool openConversation);
	bool handleModernShellScopeAction(const QString &scopeToken, const QString &actionId);
	bool handleModernShellScopeActionValueChanged(const QString &scopeToken, const QString &actionId, int value,
												  bool final);
	bool handleModernShellReplyStart(qulonglong messageID);
	void handleModernShellReplyCancel();
	bool handleModernShellReactionToggle(qulonglong messageID, const QString &emoji, bool active);
	bool handleModernShellMessageDelete(qulonglong messageID);
	bool handleModernShellParticipantMessage(qulonglong session);
	bool handleModernShellDirectMessageOpen(qulonglong session);
	bool handleModernShellDirectMessageClose(qulonglong session);
	bool handleModernShellDirectMessageMarkRead(qulonglong session);
	bool handleModernShellDirectMessageSend(qulonglong session, const QString &message);
	bool handleModernShellDirectMessageModeChange(qulonglong session, const QString &mode);
	bool handleModernShellParticipantJoin(qulonglong session);
	bool handleModernShellParticipantMove(qulonglong session, const QString &targetScopeToken);
	bool handleModernShellParticipantAction(qulonglong session, const QString &actionId);
	bool handleModernShellParticipantActionValueChanged(qulonglong session, const QString &actionId, int value,
														bool final);
	bool handleModernShellChannelMove(const QString &sourceScopeToken, const QString &targetScopeToken,
									  const QString &placement);
	bool handleModernShellAppAction(const QString &actionId);
	bool handleModernShellAppActionPayload(const QString &actionId, const QVariantMap &payload);
	QVariantMap modernTrayMenuUiTweaks() const;
	QVariantMap modernTrayProfileHeaderState() const;
	void publishModernToast(const QString &kind, const QString &title, const QString &message,
							const QString &actionID = QString(), const QString &actionLabel = QString(),
							int timeoutMs = 4500);
	void setModernUpdateBannerState(const QVariantMap &state);
	void clearModernUpdateBannerState();
	void publishModernUpdateBannerState();
	void showModernForkUpdateAvailableBanner(const QJsonObject &info);
	void showModernForkUpdateDownloadProgress(qint64 received, qint64 total);
	bool notifyForkUpdateAvailable(const QJsonObject &info, bool autocheck);
	bool handleModernVersionCheckResult(const QJsonObject &info, bool updateAvailable, bool autocheck);
	bool handleModernVersionCheckFailure(const QString &message, bool autocheck);
	bool startModernForkUpdateDownload();
	bool startModernForkUpdateDownload(const QJsonObject &info);
	bool restartForPreparedForkUpdate();
	void openModernStonksDialog();
	void requestStonksState(const QString &period = QString(),
							std::optional< unsigned int > userID = std::nullopt);
	void handleStonksState(const MumbleProto::StonksState &state);
	QVariantMap buildModernStonksDialog() const;
	bool handleModernStonksDialogAction(const QString &actionID, const QVariantMap &payload);
	bool openModernDeleteMessageDialog(unsigned int messageID, const PersistentChatTarget &target,
									   const MumbleProto::ChatMessage &message);
	bool sendModernShellMessage(const QString &message);
	void publishModernShellTalkState(const ClientUser *user);
	void publishQmlParticipantState(const ClientUser *user);
	void publishModernShellTalkStateForIndex(const QModelIndex &index);
	void startModernConnectServerPing(const QList< FavoriteServer > &favorites,
									  const QMap< UnresolvedServerAddress, unsigned int > &pingCache);
	void stopModernConnectServerPing();
	void sendNextModernConnectServerPing();
	void handleModernConnectServerPingReply();
	void publishModernConnectServerPingState();
	void sendModernConnectServerPing(const FavoriteServer &favorite);
	void sendModernConnectServerPing(const UnresolvedServerAddress &target, const QHostAddress &host,
									 unsigned short port, Version::full_t protocolVersion);
	bool writeModernConnectServerPing(const QHostAddress &host, unsigned short port,
									  Version::full_t protocolVersion,
									  const Mumble::Protocol::PingData &pingData);
	enum class ModernShellMenuContext : unsigned char {
		AppServer,
		AppSelf,
		AppConfigure,
		AppHelp,
		Participant,
		Scope,
		Listener
	};
	QVariantList serializeModernShellMenu(ClientActionList *menu, ModernShellMenuContext context,
										  ModernShellMenuSerializer::ActionRegistry *registry = nullptr) const;
	QVariantList buildModernShellConfigureMenuItems() const;
	QVariantList buildModernShellAppMenus() const;
	ModernShellMenuSerializer::ActionDefinition modernShellActionDefinition(ModernShellMenuContext context,
																		   QAction *action) const;
	bool triggerModernShellSerializedAction(const ModernShellMenuSerializer::ActionRegistry &registry,
											const QString &actionId, ClientUser *contextUser = nullptr,
											Channel *contextChannel = nullptr);
	void triggerContextAction(const QString &actionData, ClientUser *user, Channel *channel);
	bool sendChatbarTextToCurrentTarget(QString msg, bool plainText, bool clearNativeComposer);
	void updateChatBar(bool forcePersistentChatReload = false, bool scheduleQmlStateSync = true);
	void openTextMessageDialog(ClientUser *p);
	void openUserLocalNicknameDialog(const ClientUser &p);
	void queueUserTextureRequest(ClientUser *user, const QByteArray &expectedHash);
	void flushUserTextureRequests();
	void handleUserTextureHash(ClientUser *user, const QByteArray &hash);
	void handleUserTextureBlob(ClientUser *user, const QByteArray &texture);
	void refreshUserTextureViews(ClientUser *user);
	void clearUserTextureRequest(unsigned int session);
	void clearUserTextureRequests();
	void queueUserCommentRequest(ClientUser *user, const QByteArray &expectedHash);
	void flushUserCommentRequests();
	void clearUserCommentRequest(unsigned int session);
	void clearUserCommentRequests();

#ifdef Q_OS_WIN
	bool nativeEvent(const QByteArray &eventType, void *message, qintptr *result);
	unsigned int uiNewHardware;
#endif
protected:
	Usage uUsage;
	QTimer *qtReconnect;
	std::unique_ptr< NotificationSoundBlocker > m_reconnectSoundBlocker;

	QList< QAction * > qlServerActions;
	QList< QAction * > qlChannelActions;
	QList< QAction * > qlUserActions;
	QAction *qaServerSettings = nullptr;
	QAction *qaServerOpenStonks = nullptr;
	QAction *qaCreateTextRoom = nullptr;

	QHash< ShortcutTarget, int > qmCurrentTargets;
	/// A map that contains information about the currently active
	/// shout/whisper targets. The mapping is between a List of
	/// ShortcutTargets that are all triggered together and the
	/// target ID for this specific combination of ShortcutTargets.
	/// The target ID is what the server uses to identify this specific
	/// set of ShortcutTargets.
	QHash< QList< ShortcutTarget >, int > qmTargets;
	/// This is a map between all target IDs the client will ever use
	/// and a helper-number (see iTargetCounter).
	QMap< int, int > qmTargetUse;
	Channel *mapChannel(int idx) const;
	/// This is a pure helper number whose job is to always be increased
	/// if a new VoiceTarget is needed. It will be used as the helper
	/// number in qmTargetUse.
	int iTargetCounter;
	QSet< unsigned int > m_pendingUserInformationSessions;
	QTimer *m_userTextureRequestTimer = nullptr;
	QSet< unsigned int > m_queuedUserTextureSessions;
	QSet< unsigned int > m_inFlightUserTextureSessions;
	QHash< unsigned int, QByteArray > m_requestedUserTextureHashBySession;
	QHash< QByteArray, qint64 > m_failedUserTextureHashCooldownUntilMs;
	QTimer *m_userCommentRequestTimer = nullptr;
	QSet< unsigned int > m_queuedUserCommentSessions;
	QSet< unsigned int > m_inFlightUserCommentSessions;
	QHash< unsigned int, QByteArray > m_requestedUserCommentHashBySession;

	QAction *qaTransmitMode;
	QAction *qaTransmitModeSeparator;
	QAction *qaUserRemoteSpeechCleanup        = nullptr;
	QAction *qaUserGrantChatHistory           = nullptr;
	QAction *qaChannelScreenShareStart        = nullptr;
	QAction *qaChannelScreenShareStop         = nullptr;
	QAction *qaChannelScreenShareWatch        = nullptr;
	QAction *qaChannelScreenShareStopWatching = nullptr;
	QAction *qaChannelScreenShareOpenWindow   = nullptr;

	qt_unique_ptr< ListenerVolumeController > m_listenerVolumeController;
	PersistentChatGateway *m_persistentChatGateway                 = nullptr;
	PersistentChatController *m_persistentChatController           = nullptr;
	QString m_persistentChatWelcomeText;
	bool m_persistentChatMotdExpanded        = false;
	bool m_hasPersistentChatSupport          = false;
	bool m_persistentChatTargetUsesVoiceTree = false;
	std::optional< int > m_persistentChatSelectedScopeValue;
	unsigned int m_persistentChatSelectedScopeID  = 0;
	unsigned int m_defaultPersistentTextChannelID = 0;
	std::optional< unsigned int > m_pendingModernShellVoiceJoinScopeID;
	QHash< unsigned int, PersistentTextChannel > m_persistentTextChannels;
	QHash< unsigned int, unsigned int > m_userIdleSeconds;
	std::vector< MumbleProto::ChatMessage > m_persistentChatMessages;
	QHash< QString, PersistentChatPreview > m_persistentChatPreviews;
	QHash< QNetworkReply *, QString > m_persistentChatPreviewNetworkReplies;
	QSet< QString > m_persistentChatPreviewCacheWritesInFlight;
	QSet< QString > m_persistentChatPreviewCacheWritesPending;
	QSet< QString > m_persistentChatPreviewCacheReadsInFlight;
	QSet< QString > m_persistentChatPreviewCacheReadsAttempted;
	bool m_persistentChatPreviewCacheClearInFlight = false;
	QHash< QString, MumbleProto::ChatEmbedRef > m_persistentChatEmbedPreviewRefs;
	QHash< unsigned int, PersistentChatAssetDownload > m_persistentChatAssetDownloads;
	QHash< quint64, PendingChatEmbedAssist > m_pendingChatEmbedAssists;
	QHash< QString, quint64 > m_pendingChatEmbedAssistByKey;
	QHash< QNetworkReply *, quint64 > m_chatEmbedAssistNetworkReplies;
	quint64 m_chatEmbedAssistGeneration = 1;
	QHash< QString, QString > m_persistentChatInlineDataImageSources;
	QHash< QString, QImage > m_persistentChatInlineDataImagePreviewCache;
	QHash< QString, QString > m_persistentChatInlineDataImageThumbnailSourceCache;
	QHash< QString, QString > m_persistentChatInlineDataImageWarmupSources;
	QHash< QString, QSet< QString > > m_persistentChatInlineDataImageWarmupMessageKeys;
	QHash< QString, qint64 > m_persistentChatQueuedInlineDataImageWarmupCosts;
	qint64 m_persistentChatQueuedInlineDataImageWarmupBytes = 0;
	QHash< QString, quint64 > m_persistentChatActiveInlineDataImageWarmups;
	QHash< QString, QString > m_persistentChatInlineDataImageProviderUrls;
	QHash< QString, QSet< QString > > m_persistentChatInlineDataImageProviderMessageKeys;
	QHash< QString, quint64 > m_persistentChatInlineDataImageProviderRequests;
	QHash< QString, PendingFeedbackSubmission > m_pendingFeedbackSubmissions;
	QSet< QString > m_persistentChatLiveMessageKeys;
	QHash< QString, unsigned int > m_persistentChatLastReadByScope;
	QHash< QString, int > m_persistentChatUnreadByScope;
	std::optional< MumbleProto::ChatScope > m_visiblePersistentChatScope;
	unsigned int m_visiblePersistentChatScopeID           = 0;
	unsigned int m_visiblePersistentChatLastReadMessageID = 0;
	unsigned int m_visiblePersistentChatOldestMessageID   = 0;
	bool m_visiblePersistentChatHasMore                   = false;
	PersistentChatLoadingState m_visiblePersistentChatLoadingState = PersistentChatLoadingState::Idle;
	bool m_persistentChatLoadingOlder                     = false;
	QTimer *m_persistentChatPreviewRequestTimer           = nullptr;
	QTimer *m_persistentChatInlineDataImageWarmupTimer     = nullptr;
	QTimer *m_userPresenceRefreshTimer                    = nullptr;
	bool m_persistentChatPreviewRefreshPending            = false;
	QStringList m_persistentChatQueuedPreviewRequests;
	QSet< QString > m_persistentChatQueuedPreviewRequestKeys;
	QStringList m_persistentChatQueuedInlineDataImageWarmups;
	QSet< QString > m_persistentChatQueuedInlineDataImageWarmupKeys;
	quint64 m_persistentChatInlineDataImageWarmupGeneration = 1;
	QSet< QString > m_pendingPersistentChatInstagramMetadataRequests;
	std::optional< MumbleProto::ChatMessage > m_pendingPersistentChatReply;
	QTimer *m_modernShellSyncTimer                = nullptr;
	QTimer *m_nativeWindowMoveResizeRecoveryTimer = nullptr;
	qint64 m_lastQmlStateSyncMs        = 0;
	quint64 m_modernShellServerLogRevision = 1;
	QVariantList m_modernServerLogEntries;
	int m_modernServerLogMaximumEntries = 0;
	QJsonObject m_updateResumeState;
	bool m_updateResumePending                 = false;
	bool m_updateResumeConnectAttempted        = false;
	bool m_updateResumeVoiceChannelApplied     = false;
	bool m_updateResumeChatScopeApplied        = false;
	bool m_updateResumeTextChannelSyncObserved = false;
	std::unique_ptr< QmlShellHost > m_qmlShellHost;
	QHash< QString, PluginInstallService::PreparedPackage > m_pendingPluginInstalls;
	QHash< QString, std::shared_ptr< PluginCancellationGate > > m_pluginInstallCancellation;
	struct PendingPluginLoadedTransition {
		qulonglong pluginID = 0;
		QString settingsKey;
		QString pluginPath;
		QString pluginName;
		bool desiredLoaded      = false;
		bool persistSettings    = false;
		bool positionalEnabled  = false;
		bool showSuccessToast   = false;
	};
	QHash< QString, PendingPluginLoadedTransition > m_pendingPluginLoadedTransitions;
	std::unique_ptr< ModernDialogController > m_modernDialogController;
	QStringList m_modernStartupDialogQueue;
#	if defined(MUMBLE_HAS_MODERN_UI_AUTOMATION)
	std::unique_ptr< ModernUiAutomationServer > m_modernUiAutomationServer;
#	endif
	QVariantMap m_stonksState;
	QVariantMap m_modernConnectionStateProbe;
	QVariantMap m_modernScreenShareStateProbe;
	struct PendingScreenShareThumbnailJob {
		quint64 generation = 0;
		QString sourceID;
		std::shared_ptr< std::atomic< bool > > cancellation;
	};
	quint64 m_screenSharePickerGeneration = 0;
	quint64 m_nextScreenShareThumbnailJobID = 0;
	std::shared_ptr< std::atomic< bool > > m_screenShareDiscoveryCancellation;
	QHash< quint64, PendingScreenShareThumbnailJob > m_pendingScreenShareThumbnailJobs;
	QHash< QString, quint64 > m_pendingScreenShareThumbnailJobBySource;
	bool m_screenSharePickerShuttingDown = false;
	QVariantList m_modernRichPreviewProbeMessages;
	QVariantList m_modernMessageDeliveryProbeMessages;
	QString m_stonksSelectedPeriod;
	std::optional< unsigned int > m_stonksSelectedUserID;
	QVariantMap m_modernFeedbackDraftValues;
	qint64 m_modernFeedbackCaptureStartOffset = -1;
	bool m_modernFeedbackCaptureActive        = false;
	std::optional< PendingFeedbackSubmission > m_modernFeedbackFallbackSubmission;
	QJsonObject m_modernVersionCheckInfo;
	QString m_modernPreparedUpdateInstallerPath;
	QString m_modernPreparedFallbackInstallerPath;
	QVariantMap m_modernUpdateBannerState;
	qint64 m_modernUpdateLastProgressPublishMs = 0;
	int m_modernUpdateLastProgressPercent      = -1;
	bool m_modernUpdateDownloadInProgress = false;
	QMetaObject::Connection m_modernShortcutCaptureConnection;
	int m_modernShortcutCaptureRow = -1;
	bool m_modernShortcutCaptureRestoreEnabled = false;
	bool m_modernShortcutCaptureHasRestore     = false;
	std::unique_ptr< ModernConnectPingState > m_modernConnectPingState;
	quint64 m_qmlOperationRevision                         = 0;
	quint64 m_qmlMessageGeneration                         = 0;
	quint64 m_modernShellMessageDtoContextRevision         = 1;
	QHash< QString, QVariantMap > m_modernShellMessageDtoCache;
	QHash< QString, QString > m_modernShellActorAvatarDataUrls;
	QHash< unsigned int, ModernDirectMessageConversation > m_modernDirectMessageConversations;
	quint64 m_modernDirectMessageLocalID = 0;
	bool m_modernDirectMessageTrayOpenProbe = false;
	QTimer *m_modernShellPreviewHydrationTimer             = nullptr;
	QString m_modernShellPreviewHydrationScopeToken;
	QList< qulonglong > m_modernShellPreviewHydrationQueue;
	QSet< qulonglong > m_modernShellPreviewHydrationQueuedIds;
	bool m_modernShellPreviewHydrationLinkDense = false;
	QTimer *m_qmlRoomStateFlushTimer                = nullptr;
	bool m_qmlRoomStateDirty               = false;
	bool m_qmlStatePendingAfterNativeMoveResize = false;
	QHash< int, QString > m_modernAclRegisteredUserNames;
	bool m_modernAclUserListRequestPending = false;
	bool m_shellLayoutInitialized              = false;
	Settings::WindowLayout m_activeShellLayout = Settings::LayoutModern;
	bool m_modernLayoutCompatibleServer        = false;
	bool m_nativeWindowMoveResizeActive        = false;

	std::stack< unsigned int > m_previousChannels;
	std::optional< unsigned int > m_movedBackFromChannel;


	void createActions();
	void initializeBaseActions();
	void connectToServer(const QString &host, unsigned short port, const QString &username, const QString &password,
						 const QString &serverName, const QString &desiredChannel = QString());
	void beginServerHandlerRecreation();
	void scheduleServerHandlerRecreation(int delayMs = 0);
	void finishServerHandlerRecreation();
	void installPendingServerConnection();
	void pruneRetiredServerHandlers();
	void publishModernDialogState(const QVariantMap &state);
	void openModernConnectDialog();
	void openModernSettingsDialog(const QString &pageName = QString());
	bool openModernFailedConnectionDialog(const ConnectDetails &details, ConnectionFailType type);
	bool openModernSslCertificateWarningDialog(const QString &host, unsigned short port,
											   const QList< QSslCertificate > &certificates,
											   const QList< QSslError > &errors);
	bool openModernSslHandshakeFailureDialog(const QString &reason);
	void openModernSslCertificateDetailsDialog(const QVariantMap &context);
	void openModernDisconnectDialog();
	void openModernQuitDialog(bool allowMinimize);
	void openModernGenericDialog(const QVariantMap &dialog);
	void openModernDeveloperConsoleDialog();
	void openModernServerInformationDialog();
	void openModernServerTokensDialog(const QStringList &tokens = QStringList(), bool useProvidedTokens = false);
	void openModernServerUserListLoadingDialog();
	void openModernServerUserListDialog(const MumbleProto::UserList &msg);
	void openModernServerBanListLoadingDialog();
	void openModernServerBanListDialog(const MumbleProto::BanList &msg);
	void openModernAclRequestDialog(Channel *channel);
	void openModernAclDialog(const MumbleProto::ACL &msg);
	bool handleModernAclQueryUsers(const MumbleProto::QueryUsers &msg);
	bool handleModernAclUserList(const MumbleProto::UserList &msg);
	void openModernCertificateDialog(const QVariantMap &fieldValues = QVariantMap(),
									 const QVariantMap &errors = QVariantMap(),
									 const QString &statusMessage = QString());
	void openModernSearchDialog(const QVariantMap &fieldValues = QVariantMap(),
								const QVariantMap &errors = QVariantMap());
	void openModernVoiceRecorderDialog(const QVariantMap &fieldValues = QVariantMap(),
									   const QVariantMap &errors = QVariantMap(),
									   const QString &statusMessage = QString());
	void openModernCreateRoomDialog(RoomCreateType preferredType, Channel *preferredVoiceParent = nullptr,
									const QVariantMap &fieldValues = QVariantMap(),
									const QVariantMap &errors = QVariantMap());
	void openModernEditTextRoomDialog(unsigned int textChannelID, const QVariantMap &fieldValues = QVariantMap(),
									  const QVariantMap &errors = QVariantMap());
	void openModernDeleteTextRoomDialog(unsigned int textChannelID);
	void openModernServerSettingsDialog(const QVariantMap &fieldValues = QVariantMap(),
										const QVariantMap &errors = QVariantMap(),
										const QString &statusMessage = QString());
	void openModernAudioStatsDialog();
	void openModernAboutDialog();
	void openModernAboutQtDialog();
	void openModernVersionCheckDialog();
	void openModernVersionCheckLoadingDialog();
	void openModernVersionCheckResultDialog(const QJsonObject &info, bool updateAvailable);
	void openModernVersionCheckFailureDialog(const QString &message);
	void openModernHelpDialog();
	void openModernFeedbackDialog(const QVariantMap &fieldValues = QVariantMap(),
								  const QVariantMap &errors = QVariantMap(),
								  const QString &statusMessage = QString());
	void openModernFeedbackResultDialog(const PendingFeedbackSubmission &submission, const QString &title,
										const QString &subtitle, const QString &statusMessage,
										const QString &primaryActionID = QString(),
										const QString &openActionLabel = QString());
	void openModernSelfRegisterDialog();
	void openModernUserRegisterDialog(ClientUser *user);
	void openModernSelfCommentDialog();
	void openModernKickUserDialog(ClientUser *user);
	void openModernBanUserDialog(ClientUser *user);
	void openModernChatHistoryGrantDialog(ClientUser *user);
	void openModernLocalNicknameDialog(const ClientUser *user);
	void openModernUserCommentDialog(ClientUser *user);
	void openModernUserCommentResetDialog(ClientUser *user);
	void openModernUserTextureChangeDialog(ClientUser *user, const QVariantMap &fieldValues = QVariantMap(),
										   const QVariantMap &errors = QVariantMap());
	void requestModernUserTextureFromUrl(unsigned int session, const QUrl &url, const QVariantMap &fieldValues,
										 int redirectCount = 0);
	void openModernUserTextureResetDialog(ClientUser *user);
	void openModernUserInformationRequestDialog(ClientUser *user);
	void openModernUserInformationDialog(const MumbleProto::UserStats &msg);
	void openModernRemoveChannelDialog(Channel *channel);
	void openModernUnlinkChannelDialog(Channel *source, Channel *target);
	void openModernUnlinkAllChannelsDialog(Channel *source);
	void openModernUrlConnectDialog(const QUrl &url, const QString &host, unsigned short port,
									const QString &password, const QString &serverName,
									const QString &desiredChannel, const QString &username = QString(),
									const QVariantMap &errors = QVariantMap());
	void openModernDragUserConfirmDialog(unsigned int session, const QString &targetScopeToken,
										 const QString &userName, const QString &targetRoomName);
	void openModernDragChannelConfirmDialog(const QString &sourceScopeToken, const QString &targetScopeToken,
											const QString &placement, const QString &sourceRoomName,
											const QString &targetRoomName);
	void openModernChannelMoveUnavailableDialog();
	bool moveModernShellParticipant(unsigned int session, const QString &targetScopeToken, bool confirmIfNeeded);
	bool moveModernShellChannel(const QString &sourceScopeToken, const QString &targetScopeToken,
								const QString &placement, bool confirmIfNeeded);
	bool handleModernGenericDialogAction(const QString &dialogID, const QString &actionID,
										 const QVariantMap &fieldValues, const QVariantMap &payload);
	void beginAsyncPluginInstall(const QString &path);
	bool cancelPendingPluginInstallConfirmation(const QString &operationID);
	void commitAsyncPluginInstall(const QString &operationID, PluginInstallService::PreparedPackage package,
								 bool allowOverwrite);
	void reconcileAsyncPluginLoadedTransition(const QString &operationID, qulonglong pluginID, bool success,
										   const QString &message);
	void refreshOpenModernPluginSettings();
	bool handleModernFeedbackDialogAction(const QString &dialogID, const QString &actionID,
										  const QVariantMap &fieldValues);
	bool tryModernAutoConnectLastServer();
	bool handleModernShellLegacyDialogAction(const QString &actionID, ClientUser *contextUser = nullptr,
											 Channel *contextChannel = nullptr);
	void handleModernDialogOpen(const QString &dialogID, const QVariantMap &context);
	void handleModernDialogClose(const QString &dialogID);
	void handleModernDialogFieldUpdate(const QString &dialogID, const QString &fieldID, const QVariant &value);
	void handleModernDialogAction(const QString &dialogID, const QString &actionID, const QVariantMap &payload);
	void connectFromModernDialog(const QString &host, unsigned short port, const QString &username,
								 const QString &password);
	void applyModernSettings(const Settings &settings, bool accepted);
	void openNextModernStartupDialog();
	bool beginModernShortcutCapture(int rowIndex);
	void cancelModernShortcutCapture();
	void setupGui();
	void updateWindowTitle();
	/// updateToolbar updates the state of the toolbar depending on the current
	/// window layout setting.
	void updateFavoriteButton();
	void openFeedbackDialog();
	void handleFeedbackReportState(const MumbleProto::FeedbackReportState &msg);
	void showFeedbackFallback(const PendingFeedbackSubmission &submission, const QString &error);
	void customEvent(QEvent *evt) Q_DECL_OVERRIDE;
	void findDesiredChannel();
	void setupView(bool toggle_minimize = true);
	bool eventFilter(QObject *watched, QEvent *event) Q_DECL_OVERRIDE;
	void closeEvent(QCloseEvent *e);


	bool handleSpecialContextMenu(const QUrl &url, const QPoint &pos_, bool focus = false);
	Channel *getContextMenuChannel();
	ClientUser *getContextMenuUser();
	ContextMenuTarget getContextMenuTargets();
	QString screenShareStreamForChannel(const Channel *channel) const;


public slots:
	void appendModernServerLogEntry(const QString &html);
	void refreshServerActions();
	void on_qaServerConnect_triggered(bool autoconnect = false);
	void on_qaServerDisconnect_triggered();
	void on_qaServerBanList_triggered();
	void on_qaServerUserList_triggered();
	void on_qaServerInformation_triggered();
	void on_qaServerTexture_triggered();
	void on_qaServerTextureRemove_triggered();
	void on_qaServerTokens_triggered();
	void refreshSelfActions();
	void on_qaSelfComment_triggered();
	void on_qaSelfRegister_triggered();
	void qmUser_aboutToShow();
	void qmListener_aboutToShow();
	void on_qaUserCommentReset_triggered();
	void on_qaUserTextureReset_triggered();
	void on_qaUserCommentView_triggered();
	void on_qaUserKick_triggered();
	void on_qaUserBan_triggered();
	void on_qaUserMute_triggered();
	void on_qaUserDeaf_triggered();
	void on_qaSelfPrioritySpeaker_triggered();
	void on_qaUserPrioritySpeaker_triggered();
	void on_qaUserLocalIgnore_triggered();
	void on_qaUserLocalIgnoreTTS_triggered();
	void on_qaUserLocalMute_triggered();
	void triggerUserRemoteSpeechCleanup();
	void on_qaUserLocalNickname_triggered();
	void on_qaUserTextMessage_triggered();
	void on_qaUserRegister_triggered();
	void on_qaUserInformation_triggered();
	void on_qaUserFriendAdd_triggered();
	void on_qaUserFriendRemove_triggered();
	void on_qaUserFriendUpdate_triggered();
	void qmChannel_aboutToShow();
	void on_qaChannelJoin_triggered();
	void on_qaUserJoin_triggered();
	void on_qaUserMove_triggered();
	void on_qaChannelListen_triggered();
	void on_qaChannelAdd_triggered();
	void on_qaChannelRemove_triggered();
	void on_qaChannelACL_triggered();
	void on_qaChannelLink_triggered();
	void on_qaChannelUnlink_triggered();
	void on_qaChannelUnlinkAll_triggered();
	void on_qaChannelSendMessage_triggered();
	void on_qaChannelHide_triggered();
	void on_qaChannelPin_triggered();
	void on_qaChannelCopyURL_triggered();
	void startChannelScreenShare();
	void stopChannelScreenShare();
	void watchChannelScreenShare();
	void stopWatchingChannelScreenShare();
	void openChannelScreenShareWindow();
	void openModernScreenShareDialog(Channel *channel);
	QVariantMap buildModernScreenShareState(Channel *channel);
	QVariantMap buildModernScreenShareDialogDto(Channel *channel);
	bool handleModernScreenShareDialogAction(const QString &actionID, const QVariantMap &payload);
	void beginModernScreenShareDiscovery(quint64 generation);
	void requestModernScreenShareThumbnail(const QString &sourceId, quint64 generation);
	void finishModernScreenShareThumbnail(quint64 jobID, const QImage &image);
	void cancelModernScreenSharePickerJobs();
	bool openScreenShareWindowOrStatus(const QString &streamID);
	void on_qaAudioReset_triggered();
	void on_qaAudioMute_triggered();
	void on_qaAudioDeaf_triggered();
	void on_qaRecording_triggered();
	void on_qaAudioTTS_triggered();
	void on_qaAudioUnlink_triggered();
	void on_qaAudioStats_triggered();
	void on_qaConfigDialog_triggered();
	void refreshConfigActions();
	void on_qaConfigMinimal_triggered();
	void on_qaConfigCert_triggered();
	void on_qaAudioWizard_triggered();
	void on_qaDeveloperConsole_triggered();
	void on_qaHelpWhatsThis_triggered();
	void on_qaHelpAbout_triggered();
	void on_qaHelpAboutQt_triggered();
	void on_qaHelpFeedback_triggered();
	void on_qaHelpVersionCheck_triggered();
	void on_qaQuit_triggered();
	void on_persistentChatScopeChanged(int index);
	void on_PushToTalk_triggered(bool, QVariant);
	void on_PushToMute_triggered(bool, QVariant);
	void on_VolumeUp_triggered(bool, QVariant);
	void on_VolumeDown_triggered(bool, QVariant);
	void on_gsMuteSelf_down(QVariant);
	void on_gsDeafSelf_down(QVariant);
	void on_gsWhisper_triggered(bool, QVariant);
	void addTarget(ShortcutTarget *);
	void removeTarget(ShortcutTarget *);
	void on_gsListenChannel_triggered(bool, QVariant);
	void on_gsCycleTransmitMode_triggered(bool, QVariant);
	void on_gsToggleMainWindowVisibility_triggered(bool, QVariant);
	void on_gsTransmitModePushToTalk_triggered(bool, QVariant);
	void on_gsTransmitModeContinuous_triggered(bool, QVariant);
	void on_gsTransmitModeVAD_triggered(bool, QVariant);
	void on_gsSendTextMessage_triggered(bool, QVariant);
	void on_gsSendClipboardTextMessage_triggered(bool, QVariant);
	void on_gsToggleSearch_triggered(bool, QVariant);
	void on_gsServerConnect_triggered(bool, QVariant);
	void on_gsServerDisconnect_triggered(bool, QVariant);
	void on_gsServerInformation_triggered(bool, QVariant);
	void on_gsServerTokens_triggered(bool, QVariant);
	void on_gsServerUserList_triggered(bool, QVariant);
	void on_gsServerBanList_triggered(bool, QVariant);
	void on_gsSelfPrioritySpeaker_triggered(bool, QVariant);
	void on_gsRecording_triggered(bool, QVariant);
	void on_gsSelfComment_triggered(bool, QVariant);
	void on_gsServerTexture_triggered(bool, QVariant);
	void on_gsServerTextureRemove_triggered(bool, QVariant);
	void on_gsSelfRegister_triggered(bool, QVariant);
	void on_gsAudioStats_triggered(bool, QVariant);
	void on_gsConfigDialog_triggered(bool, QVariant);
	void on_gsAudioWizard_triggered(bool, QVariant);
	void on_gsConfigCert_triggered(bool, QVariant);
	void on_gsAudioTTS_triggered(bool, QVariant);
	void on_gsHelpAbout_triggered(bool, QVariant);
	void on_gsHelpAboutQt_triggered(bool, QVariant);
	void on_gsHelpVersionCheck_triggered(bool, QVariant);
	void on_gsTogglePositionalAudio_triggered(bool, QVariant);
	void on_gsMoveBack_triggered(bool, QVariant);
	void on_gsCycleListenerAttenuationMode_triggered(bool, QVariant);
	void on_gsListenerAttenuationUp_triggered(bool, QVariant);
	void on_gsListenerAttenuationDown_triggered(bool, QVariant);
	void on_gsAdaptivePush_triggered(bool, QVariant);

	void on_Reconnect_timeout();
	void serverConnected();
	void serverDisconnected(QAbstractSocket::SocketError, QString reason);
	void resolverError(QAbstractSocket::SocketError, QString reason);
	void openUrl(const QUrl &url);
	void context_triggered();
	void updateTarget();
	void updateMenuPermissions();
	void showMuteCuePopup();
	/// Handles state changes like talking mode changes and mute/unmute
	/// or priority speaker flag changes for the gui user
	void userStateChanged();
	void on_channelStateChanged(Channel *channel, bool forceUpdateTree);
	void sendChatbarMessage(QString msg);
	void sendChatbarText(QString msg, bool plainText = false);
	void pttReleased();
	void whisperReleased(QVariant scdata);
	void onResetAudio();
	void showRaiseWindow();
	void on_qaFilterToggle_triggered();
	/// Opens a save dialog for the image selected from the log or persistent chat history.
	void saveImageAs();
	/// Returns the path to the user's image directory, optionally with a
	/// filename included.
	QString getImagePath(QString filename = QString()) const;

	/// Shows a dialog with the currently selected log or chat image.
	void showImageDialog();
	/// Updates the user's image directory to the given path (any included
	/// filename is discarded).
	void updateImagePath(QString filepath) const;
	void setTransmissionMode(Settings::AudioTransmit mode);
	/// Sets the local user's mute state
	///
	/// @param mute Whether to mute the user
	void setAudioMute(bool mute);
	/// Sets the local user's deaf state
	///
	/// @param deaf Whether to deafen the user
	void setAudioDeaf(bool deaf);
	// Callback the search action being triggered
	void on_qaSearch_triggered();
	void toggleSearchDialogVisibility();
	/// Enables or disables the recording feature
	void enableRecording(bool recordingAllowed);
	/// Invokes OS native window highlighting
	void highlightWindow();
	void handleUserMoved(unsigned int sessionID, const std::optional< unsigned int > &prevChannelID,
						 unsigned int newChannelID);
	void on_qaMoveBack_triggered();
signals:
	/// Signal emitted when the server and the client have finished
	/// synchronizing (after a new connection).
	void serverSynchronized();
	/// Signal emitted whenever a user adds a new ChannelListener
	void userAddedChannelListener(ClientUser *user, Channel *channel);
	/// Signal emitted whenever a user removes a ChannelListener
	void userRemovedChannelListener(ClientUser *user, Channel *channel);
	void transmissionModeChanged(Settings::AudioTransmit newMode);

	/// Signal emitted when the local user changes their talking status either actively or passively
	void talkingStatusChanged();

	/// Signal when channel state has been changed
	void channelStateChanged(Channel *channel, bool forceUpdateTree);

	/// Signal emitted when the connection was terminated and all cleanup code has been run
	void disconnectedFromServer();

	/// Signal emitted when the window manager notifies the Mumble MainWindow that the application was just minimized
	void windowMinimized();
	/// Signal emitted when the user requested to toggle the MainWindow visibility
	void windowVisibilityToggled();
	/// Signal emitted whenever the Mumble MainWindow regains the active state from the window manager
	void windowActivated();

public:
	MainWindow(QObject *parent);
	~MainWindow() Q_DECL_OVERRIDE;
	std::optional< unsigned int > userIdleSeconds(unsigned int session) const;
	bool isUserIdle(unsigned int session) const;
	void refreshUserPresenceStats();
	bool hasPendingUpdateResumeState() const;
	void prepareUpdateResumeState();
	void clearPendingUpdateResumeState();

	// Implementation in Messages.cpp
#define PROCESS_MUMBLE_TCP_MESSAGE(name, value) void msg##name(const MumbleProto::name &);
	MUMBLE_ALL_TCP_MESSAGES
#undef PROCESS_MUMBLE_TCP_MESSAGE
	void removeContextAction(const MumbleProto::ContextActionModify &msg);
	/// Logs a message that an action could not be saved permanently because
	/// the user has no certificate and can't be reliably identified.
	///
	/// @param actionName  The name of the action that has been executed.
	/// @param p  The user on which the action was performed.
	void logChangeNotPermanent(const QString &actionName, ClientUser *const p) const;

	void openServerConnectDialog(bool autoconnect = false);
	void disconnectFromServer();
	void addServerAsFavorite();
	void openServerInformationDialog();
	void openServerTokensDialog();
	void openServerUserListDialog();
	void openServerBanListDialog();
	void toggleSelfPrioritySpeaker();
	void recording();
	void openSelfCommentDialog();
	void changeServerTexture();
	void removeServerTexture();
	void selfRegister();
	void openAudioStatsDialog();
	void openConfigDialog();
	void openConfigDialogPage(const QString &pageName);
	void openAudioWizardDialog();
	void openCertWizardDialog();
	void enableAudioTTS(bool enable);
	void openAboutDialog();
	void openAboutQtDialog();
	void versionCheck();
	void enablePositionalAudio(bool enable);
};

#endif
