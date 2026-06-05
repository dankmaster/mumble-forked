// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernShellBridge.h"

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include "AudioInput.h"
#include "Global.h"
#include "ServerHandler.h"

#	include <QtCore/QByteArray>
#	include <QtCore/QDateTime>
#	include <QtCore/QFile>
#	include <QtCore/QMimeData>
#	include <QtCore/QUrl>
#	include <QtGui/QClipboard>
#	include <QtGui/QImage>
#	include <QtGui/QImageReader>
#	include <QtGui/QPixmap>
#	include <QtNetwork/QNetworkAccessManager>
#	include <QtNetwork/QNetworkReply>
#	include <QtNetwork/QNetworkRequest>
#	include <QtWidgets/QApplication>

#	include <cmath>

namespace {
constexpr qint64 PREVIEW_BRIDGE_FETCH_MAX_TEXT_BYTES   = 1024 * 1024;
constexpr qint64 PREVIEW_BRIDGE_FETCH_MAX_BINARY_BYTES = 8 * 1024 * 1024;
const QByteArray PREVIEW_MEDIA_USER_AGENT =
	QByteArrayLiteral("Mozilla/5.0 (Windows NT 10.0; Win64; x64) AppleWebKit/537.36 "
					  "(KHTML, like Gecko) Chrome/135.0.0.0 Safari/537.36");
const QByteArray PREVIEW_MEDIA_ACCEPT_LANGUAGE = QByteArrayLiteral("en-US,en;q=0.9");

int audioMeterPercent(const float normalizedLevel) {
	return qBound(0, static_cast< int >(std::lround(normalizedLevel * 100.0f)), 100);
	}

	bool hostEqualsOrEndsWith(const QString &host, const QString &domain) {
		const QString normalizedHost   = host.trimmed().toLower();
		const QString normalizedDomain = domain.trimmed().toLower();
		return normalizedHost == normalizedDomain || normalizedHost.endsWith(QStringLiteral(".") + normalizedDomain);
	}

	bool isTrustedPreviewMediaFetchUrl(const QUrl &url) {
		if (!url.isValid() || url.scheme().toLower() != QLatin1String("https") || !url.userName().isEmpty()
			|| !url.password().isEmpty()) {
			return false;
		}

		const QString host = url.host().trimmed().toLower();
		const QString path = url.path().trimmed().toLower();
		return hostEqualsOrEndsWith(host, QStringLiteral("steamstatic.com"))
			   && path.startsWith(QLatin1String("/store_trailers/"));
	}

	QVariantMap previewMediaFetchFailure(const QString &requestId, const QString &error, int status = 0) {
		QVariantMap result;
		result.insert(QStringLiteral("requestId"), requestId);
		result.insert(QStringLiteral("ok"), false);
		result.insert(QStringLiteral("status"), status);
		result.insert(QStringLiteral("error"), error);
		return result;
	}

	void appendModernShellConnectTrace(const QString &message) {
		if (qEnvironmentVariableIntValue("MUMBLE_CONNECT_TRACE") == 0) {
			return;
		}

		QFile traceFile(Global::get().qdBasePath.filePath(QLatin1String("shared-modern-connect-trace.log")));
		if (!traceFile.open(QIODevice::Append | QIODevice::Text)) {
			return;
		}

		const QByteArray line = QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs).toUtf8()
								+ " UI " + message.toUtf8() + '\n';
		traceFile.write(line);
		traceFile.flush();
	}

	QList< QUrl > extractLocalImageUrls(const QMimeData *mimeData) {
		QList< QUrl > imageUrls;
		if (!mimeData) {
			return imageUrls;
		}

		const QList< QUrl > urls = mimeData->urls();
		for (const QUrl &url : urls) {
			if (!url.isLocalFile()) {
				continue;
			}

			const QString localPath = url.toLocalFile();
			if (QImageReader::imageFormat(localPath).isEmpty()) {
				continue;
			}

			imageUrls.push_back(url);
		}

		return imageUrls;
	}

	QImage extractMimeImage(const QMimeData *mimeData) {
		if (!mimeData || !mimeData->hasImage()) {
			return QImage();
		}

		const QVariant imageData = mimeData->imageData();
		QImage image             = qvariant_cast< QImage >(imageData);
		if (!image.isNull()) {
			return image;
		}

		const QPixmap pixmap = qvariant_cast< QPixmap >(imageData);
		return pixmap.isNull() ? QImage() : pixmap.toImage();
	}
} // namespace

