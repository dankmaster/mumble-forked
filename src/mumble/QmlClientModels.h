// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_QMLCLIENTMODELS_H_
#define MUMBLE_MUMBLE_QMLCLIENTMODELS_H_

#include <QtCore/QAbstractListModel>
#include <QtCore/QByteArray>
#include <QtCore/QHash>
#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>
#include <QtCore/QUrl>

#include <atomic>
#include <deque>
#include <functional>
#include <memory>

class ClientActionRegistry;
class QTimer;

namespace ModernMotd {
	QString serverStateKey(const QByteArray &serverDigest, const QString &host, quint16 port);
	QVariantMap serverViewState(const QString &serializedStates, const QString &serverKey);
	QString withServerViewState(const QString &serializedStates, const QString &serverKey,
							const QVariantMap &state);
	QVariantList documentBlocks(const QString &html);
}

namespace QmlVisualFixtureMutation {
	inline constexpr char OverrideProperty[] = "_mumbleVisualFixtureOverride";
	inline constexpr char WriteProperty[] = "_mumbleVisualFixtureWrite";
}

class ClientSessionController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString serverName READ serverName WRITE setServerName NOTIFY serverNameChanged)
	Q_PROPERTY(QString serverMonogram READ serverMonogram WRITE setServerMonogram NOTIFY serverMonogramChanged)
	Q_PROPERTY(QString serverImageUrl READ serverImageUrl WRITE setServerImageUrl NOTIFY serverImageUrlChanged)
	Q_PROPERTY(QString connectionLabel READ connectionLabel WRITE setConnectionLabel NOTIFY connectionLabelChanged)
	Q_PROPERTY(QString selfStatusLabel READ selfStatusLabel WRITE setSelfStatusLabel NOTIFY selfStatusLabelChanged)
	Q_PROPERTY(QString connectionState READ connectionState WRITE setConnectionState NOTIFY connectionStateChanged)
	Q_PROPERTY(QString connectionTone READ connectionTone WRITE setConnectionTone NOTIFY connectionToneChanged)
	Q_PROPERTY(QString connectionDetail READ connectionDetail WRITE setConnectionDetail NOTIFY connectionDetailChanged)
	Q_PROPERTY(int connectionRetryRemainingMs READ connectionRetryRemainingMs WRITE setConnectionRetryRemainingMs NOTIFY connectionRetryRemainingMsChanged)
	Q_PROPERTY(bool canConnect READ canConnect WRITE setCanConnect NOTIFY canConnectChanged)
	Q_PROPERTY(bool canCancel READ canCancel WRITE setCanCancel NOTIFY canCancelChanged)
	Q_PROPERTY(QString selfName READ selfName WRITE setSelfName NOTIFY selfNameChanged)
	Q_PROPERTY(bool connected READ connected WRITE setConnected NOTIFY connectedChanged)
	Q_PROPERTY(bool selfMuted READ selfMuted WRITE setSelfMuted NOTIFY selfMutedChanged)
	Q_PROPERTY(bool selfDeafened READ selfDeafened WRITE setSelfDeafened NOTIFY selfDeafenedChanged)
	Q_PROPERTY(QVariantList appMenus READ appMenus WRITE setAppMenus NOTIFY appMenusChanged)
	Q_PROPERTY(QVariantMap selfMenu READ selfMenu WRITE setSelfMenu NOTIFY selfMenuChanged)
	Q_PROPERTY(QVariantMap updateBanner READ updateBanner WRITE setUpdateBanner NOTIFY updateBannerChanged)
	Q_PROPERTY(QVariantMap stonks READ stonks WRITE setStonks NOTIFY stonksChanged)
	Q_PROPERTY(QStringList collapsedNavigationSections READ collapsedNavigationSections
				   WRITE setCollapsedNavigationSections NOTIFY collapsedNavigationSectionsChanged)
	Q_PROPERTY(QVariantList motdSegments READ motdSegments NOTIFY motdSegmentsChanged)
	Q_PROPERTY(QVariantList motdBlocks READ motdBlocks NOTIFY motdBlocksChanged)
	Q_PROPERTY(QString motdSummary READ motdSummary WRITE setMotdSummary NOTIFY motdSummaryChanged)
	Q_PROPERTY(bool hasMotd READ hasMotd NOTIFY hasMotdChanged)
	Q_PROPERTY(bool motdExpanded READ motdExpanded WRITE setMotdExpanded NOTIFY motdExpandedChanged)
	Q_PROPERTY(bool motdDismissed READ motdDismissed NOTIFY motdDismissedChanged)
	Q_PROPERTY(QString motdSignature READ motdSignature NOTIFY motdSignatureChanged)
	Q_PROPERTY(QString motdDismissedSignature READ motdDismissedSignature WRITE setMotdDismissedSignature NOTIFY motdDismissedSignatureChanged)
	Q_PROPERTY(QString motdLastSeenSignature READ motdLastSeenSignature WRITE setMotdLastSeenSignature NOTIFY motdLastSeenSignatureChanged)
	Q_PROPERTY(bool motdChanged READ motdChanged NOTIFY motdChangedChanged)
	Q_PROPERTY(QVariantList motdActions READ motdActions NOTIFY motdActionsChanged)

public:
	explicit ClientSessionController(QObject *parent = nullptr);

	QString serverName() const;
	QString serverMonogram() const;
	QString serverImageUrl() const;
	QString connectionLabel() const;
	QString selfStatusLabel() const;
	QString connectionState() const;
	QString connectionTone() const;
	QString connectionDetail() const;
	int connectionRetryRemainingMs() const;
	bool canConnect() const;
	bool canCancel() const;
	QString selfName() const;
	bool connected() const;
	bool selfMuted() const;
	bool selfDeafened() const;
	QVariantList appMenus() const;
	QVariantMap selfMenu() const;
	QVariantMap updateBanner() const;
	QVariantMap stonks() const;
	QStringList collapsedNavigationSections() const;
	QString motdHtml() const;
	QVariantList motdSegments() const;
	QVariantList motdBlocks() const;
	QString motdSummary() const;
	bool hasMotd() const;
	bool motdExpanded() const;
	bool motdDismissed() const;
	QString motdSignature() const;
	QString motdDismissedSignature() const;
	QString motdLastSeenSignature() const;
	bool motdChanged() const;
	QVariantList motdActions() const;

	void setServerName(const QString &value);
	void setServerMonogram(const QString &value);
	void setServerImageUrl(const QString &value);
	void setConnectionLabel(const QString &value);
	void setSelfStatusLabel(const QString &value);
	void setConnectionState(const QString &value);
	void setConnectionTone(const QString &value);
	void setConnectionDetail(const QString &value);
	void setConnectionRetryRemainingMs(int value);
	void setCanConnect(bool value);
	void setCanCancel(bool value);
	void setSelfName(const QString &value);
	void setConnected(bool value);
	void setSelfMuted(bool value);
	void setSelfDeafened(bool value);
	void setAppMenus(const QVariantList &value);
	void setSelfMenu(const QVariantMap &value);
	void setUpdateBanner(const QVariantMap &value);
	void setStonks(const QVariantMap &value);
	void setCollapsedNavigationSections(const QStringList &value);
	Q_INVOKABLE void setNavigationSectionExpanded(const QString &sectionKind, bool expanded);
	void setMotdHtml(const QString &value);
	void setMotdContent(const QString &html, const QString &signatureIdentity);
	void setMotdSummary(const QString &value);
	void setMotdExpanded(bool value);
	void setMotdDismissedSignature(const QString &value);
	void setMotdLastSeenSignature(const QString &value);
	void applyState(const QVariantMap &state);

