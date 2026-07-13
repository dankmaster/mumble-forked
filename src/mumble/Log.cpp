// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Log.h"

#include "Accessibility.h"
#include "AudioOutput.h"
#include "AudioOutputSample.h"
#include "AudioOutputToken.h"
#include "Channel.h"
#include "MainWindow.h"
#include "NetworkConfig.h"
#include "RichTextEditor.h"
#include "Screen.h"
#include "ServerHandler.h"
#ifndef USE_NO_TTS
#	include "TextToSpeech.h"
#endif
#include "Utils.h"
#include "VolumeAdjustment.h"
#include "Global.h"

#include <limits>
#include <type_traits>

#include <QSignalBlocker>
#include <QtCore/QCoreApplication>
#include <QtCore/QMutexLocker>
#include <QtCore/QRegularExpression>
#include <QtGui/QImageWriter>
#include <QtGui/QScreen>
#include <QtGui/QGuiApplication>
#include <QtGui/QTextBlock>
#include <QtGui/QTextDocumentFragment>
#include <QtNetwork/QNetworkReply>


QMutex Log::qmDeferredLogs;
QVector< LogMessage > Log::qvDeferredLogs;


Log::Log(QObject *p) : QObject(p) {
	qRegisterMetaType< Log::MsgType >();

#ifndef USE_NO_TTS
	tts = new TextToSpeech(this);
	tts->setVolume(Global::get().s.iTTSVolume);
#endif
	uiLastId = 0;
	qdDate   = QDate::currentDate();

	QObject::connect(this, &Log::highlightSpawned, Global::get().mw, &MainWindow::highlightWindow);
	QObject::connect(this, &Log::serverLogEntryAppended, Global::get().mw, &MainWindow::appendModernServerLogEntry);
}

// Display order in settingsscreen, allows to insert new events without breaking config-compatibility with older
// versions
const Log::MsgType Log::msgOrder[] = { DebugInfo,
									   CriticalError,
									   Warning,
									   Information,
									   ServerConnected,
									   ServerDisconnected,
									   UserJoin,
									   UserLeave,
									   ChannelListeningAdd,
									   ChannelListeningRemove,
									   Recording,
									   YouKicked,
									   UserKicked,
									   UserRenamed,
									   SelfMute,
									   SelfUnmute,
									   SelfDeaf,
									   SelfUndeaf,
									   OtherSelfMute,
									   YouMuted,
									   YouMutedOther,
									   OtherMutedOther,
									   SelfChannelJoin,
									   SelfChannelJoinOther,
									   ChannelJoin,
									   ChannelLeave,
									   ChannelJoinConnect,
									   ChannelLeaveDisconnect,
									   PermissionDenied,
									   TextMessage,
									   PrivateTextMessage,
									   PluginMessage };

const char *Log::msgNames[] = { QT_TRANSLATE_NOOP("Log", "Debug"),
								QT_TRANSLATE_NOOP("Log", "Critical"),
								QT_TRANSLATE_NOOP("Log", "Warning"),
								QT_TRANSLATE_NOOP("Log", "Information"),
								QT_TRANSLATE_NOOP("Log", "Server connected"),
								QT_TRANSLATE_NOOP("Log", "Server disconnected"),
								QT_TRANSLATE_NOOP("Log", "User joined server"),
								QT_TRANSLATE_NOOP("Log", "User left server"),
								QT_TRANSLATE_NOOP("Log", "User recording state changed"),
								QT_TRANSLATE_NOOP("Log", "User kicked (you or by you)"),
								QT_TRANSLATE_NOOP("Log", "User kicked"),
								QT_TRANSLATE_NOOP("Log", "You self-muted"),
								QT_TRANSLATE_NOOP("Log", "Other self-muted/deafened"),
								QT_TRANSLATE_NOOP("Log", "User muted (you)"),
								QT_TRANSLATE_NOOP("Log", "User muted (by you)"),
								QT_TRANSLATE_NOOP("Log", "User muted (other)"),
								QT_TRANSLATE_NOOP("Log", "User joined channel"),
								QT_TRANSLATE_NOOP("Log", "User left channel"),
								QT_TRANSLATE_NOOP("Log", "Permission denied"),
								QT_TRANSLATE_NOOP("Log", "Text message"),
								QT_TRANSLATE_NOOP("Log", "You self-unmuted"),
								QT_TRANSLATE_NOOP("Log", "You self-deafened"),
								QT_TRANSLATE_NOOP("Log", "You self-undeafened"),
								QT_TRANSLATE_NOOP("Log", "User renamed"),
								QT_TRANSLATE_NOOP("Log", "You joined channel"),
								QT_TRANSLATE_NOOP("Log", "You joined channel (moved)"),
								QT_TRANSLATE_NOOP("Log", "User connected and entered channel"),
								QT_TRANSLATE_NOOP("Log", "User left channel and disconnected"),
								QT_TRANSLATE_NOOP("Log", "Private text message"),
								QT_TRANSLATE_NOOP("Log", "User started listening to channel"),
								QT_TRANSLATE_NOOP("Log", "User stopped listening to channel"),
								QT_TRANSLATE_NOOP("Log", "Plugin message") };