ModernShellBridge::ModernShellBridge(QObject *parent) : QObject(parent) {
}

QVariantMap ModernShellBridge::snapshot() const {
	return m_snapshot;
}

QVariantMap ModernShellBridge::modernDialogState() const {
	return m_modernDialogState;
}

void ModernShellBridge::setSnapshot(const QVariantMap &snapshot) {
	appendModernShellConnectTrace(
		QStringLiteral("ModernShellBridge::setSnapshot enter keys=%1").arg(snapshot.keys().size()));
	if (m_snapshot == snapshot) {
		appendModernShellConnectTrace(QStringLiteral("ModernShellBridge::setSnapshot unchanged-skip"));
		return;
	}
	m_snapshot = snapshot;
	appendModernShellConnectTrace(QStringLiteral("ModernShellBridge::setSnapshot before-emit"));
	emit snapshotChanged();
	appendModernShellConnectTrace(QStringLiteral("ModernShellBridge::setSnapshot after-emit"));
}

void ModernShellBridge::publishModernShellPatch(const QVariantMap &patch) {
	if (patch.isEmpty()) {
		return;
	}

	emit modernPatchChanged(patch);
}

void ModernShellBridge::publishParticipantTalkState(const QVariantMap &state) {
	if (state.isEmpty()) {
		return;
	}

	emit participantTalkStateChanged(state);
}

void ModernShellBridge::publishModernDialogState(const QVariantMap &state) {
	m_modernDialogState = state;
	emit modernDialogStateChanged(state);
}

QVariantMap ModernShellBridge::currentAudioInputMeter() const {
	QVariantMap meter;
	meter.insert(QStringLiteral("available"), false);
	const bool connected = Global::get().uiSession != 0 && Global::get().sh && Global::get().sh->isConnected();
	meter.insert(QStringLiteral("connected"), connected);
	meter.insert(QStringLiteral("loopbackMode"), static_cast< int >(Global::get().s.lmLoopMode));

	const auto audioInput = Global::get().ai;
	if (!audioInput) {
		return meter;
	}

	const bool transmitting = audioInput->isTransmitting();
	const bool hasProcessedInput =
		audioInput->dPeakCleanMic < 0.0f || audioInput->dPeakSignal < 0.0f || audioInput->fSpeechProb > 0.0f
		|| transmitting;
	if (!hasProcessedInput) {
		return meter;
	}

	const float amplitudeLevel    = audioInput->amplitudeVoiceActivityLevel();
	const float signalToNoiseProb = audioInput->fSpeechProb;
	meter.insert(QStringLiteral("available"), true);
	meter.insert(QStringLiteral("amplitude"), audioMeterPercent(amplitudeLevel));
	meter.insert(QStringLiteral("signalToNoise"), audioMeterPercent(signalToNoiseProb));
	meter.insert(QStringLiteral("hybrid"), audioMeterPercent(AudioInput::voiceActivityLevelFor(
												 Settings::Hybrid, amplitudeLevel, signalToNoiseProb)));
	meter.insert(QStringLiteral("transmitting"), transmitting);
	meter.insert(QStringLiteral("peakCleanMicDb"), static_cast< double >(audioInput->dPeakCleanMic));
	return meter;
}

void ModernShellBridge::ready() {
	appendModernShellConnectTrace(QStringLiteral("ModernShellBridge::ready"));
	emit bootReady();
}

void ModernShellBridge::selectScope(const QString &scopeToken) {
	emit scopeSelectionRequested(scopeToken.trimmed());
}

void ModernShellBridge::selectScopeFromRail(const QString &scopeToken, const QString &railKind) {
	emit scopeRailSelectionRequested(scopeToken.trimmed(), railKind.trimmed());
}

void ModernShellBridge::joinVoiceChannel(const QString &scopeToken) {
	emit voiceJoinRequested(scopeToken.trimmed());
}

void ModernShellBridge::invokeScopeAction(const QString &scopeToken, const QString &actionId) {
	emit scopeActionRequested(scopeToken.trimmed(), actionId.trimmed());
}

void ModernShellBridge::scopeActionValueChanged(const QString &scopeToken, const QString &actionId, const int value,
												const bool final) {
	emit scopeActionValueChangedRequested(scopeToken.trimmed(), actionId.trimmed(), value, final);
}