signals:
	void serverNameChanged();
	void serverMonogramChanged();
	void serverImageUrlChanged();
	void connectionLabelChanged();
	void selfStatusLabelChanged();
	void connectionStateChanged();
	void connectionToneChanged();
	void connectionDetailChanged();
	void connectionRetryRemainingMsChanged();
	void canConnectChanged();
	void canCancelChanged();
	void selfNameChanged();
	void connectedChanged();
	void selfMutedChanged();
	void selfDeafenedChanged();
	void appMenusChanged();
	void selfMenuChanged();
	void updateBannerChanged();
	void stonksChanged();
	void collapsedNavigationSectionsChanged();
	void motdHtmlChanged();
	void motdSegmentsChanged();
	void motdBlocksChanged();
	void motdSummaryChanged();
	void hasMotdChanged();
	void motdExpandedChanged();
	void motdDismissedChanged();
	void motdSignatureChanged();
	void motdDismissedSignatureChanged();
	void motdLastSeenSignatureChanged();
	void motdChangedChanged();
	void motdActionsChanged();

private:
	void recomputeMotdDerivedState();
	QString m_serverName = QStringLiteral("Mumble");
	QString m_serverMonogram;
	QString m_serverImageUrl;
	QString m_connectionLabel = QStringLiteral("Disconnected");
	QString m_selfStatusLabel = QStringLiteral("Offline");
	QString m_connectionState = QStringLiteral("disconnected");
	QString m_connectionTone = QStringLiteral("muted");
	QString m_connectionDetail;
	int m_connectionRetryRemainingMs = 0;
	bool m_canConnect = true;
	bool m_canCancel = false;
	QString m_selfName = QStringLiteral("You");
	bool m_connected = false;
	bool m_selfMuted = false;
	bool m_selfDeafened = false;
	QVariantList m_appMenus;
	QVariantMap m_selfMenu;
	QVariantMap m_updateBanner;
	QVariantMap m_stonks;
	QStringList m_collapsedNavigationSections;
	QString m_motdHtml;
	QString m_motdContentSignature;
	QVariantList m_motdSegments;
	QVariantList m_motdBlocks;
	quint64 m_motdParseGeneration = 0;
	QString m_motdSummary;
	bool m_hasMotd = false;
	bool m_motdExpanded = false;
	bool m_motdDismissed = false;
	QString m_motdSignature;
	QString m_motdDismissedSignature;
	QString m_motdLastSeenSignature;
	bool m_motdChanged = false;
	QVariantList m_motdActions;
};

class ActiveScopeController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString scopeToken READ scopeToken WRITE setScopeToken NOTIFY scopeTokenChanged)
	Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
	Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
	Q_PROPERTY(QString kindLabel READ kindLabel WRITE setKindLabel NOTIFY kindLabelChanged)
	Q_PROPERTY(QString composerPlaceholder READ composerPlaceholder WRITE setComposerPlaceholder NOTIFY composerPlaceholderChanged)
	Q_PROPERTY(QString composerHint READ composerHint WRITE setComposerHint NOTIFY composerHintChanged)
	Q_PROPERTY(bool activity READ activity WRITE setActivity NOTIFY activityChanged)
	Q_PROPERTY(bool canSend READ canSend WRITE setCanSend NOTIFY canSendChanged)
	Q_PROPERTY(bool hasPendingReply READ hasPendingReply WRITE setHasPendingReply NOTIFY hasPendingReplyChanged)
	Q_PROPERTY(QString replyActor READ replyActor WRITE setReplyActor NOTIFY replyActorChanged)
	Q_PROPERTY(QString replySnippet READ replySnippet WRITE setReplySnippet NOTIFY replySnippetChanged)
	Q_PROPERTY(bool canAttachImages READ canAttachImages WRITE setCanAttachImages NOTIFY canAttachImagesChanged)
	Q_PROPERTY(bool canAttachFiles READ canAttachFiles WRITE setCanAttachFiles NOTIFY canAttachFilesChanged)
	Q_PROPERTY(bool canLoadOlder READ canLoadOlder WRITE setCanLoadOlder NOTIFY canLoadOlderChanged)
	Q_PROPERTY(qulonglong unreadCount READ unreadCount WRITE setUnreadCount NOTIFY unreadCountChanged)
	Q_PROPERTY(bool canMarkRead READ canMarkRead WRITE setCanMarkRead NOTIFY canMarkReadChanged)
	Q_PROPERTY(bool loading READ loading WRITE setLoading NOTIFY loadingChanged)
	Q_PROPERTY(QString loadingState READ loadingState WRITE setLoadingState NOTIFY loadingStateChanged)
	Q_PROPERTY(QVariantMap screenShare READ screenShare WRITE setScreenShare NOTIFY screenShareChanged)

public:
	explicit ActiveScopeController(QObject *parent = nullptr);
	QString scopeToken() const;
	QString label() const;
	QString description() const;
	QString kindLabel() const;
	QString composerPlaceholder() const;
	QString composerHint() const;
	bool activity() const;
	bool canSend() const;
	bool hasPendingReply() const;
	QString replyActor() const;
	QString replySnippet() const;
	bool canAttachImages() const;
	bool canAttachFiles() const;
	bool canLoadOlder() const;
	qulonglong unreadCount() const;
	bool canMarkRead() const;
	bool loading() const;
	QString loadingState() const;
	QVariantMap screenShare() const;
	void setScopeToken(const QString &value);
	void setLabel(const QString &value);
	void setDescription(const QString &value);
	void setKindLabel(const QString &value);
	void setComposerPlaceholder(const QString &value);
	void setComposerHint(const QString &value);
	void setActivity(bool value);
	void setCanSend(bool value);
	void setHasPendingReply(bool value);
	void setReplyActor(const QString &value);
	void setReplySnippet(const QString &value);
	void setCanAttachImages(bool value);
	void setCanAttachFiles(bool value);
	void setCanLoadOlder(bool value);
	void setUnreadCount(qulonglong value);
	void setCanMarkRead(bool value);
	void setLoading(bool value);
	void setLoadingState(const QString &value);
	void setScreenShare(const QVariantMap &value);
	void applyState(const QVariantMap &state);

signals:
	void scopeTokenChanged();
	void labelChanged();
	void descriptionChanged();
	void kindLabelChanged();
	void composerPlaceholderChanged();
	void composerHintChanged();
	void activityChanged();
	void canSendChanged();
	void hasPendingReplyChanged();
	void replyActorChanged();
	void replySnippetChanged();
	void canAttachImagesChanged();
	void canAttachFilesChanged();
	void canLoadOlderChanged();
	void unreadCountChanged();
	void canMarkReadChanged();
	void loadingChanged();
	void loadingStateChanged();
	void screenShareChanged();

private:
	QString m_scopeToken;
	QString m_label;
	QString m_description;
	QString m_kindLabel;
	QString m_composerPlaceholder;
	QString m_composerHint;
	bool m_activity        = false;
	bool m_canSend = false;
	bool m_hasPendingReply = false;
	QString m_replyActor;
	QString m_replySnippet;
	bool m_canAttachImages = false;
	bool m_canAttachFiles = false;
	bool m_canLoadOlder = false;
	qulonglong m_unreadCount = 0;
	bool m_canMarkRead = false;
	bool m_loading = false;
	QString m_loadingState;
	QVariantMap m_screenShare;
};