QString Log::msgName(MsgType t) const {
	return translatedMessageName(t);
}

QString Log::translatedMessageName(const MsgType type) {
	if (type < DebugInfo || type > PluginMessage) return QString();
	return QCoreApplication::translate("Log", msgNames[type]);
}

const char *Log::colorClasses[] = { "time", "server", "privilege", "source", "target" };

const QStringList Log::allowedSchemes() {
	QStringList qslAllowedSchemeNames;
	qslAllowedSchemeNames << QLatin1String("mumble");
	qslAllowedSchemeNames << QLatin1String("http");
	qslAllowedSchemeNames << QLatin1String("https");
	qslAllowedSchemeNames << QLatin1String("gemini");
	qslAllowedSchemeNames << QLatin1String("ftp");
	qslAllowedSchemeNames << QLatin1String("clientid");
	qslAllowedSchemeNames << QLatin1String("channelid");
	qslAllowedSchemeNames << QLatin1String("spotify");
	qslAllowedSchemeNames << QLatin1String("steam");
	qslAllowedSchemeNames << QLatin1String("irc");
	qslAllowedSchemeNames << QLatin1String("gg"); // Gadu-Gadu http://gg.pl - Polish instant messenger
	qslAllowedSchemeNames << QLatin1String("mailto");
	qslAllowedSchemeNames << QLatin1String("xmpp");
	qslAllowedSchemeNames << QLatin1String("skype");
	qslAllowedSchemeNames << QLatin1String("rtmp");   // http://en.wikipedia.org/wiki/Real_Time_Messaging_Protocol
	qslAllowedSchemeNames << QLatin1String("magnet"); // https://en.wikipedia.org/wiki/Magnet_URI_scheme

	return qslAllowedSchemeNames;
}

QString Log::msgColor(const QString &text, LogColorType t) {
	const auto colorClassIndex = static_cast<qsizetype>(t);
	const auto colorClassCount = static_cast<qsizetype>(sizeof(colorClasses) / sizeof(colorClasses[0]));

	if (colorClassIndex < 0 || colorClassIndex >= colorClassCount) {
		const QString plainText = QTextDocumentFragment::fromHtml(text).toPlainText();
		qWarning().noquote()
			<< QString::fromLatin1("Log::msgColor received invalid LogColorType=%1 for text='%2'")
				   .arg(static_cast<int>(t))
				   .arg(plainText.left(200));
		return text;
	}

	return QString::fromLatin1("<span class='log-%1'>%2</span>")
		.arg(QString::fromLatin1(colorClasses[colorClassIndex]))
		.arg(text);
}

QString Log::formatChannel(::Channel *c) {
	return QString::fromLatin1("<a href='channelid://id.%1/%3' class='log-channel'>%2</a>")
		.arg(c->iId)
		.arg(c->qsName.toHtmlEscaped())
		.arg(QString::fromLatin1(Global::get().sh->qbaDigest.toBase64()));
}