void ModernShellBridge::sendMessage(const QString &message) {
	emit messageSendRequested(message);
}

void ModernShellBridge::startReply(const qulonglong messageId) {
	emit replyStartRequested(messageId);
}

void ModernShellBridge::cancelReply() {
	emit replyCancelRequested();
}

void ModernShellBridge::toggleReaction(const qulonglong messageId, const QString &emoji, const bool active) {
	const QString trimmedEmoji = emoji.trimmed();
	if (messageId == 0 || trimmedEmoji.isEmpty()) {
		return;
	}

	emit reactionToggleRequested(messageId, trimmedEmoji, active);
}

void ModernShellBridge::deleteMessage(const qulonglong messageId) {
	if (messageId == 0) {
		return;
	}

	emit messageDeleteRequested(messageId);
}

void ModernShellBridge::retryMessageDelivery(const QString &messageKey) {
	const QString trimmedKey = messageKey.trimmed();
	if (trimmedKey.isEmpty()) {
		return;
	}

	emit messageDeliveryRetryRequested(trimmedKey);
}

void ModernShellBridge::messageParticipant(const qulonglong session) {
	emit participantMessageRequested(session);
}

void ModernShellBridge::openDirectMessage(const qulonglong session) {
	if (session == 0) {
		return;
	}

	emit directMessageOpenRequested(session);
}

void ModernShellBridge::closeDirectMessage(const qulonglong session) {
	if (session == 0) {
		return;
	}

	emit directMessageCloseRequested(session);
}

void ModernShellBridge::markDirectMessageRead(const qulonglong session) {
	if (session == 0) {
		return;
	}

	emit directMessageMarkReadRequested(session);
}

void ModernShellBridge::sendDirectMessage(const qulonglong session, const QString &message) {
	if (session == 0 || message.trimmed().isEmpty()) {
		return;
	}

	emit directMessageSendRequested(session, message);
}

void ModernShellBridge::setDirectMessageMode(const qulonglong session, const QString &mode) {
	if (session == 0) {
		return;
	}

	emit directMessageModeChangeRequested(session, mode.trimmed());
}

void ModernShellBridge::joinParticipant(const qulonglong session) {
	emit participantJoinRequested(session);
}

void ModernShellBridge::moveParticipantToChannel(const qulonglong session, const QString &scopeToken) {
	emit participantMoveRequested(session, scopeToken.trimmed());
}

void ModernShellBridge::invokeParticipantAction(const qulonglong session, const QString &actionId) {
	emit participantActionRequested(session, actionId.trimmed());
}

void ModernShellBridge::participantActionValueChanged(const qulonglong session, const QString &actionId,
													  const int value, const bool final) {
	emit participantActionValueChangedRequested(session, actionId.trimmed(), value, final);
}

void ModernShellBridge::moveChannelToChannel(const QString &sourceScopeToken, const QString &targetScopeToken,
											 const QString &placement) {
	emit channelMoveRequested(sourceScopeToken.trimmed(), targetScopeToken.trimmed(), placement.trimmed());
}

void ModernShellBridge::loadOlderHistory() {
	emit olderHistoryRequested();
}

void ModernShellBridge::markRead() {
	emit markReadRequested();
}

void ModernShellBridge::toggleSelfMute() {
	emit selfMuteToggleRequested();
}

void ModernShellBridge::toggleSelfDeaf() {
	emit selfDeafToggleRequested();
}

void ModernShellBridge::openConnectDialog() {
	emit modernDialogOpenRequested(QStringLiteral("connect"), QVariantMap());
}

void ModernShellBridge::disconnectServer() {
	emit disconnectRequested();
}

void ModernShellBridge::openSettings() {
	emit modernDialogOpenRequested(QStringLiteral("settings"), QVariantMap());
}

void ModernShellBridge::openModernDialog(const QString &dialogId, const QVariantMap &context) {
	emit modernDialogOpenRequested(dialogId.trimmed(), context);
}

void ModernShellBridge::closeModernDialog(const QString &dialogId) {
	emit modernDialogCloseRequested(dialogId.trimmed());
}

void ModernShellBridge::updateModernDialogField(const QString &dialogId, const QString &fieldId, const QVariant &value) {
	emit modernDialogFieldUpdateRequested(dialogId.trimmed(), fieldId.trimmed(), value);
}