class StableListModel : public QAbstractListModel {
	Q_OBJECT
	Q_PROPERTY(int count READ rowCount NOTIFY countChanged)

public:
	enum Role {
		StableIdRole = Qt::UserRole + 1,
		TitleRole,
		SubtitleRole,
		KindRole,
		SelectedRole,
		StatusRole,
		PayloadRole,
		DepthRole,
		UnreadCountRole,
		AvatarUrlRole,
		EnabledRole,
		CheckedRole,
		TimestampRole,
		ReplyActorRole,
		ReplySnippetRole,
		ReactionsRole,
		BodySegmentsRole,
		PreviewRole,
		OwnRole,
		DeletedRole,
		CanReplyRole,
		CanReactRole,
		CanDeleteRole,
		ScopeTokenRole,
		ShortcutRole,
		CheckableRole,
		MenuRoleRole,
		ToolTipRole,
		VisibleRole,
		AttachmentsRole,
		SourceRole,
		SectionKindRole
	};

	explicit StableListModel(QObject *parent = nullptr);
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash< int, QByteArray > roleNames() const override;
	Q_INVOKABLE QVariantMap get(int row) const;
	Q_INVOKABLE int rowForStableId(const QString &stableId) const;

	void synchronizeRows(const QVariantList &rows);
	void upsertRow(const QVariantMap &row);
	void removeRow(const QString &stableId);
	void clear();

signals:
	void countChanged();
	void rowsAboutToChange(int first, int last);
	// Emitted before beginInsertRows() for the contiguous history-pagination
	// fast path. Views use this pre-structural hook to capture stable geometry
	// before QQuickListView enters its insertion transition.
	void rowsAboutToBePrepended(int count);

protected:
	int indexOf(const QString &stableId) const;
	// Model-owned asynchronous work may complete while an external fixture
	// override is active. The caller must already have validated the row identity
	// and generation; frontend/controller writes must continue through upsertRow().
	void upsertRowFromInternalResult(const QVariantMap &row);

private:
	static QVariant valueForRole(const QVariantMap &row, int role);
	static QList< int > changedRoles(const QVariantMap &before, const QVariantMap &after);
	void rebuildRowIndex();
	QVariantList m_rows;
	QStringList m_rowIds;
	QHash< QString, int > m_rowIndexById;
};

class RoomModel final : public StableListModel {
	Q_OBJECT
public:
	using StableListModel::StableListModel;
	void clearConnectionState();
	void replaceRoomStates(const QVariantList &voiceRooms, const QVariantList &textRooms);
	void replaceDirectMessageStates(const QVariantList &conversations);
	void selectScope(const QString &scopeToken);
	void selectScopeFromRail(const QString &scopeToken, const QString &railKind);
	static QVariantMap roomRow(const QVariantMap &room, const QString &kind);

private:
	void synchronizeAllRows();
	QVariantList m_voiceRoomStates;
	QVariantList m_textRoomStates;
	QVariantList m_directMessageStates;
};

class ParticipantModel final : public StableListModel {
	Q_OBJECT
public:
	using StableListModel::StableListModel;
	void updatePresence(const QString &sessionId, const QString &talkState, const QString &talkLabel,
						const QString &talkTone, bool talking, bool isSelf, const QVariantList &badges,
						const QVariantList &statuses);
	void upsertParticipantState(const QVariantMap &participant);
	void removeParticipant(const QString &sessionId);
	void replaceParticipantStates(const QVariantList &participants);
	QVariantList participantStates() const;
	static QVariantMap participantRow(const QVariantMap &participant);

private:
};

class NavigationRailModel final : public StableListModel {
	Q_OBJECT
	Q_PROPERTY(QString filterText READ filterText WRITE setFilterText NOTIFY filterTextChanged)
public:
	using StableListModel::StableListModel;
	QString filterText() const;
	Q_INVOKABLE void setFilterText(const QString &filterText);
	Q_INVOKABLE bool isRoomExpanded(const QString &scopeToken) const;
	Q_INVOKABLE void setRoomExpanded(const QString &scopeToken, bool expanded);
	Q_INVOKABLE void toggleRoomExpanded(const QString &scopeToken);
	void clearConnectionState();
	void replaceRoomStates(const QVariantList &voiceRooms, const QVariantList &textRooms);
	void replaceDirectMessageStates(const QVariantList &conversations);
	void selectScope(const QString &scopeToken);
	void selectScopeFromRail(const QString &scopeToken, const QString &railKind);
	void updatePresence(const QString &sessionId, const QString &talkState, const QString &talkLabel,
						const QString &talkTone, bool talking, bool isSelf, const QVariantList &badges,
						const QVariantList &statuses);
	void removeParticipant(const QString &sessionId);

signals:
	void filterTextChanged();
	void roomExpansionChanged(const QString &scopeToken, bool expanded);

private:
	QVariantMap navigationRoomRow(const QVariantMap &room, const QString &kind) const;
	void synchronizeAllRows();
	QVariantList m_voiceRoomStates;
	QVariantList m_textRoomStates;
	QVariantList m_directMessageStates;
	QString m_filterText;
	QSet< QString > m_collapsedRoomScopes;
};

class ChatTimelineModel final : public StableListModel {
	Q_OBJECT
	Q_PROPERTY(bool hasUserHistory READ hasUserHistory NOTIFY hasUserHistoryChanged)
	Q_PROPERTY(QString query READ query WRITE setQuery NOTIFY queryChanged)
	Q_PROPERTY(int matchCount READ matchCount NOTIFY matchCountChanged)
	Q_PROPERTY(int currentMatchIndex READ currentMatchIndex NOTIFY currentMatchChanged)
	Q_PROPERTY(int currentMatchRow READ currentMatchRow NOTIFY currentMatchChanged)
	Q_PROPERTY(QString currentMatchStableId READ currentMatchStableId NOTIFY currentMatchChanged)
public:
	enum class MessageMutation { Ignored, Inserted, Updated, Unchanged };
	explicit ChatTimelineModel(QObject *parent = nullptr);
	~ChatTimelineModel() override = default;
	MessageMutation applyMessage(const QVariantMap &message);
	bool upsertMessage(const QVariantMap &message);
	bool removeMessage(const QString &messageId);
	int appendMessages(const QVariantList &messages);
	void replaceMessages(const QVariantList &messages);
	QVariantList messages() const;
	void clear();
	bool hasUserHistory() const;
	QString query() const;
	void setQuery(const QString &query);
	int matchCount() const;
	int currentMatchIndex() const;
	int currentMatchRow() const;
	QString currentMatchStableId() const;
	Q_INVOKABLE bool nextMatch();
	Q_INVOKABLE bool previousMatch();
	Q_INVOKABLE void clearSearch();

signals:
	void hasUserHistoryChanged();
	void queryChanged();
	void matchCountChanged();
	void currentMatchChanged();

private:
	struct RichBodyParseRequest {
		QString messageId;
		QByteArray cacheKey;
		QString bodyHtml;
		QString bodyText;
	};
	struct ParsedRichBody {
		QByteArray cacheKey;
		QVariantList segments;
	};
	struct ReadyRichBody {
		QString messageId;
		QByteArray cacheKey;
		QVariantList segments;
	};

	QVariantMap messageRow(const QVariantMap &message, QList< RichBodyParseRequest > *requests = nullptr);
	static bool isUserHistoryRow(const QVariantMap &row);
	void updateUserHistoryRow(const QString &messageId, const QVariantMap &row);
	void forgetRichBodyMessage(const QString &messageId);
	void scheduleRichBodyParses(const QList< RichBodyParseRequest > &requests);
	void refillDeferredRichBodyParses();
	void launchRichBodyParseBatch();
	void scheduleRichBodyDrain();
	void drainRichBodyResults();
	bool searchActive() const;
	bool rowMatchesQuery(const QVariantMap &row, const QString &normalizedQuery) const;
	void refreshSearchState(bool preserveCurrentMatch = true);
	bool selectMatch(int matchIndex);

