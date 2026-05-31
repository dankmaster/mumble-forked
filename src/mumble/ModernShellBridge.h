// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNSHELLBRIDGE_H_
#define MUMBLE_MUMBLE_MODERNSHELLBRIDGE_H_

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariant>

class ModernShellBridge : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ModernShellBridge)
	Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY snapshotChanged)
	Q_PROPERTY(QVariantMap modernDialogState READ modernDialogState NOTIFY modernDialogStateChanged)

public:
	explicit ModernShellBridge(QObject *parent = nullptr);

	QVariantMap snapshot() const;
	QVariantMap modernDialogState() const;
	void setSnapshot(const QVariantMap &snapshot);
	void publishModernShellPatch(const QVariantMap &patch);
	void publishParticipantTalkState(const QVariantMap &state);
	void publishModernDialogState(const QVariantMap &state);

	Q_INVOKABLE void ready();
	Q_INVOKABLE void selectScope(const QString &scopeToken);
	Q_INVOKABLE void joinVoiceChannel(const QString &scopeToken);
	Q_INVOKABLE void invokeScopeAction(const QString &scopeToken, const QString &actionId);
	Q_INVOKABLE void scopeActionValueChanged(const QString &scopeToken, const QString &actionId, int value, bool final);
	Q_INVOKABLE void sendMessage(const QString &message);
	Q_INVOKABLE void startReply(qulonglong messageId);
	Q_INVOKABLE void cancelReply();
	Q_INVOKABLE void toggleReaction(qulonglong messageId, const QString &emoji, bool active);
	Q_INVOKABLE void deleteMessage(qulonglong messageId);
	Q_INVOKABLE void retryMessageDelivery(const QString &messageKey);
	Q_INVOKABLE void messageParticipant(qulonglong session);
	Q_INVOKABLE void openDirectMessage(qulonglong session);
	Q_INVOKABLE void closeDirectMessage(qulonglong session);
	Q_INVOKABLE void markDirectMessageRead(qulonglong session);
	Q_INVOKABLE void sendDirectMessage(qulonglong session, const QString &message);
	Q_INVOKABLE void setDirectMessageMode(qulonglong session, const QString &mode);
	Q_INVOKABLE void joinParticipant(qulonglong session);
	Q_INVOKABLE void moveParticipantToChannel(qulonglong session, const QString &scopeToken);
	Q_INVOKABLE void invokeParticipantAction(qulonglong session, const QString &actionId);
	Q_INVOKABLE void participantActionValueChanged(qulonglong session, const QString &actionId, int value, bool final);
	Q_INVOKABLE void moveChannelToChannel(const QString &sourceScopeToken, const QString &targetScopeToken,
										  const QString &placement);
	Q_INVOKABLE void loadOlderHistory();
	Q_INVOKABLE void markRead();
	Q_INVOKABLE void toggleSelfMute();
	Q_INVOKABLE void toggleSelfDeaf();
	Q_INVOKABLE void openConnectDialog();
	Q_INVOKABLE void disconnectServer();
	Q_INVOKABLE void openSettings();
	Q_INVOKABLE void openModernDialog(const QString &dialogId, const QVariantMap &context);
	Q_INVOKABLE void closeModernDialog(const QString &dialogId);
	Q_INVOKABLE void updateModernDialogField(const QString &dialogId, const QString &fieldId, const QVariant &value);
	Q_INVOKABLE void invokeModernDialogAction(const QString &dialogId, const QString &actionId,
											  const QVariantMap &payload);
	Q_INVOKABLE QVariantMap currentAudioInputMeter() const;
	Q_INVOKABLE bool clipboardHasImage() const;
	Q_INVOKABLE QString clipboardText() const;
	Q_INVOKABLE void setClipboardText(const QString &text);
	Q_INVOKABLE void attachClipboardImage();
	Q_INVOKABLE void openImagePicker();
	Q_INVOKABLE void attachImageData(const QString &dataUrl);
	Q_INVOKABLE void activateLink(const QString &href);
	Q_INVOKABLE void invokeAppAction(const QString &actionId);
	Q_INVOKABLE void invokeAppActionPayload(const QString &actionId, const QVariantMap &payload);
	Q_INVOKABLE void toggleLayout();
	Q_INVOKABLE void hydrateMessagePreviews(const QString &scopeToken, const QVariantList &messageIds,
											bool highPriority);
	Q_INVOKABLE void lookupFinanceQuote(const QString &requestId, const QString &symbol);
	Q_INVOKABLE void lookupFinanceChart(const QString &requestId, const QString &symbol, const QString &range,
										 const QString &interval);
	Q_INVOKABLE void openNativeContextMenu(const QVariantMap &request);
	Q_INVOKABLE void closeNativeContextMenu();
	void publishFinanceQuoteResult(const QVariantMap &result);
	void publishFinanceChartResult(const QVariantMap &result);
	void publishToast(const QVariantMap &toast);