void Log::logOrDefer(Log::MsgType mt, const QString &console, const QString &terse, bool ownMessage,
					 const QString &overrideTTS, bool ignoreTTS) {
	if (Global::get().l) {
		// log directly as it seems the log-UI has been set-up already
		Global::get().l->log(mt, console, terse, ownMessage, overrideTTS, ignoreTTS);
	} else {
		// defer the log
		QMutexLocker mLock(&Log::qmDeferredLogs);

		qvDeferredLogs.append(LogMessage(mt, console, terse, ownMessage, overrideTTS, ignoreTTS));
	}
}

QString Log::formatClientUser(ClientUser *cu, LogColorType t, const QString &displayName) {
	QString className;
	if (t == Log::Target) {
		className = QString::fromLatin1("target");
	} else if (t == Log::Source) {
		className = QString::fromLatin1("source");
	}

	if (cu) {
		QString name = (displayName.isNull() ? cu->qsName : displayName).toHtmlEscaped();
		if (cu->qsHash.isEmpty()) {
			return QString::fromLatin1("<a href='clientid://id.%2/%4' class='log-user log-%1'>%3</a>")
				.arg(className)
				.arg(cu->uiSession)
				.arg(name)
				.arg(QString::fromLatin1(Global::get().sh->qbaDigest.toBase64()));
		} else {
			return QString::fromLatin1("<a href='clientid://%2' class='log-user log-%1'>%3</a>")
				.arg(className)
				.arg(cu->qsHash)
				.arg(name);
		}
	} else {
		return QString::fromLatin1("<span class='log-server log-%1'>%2</span>").arg(className).arg(tr("the server"));
	}
}

void Log::setIgnore(MsgType t, int ignore) {
	qmIgnore.insert(t, ignore);
}

void Log::clearIgnore() {
	qmIgnore.clear();
}

void Log::applySettings() {
#ifndef USE_NO_TTS
	if (tts) {
		tts->setVolume(Global::get().s.iTTSVolume);
	}
#endif
}

QString Log::imageToImg(const QByteArray &format, const QByteArray &image) {
	QString fmt = QLatin1String(format);

	if (fmt.isEmpty())
		fmt = QLatin1String("qt");

	QByteArray rawbase = image.toBase64();
	QByteArray encoded;
	int i     = 0;
	int begin = 0, end = 0;
	do {
		begin = i * 72;
		end   = begin + 72;

		encoded.append(QUrl::toPercentEncoding(QLatin1String(rawbase.mid(begin, 72))));
		if (end < rawbase.length())
			encoded.append('\n');

		++i;
	} while (end < rawbase.length());

	return QString::fromLatin1(
			   "<img src=\"data:image/%1;base64,%2\" style=\"border:none; outline:none; display:block; margin:0;\" />")
		.arg(fmt)
		.arg(QLatin1String(encoded));
}

QString Log::imageToImg(QImage img, int maxSize) {
	constexpr int MAX_WIDTH  = 1600;
	constexpr int MAX_HEIGHT = 1000;
	constexpr int INLINE_PREVIEW_WIDTH  = 640;
	constexpr int INLINE_PREVIEW_HEIGHT = 420;

	if ((img.width() > MAX_WIDTH) || (img.height() > MAX_HEIGHT)) {
		img = img.scaled(MAX_WIDTH, MAX_HEIGHT, Qt::KeepAspectRatio, Qt::SmoothTransformation);
	}

	int quality       = 100;
	QByteArray format = "JPEG";

	QByteArray qba;
	QString result;
	while (quality > 0) {
		qba.clear();
		QBuffer qb(&qba);
		qb.open(QIODevice::WriteOnly);

		QImageWriter imgwrite(&qb, format);
		imgwrite.setQuality(quality);
		imgwrite.write(img);
		result = imageToImg(format, qba);
		if (result.length() < maxSize || maxSize == 0) {
			const QSize previewSize =
				img.size().scaled(INLINE_PREVIEW_WIDTH, INLINE_PREVIEW_HEIGHT, Qt::KeepAspectRatio);
			result.replace(QString::fromLatin1("<img "),
						   QString::fromLatin1("<img width=\"%1\" height=\"%2\" alt=\"Image attachment\" "
											   "title=\"Double-click to preview\" ")
							   .arg(previewSize.width())
							   .arg(previewSize.height()));
			return result;
		}
		quality -= 10;
	}
	return QString();
}