	QHash< QString, QByteArray > m_expectedRichBodyKeyByMessage;
	QHash< QByteArray, QSet< QString > > m_richBodyConsumers;
	QSet< QByteArray > m_inFlightRichBodyKeys;
	QSet< QByteArray > m_activeRichBodyKeys;
	QHash< QByteArray, RichBodyParseRequest > m_pendingRichBodyParses;
	QList< QByteArray > m_pendingRichBodyOrder;
	QList< QByteArray > m_deferredRichBodyOrder;
	QSet< QByteArray > m_deferredRichBodyKeys;
	QSet< QString > m_userHistoryMessageIds;
	std::deque< ReadyRichBody > m_readyRichBodies;
	QString m_query;
	QStringList m_searchMatchIds;
	QString m_currentMatchStableId;
	int m_currentMatchIndex = -1;
	int m_currentMatchRow = -1;
	bool m_richBodyWorkerActive = false;
	bool m_richBodyDrainScheduled = false;
};

class DirectMessageSummaryModel final : public StableListModel {
	Q_OBJECT

public:
	using StableListModel::StableListModel;

	void replaceConversationStates(const QVariantList &conversations);
	static QVariantMap conversationRow(const QVariantMap &conversation);
};

class DirectMessageController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(DirectMessageSummaryModel *summaryModel READ summaryModel CONSTANT)
	Q_PROPERTY(ChatTimelineModel *timelineModel READ timelineModel CONSTANT)
	Q_PROPERTY(bool available READ available NOTIFY stateChanged)
	Q_PROPERTY(QString title READ title NOTIFY stateChanged)
	Q_PROPERTY(QString description READ description NOTIFY stateChanged)
	Q_PROPERTY(qulonglong unreadTotal READ unreadTotal NOTIFY stateChanged)
	Q_PROPERTY(bool hasUnread READ hasUnread NOTIFY stateChanged)
	Q_PROPERTY(bool trayOpen READ trayOpen WRITE setTrayOpen NOTIFY trayOpenChanged)
	Q_PROPERTY(bool conversationOpen READ conversationOpen NOTIFY stateChanged)
	Q_PROPERTY(QString activeSessionId READ activeSessionId NOTIFY stateChanged)
	Q_PROPERTY(QString activeScopeToken READ activeScopeToken NOTIFY stateChanged)
	Q_PROPERTY(QString activeLabel READ activeLabel NOTIFY stateChanged)
	Q_PROPERTY(QString activeSubtitle READ activeSubtitle NOTIFY stateChanged)
	Q_PROPERTY(QString activeAvatarUrl READ activeAvatarUrl NOTIFY stateChanged)
	Q_PROPERTY(qulonglong activeUnreadCount READ activeUnreadCount NOTIFY stateChanged)
	Q_PROPERTY(bool canSend READ canSend NOTIFY stateChanged)
	Q_PROPERTY(QString mode READ mode NOTIFY stateChanged)
	Q_PROPERTY(bool persistentHistoryAvailable READ persistentHistoryAvailable NOTIFY stateChanged)
	Q_PROPERTY(bool historyLoading READ historyLoading NOTIFY stateChanged)
	Q_PROPERTY(QString historyError READ historyError NOTIFY stateChanged)
	Q_PROPERTY(QString emptyCopy READ emptyCopy NOTIFY stateChanged)
	Q_PROPERTY(bool canAttachImages READ canAttachImages NOTIFY stateChanged)
	Q_PROPERTY(bool canAttachFiles READ canAttachFiles NOTIFY stateChanged)
	Q_PROPERTY(QVariantList draftAttachments READ draftAttachments NOTIFY stateChanged)
	Q_PROPERTY(bool hasPendingReply READ hasPendingReply NOTIFY pendingReplyChanged)
	Q_PROPERTY(QString pendingReplyMessageId READ pendingReplyMessageId NOTIFY pendingReplyChanged)
	Q_PROPERTY(QString pendingReplyActor READ pendingReplyActor NOTIFY pendingReplyChanged)
	Q_PROPERTY(QString pendingReplySnippet READ pendingReplySnippet NOTIFY pendingReplyChanged)
	Q_PROPERTY(QString draft READ draft WRITE setDraft NOTIFY draftChanged)
	Q_PROPERTY(bool windowDocked READ windowDocked WRITE setWindowDocked NOTIFY windowDockedChanged)
	Q_PROPERTY(bool windowMinimized READ windowMinimized WRITE setWindowMinimized NOTIFY windowMinimizedChanged)

public:
	explicit DirectMessageController(QObject *parent = nullptr);

	DirectMessageSummaryModel *summaryModel();
	ChatTimelineModel *timelineModel();
	bool available() const;
	QString title() const;
	QString description() const;
	qulonglong unreadTotal() const;
	bool hasUnread() const;
	bool trayOpen() const;
	bool conversationOpen() const;
	QString activeSessionId() const;
	QString activeScopeToken() const;
	QString activeLabel() const;
	QString activeSubtitle() const;
	QString activeAvatarUrl() const;
	qulonglong activeUnreadCount() const;
	bool canSend() const;
	QString mode() const;
	bool persistentHistoryAvailable() const;
	bool historyLoading() const;
	QString historyError() const;
	QString emptyCopy() const;
	bool canAttachImages() const;
	bool canAttachFiles() const;
	QVariantList draftAttachments() const;
	bool hasPendingReply() const;
	QString pendingReplyMessageId() const;
	QString pendingReplyActor() const;
	QString pendingReplySnippet() const;
	QString draft() const;
	bool windowDocked() const;
	bool windowMinimized() const;

	void applyState(const QVariantMap &state);
	bool applyMessageState(const QString &sessionId, const QVariantMap &message);
	Q_INVOKABLE void setTrayOpen(bool open);
	Q_INVOKABLE void openConversation(const QString &sessionId);
	Q_INVOKABLE void closeConversation();
	Q_INVOKABLE void markRead(const QString &sessionId = QString());
	Q_INVOKABLE void setMode(const QString &mode);
	Q_INVOKABLE void setDraft(const QString &draft);
	Q_INVOKABLE void clearDraft(const QString &sessionId = QString());
	Q_INVOKABLE void sendDraft();
	Q_INVOKABLE void replyToMessage(const QString &messageId);
	Q_INVOKABLE void cancelPendingReply();
	Q_INVOKABLE void retryMessage(const QString &messageId);
	Q_INVOKABLE void deleteMessage(const QString &messageId);
	Q_INVOKABLE void toggleMessageReaction(const QString &messageId, const QString &emoji);
	Q_INVOKABLE void chooseAttachment();
	Q_INVOKABLE void removeDraftAttachment(const QString &attachmentId);
	Q_INVOKABLE void retryDraftAttachment(const QString &attachmentId);
	Q_INVOKABLE void openAttachment(const QString &assetId, const QString &fileName);
	Q_INVOKABLE void downloadAttachment(const QString &assetId, const QString &fileName);
	Q_INVOKABLE void retryAttachmentPreview(const QString &messageId, const QString &assetId);
	Q_INVOKABLE void requestContentHydration(const QString &messageId, bool highPriority = false);
	Q_INVOKABLE void setWindowDocked(bool docked);
	Q_INVOKABLE void setWindowMinimized(bool minimized);

