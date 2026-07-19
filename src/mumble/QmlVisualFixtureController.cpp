// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlVisualFixtureController.h"

#include "ComposerController.h"
#include "GlobalShortcutTypes.h"
#include "HostAddress.h"
#include "Log.h"
#include "ModernConnectController.h"
#include "ModernProductDialogStateFactory.h"
#include "ModernRecorderController.h"
#include "ModernServerAdminController.h"
#include "ModernSettingsController.h"
#include "PersistentChatMediaCache.h"
#include "QmlClientModels.h"
#include "QmlImageProvider.h"
#include "QmlShellHost.h"
#include "QmlThemeController.h"
#include "ScreenShareManager.h"
#include "ScreenShareViewBackend.h"
#ifdef USE_MANUAL_PLUGIN
#	include "ManualPluginController.h"
#endif

#include <QtCore/QCoreApplication>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEvent>
#include <QtCore/QEventLoop>
#include <QtCore/QHash>
#include <QtCore/QSignalBlocker>
#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QLinearGradient>
#include <QtGui/QPainter>
#include <QtGui/QPolygon>
#include <QtNetwork/QHostAddress>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>

#include <atomic>
#include <memory>

namespace {
	QQuickItem *quickItemByObjectName(QQuickItem *root, const QString &objectName) {
		if (!root || objectName.isEmpty()) return nullptr;
		if (root->objectName() == objectName) return root;
		for (QQuickItem *child : root->childItems()) {
			if (QQuickItem *match = quickItemByObjectName(child, objectName)) return match;
		}
		return nullptr;
	}

#if defined(MUMBLE_HAS_MODERN_UI_MOCKUPS) || defined(MUMBLE_HAS_MODERN_UI_AUTOMATION)
	QImage visualScreenShareFrame() {
		QImage frame(QSize(1280, 720), QImage::Format_ARGB32_Premultiplied);
		QPainter painter(&frame);
		painter.setRenderHint(QPainter::Antialiasing, true);
		QLinearGradient background(0, 0, frame.width(), frame.height());
		background.setColorAt(0.0, QColor(QStringLiteral("#13243f")));
		background.setColorAt(1.0, QColor(QStringLiteral("#27183f")));
		painter.fillRect(frame.rect(), background);

		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(QStringLiteral("#0b1220")));
		painter.drawRoundedRect(QRectF(48, 42, 1184, 636), 22, 22);
		painter.setBrush(QColor(QStringLiteral("#111b2e")));
		painter.drawRoundedRect(QRectF(70, 64, 1140, 60), 14, 14);
		painter.setBrush(QColor(QStringLiteral("#7c6df2")));
		painter.drawEllipse(QRectF(92, 84, 20, 20));
		painter.setBrush(QColor(QStringLiteral("#31405b")));
		painter.drawRoundedRect(QRectF(128, 82, 250, 24), 8, 8);
		painter.setBrush(QColor(QStringLiteral("#22304a")));
		painter.drawRoundedRect(QRectF(70, 146, 190, 510), 14, 14);

		const QList< QColor > railColors { QColor(QStringLiteral("#7868e6")), QColor(QStringLiteral("#2fbb9b")),
			QColor(QStringLiteral("#ef8b68")), QColor(QStringLiteral("#5797e8")) };
		for (int index = 0; index < railColors.size(); ++index) {
			painter.setBrush(railColors.at(index));
			painter.drawRoundedRect(QRectF(92, 174 + index * 64, 34, 34), 10, 10);
			painter.setBrush(QColor(QStringLiteral("#40506b")));
			painter.drawRoundedRect(QRectF(140, 181 + index * 64, 88 + index * 12, 10), 5, 5);
			painter.drawRoundedRect(QRectF(140, 197 + index * 64, 62, 7), 3.5, 3.5);
		}

		painter.setBrush(QColor(QStringLiteral("#18243a")));
		painter.drawRoundedRect(QRectF(282, 146, 620, 510), 14, 14);
		painter.setBrush(QColor(QStringLiteral("#263653")));
		painter.drawRoundedRect(QRectF(312, 178, 560, 84), 12, 12);
		painter.setBrush(QColor(QStringLiteral("#7081ff")));
		painter.drawRoundedRect(QRectF(336, 199, 240, 16), 8, 8);
		painter.setBrush(QColor(QStringLiteral("#455571")));
		painter.drawRoundedRect(QRectF(336, 226, 448, 10), 5, 5);

		const QList< QColor > cardColors { QColor(QStringLiteral("#1d6a72")), QColor(QStringLiteral("#4b3f82")),
			QColor(QStringLiteral("#7c493c")) };
		for (int index = 0; index < cardColors.size(); ++index) {
			const qreal x = 312 + index * 184;
			painter.setBrush(cardColors.at(index));
			painter.drawRoundedRect(QRectF(x, 292, 160, 128), 12, 12);
			painter.setBrush(QColor(255, 255, 255, 46));
			painter.drawRoundedRect(QRectF(x + 18, 314, 124, 52), 9, 9);
			painter.drawRoundedRect(QRectF(x + 18, 382, 92, 9), 4.5, 4.5);
		}

		painter.setBrush(QColor(QStringLiteral("#22304a")));
		painter.drawRoundedRect(QRectF(312, 448, 560, 176), 12, 12);
		painter.setPen(QPen(QColor(QStringLiteral("#6f7ff4")), 5, Qt::SolidLine, Qt::RoundCap, Qt::RoundJoin));
		painter.drawPolyline(QPolygonF { QPointF(342, 572), QPointF(430, 528), QPointF(510, 548),
			QPointF(612, 484), QPointF(706, 512), QPointF(840, 472) });
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(QStringLiteral("#18243a")));
		painter.drawRoundedRect(QRectF(924, 146, 286, 510), 14, 14);
		for (int index = 0; index < 5; ++index) {
			painter.setBrush(index == 0 ? QColor(QStringLiteral("#2fbb9b")) : QColor(QStringLiteral("#34445f")));
			painter.drawEllipse(QRectF(952, 180 + index * 78, 38, 38));
			painter.setBrush(QColor(QStringLiteral("#4d5d78")));
			painter.drawRoundedRect(QRectF(1004, 186 + index * 78, 154, 10), 5, 5);
			painter.drawRoundedRect(QRectF(1004, 204 + index * 78, 106, 8), 4, 4);
		}
		painter.end();
		return frame;
	}