signals:
	void bootReady();
	void snapshotChanged();
	void modernPatchChanged(const QVariantMap &patch);
	void participantTalkStateChanged(const QVariantMap &state);
	void modernDialogStateChanged(const QVariantMap &state);
	void scopeSelectionRequested(const QString &scopeToken);
	void voiceJoinRequested(const QString &scopeToken);
	void scopeActionRequested(const QString &scopeToken, const QString &actionId);
	void scopeActionValueChangedRequested(const QString &scopeToken, const QString &actionId, int value, bool final);
	void messageSendRequested(const QString &message);
	void replyStartRequested(qulonglong messageId);
	void replyCancelRequested();
	void reactionToggleRequested(qulonglong messageId, const QString &emoji, bool active);
	void messageDeleteRequested(qulonglong messageId);
	void messageDeliveryRetryRequested(const QString &messageKey);
	void participantMessageRequested(qulonglong session);
	void directMessageOpenRequested(qulonglong session);
	void directMessageCloseRequested(qulonglong session);
	void directMessageMarkReadRequested(qulonglong session);
	void directMessageSendRequested(qulonglong session, const QString &message);
	void directMessageModeChangeRequested(qulonglong session, const QString &mode);
	void participantJoinRequested(qulonglong session);
	void participantMoveRequested(qulonglong session, const QString &scopeToken);
	void participantActionRequested(qulonglong session, const QString &actionId);
	void participantActionValueChangedRequested(qulonglong session, const QString &actionId, int value, bool final);
	void channelMoveRequested(const QString &sourceScopeToken, const QString &targetScopeToken,
							  const QString &placement);
	void olderHistoryRequested();
	void markReadRequested();
	void selfMuteToggleRequested();
	void selfDeafToggleRequested();
	void connectDialogRequested();
	void disconnectRequested();
	void settingsRequested();
	void modernDialogOpenRequested(const QString &dialogId, const QVariantMap &context);
	void modernDialogCloseRequested(const QString &dialogId);
	void modernDialogFieldUpdateRequested(const QString &dialogId, const QString &fieldId, const QVariant &value);
	void modernDialogActionRequested(const QString &dialogId, const QString &actionId, const QVariantMap &payload);
	void clipboardImageAttachmentRequested();
	void imagePickerRequested();
	void imageDataAttachmentRequested(const QString &dataUrl);
	void linkActivationRequested(const QString &href);
	void appActionRequested(const QString &actionId);
	void appActionPayloadRequested(const QString &actionId, const QVariantMap &payload);
	void layoutToggleRequested();
	void messagePreviewHydrationRequested(const QString &scopeToken, const QVariantList &messageIds,
										  bool highPriority);
	void financeQuoteLookupRequested(const QString &requestId, const QString &symbol);
	void financeChartLookupRequested(const QString &requestId, const QString &symbol, const QString &range,
									  const QString &interval);
	void nativeContextMenuRequested(const QVariantMap &request);
	void nativeContextMenuCloseRequested();
	void financeQuoteResultReady(const QVariantMap &result);
	void financeChartResultReady(const QVariantMap &result);
	void toastRequested(const QVariantMap &toast);

private:
	QVariantMap m_snapshot;
	QVariantMap m_modernDialogState;
};

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)

#endif // MUMBLE_MUMBLE_MODERNSHELLBRIDGE_H_