signals:
	void stateChanged();
	void trayOpenChanged();
	void draftChanged();
	void pendingReplyChanged();
	void windowDockedChanged();
	void windowMinimizedChanged();
	void trayOpenChangeRequested(bool open);
	void openRequested(const QString &sessionId);
	void closeRequested(const QString &sessionId);
	void markReadRequested(const QString &sessionId);
	void modeChangeRequested(const QString &sessionId, const QString &mode);
	void sendRequested(const QString &sessionId, const QString &message);
	void richSendRequested(const QString &sessionId, const QString &message,
						   const QString &replyMessageId, const QVariantList &draftAttachments);
	void messageReplyRequested(const QString &sessionId, const QString &messageId);
	void messageRetryRequested(const QString &sessionId, const QString &messageId);
	void messageDeleteRequested(const QString &sessionId, const QString &messageId);
	void messageReactionToggleRequested(const QString &sessionId, const QString &messageId,
								const QString &emoji);
	void attachmentChooseRequested(const QString &sessionId);
	void draftAttachmentRemoveRequested(const QString &sessionId, const QString &attachmentId);
	void draftAttachmentRetryRequested(const QString &sessionId, const QString &attachmentId);
	void attachmentOpenRequested(const QString &sessionId, unsigned int assetId, const QString &fileName);
	void attachmentDownloadRequested(const QString &sessionId, unsigned int assetId, const QString &fileName);
	void attachmentPreviewRetryRequested(const QString &sessionId, const QString &messageId,
										 unsigned int assetId);
	void contentHydrationRequested(const QString &sessionId, const QVariantList &messageIds, bool highPriority);

private:
	static QString normalizedSessionId(const QVariant &value);
	QVariantMap timelineRow(const QString &stableId) const;
	QString protocolMessageId(const QVariantMap &row) const;
	void clearPendingReplyState();
	void switchActiveConversation(const QVariantMap &conversation);
	void pruneDrafts();

	DirectMessageSummaryModel m_summaryModel;
	ChatTimelineModel m_timelineModel;
	QVariantMap m_state;
	QVariantMap m_activeConversation;
	QHash< QString, QString > m_drafts;
	QString m_activeSessionId;
	QString m_pendingReplyMessageId;
	QString m_pendingReplyActor;
	QString m_pendingReplySnippet;
	bool m_trayOpen = false;
	bool m_windowDocked = false;
	bool m_windowMinimized = false;
};

class ToastController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool visible READ visible NOTIFY stateChanged)
	Q_PROPERTY(QString tone READ tone NOTIFY stateChanged)
	Q_PROPERTY(QString title READ title NOTIFY stateChanged)
	Q_PROPERTY(QString message READ message NOTIFY stateChanged)
	Q_PROPERTY(QString actionId READ actionId NOTIFY stateChanged)
	Q_PROPERTY(QString actionLabel READ actionLabel NOTIFY stateChanged)
	Q_PROPERTY(int repeatCount READ repeatCount NOTIFY stateChanged)
	Q_PROPERTY(qulonglong revision READ revision NOTIFY stateChanged)

public:
	explicit ToastController(QObject *parent = nullptr);

	bool visible() const;
	QString tone() const;
	QString title() const;
	QString message() const;
	QString actionId() const;
	QString actionLabel() const;
	int repeatCount() const;
	qulonglong revision() const;

	void publish(const QString &tone, const QString &title, const QString &message,
				 const QString &actionId = QString(), const QString &actionLabel = QString(),
				 int timeoutMs = 4500);
	Q_INVOKABLE void dismiss();
	Q_INVOKABLE void setInteractionActive(bool active);

signals:
	void stateChanged();

private:
	void scheduleDismiss();

	QTimer *m_dismissTimer = nullptr;
	bool m_visible = false;
	bool m_interactionActive = false;
	QString m_tone;
	QString m_title;
	QString m_message;
	QString m_actionId;
	QString m_actionLabel;
	int m_repeatCount = 0;
	int m_remainingMs = 4500;
	qint64 m_timerStartedMs = 0;
	qulonglong m_revision = 0;
};

class AsyncOperationModel final : public StableListModel {
	Q_OBJECT
public:
	using StableListModel::StableListModel;
	void startOperation(const QString &operationId, const QString &title, const QString &subtitle, bool cancellable);
	void startStructuredOperation(const QString &operationId, const QString &kind, const QString &title,
								  const QString &subtitle, int totalItems, bool cancellable);
	void updateProgress(const QString &operationId, qint64 bytesReceived, qint64 bytesTotal);
	bool updateStructuredProgress(const QString &operationId, const QString &phase, int completedItems,
								 int totalItems, qulonglong currentPluginId, qint64 bytesReceived,
								 qint64 bytesTotal);
	bool appendItemResult(const QString &operationId, const QString &itemId, qulonglong pluginId, bool success,
						  bool cancelled, const QString &errorCode, const QString &message);
	Q_INVOKABLE QVariantList itemResultPage(const QString &operationId, int offset, int limit,
										 bool unsuccessfulOnly = false) const;
	Q_INVOKABLE int itemResultCount(const QString &operationId, bool unsuccessfulOnly = false) const;
	void finishOperation(const QString &operationId, bool success, const QString &errorCode, const QString &message);
	bool finishStructuredOperation(const QString &operationId, const QString &status, int successfulItems,
								 int failedItems, int cancelledItems);
	void interruptOperations(const QString &prefix);
	Q_INVOKABLE bool hasOperation(const QString &operationId) const;
	Q_INVOKABLE void cancel(const QString &operationId);
	Q_INVOKABLE void dismiss(const QString &operationId);
	void clear();

signals:
	void cancellationRequested(const QString &operationId);

private:
	struct ItemResultStore {
		QVariantList results;
		QHash< QString, int > indexByItemId;
		int unsuccessfulCount = 0;
		qulonglong revision = 0;
	};

	QHash< QString, ItemResultStore > m_itemResultsByOperation;
};

class ActionModel final : public StableListModel {
	Q_OBJECT

public:
	explicit ActionModel(ClientActionRegistry *registry, QObject *parent = nullptr);
	Q_INVOKABLE bool trigger(const QString &actionId);

private:
	void refresh();
	ClientActionRegistry *m_registry = nullptr;
};

class DialogStateController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool open READ open NOTIFY stateChanged)
	Q_PROPERTY(QString dialogId READ dialogId NOTIFY stateChanged)
	Q_PROPERTY(QString kind READ kind NOTIFY stateChanged)
	Q_PROPERTY(QString title READ title NOTIFY stateChanged)
	Q_PROPERTY(QString subtitle READ subtitle NOTIFY stateChanged)
	Q_PROPERTY(QString activePage READ activePage NOTIFY stateChanged)
	Q_PROPERTY(QVariantList pages READ pages NOTIFY stateChanged)
	Q_PROPERTY(QVariantList sections READ sections NOTIFY stateChanged)
	Q_PROPERTY(QVariantList actions READ actions NOTIFY stateChanged)
	Q_PROPERTY(QVariantList favorites READ favorites NOTIFY stateChanged)
	Q_PROPERTY(int selectedFavoriteIndex READ selectedFavoriteIndex NOTIFY stateChanged)
	Q_PROPERTY(bool editorOpen READ editorOpen NOTIFY stateChanged)
	Q_PROPERTY(QString editorTitle READ editorTitle NOTIFY stateChanged)
	Q_PROPERTY(QString primaryActionId READ primaryActionId NOTIFY stateChanged)
	Q_PROPERTY(bool loading READ loading NOTIFY stateChanged)
	Q_PROPERTY(QString loadingScaffold READ loadingScaffold NOTIFY stateChanged)
	Q_PROPERTY(QString statusMessage READ statusMessage NOTIFY stateChanged)
	Q_PROPERTY(QString tone READ tone NOTIFY stateChanged)
	Q_PROPERTY(int preferredWidth READ preferredWidth NOTIFY stateChanged)
	Q_PROPERTY(int preferredHeight READ preferredHeight NOTIFY stateChanged)
	Q_PROPERTY(QString initialFocusId READ initialFocusId NOTIFY stateChanged)
	Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)
	Q_PROPERTY(qulonglong revision READ revision NOTIFY stateChanged)
	Q_PROPERTY(QVariantMap presentationFieldValues READ presentationFieldValues NOTIFY presentationFieldValuesChanged)

