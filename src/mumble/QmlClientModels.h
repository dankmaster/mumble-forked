// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_QMLCLIENTMODELS_H_
#define MUMBLE_MUMBLE_QMLCLIENTMODELS_H_

#include <QtCore/QAbstractListModel>
#include <QtCore/QObject>
#include <QtCore/QStringList>
#include <QtCore/QVariantList>
#include <QtCore/QUrl>

class ClientActionRegistry;

class ClientSessionController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString serverName READ serverName WRITE setServerName NOTIFY serverNameChanged)
	Q_PROPERTY(QString connectionLabel READ connectionLabel WRITE setConnectionLabel NOTIFY connectionLabelChanged)
	Q_PROPERTY(QString selfName READ selfName WRITE setSelfName NOTIFY selfNameChanged)
	Q_PROPERTY(bool connected READ connected WRITE setConnected NOTIFY connectedChanged)
	Q_PROPERTY(bool selfMuted READ selfMuted WRITE setSelfMuted NOTIFY selfMutedChanged)
	Q_PROPERTY(bool selfDeafened READ selfDeafened WRITE setSelfDeafened NOTIFY selfDeafenedChanged)
	Q_PROPERTY(QVariantMap updateBanner READ updateBanner WRITE setUpdateBanner NOTIFY updateBannerChanged)
	Q_PROPERTY(QString motdHtml READ motdHtml WRITE setMotdHtml NOTIFY motdHtmlChanged)
	Q_PROPERTY(QString motdSummary READ motdSummary WRITE setMotdSummary NOTIFY motdSummaryChanged)

public:
	explicit ClientSessionController(QObject *parent = nullptr);

	QString serverName() const;
	QString connectionLabel() const;
	QString selfName() const;
	bool connected() const;
	bool selfMuted() const;
	bool selfDeafened() const;
	QVariantMap updateBanner() const;
	QString motdHtml() const;
	QString motdSummary() const;

	void setServerName(const QString &value);
	void setConnectionLabel(const QString &value);
	void setSelfName(const QString &value);
	void setConnected(bool value);
	void setSelfMuted(bool value);
	void setSelfDeafened(bool value);
	void setUpdateBanner(const QVariantMap &value);
	void setMotdHtml(const QString &value);
	void setMotdSummary(const QString &value);

signals:
	void serverNameChanged();
	void connectionLabelChanged();
	void selfNameChanged();
	void connectedChanged();
	void selfMutedChanged();
	void selfDeafenedChanged();
	void updateBannerChanged();
	void motdHtmlChanged();
	void motdSummaryChanged();

private:
	QString m_serverName = QStringLiteral("Mumble");
	QString m_connectionLabel = QStringLiteral("Disconnected");
	QString m_selfName = QStringLiteral("You");
	bool m_connected = false;
	bool m_selfMuted = false;
	bool m_selfDeafened = false;
	QVariantMap m_updateBanner;
	QString m_motdHtml;
	QString m_motdSummary;
};

class ActiveScopeController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString scopeToken READ scopeToken WRITE setScopeToken NOTIFY scopeTokenChanged)
	Q_PROPERTY(QString label READ label WRITE setLabel NOTIFY labelChanged)
	Q_PROPERTY(QString description READ description WRITE setDescription NOTIFY descriptionChanged)
	Q_PROPERTY(QString kindLabel READ kindLabel WRITE setKindLabel NOTIFY kindLabelChanged)
	Q_PROPERTY(QString composerPlaceholder READ composerPlaceholder WRITE setComposerPlaceholder NOTIFY composerPlaceholderChanged)
	Q_PROPERTY(QString composerHint READ composerHint WRITE setComposerHint NOTIFY composerHintChanged)
	Q_PROPERTY(bool canSend READ canSend WRITE setCanSend NOTIFY canSendChanged)
	Q_PROPERTY(bool hasPendingReply READ hasPendingReply WRITE setHasPendingReply NOTIFY hasPendingReplyChanged)
	Q_PROPERTY(QString replyActor READ replyActor WRITE setReplyActor NOTIFY replyActorChanged)
	Q_PROPERTY(QString replySnippet READ replySnippet WRITE setReplySnippet NOTIFY replySnippetChanged)
	Q_PROPERTY(bool canAttachImages READ canAttachImages WRITE setCanAttachImages NOTIFY canAttachImagesChanged)

public:
	explicit ActiveScopeController(QObject *parent = nullptr);
	QString scopeToken() const;
	QString label() const;
	QString description() const;
	QString kindLabel() const;
	QString composerPlaceholder() const;
	QString composerHint() const;
	bool canSend() const;
	bool hasPendingReply() const;
	QString replyActor() const;
	QString replySnippet() const;
	bool canAttachImages() const;
	void setScopeToken(const QString &value);
	void setLabel(const QString &value);
	void setDescription(const QString &value);
	void setKindLabel(const QString &value);
	void setComposerPlaceholder(const QString &value);
	void setComposerHint(const QString &value);
	void setCanSend(bool value);
	void setHasPendingReply(bool value);
	void setReplyActor(const QString &value);
	void setReplySnippet(const QString &value);
	void setCanAttachImages(bool value);
	void applyState(const QVariantMap &state);