void ModernShellBridge::invokeModernDialogAction(const QString &dialogId, const QString &actionId,
												 const QVariantMap &payload) {
	emit modernDialogActionRequested(dialogId.trimmed(), actionId.trimmed(), payload);
}

bool ModernShellBridge::clipboardHasImage() const {
	const QClipboard *clipboard = QApplication::clipboard();
	const QMimeData *mimeData   = clipboard ? clipboard->mimeData() : nullptr;
	return !extractMimeImage(mimeData).isNull() || !extractLocalImageUrls(mimeData).isEmpty();
}

QString ModernShellBridge::clipboardText() const {
	const QClipboard *clipboard = QApplication::clipboard();
	return clipboard ? clipboard->text() : QString();
}

void ModernShellBridge::setClipboardText(const QString &text) {
	QClipboard *clipboard = QApplication::clipboard();
	if (clipboard) {
		clipboard->setText(text);
	}
}

void ModernShellBridge::attachClipboardImage() {
	emit clipboardImageAttachmentRequested();
}

void ModernShellBridge::openImagePicker() {
	emit imagePickerRequested();
}

void ModernShellBridge::attachImageData(const QString &dataUrl) {
	emit imageDataAttachmentRequested(dataUrl.trimmed());
}

void ModernShellBridge::activateLink(const QString &href) {
	const QString trimmedHref = href.trimmed();
	if (trimmedHref.isEmpty()) {
		return;
	}

	emit linkActivationRequested(trimmedHref);
}

void ModernShellBridge::openProviderSession(const QString &href) {
	const QString trimmedHref = href.trimmed();
	if (trimmedHref.isEmpty()) {
		return;
	}

	emit providerSessionRequested(trimmedHref);
}

void ModernShellBridge::fetchPreviewMedia(const QString &requestId, const QString &url, const QString &responseType) {
	const QString trimmedRequestID = requestId.trimmed();
	const QUrl requestUrl(url.trimmed());
	if (trimmedRequestID.isEmpty()) {
		return;
	}
	if (!isTrustedPreviewMediaFetchUrl(requestUrl)) {
		emit previewMediaFetchResultReady(
			previewMediaFetchFailure(trimmedRequestID, QStringLiteral("URL not allowed")));
		return;
	}
	if (!Global::get().nam) {
		emit previewMediaFetchResultReady(
			previewMediaFetchFailure(trimmedRequestID, QStringLiteral("Network manager unavailable")));
		return;
	}

	const QString normalizedResponseType = responseType.trimmed().toLower();
	const bool wantsText                 = normalizedResponseType == QLatin1String("text");
	const qint64 maxBytes = wantsText ? PREVIEW_BRIDGE_FETCH_MAX_TEXT_BYTES : PREVIEW_BRIDGE_FETCH_MAX_BINARY_BYTES;

	QNetworkRequest request(requestUrl);
	request.setRawHeader(QByteArrayLiteral("User-Agent"), PREVIEW_MEDIA_USER_AGENT);
	request.setRawHeader(QByteArrayLiteral("Accept-Language"), PREVIEW_MEDIA_ACCEPT_LANGUAGE);
	request.setRawHeader(QByteArrayLiteral("Accept"),
						 wantsText ? QByteArrayLiteral("application/vnd.apple.mpegurl,text/plain;q=0.9,*/*;q=0.5")
								   : QByteArrayLiteral("video/mp4,audio/mp4,application/octet-stream,*/*;q=0.5"));
	request.setRawHeader(QByteArrayLiteral("Referer"), QByteArrayLiteral("https://store.steampowered.com/"));
	request.setRawHeader(QByteArrayLiteral("Origin"), QByteArrayLiteral("https://store.steampowered.com"));

	QNetworkReply *reply = Global::get().nam->get(request);
	reply->setProperty("mumblePreviewMediaRequestId", trimmedRequestID);
	reply->setProperty("mumblePreviewMediaWantsText", wantsText);
	reply->setProperty("mumblePreviewMediaMaxBytes", maxBytes);
	connect(reply, &QNetworkReply::downloadProgress, reply, [reply, maxBytes](qint64 received, qint64) {
		if (received > maxBytes && !reply->property("mumblePreviewMediaTooLarge").toBool()) {
			reply->setProperty("mumblePreviewMediaTooLarge", true);
			reply->abort();
		}
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply]() {
		const QString finishedRequestID = reply->property("mumblePreviewMediaRequestId").toString();
		const bool wantsText            = reply->property("mumblePreviewMediaWantsText").toBool();
		const qint64 maxBytes           = reply->property("mumblePreviewMediaMaxBytes").toLongLong();
		const bool tooLarge             = reply->property("mumblePreviewMediaTooLarge").toBool();
		const int status                = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).isValid()
							   ? reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt()
							   : 0;
		const QUrl finalUrl     = reply->url();
		const QByteArray data   = tooLarge ? QByteArray() : reply->readAll();
		const QString errorText = reply->errorString();
		const QString contentType =
			reply->header(QNetworkRequest::ContentTypeHeader).toString().section(QLatin1Char(';'), 0, 0);
		const bool success = !tooLarge && reply->error() == QNetworkReply::NoError
							 && isTrustedPreviewMediaFetchUrl(finalUrl) && data.size() <= maxBytes;
		reply->deleteLater();

		if (!success) {
			emit previewMediaFetchResultReady(
				previewMediaFetchFailure(finishedRequestID,
										 tooLarge ? QStringLiteral("Response too large")
												  : (errorText.isEmpty() ? QStringLiteral("Fetch failed") : errorText),
										 status));
			return;
		}

		QVariantMap result;
		result.insert(QStringLiteral("requestId"), finishedRequestID);
		result.insert(QStringLiteral("ok"), true);
		result.insert(QStringLiteral("status"), status);
		result.insert(QStringLiteral("url"), finalUrl.toString(QUrl::FullyEncoded));
		result.insert(QStringLiteral("byteLength"), data.size());
		result.insert(QStringLiteral("contentType"), contentType);
		if (wantsText) {
			result.insert(QStringLiteral("text"), QString::fromUtf8(data));
		} else {
			result.insert(QStringLiteral("dataBase64"), QString::fromLatin1(data.toBase64()));
		}
		emit previewMediaFetchResultReady(result);
	});
}