public:
	explicit DialogStateController(QObject *parent = nullptr);
	bool open() const;
	QString dialogId() const;
	QString kind() const;
	QString title() const;
	QString subtitle() const;
	QString activePage() const;
	QVariantList pages() const;
	QVariantList sections() const;
	QVariantList actions() const;
	QVariantList favorites() const;
	int selectedFavoriteIndex() const;
	bool editorOpen() const;
	QString editorTitle() const;
	QString primaryActionId() const;
	bool loading() const;
	QString loadingScaffold() const;
	QString statusMessage() const;
	QString tone() const;
	int preferredWidth() const;
	int preferredHeight() const;
	QString initialFocusId() const;
	QVariantMap state() const;
	qulonglong revision() const;
	QVariantMap presentationFieldValues() const;
	Q_INVOKABLE QVariant fieldValue(const QString &fieldId) const;
	Q_INVOKABLE QVariant presentationFieldValue(const QString &fieldId) const;
	Q_INVOKABLE QString fieldError(const QString &fieldId) const;
	void applyState(const QVariantMap &state);
	bool updatePresentationFieldValue(const QString &fieldId, const QVariant &value);

	Q_INVOKABLE void updateField(const QString &fieldId, const QVariant &value);
	Q_INVOKABLE void invokeAction(const QString &actionId, const QVariantMap &payload = {});
	Q_INVOKABLE void requestClose();

signals:
	void stateChanged();
	void presentationFieldValuesChanged();
	void fieldUpdateRequested(const QString &dialogId, const QString &fieldId, const QVariant &value);
	void actionRequested(const QString &dialogId, const QString &actionId, const QVariantMap &payload);
	void closeRequested(const QString &dialogId);

private:
	QVariantMap m_state;
	QVariantMap m_presentationFieldValues;
	QSet< QString > m_presentationFieldIds;
	qulonglong m_revision = 0;
};

class MediaSessionBackend final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool active READ active NOTIFY stateChanged)
	Q_PROPERTY(bool sharedAvailable READ sharedAvailable NOTIFY stateChanged)
	Q_PROPERTY(bool sharedJoined READ sharedJoined NOTIFY stateChanged)
	Q_PROPERTY(bool sharedHost READ sharedHost NOTIFY stateChanged)
	Q_PROPERTY(QString sharedTitle READ sharedTitle NOTIFY stateChanged)
	Q_PROPERTY(QString sharedSessionId READ sharedSessionId NOTIFY stateChanged)
	Q_PROPERTY(qulonglong sharedScopeId READ sharedScopeId NOTIFY stateChanged)
	Q_PROPERTY(qulonglong sharedHostSession READ sharedHostSession NOTIFY stateChanged)
	Q_PROPERTY(int sharedParticipantCount READ sharedParticipantCount NOTIFY stateChanged)
	Q_PROPERTY(QVariantList sharedParticipantSessions READ sharedParticipantSessions NOTIFY stateChanged)
	Q_PROPERTY(QString sharedOperationStatus READ sharedOperationStatus NOTIFY stateChanged)
	Q_PROPERTY(QString sharedOperationError READ sharedOperationError NOTIFY stateChanged)
	Q_PROPERTY(QUrl url READ url NOTIFY sourceChanged)
	Q_PROPERTY(QUrl audioUrl READ audioUrl NOTIFY sourceChanged)
	Q_PROPERTY(QUrl playbackUrl READ playbackUrl NOTIFY playbackSourceChanged)
	Q_PROPERTY(QUrl playbackAudioUrl READ playbackAudioUrl NOTIFY playbackSourceChanged)
	Q_PROPERTY(QString playbackAudioWarning READ playbackAudioWarning NOTIFY playbackSourceChanged)
	Q_PROPERTY(bool playbackSourceReady READ playbackSourceReady NOTIFY playbackSourceChanged)
	Q_PROPERTY(bool playbackSourcePreparing READ playbackSourcePreparing NOTIFY playbackSourceChanged)
	Q_PROPERTY(qulonglong playbackSourceGeneration READ playbackSourceGeneration NOTIFY playbackSourceChanged)
	Q_PROPERTY(QString provider READ provider NOTIFY sourceChanged)
	Q_PROPERTY(bool detached READ detached NOTIFY sourceChanged)
	Q_PROPERTY(bool playbackControllable READ playbackControllable NOTIFY sourceChanged)
	Q_PROPERTY(bool playbackControlAllowed READ playbackControlAllowed NOTIFY stateChanged)
	Q_PROPERTY(QString mediaMime READ mediaMime NOTIFY sourceChanged)
	Q_PROPERTY(QString audioMime READ audioMime NOTIFY sourceChanged)
	Q_PROPERTY(QString sessionId READ sessionId NOTIFY sourceChanged)
	Q_PROPERTY(QString state READ state NOTIFY stateChanged)
	Q_PROPERTY(double position READ position NOTIFY stateChanged)
	Q_PROPERTY(double duration READ duration NOTIFY stateChanged)
	Q_PROPERTY(QString error READ error NOTIFY stateChanged)
	Q_PROPERTY(QString errorCode READ errorCode NOTIFY stateChanged)
	Q_PROPERTY(qulonglong syncGeneration READ syncGeneration NOTIFY stateChanged)
	Q_PROPERTY(int loadProgress READ loadProgress NOTIFY loadProgressChanged)
	Q_PROPERTY(int volume READ volume WRITE setVolume NOTIFY volumeChanged)
	Q_PROPERTY(bool muted READ muted WRITE setMuted NOTIFY mutedChanged)