signals:
	void scopeTokenChanged();
	void labelChanged();
	void descriptionChanged();
	void kindLabelChanged();
	void composerPlaceholderChanged();
	void composerHintChanged();
	void canSendChanged();
	void hasPendingReplyChanged();
	void replyActorChanged();
	void replySnippetChanged();
	void canAttachImagesChanged();

private:
	QString m_scopeToken;
	QString m_label;
	QString m_description;
	QString m_kindLabel;
	QString m_composerPlaceholder;
	QString m_composerHint;
	bool m_canSend = false;
	bool m_hasPendingReply = false;
	QString m_replyActor;
	QString m_replySnippet;
	bool m_canAttachImages = false;
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
		VisibleRole
	};

	explicit StableListModel(QObject *parent = nullptr);
	int rowCount(const QModelIndex &parent = QModelIndex()) const override;
	QVariant data(const QModelIndex &index, int role) const override;
	QHash< int, QByteArray > roleNames() const override;
	Q_INVOKABLE QVariantMap get(int row) const;

	void replaceRows(const QVariantList &rows);
	void synchronizeRows(const QVariantList &rows);
	void upsertRow(const QVariantMap &row);
	void removeRow(const QString &stableId);
	void clear();

signals:
	void countChanged();

private:
	int indexOf(const QString &stableId) const;
	QVariantList m_rows;
	QStringList m_rowIds;
};

class RoomModel final : public StableListModel {
	Q_OBJECT
public:
	using StableListModel::StableListModel;
	void replaceRoomStates(const QVariantList &voiceRooms, const QVariantList &textRooms);
	void replaceDirectMessageStates(const QVariantList &conversations);

private:
	static QVariantMap roomRow(const QVariantMap &room, const QString &kind);
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
	void replaceParticipantStates(const QVariantList &participants);

private:
	static QVariantMap participantRow(const QVariantMap &participant);
};

class ChatTimelineModel final : public StableListModel {
	Q_OBJECT
public:
	using StableListModel::StableListModel;
	bool upsertMessage(const QVariantMap &message);
	int appendMessages(const QVariantList &messages);
	void replaceMessages(const QVariantList &messages);

private:
	static QVariantMap messageRow(const QVariantMap &message);
};

class AsyncOperationModel final : public StableListModel {
	Q_OBJECT
public:
	using StableListModel::StableListModel;
	void startOperation(const QString &operationId, const QString &title, const QString &subtitle, bool cancellable);
	void updateProgress(const QString &operationId, qint64 bytesReceived, qint64 bytesTotal);
	void finishOperation(const QString &operationId, bool success, const QString &errorCode, const QString &message);
	void interruptOperations(const QString &prefix);
	Q_INVOKABLE void cancel(const QString &operationId);
	Q_INVOKABLE void dismiss(const QString &operationId);

signals:
	void cancellationRequested(const QString &operationId);
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
	Q_PROPERTY(QVariantMap state READ state NOTIFY stateChanged)

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
	QVariantMap state() const;
	Q_INVOKABLE QVariant fieldValue(const QString &fieldId) const;
	Q_INVOKABLE QString fieldError(const QString &fieldId) const;
	void applyState(const QVariantMap &state);

	Q_INVOKABLE void updateField(const QString &fieldId, const QVariant &value);
	Q_INVOKABLE void invokeAction(const QString &actionId, const QVariantMap &payload = {});
	Q_INVOKABLE void requestClose();

signals:
	void stateChanged();
	void fieldUpdateRequested(const QString &dialogId, const QString &fieldId, const QVariant &value);
	void actionRequested(const QString &dialogId, const QString &actionId, const QVariantMap &payload);
	void closeRequested(const QString &dialogId);

private:
	QVariantMap m_state;
};

class MediaSessionBackend final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool active READ active NOTIFY stateChanged)
	Q_PROPERTY(QUrl url READ url NOTIFY stateChanged)
	Q_PROPERTY(QString provider READ provider NOTIFY stateChanged)
	Q_PROPERTY(QString sessionId READ sessionId NOTIFY stateChanged)
	Q_PROPERTY(QString state READ state NOTIFY stateChanged)
	Q_PROPERTY(double position READ position NOTIFY stateChanged)
	Q_PROPERTY(double duration READ duration NOTIFY stateChanged)
	Q_PROPERTY(QString error READ error NOTIFY stateChanged)
	Q_PROPERTY(qulonglong syncGeneration READ syncGeneration NOTIFY stateChanged)