QString Log::validHtml(const QString &html, QTextCursor *tc) {
	LogDocument qtd;

	const QScreen *primaryScreen = QGuiApplication::primaryScreen();
	const QRectF qr = primaryScreen ? QRectF(primaryScreen->availableGeometry()) : QRectF(0, 0, 1920, 1080);
	qtd.setTextWidth(qr.width() / 2);

	// Call documentLayout on our LogDocument to ensure
	// it has a layout backing it. With a layout set on
	// the document, it will attempt to load all the
	// resources it contains as soon as we call setHtml(),
	// allowing our validation checks for things such as
	// data URL images to run.
	(void) qtd.documentLayout();
	qtd.setHtml(html);

	QStringList qslAllowed = allowedSchemes();
	for (QTextBlock qtb = qtd.begin(); qtb != qtd.end(); qtb = qtb.next()) {
		for (QTextBlock::iterator qtbi = qtb.begin(); qtbi != qtb.end(); ++qtbi) {
			const QTextFragment &qtf = qtbi.fragment();
			QTextCharFormat qcf      = qtf.charFormat();
			if (!qcf.anchorHref().isEmpty()) {
				QUrl url(qcf.anchorHref());
				if (!url.isValid() || !qslAllowed.contains(url.scheme())) {
					QTextCharFormat qcfn = QTextCharFormat();
					QTextCursor qtc(&qtd);
					qtc.setPosition(qtf.position(), QTextCursor::MoveAnchor);
					qtc.setPosition(qtf.position() + qtf.length(), QTextCursor::KeepAnchor);
					qtc.setCharFormat(qcfn);
					qtbi = qtb.begin();
				}
			}
		}
	}

	qtd.adjustSize();
	QSizeF s = qtd.size();

	if (!s.isValid() || s.width() == 0 || s.height() == 0) {
		QString errorInvalidSizeMessage = tr("[[ Invalid size ]]");
		if (tc) {
			tc->insertText(errorInvalidSizeMessage);
			return QString();
		} else {
			return errorInvalidSizeMessage;
		}
	}

	static constexpr unsigned int allowedSize = 2048 * 2048;
	// This checks against negative sizes as well as sizes whose product overflows the integral type we use to represent
	// the maximum allowed size
	const bool saneSize =
		s.width() > 0 && s.height() > 0 && s.width() < std::numeric_limits< decltype(allowedSize) >::max() / s.height();
	const auto messageSize = static_cast< std::decay_t< decltype(allowedSize) > >(s.width() * s.height());

	if (!saneSize || messageSize > allowedSize) {
		QString errorSizeMessage = tr("[[ Text object too large to display ]]");
		if (tc) {
			tc->insertText(errorSizeMessage);
			return QString();
		} else {
			return errorSizeMessage;
		}
	}

	const QString sanitizedHtml = qtd.toHtml();
	if (tc) {
		tc->insertHtml(sanitizedHtml);
		return QString();
	}

	return sanitizedHtml;
}