public:
	explicit MediaSessionBackend(QObject *parent = nullptr, int sharedOperationAcknowledgementTimeoutMs = 10000);
	~MediaSessionBackend() override;
	bool active() const;
	bool sharedAvailable() const;
	bool sharedJoined() const;
	bool sharedHost() const;
	QString sharedTitle() const;
	QString sharedSessionId() const;
	qulonglong sharedScopeId() const;
	qulonglong sharedHostSession() const;
	int sharedParticipantCount() const;
	QVariantList sharedParticipantSessions() const;
	QString sharedOperationStatus() const;
	QString sharedOperationError() const;
	QUrl url() const;
	QUrl audioUrl() const;
	QUrl playbackUrl() const;
	QUrl playbackAudioUrl() const;
	QString playbackAudioWarning() const;
	bool playbackSourceReady() const;
	bool playbackSourcePreparing() const;
	qulonglong playbackSourceGeneration() const;
	QString provider() const;
	bool detached() const;
	bool playbackControllable() const;
	bool playbackControlAllowed() const;
	QString mediaMime() const;
	QString audioMime() const;
	QString sessionId() const;
	QString state() const;
	double position() const;
	double duration() const;
	QString error() const;
	QString errorCode() const;
	qulonglong syncGeneration() const;
	int loadProgress() const;
	int volume() const;
	bool muted() const;
	Q_INVOKABLE bool open(const QUrl &url, const QString &provider, const QString &sessionId);
	Q_INVOKABLE bool openInline(const QUrl &url, const QString &provider, const QString &sessionId);
	Q_INVOKABLE bool openDirect(const QUrl &url, const QString &mediaMime, const QUrl &audioUrl,
								 const QString &audioMime, const QString &sessionId);
	Q_INVOKABLE bool openDirectInline(const QUrl &url, const QString &mediaMime, const QUrl &audioUrl,
									   const QString &audioMime, const QString &sessionId);
	Q_INVOKABLE bool startShared(const QUrl &url, const QString &provider, const QString &title);
	Q_INVOKABLE void joinShared();
	Q_INVOKABLE void leaveShared();
	Q_INVOKABLE void endShared();
	Q_INVOKABLE void transferSharedHost(const QString &sessionId);
	Q_INVOKABLE bool reopenSharedPlayer();
	Q_INVOKABLE void retry();
	Q_INVOKABLE bool isNavigationAllowed(const QUrl &url) const;
	Q_INVOKABLE bool supportsSynchronizedPlayback(const QString &provider) const;
	Q_INVOKABLE void detach();
	Q_INVOKABLE void attach();
	Q_INVOKABLE void close();
	Q_INVOKABLE void closePlayer();
	Q_INVOKABLE void play();
	Q_INVOKABLE void pause();
	Q_INVOKABLE void seek(double seconds);
	Q_INVOKABLE void setVolume(int volume);
	Q_INVOKABLE void setMuted(bool muted);
	Q_INVOKABLE void toggleMuted();
	Q_INVOKABLE void reportLoadProgress(int progress);
	Q_INVOKABLE void reportPlaybackState(double position, double duration, bool paused);
	Q_INVOKABLE void reportError(const QString &message);
	Q_INVOKABLE void reportTypedError(const QString &code, const QString &message);
	void setCurrentVoiceScopeId(qulonglong scopeId);
	void applyRemoteState(const QUrl &url, const QString &provider, const QString &sessionId, double position,
						  bool paused, qulonglong generation);
	void applySharedState(const QString &sessionId, const QUrl &url, const QString &provider, const QString &title,
					  qulonglong scopeId, qulonglong actorSession, qulonglong hostSession,
					  const QVariantList &participantSessions, const QString &event, double position, bool paused,
					  qulonglong generation, qulonglong selfSession);
	void clearSharedState();

signals:
	void stateChanged();
	void sourceChanged();
	void playbackSourceChanged();
	void loadProgressChanged();
	void volumeChanged();
	void mutedChanged();
	void playRequested();
	void pauseRequested();
	void seekRequested(double seconds);
	void volumeRequested(int volume);
	void mutedRequested(bool muted);
	void retryRequested();
	void playbackRejected(const QString &message);
	void sharedStartRequested(const QString &sessionId, const QUrl &url, const QString &provider,
						  const QString &title);
	void sharedEventRequested(const QString &sessionId, const QString &event, qulonglong targetHostSession);
	void sharedPlaybackStateRequested(const QString &sessionId, double position, bool paused);

private:
	enum class SharedOperationKind {
		None,
		Start,
		Join
	};

	bool validateSource(const QUrl &url, const QString &provider, QUrl *normalized, QString *error) const;
	bool validateDirectSource(const QUrl &url, const QString &mime, bool audio, QUrl *normalized,
							  QString *error) const;
	bool openWithPresentation(const QUrl &url, const QString &provider, const QString &sessionId,
							  bool detached);
	bool openDirectWithPresentation(const QUrl &url, const QString &mediaMime, const QUrl &audioUrl,
									const QString &audioMime, const QString &sessionId, bool detached);
	void prepareNativePlaybackSources();
	void invalidateNativePlaybackSources();
	void rejectPlayback(const QString &message);
	void updateLoadProgress(int progress);
	void publishSharedPlaybackState(double position, bool paused, bool force);
	void armSharedOperationAcknowledgementTimeout(SharedOperationKind kind, const QString &sessionId);
	void cancelSharedOperationAcknowledgementTimeout();
	void recoverTimedOutSharedOperation(qulonglong generation, SharedOperationKind kind,
										const QString &sessionId);
	bool sharedScopeMatchesCurrentVoiceRoom() const;
	bool m_active = false;
	bool m_sharedAvailable = false;
	bool m_sharedJoined = false;
	bool m_sharedHost = false;
	QString m_sharedTitle;
	QString m_sharedSessionId;
	QUrl m_sharedUrl;
	QString m_sharedProvider;
	qulonglong m_sharedScopeId = 0;
	qulonglong m_currentVoiceScopeId = 0;
	qulonglong m_sharedHostSession = 0;
	QVariantList m_sharedParticipantSessions;
	QString m_sharedOperationStatus = QStringLiteral("idle");
	QString m_sharedOperationError;
	QTimer *m_sharedOperationAcknowledgementTimer = nullptr;
	QMetaObject::Connection m_sharedOperationAcknowledgementConnection;
	int m_sharedOperationAcknowledgementTimeoutMs = 10000;
	qulonglong m_sharedOperationGeneration = 0;
	qulonglong m_pendingSharedOperationGeneration = 0;
	SharedOperationKind m_pendingSharedOperationKind = SharedOperationKind::None;
	bool m_sharedPlayerSuppressed = false;
	double m_sharedPosition = 0.0;
	bool m_sharedPaused = true;
	qulonglong m_sharedGeneration = 0;
	QString m_pendingExplicitSessionId;
	QUrl m_url;
	QUrl m_audioUrl;
	QUrl m_playbackUrl;
	QUrl m_playbackAudioUrl;
	QString m_playbackAudioWarning;
	bool m_playbackSourceReady = false;
	bool m_playbackSourcePreparing = false;
	qulonglong m_playbackSourceGeneration = 0;
	QStringList m_materializedPlaybackPaths;
	QString m_nativePlaybackSessionDirectory;
	std::shared_ptr< std::atomic_bool > m_nativePreparationToken;
	QString m_provider;
	bool m_detached = true;
	QString m_mediaMime;
	QString m_audioMime;
	QString m_sessionId;
	QString m_state = QStringLiteral("idle");
	double m_position = 0.0;
	double m_duration = 0.0;
	QString m_error;
	QString m_errorCode;
	qulonglong m_syncGeneration = 0;
	QString m_remoteStateSessionId;
	qulonglong m_remoteStateGeneration = 0;
	int m_loadProgress = 0;
	int m_volume = 100;
	bool m_muted = false;
	qint64 m_lastSharedPublishMs = 0;
	double m_lastSharedPublishPosition = -1.0;
	bool m_lastSharedPublishPaused = true;
};

class QmlSelectionState final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString scopeToken READ scopeToken WRITE setScopeToken NOTIFY scopeTokenChanged)
	Q_PROPERTY(int scopeValue READ scopeValue WRITE setScopeValue NOTIFY scopeValueChanged)
	Q_PROPERTY(QVariant scopeId READ scopeId WRITE setScopeId NOTIFY scopeIdChanged)
	Q_PROPERTY(QVariant selectedUserSession READ selectedUserSession WRITE setSelectedUserSession NOTIFY selectedUserSessionChanged)
	Q_PROPERTY(QVariant selectedVoiceChannelId READ selectedVoiceChannelId WRITE setSelectedVoiceChannelId NOTIFY selectedVoiceChannelIdChanged)