void ModernShellBridge::invokeAppAction(const QString &actionId) {
	emit appActionRequested(actionId.trimmed());
}

void ModernShellBridge::invokeAppActionPayload(const QString &actionId, const QVariantMap &payload) {
	emit appActionPayloadRequested(actionId.trimmed(), payload);
}

void ModernShellBridge::toggleLayout() {
	emit layoutToggleRequested();
}

void ModernShellBridge::hydrateMessagePreviews(const QString &scopeToken, const QVariantList &messageIds,
											   const bool highPriority) {
	if (scopeToken.trimmed().isEmpty() || messageIds.isEmpty()) {
		return;
	}

	emit messagePreviewHydrationRequested(scopeToken.trimmed(), messageIds, highPriority);
}

void ModernShellBridge::lookupFinanceQuote(const QString &requestId, const QString &symbol) {
	const QString trimmedRequestID = requestId.trimmed();
	const QString trimmedSymbol    = symbol.trimmed();
	if (trimmedRequestID.isEmpty() || trimmedSymbol.isEmpty()) {
		return;
	}

	emit financeQuoteLookupRequested(trimmedRequestID, trimmedSymbol);
}

void ModernShellBridge::lookupFinanceChart(const QString &requestId, const QString &symbol, const QString &range,
										   const QString &interval) {
	const QString trimmedRequestID = requestId.trimmed();
	const QString trimmedSymbol    = symbol.trimmed();
	if (trimmedRequestID.isEmpty() || trimmedSymbol.isEmpty()) {
		return;
	}

	emit financeChartLookupRequested(trimmedRequestID, trimmedSymbol, range.trimmed(), interval.trimmed());
}

void ModernShellBridge::openNativeContextMenu(const QVariantMap &request) {
	if (request.isEmpty()) {
		return;
	}

	emit nativeContextMenuRequested(request);
}

void ModernShellBridge::closeNativeContextMenu() {
	emit nativeContextMenuCloseRequested();
}

void ModernShellBridge::publishFinanceQuoteResult(const QVariantMap &result) {
	if (result.isEmpty()) {
		return;
	}

	emit financeQuoteResultReady(result);
}

void ModernShellBridge::publishFinanceChartResult(const QVariantMap &result) {
	if (result.isEmpty()) {
		return;
	}

	emit financeChartResultReady(result);
}

void ModernShellBridge::publishToast(const QVariantMap &toast) {
	if (toast.isEmpty()) {
		return;
	}

	emit toastRequested(toast);
}

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)