#endif

	QStringList supportedMotdVariants() {
		return { QStringLiteral("none"), QStringLiteral("expanded"), QStringLiteral("collapsed"),
				 QStringLiteral("changed"), QStringLiteral("history-hidden") };
	}

	QStringList supportedRichPreviewVariants() {
		return { QStringLiteral("none"), QStringLiteral("youtube"), QStringLiteral("spotify"),
				 QStringLiteral("tiktok"), QStringLiteral("vimeo"), QStringLiteral("dailymotion"),
				 QStringLiteral("soundcloud"), QStringLiteral("instagram"), QStringLiteral("finance"),
				 QStringLiteral("audio"), QStringLiteral("product"), QStringLiteral("steam"),
				 QStringLiteral("google"), QStringLiteral("twitch"), QStringLiteral("flashback"),
				 QStringLiteral("x"), QStringLiteral("github"), QStringLiteral("social"),
				 QStringLiteral("vehicle"), QStringLiteral("property"), QStringLiteral("marketplace"),
				 QStringLiteral("article"), QStringLiteral("weather"), QStringLiteral("place"),
				 QStringLiteral("traffic"), QStringLiteral("link-digest"),
				 QStringLiteral("sensitive"), QStringLiteral("direct-media"),
				 QStringLiteral("loading"), QStringLiteral("error") };
	}

	QString presentationFamilyForCaseVariant(const QString &variant) {
		if (variant == QLatin1String("none")) return QStringLiteral("shell");
		if (QStringList { QStringLiteral("youtube"), QStringLiteral("spotify"),
				QStringLiteral("tiktok"), QStringLiteral("vimeo"), QStringLiteral("dailymotion"),
				QStringLiteral("soundcloud") }.contains(variant)) return QStringLiteral("embed");
		if (QStringList { QStringLiteral("instagram"), QStringLiteral("audio"), QStringLiteral("x"),
				QStringLiteral("github"), QStringLiteral("twitch") }.contains(variant)) {
			return QStringLiteral("identity");
		}
		if (variant == QLatin1String("finance")) return QStringLiteral("market");
		if (QStringList { QStringLiteral("product"), QStringLiteral("steam"), QStringLiteral("vehicle"),
				QStringLiteral("property"), QStringLiteral("marketplace") }.contains(variant)) {
			return QStringLiteral("commerce");
		}
		if (QStringList { QStringLiteral("google"), QStringLiteral("flashback"),
				QStringLiteral("article"), QStringLiteral("weather"), QStringLiteral("place"),
				QStringLiteral("traffic"), QStringLiteral("link-digest") }.contains(variant)) {
			return QStringLiteral("details");
		}
		if (variant == QLatin1String("social")) return QStringLiteral("generic");
		if (QStringList { QStringLiteral("sensitive"), QStringLiteral("direct-media") }.contains(variant)) {
			return QStringLiteral("media");
		}
		if (QStringList { QStringLiteral("loading"), QStringLiteral("error") }.contains(variant)) {
			return QStringLiteral("state");
		}
		if (variant == QLatin1String("rich-image-link")) return QStringLiteral("message-body");
		return {};
	}

	QStringList supportedCaseVariants() {
		QStringList variants = supportedRichPreviewVariants();
		variants.push_back(QStringLiteral("rich-image-link"));
		return variants;
	}

	QStringList supportedPresentationFamilies() {
		return { QStringLiteral("shell"), QStringLiteral("embed"), QStringLiteral("identity"),
				 QStringLiteral("market"), QStringLiteral("commerce"), QStringLiteral("details"),
				 QStringLiteral("generic"), QStringLiteral("media"), QStringLiteral("state"),
				 QStringLiteral("message-body") };
	}

	QStringList supportedSurfaceVariants() {
		return { QStringLiteral("none"),
			QStringLiteral("settings-audio-input"), QStringLiteral("settings-audio-input-advanced"),
			QStringLiteral("settings-audio-output"), QStringLiteral("settings-audio-output-advanced"),
			QStringLiteral("settings-appearance"), QStringLiteral("settings-user-interface"),
			QStringLiteral("settings-messages-sounds"), QStringLiteral("settings-messages-events"),
			QStringLiteral("settings-messages-events-compact"),
			QStringLiteral("settings-key-bindings"), QStringLiteral("settings-key-bindings-populated"),
			QStringLiteral("settings-network"), QStringLiteral("settings-network-advanced"),
			QStringLiteral("settings-screen-sharing"), QStringLiteral("settings-plugins"),
			QStringLiteral("settings-plugins-updating"), QStringLiteral("settings-plugins-partial"),
			QStringLiteral("settings-about"), QStringLiteral("dialog-connect"),
			QStringLiteral("dialog-connect-editor"), QStringLiteral("dialog-connect-validation"),
			QStringLiteral("dialog-connect-empty"),
			QStringLiteral("dialog-search-empty"), QStringLiteral("dialog-search-results"),
			QStringLiteral("dialog-search-regex-error"),
			QStringLiteral("dialog-certificate"), QStringLiteral("dialog-certificate-create"),
			QStringLiteral("dialog-acl-populated"), QStringLiteral("dialog-stonks-populated"),
			QStringLiteral("dialog-recorder"), QStringLiteral("dialog-recorder-recording"),
			QStringLiteral("dialog-server-users-loading"), QStringLiteral("dialog-server-users-ready"),
			QStringLiteral("dialog-server-users-edit"), QStringLiteral("dialog-server-users-confirm"),
			QStringLiteral("dialog-server-bans-empty"), QStringLiteral("dialog-server-bans-edit"),
			QStringLiteral("dialog-server-bans-error"),
			QStringLiteral("menu-app"), QStringLiteral("menu-app-server"),
			QStringLiteral("menu-profile"),
			QStringLiteral("menu-room"), QStringLiteral("menu-text-room"),
			QStringLiteral("menu-participant"), QStringLiteral("menu-chat-background"),
			QStringLiteral("menu-message"), QStringLiteral("direct-message-main"),
			QStringLiteral("chat-message-states"), QStringLiteral("chat-composer-states"),
			QStringLiteral("chat-attachment-states"), QStringLiteral("chat-history-prepend-anchor"),
			QStringLiteral("conversation-search-match"), QStringLiteral("conversation-search-empty"),
			QStringLiteral("direct-message-tray"), QStringLiteral("direct-message-window"),
			QStringLiteral("screen-share-editor"),
			QStringLiteral("screen-share-editor-compact"),
			QStringLiteral("screen-share-view-loading"), QStringLiteral("screen-share-view-error"),
#if defined(MUMBLE_HAS_MODERN_UI_MOCKUPS) || defined(MUMBLE_HAS_MODERN_UI_AUTOMATION)
			QStringLiteral("screen-share-view-active"),
#endif
			QStringLiteral("screen-share-view-paused"), QStringLiteral("manual-plugin"),
			QStringLiteral("ptt-idle"), QStringLiteral("ptt-active"),
			QStringLiteral("toast-single"), QStringLiteral("toast-duplicate"),
			QStringLiteral("async-running"), QStringLiteral("async-error"),
			QStringLiteral("async-success"), QStringLiteral("update-banner"),
			QStringLiteral("watch-together-hosting"),
			QStringLiteral("attachment-viewer"), QStringLiteral("image-viewer"),
			QStringLiteral("media-inline-loading"), QStringLiteral("media-inline-active"),
			QStringLiteral("media-inline-error"), QStringLiteral("media-inline-retry"),
			QStringLiteral("media-inline-external"), QStringLiteral("media-inline-controls"),
			QStringLiteral("media-detached-loading"),
			QStringLiteral("media-detached-active"), QStringLiteral("media-detached-error"),
			QStringLiteral("media-detached-retry"), QStringLiteral("media-detached-external"),
			QStringLiteral("media-detached-controls") };
	}

	bool surfaceNeedsConnectedState(const QString &variant) {
		return variant != QLatin1String("none")
			&& !variant.startsWith(QLatin1String("dialog-connect"))
			&& !variant.startsWith(QLatin1String("dialog-certificate"));
	}

	QVariantMap visualOption(const QString &label, const QVariant &value, const bool enabled = true) {
		return { { QStringLiteral("label"), label }, { QStringLiteral("value"), value },
			{ QStringLiteral("enabled"), enabled } };
	}

	QVariantMap visualAction(const QString &id, const QString &label, const QString &tone = {},
						  const bool primary = false) {
		return { { QStringLiteral("id"), id }, { QStringLiteral("label"), label },
			{ QStringLiteral("tone"), tone }, { QStringLiteral("enabled"), true },
			{ QStringLiteral("primary"), primary } };
	}

	QVariantMap visualMenuAction(const QString &id, const QString &label, const QString &tone = {},
							 const bool checkable = false, const bool checked = false) {
		return { { QStringLiteral("kind"), QStringLiteral("action") },
			{ QStringLiteral("id"), id }, { QStringLiteral("label"), label },
			{ QStringLiteral("tone"), tone }, { QStringLiteral("enabled"), true },
			{ QStringLiteral("visible"), true }, { QStringLiteral("checkable"), checkable },
			{ QStringLiteral("checked"), checked } };
	}

	QVariantMap visualConnectDialog(const QString &variant) {
		Settings settings;
		settings.qsUsername = QStringLiteral("Demo User");

		FavoriteServer community;
		community.qsName     = QStringLiteral("Mumble Community");
		community.qsHostname = QStringLiteral("voice.example.invalid");
		community.usPort     = 64738;
		community.qsUsername = QStringLiteral("Demo User");

		FavoriteServer studio;
		studio.qsName     = QStringLiteral("Studio");
		studio.qsHostname = QStringLiteral("studio.example.invalid");
		studio.usPort     = 64739;
		studio.qsUsername = QStringLiteral("Producer");

		ModernConnectController controller;
		if (variant == QLatin1String("dialog-connect-empty")) {
			controller.open({}, settings);
			return controller.state();
		}

		controller.open({ community, studio }, settings);
		controller.setFavoritePing(community.qsHostname, community.usPort, 28, 18, 128);
		controller.setFavoritePing(studio.qsHostname, studio.usPort, 41, 6, 64);
		if (variant == QLatin1String("dialog-connect-editor")) {
			controller.invokeAction(QStringLiteral("editFavorite"),
				QVariantMap { { QStringLiteral("index"), 1 } });
		} else if (variant == QLatin1String("dialog-connect-validation")) {
			controller.invokeAction(QStringLiteral("newFavorite"), {});
			controller.updateField(QStringLiteral("username"), QString());
		}
		return controller.state();
	}

	QStringList supportedChatSurfaceVariants() {
		return { QStringLiteral("chat-message-states"), QStringLiteral("chat-composer-states"),
			QStringLiteral("chat-attachment-states"), QStringLiteral("chat-history-prepend-anchor") };
	}

	bool isChatSurfaceVariant(const QString &variant) {
		return supportedChatSurfaceVariants().contains(variant);
	}

	constexpr int VisualHistoryInitialMessageCount = 24;
	constexpr int VisualHistoryPrependMessageCount = 6;

	int visualMessageCount(const QString &state, const QString &surfaceVariant, const bool afterPresentation) {
		if (state != QLatin1String("connected")) return 0;
		if (surfaceVariant == QLatin1String("chat-message-states")) return 4;
		if (surfaceVariant == QLatin1String("chat-attachment-states")) return 1;
		if (surfaceVariant == QLatin1String("chat-history-prepend-anchor")) {
			return VisualHistoryInitialMessageCount
				+ (afterPresentation ? VisualHistoryPrependMessageCount : 0);
		}
		return 2;
	}

	QString visualHistoryMessageID(const QString &generation, const int sequence) {
		return QStringLiteral("fixture:%1:history:%2").arg(generation, QString::number(sequence));
	}

	QVariantList visualHistoryMessages(const QString &generation, const int firstSequence, const int count) {
		QVariantList messages;
		messages.reserve(count);
		for (int offset = 0; offset < count; ++offset) {
			const int sequence = firstSequence + offset;
			const bool own = sequence % 3 == 0;
			const QString actor = own ? QStringLiteral("Demo User")
				: sequence % 2 == 0 ? QStringLiteral("Alex") : QStringLiteral("Kira");
			QString body;
			if (sequence == 12) {
				body = QStringLiteral("Anchor message 12 remains in place after older history loads.");
			} else if (sequence <= 0) {
				body = QStringLiteral("Older history item %1 loaded incrementally.").arg(sequence + 7);
			} else {
				body = QStringLiteral("History message %1 keeps the timeline scrollable.").arg(sequence);
			}
			messages.push_back(QVariantMap {
				{ QStringLiteral("messageKey"), visualHistoryMessageID(generation, sequence) },
				{ QStringLiteral("actor"), actor }, { QStringLiteral("actorKey"), actor.toLower() },
				{ QStringLiteral("bodyText"), body },
				{ QStringLiteral("timeLabel"), QStringLiteral("09:%1").arg(qBound(0, sequence + 24, 59), 2, 10, QLatin1Char('0')) },
				{ QStringLiteral("own"), own }, { QStringLiteral("canReply"), true },
				{ QStringLiteral("canReact"), true }, { QStringLiteral("canDelete"), own },
				{ QStringLiteral("preview"), QVariantMap() }, { QStringLiteral("attachments"), QVariantList() },
				{ QStringLiteral("reactions"), QVariantList() }
			});
		}
		return messages;
	}

	QVariantMap visualSearchDialog(const QString &variant) {
		const bool hasResults = variant == QLatin1String("dialog-search-results");
		const bool regexError = variant == QLatin1String("dialog-search-regex-error");
		const QString query = hasResults ? QStringLiteral("relay")
			: regexError ? QStringLiteral("[") : QString();
		const QVariantList results = hasResults ? QVariantList {
			QVariantMap { { QStringLiteral("type"), QStringLiteral("channel") },
				{ QStringLiteral("id"), 1 }, { QStringLiteral("index"), 0 },
				{ QStringLiteral("title"), QObject::tr("Relay Ops") },
				{ QStringLiteral("subtitle"), QObject::tr("Root / Operations - room name match") },
				{ QStringLiteral("matchStart"), 0 }, { QStringLiteral("matchLength"), 5 },
				{ QStringLiteral("primaryAction"), QObject::tr("Open") },
				{ QStringLiteral("secondaryAction"), QObject::tr("Join") } },
			QVariantMap { { QStringLiteral("type"), QStringLiteral("textRoom") },
				{ QStringLiteral("id"), 2 }, { QStringLiteral("index"), 1 },
				{ QStringLiteral("title"), QObject::tr("#relay") },
				{ QStringLiteral("subtitle"), QObject::tr("Text room - 4 matching messages") },
				{ QStringLiteral("matchStart"), 1 }, { QStringLiteral("matchLength"), 5 },
				{ QStringLiteral("primaryAction"), QObject::tr("Open") } },
			QVariantMap { { QStringLiteral("type"), QStringLiteral("user") },
				{ QStringLiteral("id"), 7 }, { QStringLiteral("index"), 2 },
				{ QStringLiteral("title"), QObject::tr("Relay_Bot") },
				{ QStringLiteral("subtitle"), QObject::tr("User - Root / Operations") },
				{ QStringLiteral("matchStart"), 0 }, { QStringLiteral("matchLength"), 5 },
				{ QStringLiteral("primaryAction"), QObject::tr("Message") },
				{ QStringLiteral("secondaryAction"), QObject::tr("Select") } }
		} : QVariantList {};

		QVariantMap queryField {
			{ QStringLiteral("id"), QStringLiteral("search.query") },
			{ QStringLiteral("label"), QObject::tr("Search") },
			{ QStringLiteral("type"), QStringLiteral("text") },
			{ QStringLiteral("value"), query }, { QStringLiteral("enabled"), true },
			{ QStringLiteral("liveUpdate"), true }, { QStringLiteral("updateDelayMs"), 140 },
			{ QStringLiteral("resultListId"), QStringLiteral("search.results") }
		};
		QVariantMap resultsField {
			{ QStringLiteral("id"), QStringLiteral("search.results") },
			{ QStringLiteral("label"), QObject::tr("Results") },
			{ QStringLiteral("type"), QStringLiteral("resultList") },
			{ QStringLiteral("value"), results }, { QStringLiteral("items"), results },
			{ QStringLiteral("enabled"), false },
			{ QStringLiteral("emptyText"), query.isEmpty()
				? QObject::tr("Start typing to search users and rooms.")
				: QObject::tr("No matching users or rooms.") },
			{ QStringLiteral("inputFieldId"), QStringLiteral("search.query") }
		};
		const auto checkbox = [](const QString &id, const QString &label, const bool value) {
			return QVariantMap { { QStringLiteral("id"), id }, { QStringLiteral("label"), label },
				{ QStringLiteral("type"), QStringLiteral("checkbox") },
				{ QStringLiteral("value"), value }, { QStringLiteral("enabled"), true } };
		};
		const QVariantMap section {
			{ QStringLiteral("title"), QObject::tr("Search") },
			{ QStringLiteral("fields"), QVariantList { queryField,
				checkbox(QStringLiteral("search.users"), QObject::tr("Users"), true),
				checkbox(QStringLiteral("search.channels"), QObject::tr("Rooms"), true),
				checkbox(QStringLiteral("search.caseSensitive"), QObject::tr("Case sensitive"), false),
				checkbox(QStringLiteral("search.regex"), QObject::tr("Regular expression"), regexError),
				resultsField } }
		};
		const QVariantMap closeAction {
			{ QStringLiteral("kind"), QStringLiteral("action") },
			{ QStringLiteral("id"), QStringLiteral("close") },
			{ QStringLiteral("label"), QObject::tr("Close") },
			{ QStringLiteral("enabled"), true }, { QStringLiteral("checked"), false },
			{ QStringLiteral("closesDialog"), true }
		};
		QVariantMap dialog {
			{ QStringLiteral("id"), QStringLiteral("serverSearch") },
			{ QStringLiteral("kind"), QStringLiteral("form") },
			{ QStringLiteral("title"), QObject::tr("Search") },
			{ QStringLiteral("subtitle"), QObject::tr("Find users and rooms on the current server.") },
			{ QStringLiteral("sections"), QVariantList { section } },
			{ QStringLiteral("actions"), QVariantList { closeAction } },
			{ QStringLiteral("primaryActionId"), QStringLiteral("close") },
			{ QStringLiteral("initialFocusId"), QStringLiteral("search.query") },
			{ QStringLiteral("width"), 820 }, { QStringLiteral("height"), 680 }
		};
		if (regexError) {
			dialog.insert(QStringLiteral("errors"), QVariantMap {
				{ QStringLiteral("search.query"), QObject::tr("Invalid regular expression.") }
			});
		}
		return dialog;
	}

	QVariantMap visualSettingsDialog(const QString &variant) {
		// The visual gate deliberately consumes the same state builder as the live
		// Settings command. This keeps page definitions, field types, defaults and
		// footer actions from drifting into a second hand-authored fixture schema.
		static const QHash< QString, QString > pageForSurface {
			{ QStringLiteral("settings-audio-input"), QStringLiteral("audioInput") },
			{ QStringLiteral("settings-audio-input-advanced"), QStringLiteral("audioInput") },
			{ QStringLiteral("settings-audio-output"), QStringLiteral("audioOutput") },
			{ QStringLiteral("settings-audio-output-advanced"), QStringLiteral("audioOutput") },
			{ QStringLiteral("settings-appearance"), QStringLiteral("look") },
			{ QStringLiteral("settings-user-interface"), QStringLiteral("ui") },
			{ QStringLiteral("settings-messages-sounds"), QStringLiteral("messages") },
			{ QStringLiteral("settings-messages-events"), QStringLiteral("messages") },
			{ QStringLiteral("settings-messages-events-compact"), QStringLiteral("messages") },
			{ QStringLiteral("settings-key-bindings"), QStringLiteral("keys") },
			{ QStringLiteral("settings-key-bindings-populated"), QStringLiteral("keys") },
			{ QStringLiteral("settings-network"), QStringLiteral("network") },
			{ QStringLiteral("settings-network-advanced"), QStringLiteral("network") },
			{ QStringLiteral("settings-screen-sharing"), QStringLiteral("screenShare") },
			{ QStringLiteral("settings-plugins"), QStringLiteral("plugins") },
			{ QStringLiteral("settings-plugins-updating"), QStringLiteral("plugins") },
			{ QStringLiteral("settings-plugins-partial"), QStringLiteral("plugins") },
			{ QStringLiteral("settings-about"), QStringLiteral("about") }
		};
		const auto page = pageForSurface.constFind(variant);
		if (page == pageForSurface.cend()) return {};

		Settings visualSettings;
		if (variant == QLatin1String("settings-key-bindings-populated")) {
			Shortcut pushToTalk;
			pushToTalk.iIndex = GlobalShortcutType::PushToTalk;

			Shortcut muteSelf;
			muteSelf.iIndex    = GlobalShortcutType::MuteSelf;
			muteSelf.qvData    = 0;
			muteSelf.bSuppress = true;
			visualSettings.qlShortcuts = { pushToTalk, muteSelf };
		}
		ModernSettingsController controller;
		controller.open(visualSettings, page.value());
		if (variant.startsWith(QLatin1String("settings-messages-events"))) {
			const auto toggleEvent = [&controller](const Log::MsgType type, const QString &property) {
				controller.invokeAction(QStringLiteral("messages.toggleEvent"),
					QVariantMap { { QStringLiteral("messageType"), static_cast< int >(type) },
						{ QStringLiteral("property"), property },
						{ QStringLiteral("value"), true } });
			};
			toggleEvent(Log::DebugInfo, QStringLiteral("console"));
			toggleEvent(Log::CriticalError, QStringLiteral("console"));
			toggleEvent(Log::CriticalError, QStringLiteral("notification"));
			toggleEvent(Log::Warning, QStringLiteral("console"));
			toggleEvent(Log::Warning, QStringLiteral("highlight"));
			toggleEvent(Log::Information, QStringLiteral("console"));
			toggleEvent(Log::ServerConnected, QStringLiteral("console"));
			toggleEvent(Log::ServerConnected, QStringLiteral("tts"));
			toggleEvent(Log::ServerDisconnected, QStringLiteral("console"));
			toggleEvent(Log::ServerDisconnected, QStringLiteral("sound"));
		} else if (variant == QLatin1String("settings-key-bindings-populated")) {
			controller.invokeAction(QStringLiteral("keys.shortcutData"),
				QVariantMap { { QStringLiteral("index"), 1 }, { QStringLiteral("value"), 1 } });
			controller.invokeAction(QStringLiteral("keys.beginShortcutCapture"),
				QVariantMap { { QStringLiteral("index"), 0 } });
		}
		QVariantMap dialog = controller.state();
		QVariantList sections = dialog.value(QStringLiteral("sections")).toList();
		for (QVariant &sectionValue : sections) {
			QVariantMap section = sectionValue.toMap();
			QVariantList fields  = section.value(QStringLiteral("fields")).toList();
			for (QVariant &fieldValue : fields) {
				QVariantMap field = fieldValue.toMap();
				if (variant.startsWith(QLatin1String("settings-plugins"))
					&& field.value(QStringLiteral("type")).toString() == QLatin1String("pluginEditor")) {
					// Exercise the production PluginEditor with representative runtime states
					// instead of depending on whichever plugins happen to be installed on the
					// machine that records the visual gate. Stable synthetic IDs keep this a
					// presentation-only fixture; the live controller remains the source of the
					// field schema and all user actions.
					field.insert(QStringLiteral("rows"), QVariantList {
						QVariantMap {
							{ QStringLiteral("id"), 91001 },
							{ QStringLiteral("name"), QObject::tr("Manual placement") },
							{ QStringLiteral("description"),
							  QObject::tr("Place your positional-audio avatar manually when no game plugin is active.") },
							{ QStringLiteral("version"), QStringLiteral("1.2.0") },
							{ QStringLiteral("author"), QStringLiteral("Mumble") },
							{ QStringLiteral("path"), QObject::tr("Built in") },
							{ QStringLiteral("loaded"), true },
							{ QStringLiteral("enabled"), true },
							{ QStringLiteral("positionalAvailable"), true },
							{ QStringLiteral("positionalEnabled"), true },
							{ QStringLiteral("keyboardMonitoringAllowed"), false },
							{ QStringLiteral("canConfigure"), true },
							{ QStringLiteral("canShowAbout"), true },
							{ QStringLiteral("builtIn"), true } },
						QVariantMap {
							{ QStringLiteral("id"), 91002 },
							{ QStringLiteral("name"), QObject::tr("Game telemetry") },
							{ QStringLiteral("description"),
							  QObject::tr("Provides positional audio and optional keyboard-aware game context.") },
							{ QStringLiteral("version"), QStringLiteral("2.4.1") },
							{ QStringLiteral("author"), QStringLiteral("Community Labs") },
							{ QStringLiteral("path"),
							  QStringLiteral("C:/Users/fixture/AppData/Roaming/Mumble/Plugins/game-telemetry.mumble_plugin.dll") },
							{ QStringLiteral("loaded"), false },
							{ QStringLiteral("enabled"), false },
							{ QStringLiteral("positionalAvailable"), true },
							{ QStringLiteral("positionalEnabled"), false },
							{ QStringLiteral("keyboardMonitoringAllowed"), true },
							{ QStringLiteral("canConfigure"), false },
							{ QStringLiteral("canShowAbout"), false },
							{ QStringLiteral("builtIn"), false } },
						QVariantMap {
							{ QStringLiteral("id"), 91003 },
							{ QStringLiteral("name"), QObject::tr("Stream Deck controls") },
							{ QStringLiteral("description"),
							  QObject::tr("Adds mute, deafen and channel controls to supported control surfaces.") },
							{ QStringLiteral("version"), QStringLiteral("0.9.8") },
							{ QStringLiteral("author"), QStringLiteral("Control Surface Project") },
							{ QStringLiteral("path"),
							  QStringLiteral("C:/Program Files/Mumble/plugins/stream-deck.mumble_plugin.dll") },
							{ QStringLiteral("loaded"), true },
							{ QStringLiteral("enabled"), true },
							{ QStringLiteral("positionalAvailable"), false },
							{ QStringLiteral("positionalEnabled"), false },
							{ QStringLiteral("keyboardMonitoringAllowed"), false },
							{ QStringLiteral("canConfigure"), true },
							{ QStringLiteral("canShowAbout"), true },
							{ QStringLiteral("builtIn"), false } }
					});
					if (variant == QLatin1String("settings-plugins-updating")) {
						field.insert(QStringLiteral("operation"), QVariantMap {
							{ QStringLiteral("id"), QStringLiteral("visual:plugin-update") },
							{ QStringLiteral("kind"), QStringLiteral("plugin-update") },
							{ QStringLiteral("status"), QStringLiteral("running") },
							{ QStringLiteral("title"), QObject::tr("Updating plugins") },
							{ QStringLiteral("subtitle"), QObject::tr("Downloading Game telemetry") },
							{ QStringLiteral("phase"), QStringLiteral("download") },
							{ QStringLiteral("progress"), 54 },
							{ QStringLiteral("completedItems"), 1 },
							{ QStringLiteral("totalItems"), 3 },
							{ QStringLiteral("cancellable"), true },
							{ QStringLiteral("itemResults"), QVariantList {
								QVariantMap { { QStringLiteral("itemId"), QStringLiteral("plugin:91001") },
									{ QStringLiteral("pluginId"), 91001 }, { QStringLiteral("name"), QObject::tr("Manual placement") },
									{ QStringLiteral("success"), true }, { QStringLiteral("cancelled"), false },
									{ QStringLiteral("message"), QObject::tr("Already current") } } } }
						});
					} else if (variant == QLatin1String("settings-plugins-partial")) {
						field.insert(QStringLiteral("operation"), QVariantMap {
							{ QStringLiteral("id"), QStringLiteral("visual:plugin-update") },
							{ QStringLiteral("kind"), QStringLiteral("plugin-update") },
							{ QStringLiteral("status"), QStringLiteral("partial") },
							{ QStringLiteral("title"), QObject::tr("Plugin update finished") },
							{ QStringLiteral("subtitle"), QObject::tr("2 updated · 1 failed") },
							{ QStringLiteral("phase"), QStringLiteral("complete") },
							{ QStringLiteral("progress"), 100 },
							{ QStringLiteral("completedItems"), 3 },
							{ QStringLiteral("totalItems"), 3 },
							{ QStringLiteral("successfulItems"), 2 },
							{ QStringLiteral("failedItems"), 1 },
							{ QStringLiteral("cancelledItems"), 0 },
							{ QStringLiteral("cancellable"), false },
							{ QStringLiteral("itemResults"), QVariantList {
								QVariantMap { { QStringLiteral("itemId"), QStringLiteral("plugin:91001") },
									{ QStringLiteral("pluginId"), 91001 }, { QStringLiteral("name"), QObject::tr("Manual placement") },
									{ QStringLiteral("success"), true }, { QStringLiteral("cancelled"), false },
									{ QStringLiteral("message"), QObject::tr("Already current") } },
								QVariantMap { { QStringLiteral("itemId"), QStringLiteral("plugin:91002") },
									{ QStringLiteral("pluginId"), 91002 }, { QStringLiteral("name"), QObject::tr("Game telemetry") },
									{ QStringLiteral("success"), false }, { QStringLiteral("cancelled"), false },
									{ QStringLiteral("errorCode"), QStringLiteral("signature-invalid") },
									{ QStringLiteral("message"), QObject::tr("Signature verification failed") } },
								QVariantMap { { QStringLiteral("itemId"), QStringLiteral("plugin:91003") },
									{ QStringLiteral("pluginId"), 91003 }, { QStringLiteral("name"), QObject::tr("Stream Deck controls") },
									{ QStringLiteral("success"), true }, { QStringLiteral("cancelled"), false },
									{ QStringLiteral("message"), QObject::tr("Updated to 0.9.9") } } } }
						});
					}
				} else if (field.value(QStringLiteral("id")).toString()
					== QLatin1String("network.clearPreviewCache")) {
					field.insert(QStringLiteral("hint"),
						QObject::tr("Current cache: %1. Stored only on this device.")
							.arg(PersistentChatMediaCache::formattedSize(0)));
				} else if (field.value(QStringLiteral("type")).toString() == QLatin1String("readonly")
						   && field.value(QStringLiteral("label")).toString()
							  == QObject::tr("Operating system")) {
					// The live page still reports QSysInfo's precise product label. The
					// gate uses its platform scope so baselines do not depend on the
					// developer/CI Windows edition string.
					field.insert(QStringLiteral("value"), QStringLiteral("Windows"));
				}
				fieldValue = field;
			}
			section.insert(QStringLiteral("fields"), fields);
			sectionValue = section;
		}
		dialog.insert(QStringLiteral("sections"), sections);
		if (variant == QLatin1String("settings-network-advanced")
			|| variant == QLatin1String("settings-audio-input-advanced")
			|| variant == QLatin1String("settings-audio-output-advanced")) {
			// This is the state reached by the native dialog's Advanced toggle. The
			// underlying sections remain the production controller's unmodified DTO.
			dialog.insert(QStringLiteral("showAdvanced"), true);
			// Capture the expert controls rather than repeating the basic page from
			// contentY 0. QmlDialog uses this same initial-focus contract to reveal a
			// keyboard target without exposing partially clipped fields to UIA.
			const QString initialFocusId = variant == QLatin1String("settings-network-advanced")
				? QStringLiteral("network.qos")
				: variant == QLatin1String("settings-audio-input-advanced")
					? QStringLiteral("audio.vadMin")
					: QStringLiteral("audio.jitterBuffer");
			dialog.insert(QStringLiteral("initialFocusId"), initialFocusId);
		} else if (variant.startsWith(QLatin1String("settings-messages-events"))) {
			dialog.insert(QStringLiteral("initialFocusId"), QStringLiteral("messageEventList"));
		} else if (variant == QLatin1String("settings-key-bindings-populated")) {
			dialog.insert(QStringLiteral("initialFocusId"), QStringLiteral("shortcutList"));
		} else if (variant == QLatin1String("settings-plugins-updating")) {
			dialog.insert(QStringLiteral("initialFocusId"), QStringLiteral("pluginOperationCancelButton"));
		} else if (variant == QLatin1String("settings-plugins-partial")) {
			dialog.insert(QStringLiteral("initialFocusId"), QStringLiteral("pluginCheckUpdatesButton"));
		}
		return dialog;
	}

	QVariantMap visualAclDialog() {
		const QVariantList permissions {
			QVariantMap { { QStringLiteral("id"), 1 }, { QStringLiteral("label"), QObject::tr("Write") } },
			QVariantMap { { QStringLiteral("id"), 2 }, { QStringLiteral("label"), QObject::tr("Traverse") } },
			QVariantMap { { QStringLiteral("id"), 4 }, { QStringLiteral("label"), QObject::tr("Enter") } },
			QVariantMap { { QStringLiteral("id"), 8 }, { QStringLiteral("label"), QObject::tr("Speak") } },
			QVariantMap { { QStringLiteral("id"), 16 }, { QStringLiteral("label"), QObject::tr("Mute/deafen") } },
			QVariantMap { { QStringLiteral("id"), 32 }, { QStringLiteral("label"), QObject::tr("Move") } }
		};
		const QVariantList groups {
			QVariantMap { { QStringLiteral("name"), QStringLiteral("scrim-team") },
				{ QStringLiteral("inherit"), false }, { QStringLiteral("inheritable"), true },
				{ QStringLiteral("inherited"), false }, { QStringLiteral("add"), QVariantList { 2, 7 } },
				{ QStringLiteral("remove"), QVariantList() }, { QStringLiteral("inheritedMembers"), QVariantList() } }
		};
		const QVariantList rules {
			QVariantMap { { QStringLiteral("targetType"), QStringLiteral("group") },
				{ QStringLiteral("target"), QStringLiteral("all") }, { QStringLiteral("applyHere"), true },
				{ QStringLiteral("applySubs"), true }, { QStringLiteral("inherited"), true },
				{ QStringLiteral("allow"), QVariantList { 2 } }, { QStringLiteral("deny"), QVariantList { 1 } } },
			QVariantMap { { QStringLiteral("targetType"), QStringLiteral("group") },
				{ QStringLiteral("target"), QStringLiteral("scrim-team") }, { QStringLiteral("applyHere"), true },
				{ QStringLiteral("applySubs"), false }, { QStringLiteral("inherited"), false },
				{ QStringLiteral("allow"), QVariantList { 1, 2, 4, 8 } }, { QStringLiteral("deny"), QVariantList() } },
			QVariantMap { { QStringLiteral("targetType"), QStringLiteral("user") },
				{ QStringLiteral("target"), QStringLiteral("Kira") }, { QStringLiteral("userId"), 2 },
				{ QStringLiteral("applyHere"), true }, { QStringLiteral("applySubs"), true },
				{ QStringLiteral("inherited"), false }, { QStringLiteral("allow"), QVariantList { 16, 32 } },
				{ QStringLiteral("deny"), QVariantList() } }
		};
		const QVariantMap aclModel {
			{ QStringLiteral("channelId"), 9001 }, { QStringLiteral("inheritAcls"), true },
			{ QStringLiteral("password"), QStringLiteral("scrim-night") },
			{ QStringLiteral("groups"), groups }, { QStringLiteral("acls"), rules },
			{ QStringLiteral("permissions"), permissions },
			{ QStringLiteral("userOptions"), QVariantList {
				QVariantMap { { QStringLiteral("value"), 2 }, { QStringLiteral("label"), QStringLiteral("Kira (#2)") } },
				QVariantMap { { QStringLiteral("value"), 7 }, { QStringLiteral("label"), QStringLiteral("Nova (#7)") } }
			} }
		};
		return {
			{ QStringLiteral("id"), QStringLiteral("acl") }, { QStringLiteral("kind"), QStringLiteral("form") },
			{ QStringLiteral("title"), QObject::tr("Edit room") },
			{ QStringLiteral("subtitle"), QObject::tr("Manage room details, groups, and access rules for Lobby.") },
			{ QStringLiteral("sections"), QVariantList {
				QVariantMap { { QStringLiteral("title"), QObject::tr("Access control") },
					{ QStringLiteral("fields"), QVariantList { QVariantMap {
						{ QStringLiteral("id"), QStringLiteral("acl.model") },
						{ QStringLiteral("label"), QObject::tr("ACL") },
						{ QStringLiteral("type"), QStringLiteral("aclEditor") },
						{ QStringLiteral("value"), aclModel }, { QStringLiteral("enabled"), true } } } } }
			} },
			{ QStringLiteral("actions"), QVariantList {
				visualAction(QStringLiteral("cancel"), QObject::tr("Cancel")),
				visualAction(QStringLiteral("saveAcl"), QObject::tr("Save room"), QStringLiteral("accent"), true) } },
			{ QStringLiteral("primaryActionId"), QStringLiteral("saveAcl") },
			{ QStringLiteral("initialFocusId"), QStringLiteral("aclGroupName_0") },
			{ QStringLiteral("tone"), QStringLiteral("wide") },
			{ QStringLiteral("preferredWidth"), 1040 }, { QStringLiteral("preferredHeight"), 780 }
		};
	}

	QVariantMap visualStonksDialog() {
		const QVariantMap position {
			{ QStringLiteral("symbol"), QStringLiteral("RKLB") },
			{ QStringLiteral("displayName"), QObject::tr("Rocket Lab USA") },
			{ QStringLiteral("quantity"), 42.0 }, { QStringLiteral("price"), 18.42 },
			{ QStringLiteral("marketValue"), 773.64 }, { QStringLiteral("currency"), QStringLiteral("USD") },
			{ QStringLiteral("providerId"), QStringLiteral("fixture") },
			{ QStringLiteral("exchange"), QStringLiteral("Nasdaq") }
		};
		const QVariantMap snapshot {
			{ QStringLiteral("snapshotId"), 77 }, { QStringLiteral("userId"), 1 },
			{ QStringLiteral("userName"), QStringLiteral("Demo User") },
			{ QStringLiteral("createdAt"), QVariant::fromValue< qulonglong >(1779926400ULL) },
			{ QStringLiteral("currency"), QStringLiteral("USD") }, { QStringLiteral("totalValue"), 2744.04 },
			{ QStringLiteral("note"), QObject::tr("Community test portfolio") },
			{ QStringLiteral("positionsRedacted"), false }, { QStringLiteral("positions"), QVariantList { position } }
		};
		const QVariantMap stonks {
			{ QStringLiteral("supported"), true }, { QStringLiteral("enabled"), true },
			{ QStringLiteral("registered"), true }, { QStringLiteral("canAdmin"), true },
			{ QStringLiteral("selfUserId"), 1 }, { QStringLiteral("selectedUserId"), 1 },
			{ QStringLiteral("selectedUserName"), QStringLiteral("Demo User") },
			{ QStringLiteral("selectedPeriod"), QStringLiteral("30d") },
			{ QStringLiteral("periods"), QVariantList { QStringLiteral("7d"), QStringLiteral("30d"), QStringLiteral("ytd") } },
			{ QStringLiteral("status"), QObject::tr("Portfolio data is ready.") },
			{ QStringLiteral("snapshots"), QVariantList { snapshot } },
			{ QStringLiteral("leaderboard"), QVariantList {
				QVariantMap { { QStringLiteral("rank"), 1 }, { QStringLiteral("userId"), 2 },
					{ QStringLiteral("userName"), QStringLiteral("Alex") },
					{ QStringLiteral("period"), QStringLiteral("30d") },
					{ QStringLiteral("returnPercent"), 18.42 }, { QStringLiteral("followed"), true } },
				QVariantMap { { QStringLiteral("rank"), 2 }, { QStringLiteral("userId"), 1 },
					{ QStringLiteral("userName"), QStringLiteral("Demo User") },
					{ QStringLiteral("period"), QStringLiteral("30d") },
					{ QStringLiteral("returnPercent"), 9.75 }, { QStringLiteral("followed"), false } }
			} },
			{ QStringLiteral("users"), QVariantList {
				QVariantMap { { QStringLiteral("userId"), 1 }, { QStringLiteral("userName"), QStringLiteral("Demo User") } },
				QVariantMap { { QStringLiteral("userId"), 2 }, { QStringLiteral("userName"), QStringLiteral("Alex") },
					{ QStringLiteral("followed"), true } } } },
			{ QStringLiteral("popularTickers"), QVariantList {
				QVariantMap { { QStringLiteral("symbol"), QStringLiteral("RKLB") },
					{ QStringLiteral("displayName"), QObject::tr("Rocket Lab USA") },
					{ QStringLiteral("holderCount"), 4 }, { QStringLiteral("currency"), QStringLiteral("USD") } } } },
			{ QStringLiteral("personalTickers"), QVariantList { position } },
			{ QStringLiteral("pinnedTickers"), QVariantList { position } },
			{ QStringLiteral("feedPreferences"), QVariantMap { { QStringLiteral("showMine"), true },
				{ QStringLiteral("showPopular"), true }, { QStringLiteral("showPins"), true } } },
			{ QStringLiteral("feature"), QVariantMap { { QStringLiteral("tickerBannerEnabled"), true },
				{ QStringLiteral("tickerBannerAlwaysScroll"), false } } },
			{ QStringLiteral("textChannelId"), 7 }, { QStringLiteral("socialAnnouncementsEnabled"), true },
			{ QStringLiteral("textChannels"), QVariantList { QVariantMap {
				{ QStringLiteral("textChannelId"), 7 }, { QStringLiteral("name"), QStringLiteral("stonks") } } } },
			{ QStringLiteral("leaderboardDescription"), QObject::tr("Portfolio returns over 30 days.") }
		};
		return {
			{ QStringLiteral("id"), QStringLiteral("stonks") }, { QStringLiteral("kind"), QStringLiteral("stonks") },
			{ QStringLiteral("title"), QObject::tr("Stonks") },
			{ QStringLiteral("subtitle"), QObject::tr("Portfolio, leaderboard, following, and server settings.") },
			{ QStringLiteral("actions"), QVariantList {
				visualAction(QStringLiteral("close"), QObject::tr("Close"), QStringLiteral("accent"), true) } },
			{ QStringLiteral("primaryActionId"), QStringLiteral("close") },
			{ QStringLiteral("initialFocusId"), QStringLiteral("stonksTab_overview") },
			{ QStringLiteral("tone"), QStringLiteral("wide") },
			{ QStringLiteral("preferredWidth"), 900 }, { QStringLiteral("preferredHeight"), 760 },
			{ QStringLiteral("stonks"), stonks }
		};
	}

	QVariantMap visualDialogForSurface(const QString &variant, QObject *serverAdminEditorController = nullptr) {
		if (variant.startsWith(QLatin1String("settings-"))) return visualSettingsDialog(variant);
		if (variant == QLatin1String("dialog-acl-populated")) return visualAclDialog();
		if (variant == QLatin1String("dialog-stonks-populated")) return visualStonksDialog();
		if (variant.startsWith(QLatin1String("dialog-connect"))) return visualConnectDialog(variant);
		if (variant.startsWith(QLatin1String("dialog-search"))) return visualSearchDialog(variant);
		if (variant.startsWith(QLatin1String("dialog-certificate"))) {
			Mumble::ModernProductDialogs::CertificateDialogInput input;
			input.certificate = { true, QStringLiteral("Demo User"), QStringLiteral("demo@example.com"),
				QStringLiteral("Mumble Demo CA"), QStringLiteral("2034-05-16T10:30:00+02:00"),
				QStringLiteral("7a:18:93:24:df:8a:61:4b:15:8d:7c:60:9f:42:aa:b1:43:5c:de:72"),
				QStringLiteral("2034-05-16") };
			if (variant == QLatin1String("dialog-certificate-create")) {
				input.fieldValues = { { QStringLiteral("cert.mode"), QStringLiteral("create") },
					{ QStringLiteral("cert.name"), QStringLiteral("Demo User") },
					{ QStringLiteral("cert.email"), QStringLiteral("demo@example.com") } };
			}
			return Mumble::ModernProductDialogs::certificateDialog(input);
		}
		if (variant.startsWith(QLatin1String("dialog-recorder"))) {
			return {
				{ QStringLiteral("id"), QStringLiteral("voiceRecorder") },
				{ QStringLiteral("kind"), QStringLiteral("recorder") },
				{ QStringLiteral("title"), QStringLiteral("Voice recorder") },
				{ QStringLiteral("subtitle"), QStringLiteral("Record the current voice session.") },
				{ QStringLiteral("sections"), QVariantList() },
				{ QStringLiteral("actions"), QVariantList {
					visualAction(QStringLiteral("close"), QStringLiteral("Close"), QString(), true) } },
				{ QStringLiteral("primaryActionId"), QStringLiteral("close") },
				{ QStringLiteral("initialFocusId"), variant == QLatin1String("dialog-recorder-recording")
					? QStringLiteral("recorderPauseButton") : QStringLiteral("recording.path") },
				{ QStringLiteral("preferredWidth"), 760 },
				{ QStringLiteral("preferredHeight"), 700 }
			};
		}
		if (variant.startsWith(QLatin1String("dialog-server-users"))
			|| variant.startsWith(QLatin1String("dialog-server-bans"))) {
			const bool users = variant.startsWith(QLatin1String("dialog-server-users"));
			QVariantMap editor {
				{ QStringLiteral("id"), users ? QStringLiteral("registeredUsers.admin")
										 : QStringLiteral("banList.admin") },
				{ QStringLiteral("type"), QStringLiteral("serverAdminEditor") },
				{ QStringLiteral("adminKind"), users ? QStringLiteral("users") : QStringLiteral("bans") }
			};
			if (serverAdminEditorController) {
				editor.insert(QStringLiteral("controller"), QVariant::fromValue(serverAdminEditorController));
			}
			return {
				{ QStringLiteral("id"), users ? QStringLiteral("serverUserList") : QStringLiteral("serverBanList") },
				{ QStringLiteral("kind"), QStringLiteral("serverAdmin") },
				{ QStringLiteral("title"), users ? QStringLiteral("Registered users") : QStringLiteral("Ban list") },
				{ QStringLiteral("subtitle"), users
					? QStringLiteral("Inspect, rename, or unregister server accounts.")
					: QStringLiteral("Inspect and manage active server bans.") },
				{ QStringLiteral("sections"), QVariantList { QVariantMap {
					{ QStringLiteral("title"), QString() },
					{ QStringLiteral("presentation"), QStringLiteral("form") },
					{ QStringLiteral("fields"), QVariantList { editor } }
				} } },
				{ QStringLiteral("actions"), QVariantList {
					visualAction(QStringLiteral("close"), QStringLiteral("Close"), QString(), true) } },
				{ QStringLiteral("primaryActionId"), QStringLiteral("close") },
				{ QStringLiteral("initialFocusId"), variant.endsWith(QLatin1String("-confirm"))
					? QStringLiteral("serverAdminConfirm") : QStringLiteral("serverAdminSearch") },
				{ QStringLiteral("preferredWidth"), 1040 },
				{ QStringLiteral("preferredHeight"), 760 }
			};
		}
		if (variant.startsWith(QLatin1String("screen-share-editor"))) {
			Mumble::ModernProductDialogs::ScreenShareEditorStateInput input;
			input.channelName = QStringLiteral("Lobby");
			input.channelId = QStringLiteral("1");
			input.selectedSourceId = variant == QLatin1String("screen-share-editor-compact")
				? QStringLiteral("window:4242") : QStringLiteral("monitor:0");
			input.sources = {
				QVariantMap { { QStringLiteral("id"), QStringLiteral("screens") },
					{ QStringLiteral("section"), QStringLiteral("Screens") },
					{ QStringLiteral("items"), QVariantList {
						QVariantMap { { QStringLiteral("id"), QStringLiteral("monitor:0") },
							{ QStringLiteral("title"), QStringLiteral("Screen 1") },
							{ QStringLiteral("detail"), QStringLiteral("DISPLAY1 - 2560x1440 - Primary display") },
							{ QStringLiteral("processId"), 0 }, { QStringLiteral("audioAuto"), false } } } } },
				QVariantMap { { QStringLiteral("id"), QStringLiteral("windows") },
					{ QStringLiteral("section"), QStringLiteral("Open windows") },
					{ QStringLiteral("loading"), false },
					{ QStringLiteral("emptyText"), QStringLiteral("No shareable app windows are open") },
					{ QStringLiteral("items"), QVariantList {
						QVariantMap { { QStringLiteral("id"), QStringLiteral("window:4242") },
							{ QStringLiteral("title"), QStringLiteral("Qt Quick Design Review") },
							{ QStringLiteral("detail"), QStringLiteral("Mumble") },
							{ QStringLiteral("processId"), 4242 }, { QStringLiteral("audioAuto"), true } } } } }
			};
			input.resolutionOptions = {
				visualOption(QStringLiteral("720p (1280x720)"), QStringLiteral("1280x720")),
				visualOption(QStringLiteral("1080p (1920x1080)"), QStringLiteral("1920x1080")),
				visualOption(QStringLiteral("1440p (2560x1440)"), QStringLiteral("2560x1440"))
			};
			input.resolutionDefault = QStringLiteral("1920x1080");
			input.frameRateOptions = { visualOption(QStringLiteral("30 FPS"), 30),
				visualOption(QStringLiteral("60 FPS"), 60) };
			input.frameRateDefault = 30;
			input.audioOptions = {
				visualOption(QStringLiteral("No audio"), QString()),
				visualOption(QStringLiteral("System audio (excluding Mumble)"), QStringLiteral("default-loopback")),
				visualOption(QStringLiteral("App: Qt Quick Design Review"), QStringLiteral("process:4242"))
			};
			input.audioDefault = variant == QLatin1String("screen-share-editor-compact")
				? QStringLiteral("process:4242") : QString();
			input.audioNote = QStringLiteral("System and output-device audio exclude this Mumble client to avoid voice feedback.");
			input.qualityNote = QStringLiteral("Server limit: 2560x1440@60");
			input.sourcesLoading = false;
			return Mumble::ModernProductDialogs::screenShareEditorDialog(input);
		}
		return {};
	}

	QString expectedRichPreviewAspect(const QString &variant) {
		if (QStringList { QStringLiteral("youtube"), QStringLiteral("vimeo"),
				QStringLiteral("dailymotion") }.contains(variant)) return QStringLiteral("wide");
		if (variant == QLatin1String("spotify")) return QStringLiteral("compact-audio");
		if (variant == QLatin1String("soundcloud")) return QStringLiteral("compact-audio");
		if (variant == QLatin1String("tiktok")) return QStringLiteral("short");
		if (variant == QLatin1String("instagram")) return QStringLiteral("square");
		if (variant == QLatin1String("twitch")) return QStringLiteral("wide");
		return {};
	}

	QString registerVisualPreviewImage(QmlShellHost *host, const QString &variant, const QString &title,
									 const QString &subtitle, const QColor &start, const QColor &end,
									 const QSize &size = QSize(960, 540)) {
		Q_UNUSED(title);
		Q_UNUSED(subtitle);
		if (!host || !host->imagePipeline() || !size.isValid()) return {};
		QImage image(size, QImage::Format_ARGB32_Premultiplied);
		image.fill(QColor(QStringLiteral("#05070a")));
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing, true);
		QLinearGradient gradient(QPointF(0, 0), QPointF(size.width(), size.height()));
		gradient.setColorAt(0.0, start);
		gradient.setColorAt(1.0, end);
		painter.fillRect(image.rect(), gradient);
		painter.setPen(QColor(255, 255, 255, 42));
		painter.setBrush(Qt::NoBrush);
		const qreal margin = qMax< qreal >(24.0, qMin(size.width(), size.height()) * 0.055);
		painter.drawRoundedRect(QRectF(margin, margin, size.width() - margin * 2,
									 size.height() - margin * 2), 24, 24);
		const qreal unit = qMin(size.width(), size.height());
		painter.setPen(Qt::NoPen);
		painter.setBrush(QColor(255, 255, 255, 24));
		painter.drawEllipse(QRectF(size.width() * 0.58, -unit * 0.16, unit * 0.72, unit * 0.72));
		painter.setBrush(QColor(255, 255, 255, 34));
		painter.drawRoundedRect(QRectF(margin * 1.7, size.height() * 0.66,
									 qMin(size.width() * 0.34, unit * 0.82), qMax< qreal >(8.0, unit * 0.035)),
							 8, 8);
		painter.setBrush(QColor(255, 255, 255, 18));
		painter.drawRoundedRect(QRectF(margin * 1.7, size.height() * 0.73,
									 qMin(size.width() * 0.22, unit * 0.58), qMax< qreal >(6.0, unit * 0.024)),
							 6, 6);
		painter.end();
		return host->imagePipeline()->registerImage(image, QStringLiteral("visual-preview:%1").arg(variant));
	}

	QVariantMap visualRichPreview(QmlShellHost *host, const QString &variant, const QString &size) {
		if (variant == QLatin1String("none")) return {};
		QVariantMap preview {
			{ QStringLiteral("kind"), QStringLiteral("link") },
			{ QStringLiteral("state"), QStringLiteral("ready") },
			{ QStringLiteral("loading"), false }, { QStringLiteral("failed"), false },
			{ QStringLiteral("previewSize"), size },
			{ QStringLiteral("presentationFamily"), presentationFamilyForCaseVariant(variant) },
			{ QStringLiteral("caseVariant"), variant }
		};
		const auto attachImage = [&preview](const QString &source, const QString &title) {
			if (source.isEmpty()) return;
			preview.insert(QStringLiteral("thumbnailUrl"), source);
			preview.insert(QStringLiteral("mediaItems"), QVariantList { QVariantMap {
				{ QStringLiteral("kind"), QStringLiteral("image") },
				{ QStringLiteral("mime"), QStringLiteral("image/png") },
				{ QStringLiteral("url"), source }, { QStringLiteral("title"), title }
			} });
		};

		if (variant == QLatin1String("loading") || variant == QLatin1String("error")) {
			const bool failed = variant == QLatin1String("error");
			preview.insert(QStringLiteral("url"), failed
				? QStringLiteral("https://example.com/preview-unavailable")
				: QStringLiteral("https://example.com/preview-loading"));
			preview.insert(QStringLiteral("title"), failed
				? QStringLiteral("Preview unavailable") : QStringLiteral("Fetching link preview"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("example.com"));
			preview.insert(QStringLiteral("description"), failed
				? QStringLiteral("The provider returned a deterministic error.") : QString());
			preview.insert(QStringLiteral("loadingLabel"), QStringLiteral("Fetching preview"));
			preview.insert(QStringLiteral("loading"), !failed);
			preview.insert(QStringLiteral("failed"), failed);
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open link"));
			return preview;
		}

		if (variant == QLatin1String("youtube")) {
			const QString title = QStringLiteral("Qt Quick media preview");
			preview.insert(QStringLiteral("url"), QStringLiteral("https://www.youtube.com/watch?v=fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("YouTube · Mumble Design"));
			preview.insert(QStringLiteral("description"), QStringLiteral("A media-first card with native playback actions."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on YouTube"));
			preview.insert(QStringLiteral("embedKind"), QStringLiteral("youtube"));
			preview.insert(QStringLiteral("embedUrl"), QStringLiteral("https://www.youtube.com/embed/fixture"));
			preview.insert(QStringLiteral("embedAspect"), QStringLiteral("wide"));
			attachImage(registerVisualPreviewImage(host, variant, title, QStringLiteral("Media-first 16:9 fixture"),
				QColor(QStringLiteral("#8b1538")), QColor(QStringLiteral("#243b55"))), title);
			return preview;
		}
		if (variant == QLatin1String("spotify")) {
			const QString title = QStringLiteral("Native client mix");
			preview.insert(QStringLiteral("url"), QStringLiteral("https://open.spotify.com/track/fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Spotify · Mumble Sessions"));
			preview.insert(QStringLiteral("description"), QStringLiteral("Provider-owned audio controls stay inside the embed."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Spotify"));
			preview.insert(QStringLiteral("embedKind"), QStringLiteral("spotify"));
			preview.insert(QStringLiteral("embedUrl"), QStringLiteral("https://open.spotify.com/embed/track/fixture123"));
			preview.insert(QStringLiteral("embedAspect"), QStringLiteral("compact-audio"));
			attachImage(registerVisualPreviewImage(host, variant, title, QStringLiteral("Audio provider fixture"),
				QColor(QStringLiteral("#12633c")), QColor(QStringLiteral("#10151c")), QSize(720, 720)), title);
			return preview;
		}
		if (variant == QLatin1String("tiktok")) {
			const QString title = QStringLiteral("Vertical creator preview");
			preview.insert(QStringLiteral("url"), QStringLiteral("https://www.tiktok.com/@mumble/video/fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("TikTok · @mumble"));
			preview.insert(QStringLiteral("description"), QStringLiteral("A centered 9:16 provider surface."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on TikTok"));
			preview.insert(QStringLiteral("embedKind"), QStringLiteral("tiktok"));
			preview.insert(QStringLiteral("embedUrl"), QStringLiteral("https://www.tiktok.com/player/v1/fixture"));
			preview.insert(QStringLiteral("embedAspect"), QStringLiteral("short"));
			attachImage(registerVisualPreviewImage(host, variant, title, QStringLiteral("Vertical 9:16 fixture"),
				QColor(QStringLiteral("#18181b")), QColor(QStringLiteral("#a11c59")), QSize(720, 1280)), title);
			return preview;
		}
		if (variant == QLatin1String("vimeo") || variant == QLatin1String("dailymotion")) {
			const bool isVimeo = variant == QLatin1String("vimeo");
			const QString provider = isVimeo ? QStringLiteral("Vimeo") : QStringLiteral("Dailymotion");
			const QString title = isVimeo ? QStringLiteral("Qt Quick animation showcase")
				: QStringLiteral("Native desktop performance story");
			preview.insert(QStringLiteral("url"), isVimeo
				? QStringLiteral("https://vimeo.com/76979871")
				: QStringLiteral("https://www.dailymotion.com/video/x84sh87"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("%1 · Mumble Design").arg(provider));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("A lazy provider player with native card chrome and deterministic poster art."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on %1").arg(provider));
			preview.insert(QStringLiteral("embedKind"), variant);
			preview.insert(QStringLiteral("embedUrl"), isVimeo
				? QStringLiteral("https://player.vimeo.com/video/76979871")
				: QStringLiteral("https://geo.dailymotion.com/player.html?video=x84sh87"));
			preview.insert(QStringLiteral("embedAspect"), QStringLiteral("wide"));
			attachImage(registerVisualPreviewImage(host, variant, title,
				QStringLiteral("Wide provider video fixture"),
				isVimeo ? QColor(QStringLiteral("#1264a3")) : QColor(QStringLiteral("#112c58")),
				isVimeo ? QColor(QStringLiteral("#5ec8f2")) : QColor(QStringLiteral("#00aaff"))), title);
			return preview;
		}
		if (variant == QLatin1String("soundcloud")) {
			const QString title = QStringLiteral("Qt Quick focus mix");
			preview.insert(QStringLiteral("url"), QStringLiteral("https://soundcloud.com/mumble-design/focus-mix"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("SoundCloud · Mumble Sessions"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("A compact audio provider surface that keeps artwork and controls aligned."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on SoundCloud"));
			preview.insert(QStringLiteral("embedKind"), QStringLiteral("soundcloud"));
			preview.insert(QStringLiteral("embedUrl"),
				QStringLiteral("https://w.soundcloud.com/player/?url=https%3A%2F%2Fsoundcloud.com%2Fmumble-design%2Ffocus-mix"));
			preview.insert(QStringLiteral("embedAspect"), QStringLiteral("compact-audio"));
			attachImage(registerVisualPreviewImage(host, variant, title,
				QStringLiteral("Compact audio provider fixture"), QColor(QStringLiteral("#ff5500")),
				QColor(QStringLiteral("#652500")), QSize(720, 720)), title);
			return preview;
		}
		if (variant == QLatin1String("instagram")) {
			const QString title = QStringLiteral("Post by @mumblequick");
			const QString image = registerVisualPreviewImage(host, variant, title, QStringLiteral("Square social fixture"),
				QColor(QStringLiteral("#5b247a")), QColor(QStringLiteral("#d17c45")), QSize(900, 900));
			preview.insert(QStringLiteral("url"), QStringLiteral("https://www.instagram.com/p/fixture/"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Instagram"));
			preview.insert(QStringLiteral("description"), QStringLiteral("Identity, caption, media, and actions share one hierarchy."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Instagram"));
			preview.insert(QStringLiteral("embedKind"), QStringLiteral("instagram"));
			preview.insert(QStringLiteral("embedUrl"), QStringLiteral("https://www.instagram.com/p/fixture/embed"));
			preview.insert(QStringLiteral("embedAspect"), QStringLiteral("square"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("instagram") },
				{ QStringLiteral("instagramDisplayName"), QStringLiteral("Mumble Quick") },
				{ QStringLiteral("instagramHandle"), QStringLiteral("mumblequick") },
				{ QStringLiteral("instagramCaption"), QStringLiteral("Native previews, consistent in every theme.") },
				{ QStringLiteral("instagramMediaKind"), QStringLiteral("post") },
				{ QStringLiteral("instagramLikeCount"), 2480 },
				{ QStringLiteral("instagramCommentCount"), 86 },
				{ QStringLiteral("instagramAvatarUrl"), image }
			});
			attachImage(image, title);
			return preview;
		}
		if (variant == QLatin1String("finance")) {
			preview.insert(QStringLiteral("url"), QStringLiteral("https://finance.yahoo.com/quote/MSFT"));
			preview.insert(QStringLiteral("title"), QStringLiteral("Microsoft Corporation (MSFT)"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Yahoo Finance"));
			preview.insert(QStringLiteral("description"), QStringLiteral("448.37 USD · +5.21 (+1.18%)"));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Yahoo Finance"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("yahoo-finance") },
				{ QStringLiteral("previewProvider"), QStringLiteral("yahoo-finance") },
				{ QStringLiteral("providerName"), QStringLiteral("Yahoo Finance") },
				{ QStringLiteral("tickerSymbol"), QStringLiteral("MSFT") },
				{ QStringLiteral("financeName"), QStringLiteral("Microsoft Corporation") },
				{ QStringLiteral("financePrice"), QStringLiteral("448.37") },
				{ QStringLiteral("financeCurrency"), QStringLiteral("USD") },
				{ QStringLiteral("financeDayChange"), QStringLiteral("+5.21") },
				{ QStringLiteral("financeDayChangePercent"), QStringLiteral("+1.18%") },
				{ QStringLiteral("financeDayTrend"), QStringLiteral("up") },
				{ QStringLiteral("financeExchange"), QStringLiteral("NasdaqGS") },
				{ QStringLiteral("financeInstrument"), QStringLiteral("EQUITY") },
				{ QStringLiteral("financeRangeLabel"), QStringLiteral("1M") },
				{ QStringLiteral("financeRangeChangePercent"), QStringLiteral("+2.85%") },
				{ QStringLiteral("financeRangeTrend"), QStringLiteral("up") },
				{ QStringLiteral("financeSparkline"), QVariantList {
					QVariantMap { { QStringLiteral("timestamp"), 1 }, { QStringLiteral("close"), 435.95 } },
					QVariantMap { { QStringLiteral("timestamp"), 2 }, { QStringLiteral("close"), 441.10 } },
					QVariantMap { { QStringLiteral("timestamp"), 3 }, { QStringLiteral("close"), 438.72 } },
					QVariantMap { { QStringLiteral("timestamp"), 4 }, { QStringLiteral("close"), 448.37 } }
				} }
			});
			return preview;
		}
		if (variant == QLatin1String("audio")) {
			const QString title = QStringLiteral("Vetenskapsradion");
			const QString artwork = registerVisualPreviewImage(host, variant, title,
				QStringLiteral("Sveriges Radio audio fixture"), QColor(QStringLiteral("#e65100")),
				QColor(QStringLiteral("#ffb74d")), QSize(720, 720));
			preview.insert(QStringLiteral("url"), QStringLiteral("https://sverigesradio.se/avsnitt/fixture"));
			preview.insert(QStringLiteral("title"), QStringLiteral("Så blir framtidens datacenter mer energieffektiva"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Vetenskapsradion · Sveriges Radio"));
			preview.insert(QStringLiteral("description"), QStringLiteral("A native audio card with clear programme hierarchy."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Sveriges Radio"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("sveriges-radio") },
				{ QStringLiteral("previewKind"), QStringLiteral("audio") },
				{ QStringLiteral("providerName"), QStringLiteral("Sveriges Radio") },
				{ QStringLiteral("audioProvider"), QStringLiteral("Sveriges Radio") },
				{ QStringLiteral("audioProgram"), QStringLiteral("Vetenskapsradion") },
				{ QStringLiteral("articlePublishedAt"), QStringLiteral("28 May 2026") }
			});
			attachImage(artwork, title);
			return preview;
		}
		if (variant == QLatin1String("steam")) {
			const QString title = QStringLiteral("Hades II");
			const QString hero = registerVisualPreviewImage(host, variant, title,
				QStringLiteral("Steam store fixture"), QColor(QStringLiteral("#123c69")),
				QColor(QStringLiteral("#66c0f4")));
			preview.insert(QStringLiteral("url"), QStringLiteral("https://store.steampowered.com/app/fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Steam"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("A native store card with price, reviews, platforms, and release details."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Steam"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("previewProvider"), QStringLiteral("game-store") },
				{ QStringLiteral("previewKind"), QStringLiteral("gameStoreProduct") },
				{ QStringLiteral("gameStoreProvider"), QStringLiteral("steam") },
				{ QStringLiteral("gameStoreName"), QStringLiteral("Steam") },
				{ QStringLiteral("gameStoreDescription"),
					QStringLiteral("Battle beyond the Underworld in this deterministic visual fixture.") },
				{ QStringLiteral("steamPrice"), QStringLiteral("29,99 €") },
				{ QStringLiteral("steamOriginalPrice"), QStringLiteral("39,99 €") },
				{ QStringLiteral("steamDiscountPercent"), 25 },
				{ QStringLiteral("steamPlatforms"), QStringLiteral("Windows") },
				{ QStringLiteral("steamReviewSummary"), QStringLiteral("Very Positive") },
				{ QStringLiteral("steamReviewPercent"), 92 },
				{ QStringLiteral("steamReviewTotal"), 58420 },
				{ QStringLiteral("steamDeveloper"), QStringLiteral("Supergiant Games") },
				{ QStringLiteral("steamReleaseDate"), QStringLiteral("6 May 2024") },
				{ QStringLiteral("steamMetacriticScore"), 86 },
				{ QStringLiteral("gameStoreTags"),
					QVariantList { QStringLiteral("Action roguelike"), QStringLiteral("Mythology") } }
			});
			attachImage(hero, title);
			return preview;
		}
		if (variant == QLatin1String("google")) {
			preview.insert(QStringLiteral("url"),
				QStringLiteral("https://www.google.com/search?q=Qt+Quick+model+performance"));
			preview.insert(QStringLiteral("title"), QStringLiteral("Qt Quick model performance"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Google Search"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("Search results open externally without loading remote content in the fixture."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open Google Search"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("google-search") },
				{ QStringLiteral("previewKind"), QStringLiteral("googleSearch") },
				{ QStringLiteral("providerName"), QStringLiteral("Google") },
				{ QStringLiteral("googleSearchQuery"), QStringLiteral("Qt Quick model performance") },
				{ QStringLiteral("googleSearchMode"), QStringLiteral("web") },
				{ QStringLiteral("googleSearchModeLabel"), QStringLiteral("Google Search") }
			});
			return preview;
		}
		if (variant == QLatin1String("twitch")) {
			const QString title = QStringLiteral("Mumble Dev is live");
			preview.insert(QStringLiteral("url"), QStringLiteral("https://www.twitch.tv/mumbledev"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Twitch · mumbledev"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("A live provider card with native identity and playback affordances."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Twitch"));
			preview.insert(QStringLiteral("embedKind"), QStringLiteral("twitch"));
			preview.insert(QStringLiteral("embedUrl"),
				QStringLiteral("https://player.twitch.tv/?channel=mumbledev&parent=localhost"));
			preview.insert(QStringLiteral("embedAspect"), QStringLiteral("wide"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("twitch") },
				{ QStringLiteral("previewKind"), QStringLiteral("twitch") },
				{ QStringLiteral("providerName"), QStringLiteral("Twitch") },
				{ QStringLiteral("twitchDisplayName"), QStringLiteral("Mumble Dev") },
				{ QStringLiteral("twitchChannel"), QStringLiteral("mumbledev") },
				{ QStringLiteral("twitchLiveState"), QStringLiteral("live") },
				{ QStringLiteral("twitchBadge"), QStringLiteral("Live") },
				{ QStringLiteral("twitchGame"), QStringLiteral("Software and Game Development") },
				{ QStringLiteral("twitchViewerCount"), 12500 },
				{ QStringLiteral("twitchEmbedMode"), QStringLiteral("Live player") },
				{ QStringLiteral("twitchPlaybackNote"),
					QStringLiteral("Provider playback starts only after explicit interaction.") }
			});
			attachImage(registerVisualPreviewImage(host, variant, title,
				QStringLiteral("Twitch live fixture"), QColor(QStringLiteral("#3d1f70")),
				QColor(QStringLiteral("#a970ff"))), title);
			return preview;
		}
		if (variant == QLatin1String("flashback")) {
			preview.insert(QStringLiteral("url"),
				QStringLiteral("https://www.flashback.org/tfixturep42"));
			preview.insert(QStringLiteral("title"), QStringLiteral("Qt Quick render loops and fluency"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Flashback · Programmering"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("Thread context, linked post, and quoted reply stay in one native card."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Flashback"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("flashback") },
				{ QStringLiteral("previewKind"), QStringLiteral("forum") },
				{ QStringLiteral("providerName"), QStringLiteral("Flashback") },
				{ QStringLiteral("forumProvider"), QStringLiteral("Flashback") },
				{ QStringLiteral("forumThreadId"), QStringLiteral("fixture") },
				{ QStringLiteral("forumThreadTitle"), QStringLiteral("Qt Quick render loops and fluency") },
				{ QStringLiteral("forumCategory"), QStringLiteral("Dator") },
				{ QStringLiteral("forumName"), QStringLiteral("Programmering") },
				{ QStringLiteral("forumPage"), QStringLiteral("42") },
				{ QStringLiteral("forumPageCount"), QStringLiteral("73") },
				{ QStringLiteral("forumPostNumber"), QStringLiteral("#628") },
				{ QStringLiteral("forumPostAuthor"), QStringLiteral("rendernisse") },
				{ QStringLiteral("forumPostTime"), QStringLiteral("Today 12:00") },
				{ QStringLiteral("forumPostExcerpt"),
					QStringLiteral("Frame pacing stays smooth when model updates remain incremental.") },
				{ QStringLiteral("forumQuoteAuthor"), QStringLiteral("qmlvän") },
				{ QStringLiteral("forumQuotePostNumber"), QStringLiteral("#627") },
				{ QStringLiteral("forumQuoteExcerpt"),
					QStringLiteral("Keep every provider card in the same visual system.") },
				{ QStringLiteral("forumPostCount"), 628 }
			});
			return preview;
		}
		if (variant == QLatin1String("x")) {
			preview.insert(QStringLiteral("url"), QStringLiteral("https://x.com/mumbledesign/status/fixture"));
			preview.insert(QStringLiteral("title"), QStringLiteral("Frame pacing stays native"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("X · @mumbledesign"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("Typed models keep every interaction responsive without a browser bridge."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on X"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("x") },
				{ QStringLiteral("previewKind"), QStringLiteral("x") },
				{ QStringLiteral("providerName"), QStringLiteral("X") },
				{ QStringLiteral("xDisplayName"), QStringLiteral("Mumble Design") },
				{ QStringLiteral("xHandle"), QStringLiteral("@mumbledesign") },
				{ QStringLiteral("xVerified"), true },
				{ QStringLiteral("xCreatedAt"), QStringLiteral("Today 10:26") },
				{ QStringLiteral("xReplyCount"), 18 }, { QStringLiteral("xRepostCount"), 74 },
				{ QStringLiteral("xLikeCount"), 624 }, { QStringLiteral("xViewCount"), 18200 }
			});
			return preview;
		}
		if (variant == QLatin1String("github")) {
			preview.insert(QStringLiteral("url"), QStringLiteral("https://github.com/mumble-voip/mumble"));
			preview.insert(QStringLiteral("title"), QStringLiteral("mumble-voip/mumble"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("GitHub · Public repository"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("Low latency, high quality voice chat with a native Qt Quick client."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on GitHub"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("github") },
				{ QStringLiteral("previewKind"), QStringLiteral("github") },
				{ QStringLiteral("providerName"), QStringLiteral("GitHub") },
				{ QStringLiteral("githubOwner"), QStringLiteral("mumble-voip") },
				{ QStringLiteral("githubRepo"), QStringLiteral("mumble") },
				{ QStringLiteral("githubFullName"), QStringLiteral("mumble-voip/mumble") },
				{ QStringLiteral("githubLanguage"), QStringLiteral("C++") },
				{ QStringLiteral("githubLicense"), QStringLiteral("BSD-3-Clause") },
				{ QStringLiteral("githubStars"), 7200 }, { QStringLiteral("githubForks"), 1180 },
				{ QStringLiteral("githubOpenIssues"), 540 },
				{ QStringLiteral("githubTopics"), QVariantList { QStringLiteral("voice-chat"),
					QStringLiteral("qt-quick"), QStringLiteral("low-latency") } }
			});
			return preview;
		}
		if (variant == QLatin1String("social")) {
			preview.insert(QStringLiteral("url"), QStringLiteral("https://social.example/@mumble/fixture"));
			preview.insert(QStringLiteral("title"), QStringLiteral("Mumble community update"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Social · @mumble"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("A deterministic generic social card without provider-specific chrome."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open post"));
			return preview;
		}
		if (variant == QLatin1String("vehicle")) {
			const QString title = QStringLiteral("Volvo EX30 Twin Motor Performance");
			const QString image = registerVisualPreviewImage(host, variant, title,
				QStringLiteral("Vehicle listing fixture"), QColor(QStringLiteral("#17324d")),
				QColor(QStringLiteral("#5f8da8")));
			preview.insert(QStringLiteral("url"), QStringLiteral("https://www.bytbil.com/fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Bytbil · Stockholm"));
			preview.insert(QStringLiteral("description"), QStringLiteral("2025 · Electric · Automatic · 1 200 mil"));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Bytbil"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("bytbil") },
				{ QStringLiteral("previewKind"), QStringLiteral("vehicleListing") },
				{ QStringLiteral("providerName"), QStringLiteral("Bytbil") },
				{ QStringLiteral("vehiclePrice"), QStringLiteral("429 900 kr") },
				{ QStringLiteral("vehicleKind"), QStringLiteral("SUV") },
				{ QStringLiteral("vehicleYear"), QStringLiteral("2025") },
				{ QStringLiteral("vehicleMileage"), QStringLiteral("1 200 mil") },
				{ QStringLiteral("vehicleFuel"), QStringLiteral("Electric") },
				{ QStringLiteral("vehicleTransmission"), QStringLiteral("Automatic") },
				{ QStringLiteral("vehicleDealer"), QStringLiteral("Mumble Motors") },
				{ QStringLiteral("vehicleLocation"), QStringLiteral("Stockholm") },
				{ QStringLiteral("vehicleImage"), image }
			});
			attachImage(image, title);
			return preview;
		}
		if (variant == QLatin1String("property")) {
			const QString title = QStringLiteral("Ljus trea nära vattnet");
			const QString image = registerVisualPreviewImage(host, variant, title,
				QStringLiteral("Property listing fixture"), QColor(QStringLiteral("#355c4d")),
				QColor(QStringLiteral("#9cc8a7")));
			preview.insert(QStringLiteral("url"), QStringLiteral("https://www.hemnet.se/bostad/fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Hemnet · Stockholm"));
			preview.insert(QStringLiteral("description"), QStringLiteral("74 m² · 3 rooms · Balcony"));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Hemnet"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("hemnet") },
				{ QStringLiteral("previewKind"), QStringLiteral("realEstate") },
				{ QStringLiteral("providerName"), QStringLiteral("Hemnet") },
				{ QStringLiteral("realEstatePrice"), QStringLiteral("5 495 000 kr") },
				{ QStringLiteral("realEstateArea"), QStringLiteral("74 m²") },
				{ QStringLiteral("realEstateRooms"), QStringLiteral("3 rooms") },
				{ QStringLiteral("realEstateFee"), QStringLiteral("4 125 kr/month") }
			});
			attachImage(image, title);
			return preview;
		}
		if (variant == QLatin1String("article")) {
			preview.insert(QStringLiteral("url"), QStringLiteral("https://www.svt.se/nyheter/fixture"));
			preview.insert(QStringLiteral("title"), QStringLiteral("Qt Quick makes desktop chat feel immediate"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("SVT Nyheter · Technology"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("Native models and bounded delegates improve startup, scrolling, and input latency."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Read on SVT"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("svt") },
				{ QStringLiteral("previewKind"), QStringLiteral("article") },
				{ QStringLiteral("providerName"), QStringLiteral("SVT") },
				{ QStringLiteral("articlePublisher"), QStringLiteral("SVT") },
				{ QStringLiteral("articleSection"), QStringLiteral("Technology") },
				{ QStringLiteral("articleAuthor"), QStringLiteral("Mumble Visual Fixture") },
				{ QStringLiteral("articlePublishedAt"), QStringLiteral("15 July 2026 · 10:30") }
			});
			return preview;
		}
		if (variant == QLatin1String("marketplace")) {
			const QString title = QStringLiteral("Herman Miller Aeron · graphite");
			const QString image = registerVisualPreviewImage(host, variant, title,
				QStringLiteral("Marketplace listing fixture"), QColor(QStringLiteral("#8b3d2f")),
				QColor(QStringLiteral("#e2a15e")));
			preview.insert(QStringLiteral("url"), QStringLiteral("https://www.blocket.se/annons/fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Blocket · Stockholm"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("A structured marketplace card with price, condition, location, and sale state."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Blocket"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("blocket") },
				{ QStringLiteral("previewKind"), QStringLiteral("marketplaceListing") },
				{ QStringLiteral("providerName"), QStringLiteral("Blocket") },
				{ QStringLiteral("marketplaceProvider"), QStringLiteral("Blocket") },
				{ QStringLiteral("listingPrice"), QStringLiteral("8 500 kr") },
				{ QStringLiteral("listingOriginalPrice"), QStringLiteral("10 000 kr") },
				{ QStringLiteral("listingCondition"), QStringLiteral("Very good condition") },
				{ QStringLiteral("listingLocation"), QStringLiteral("Södermalm, Stockholm") },
				{ QStringLiteral("listingSaleType"), QStringLiteral("Buy now") },
				{ QStringLiteral("listingEndsAt"), QStringLiteral("Tomorrow 18:00") },
				{ QStringLiteral("listingId"), QStringLiteral("fixture-2048") },
				{ QStringLiteral("listingSpecs"), QVariantList {
					QVariantMap { { QStringLiteral("label"), QStringLiteral("Size") },
						{ QStringLiteral("value"), QStringLiteral("B") } },
					QVariantMap { { QStringLiteral("label"), QStringLiteral("Pickup") },
						{ QStringLiteral("value"), QStringLiteral("Stockholm") } }
				} }
			});
			attachImage(image, title);
			return preview;
		}
		if (variant == QLatin1String("weather") || variant == QLatin1String("place")
			|| variant == QLatin1String("traffic")) {
			const bool isWeather = variant == QLatin1String("weather");
			const bool isPlace = variant == QLatin1String("place");
			const QString provider = isWeather ? QStringLiteral("SMHI")
				: isPlace ? QStringLiteral("OpenStreetMap") : QStringLiteral("Trafikverket");
			const QString title = isWeather ? QStringLiteral("Stockholm weather")
				: isPlace ? QStringLiteral("Mumble Café") : QStringLiteral("Traffic on E4 Stockholm");
			const QString location = isWeather ? QStringLiteral("Stockholm, Sweden")
				: isPlace ? QStringLiteral("Södermalm, Stockholm") : QStringLiteral("E4 · Solna to Norrtull");
			const QString status = isWeather ? QStringLiteral("8 °C · Partly cloudy · 4 m/s")
				: isPlace ? QStringLiteral("Café · Open until 18:00")
					: QStringLiteral("Slow traffic · 12 minute delay");
			preview.insert(QStringLiteral("url"), isWeather
				? QStringLiteral("https://www.smhi.se/vader/prognoser/stockholm")
				: isPlace ? QStringLiteral("https://www.openstreetmap.org/node/fixture")
					: QStringLiteral("https://www.trafikverket.se/trafikinformation/fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), provider);
			preview.insert(QStringLiteral("description"), status);
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open %1 details").arg(variant));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), provider.toLower() },
				{ QStringLiteral("previewKind"), variant },
				{ QStringLiteral("providerName"), provider },
				{ QStringLiteral("locationLabel"), location },
				{ QStringLiteral("statusLabel"), status }
			});
			return preview;
		}
		if (variant == QLatin1String("link-digest")) {
			preview.insert(QStringLiteral("url"), QStringLiteral("https://existenz.se/out?fixture"));
			preview.insert(QStringLiteral("title"), QStringLiteral("Today’s design and performance links"));
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Existenz · Link digest"));
			preview.insert(QStringLiteral("description"),
				QStringLiteral("A concise editorial summary for a curated collection of external links."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open link digest"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("provider"), QStringLiteral("existenz") },
				{ QStringLiteral("previewKind"), QStringLiteral("linkDigest") },
				{ QStringLiteral("providerName"), QStringLiteral("Existenz") },
				{ QStringLiteral("linkDigestTitle"), QStringLiteral("Today’s design and performance links") },
				{ QStringLiteral("linkDigestSource"), QStringLiteral("Existenz") },
				{ QStringLiteral("linkDigestCaption"),
					QStringLiteral("Qt Quick rendering, input latency, and native desktop interaction patterns.") }
			});
			return preview;
		}
		if (variant == QLatin1String("sensitive")) {
			const QString title = QStringLiteral("Sensitive media preview");
			const QString image = registerVisualPreviewImage(host, variant, title,
				QStringLiteral("Blurred until explicitly revealed"), QColor(QStringLiteral("#40244b")),
				QColor(QStringLiteral("#a45875")));
			preview.insert(QStringLiteral("url"), QStringLiteral("https://example.com/sensitive-fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Content notice"));
			preview.insert(QStringLiteral("description"), QStringLiteral("Media remains hidden until you choose to reveal it."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open externally"));
			preview.insert(QStringLiteral("metadata"), QVariantMap {
				{ QStringLiteral("contentWarning"), QStringLiteral("Sensitive media") },
				{ QStringLiteral("thumbnailBlur"), true }
			});
			attachImage(image, title);
			return preview;
		}
		if (variant == QLatin1String("direct-media")) {
			const QString title = QStringLiteral("Native direct-media clip");
			const QString poster = registerVisualPreviewImage(host, variant, title,
				QStringLiteral("Direct media fixture"), QColor(QStringLiteral("#0b3954")),
				QColor(QStringLiteral("#087e8b")));
			const QString video = QStringLiteral("data:video/mp4;base64,AAAA");
			const QString audio = QStringLiteral("data:audio/mp4;base64,AAAA");
			preview.insert(QStringLiteral("url"), QStringLiteral("https://media.example.com/fixture"));
			preview.insert(QStringLiteral("title"), title);
			preview.insert(QStringLiteral("subtitle"), QStringLiteral("Direct media · 00:24"));
			preview.insert(QStringLiteral("description"), QStringLiteral("Playback remains lazy until explicit interaction."));
			preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open media source"));
			preview.insert(QStringLiteral("thumbnailUrl"), poster);
			preview.insert(QStringLiteral("mediaKind"), QStringLiteral("video"));
			preview.insert(QStringLiteral("mediaMime"), QStringLiteral("video/mp4"));
			preview.insert(QStringLiteral("mediaUrl"), video);
			preview.insert(QStringLiteral("mediaAudioMime"), QStringLiteral("audio/mp4"));
			preview.insert(QStringLiteral("mediaAudioUrl"), audio);
			preview.insert(QStringLiteral("mediaItems"), QVariantList { QVariantMap {
				{ QStringLiteral("kind"), QStringLiteral("video") },
				{ QStringLiteral("mime"), QStringLiteral("video/mp4") },
				{ QStringLiteral("url"), video }, { QStringLiteral("poster"), poster },
				{ QStringLiteral("title"), title }
			} });
			return preview;
		}

		const QString title = QStringLiteral("Logitech G Pro X Superlight 2");
		const QString product = registerVisualPreviewImage(host, variant, title, QStringLiteral("Product fixture"),
			QColor(QStringLiteral("#086f83")), QColor(QStringLiteral("#49c5b6")));
		preview.insert(QStringLiteral("url"), QStringLiteral("https://www.inet.se/produkt/fixture"));
		preview.insert(QStringLiteral("title"), title);
		preview.insert(QStringLiteral("subtitle"), QStringLiteral("Inet"));
		preview.insert(QStringLiteral("description"), QStringLiteral("Wireless mouse · 60 g · LIGHTSPEED"));
		preview.insert(QStringLiteral("openLabel"), QStringLiteral("Open on Inet"));
		preview.insert(QStringLiteral("metadata"), QVariantMap {
			{ QStringLiteral("provider"), QStringLiteral("inet") },
			{ QStringLiteral("previewProvider"), QStringLiteral("inet") },
			{ QStringLiteral("previewKind"), QStringLiteral("product") },
			{ QStringLiteral("providerName"), QStringLiteral("Inet") },
			{ QStringLiteral("productTitle"), title },
			{ QStringLiteral("productPrice"), QStringLiteral("1 499 kr") },
			{ QStringLiteral("productAvailability"), QStringLiteral("In stock online") },
			{ QStringLiteral("productRating"), QStringLiteral("4.7/5 · 128 reviews") },
			{ QStringLiteral("productSku"), QStringLiteral("910-006630") },
			{ QStringLiteral("productImage"), product }
		});
		attachImage(product, title);
		return preview;
	}

	QVariantMap visualChatFixtureState(QmlShellHost *host, const QString &surfaceVariant,
									 const QVariantMap &prependState = {}) {
		QVariantMap state {
			{ QStringLiteral("variant"), surfaceVariant },
			{ QStringLiteral("message_count"), 0 },
			{ QStringLiteral("reply_message_count"), 0 },
			{ QStringLiteral("reaction_message_count"), 0 },
			{ QStringLiteral("sending_message_count"), 0 },
			{ QStringLiteral("failed_retry_message_count"), 0 },
			{ QStringLiteral("deleted_message_count"), 0 },
			{ QStringLiteral("ready_attachment_count"), 0 },
			{ QStringLiteral("loading_attachment_count"), 0 },
			{ QStringLiteral("error_attachment_count"), 0 },
			{ QStringLiteral("composer_text"), QString() },
			{ QStringLiteral("composer_has_pending_reply"), false },
			{ QStringLiteral("composer_reply_actor"), QString() },
			{ QStringLiteral("composer_reply_snippet"), QString() },
			{ QStringLiteral("composer_attachment_count"), 0 },
			{ QStringLiteral("composer_uploading_count"), 0 },
			{ QStringLiteral("composer_failed_count"), 0 },
			{ QStringLiteral("composer_autocomplete_count"), 0 },
			{ QStringLiteral("composer_upload_progress_percent"), 0 },
			{ QStringLiteral("prepend_count"), 0 },
			{ QStringLiteral("anchor_id"), QString() },
			{ QStringLiteral("anchor_before_row"), -1 },
			{ QStringLiteral("anchor_after_row"), -1 },
			{ QStringLiteral("anchor_before_offset"), 0.0 },
			{ QStringLiteral("anchor_after_offset"), 0.0 },
			{ QStringLiteral("anchor_offset_delta"), 0.0 },
			{ QStringLiteral("anchor_preserved"), false },
			{ QStringLiteral("pure_prepend_applied"), false }
		};
		if (!host || !isChatSurfaceVariant(surfaceVariant)) return state;

		ChatTimelineModel *chat = host->chatModel();
		state.insert(QStringLiteral("message_count"), chat->rowCount());
		int replyMessages = 0;
		int reactionMessages = 0;
		int sendingMessages = 0;
		int failedRetryMessages = 0;
		int deletedMessages = 0;
		int readyAttachments = 0;
		int loadingAttachments = 0;
		int errorAttachments = 0;
		for (int rowIndex = 0; rowIndex < chat->rowCount(); ++rowIndex) {
			const QVariantMap row = chat->get(rowIndex);
			const QVariantMap source = row.value(QStringLiteral("source")).toMap();
			if (!row.value(QStringLiteral("replyActor")).toString().isEmpty()
				|| !row.value(QStringLiteral("replySnippet")).toString().isEmpty()) {
				++replyMessages;
			}
			if (!row.value(QStringLiteral("reactions")).toList().isEmpty()) ++reactionMessages;
			const QString deliveryState = source.value(QStringLiteral("deliveryState"),
				row.value(QStringLiteral("status"))).toString().trimmed().toLower();
			if (deliveryState == QLatin1String("sending")) ++sendingMessages;
			if (deliveryState == QLatin1String("failed")
				&& source.value(QStringLiteral("deliveryCanRetry")).toBool()) {
				++failedRetryMessages;
			}
			if (row.value(QStringLiteral("deleted")).toBool()) ++deletedMessages;
			for (const QVariant &entry : row.value(QStringLiteral("attachments")).toList()) {
				const QString attachmentState = entry.toMap().value(
					QStringLiteral("state"), QStringLiteral("ready")).toString().trimmed().toLower();
				if (attachmentState == QLatin1String("loading")) {
					++loadingAttachments;
				} else if (attachmentState == QLatin1String("error")) {
					++errorAttachments;
				} else {
					++readyAttachments;
				}
			}
		}
		state.insert(QStringLiteral("reply_message_count"), replyMessages);
		state.insert(QStringLiteral("reaction_message_count"), reactionMessages);
		state.insert(QStringLiteral("sending_message_count"), sendingMessages);
		state.insert(QStringLiteral("failed_retry_message_count"), failedRetryMessages);
		state.insert(QStringLiteral("deleted_message_count"), deletedMessages);
		state.insert(QStringLiteral("ready_attachment_count"), readyAttachments);
		state.insert(QStringLiteral("loading_attachment_count"), loadingAttachments);
		state.insert(QStringLiteral("error_attachment_count"), errorAttachments);

		ComposerController *composer = host->composerController();
		state.insert(QStringLiteral("composer_text"), composer->text());
		state.insert(QStringLiteral("composer_has_pending_reply"),
			host->activeScopeController()->hasPendingReply());
		state.insert(QStringLiteral("composer_reply_actor"), host->activeScopeController()->replyActor());
		state.insert(QStringLiteral("composer_reply_snippet"), host->activeScopeController()->replySnippet());
		state.insert(QStringLiteral("composer_attachment_count"), composer->attachments()->rowCount());
		state.insert(QStringLiteral("composer_autocomplete_count"), composer->autocompleteItems().size());
		int uploadingDrafts = 0;
		int failedDrafts = 0;
		int maximumUploadPercent = 0;
		for (const DraftAttachmentModel::Item &item : composer->attachments()->items()) {
			if (item.status == QLatin1String("uploading")) {
				++uploadingDrafts;
				maximumUploadPercent = qMax(maximumUploadPercent, qRound(item.progress * 100.0));
			} else if (item.status == QLatin1String("failed")) {
				++failedDrafts;
			}
		}
		state.insert(QStringLiteral("composer_uploading_count"), uploadingDrafts);
		state.insert(QStringLiteral("composer_failed_count"), failedDrafts);
		state.insert(QStringLiteral("composer_upload_progress_percent"), maximumUploadPercent);
		for (auto it = prependState.constBegin(); it != prependState.constEnd(); ++it) state.insert(it.key(), it.value());
		return state;
	}
}

QmlVisualFixtureController::QmlVisualFixtureController(QmlShellHost *host) : m_host(host) {
}

QmlVisualFixtureController::~QmlVisualFixtureController() {
	resetSurfaceFixtures();
}

double QmlVisualFixtureController::actualDevicePixelRatio() const {
	return m_host && m_host->window() ? m_host->window()->devicePixelRatio() : 0.0;
}

bool QmlVisualFixtureController::ensureFocus(const QString &windowId, QString *error) {
	const QString normalizedWindowId = windowId.trimmed().isEmpty() ? QStringLiteral("main") : windowId.trimmed();
	if (!m_host || m_generation == 0 || !m_focusWindow || !m_focusItem || m_focusItemName.isEmpty()
		|| normalizedWindowId != m_focusWindowId) {
		if (error) {
			*error = QStringLiteral("The visual fixture focus contract is unavailable for window '%1'.")
					 .arg(normalizedWindowId);
		}
		return false;
	}

	QString targetError;
	QQuickWindow *targetWindow = m_host->captureWindowTarget(normalizedWindowId, &targetError);
	bool focusWasRebound = false;
	if (targetWindow && (normalizedWindowId == QLatin1String("main")
			|| normalizedWindowId == QLatin1String("settings")) && !m_focusState.isEmpty()) {
		// Viewport automation can open the compact navigation Drawer after the
		// fixture state itself has selected a focus target. Resolve the target
		// again against the presented hierarchy so we never force focus back to a
		// control hidden behind a modal surface.
		QVariant refreshedFocusTarget;
		if (QMetaObject::invokeMethod(targetWindow, "focusVisualFixture",
				Q_RETURN_ARG(QVariant, refreshedFocusTarget), Q_ARG(QVariant, QVariant(m_focusState)),
				Q_ARG(QVariant, QVariant(m_focusSurfaceVariant)))) {
			const QString refreshedName = refreshedFocusTarget.toString().trimmed();
			QQuickItem *refreshedItem = quickItemByObjectName(targetWindow->contentItem(), refreshedName);
			if (refreshedItem && !refreshedName.isEmpty()) {
				focusWasRebound = refreshedItem != m_focusItem || refreshedName != m_focusItemName;
				m_focusWindow = targetWindow;
				m_focusItem = refreshedItem;
				m_focusItemName = refreshedName;
			}
		}
	}
	if (!targetWindow || targetWindow != m_focusWindow || !targetWindow->isVisible() || !targetWindow->isExposed()
		|| !m_focusItem->isVisible() || !m_focusItem->isEnabled()) {
		if (error) {
			*error = targetError.isEmpty()
				? QStringLiteral("The visual fixture focus target '%1' is no longer capturable.").arg(m_focusItemName)
				: targetError;
		}
		return false;
	}
	if (focusWasRebound) {
		// The accepted scene was already stabilized before accessibility/capture
		// asks us to restore focus. Process the drawer's queued row handoff once,
		// but do not enter another scene-graph frame loop from inside the
		// automation request; that can starve the synchronous TCP response on the
		// Windows software render loop.
		targetWindow->requestActivate();
		m_focusItem->forceActiveFocus(Qt::TabFocusReason);
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
		for (QQuickItem *item = targetWindow->activeFocusItem(); item; item = item->parentItem()) {
			if (item == m_focusItem) return true;
		}
	}

	constexpr int maximumFocusAttempts = 3;
	for (int attempt = 0; attempt < maximumFocusAttempts; ++attempt) {
		targetWindow->requestActivate();
		m_focusItem->forceActiveFocus(Qt::TabFocusReason);
		// Focus ownership is a GUI-item contract, not a scene-graph presentation
		// contract. captureQml immediately proves the rendered frame with
		// grabWindow() and the worker's three-frame stability gate; waiting for a
		// separate render here made every capture consume the full frame timeout on
		// the Windows software loop.
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
		for (QQuickItem *item = targetWindow->activeFocusItem(); item; item = item->parentItem()) {
			if (item == m_focusItem) return true;
		}
	}

	if (error) {
		*error = QStringLiteral("The visual fixture focus target '%1' could not be restored before capture.")
				 .arg(m_focusItemName);
	}
	return false;
}

QVariantMap QmlVisualFixtureController::capabilities() const {
	const bool available = m_host && m_host->window();
	return { { QStringLiteral("capture"), available }, { QStringLiteral("state_injection"), available },
			 { QStringLiteral("window_resize"), available }, { QStringLiteral("theme_override"), available },
			 { QStringLiteral("accessibility_snapshot"), available },
			 { QStringLiteral("supported_states"), QStringList { QStringLiteral("empty"), QStringLiteral("loading"),
														 QStringLiteral("error"), QStringLiteral("connected") } },
			 { QStringLiteral("supported_motd_variants"), supportedMotdVariants() },
			 { QStringLiteral("supported_rich_preview_variants"), supportedRichPreviewVariants() },
			 { QStringLiteral("supported_presentation_families"), supportedPresentationFamilies() },
			 { QStringLiteral("supported_case_variants"), supportedCaseVariants() },
			 { QStringLiteral("supported_surface_variants"), supportedSurfaceVariants() },
			 { QStringLiteral("supported_densities"), QStringList { QStringLiteral("compact"),
				 QStringLiteral("comfortable"), QStringLiteral("spacious") } },
			 { QStringLiteral("actual_device_pixel_ratio"), actualDevicePixelRatio() } };
}

QVariantMap QmlVisualFixtureController::apply(const QVariantMap &request, QString *error) {
	if (!m_host || !m_host->window()) {
		if (error) *error = QStringLiteral("The Qt Quick frontend is not active.");
		return {};
	}
	const QString state = request.value(QStringLiteral("state")).toString().trimmed().toLower();
	const QString theme = request.value(QStringLiteral("theme")).toString().trimmed().toLower();
	const QString layout = request.value(QStringLiteral("layout")).toString().trimmed().toLower();
	const QString requestedMotdVariant = request.value(QStringLiteral("motd_variant")).toString().trimmed().toLower();
	const QString motdVariant = requestedMotdVariant.isEmpty() ? QStringLiteral("none") : requestedMotdVariant;
	const QString requestedRichPreviewVariant =
		request.value(QStringLiteral("rich_preview_variant")).toString().trimmed().toLower();
	const QString richPreviewVariant = requestedRichPreviewVariant.isEmpty()
		? QStringLiteral("none") : requestedRichPreviewVariant;
	const QString requestedRichPreviewSize =
		request.value(QStringLiteral("rich_preview_size")).toString().trimmed().toLower();
	const QString richPreviewSize = requestedRichPreviewSize.isEmpty()
		? QStringLiteral("default") : requestedRichPreviewSize;
	const QString requestedCaseVariant =
		request.value(QStringLiteral("case_variant")).toString().trimmed().toLower();
	const QString caseVariant = requestedCaseVariant.isEmpty() ? richPreviewVariant : requestedCaseVariant;
	const QString requestedPresentationFamily =
		request.value(QStringLiteral("presentation_family")).toString().trimmed().toLower();
	const QString presentationFamily = requestedPresentationFamily.isEmpty()
		? presentationFamilyForCaseVariant(caseVariant) : requestedPresentationFamily;
	const QString expectedPresentationFamily = presentationFamilyForCaseVariant(caseVariant);
	const QString requestedSurfaceVariant =
		request.value(QStringLiteral("surface_variant")).toString().trimmed().toLower();
	const QString surfaceVariant = requestedSurfaceVariant.isEmpty()
		? QStringLiteral("none") : requestedSurfaceVariant;
	const QString caseId = request.value(QStringLiteral("case_id")).toString().trimmed();
	const QString requestedDensity = request.value(QStringLiteral("density")).toString().trimmed().toLower();
	const QString density = requestedDensity.isEmpty()
		? (layout == QLatin1String("compact") ? QStringLiteral("compact") : QStringLiteral("comfortable"))
		: requestedDensity;
	const int width = request.value(QStringLiteral("width")).toInt();
	const int height = request.value(QStringLiteral("height")).toInt();
	if (caseId.isEmpty() || !QStringList { QStringLiteral("empty"), QStringLiteral("loading"),
										 QStringLiteral("error"), QStringLiteral("connected") }.contains(state)
		|| width < 420 || height < 520 || width > 4096 || height > 2160
		|| !QStringList { QStringLiteral("light"), QStringLiteral("dark"), QStringLiteral("custom") }.contains(theme)
		|| !QStringList { QStringLiteral("regular"), QStringLiteral("compact") }.contains(layout)
		|| !supportedMotdVariants().contains(motdVariant)
		|| !supportedRichPreviewVariants().contains(richPreviewVariant)
		|| !supportedCaseVariants().contains(caseVariant)
		|| !supportedPresentationFamilies().contains(presentationFamily)
		|| !supportedSurfaceVariants().contains(surfaceVariant)
		|| !QStringList { QStringLiteral("compact"), QStringLiteral("comfortable"),
			QStringLiteral("spacious") }.contains(density)
		|| presentationFamily != expectedPresentationFamily
		|| (caseVariant == QLatin1String("rich-image-link")
			? richPreviewVariant != QLatin1String("none") : caseVariant != richPreviewVariant)
		|| !QStringList { QStringLiteral("compact"), QStringLiteral("default"), QStringLiteral("large") }
			.contains(richPreviewSize)
		|| (motdVariant != QLatin1String("none") && state != QLatin1String("connected"))
		|| (caseVariant != QLatin1String("none") && state != QLatin1String("connected"))
		|| (isChatSurfaceVariant(surfaceVariant)
			&& (motdVariant != QLatin1String("none")
				|| richPreviewVariant != QLatin1String("none")
				|| caseVariant != QLatin1String("none")))
		|| (surfaceNeedsConnectedState(surfaceVariant) && state != QLatin1String("connected"))) {
		if (error) *error = QStringLiteral("The visual fixture request is invalid or unsupported.");
		return {};
	}

	const bool previousFixtureOverride = m_host->visualFixtureOverrideActive();
	m_host->setVisualFixtureOverrideActive(true);
	struct FixtureOverrideRollback {
		QmlShellHost *host;
		bool previous;
		bool committed = false;
		~FixtureOverrideRollback() {
			if (!committed) host->setVisualFixtureOverrideActive(previous);
		}
	} fixtureOverrideRollback { m_host, previousFixtureOverride };
	QQuickWindow *window = m_host->window();
	const int initialExpectedMessageCount = visualMessageCount(state, surfaceVariant, false);
	const int finalExpectedMessageCount = visualMessageCount(state, surfaceVariant, true);
	QVariantMap prependFixtureState;
	{
		struct MutationScope {
			QmlShellHost *host;
			explicit MutationScope(QmlShellHost *value) : host(value) { host->setVisualFixtureMutationActive(true); }
			~MutationScope() { host->setVisualFixtureMutationActive(false); }
		} mutationScope(m_host);
		// Replacing one modal dialog with another in the same event turn leaves
		// Qt Quick's popup stack without an intervening presentation on the
		// software/basic render loop. Finish the old dialog's teardown first so
		// focus barriers, loaders, and deferred editor callbacks are gone before
		// the next fixture surface is materialized.
		if (m_host->dialogController()->state().value(QStringLiteral("open")).toBool()) {
			m_host->dialogController()->applyState({ { QStringLiteral("open"), false } });
			QEventLoop closeLoop;
			QTimer closePoll;
			QTimer closeTimeout;
			closePoll.setInterval(16);
			closeTimeout.setSingleShot(true);
			QObject::connect(&closePoll, &QTimer::timeout, &closeLoop, [&]() {
				if (!window->property("productDialogTransitionActive").toBool()) {
					closeLoop.quit();
					return;
				}
				window->requestUpdate();
			});
			QObject::connect(&closeTimeout, &QTimer::timeout, &closeLoop, &QEventLoop::quit);
			if (window->property("productDialogTransitionActive").toBool()) {
				closePoll.start();
				closeTimeout.start(2000);
				window->requestUpdate();
				closeLoop.exec();
			}
			if (window->property("productDialogTransitionActive").toBool()) {
				if (error) *error = QStringLiteral("The previous visual product dialog did not finish closing.");
				return {};
			}
		}
		if (!m_host->themeController()->applyVisualGateAppearance(theme, layout, density)) {
			if (error) *error = QStringLiteral("The visual fixture appearance is unsupported.");
			return {};
		}
		window->setWidth(width);
		window->setHeight(height);
		applyState(state, motdVariant, richPreviewVariant, richPreviewSize, caseVariant, surfaceVariant);
		if (m_host->chatModel()->rowCount() != initialExpectedMessageCount) {
			if (error) {
				*error = QStringLiteral("Visual fixture timeline contains %1 messages immediately after injection; expected %2.")
						 .arg(m_host->chatModel()->rowCount())
						 .arg(initialExpectedMessageCount);
			}
			return {};
		}
		const bool expectMotd = motdVariant != QLatin1String("none");
		const bool expectExpanded = expectMotd && motdVariant != QLatin1String("collapsed");
		const bool expectChanged = motdVariant == QLatin1String("changed");
		const bool expectUserHistory = state == QLatin1String("connected")
			&& (motdVariant == QLatin1String("none") || motdVariant == QLatin1String("history-hidden"));
		if (m_host->sessionController()->hasMotd() != expectMotd
			|| (expectMotd && m_host->sessionController()->motdExpanded() != expectExpanded)
			|| m_host->sessionController()->motdChanged() != expectChanged
			|| m_host->chatModel()->hasUserHistory() != expectUserHistory) {
			if (error) *error = QStringLiteral("The visual fixture could not establish the requested MOTD variant.");
			return {};
		}
	}
	// A previous compact case may have left the navigation drawer open. Surface
	// entry points intentionally reject work while a modal owns focus, so close
	// and fully present that transition before opening the next dialog, search,
	// menu, or tool. The gate can request an open drawer again after apply().
	QVariant navigationCloseRequested;
	if (!QMetaObject::invokeMethod(window, "setAutomationNavigationOpen",
			Q_RETURN_ARG(QVariant, navigationCloseRequested), Q_ARG(QVariant, QVariant(false)))) {
		if (error) *error = QStringLiteral("The visual fixture could not reset compact navigation.");
		return {};
	}
	QElapsedTimer preSurfaceNavigationCloseTimer;
	preSurfaceNavigationCloseTimer.start();
	while (window->property("navigationModalActive").toBool()
			&& preSurfaceNavigationCloseTimer.elapsed() < 2000) {
		window->requestUpdate();
		QString frameError;
		if (!waitForPresentedFrame(&frameError, window)) {
			if (error) *error = QStringLiteral("Pre-surface navigation close failed: %1").arg(frameError);
			return {};
		}
	}
	if (window->property("navigationModalActive").toBool()) {
		if (error) *error = QStringLiteral("The previous visual navigation drawer did not finish closing.");
		return {};
	}
	QString captureWindow = QStringLiteral("main");
	if (!applySurface(surfaceVariant, &captureWindow, error)) return {};
	QQuickWindow *presentationWindow = waitForCaptureWindow(captureWindow, error);
	if (!presentationWindow) return {};
	if (captureWindow == QLatin1String("attachment-viewer")) {
		m_visualAttachmentViewer = presentationWindow;
	} else if (captureWindow == QLatin1String("image-viewer")) {
		m_visualImageViewer = presentationWindow;
	}
	if (captureWindow != QLatin1String("main")) {
		presentationWindow->setWidth(width);
		presentationWindow->setHeight(height);
	}
	presentationWindow->requestActivate();
	presentationWindow->requestUpdate();
	if (presentationWindow == window) window->requestActivate();
	// Surface delegates and tool-window content are created asynchronously. Wait
	// for their first capturable frame before resolving a fixture focus target.
	{
		QString frameError;
		if (!waitForPresentedFrame(&frameError, presentationWindow)) {
			if (error) *error = QStringLiteral("Initial visual surface presentation failed: %1").arg(frameError);
			return {};
		}
	}
	if (surfaceVariant == QLatin1String("chat-history-prepend-anchor")) {
		const QString fixtureGeneration = QString::number(m_generation + 1);
		const QString anchorID = visualHistoryMessageID(fixtureGeneration, 12);
		QVariant positioned;
		if (!QMetaObject::invokeMethod(window, "positionVisualFixtureTimelineAt",
				Q_RETURN_ARG(QVariant, positioned), Q_ARG(QVariant, QVariant(anchorID)))
			|| !positioned.toBool()) {
			if (error) *error = QStringLiteral("The history fixture could not position its production timeline anchor.");
			return {};
		}

		const auto readTimelineState = [window](QVariantMap *state) {
			QVariant result;
			if (!QMetaObject::invokeMethod(window, "visualFixtureTimelineState", Q_RETURN_ARG(QVariant, result))) {
				return false;
			}
			*state = result.toMap();
			return !state->isEmpty();
		};
		QVariantMap beforeState;
		for (int attempt = 0; attempt < 8; ++attempt) {
			QString frameError;
			if (!waitForPresentedFrame(&frameError, presentationWindow)) {
				if (error) *error = QStringLiteral("History-anchor positioning failed: %1").arg(frameError);
				return {};
			}
			if (!readTimelineState(&beforeState)) {
				if (error) *error = QStringLiteral("The history fixture could not read its production timeline state.");
				return {};
			}
			if (beforeState.value(QStringLiteral("firstVisibleStableId")).toString() == anchorID) break;
		}
		const int anchorBeforeRow = m_host->chatModel()->rowForStableId(anchorID);
		const double anchorBeforeOffset = beforeState.value(QStringLiteral("firstVisibleOffset")).toDouble();
		if (anchorBeforeRow < 0
			|| beforeState.value(QStringLiteral("firstVisibleStableId")).toString() != anchorID) {
			if (error) *error = QStringLiteral("The requested history anchor did not become the first visible message.");
			return {};
		}

		QVariantList prependedMessages = visualHistoryMessages(
			fixtureGeneration, 1 - VisualHistoryPrependMessageCount, VisualHistoryPrependMessageCount);
		prependedMessages.append(m_host->chatModel()->messages());
		{
			struct MutationScope {
				QmlShellHost *host;
				explicit MutationScope(QmlShellHost *value) : host(value) {
					host->setVisualFixtureMutationActive(true);
				}
				~MutationScope() { host->setVisualFixtureMutationActive(false); }
			} mutationScope(m_host);
			m_host->chatModel()->replaceMessages(prependedMessages);
		}

		QVariantMap afterState;
		bool anchorPreserved = false;
		for (int attempt = 0; attempt < 12; ++attempt) {
			QString frameError;
			if (!waitForPresentedFrame(&frameError, presentationWindow)) {
				if (error) *error = QStringLiteral("History-anchor restoration failed: %1").arg(frameError);
				return {};
			}
			if (!readTimelineState(&afterState)) {
				if (error) *error = QStringLiteral("The history fixture lost its production timeline state.");
				return {};
			}
			const double afterOffset = afterState.value(QStringLiteral("firstVisibleOffset")).toDouble();
			anchorPreserved = afterState.value(QStringLiteral("firstVisibleStableId")).toString() == anchorID
				&& qAbs(afterOffset - anchorBeforeOffset) <= 1.0;
			if (anchorPreserved) break;
		}
		const int anchorAfterRow = m_host->chatModel()->rowForStableId(anchorID);
		const double anchorAfterOffset = afterState.value(QStringLiteral("firstVisibleOffset")).toDouble();
		const bool purePrependApplied = m_host->chatModel()->rowCount() == finalExpectedMessageCount
			&& anchorAfterRow == anchorBeforeRow + VisualHistoryPrependMessageCount
			&& afterState.value(QStringLiteral("count")).toInt() == finalExpectedMessageCount;
		if (!anchorPreserved || !purePrependApplied) {
			if (error) {
				*error = QStringLiteral("The production history prepend did not preserve its viewport anchor "
					"(before row %1/offset %2, after row %3/offset %4, count %5/%6).")
					.arg(anchorBeforeRow).arg(anchorBeforeOffset, 0, 'f', 2)
					.arg(anchorAfterRow).arg(anchorAfterOffset, 0, 'f', 2)
					.arg(m_host->chatModel()->rowCount()).arg(finalExpectedMessageCount);
			}
			return {};
		}
		prependFixtureState = {
			{ QStringLiteral("prepend_count"), VisualHistoryPrependMessageCount },
			{ QStringLiteral("anchor_id"), anchorID },
			{ QStringLiteral("anchor_before_row"), anchorBeforeRow },
			{ QStringLiteral("anchor_after_row"), anchorAfterRow },
			{ QStringLiteral("anchor_before_offset"), anchorBeforeOffset },
			{ QStringLiteral("anchor_after_offset"), anchorAfterOffset },
			{ QStringLiteral("anchor_offset_delta"), qAbs(anchorAfterOffset - anchorBeforeOffset) },
			{ QStringLiteral("anchor_preserved"), anchorPreserved },
			{ QStringLiteral("pure_prepend_applied"), purePrependApplied }
		};
	}
	bool screenShareNativeFrameReady = false;
	if (surfaceVariant == QLatin1String("screen-share-view-active")) {
#if defined(MUMBLE_HAS_MODERN_UI_MOCKUPS) || defined(MUMBLE_HAS_MODERN_UI_AUTOMATION)
		const auto *screenShareBackend = qobject_cast< ScreenShareViewBackend * >(m_visualScreenShareBackend.get());
		QQuickItem *videoFrameItem = quickItemByObjectName(
			presentationWindow->contentItem(), QStringLiteral("screenShareNativeVideoFrame"));
		screenShareNativeFrameReady = screenShareBackend && screenShareBackend->nativeFrameActive()
			&& screenShareBackend->hasCurrentFrame() && videoFrameItem && videoFrameItem->isVisible()
			&& videoFrameItem->width() > 0.0 && videoFrameItem->height() > 0.0;
		if (!screenShareNativeFrameReady) {
			if (error) *error = QStringLiteral("The active screen-share fixture did not expose a visible native frame item.");
			return {};
		}
#else
		if (error) *error = QStringLiteral("The active screen-share fixture is unavailable in this build.");
		return {};
#endif
	}
	if (surfaceVariant == QLatin1String("attachment-viewer")
		|| surfaceVariant == QLatin1String("image-viewer")) {
		const QString imageObjectName = surfaceVariant == QLatin1String("attachment-viewer")
			? QStringLiteral("attachmentViewerImage") : QStringLiteral("imageViewerImage");
		QElapsedTimer imageDeadline;
		imageDeadline.start();
		QQuickItem *imageItem = nullptr;
		int imageStatus = 0;
		do {
			imageItem = quickItemByObjectName(presentationWindow->contentItem(), imageObjectName);
			imageStatus = imageItem ? imageItem->property("status").toInt() : 0;
			if (imageStatus == 1) break;
			if (imageStatus == 3) {
				if (error) *error = QStringLiteral("The visual viewer image '%1' failed to load.").arg(imageObjectName);
				return {};
			}
			QString frameError;
			if (!waitForPresentedFrame(&frameError, presentationWindow)) {
				if (error) *error = QStringLiteral("Visual image readiness presentation failed: %1").arg(frameError);
				return {};
			}
		} while (imageDeadline.elapsed() < 5000);
		if (!imageItem || imageStatus != 1) {
			if (error) *error = QStringLiteral("Timed out waiting for visual viewer image '%1'.").arg(imageObjectName);
			return {};
		}
	}
	// Resizing from a compact saved geometry to a regular fixture can start the
	// Drawer's real close transition. The live product surface stays hidden while
	// any part of that modal remains on screen, so wait for the transition to
	// finish before establishing the case's deterministic focus.
	QElapsedTimer navigationCloseTimer;
	navigationCloseTimer.start();
	while (window->property("navigationModalActive").toBool() && navigationCloseTimer.elapsed() < 2000) {
		QString frameError;
		if (!waitForPresentedFrame(&frameError, presentationWindow)) {
			if (error) *error = QStringLiteral("Navigation close presentation failed: %1").arg(frameError);
			return {};
		}
	}
	if (window->property("navigationModalActive").toBool()) {
		if (error) *error = QStringLiteral("The visual fixture navigation drawer did not close before focus setup.");
		return {};
	}
	QQuickItem *requestedFocusItem = nullptr;
	QString requestedFocusName;
	bool requestedTargetOwnsFocus = false;
	constexpr int maximumFocusAttempts = 3;
	for (int attempt = 0; attempt < maximumFocusAttempts && !requestedTargetOwnsFocus; ++attempt) {
		QVariant focusTarget;
		if (captureWindow == QLatin1String("main") || captureWindow == QLatin1String("settings")
			|| captureWindow == QLatin1String("product-dialog")) {
			QQuickWindow *focusWindow = captureWindow == QLatin1String("main")
				? window : presentationWindow;
			if (!QMetaObject::invokeMethod(focusWindow, "focusVisualFixture", Q_RETURN_ARG(QVariant, focusTarget),
									  Q_ARG(QVariant, QVariant(state)),
									  Q_ARG(QVariant, QVariant(surfaceVariant)))) {
				if (error) *error = QStringLiteral("The visual fixture could not establish a deterministic focus target.");
				return {};
			}
		} else {
			static const QHash< QString, QString > toolFocusTargets {
				{ QStringLiteral("direct-message-window"), QStringLiteral("directMessageComposer") },
				{ QStringLiteral("manual-plugin"), QStringLiteral("manualXField") },
				{ QStringLiteral("ptt-idle"), QStringLiteral("pttHoldButton") },
				{ QStringLiteral("ptt-active"), QStringLiteral("pttHoldButton") },
				{ QStringLiteral("screen-share-view-loading"), QStringLiteral("screenShareCloseButton") },
				{ QStringLiteral("screen-share-view-error"), QStringLiteral("screenShareFailureRetryButton") },
				{ QStringLiteral("screen-share-view-active"), QStringLiteral("screenSharePauseButton") },
				{ QStringLiteral("screen-share-view-paused"), QStringLiteral("screenSharePauseButton") },
				{ QStringLiteral("attachment-viewer"), QStringLiteral("attachmentViewerSaveButton") },
				{ QStringLiteral("image-viewer"), QStringLiteral("imageViewerFit") },
				{ QStringLiteral("media-detached-loading"), QStringLiteral("mediaCloseButton") },
				{ QStringLiteral("media-detached-active"), QStringLiteral("mediaPlayButton") },
				{ QStringLiteral("media-detached-error"), QStringLiteral("mediaSessionRetryButton") },
				{ QStringLiteral("media-detached-retry"), QStringLiteral("mediaSessionRetryButton") },
				{ QStringLiteral("media-detached-external"), QStringLiteral("mediaSessionFailureExternalButton") },
				{ QStringLiteral("media-detached-controls"), QStringLiteral("mediaPlayButton") }
			};
			requestedFocusName = toolFocusTargets.value(surfaceVariant);
			requestedFocusItem = quickItemByObjectName(presentationWindow->contentItem(), requestedFocusName);
			if (!requestedFocusItem) {
				if (error) *error = QStringLiteral("The visual fixture tool focus target '%1' is unavailable.")
					.arg(requestedFocusName);
				return {};
			}
			// A separate QQuickWindow cannot acquire an active-focus item until the
			// platform has activated that window. Windows may expose the scene before
			// activation completes, so perform the same bounded handoff used by the
			// capture-time focus restoration contract before forcing item focus.
			presentationWindow->requestActivate();
			QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
			requestedFocusItem->forceActiveFocus(Qt::TabFocusReason);
			focusTarget = requestedFocusName;
		}
		requestedFocusName = focusTarget.toString().trimmed();
		requestedFocusItem = quickItemByObjectName(presentationWindow->contentItem(), requestedFocusName);
		if (!requestedFocusItem || requestedFocusName.isEmpty()) {
			if (error) {
				*error = QStringLiteral("The visual fixture returned an invalid focus target "
								"(variant type=%1, object name='%2').")
						 .arg(QString::fromLatin1(focusTarget.typeName() ? focusTarget.typeName() : "<unknown>"),
							  requestedFocusName);
			}
			return {};
		}
		QString frameError;
		if (!waitForPresentedFrame(&frameError, presentationWindow)) {
			if (error) *error = QStringLiteral("Visual focus presentation failed: %1").arg(frameError);
			return {};
		}
		for (QQuickItem *item = presentationWindow->activeFocusItem(); item; item = item->parentItem()) {
			if (item == requestedFocusItem) {
				requestedTargetOwnsFocus = true;
				break;
			}
		}
	}
	if (!requestedTargetOwnsFocus) {
		if (error) {
			QStringList activeFocusPath;
			for (QQuickItem *item = presentationWindow->activeFocusItem(); item; item = item->parentItem()) {
				const QString name = item->objectName().trimmed();
				activeFocusPath.append(name.isEmpty()
					? QString::fromLatin1(item->metaObject()->className()) : name);
			}
			*error = QStringLiteral("The visual fixture focus target '%1' did not receive active focus "
								"(target visible=%2, enabled=%3; modal=%4, navigation modal=%5; active path: %6).")
					 .arg(requestedFocusName,
						  requestedFocusItem && requestedFocusItem->isVisible() ? QStringLiteral("true") : QStringLiteral("false"),
						  requestedFocusItem && requestedFocusItem->isEnabled() ? QStringLiteral("true") : QStringLiteral("false"),
						  window->property("modalUiActive").toBool() ? QStringLiteral("true") : QStringLiteral("false"),
						  window->property("navigationModalActive").toBool() ? QStringLiteral("true") : QStringLiteral("false"),
						  activeFocusPath.isEmpty() ? QStringLiteral("<none>") : activeFocusPath.join(QStringLiteral(" > ")));
		}
		return {};
	}
	if (m_host->chatModel()->rowCount() != finalExpectedMessageCount) {
		if (error) {
			*error = QStringLiteral("Visual fixture timeline was clobbered before presentation: observed %1 messages, expected %2.")
					 .arg(m_host->chatModel()->rowCount())
					 .arg(finalExpectedMessageCount);
		}
		return {};
	}
	m_focusWindow = presentationWindow;
	m_focusItem = requestedFocusItem;
	m_focusWindowId = captureWindow;
	m_focusItemName = requestedFocusName;
	m_focusState = state;
	m_focusSurfaceVariant = surfaceVariant;
	const int richPreviewRow = 1;
	const QString richBodyMessageId = m_host->chatModel()->rowCount() > richPreviewRow
		? m_host->chatModel()->get(richPreviewRow).value(QStringLiteral("id")).toString()
		: QString();
	const QVariantMap normalizedRichPreview = richPreviewVariant == QLatin1String("none")
		? QVariantMap() : m_host->chatModel()->get(richPreviewRow).value(QStringLiteral("preview")).toMap();
	const bool expectRichPreview = richPreviewVariant != QLatin1String("none");
	const QString expectedAspect = expectedRichPreviewAspect(richPreviewVariant);
	const bool expectGeneratedImage = QStringList { QStringLiteral("youtube"), QStringLiteral("spotify"),
		QStringLiteral("tiktok"), QStringLiteral("vimeo"), QStringLiteral("dailymotion"),
		QStringLiteral("soundcloud"), QStringLiteral("instagram"), QStringLiteral("audio"),
		QStringLiteral("product"), QStringLiteral("steam"), QStringLiteral("twitch"),
		QStringLiteral("vehicle"), QStringLiteral("property"), QStringLiteral("marketplace"),
		QStringLiteral("sensitive"),
		QStringLiteral("direct-media") }
		.contains(richPreviewVariant);
	if (normalizedRichPreview.isEmpty() != !expectRichPreview
		|| (expectRichPreview && normalizedRichPreview.value(QStringLiteral("title")).toString().isEmpty())
		|| (expectRichPreview
			&& (normalizedRichPreview.value(QStringLiteral("presentationFamily")).toString() != presentationFamily
				|| normalizedRichPreview.value(QStringLiteral("caseVariant")).toString() != caseVariant))
		|| (expectRichPreview
			&& normalizedRichPreview.value(QStringLiteral("previewSize")).toString() != richPreviewSize)
		|| (!expectedAspect.isEmpty()
			&& (normalizedRichPreview.value(QStringLiteral("embedKind")).toString() != richPreviewVariant
				|| normalizedRichPreview.value(QStringLiteral("embedAspect")).toString() != expectedAspect))
		|| (expectGeneratedImage
			&& (normalizedRichPreview.value(QStringLiteral("thumbnailUrl")).toString().isEmpty()
				|| normalizedRichPreview.value(QStringLiteral("mediaItems")).toList().isEmpty()))
		|| (richPreviewVariant == QLatin1String("loading")
			&& normalizedRichPreview.value(QStringLiteral("state")).toString() != QLatin1String("loading"))
		|| (richPreviewVariant == QLatin1String("error")
			&& normalizedRichPreview.value(QStringLiteral("state")).toString() != QLatin1String("error"))) {
		if (error) *error = QStringLiteral("The visual fixture could not establish the normalized rich-preview contract.");
		return {};
	}
	QVariantMap manualPluginState;
#ifdef USE_MANUAL_PLUGIN
	if (surfaceVariant == QLatin1String("manual-plugin")) {
		const ManualPluginController *manual = m_host->manualPluginController();
		manualPluginState = {
			{ QStringLiteral("x"), manual->x() }, { QStringLiteral("y"), manual->y() },
			{ QStringLiteral("z"), manual->z() }, { QStringLiteral("azimuth"), manual->azimuth() },
			{ QStringLiteral("elevation"), manual->elevation() },
			{ QStringLiteral("context"), manual->context() },
			{ QStringLiteral("identity"), manual->identity() },
			{ QStringLiteral("stale_seconds"), manual->staleSeconds() },
			{ QStringLiteral("active"), manual->active() }, { QStringLiteral("linked"), manual->linked() }
		};
	}
#endif
	QVariantMap recorderState;
	if (surfaceVariant.startsWith(QLatin1String("dialog-recorder"))) {
		const Mumble::ModernRecorderController *recorder = m_host->recorderController();
		recorderState = {
			{ QStringLiteral("state"), recorder->state() },
			{ QStringLiteral("elapsed_milliseconds"), recorder->elapsedMilliseconds() },
			{ QStringLiteral("elapsed_text"), recorder->elapsedText() },
			{ QStringLiteral("output_directory"), recorder->outputDirectory() },
			{ QStringLiteral("file_name"), recorder->fileName() },
			{ QStringLiteral("format"), recorder->format() },
			{ QStringLiteral("mode"), recorder->mode() },
			{ QStringLiteral("can_edit"), recorder->canEdit() },
			{ QStringLiteral("can_start"), recorder->canStart() },
			{ QStringLiteral("can_pause"), recorder->canPause() },
			{ QStringLiteral("can_stop"), recorder->canStop() }
		};
	}
	QVariantMap toastState;
	if (surfaceVariant.startsWith(QLatin1String("toast-"))) {
		const ToastController *toast = m_host->toastController();
		const int expectedRepeats = surfaceVariant == QLatin1String("toast-duplicate") ? 4 : 1;
		if (!toast || !toast->visible() || toast->repeatCount() != expectedRepeats) {
			if (error) *error = QStringLiteral("The typed toast fixture did not establish the requested burst state.");
			return {};
		}
		toastState = {
			{ QStringLiteral("visible"), toast->visible() }, { QStringLiteral("tone"), toast->tone() },
			{ QStringLiteral("title"), toast->title() }, { QStringLiteral("message"), toast->message() },
			{ QStringLiteral("action_id"), toast->actionId() },
			{ QStringLiteral("repeat_count"), toast->repeatCount() }
		};
	}
	const QVariantMap conversationSearchState {
		{ QStringLiteral("query"), m_host->chatModel()->query() },
		{ QStringLiteral("match_count"), m_host->chatModel()->matchCount() },
		{ QStringLiteral("current_match_index"), m_host->chatModel()->currentMatchIndex() },
		{ QStringLiteral("current_match_row"), m_host->chatModel()->currentMatchRow() },
		{ QStringLiteral("current_match_stable_id"), m_host->chatModel()->currentMatchStableId() }
	};
	const QVariantMap chatFixtureState = visualChatFixtureState(m_host, surfaceVariant, prependFixtureState);
	fixtureOverrideRollback.committed = true;
	++m_generation;
	const bool motdHiddenForHistory = m_host->sessionController()->hasMotd()
		&& m_host->chatModel()->hasUserHistory();
	return { { QStringLiteral("case_id"), caseId }, { QStringLiteral("state"), state },
			 { QStringLiteral("theme"), theme }, { QStringLiteral("layout"), layout },
			 { QStringLiteral("density"), density },
			 { QStringLiteral("motd_variant"), motdVariant },
			 { QStringLiteral("rich_preview_variant"), richPreviewVariant },
			 { QStringLiteral("rich_preview_size"), richPreviewSize },
			 { QStringLiteral("presentation_family"), presentationFamily },
			 { QStringLiteral("case_variant"), caseVariant },
			 { QStringLiteral("surface_variant"), surfaceVariant },
			 { QStringLiteral("capture_window"), captureWindow },
			 { QStringLiteral("surface_present"), surfaceVariant != QLatin1String("none") },
			 { QStringLiteral("screen_share_native_frame_ready"), screenShareNativeFrameReady },
			 { QStringLiteral("rich_preview_present"), !normalizedRichPreview.isEmpty() },
			 { QStringLiteral("rich_preview_message_id"), normalizedRichPreview.isEmpty()
				 ? QString() : richBodyMessageId },
			 { QStringLiteral("rich_body_message_id"), richBodyMessageId },
			 { QStringLiteral("rich_preview_title"), normalizedRichPreview.value(QStringLiteral("title")).toString() },
			 { QStringLiteral("rich_preview_open_label"), normalizedRichPreview.value(QStringLiteral("openLabel")).toString() },
			 { QStringLiteral("rich_preview_embed_provider"), normalizedRichPreview.value(QStringLiteral("embedKind")).toString() },
			 { QStringLiteral("rich_preview_embed_aspect"), normalizedRichPreview.value(QStringLiteral("embedAspect")).toString() },
			 { QStringLiteral("rich_preview_media_count"), normalizedRichPreview.value(QStringLiteral("mediaItems")).toList().size() },
			 { QStringLiteral("rich_preview_has_thumbnail"),
				 !normalizedRichPreview.value(QStringLiteral("thumbnailUrl")).toString().isEmpty() },
			 { QStringLiteral("width"), window->width() }, { QStringLiteral("height"), window->height() },
			 { QStringLiteral("message_count"), m_host->chatModel()->rowCount() },
			 { QStringLiteral("motd_present"), m_host->sessionController()->hasMotd() },
			 { QStringLiteral("motd_expanded"), m_host->sessionController()->hasMotd()
				&& m_host->sessionController()->motdExpanded() },
			 { QStringLiteral("motd_changed"), m_host->sessionController()->motdChanged() },
			 { QStringLiteral("motd_has_user_history"), m_host->chatModel()->hasUserHistory() },
			 { QStringLiteral("motd_visible"), m_host->sessionController()->hasMotd()
				&& !m_host->sessionController()->motdDismissed() && !motdHiddenForHistory },
			 { QStringLiteral("manual_plugin_state"), manualPluginState },
			 { QStringLiteral("recorder_state"), recorderState },
			 { QStringLiteral("toast_state"), toastState },
			 { QStringLiteral("conversation_search_state"), conversationSearchState },
			 { QStringLiteral("chat_fixture_state"), chatFixtureState },
			 { QStringLiteral("focus_target"), requestedFocusName },
			 { QStringLiteral("actual_device_pixel_ratio"), actualDevicePixelRatio() },
			 { QStringLiteral("generation"), m_generation } };
}

void QmlVisualFixtureController::resetSurfaceFixtures(const bool preserveDetachedMediaWindow,
													 const bool preserveProductMenus) {
	m_focusWindow.clear();
	m_focusItem.clear();
	m_focusWindowId.clear();
	m_focusItemName.clear();
	if (!m_host) return;
	if (m_visualAttachmentViewer) {
		m_visualAttachmentViewer->close();
		m_visualAttachmentViewer.clear();
	}
	if (m_visualImageViewer) {
		m_visualImageViewer->close();
		m_visualImageViewer.clear();
	}
	if (m_visualScreenShareView) {
		QPointer< QObject > previousView(m_visualScreenShareView);
		m_host->closeScreenShareView(previousView);
		// The fixture opens several detached viewers back-to-back in one process.
		// Destroy exactly the old fixture window before reusing its surface ID so
		// capture lookup can never resolve a deferred, already closed window.
		if (previousView) {
			QCoreApplication::sendPostedEvents(previousView.data(), QEvent::DeferredDelete);
		}
		m_visualScreenShareView.clear();
	}
	if (m_visualScreenShareBackend) {
		QObject *previousBackend = m_visualScreenShareBackend.release();
		previousBackend->deleteLater();
		// Keep the product's close path asynchronous. Only fixture-owned objects
		// are drained here, and only after the QML view released its bindings.
		QCoreApplication::sendPostedEvents(previousBackend, QEvent::DeferredDelete);
	}
	m_host->commandController()->releasePtt();
	m_host->showPttTool(false);
#ifdef USE_MANUAL_PLUGIN
	if (QQuickWindow *manualWindow = m_host->captureWindowTarget(QStringLiteral("manual-plugin"))) {
		manualWindow->setProperty("visualFixtureMode", false);
	}
	m_host->showManualPluginTool(false);
#endif
	if (!preserveDetachedMediaWindow) m_host->mediaSession()->clearSharedState();
	if (ModernServerAdminController *admin = m_host->serverAdminController()) {
		admin->users()->reset();
		admin->bans()->reset();
		admin->users()->setCanManage(false);
		admin->bans()->setCanManage(false);
	}
	if (Mumble::ModernRecorderController *recorder = m_host->recorderController()) {
		recorder->clearVisualFixtureState();
	}
	if (ToastController *toast = m_host->toastController()) {
		toast->dismiss();
	}
	if (QQuickWindow *window = m_host->window()) {
		if (!preserveDetachedMediaWindow) window->setProperty("visualMediaFixtureMode", QString());
		if (!preserveProductMenus) QMetaObject::invokeMethod(window, "closeProductMenus");
		QMetaObject::invokeMethod(window, "closeConversationSearch",
			Q_ARG(QVariant, QVariant(false)));
	}
	m_host->chatModel()->clearSearch();
}

bool QmlVisualFixtureController::applySurface(const QString &surfaceVariant, QString *captureWindow,
											   QString *error) {
	if (!m_host || !m_host->window()) return false;
	// Consecutive detached media fixtures use the same asynchronous Loader item.
	// Toggling the backend inactive and active in one automation request makes the
	// Loader race its deferred teardown against focus setup: the old top-level
	// window can receive focus and then be destroyed before captureQml. Preserve
	// only an already-active detached fixture presentation and reset the backend
	// below through open(); non-media and inline transitions still take the normal
	// close/unload path. This is fixture-only and does not alter product playback,
	// detach/attach or watch-together continuity.
	const bool preserveDetachedMediaWindow =
		surfaceVariant.startsWith(QLatin1String("media-detached-"))
		&& m_host->mediaSession()->active() && m_host->mediaSession()->detached();
	// The appServer fixture is a real second-level state of the immediately
	// preceding app menu. Preserve that already-present parent instead of starting
	// its exit transition and reopening it in the same scene turn.
	const bool preserveProductMenus = surfaceVariant == QLatin1String("menu-app-server");
	resetSurfaceFixtures(preserveDetachedMediaWindow, preserveProductMenus);
	if (captureWindow) *captureWindow = QStringLiteral("main");
	struct MutationScope {
		QmlShellHost *host;
		explicit MutationScope(QmlShellHost *value) : host(value) { host->setVisualFixtureMutationActive(true); }
		~MutationScope() { host->setVisualFixtureMutationActive(false); }
	} mutationScope(m_host);
	QObject *serverAdminEditorController = nullptr;

	if (surfaceVariant == QLatin1String("none")) return true;
	if (isChatSurfaceVariant(surfaceVariant)) return true;
	if (surfaceVariant.startsWith(QLatin1String("conversation-search-"))) {
		if (!QMetaObject::invokeMethod(m_host->window(), "openConversationSearch")) {
			if (error) *error = QStringLiteral("The conversation-search fixture could not open its native surface.");
			return false;
		}
		const bool expectMatch = surfaceVariant == QLatin1String("conversation-search-match");
		m_host->chatModel()->setQuery(expectMatch ? QStringLiteral("Qt Quick")
			: QStringLiteral("missing constellation"));
		const int expectedMatches = expectMatch ? 1 : 0;
		if (m_host->chatModel()->matchCount() != expectedMatches
			|| (expectMatch && m_host->chatModel()->currentMatchStableId().isEmpty())) {
			if (error) *error = QStringLiteral("The conversation-search fixture did not establish its typed match state.");
			return false;
		}
		return true;
	}
	if (surfaceVariant.startsWith(QLatin1String("dialog-recorder"))) {
		Mumble::ModernRecorderController *recorder = m_host->recorderController();
		int fixtureFormat = 0;
		for (const QVariant &entry : recorder->formatOptions()) {
			const QVariantMap option = entry.toMap();
			if (option.value(QStringLiteral("label")).toString().contains(
					QStringLiteral("FLAC"), Qt::CaseInsensitive)) {
				fixtureFormat = option.value(QStringLiteral("value")).toInt();
				break;
			}
		}
		const bool recording = surfaceVariant == QLatin1String("dialog-recorder-recording");
		if (!recorder->applyVisualFixtureState(
				recording ? QStringLiteral("recording") : QStringLiteral("idle"),
				recording ? 872000 : 0, QStringLiteral("C:/Users/Demo/Mumble recordings"),
				QStringLiteral("Mumble-%date-%time-%user"), fixtureFormat,
				Mumble::ModernRecorderController::Mixdown, true)) {
			if (error) *error = QStringLiteral("The typed recorder fixture state could not be applied.");
			return false;
		}
	}
	if (surfaceVariant.startsWith(QLatin1String("dialog-server-users"))
		|| surfaceVariant.startsWith(QLatin1String("dialog-server-bans"))) {
		ModernServerAdminController *admin = m_host->serverAdminController();
		const bool users = surfaceVariant.startsWith(QLatin1String("dialog-server-users"));
		admin->users()->setCanManage(users);
		admin->bans()->setCanManage(!users);
		if (users) {
			ModernRegisteredUsersController *controller = admin->users();
			serverAdminEditorController = controller;
			if (surfaceVariant == QLatin1String("dialog-server-users-loading")) {
				// Keep the production loading transition without dispatching a real
				// request from this deterministic, synthetic fixture.
				const QSignalBlocker blocker(controller);
				controller->refresh();
			} else {
				MumbleProto::UserList snapshot;
				auto *adminUser = snapshot.add_users();
				adminUser->set_user_id(1);
				adminUser->set_name("Demo Admin <ops>");
				adminUser->set_last_seen("2026-07-17T08:30:00Z");
				adminUser->set_last_channel(1);
				auto *moderator = snapshot.add_users();
				moderator->set_user_id(42);
				moderator->set_name("Demo Moderator");
				moderator->set_last_seen("2026-07-16T20:10:00Z");
				moderator->set_last_channel(2);
				controller->applySnapshot(snapshot,
					{ { 1, QStringLiteral("Root / Lobby") }, { 2, QStringLiteral("Root / Community") } });
				if (surfaceVariant.endsWith(QLatin1String("-edit"))
					|| surfaceVariant.endsWith(QLatin1String("-confirm"))) {
					controller->typedModel()->setSelectedStableId(QStringLiteral("user:42"));
				}
				if (surfaceVariant.endsWith(QLatin1String("-confirm"))) {
					controller->beginRename(QStringLiteral("user:42"), QStringLiteral("Community Moderator"));
				}
			}
		} else {
			ModernBanListController *controller = admin->bans();
			serverAdminEditorController = controller;
			if (surfaceVariant == QLatin1String("dialog-server-bans-error")) {
				controller->applyLoadError(QStringLiteral("The server rejected the ban-list request."));
			} else {
				MumbleProto::BanList snapshot;
				if (surfaceVariant != QLatin1String("dialog-server-bans-empty")) {
					auto *ban = snapshot.add_bans();
					const HostAddress host(QHostAddress(QStringLiteral("192.0.2.42")));
					ban->set_address(host.toStdString());
					ban->set_mask(128);
					ban->set_name("Demo Spammer");
					ban->set_hash("visual-fixture-certificate-hash");
					ban->set_reason("Repeated channel spam");
					ban->set_start("2026-07-17T07:30:00Z");
					ban->set_duration(7200);
				}
				controller->applySnapshot(snapshot);
				if (surfaceVariant == QLatin1String("dialog-server-bans-edit")
					&& controller->typedModel()->rowCount() > 0) {
					const QString stableId = controller->typedModel()->data(
						controller->typedModel()->index(0, 0), ModernBanListModel::StableIdRole).toString();
					controller->typedModel()->setSelectedStableId(stableId);
				}
			}
		}
	}
	if (surfaceVariant.startsWith(QLatin1String("settings-"))
		|| surfaceVariant.startsWith(QLatin1String("dialog-"))
		|| surfaceVariant.startsWith(QLatin1String("screen-share-editor"))) {
		QVariantMap dialog = visualDialogForSurface(surfaceVariant, serverAdminEditorController);
		if (dialog.isEmpty()) {
			if (error) *error = QStringLiteral("The requested visual dialog fixture is unavailable.");
			return false;
		}
		// DialogStateController is the frontend mirror of the live
		// ModernDialogController. The live controller adds this lifecycle bit in
		// openGenericDialog; visual fixtures add only that bit before publishing
		// the otherwise identical production DTO.
		dialog.insert(QStringLiteral("open"), true);
		m_host->dialogController()->applyState(dialog);
		if (surfaceVariant.startsWith(QLatin1String("settings-")) && captureWindow) {
			*captureWindow = QStringLiteral("settings");
		} else if (captureWindow) {
			*captureWindow = QStringLiteral("product-dialog");
		}
		return true;
	}

	if (surfaceVariant.startsWith(QLatin1String("menu-"))) {
		QString menuVariant = surfaceVariant.mid(QStringLiteral("menu-").size());
		if (menuVariant == QLatin1String("text-room")) {
			menuVariant = QStringLiteral("textRoom");
		} else if (menuVariant == QLatin1String("chat-background")) {
			menuVariant = QStringLiteral("chatBackground");
		}
		QVariant result;
		if (!QMetaObject::invokeMethod(m_host->window(), "openAutomationMenuProbe", Q_RETURN_ARG(QVariant, result),
									  Q_ARG(QVariant, QVariant(menuVariant)))) {
			if (error) *error = QStringLiteral("The requested visual menu fixture could not be opened.");
			return false;
		}
		const QVariantMap menu = result.toMap();
		if (!menu.value(QStringLiteral("handled")).toBool() || !menu.value(QStringLiteral("visible")).toBool()) {
			if (error) {
				*error = QStringLiteral(
					"The requested visual menu fixture is not visible "
					"(variant=%1, handled=%2, open=%3, count=%4, height=%5/%6, reply=%7, react=%8, labels=%9).")
					.arg(menuVariant,
						 menu.value(QStringLiteral("handled")).toBool() ? QStringLiteral("true") : QStringLiteral("false"),
						 menu.value(QStringLiteral("open")).toBool() ? QStringLiteral("true") : QStringLiteral("false"),
						 QString::number(menu.value(QStringLiteral("menuCount")).toInt()),
						 QString::number(menu.value(QStringLiteral("menuHeight")).toDouble()),
						 QString::number(menu.value(QStringLiteral("menuImplicitHeight")).toDouble()),
						 menu.value(QStringLiteral("targetCanReply")).toBool() ? QStringLiteral("true") : QStringLiteral("false"),
						 menu.value(QStringLiteral("targetCanReact")).toBool() ? QStringLiteral("true") : QStringLiteral("false"),
						 menu.value(QStringLiteral("labels")).toStringList().join(QLatin1Char('|')));
			}
			return false;
		}
		return true;
	}

	if (surfaceVariant.startsWith(QLatin1String("direct-message-"))) {
		const bool dockedMain = surfaceVariant == QLatin1String("direct-message-main");
		const bool traySurface = surfaceVariant == QLatin1String("direct-message-tray");
		const QString imageSource = registerVisualPreviewImage(m_host, QStringLiteral("direct-message-attachment"),
			QStringLiteral("Release checklist attachment"), QStringLiteral("Direct-message attachment gallery fixture"),
			QColor(QStringLiteral("#3a2f78")), QColor(QStringLiteral("#8a76e8")), QSize(960, 540));
		if (imageSource.isEmpty()) {
			if (error) *error = QStringLiteral("The direct-message gallery fixture image could not be registered.");
			return false;
		}
		const QVariantMap attachment {
			{ QStringLiteral("id"), QStringLiteral("dm-visual-attachment") },
			{ QStringLiteral("url"), imageSource }, { QStringLiteral("thumbnailUrl"), imageSource },
			{ QStringLiteral("name"), QStringLiteral("release-checklist.png") },
			{ QStringLiteral("fileName"), QStringLiteral("release-checklist.png") },
			{ QStringLiteral("alt"), QStringLiteral("Release checklist attachment") },
			{ QStringLiteral("kind"), QStringLiteral("image") }, { QStringLiteral("mime"), QStringLiteral("image/png") },
			{ QStringLiteral("inlineToken"), QStringLiteral("dm-visual-attachment-token") },
			{ QStringLiteral("byteSize"), 147456 }
		};
		const QVariantMap conversation {
			{ QStringLiteral("peerSession"), 102 }, { QStringLiteral("token"), QStringLiteral("-2:102") },
			{ QStringLiteral("label"), QStringLiteral("Alex") },
			{ QStringLiteral("subtitle"), QStringLiteral("Private conversation · Lobby") },
			{ QStringLiteral("lastMessagePreview"), QStringLiteral("The native DM surface feels fast.") },
			{ QStringLiteral("open"), !dockedMain && !traySurface }, { QStringLiteral("canSend"), true },
			{ QStringLiteral("unreadCount"), traySurface ? 2 : 0 }, { QStringLiteral("persistentHistory"), false },
			{ QStringLiteral("persistentHistoryAvailable"), true },
			{ QStringLiteral("messages"), QVariantList {
				QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("dm:fixture:1") },
					{ QStringLiteral("actorName"), QStringLiteral("Alex") },
					{ QStringLiteral("plainText"), QStringLiteral("The native DM surface feels fast.") },
					{ QStringLiteral("timeLabel"), QStringLiteral("10:31") }, { QStringLiteral("own"), false } },
				QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("dm:fixture:2") },
					{ QStringLiteral("actorName"), QStringLiteral("Demo User") },
					{ QStringLiteral("plainText"), QStringLiteral("And private mode keeps this conversation local.") },
					{ QStringLiteral("timeLabel"), QStringLiteral("10:32") }, { QStringLiteral("own"), true },
					{ QStringLiteral("attachments"), QVariantList { attachment } } }
			} }
		};
		m_host->directMessageController()->setWindowDocked(dockedMain || traySurface);
		m_host->directMessageController()->applyState({
			{ QStringLiteral("available"), true }, { QStringLiteral("title"), QStringLiteral("Direct messages") },
			{ QStringLiteral("description"), QStringLiteral("Private and persistent conversations") },
			{ QStringLiteral("unreadTotal"), traySurface ? 2 : 0 },
			{ QStringLiteral("trayOpen"), traySurface },
			{ QStringLiteral("conversations"), QVariantList { conversation } },
			{ QStringLiteral("activeConversation"), conversation }
		});
		m_host->roomModel()->replaceDirectMessageStates({ conversation });
		m_host->navigationModel()->replaceDirectMessageStates({ conversation });
		if (dockedMain) {
			m_host->roomModel()->selectScope(QStringLiteral("-2:102"));
			m_host->navigationModel()->selectScope(QStringLiteral("-2:102"));
			m_host->activeScopeController()->applyState({
				{ QStringLiteral("scopeToken"), QStringLiteral("-2:102") },
				{ QStringLiteral("label"), QStringLiteral("Alex") },
				{ QStringLiteral("description"), QStringLiteral("Private conversation · Lobby") },
				{ QStringLiteral("kindLabel"), QStringLiteral("DIRECT MESSAGE") },
				{ QStringLiteral("composerPlaceholder"), QStringLiteral("Message Alex") },
				{ QStringLiteral("canSend"), true }, { QStringLiteral("canAttachImages"), true }
			});
			m_host->chatModel()->replaceMessages({
				QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("dm:fixture:1") },
					{ QStringLiteral("actor"), QStringLiteral("Alex") },
					{ QStringLiteral("bodyText"), QStringLiteral("The native DM surface feels fast.") },
					{ QStringLiteral("timeLabel"), QStringLiteral("10:31") },
					{ QStringLiteral("own"), false }, { QStringLiteral("canReply"), true },
					{ QStringLiteral("preview"), QVariantMap() },
					{ QStringLiteral("attachments"), QVariantList() },
					{ QStringLiteral("reactions"), QVariantList() } },
				QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("dm:fixture:2") },
					{ QStringLiteral("actor"), QStringLiteral("Demo User") },
					{ QStringLiteral("bodyText"), QStringLiteral("And private mode keeps this conversation local.") },
					{ QStringLiteral("timeLabel"), QStringLiteral("10:32") },
					{ QStringLiteral("own"), true }, { QStringLiteral("canReply"), true },
					{ QStringLiteral("preview"), QVariantMap() },
					{ QStringLiteral("attachments"), QVariantList { attachment } },
					{ QStringLiteral("reactions"), QVariantList() } }
			});
		}
		if (surfaceVariant == QLatin1String("direct-message-window") && captureWindow) {
			*captureWindow = QStringLiteral("direct-message");
		}
		return true;
	}

	if (surfaceVariant == QLatin1String("watch-together-hosting")) {
		m_host->mediaSession()->setCurrentVoiceScopeId(1);
		m_host->mediaSession()->applySharedState(
			QStringLiteral("visual-watch-together"), QUrl(QStringLiteral("https://www.youtube.com/watch?v=fixture")),
			QStringLiteral("youtube"), QStringLiteral("Community release watch party"), 1, 101, 101,
			QVariantList { 101, 102 }, QStringLiteral("start"), 42.0, false, 1, 101);
		if (!m_host->mediaSession()->sharedAvailable() || !m_host->mediaSession()->sharedJoined()
			|| !m_host->mediaSession()->sharedHost()) {
			if (error) *error = QStringLiteral("The Watch Together fixture did not establish its typed host state.");
			return false;
		}
		return true;
	}

	if (surfaceVariant == QLatin1String("attachment-viewer")) {
		const QString imageSource = registerVisualPreviewImage(m_host, surfaceVariant,
			QStringLiteral("Qt Quick attachment artwork"),
			QStringLiteral("Deterministic attachment viewer fixture"),
			QColor(QStringLiteral("#1f5f78")), QColor(QStringLiteral("#79c9d8")), QSize(1200, 800));
		if (imageSource.isEmpty()) {
			if (error) *error = QStringLiteral("The attachment viewer fixture image could not be registered.");
			return false;
		}
		const QVariantMap attachment {
			{ QStringLiteral("id"), QStringLiteral("visual-attachment") },
			{ QStringLiteral("url"), imageSource },
			{ QStringLiteral("thumbnailUrl"), imageSource },
			{ QStringLiteral("name"), QStringLiteral("qt-quick-attachment.png") },
			{ QStringLiteral("fileName"), QStringLiteral("qt-quick-attachment.png") },
			{ QStringLiteral("alt"), QStringLiteral("Qt Quick attachment artwork") },
			{ QStringLiteral("kind"), QStringLiteral("image") },
			{ QStringLiteral("mime"), QStringLiteral("image/png") },
			{ QStringLiteral("inlineToken"), QStringLiteral("visual-attachment-token") },
			{ QStringLiteral("byteSize"), 184320 }
		};
		QVariant opened;
		if (!QMetaObject::invokeMethod(m_host->window(), "openAttachment", Q_RETURN_ARG(QVariant, opened),
								  Q_ARG(QVariant, QVariant(attachment)),
								  Q_ARG(QVariant, QVariant(QStringLiteral("Qt Quick attachment artwork"))),
								  Q_ARG(QVariant, QVariant(QString())))
			|| !opened.toBool()) {
			if (error) *error = QStringLiteral("The attachment viewer visual fixture could not be opened.");
			return false;
		}
		if (captureWindow) *captureWindow = QStringLiteral("attachment-viewer");
		return true;
	}

	if (surfaceVariant == QLatin1String("image-viewer")) {
		const QString imageSource = registerVisualPreviewImage(m_host, surfaceVariant,
			QStringLiteral("Qt Quick image canvas"),
			QStringLiteral("Deterministic image viewer fixture"),
			QColor(QStringLiteral("#4c2a78")), QColor(QStringLiteral("#b08ce5")), QSize(1200, 800));
		if (imageSource.isEmpty()) {
			if (error) *error = QStringLiteral("The image viewer fixture image could not be registered.");
			return false;
		}
		m_host->dialogController()->applyState({
			{ QStringLiteral("id"), QStringLiteral("visual:image-viewer") },
			{ QStringLiteral("kind"), QStringLiteral("imageViewer") },
			{ QStringLiteral("title"), QStringLiteral("Qt Quick image canvas") },
			{ QStringLiteral("description"), QString() },
			{ QStringLiteral("fields"), QVariantList() },
			{ QStringLiteral("actions"), QVariantList() },
			{ QStringLiteral("open"), true },
			{ QStringLiteral("imageViewer"), QVariantMap {
				{ QStringLiteral("src"), imageSource },
				{ QStringLiteral("width"), 1200 },
				{ QStringLiteral("height"), 800 }
			} }
		});
		if (captureWindow) *captureWindow = QStringLiteral("image-viewer");
		return true;
	}

	if (surfaceVariant.startsWith(QLatin1String("async-"))) {
		AsyncOperationModel *operations = m_host->operationModel();
		if (surfaceVariant == QLatin1String("async-running")) {
			operations->startStructuredOperation(QStringLiteral("visual:update"), QStringLiteral("plugin-update"),
				QStringLiteral("Updating plugins"), QStringLiteral("Downloading Positional Audio"), 3, true);
			operations->updateStructuredProgress(QStringLiteral("visual:update"), QStringLiteral("download"),
				1, 3, 42, 7340032, 12582912);
		} else {
			operations->startStructuredOperation(QStringLiteral("visual:update"), QStringLiteral("plugin-update"),
				QStringLiteral("Plugin update results"), QStringLiteral("2 of 3 plugins completed"), 3, false);
			operations->appendItemResult(QStringLiteral("visual:update"), QStringLiteral("plugin:1"), 1,
				true, false, {}, QStringLiteral("Positional Audio updated"));
			if (surfaceVariant == QLatin1String("async-error")) {
				operations->appendItemResult(QStringLiteral("visual:update"), QStringLiteral("plugin:2"), 2,
					false, false, QStringLiteral("network-error"), QStringLiteral("Update server unavailable"));
				operations->finishOperation(QStringLiteral("visual:update"), false,
					QStringLiteral("partial-failure"), QStringLiteral("One plugin could not be updated"));
			} else {
				operations->appendItemResult(QStringLiteral("visual:update"), QStringLiteral("plugin:2"), 2,
					true, false, {}, QStringLiteral("Stream Deck updated"));
				operations->finishOperation(QStringLiteral("visual:update"), true, {},
					QStringLiteral("All selected plugins are up to date"));
			}
		}
		return true;
	}

	if (surfaceVariant.startsWith(QLatin1String("toast-"))) {
		ToastController *toast = m_host->toastController();
		const int burstCount = surfaceVariant == QLatin1String("toast-duplicate") ? 4 : 1;
		for (int index = 0; index < burstCount; ++index) {
			toast->publish(QStringLiteral("success"), QStringLiteral("Settings saved"),
				QStringLiteral("Your Modern client preferences are ready."),
				QStringLiteral("configure.settings"), QStringLiteral("Review"), 60000);
		}
		return true;
	}

	if (surfaceVariant == QLatin1String("update-banner")) {
		m_host->sessionController()->setUpdateBanner({
			{ QStringLiteral("visible"), true }, { QStringLiteral("tone"), QStringLiteral("accent") },
			{ QStringLiteral("title"), QStringLiteral("Mumble update ready") },
			{ QStringLiteral("detail"), QStringLiteral("Restart to install the signed Windows client update.") },
			{ QStringLiteral("progressVisible"), true }, { QStringLiteral("progressPercent"), 100 },
			{ QStringLiteral("progressLabel"), QStringLiteral("Download complete") },
			{ QStringLiteral("actions"), QVariantList {
				visualAction(QStringLiteral("update.restart"), QStringLiteral("Restart now"), QStringLiteral("accent"), true),
				visualAction(QStringLiteral("update.notes"), QStringLiteral("What’s new")) } }
		});
		return true;
	}

	if (surfaceVariant.startsWith(QLatin1String("ptt-"))) {
		m_host->showPttTool(true);
		if (!m_host->pttToolVisible()) {
			if (error) *error = QStringLiteral("The PTT visual fixture window could not be opened.");
			return false;
		}
		QQuickWindow *pttWindow = m_host->captureWindowTarget(QStringLiteral("ptt"));
		if (!pttWindow) {
			if (error) *error = QStringLiteral("The PTT visual fixture window is not available.");
			return false;
		}
		m_host->commandController()->releasePtt();
		if (surfaceVariant == QLatin1String("ptt-active")) {
			// Showing the tool transfers focus away from the main window. That
			// transition intentionally releases any in-flight PTT state, so drive
			// the real hold path only after the tool owns focus. This keeps the
			// focus-loss fail-safe intact while making the active fixture
			// deterministic.
			const auto beginFixtureHold = [pttWindow]() {
				if (pttWindow && pttWindow->isVisible()) {
					QMetaObject::invokeMethod(pttWindow, "beginHold");
				}
			};
			if (pttWindow->isActive()) {
				beginFixtureHold();
			} else {
				QObject::connect(pttWindow, &QWindow::activeChanged, pttWindow,
					[pttWindow, beginFixtureHold]() {
						if (pttWindow->isActive()) beginFixtureHold();
					}, Qt::SingleShotConnection);
			}
		}
		if (captureWindow) *captureWindow = QStringLiteral("ptt");
		return true;
	}

	if (surfaceVariant == QLatin1String("manual-plugin")) {
#ifdef USE_MANUAL_PLUGIN
		m_host->showManualPluginTool(true);
		if (!m_host->manualPluginToolVisible()) {
			if (error) *error = QStringLiteral("The Manual Plugin visual fixture window could not be opened.");
			return false;
		}
		if (QQuickWindow *manualWindow = m_host->captureWindowTarget(QStringLiteral("manual-plugin"))) {
			manualWindow->setProperty("visualFixtureMode", true);
		}
		ManualPluginController *manual = m_host->manualPluginController();
		manual->setX(2.75); manual->setY(1.4); manual->setZ(-4.25);
		manual->setAzimuth(32); manual->setElevation(-8);
		manual->setContext(QStringLiteral("visual-fixture:lobby"));
		manual->setIdentity(QStringLiteral("Demo User · Qt Quick"));
		manual->setStaleSeconds(15); manual->setActive(true); manual->setLinked(true);
		if (captureWindow) *captureWindow = QStringLiteral("manual-plugin");
		return true;
#else
		if (error) *error = QStringLiteral("The Manual Plugin is not available in this build.");
		return false;
#endif
	}

	if (surfaceVariant.startsWith(QLatin1String("screen-share-view-"))) {
		ScreenShareSession session;
		session.streamID = QStringLiteral("visual-screen-share");
		session.ownerSession = 102;
		session.scopeID = 1;
		session.captureAudio = true;
		session.width = 1920;
		session.height = 1080;
		session.fps = 30;
		session.state = MumbleProto::ScreenShareLifecycleStateActive;
		auto backend = std::make_unique< ScreenShareViewBackend >(session);
		backend->setIdentity(QStringLiteral("Alex"), QStringLiteral("Lobby"));
		if (surfaceVariant == QLatin1String("screen-share-view-loading")) {
			backend->setOperationState(QStringLiteral("loading"), {}, true);
		} else if (surfaceVariant == QLatin1String("screen-share-view-error")) {
			backend->setOperationState(QStringLiteral("error"),
				QStringLiteral("The deterministic helper handshake timed out."), false);
#if defined(MUMBLE_HAS_MODERN_UI_MOCKUPS) || defined(MUMBLE_HAS_MODERN_UI_AUTOMATION)
		} else if (surfaceVariant == QLatin1String("screen-share-view-active")) {
			backend->setVisualFixtureFrame(visualScreenShareFrame());
			backend->setOperationState(QStringLiteral("idle"), {}, false);
#endif
		} else {
			backend->setPaused(true);
			backend->setOperationState(QStringLiteral("idle"), {}, false);
		}
		QObject *view = m_host->createScreenShareView(backend.get());
		if (!view) {
			if (error) *error = QStringLiteral("The screen-share visual fixture window could not be opened.");
			return false;
		}
		m_visualScreenShareBackend = std::move(backend);
		m_visualScreenShareView = view;
		if (captureWindow) *captureWindow = QStringLiteral("screen-share");
		return true;
	}

	if (surfaceVariant.startsWith(QLatin1String("media-"))) {
		const bool detached = surfaceVariant.startsWith(QLatin1String("media-detached-"));
		const QString mode = surfaceVariant.section(QLatin1Char('-'), 2);
		const QString messageId = m_host->chatModel()->rowCount() > 1
			? m_host->chatModel()->get(1).value(QStringLiteral("id")).toString() : QString();
		if (messageId.isEmpty()) {
			if (error) *error = QStringLiteral("Media visual fixtures require the deterministic preview message.");
			return false;
		}
		m_host->window()->setProperty("visualMediaFixtureMode", mode);
		const bool opened = detached
			? m_host->mediaSession()->open(QUrl(QStringLiteral("https://www.youtube.com/embed/dQw4w9WgXcQ")),
				QStringLiteral("youtube"), messageId)
			: m_host->mediaSession()->openInline(QUrl(QStringLiteral("https://www.youtube.com/embed/dQw4w9WgXcQ")),
				QStringLiteral("youtube"), messageId);
		if (!opened) {
			if (error) *error = m_host->mediaSession()->error();
			return false;
		}
		if (mode == QLatin1String("active") || mode == QLatin1String("controls")) {
			m_host->mediaSession()->reportPlaybackState(83.0, 252.0, mode == QLatin1String("controls"));
			m_host->mediaSession()->setVolume(72);
		} else if (mode == QLatin1String("error") || mode == QLatin1String("retry")
				   || mode == QLatin1String("external")) {
			m_host->mediaSession()->reportTypedError(QStringLiteral("visual-fixture"),
				QStringLiteral("The provider renderer stopped. Retry here or continue in your browser."));
		} else {
			m_host->mediaSession()->reportLoadProgress(42);
		}
		if (detached && captureWindow) *captureWindow = QStringLiteral("media-session");
		return true;
	}

	if (error) *error = QStringLiteral("The requested visual surface fixture is unsupported.");
	return false;
}

void QmlVisualFixtureController::applyState(const QString &state, const QString &motdVariant,
										 const QString &richPreviewVariant, const QString &richPreviewSize,
										 const QString &caseVariant, const QString &surfaceVariant) {
	ClientSessionController *session = m_host->sessionController();
	ActiveScopeController *scope = m_host->activeScopeController();
	RoomModel *rooms = m_host->roomModel();
	NavigationRailModel *navigation = m_host->navigationModel();
	ParticipantModel *participants = m_host->participantModel();
	ChatTimelineModel *chat = m_host->chatModel();
	AsyncOperationModel *operations = m_host->operationModel();
	DialogStateController *dialog = m_host->dialogController();
	ComposerController *composer = m_host->composerController();
	DirectMessageController *directMessages = m_host->directMessageController();
	rooms->replaceDirectMessageStates({});
	navigation->replaceDirectMessageStates({});
	participants->replaceParticipantStates({});
	composer->finishSend(false);
	composer->setText({});
	composer->attachments()->clear();
	composer->setAutocompleteSources({}, {});
	composer->setCanSend(false);
	operations->clear();
	dialog->applyState({ { QStringLiteral("open"), false } });
	session->setUpdateBanner({});
	session->setMotdContent({}, {});
	session->setMotdSummary({});
	session->setMotdExpanded(true);
	session->setMotdDismissedSignature({});
	session->setMotdLastSeenSignature({});
	session->setSelfMuted(false);
	session->setSelfDeafened(false);
	session->setSelfMenu({});
	session->setAppMenus({});
	directMessages->applyState({
		{ QStringLiteral("available"), false },
		{ QStringLiteral("title"), QStringLiteral("Direct messages") },
		{ QStringLiteral("trayOpen"), false },
		{ QStringLiteral("conversations"), QVariantList() }
	});

	if (state == QLatin1String("connected")) {
		directMessages->applyState({
			{ QStringLiteral("available"), true },
			{ QStringLiteral("title"), QStringLiteral("Direct messages") },
			{ QStringLiteral("description"),
			  QStringLiteral("Private conversations stay separate from room chat.") },
			{ QStringLiteral("trayOpen"), false },
			{ QStringLiteral("conversations"), QVariantList() }
		});
		session->setConnected(true);
		session->setServerName(QStringLiteral("Mumble Visual Fixture"));
		session->setConnectionLabel(QStringLiteral("Connected"));
		session->setConnectionState(QStringLiteral("connected"));
		session->setConnectionTone(QStringLiteral("success"));
		session->setConnectionDetail({});
		session->setConnectionRetryRemainingMs(0);
		session->setCanConnect(false);
		session->setCanCancel(false);
		session->setSelfStatusLabel(QStringLiteral("Online"));
		session->setSelfName(QStringLiteral("Demo User"));
		session->setSelfMenu({
			{ QStringLiteral("name"), QStringLiteral("Demo User") },
			{ QStringLiteral("statusLabel"), QStringLiteral("Online") },
			{ QStringLiteral("statusTone"), QStringLiteral("success") },
			{ QStringLiteral("presence"), QVariantList {
				visualMenuAction(QStringLiteral("self.presence.online"), QStringLiteral("Online"),
					QStringLiteral("success"), true, true),
				visualMenuAction(QStringLiteral("self.presence.away"), QStringLiteral("Away"),
					QStringLiteral("warning"), true, false),
				visualMenuAction(QStringLiteral("self.presence.muted"), QStringLiteral("Muted"),
					QStringLiteral("warning"), true, false),
				visualMenuAction(QStringLiteral("self.presence.deafened"), QStringLiteral("Deafened"),
					QStringLiteral("danger"), true, false) } },
			{ QStringLiteral("actions"), QVariantList {
				visualMenuAction(QStringLiteral("server.disconnect"), QStringLiteral("Disconnect"),
					QStringLiteral("danger")) } }
		});
		// App-menu visuals must not inherit availability from the live QAction
		// registry behind the deterministic fixture. Keep the production IDs and
		// hierarchy, but publish a stable typed menu payload just like every other
		// fixture-owned model in this method.
		session->setAppMenus({
			QVariantMap {
				{ QStringLiteral("id"), QStringLiteral("server") },
				{ QStringLiteral("label"), QStringLiteral("Server") },
				{ QStringLiteral("items"), QVariantList {
					visualMenuAction(QStringLiteral("server.information"), QStringLiteral("Server information…")),
					visualMenuAction(QStringLiteral("server.search"), QStringLiteral("Search…")),
					visualMenuAction(QStringLiteral("server.connect"), QStringLiteral("Connect to a server…")),
					visualMenuAction(QStringLiteral("server.disconnect"), QStringLiteral("Disconnect…"),
						QStringLiteral("danger")),
					QVariantMap { { QStringLiteral("kind"), QStringLiteral("separator") } },
					visualMenuAction(QStringLiteral("server.tokens"), QStringLiteral("Access tokens…")) } }
			},
			QVariantMap {
				{ QStringLiteral("id"), QStringLiteral("room") },
				{ QStringLiteral("label"), QStringLiteral("Room") },
				{ QStringLiteral("items"), QVariantList {
					visualMenuAction(QStringLiteral("server.createRoom"), QStringLiteral("Create room…")) } }
			},
			QVariantMap {
				{ QStringLiteral("id"), QStringLiteral("configure") },
				{ QStringLiteral("label"), QStringLiteral("Configure") },
				{ QStringLiteral("items"), QVariantList {
					visualMenuAction(QStringLiteral("configure.settings"), QStringLiteral("Settings")),
					visualMenuAction(QStringLiteral("configure.certificate"), QStringLiteral("Certificate…")) } }
			},
			QVariantMap {
				{ QStringLiteral("id"), QStringLiteral("help") },
				{ QStringLiteral("label"), QStringLiteral("Help") },
				{ QStringLiteral("items"), QVariantList {
					visualMenuAction(QStringLiteral("help.about"), QStringLiteral("About Mumble")) } }
			}
		});
		if (motdVariant != QLatin1String("none")) {
			const QString fixtureMotd = QStringLiteral(
				"<h2>Welcome to Mumble</h2>"
				"<p>This deterministic server message verifies the native Qt Quick welcome surface.</p>"
				"<p><b>Tip:</b> Choose a room on the left, then say hello.</p>");
			session->setMotdContent(fixtureMotd, QStringLiteral("qml-visual-motd-v1"));
			session->setMotdSummary(QStringLiteral("Welcome to Mumble. Choose a room, then say hello."));
			session->setMotdExpanded(motdVariant != QLatin1String("collapsed"));
			if (motdVariant == QLatin1String("changed"))
				session->setMotdLastSeenSignature(QStringLiteral("qml-visual-motd-previous"));
		}
		const QVariantList participantActions {
			visualMenuAction(QStringLiteral("textMessage"), QStringLiteral("Send private message…")),
			visualMenuAction(QStringLiteral("localMute"), QStringLiteral("Mute locally"), {}, true, false),
			visualMenuAction(QStringLiteral("userInfo"), QStringLiteral("View user information"))
		};
		const QVariantList fixtureParticipants {
			QVariantMap { { QStringLiteral("session"), QStringLiteral("101") },
						{ QStringLiteral("participantKey"), QStringLiteral("user:101") },
						{ QStringLiteral("name"), QStringLiteral("Demo User") },
						{ QStringLiteral("isSelf"), true },
						{ QStringLiteral("statusLabel"), QStringLiteral("Listening") },
						{ QStringLiteral("talkState"), QStringLiteral("passive") } },
			QVariantMap { { QStringLiteral("session"), QStringLiteral("102") },
						{ QStringLiteral("participantKey"), QStringLiteral("user:102") },
						{ QStringLiteral("name"), QStringLiteral("Alex") },
						{ QStringLiteral("statusLabel"), QStringLiteral("Talking") },
						{ QStringLiteral("talkState"), QStringLiteral("talking") },
						{ QStringLiteral("canMessage"), true },
						{ QStringLiteral("actionsAvailable"), true },
						{ QStringLiteral("actions"), participantActions } }
		};
		const QVariantList voiceRoomActions {
			visualMenuAction(QStringLiteral("sendMessage"), QStringLiteral("Send room message…")),
			visualMenuAction(QStringLiteral("copyUrl"), QStringLiteral("Copy room URL")),
			visualMenuAction(QStringLiteral("acl"), QStringLiteral("Edit room…")),
			visualMenuAction(QStringLiteral("screenShareStart"), QStringLiteral("Share your screen…"))
		};
		const QVariantList voiceRooms {
			QVariantMap { { QStringLiteral("token"), QStringLiteral("0:1") }, { QStringLiteral("label"), QStringLiteral("Lobby") },
						  { QStringLiteral("selected"), true }, { QStringLiteral("joined"), true }, { QStringLiteral("depth"), 0 },
						  { QStringLiteral("unreadCount"), 0 }, { QStringLiteral("participantCount"), 2 },
						  { QStringLiteral("actions"), voiceRoomActions },
						  { QStringLiteral("participants"), fixtureParticipants } },
			QVariantMap { { QStringLiteral("token"), QStringLiteral("0:2") }, { QStringLiteral("label"), QStringLiteral("Studio") },
						  { QStringLiteral("depth"), 0 }, { QStringLiteral("unreadCount"), 0 } }
		};
		const QVariantList textRoomActions {
			QVariantMap { { QStringLiteral("kind"), QStringLiteral("action") },
						  { QStringLiteral("id"), QStringLiteral("markRead") },
						  { QStringLiteral("label"), QStringLiteral("Mark read") },
						  { QStringLiteral("enabled"), true }, { QStringLiteral("visible"), true },
						  { QStringLiteral("checkable"), false }, { QStringLiteral("checked"), false } }
		};
		const QVariantList textRooms {
			QVariantMap { { QStringLiteral("token"), QStringLiteral("3:1") },
						  { QStringLiteral("label"), QStringLiteral("#general") },
						  { QStringLiteral("description"), QStringLiteral("Text room") },
						  { QStringLiteral("selected"), false }, { QStringLiteral("depth"), 0 },
						  { QStringLiteral("unreadCount"), 0 },
						  { QStringLiteral("actions"), textRoomActions } }
		};
		// Scope changes can synchronously publish live room, participant, and
		// conversation state. Apply every fixture model after the scope so those
		// signal side-effects cannot clobber deterministic state while fixture
		// writes are temporarily enabled.
		QVariantMap scopeState {
			{ QStringLiteral("scopeToken"), QStringLiteral("0:1") },
			{ QStringLiteral("label"), QStringLiteral("Lobby") },
			{ QStringLiteral("description"), QStringLiteral("Voice room") },
			{ QStringLiteral("kindLabel"), QStringLiteral("VOICE") },
			{ QStringLiteral("composerPlaceholder"), QStringLiteral("Message Lobby") },
			{ QStringLiteral("canSend"), true }, { QStringLiteral("canAttachImages"), true }
		};
		if (surfaceVariant == QLatin1String("chat-composer-states")) {
			scopeState.insert(QStringLiteral("hasPendingReply"), true);
			scopeState.insert(QStringLiteral("replyActor"), QStringLiteral("Alex"));
			scopeState.insert(QStringLiteral("replySnippet"),
				QStringLiteral("Use the community test build once this check passes."));
		}
		scope->applyState(scopeState);
		participants->replaceParticipantStates(fixtureParticipants);
		const bool systemMessages = motdVariant != QLatin1String("none")
			&& motdVariant != QLatin1String("history-hidden");
		const QVariantMap richPreview = visualRichPreview(m_host, richPreviewVariant, richPreviewSize);
		QString fixtureBodyText = QStringLiteral("Qt Quick is ready for review.");
		QString fixtureBodyHtml;
		if (caseVariant == QLatin1String("rich-image-link")) {
			const QString image = registerVisualPreviewImage(m_host, caseVariant,
				QStringLiteral("Qt Quick release artwork"), QStringLiteral("Linked rich-message image fixture"),
				QColor(QStringLiteral("#4c2a78")), QColor(QStringLiteral("#8b6bd6")), QSize(960, 540));
			fixtureBodyText = QStringLiteral("Open the linked Qt Quick release artwork.");
			fixtureBodyHtml = QStringLiteral(
				"<p>Open the linked Qt Quick release artwork.</p><p><a href=\"https://example.com/qt-quick-release\">"
				"<img src=\"%1\" alt=\"Qt Quick release artwork\" width=\"640\" height=\"360\"></a></p>")
				.arg(image.toHtmlEscaped());
		}
		const QString fixtureGeneration = QString::number(m_generation + 1);
		QVariantList fixtureMessages {
			QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("fixture:%1:1").arg(fixtureGeneration) }, { QStringLiteral("actor"), QStringLiteral("Alex") },
							{ QStringLiteral("bodyText"), QStringLiteral("Welcome to the deterministic visual fixture.") },
							{ QStringLiteral("timeLabel"), QStringLiteral("10:24") }, { QStringLiteral("own"), false },
							{ QStringLiteral("canReply"), true },
							{ QStringLiteral("system"), systemMessages },
							{ QStringLiteral("preview"), QVariantMap() }, { QStringLiteral("attachments"), QVariantList() },
							{ QStringLiteral("reactions"), QVariantList() } },
			QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("fixture:%1:2").arg(fixtureGeneration) }, { QStringLiteral("actor"), QStringLiteral("Demo User") },
							{ QStringLiteral("bodyText"), fixtureBodyText },
							{ QStringLiteral("bodyHtml"), fixtureBodyHtml },
							{ QStringLiteral("timeLabel"), QStringLiteral("10:25") }, { QStringLiteral("own"), true },
							{ QStringLiteral("system"), systemMessages },
							{ QStringLiteral("preview"), richPreview }, { QStringLiteral("attachments"), QVariantList() },
							{ QStringLiteral("reactions"), QVariantList() } }
		};
		if (surfaceVariant == QLatin1String("chat-message-states")) {
			fixtureMessages = {
				QVariantMap {
					{ QStringLiteral("messageKey"), QStringLiteral("fixture:%1:message:reply").arg(fixtureGeneration) },
					{ QStringLiteral("actor"), QStringLiteral("Alex") },
					{ QStringLiteral("actorKey"), QStringLiteral("alex") },
					{ QStringLiteral("bodyText"), QStringLiteral("The latest community review build is ready.") },
					{ QStringLiteral("timeLabel"), QStringLiteral("10:31") },
					{ QStringLiteral("replyActor"), QStringLiteral("Kira") },
					{ QStringLiteral("replySnippet"), QStringLiteral("Ship the Qt Quick candidate after the gate.") },
					{ QStringLiteral("canReply"), true }, { QStringLiteral("canReact"), true },
					{ QStringLiteral("reactions"), QVariantList { QVariantMap {
						{ QStringLiteral("emoji"), QStringLiteral("👍") }, { QStringLiteral("count"), 3 },
						{ QStringLiteral("selfReacted"), true },
						{ QStringLiteral("actorNames"), QVariantList {
							QStringLiteral("Demo User"), QStringLiteral("Alex"), QStringLiteral("Kira") } }
					} } },
					{ QStringLiteral("preview"), QVariantMap() }, { QStringLiteral("attachments"), QVariantList() }
				},
				QVariantMap {
					{ QStringLiteral("messageKey"), QStringLiteral("fixture:%1:message:sending").arg(fixtureGeneration) },
					{ QStringLiteral("actor"), QStringLiteral("Demo User") },
					{ QStringLiteral("bodyText"), QStringLiteral("Uploading the final review notes now…") },
					{ QStringLiteral("timeLabel"), QStringLiteral("10:32") }, { QStringLiteral("own"), true },
					{ QStringLiteral("deliveryState"), QStringLiteral("sending") },
					{ QStringLiteral("deliveryLabel"), QStringLiteral("Sending…") },
					{ QStringLiteral("canDelete"), true }, { QStringLiteral("preview"), QVariantMap() },
					{ QStringLiteral("attachments"), QVariantList() }, { QStringLiteral("reactions"), QVariantList() }
				},
				QVariantMap {
					{ QStringLiteral("messageKey"), QStringLiteral("fixture:%1:message:failed").arg(fixtureGeneration) },
					{ QStringLiteral("actor"), QStringLiteral("Demo User") },
					{ QStringLiteral("bodyText"), QStringLiteral("This message needs another attempt.") },
					{ QStringLiteral("timeLabel"), QStringLiteral("10:33") }, { QStringLiteral("own"), true },
					{ QStringLiteral("deliveryState"), QStringLiteral("failed") },
					{ QStringLiteral("deliveryLabel"), QStringLiteral("Couldn’t send") },
					{ QStringLiteral("deliveryCanRetry"), true },
					{ QStringLiteral("deliveryRetryLabel"), QStringLiteral("Retry") },
					{ QStringLiteral("canDelete"), true }, { QStringLiteral("preview"), QVariantMap() },
					{ QStringLiteral("attachments"), QVariantList() }, { QStringLiteral("reactions"), QVariantList() }
				},
				QVariantMap {
					{ QStringLiteral("messageKey"), QStringLiteral("fixture:%1:message:deleted").arg(fixtureGeneration) },
					{ QStringLiteral("actor"), QStringLiteral("Demo User") },
					{ QStringLiteral("bodyText"), QStringLiteral("This obsolete draft was removed.") },
					{ QStringLiteral("timeLabel"), QStringLiteral("10:34") }, { QStringLiteral("own"), true },
					{ QStringLiteral("deleted"), true }, { QStringLiteral("preview"), QVariantMap() },
					{ QStringLiteral("attachments"), QVariantList() }, { QStringLiteral("reactions"), QVariantList() }
				}
			};
		} else if (surfaceVariant == QLatin1String("chat-attachment-states")) {
			const QString readyImage = registerVisualPreviewImage(m_host,
				QStringLiteral("chat-attachment-ready"), QStringLiteral("Community test dashboard"),
				QStringLiteral("Ready message attachment"), QColor(QStringLiteral("#24516f")),
				QColor(QStringLiteral("#55b6c8")), QSize(960, 540));
			const QString loadingImage = registerVisualPreviewImage(m_host,
				QStringLiteral("chat-attachment-loading"), QStringLiteral("Performance trace"),
				QStringLiteral("Loading message attachment"), QColor(QStringLiteral("#3f315f")),
				QColor(QStringLiteral("#8b78d8")), QSize(960, 540));
			fixtureMessages = { QVariantMap {
				{ QStringLiteral("messageKey"), QStringLiteral("fixture:%1:attachment:states").arg(fixtureGeneration) },
				{ QStringLiteral("actor"), QStringLiteral("Alex") }, { QStringLiteral("actorKey"), QStringLiteral("alex") },
				{ QStringLiteral("bodyText"), QStringLiteral("Here are the assets from the latest community-test pass.") },
				{ QStringLiteral("timeLabel"), QStringLiteral("10:36") },
				{ QStringLiteral("canReply"), true }, { QStringLiteral("canReact"), true },
				{ QStringLiteral("preview"), QVariantMap() }, { QStringLiteral("reactions"), QVariantList() },
				{ QStringLiteral("attachments"), QVariantList {
					QVariantMap {
						{ QStringLiteral("id"), QStringLiteral("attachment-ready") },
						{ QStringLiteral("inlineToken"), QStringLiteral("abcdef0123456789abcdef01") },
						{ QStringLiteral("url"), readyImage }, { QStringLiteral("thumbnailUrl"), readyImage },
						{ QStringLiteral("fileName"), QStringLiteral("community-dashboard.png") },
						{ QStringLiteral("alt"), QStringLiteral("Community test dashboard") },
						{ QStringLiteral("kind"), QStringLiteral("image") },
						{ QStringLiteral("mime"), QStringLiteral("image/png") },
						{ QStringLiteral("byteSize"), 286720 }, { QStringLiteral("width"), 960 },
						{ QStringLiteral("height"), 540 }, { QStringLiteral("state"), QStringLiteral("ready") }
					},
					QVariantMap {
						{ QStringLiteral("id"), QStringLiteral("attachment-loading") },
						{ QStringLiteral("inlineToken"), QStringLiteral("89abcdef0123456789abcdef") },
						{ QStringLiteral("url"), loadingImage }, { QStringLiteral("thumbnailUrl"), loadingImage },
						{ QStringLiteral("fileName"), QStringLiteral("performance-trace.png") },
						{ QStringLiteral("alt"), QStringLiteral("Performance trace") },
						{ QStringLiteral("kind"), QStringLiteral("image") },
						{ QStringLiteral("mime"), QStringLiteral("image/png") },
						{ QStringLiteral("byteSize"), 196608 }, { QStringLiteral("width"), 960 },
						{ QStringLiteral("height"), 540 }, { QStringLiteral("state"), QStringLiteral("loading") }
					},
					QVariantMap {
						{ QStringLiteral("id"), QStringLiteral("attachment-error") },
						{ QStringLiteral("assetId"), 9003 },
						{ QStringLiteral("inlineToken"), QStringLiteral("0123456789abcdef01234567") },
						{ QStringLiteral("fileName"), QStringLiteral("failed-preview.png") },
						{ QStringLiteral("alt"), QStringLiteral("Failed attachment preview") },
						{ QStringLiteral("kind"), QStringLiteral("image") },
						{ QStringLiteral("mime"), QStringLiteral("image/png") },
						{ QStringLiteral("byteSize"), 81920 }, { QStringLiteral("width"), 960 },
						{ QStringLiteral("height"), 540 }, { QStringLiteral("state"), QStringLiteral("error") },
						{ QStringLiteral("previewCanRetry"), true }
					}
				} }
			} };
		} else if (surfaceVariant == QLatin1String("chat-history-prepend-anchor")) {
			fixtureMessages = visualHistoryMessages(fixtureGeneration, 1, VisualHistoryInitialMessageCount);
		}
		chat->replaceMessages(fixtureMessages);
		if (surfaceVariant == QLatin1String("chat-composer-states")) {
			const QString draftImage = registerVisualPreviewImage(m_host,
				QStringLiteral("chat-composer-upload"), QStringLiteral("Community preview"),
				QStringLiteral("Uploading composer attachment"), QColor(QStringLiteral("#214d62")),
				QColor(QStringLiteral("#5bb8c6")), QSize(640, 360));
			DraftAttachmentModel::Item uploading;
			uploading.id = QStringLiteral("fixture:%1:draft:uploading").arg(fixtureGeneration);
			uploading.thumbnailUrl = draftImage;
			uploading.fileName = QStringLiteral("community-preview.png");
			uploading.mime = QStringLiteral("image/png");
			uploading.kind = Mumble::ChatAttachments::Kind::Image;
			uploading.byteSize = 184320;
			uploading.status = QStringLiteral("uploading");
			uploading.progress = 0.54;
			composer->attachments()->append(uploading);

			DraftAttachmentModel::Item failed;
			failed.id = QStringLiteral("fixture:%1:draft:failed").arg(fixtureGeneration);
			failed.fileName = QStringLiteral("release-notes.pdf");
			failed.mime = QStringLiteral("application/pdf");
			failed.kind = Mumble::ChatAttachments::Kind::Document;
			failed.byteSize = 94208;
			failed.status = QStringLiteral("failed");
			failed.error = QStringLiteral("Upload interrupted");
			composer->attachments()->append(failed);
			composer->setCanSend(true);
			composer->setAutocompleteSources(
				{ QStringLiteral("Alex"), QStringLiteral("Kira") },
				{ QStringLiteral("mute"), QStringLiteral("shrug") });
			composer->setText(QStringLiteral("@Al"));
		}
		// Timeline publication can synchronously refresh persistent unread counts.
		// Keep the synthetic room rows last so no live badge leaks into the fixture.
		rooms->replaceRoomStates(voiceRooms, textRooms);
		navigation->replaceRoomStates(voiceRooms, textRooms);
		return;
	}

	chat->replaceMessages({});
	session->setConnected(false);
	session->setServerName(QStringLiteral("Mumble"));
	session->setConnectionLabel(state == QLatin1String("loading") ? QStringLiteral("Connecting…") : QStringLiteral("Disconnected"));
	session->setConnectionState(state == QLatin1String("loading") ? QStringLiteral("connecting")
															 : QStringLiteral("disconnected"));
	session->setConnectionTone(state == QLatin1String("error") ? QStringLiteral("danger") : QStringLiteral("muted"));
	session->setConnectionDetail(state == QLatin1String("error")
		? QStringLiteral("The test server could not be reached. Try again.") : QString());
	session->setConnectionRetryRemainingMs(0);
	session->setCanConnect(state != QLatin1String("loading"));
	session->setCanCancel(state == QLatin1String("loading"));
	session->setSelfStatusLabel(QStringLiteral("Offline"));
	session->setSelfName(QStringLiteral("You"));
	rooms->replaceRoomStates({}, {});
	navigation->replaceRoomStates({}, {});
	scope->applyState({ { QStringLiteral("label"), state == QLatin1String("empty") ? QStringLiteral("No conversation selected")
																								 : QStringLiteral("Connection") },
						 { QStringLiteral("description"), QStringLiteral("Choose a server to begin") },
						 { QStringLiteral("kindLabel"), QStringLiteral("STATUS") }, { QStringLiteral("canSend"), false } });
}

QQuickWindow *QmlVisualFixtureController::waitForCaptureWindow(const QString &windowId, QString *error) {
	if (!m_host) {
		if (error) *error = QStringLiteral("The Qt Quick frontend is unavailable for visual capture.");
		return nullptr;
	}

	QString targetError;
	if (QQuickWindow *window = m_host->captureWindowTarget(windowId, &targetError);
		window && window->isVisible() && window->isExposed()) {
		return window;
	}

	// Separate QML windows are created by asynchronous Loaders. Allow that
	// loader to finish without making QmlShellHost's read-only lookup block.
	constexpr int captureWindowTimeoutMilliseconds = 5000;
	QPointer< QmlShellHost > guardedHost(m_host);
	QPointer< QQuickWindow > targetWindow;
	QEventLoop loop;
	QTimer poll;
	QTimer timeout;
	poll.setInterval(16);
	timeout.setSingleShot(true);
	QObject::connect(&poll, &QTimer::timeout, &loop, [&]() {
		if (!guardedHost) {
			targetError = QStringLiteral("The Qt Quick frontend was destroyed while waiting for a capture window.");
			loop.quit();
			return;
		}
		targetError.clear();
		QQuickWindow *candidate = guardedHost->captureWindowTarget(windowId, &targetError);
		if (candidate && candidate->isVisible() && candidate->isExposed()) {
			targetWindow = candidate;
			loop.quit();
			return;
		}
		targetWindow.clear();
		if (windowId.compare(QLatin1String("media-session"), Qt::CaseInsensitive) == 0
			&& guardedHost->window()
			&& guardedHost->window()->property("mediaSessionWindowComponentFailed").toBool()) {
			targetError = guardedHost->mediaSession() ? guardedHost->mediaSession()->error() : QString();
			if (targetError.isEmpty()) {
				targetError = QStringLiteral("The isolated media fixture window component could not be loaded.");
			}
			loop.quit();
		}
	});
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	poll.start();
	timeout.start(captureWindowTimeoutMilliseconds);
	loop.exec();

	if (!targetWindow && error) {
		*error = targetError.isEmpty()
			? QStringLiteral("Timed out waiting for Qt Quick capture window '%1'.").arg(windowId)
			: targetError;
	}
	return targetWindow.data();
}

bool QmlVisualFixtureController::waitForPresentedFrame(QString *error, QQuickWindow *window) {
	if (!window) window = m_host ? m_host->window() : nullptr;
	QPointer< QQuickWindow > guardedWindow(window);
	if (!guardedWindow) {
		if (error) *error = QStringLiteral("The Qt Quick window is unavailable for visual capture.");
		return false;
	}

	constexpr int exposureTimeoutMilliseconds = 5000;
	constexpr int presentationTimeoutMilliseconds = 5000;
	QElapsedTimer exposureElapsed;
	exposureElapsed.start();
	while (guardedWindow && !guardedWindow->isExposed()
		   && exposureElapsed.elapsed() < exposureTimeoutMilliseconds) {
		// A nested QEventLoop::exec() inherits a process-level quit flag and can
		// return without dispatching its local timers after repeated popup
		// replacements. Pump one bounded GUI event turn directly instead.
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
		QThread::msleep(1);
	}
	if (!guardedWindow) {
		if (error) *error = QStringLiteral("The Qt Quick window was destroyed while waiting for exposure.");
		return false;
	}
	if (!guardedWindow->isExposed()) {
		if (error) *error = QStringLiteral("Timed out waiting for Qt Quick window exposure.");
		return false;
	}

	QObject frameContext;
	const auto presented = std::make_shared< std::atomic_bool >(false);
	const auto markFrameCompleted = [presented]() { presented->store(true, std::memory_order_release); };
	const QMetaObject::Connection frameConnection = QObject::connect(
		guardedWindow.data(), &QQuickWindow::frameSwapped, &frameContext, markFrameCompleted, Qt::DirectConnection);
	const QMetaObject::Connection frameEndConnection = QObject::connect(
		guardedWindow.data(), &QQuickWindow::afterFrameEnd, &frameContext, markFrameCompleted, Qt::DirectConnection);
	const QMetaObject::Connection renderingConnection = QObject::connect(
		guardedWindow.data(), &QQuickWindow::afterRendering, &frameContext, markFrameCompleted, Qt::DirectConnection);
	const QMetaObject::Connection animatingConnection = QObject::connect(
		guardedWindow.data(), &QQuickWindow::afterAnimating, &frameContext, markFrameCompleted, Qt::DirectConnection);
	// Some fixture transitions (notably focus changes in detached tool windows)
	// can finish their scheduled frame before these completion hooks are connected.
	// Request a fresh frame only after all hooks are live so the bounded wait cannot
	// miss the sole presentation and time out on an otherwise idle scene graph.
	guardedWindow->requestUpdate();
	QElapsedTimer presentationElapsed;
	presentationElapsed.start();
	int frameEventTurns = 0;
	while (guardedWindow && !presented->load(std::memory_order_acquire)
		   && presentationElapsed.elapsed() < presentationTimeoutMilliseconds) {
		++frameEventTurns;
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 50);
		if (!presented->load(std::memory_order_acquire)) QThread::msleep(1);
	}
	QObject::disconnect(frameConnection);
	QObject::disconnect(frameEndConnection);
	QObject::disconnect(renderingConnection);
	QObject::disconnect(animatingConnection);
	if (!guardedWindow) {
		if (error) *error = QStringLiteral("The Qt Quick window was destroyed before presenting a frame.");
		return false;
	}
	if (!presented->load(std::memory_order_acquire) && error) {
		*error = QStringLiteral("Timed out waiting for a completed Qt Quick scene-graph frame after %1 bounded event turns.")
			.arg(frameEventTurns);
	}
	return presented->load(std::memory_order_acquire);
}