public:
	explicit QmlSelectionState(QObject *parent = nullptr);
	void bindModels(RoomModel *rooms, ParticipantModel *participants);
	QString scopeToken() const;
	int scopeValue() const;
	QVariant scopeId() const;
	QVariant selectedUserSession() const;
	QVariant selectedVoiceChannelId() const;
	void setScopeToken(const QString &value);
	void setScopeValue(int value);
	void setScopeId(const QVariant &value);
	void applySelection(const QString &scopeToken, int scopeValue, const QVariant &scopeId,
						const QVariant &selectedUserSession, const QVariant &selectedVoiceChannelId);
	void setSelectedUserSession(const QVariant &value);
	void setSelectedVoiceChannelId(const QVariant &value);

signals:
	void scopeTokenChanged();
	void scopeValueChanged();
	void scopeIdChanged();
	void selectedUserSessionChanged();
	void selectedVoiceChannelIdChanged();

private:
	void validate();
	bool hasScopeToken(const QString &scopeToken) const;
	bool hasVoiceChannelId(const QString &channelId) const;
	bool hasParticipantSession(const QString &sessionId) const;
	RoomModel *m_rooms = nullptr;
	ParticipantModel *m_participants = nullptr;
	QString m_scopeToken;
	int m_scopeValue = -1;
	QVariant m_scopeId;
	QVariant m_selectedUserSession;
	QVariant m_selectedVoiceChannelId;
};

class UiCommandController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool pttPressed READ pttPressed NOTIFY pttPressedChanged)

public:
	using ScopeActionsProvider = std::function< QVariantList(const QString &, const QString &) >;
	using ParticipantActionsProvider =
		std::function< QVariantList(const QString &, const QString &, const QString &) >;

	explicit UiCommandController(QObject *parent = nullptr);

	Q_INVOKABLE void selectScope(const QString &scopeToken);
	Q_INVOKABLE void selectScopeFromRail(const QString &scopeToken, const QString &railKind);
	Q_INVOKABLE void joinVoiceChannel(const QString &scopeToken);
	Q_INVOKABLE void selectParticipant(const QString &sessionId);
	Q_INVOKABLE void openDirectMessage(const QString &sessionId);
	Q_INVOKABLE void moveParticipant(const QString &sessionId, const QString &targetScopeToken);
	Q_INVOKABLE void moveScope(const QString &sourceScopeToken, const QString &targetScopeToken,
						   const QString &placement);
	Q_INVOKABLE void sendMessage(const QString &message);
	Q_INVOKABLE void requestOlderMessages();
	Q_INVOKABLE void markActiveScopeRead();
	Q_INVOKABLE void requestPreviewHydration(const QString &scopeToken, const QVariantList &messageIds,
									  bool highPriority = false);
	Q_INVOKABLE void cancelPendingReply();
	Q_INVOKABLE void chooseAttachment();
	Q_INVOKABLE void openChatAttachment(const QString &assetId, const QString &fileName);
	Q_INVOKABLE void downloadChatAttachment(const QString &assetId, const QString &fileName);
	Q_INVOKABLE void requestChatAttachmentImage(const QString &assetId, const QString &messageId);
	Q_INVOKABLE void requestChatInlineImage(const QString &token, const QString &messageId);
	Q_INVOKABLE void retryChatAttachmentPreview(const QString &scopeToken, const QString &messageId,
											 const QString &assetId);
	Q_INVOKABLE void saveChatInlineImage(const QString &token, const QString &fileName);
	Q_INVOKABLE void replyToMessage(const QString &messageId);
	Q_INVOKABLE void retryMessage(const QString &messageId);
	Q_INVOKABLE void deleteMessage(const QString &messageId);
	Q_INVOKABLE void toggleMessageReaction(const QString &messageId, const QString &emoji);
	Q_INVOKABLE void invokeAction(const QString &actionId);
	Q_INVOKABLE void invokeAppAction(const QString &actionId, const QVariantMap &payload = {});
	Q_INVOKABLE void invokeScopeAction(const QString &scopeToken, const QString &actionId);
	Q_INVOKABLE void invokeScopeActionValue(const QString &scopeToken, const QString &actionId, int value,
										bool finalValue);
	Q_INVOKABLE QVariantList requestScopeActions(const QString &scopeToken, const QString &kind) const;
	Q_INVOKABLE QVariantList requestParticipantActions(const QString &sessionId, const QString &entryKind,
																	 const QString &scopeToken) const;
	Q_INVOKABLE void invokeParticipantAction(const QString &sessionId, const QString &actionId);
	Q_INVOKABLE void invokeParticipantActionValue(const QString &sessionId, const QString &actionId, int value,
											  bool finalValue);
	Q_INVOKABLE void toggleSelfMute();
	Q_INVOKABLE void toggleSelfDeaf();
	Q_INVOKABLE void setPttPressed(bool pressed);
	Q_INVOKABLE void releasePtt();
	bool pttPressed() const;
	void setScopeActionsProvider(ScopeActionsProvider provider);
	void setParticipantActionsProvider(ParticipantActionsProvider provider);

signals:
	void scopeSelectionRequested(const QString &scopeToken);
	void scopeRailSelectionRequested(const QString &scopeToken, const QString &railKind);
	void voiceJoinRequested(const QString &scopeToken);
	void participantSelectionRequested(const QString &sessionId);
	void directMessageOpenRequested(const QString &sessionId);
	void participantMoveRequested(qulonglong sessionId, const QString &targetScopeToken);
	void scopeMoveRequested(const QString &sourceScopeToken, const QString &targetScopeToken,
						const QString &placement);
	void messageSendRequested(const QString &message);
	void olderMessagesRequested();
	void activeScopeMarkReadRequested();
	void previewHydrationRequested(const QString &scopeToken, const QVariantList &messageIds, bool highPriority);
	void pendingReplyCancelRequested();
	void attachmentChooseRequested();
	void chatAttachmentOpenRequested(unsigned int assetId, const QString &fileName);
	void chatAttachmentDownloadRequested(unsigned int assetId, const QString &fileName);
	void chatAttachmentImageRequested(unsigned int assetId, const QString &messageId);
	void chatInlineImageRequested(const QString &token, const QString &messageId);
	void chatAttachmentPreviewRetryRequested(const QString &scopeToken, const QString &messageId,
											unsigned int assetId);
	void chatInlineImageSaveRequested(const QString &token, const QString &fileName);
	void messageReplyRequested(const QString &messageId);
	void messageRetryRequested(const QString &messageId);
	void messageDeleteRequested(const QString &messageId);
	void messageReactionToggleRequested(const QString &messageId, const QString &emoji);
	void actionRequested(const QString &actionId);
	void appActionRequested(const QString &actionId, const QVariantMap &payload);
	void scopeActionRequested(const QString &scopeToken, const QString &actionId);
	void scopeActionValueRequested(const QString &scopeToken, const QString &actionId, int value, bool finalValue);
	void participantActionRequested(const QString &sessionId, const QString &actionId);
	void participantActionValueRequested(const QString &sessionId, const QString &actionId, int value,
										 bool finalValue);
	void selfMuteToggleRequested();
	void selfDeafToggleRequested();
	void pttPressedChanged();
	void pttStateRequested(bool pressed);

private:
	bool m_pttPressed = false;
	ScopeActionsProvider m_scopeActionsProvider;
	ParticipantActionsProvider m_participantActionsProvider;
};

enum class PttSafetyReason {
	WindowClosing,
	WindowDeactivated,
	ApplicationDeactivated,
	WindowHidden,
	SceneGraphError,
	HostDestroyed
};

class PttSafetyController final {
public:
	explicit PttSafetyController(UiCommandController *commands);
	void release(PttSafetyReason reason);

private:
	UiCommandController *m_commands = nullptr;
};

#endif // MUMBLE_MUMBLE_QMLCLIENTMODELS_H_