void Log::log(MsgType mt, const QString &console, const QString &terse, bool ownMessage, const QString &overrideTTS,
			  bool ignoreTTS) {
	if (QThread::currentThread() != thread()) {
		// Invoke in main thread in order to keep the Qt gods on our side by not calling any UI
		// functions from a separate thread (can lead to program crashes)
		QMetaObject::invokeMethod(this, "log", Qt::QueuedConnection, Q_ARG(Log::MsgType, mt),
								  Q_ARG(const QString &, console), Q_ARG(const QString &, terse),
								  Q_ARG(bool, ownMessage), Q_ARG(const QString &, overrideTTS), Q_ARG(bool, ignoreTTS));
		return;
	}

	QDateTime dt = QDateTime::currentDateTime();

	int ignore = qmIgnore.value(mt);
	if (ignore) {
		ignore--;
		qmIgnore.insert(mt, ignore);
		return;
	}

	QString plain = QTextDocumentFragment::fromHtml(console).toPlainText();

	quint32 flags = Global::get().s.qmMessages.value(mt);

	// Message output on console
	if ((flags & Settings::LogConsole)) {
		bool dateChanged = false;
		if (qdDate != dt.date()) {
			qdDate      = dt.date();
			dateChanged = true;
		}

		const QString timeString =
			dt.time().toString(QLatin1String(Global::get().s.bLog24HourClock ? "HH:mm:ss" : "hh:mm:ss AP"));

		// Keep the historical console-log newline normalization for notification/TTS text.
		plain.replace(QLatin1String("\r\n"), QLatin1String("\n")).replace(QLatin1String("\r"), QLatin1String("\n"));

		LogDocument modernEntryDocument;
		QTextCursor modernEntryCursor(&modernEntryDocument);
		if (dateChanged) {
			modernEntryCursor.insertHtml(
				tr("[Date changed to %1]\n").arg(QLocale().toString(qdDate, QLocale::ShortFormat).toHtmlEscaped()));
			modernEntryCursor.insertBlock();
		}
		modernEntryCursor.insertHtml(
			Log::msgColor(QString::fromLatin1("[%1] ").arg(timeString.toHtmlEscaped()), Log::Time));
		validHtml(console, &modernEntryCursor);
		emit serverLogEntryAppended(QTextDocumentFragment(&modernEntryDocument).toHtml());

	}

	if (!ownMessage) {
		if (!(Global::get().mw->isActiveWindow() && Global::get().mw->isServerLogViewVisible())) {
			// Message notification with window highlight
			if (flags & Settings::LogHighlight) {
				emit highlightSpawned();
			}

			// Message notification with balloon tooltips
			if (flags & Settings::LogBalloon) {
				// Replace any instances of a "Object Replacement Character" from QTextDocumentFragment::toPlainText
				plain = plain.replace("\xEF\xBF\xBC", tr("[embedded content]"));

				QSystemTrayIcon::MessageIcon msgIcon = QSystemTrayIcon::NoIcon;
				switch (mt) {
					case DebugInfo:
					case CriticalError:
						msgIcon = QSystemTrayIcon::Critical;
						break;
					case Warning:
						msgIcon = QSystemTrayIcon::Warning;
						break;
					case TextMessage:
					case PrivateTextMessage:
						msgIcon = QSystemTrayIcon::NoIcon;
						break;
					case Information:
					case ServerConnected:
					case ServerDisconnected:
					case UserJoin:
					case UserLeave:
					case Recording:
					case YouKicked:
					case UserKicked:
					case SelfMute:
					case OtherSelfMute:
					case YouMuted:
					case YouMutedOther:
					case OtherMutedOther:
					case ChannelJoin:
					case ChannelLeave:
					case PermissionDenied:
					case SelfUnmute:
					case SelfDeaf:
					case SelfUndeaf:
					case UserRenamed:
					case SelfChannelJoin:
					case SelfChannelJoinOther:
					case ChannelJoinConnect:
					case ChannelLeaveDisconnect:
					case ChannelListeningAdd:
					case ChannelListeningRemove:
					case PluginMessage:
						msgIcon = QSystemTrayIcon::Information;
						break;
				}
				emit notificationSpawned(msgName(mt), plain, msgIcon);
			}
		}

		// Don't make any noise if we are self deafened (Unless it is the sound for activating self deaf)
		if (Global::get().s.bDeaf && mt != Log::SelfDeaf) {
			return;
		}

		// Message notification with static sounds
		qsizetype connectedUsers = 0;
		{
			QReadLocker lock(&ClientUser::c_qrwlUsers);
			connectedUsers = ClientUser::c_qmUsers.size();
		}
		if ((flags & Settings::LogSoundfile)
			&& !(flags & Settings::LogMessageLimit && connectedUsers > Global::get().s.iMessageLimitUserThreshold)
			&& !NotificationSoundBlocker::s_blockedNotificationSounds.contains(mt)) {
			QString sSound    = Global::get().s.qmMessageSounds.value(mt);
			AudioOutputPtr ao = Global::get().ao;

			if (!ao || !ao->playSample(sSound, Global::get().s.notificationVolume)) {
				qWarning() << "Sound file" << sSound << "is not a valid audio file, fallback to TTS.";
				flags ^= Settings::LogSoundfile | Settings::LogTTS; // Fallback to TTS
			}
		}
	} else if (!Global::get().s.bTTSMessageReadBack) {
		return;
	}

	// Message notification with Text-To-Speech
	if (Global::get().s.bDeaf || !Global::get().s.bTTS || !(flags & Settings::LogTTS) || ignoreTTS) {
		return;
	}

	// If overrideTTS is a valid string use its contents as message
	if (!overrideTTS.isNull()) {
		plain = overrideTTS;
	}

	// Apply simplifications to spoken text
	const QRegularExpression identifyURL(QRegularExpression::anchoredPattern(QLatin1String("[a-z-]+://[^ <]*")),
										 QRegularExpression::CaseInsensitiveOption);

	const QStringList qslAllowed  = allowedSchemes();
	QRegularExpressionMatch match = identifyURL.match(plain);
	qsizetype pos                 = 0;

	while (match.hasMatch()) {
		QUrl url(match.captured(0).toLower());
		if (url.isValid() && qslAllowed.contains(url.scheme())) {
			// Replace it appropriately
			QString replacement;
			QString host = url.host().replace(QRegularExpression(QLatin1String("^www.")), QString());

			if (url.scheme() == QLatin1String("http") || url.scheme() == QLatin1String("https"))
				replacement = tr("link to %1").arg(host);
			else if (url.scheme() == QLatin1String("ftp"))
				replacement = tr("FTP link to %1").arg(host);
			else if (url.scheme() == QLatin1String("clientid"))
				replacement = tr("player link");
			else if (url.scheme() == QLatin1String("channelid"))
				replacement = tr("channel link");
			else
				replacement = tr("%1 link").arg(url.scheme());

			plain.replace(pos, match.capturedLength(), replacement);
		} else {
			pos += match.capturedLength();
		}

		match = identifyURL.match(plain, pos);
	}

#ifndef USE_NO_TTS
	// TTS threshold limiter.
	if (plain.length() <= Global::get().s.iTTSThreshold)
		tts->say(plain);
	else if ((!terse.isEmpty()) && (terse.length() <= Global::get().s.iTTSThreshold))
		tts->say(terse);
#else
	// Mark as unused
	Q_UNUSED(terse);
#endif
}