public:
	explicit MediaSessionBackend(QObject *parent = nullptr);
	bool active() const;
	QUrl url() const;
	QString provider() const;
	QString sessionId() const;
	QString state() const;
	double position() const;
	double duration() const;
	QString error() const;
	qulonglong syncGeneration() const;
	Q_INVOKABLE bool open(const QUrl &url, const QString &provider, const QString &sessionId);
	Q_INVOKABLE void close();
	Q_INVOKABLE void play();
	Q_INVOKABLE void pause();
	Q_INVOKABLE void seek(double seconds);
	Q_INVOKABLE void reportPlaybackState(double position, double duration, bool paused);
	Q_INVOKABLE void reportError(const QString &message);
	void applyRemoteState(const QUrl &url, const QString &provider, const QString &sessionId, double position,
						  bool paused, qulonglong generation);

signals:
	void stateChanged();
	void playRequested();
	void pauseRequested();
	void seekRequested(double seconds);

private:
	bool m_active = false;
	QUrl m_url;
	QString m_provider;
	QString m_sessionId;
	QString m_state = QStringLiteral("idle");
	double m_position = 0.0;
	double m_duration = 0.0;
	QString m_error;
	qulonglong m_syncGeneration = 0;
};

class QmlSelectionState final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString scopeToken READ scopeToken WRITE setScopeToken NOTIFY scopeTokenChanged)
	Q_PROPERTY(QVariant selectedUserSession READ selectedUserSession WRITE setSelectedUserSession NOTIFY selectedUserSessionChanged)
	Q_PROPERTY(QVariant selectedVoiceChannelId READ selectedVoiceChannelId WRITE setSelectedVoiceChannelId NOTIFY selectedVoiceChannelIdChanged)

public:
	explicit QmlSelectionState(QObject *parent = nullptr);
	QString scopeToken() const;
	QVariant selectedUserSession() const;
	QVariant selectedVoiceChannelId() const;
	void setScopeToken(const QString &value);
	void setSelectedUserSession(const QVariant &value);
	void setSelectedVoiceChannelId(const QVariant &value);

signals:
	void scopeTokenChanged();
	void selectedUserSessionChanged();
	void selectedVoiceChannelIdChanged();

private:
	QString m_scopeToken;
	QVariant m_selectedUserSession;
	QVariant m_selectedVoiceChannelId;
};

class UiCommandController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(bool pttPressed READ pttPressed NOTIFY pttPressedChanged)

public:
	explicit UiCommandController(QObject *parent = nullptr);

	Q_INVOKABLE void selectScope(const QString &scopeToken);
	Q_INVOKABLE void joinVoiceChannel(const QString &scopeToken);
	Q_INVOKABLE void selectParticipant(const QString &sessionId);
	Q_INVOKABLE void openDirectMessage(const QString &sessionId);
	Q_INVOKABLE void sendMessage(const QString &message);
	Q_INVOKABLE void cancelPendingReply();
	Q_INVOKABLE void chooseAttachment();
	Q_INVOKABLE void replyToMessage(const QString &messageId);
	Q_INVOKABLE void retryMessage(const QString &messageId);
	Q_INVOKABLE void deleteMessage(const QString &messageId);
	Q_INVOKABLE void toggleMessageReaction(const QString &messageId, const QString &emoji);
	Q_INVOKABLE void invokeAction(const QString &actionId);
	Q_INVOKABLE void invokeScopeAction(const QString &scopeToken, const QString &actionId);
	Q_INVOKABLE void invokeParticipantAction(const QString &sessionId, const QString &actionId);
	Q_INVOKABLE void toggleSelfMute();
	Q_INVOKABLE void toggleSelfDeaf();
	Q_INVOKABLE void setPttPressed(bool pressed);
	Q_INVOKABLE void releasePtt();
	bool pttPressed() const;

signals:
	void scopeSelectionRequested(const QString &scopeToken);
	void voiceJoinRequested(const QString &scopeToken);
	void participantSelectionRequested(const QString &sessionId);
	void directMessageOpenRequested(const QString &sessionId);
	void messageSendRequested(const QString &message);
	void pendingReplyCancelRequested();
	void attachmentChooseRequested();
	void messageReplyRequested(const QString &messageId);
	void messageRetryRequested(const QString &messageId);
	void messageDeleteRequested(const QString &messageId);
	void messageReactionToggleRequested(const QString &messageId, const QString &emoji);
	void actionRequested(const QString &actionId);
	void scopeActionRequested(const QString &scopeToken, const QString &actionId);
	void participantActionRequested(const QString &sessionId, const QString &actionId);
	void selfMuteToggleRequested();
	void selfDeafToggleRequested();
	void pttPressedChanged();
	void pttStateRequested(bool pressed);

private:
	bool m_pttPressed = false;
};

#endif // MUMBLE_MUMBLE_QMLCLIENTMODELS_H_