void Log::processDeferredLogs() {
	QMutexLocker mLocker(&Log::qmDeferredLogs);

	while (!qvDeferredLogs.isEmpty()) {
		LogMessage msg = qvDeferredLogs.takeFirst();

		log(msg.mt, msg.console, msg.terse, msg.ownMessage, msg.overrideTTS, msg.ignoreTTS);
	}
}

LogMessage::LogMessage(Log::MsgType mt, const QString &console, const QString &terse, bool ownMessage,
					   const QString &overrideTTS, bool ignoreTTS)
	: mt(mt), console(console), terse(terse), ownMessage(ownMessage), overrideTTS(overrideTTS), ignoreTTS(ignoreTTS) {
}

LogDocument::LogDocument(QObject *p) : QTextDocument(p) {
}

QVariant LogDocument::loadResource(int type, const QUrl &url) {
	// Ignore requests for all external resources
	// that aren't images. We don't support any of them.
	if (type != QTextDocument::ImageResource) {
		addResource(type, url, QByteArray());
		return QByteArray();
	}

	// Allow inline data URLs plus internal chat image resources that were pre-registered
	// on the document before setHtml().
	if (url.isValid() && (url.scheme() == QLatin1String("data")
						  || url.scheme() == QLatin1String("mumble-chat-image")
						  || url.scheme() == QLatin1String("mumble-preview"))) {
		return QTextDocument::loadResource(type, url);
	}

	QImage qi(1, 1, QImage::Format_Mono);
	addResource(type, url, qi);

	return qi;
}

std::set< Log::MsgType > NotificationSoundBlocker::s_blockedNotificationSounds;

NotificationSoundBlocker::NotificationSoundBlocker(Log::MsgType msgType) : m_msgType(msgType) {
	s_blockedNotificationSounds.insert(m_msgType);
}

NotificationSoundBlocker::~NotificationSoundBlocker() {
	s_blockedNotificationSounds.erase(m_msgType);
}
