// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernUiAutomationServer.h"

#include "CertService.h"
#include "ChatPerfTrace.h"
#include "ClientUser.h"
#include "FeedbackReport.h"
#include "Global.h"
#include "GlobalShortcut.h"
#include "MainWindow.h"
#include "ModernDialogController.h"
#include "ModernProductDialogStateFactory.h"
#include "ModernRecorderController.h"
#include "QmlClientModels.h"
#include "QmlImageProvider.h"
#include "QmlAccessibilitySnapshot.h"
#include "QmlPerformanceMonitor.h"
#include "QmlShellHost.h"
#include "QmlThemeController.h"
#include "QmlVisualFixtureController.h"
#include "MumbleConstants.h"
#include "Net.h"
#include "OSInfo.h"
#include "PersistentChatController.h"
#include "Settings.h"
#include "ScreenShareViewBackend.h"
#include "ServerHandler.h"
#include "Version.h"
#include "VersionCheck.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QBuffer>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHash>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMetaType>
#include <QtCore/QPointer>
#include <QtCore/QRectF>
#include <QtCore/QReadLocker>
#include <QtCore/QSet>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtGui/QClipboard>
#include <QtGui/QGuiApplication>
#include <QtGui/QPainter>
#include <QtGui/QWindow>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtQuick/QQuickItem>
#include <QtQuick/QQuickWindow>
#include <QtNetwork/QTcpSocket>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

#include <cmath>
#include <limits>
#include <memory>

namespace {
	QVariantMap errorResponse(const QString &message) {
		QVariantMap response;
		response.insert(QStringLiteral("ok"), false);
		response.insert(QStringLiteral("error"), message);
		return response;
	}

	QVariantMap okResponse() {
		QVariantMap response;
		response.insert(QStringLiteral("ok"), true);
		return response;
	}

	qulonglong unsignedLongLongValue(const QVariant &value, bool *ok) {
		if (!ok) {
			bool ignored = false;
			return unsignedLongLongValue(value, &ignored);
		}

		*ok = false;
		if (value.canConvert< qulonglong >()) {
			const qulonglong result = value.toULongLong(ok);
			if (*ok) {
				return result;
			}
		}

		const QString text = value.toString().trimmed();
		if (!text.isEmpty()) {
			return text.toULongLong(ok);
		}

		return 0;
	}

	bool findNativeContextActionIndexByLabel(const QVariantList &items, const QString &label, int *actionIndex) {
		if (!actionIndex) {
			return false;
		}

		const QString target = label.trimmed();
		if (target.isEmpty()) {
			return false;
		}

		for (const QVariant &itemValue : items) {
			const QVariantMap item = itemValue.toMap();
			if (item.value(QStringLiteral("kind")).toString() == QLatin1String("action")
				&& item.value(QStringLiteral("label")).toString().trimmed().compare(target, Qt::CaseInsensitive) == 0) {
				bool ok          = false;
				const int index  = item.value(QStringLiteral("actionIndex")).toInt(&ok);
				*actionIndex     = ok ? index : -1;
				return ok && index >= 0;
			}

			const QVariantList children = item.value(QStringLiteral("items")).toList();
			if (!children.isEmpty() && findNativeContextActionIndexByLabel(children, target, actionIndex)) {
				return true;
			}
		}

		return false;
	}

	unsigned int firstAutomationDirectMessagePeerSession() {
		const unsigned int selfSession = Global::get().uiSession;
		QReadLocker locker(&ClientUser::c_qrwlUsers);
		for (ClientUser *user : ClientUser::c_qmUsers) {
			if (user && user->uiSession != 0 && user->uiSession != selfSession) {
				return user->uiSession;
			}
		}

		return 0;
	}

	QVariantMap automationConnectionStateProbe(const QString &variant) {
		const QString normalized = variant.trimmed().toLower();
		QVariantMap state;
		if (normalized == QLatin1String("retrying")) {
			state.insert(QStringLiteral("connectionState"), QStringLiteral("retrying"));
			state.insert(QStringLiteral("connectionLabel"), QObject::tr("Retry in 12s"));
			state.insert(QStringLiteral("connectionTone"), QStringLiteral("retry"));
			state.insert(QStringLiteral("connectionTooltip"), QObject::tr("Automatic reconnect is scheduled"));
			state.insert(QStringLiteral("connectionRetryRemainingMs"), 12000);
			state.insert(QStringLiteral("canConnect"), false);
			state.insert(QStringLiteral("canDisconnect"), false);
			state.insert(QStringLiteral("canCancelConnection"), true);
			return state;
		}
		if (normalized == QLatin1String("connecting")) {
			state.insert(QStringLiteral("connectionState"), QStringLiteral("connecting"));
			state.insert(QStringLiteral("connectionLabel"), QObject::tr("Connecting"));
			state.insert(QStringLiteral("connectionTone"), QStringLiteral("warning"));
			state.insert(QStringLiteral("connectionTooltip"), QObject::tr("Connecting to server"));
			state.insert(QStringLiteral("canConnect"), false);
			state.insert(QStringLiteral("canDisconnect"), false);
			state.insert(QStringLiteral("canCancelConnection"), true);
			return state;
		}
		if (normalized == QLatin1String("disconnected")) {
			state.insert(QStringLiteral("connectionState"), QStringLiteral("disconnected"));
			state.insert(QStringLiteral("connectionLabel"), QObject::tr("Disconnected"));
			state.insert(QStringLiteral("connectionTone"), QStringLiteral("danger"));
			state.insert(QStringLiteral("connectionTooltip"), QObject::tr("Open the server browser to reconnect."));
			state.insert(QStringLiteral("canConnect"), true);
			state.insert(QStringLiteral("canDisconnect"), false);
			state.insert(QStringLiteral("canCancelConnection"), false);
			return state;
		}
		return state;
	}

	QVariantMap automationScreenShareStateProbe(const QString &variant, const QString &scopeToken) {
		const QString normalized = variant.trimmed().toLower();
		QVariantMap state;
		if (scopeToken.trimmed().isEmpty()) {
			return state;
		}

		state.insert(QStringLiteral("visible"), true);
		state.insert(QStringLiteral("available"), true);
		state.insert(QStringLiteral("ownerLabel"), QString());
		state.insert(QStringLiteral("ownerSession"), 0);
		state.insert(QStringLiteral("streamId"), QString());
		state.insert(QStringLiteral("detachedWindowOpen"), false);
		state.insert(QStringLiteral("usingFallback"), false);
		state.insert(QStringLiteral("fallbackLabel"), QString());
		state.insert(QStringLiteral("primaryActionId"), QString());
		state.insert(QStringLiteral("primaryLabel"), QString());
		state.insert(QStringLiteral("primaryEnabled"), false);
		state.insert(QStringLiteral("primaryHint"), QString());
		state.insert(QStringLiteral("primaryTone"), QString());
		state.insert(QStringLiteral("overflowActions"), QVariantList());
		state.insert(QStringLiteral("badgeLabel"), QString());
		state.insert(QStringLiteral("badgeTone"), QString());
		state.insert(QStringLiteral("scopeToken"), scopeToken.trimmed());

		if (normalized == QLatin1String("idle")) {
			state.insert(QStringLiteral("mode"), QStringLiteral("idle"));
			state.insert(QStringLiteral("statusLabel"), QObject::tr("Ready to share"));
			state.insert(QStringLiteral("statusTone"), QStringLiteral("success"));
			state.insert(QStringLiteral("primaryActionId"), QStringLiteral("screenShareStart"));
			state.insert(QStringLiteral("primaryLabel"), QObject::tr("Share screen"));
			state.insert(QStringLiteral("primaryEnabled"), true);
			state.insert(QStringLiteral("primaryHint"), QObject::tr("Start screen sharing in this voice room."));
			state.insert(QStringLiteral("primaryTone"), QStringLiteral("success"));
			return state;
		}
		if (normalized == QLatin1String("publishing")) {
			state.insert(QStringLiteral("mode"), QStringLiteral("publishing"));
			state.insert(QStringLiteral("ownerLabel"), QObject::tr("You"));
			state.insert(QStringLiteral("ownerSession"), static_cast< qulonglong >(Global::get().uiSession));
			state.insert(QStringLiteral("streamId"), QStringLiteral("automation-screen-share"));
			state.insert(QStringLiteral("detachedWindowOpen"), true);
			state.insert(QStringLiteral("statusLabel"), QObject::tr("You are sharing in this room"));
			state.insert(QStringLiteral("statusTone"), QStringLiteral("success"));
			state.insert(QStringLiteral("primaryActionId"), QStringLiteral("screenShareOpenWindow"));
			state.insert(QStringLiteral("primaryLabel"), QObject::tr("Open share window"));
			state.insert(QStringLiteral("primaryEnabled"), true);
			state.insert(QStringLiteral("primaryHint"), QObject::tr("Open the active screen-share window."));
			state.insert(QStringLiteral("primaryTone"), QStringLiteral("success"));
			state.insert(QStringLiteral("badgeLabel"), QObject::tr("Live"));
			state.insert(QStringLiteral("badgeTone"), QStringLiteral("success"));
			return state;
		}
		if (normalized == QLatin1String("available")) {
			state.insert(QStringLiteral("mode"), QStringLiteral("available"));
			state.insert(QStringLiteral("ownerLabel"), QObject::tr("Kira Mockup"));
			state.insert(QStringLiteral("ownerSession"), static_cast< qulonglong >(9001));
			state.insert(QStringLiteral("streamId"), QStringLiteral("automation-screen-share"));
			state.insert(QStringLiteral("statusLabel"), QObject::tr("Kira Mockup is sharing in this room"));
			state.insert(QStringLiteral("statusTone"), QStringLiteral("accent"));
			state.insert(QStringLiteral("primaryActionId"), QStringLiteral("screenShareWatch"));
			state.insert(QStringLiteral("primaryLabel"), QObject::tr("Watch share"));
			state.insert(QStringLiteral("primaryEnabled"), true);
			state.insert(QStringLiteral("primaryHint"), QObject::tr("Watch this room's screen share."));
			state.insert(QStringLiteral("primaryTone"), QStringLiteral("accent"));
			state.insert(QStringLiteral("badgeLabel"), QObject::tr("Live"));
			state.insert(QStringLiteral("badgeTone"), QStringLiteral("accent"));
			return state;
		}
		if (normalized == QLatin1String("fallback")) {
			state.insert(QStringLiteral("mode"), QStringLiteral("fallback"));
			state.insert(QStringLiteral("ownerLabel"), QObject::tr("Kira Mockup"));
			state.insert(QStringLiteral("ownerSession"), static_cast< qulonglong >(9001));
			state.insert(QStringLiteral("streamId"), QStringLiteral("automation-screen-share"));
			state.insert(QStringLiteral("detachedWindowOpen"), true);
			state.insert(QStringLiteral("usingFallback"), true);
			state.insert(QStringLiteral("fallbackLabel"), QObject::tr("Using helper/browser fallback."));
			state.insert(QStringLiteral("statusLabel"), QObject::tr("Watching Kira Mockup's share"));
			state.insert(QStringLiteral("statusTone"), QStringLiteral("warning"));
			state.insert(QStringLiteral("primaryActionId"), QStringLiteral("screenShareOpenWindow"));
			state.insert(QStringLiteral("primaryLabel"), QObject::tr("Open share window"));
			state.insert(QStringLiteral("primaryEnabled"), true);
			state.insert(QStringLiteral("primaryHint"), QObject::tr("Open the fallback screen-share window."));
			state.insert(QStringLiteral("primaryTone"), QStringLiteral("warning"));
			state.insert(QStringLiteral("badgeLabel"), QObject::tr("Live"));
			state.insert(QStringLiteral("badgeTone"), QStringLiteral("warning"));
			return state;
		}
		return QVariantMap();
	}

	QVariantMap automationDialogAction(const QString &id, const QString &label, const QString &tone = QString(),
									   const bool closesDialog = true) {
		QVariantMap action;
		action.insert(QStringLiteral("kind"), QStringLiteral("action"));
		action.insert(QStringLiteral("id"), id);
		action.insert(QStringLiteral("label"), label);
		action.insert(QStringLiteral("enabled"), true);
		action.insert(QStringLiteral("checked"), false);
		action.insert(QStringLiteral("closesDialog"), closesDialog);
		if (!tone.isEmpty()) {
			action.insert(QStringLiteral("tone"), tone);
		}
		return action;
	}

	QVariantMap automationDialogField(const QString &id, const QString &label, const QString &type,
									  const QVariant &value = QVariant(), const bool enabled = true) {
		QVariantMap field;
		field.insert(QStringLiteral("id"), id);
		field.insert(QStringLiteral("label"), label);
		field.insert(QStringLiteral("type"), type);
		field.insert(QStringLiteral("value"), value);
		field.insert(QStringLiteral("enabled"), enabled);
		return field;
	}

	QVariantMap automationPathPickerField(const QString &id, const QString &label, const QString &value,
										  const QString &browseActionID, const QString &browseLabel,
										  const bool enabled = true) {
		QVariantMap field = automationDialogField(id, label, QStringLiteral("pathPicker"), value, enabled);
		field.insert(QStringLiteral("browseActionId"), browseActionID);
		field.insert(QStringLiteral("browseLabel"), browseLabel);
		return field;
	}

	QVariantMap automationReadonlyField(const QString &label, const QVariant &value) {
		return automationDialogField(QString(), label, QStringLiteral("readonly"), value, false);
	}

	QVariantMap automationHiddenField(const QString &id, const QVariant &value) {
		return automationDialogField(id, QString(), QStringLiteral("hidden"), value, false);
	}

	QVariantMap automationNoteField(const QString &text) {
		QVariantMap field;
		field.insert(QStringLiteral("type"), QStringLiteral("note"));
		field.insert(QStringLiteral("text"), text);
		return field;
	}

	QVariantMap automationSection(const QString &title, const QVariantList &fields,
								  const QString &presentation = QString(), const QString &subtitle = QString()) {
		QVariantMap section;
		section.insert(QStringLiteral("title"), title);
		section.insert(QStringLiteral("fields"), fields);
		if (!presentation.isEmpty()) {
			section.insert(QStringLiteral("presentation"), presentation);
		}
		if (!subtitle.isEmpty()) {
			section.insert(QStringLiteral("subtitle"), subtitle);
		}
		return section;
	}

	QVariantMap automationSelectOption(const QString &label, const QVariant &value, const bool enabled = true) {
		QVariantMap option;
		option.insert(QStringLiteral("label"), label);
		option.insert(QStringLiteral("value"), value);
		option.insert(QStringLiteral("enabled"), enabled);
		return option;
	}

	QVariantMap automationSelectField(const QString &id, const QString &label, const QVariant &value,
									  const QVariantList &options,
									  const QString &valueType = QStringLiteral("number")) {
		QVariantMap field = automationDialogField(id, label, QStringLiteral("select"), value);
		field.insert(QStringLiteral("options"), options);
		field.insert(QStringLiteral("valueType"), valueType);
		return field;
	}

	QVariantMap automationResultListField(const QString &id, const QString &label, const QVariantList &items,
										  const QString &emptyText) {
		QVariantMap field = automationDialogField(id, label, QStringLiteral("resultList"), items, false);
		field.insert(QStringLiteral("items"), items);
		field.insert(QStringLiteral("emptyText"), emptyText);
		return field;
	}

	QVariantMap automationSearchResult(const QString &type, const int id, const int index, const QString &title,
									   const QString &subtitle, const int matchStart, const int matchLength,
									   const QString &primaryAction, const QString &secondaryAction) {
		QVariantMap item;
		item.insert(QStringLiteral("type"), type);
		item.insert(QStringLiteral("id"), id);
		item.insert(QStringLiteral("index"), index);
		item.insert(QStringLiteral("title"), title);
		item.insert(QStringLiteral("subtitle"), subtitle);
		item.insert(QStringLiteral("matchStart"), matchStart);
		item.insert(QStringLiteral("matchLength"), matchLength);
		item.insert(QStringLiteral("primaryAction"), primaryAction);
		item.insert(QStringLiteral("secondaryAction"), secondaryAction);
		return item;
	}

	QVariantMap automationAclPermission(const int id, const QString &label) {
		QVariantMap item;
		item.insert(QStringLiteral("id"), id);
		item.insert(QStringLiteral("label"), label);
		return item;
	}

	QVariantMap automationAclGroup(const QString &name, const bool inherit, const bool inheritable,
								   const bool inherited, const QVariantList &add, const QVariantList &remove,
								   const QVariantList &inheritedMembers = QVariantList()) {
		QVariantMap item;
		item.insert(QStringLiteral("name"), name);
		item.insert(QStringLiteral("inherit"), inherit);
		item.insert(QStringLiteral("inheritable"), inheritable);
		item.insert(QStringLiteral("inherited"), inherited);
		item.insert(QStringLiteral("add"), add);
		item.insert(QStringLiteral("remove"), remove);
		item.insert(QStringLiteral("inheritedMembers"), inheritedMembers);
		return item;
	}

	QVariantMap automationAclRule(const QString &targetType, const QString &target, const int userID,
								  const bool inherited, const bool applyHere, const bool applySubs,
								  const QVariantList &allow, const QVariantList &deny, const bool expanded = false) {
		QVariantMap item;
		item.insert(QStringLiteral("targetType"), targetType);
		item.insert(QStringLiteral("target"), target);
		item.insert(QStringLiteral("userId"), userID);
		item.insert(QStringLiteral("inherited"), inherited);
		item.insert(QStringLiteral("applyHere"), applyHere);
		item.insert(QStringLiteral("applySubs"), applySubs);
		item.insert(QStringLiteral("allow"), allow);
		item.insert(QStringLiteral("deny"), deny);
		item.insert(QStringLiteral("expanded"), expanded);
		return item;
	}

	QVariantMap automationHighlight(const QString &label, const QVariant &value, const QString &tone = QString()) {
		QVariantMap highlight;
		highlight.insert(QStringLiteral("label"), label);
		highlight.insert(QStringLiteral("value"), value);
		if (!tone.isEmpty()) {
			highlight.insert(QStringLiteral("tone"), tone);
		}
		return highlight;
	}

	QVariantMap automationDialogFromSections(const QString &id, const QString &kind, const QString &title,
											 const QString &subtitle, const QVariantList &sections,
											 const QVariantList &actions, const QString &primaryActionID,
											 const QString &tone = QString(), const QSize &size = QSize()) {
		QVariantMap dialog;
		dialog.insert(QStringLiteral("id"), id);
		dialog.insert(QStringLiteral("kind"), kind);
		dialog.insert(QStringLiteral("title"), title);
		dialog.insert(QStringLiteral("subtitle"), subtitle);
		dialog.insert(QStringLiteral("sections"), sections);
		dialog.insert(QStringLiteral("actions"), actions);
		dialog.insert(QStringLiteral("primaryActionId"), primaryActionID);
		if (!tone.isEmpty()) {
			dialog.insert(QStringLiteral("tone"), tone);
		}
		if (size.isValid()) {
			dialog.insert(QStringLiteral("width"), size.width());
			dialog.insert(QStringLiteral("height"), size.height());
		}
		return dialog;
	}

	QVariantMap automationDialog(const QString &id, const QString &kind, const QString &title,
								 const QString &subtitle, const QVariantList &fields, const QVariantList &actions,
								 const QString &primaryActionID, const QString &tone = QString(),
								 const QSize &size = QSize()) {
		return automationDialogFromSections(
			id, kind, title, subtitle, QVariantList { automationSection(QObject::tr("Confirmation"), fields) },
			actions, primaryActionID, tone, size);
	}

	QVariantMap automationUserDialogProbe(const QString &variant, const unsigned int session,
										  const QString &userName) {
		const QString displayName = userName.trimmed().isEmpty() ? QObject::tr("Demo User") : userName.trimmed();
		const QVariantMap cancel =
			automationDialogAction(QStringLiteral("cancel"), QObject::tr("Cancel"), QString(), true);
		if (variant == QLatin1String("selfRegister")) {
			return automationDialog(
				QStringLiteral("selfRegister"), QStringLiteral("confirm"),
				QObject::tr("Register yourself as %1").arg(displayName),
				QObject::tr("This action cannot be undone and your username cannot be changed afterwards."),
				QVariantList { automationHiddenField(QStringLiteral("session"), session),
							   automationReadonlyField(QObject::tr("Username"), displayName),
							   automationNoteField(QObject::tr("You will forever be known as this username on the current server.")) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("confirmSelfRegister"),
													  QObject::tr("Register"), QStringLiteral("accent"), true) },
				QStringLiteral("confirmSelfRegister"), QString(), QSize(520, 300));
		}
		if (variant == QLatin1String("register")) {
			return automationDialog(
				QStringLiteral("registerUser:%1").arg(session), QStringLiteral("confirm"),
				QObject::tr("Register user %1").arg(displayName),
				QObject::tr("This permanently binds the current certificate for this user to their server account."),
				QVariantList { automationHiddenField(QStringLiteral("session"), session),
							   automationReadonlyField(QObject::tr("Username"), displayName),
							   automationNoteField(QObject::tr("The username cannot be changed after registration from this dialog.")) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("confirmRegisterUser"),
													  QObject::tr("Register"), QStringLiteral("accent"), true) },
				QStringLiteral("confirmRegisterUser"), QString(), QSize(560, 320));
		}
		if (variant == QLatin1String("kick")) {
			return automationDialog(
				QStringLiteral("kickUser:%1").arg(session), QStringLiteral("confirm"),
				QObject::tr("Kick %1").arg(displayName),
				QObject::tr("Optionally include a reason that will be sent to the user."),
				QVariantList { automationHiddenField(QStringLiteral("session"), session),
							   automationReadonlyField(QObject::tr("User"), displayName),
							   automationDialogField(QStringLiteral("reason"), QObject::tr("Reason"),
													 QStringLiteral("text"), QString()) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("confirmKick"), QObject::tr("Kick"),
													  QStringLiteral("danger"), true) },
				QStringLiteral("confirmKick"), QStringLiteral("danger"), QSize(520, 320));
		}
		if (variant == QLatin1String("ban")) {
			return automationDialog(
				QStringLiteral("banUser:%1").arg(session), QStringLiteral("confirm"),
				QObject::tr("Ban %1").arg(displayName),
				QObject::tr("Choose what to ban and optionally include a reason."),
				QVariantList { automationHiddenField(QStringLiteral("session"), session),
							   automationReadonlyField(QObject::tr("User"), displayName),
							   automationDialogField(QStringLiteral("reason"), QObject::tr("Reason"),
													 QStringLiteral("text"), QString()),
							   automationDialogField(QStringLiteral("banCertificate"),
													 QObject::tr("Ban certificate"), QStringLiteral("checkbox"), true),
							   automationDialogField(QStringLiteral("banIP"), QObject::tr("Ban IP"),
													 QStringLiteral("checkbox"), false) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("confirmBan"), QObject::tr("Ban"),
													  QStringLiteral("danger"), true) },
				QStringLiteral("confirmBan"), QStringLiteral("danger"), QSize(560, 390));
		}
		if (variant == QLatin1String("commentReset")) {
			return automationDialog(
				QStringLiteral("resetComment:%1").arg(session), QStringLiteral("confirm"),
				QObject::tr("Reset user comment"),
				QObject::tr("Reset the server-side comment for %1.").arg(displayName),
				QVariantList { automationHiddenField(QStringLiteral("session"), session),
							   automationReadonlyField(QObject::tr("User"), displayName) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("confirmResetComment"),
													  QObject::tr("Reset"), QStringLiteral("danger"), true) },
				QStringLiteral("confirmResetComment"), QStringLiteral("danger"), QSize(520, 260));
		}
		if (variant == QLatin1String("userComment")) {
			return automationDialogFromSections(
				QStringLiteral("userComment:%1").arg(session), QStringLiteral("info"),
				QObject::tr("User comment"), QObject::tr("Comment for %1.").arg(displayName),
				QVariantList { automationSection(
					QObject::tr("Comment"),
					QVariantList { automationReadonlyField(QObject::tr("User"), displayName),
								   automationReadonlyField(
									   QObject::tr("Comment"),
									   QObject::tr("Demo profile comment used for Modern dialog visual review.")) }) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"),
													  QStringLiteral("accent"), true) },
				QStringLiteral("close"), QString(), QSize(680, 520));
		}
		if (variant == QLatin1String("textureReset")) {
			return automationDialog(
				QStringLiteral("resetAvatar:%1").arg(session), QStringLiteral("confirm"),
				QObject::tr("Reset avatar"),
				QObject::tr("Reset the server-side avatar for %1.").arg(displayName),
				QVariantList { automationHiddenField(QStringLiteral("session"), session),
							   automationReadonlyField(QObject::tr("User"), displayName) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("confirmResetAvatar"),
													  QObject::tr("Reset"), QStringLiteral("danger"), true) },
				QStringLiteral("confirmResetAvatar"), QStringLiteral("danger"), QSize(520, 260));
		}
		if (variant == QLatin1String("userInformation")) {
			return automationDialogFromSections(
				QStringLiteral("userInformation:%1").arg(session), QStringLiteral("info"),
				QObject::tr("User information"), QObject::tr("Statistics for %1.").arg(displayName),
				QVariantList {
					automationSection(QObject::tr("Identity"),
									  QVariantList { automationReadonlyField(QObject::tr("User"), displayName),
													 automationReadonlyField(QObject::tr("Session"), session),
													 automationReadonlyField(QObject::tr("Version"),
																			 QObject::tr("Mumble 1.7.0 DEV")),
													 automationReadonlyField(QObject::tr("Strong certificate"),
																			 QObject::tr("Yes")) },
									  QStringLiteral("list")),
					automationSection(QObject::tr("Connection"),
									  QVariantList { automationReadonlyField(QObject::tr("TCP ping"),
																			 QObject::tr("42 ms")),
													 automationReadonlyField(QObject::tr("UDP ping"),
																			 QObject::tr("38 ms")),
													 automationReadonlyField(QObject::tr("Bandwidth"),
																			 QObject::tr("72 kbit/s")) },
									  QStringLiteral("list")) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"),
													  QStringLiteral("accent"), true) },
				QStringLiteral("close"), QString(), QSize(720, 560));
		}
		if (variant == QLatin1String("localNickname")) {
			return automationDialog(
				QStringLiteral("localNickname:%1").arg(session), QStringLiteral("form"),
				QObject::tr("Local nickname for %1").arg(displayName),
				QObject::tr("This nickname is stored locally for your client."),
				QVariantList { automationHiddenField(QStringLiteral("session"), session),
							   automationReadonlyField(QObject::tr("Server name"), displayName),
							   automationDialogField(QStringLiteral("nickname"), QObject::tr("Local nickname"),
													 QStringLiteral("text"), QString()) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("saveLocalNickname"),
													  QObject::tr("Save"), QStringLiteral("accent"), true) },
				QStringLiteral("saveLocalNickname"), QString(), QSize(640, 430));
		}
		if (variant == QLatin1String("chatHistoryGrant")) {
			constexpr int registeredUserID = 8;
			constexpr quint64 connectionGeneration = 1;
			const QVariantList scopeOptions {
				automationSelectOption(QObject::tr("Current voice room: Root"), QStringLiteral("0:0")),
				automationSelectOption(QObject::tr("Server-wide chat"), QStringLiteral("2:0"))
			};
			const QVariantList windowOptions {
				automationSelectOption(QObject::tr("From now"), 0),
				automationSelectOption(QObject::tr("5 days back"), 5),
				automationSelectOption(QObject::tr("30 days back"), 30),
				automationSelectOption(QObject::tr("All history"), -2),
				automationSelectOption(QObject::tr("Revoke access"), -3)
			};
			return automationDialogFromSections(
				QStringLiteral("chatHistoryGrant:%1").arg(session), QStringLiteral("form"),
				QObject::tr("Grant chat history"),
				QObject::tr("Grant or revoke persistent chat history access for %1.").arg(displayName),
				QVariantList { automationSection(
					QObject::tr("Access"),
					QVariantList {
						automationHiddenField(QStringLiteral("session"), session),
						automationHiddenField(QStringLiteral("persistentUserId"), registeredUserID),
						automationHiddenField(QStringLiteral("connectionGeneration"), connectionGeneration),
						automationReadonlyField(QObject::tr("User"), displayName),
						automationSelectField(QStringLiteral("history.scope"), QObject::tr("Scope"),
											  QStringLiteral("0:0"), scopeOptions, QStringLiteral("string")),
						automationSelectField(QStringLiteral("history.window"), QObject::tr("Window"), 5,
											  windowOptions),
						automationDialogField(QStringLiteral("history.customDays"), QObject::tr("Custom days"),
											  QStringLiteral("number"), 30) }) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("saveChatHistoryGrant"),
												  QObject::tr("Apply"), QStringLiteral("accent"), false) },
				QStringLiteral("saveChatHistoryGrant"), QString(), QSize(700, 560));
		}

		return {};
	}

	QVariantMap automationAppDialogProbe(const QString &variant, const QString &userName) {
		const QVariantMap cancel =
			automationDialogAction(QStringLiteral("cancel"), QObject::tr("Cancel"), QString(), true);
		if (variant == QLatin1String("createRoom") || variant == QLatin1String("createRoomValidation")) {
			const bool validation = variant == QLatin1String("createRoomValidation");
			const QVariantList roomTypeOptions {
				automationSelectOption(QObject::tr("Voice room"), 0),
				automationSelectOption(QObject::tr("Text room"), 1)
			};
			const QVariantList rootOptions {
				automationSelectOption(QObject::tr("Root"), 0)
			};
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("createRoom"), QStringLiteral("form"), QObject::tr("Create room"),
				QObject::tr("Create a voice room or a persistent text room without leaving Modern layout."),
				QVariantList { automationSection(
					QObject::tr("Room"),
					QVariantList {
						automationSelectField(QStringLiteral("room.type"), QObject::tr("Type"), 0, roomTypeOptions),
						automationDialogField(QStringLiteral("room.name"), QObject::tr("Name"),
											  QStringLiteral("text"),
											  validation ? QString() : QObject::tr("Demo room")),
						automationDialogField(QStringLiteral("room.description"), QObject::tr("Topic"),
											  QStringLiteral("textarea"),
											  QObject::tr("Modern UI review room created from automation probe.")),
						automationSelectField(QStringLiteral("voice.parent"), QObject::tr("Voice parent"), 0,
											  rootOptions),
						automationDialogField(QStringLiteral("voice.position"), QObject::tr("Voice order"),
											  QStringLiteral("number"), 0),
						automationDialogField(QStringLiteral("voice.temporary"),
											  QObject::tr("Temporary voice room"), QStringLiteral("checkbox"),
											  false),
						automationDialogField(QStringLiteral("voice.maxUsers"), QObject::tr("Max voice users"),
											  QStringLiteral("number"), 0),
						automationSelectField(QStringLiteral("text.visibility"),
											  QObject::tr("Text visibility source"), 0, rootOptions),
						automationDialogField(QStringLiteral("text.position"), QObject::tr("Text order"),
											  QStringLiteral("number"), 0) }) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("createRoom"), QObject::tr("Create"),
													  QStringLiteral("accent"), false) },
				QStringLiteral("createRoom"), QString(), QSize(720, 620));
			if (validation) {
				dialog.insert(QStringLiteral("errors"),
							  QVariantMap { { QStringLiteral("room.name"), QObject::tr("Enter a room name.") } });
				dialog.insert(QStringLiteral("highlights"),
							  QVariantList { automationHighlight(QObject::tr("Validation"),
																 QObject::tr("Name required"),
																 QStringLiteral("warning")) });
			}
			return dialog;
		}
		if (variant == QLatin1String("voiceRecorder") || variant == QLatin1String("voiceRecorderActive")) {
			const bool active = variant == QLatin1String("voiceRecorderActive");
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("voiceRecorder"), QStringLiteral("recorder"), QObject::tr("Voice recorder"),
				QObject::tr("Record the current voice session."), QVariantList(),
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"),
					QString(), true) }, QStringLiteral("close"), QString(), QSize(760, 700));
			dialog.insert(QStringLiteral("initialFocusId"), active
				? QStringLiteral("recorderPauseButton") : QStringLiteral("recording.path"));
			return dialog;
		}
		if (variant == QLatin1String("selfComment")) {
			const QString displayName =
				userName.trimmed().isEmpty() ? QObject::tr("Current user") : userName.trimmed();
			return automationDialogFromSections(
				QStringLiteral("selfComment"), QStringLiteral("form"), QObject::tr("My comment"),
				QObject::tr("Edit the comment shown on your user profile."),
				QVariantList { automationSection(
					QObject::tr("Comment"),
					QVariantList {
						automationHiddenField(QStringLiteral("session"), 1),
						automationReadonlyField(QObject::tr("User"), displayName),
						automationDialogField(QStringLiteral("comment"), QObject::tr("Comment"),
											  QStringLiteral("textarea"),
											  QObject::tr("Modern profile comment used for UI review.")) }) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("saveSelfComment"), QObject::tr("Save"),
													  QStringLiteral("accent"), true) },
				QStringLiteral("saveSelfComment"), QString(), QSize(680, 520));
		}
		if (variant == QLatin1String("certificate") || variant == QLatin1String("certificateEmailError")
			|| variant == QLatin1String("certificateImportError")) {
			const bool emailError  = variant == QLatin1String("certificateEmailError");
			const bool importError = variant == QLatin1String("certificateImportError");
			Mumble::ModernProductDialogs::CertificateDialogInput input;
			input.certificate = { true, QObject::tr("You"), QObject::tr("None"), QObject::tr("You"),
				QStringLiteral("2042-04-06T11:51:48"),
				QStringLiteral("7B:51:90:1E:3A:76:44:9C:DA:EE:20:78:33:8F:99:91:2D:14:5E:A4"),
				QStringLiteral("2042-04-06") };
			if (emailError) {
				input.fieldValues = { { QStringLiteral("cert.mode"), QStringLiteral("create") },
					{ QStringLiteral("cert.name"), QObject::tr("Design Review") },
					{ QStringLiteral("cert.email"), QStringLiteral("not an email address") } };
				input.errors = { { QStringLiteral("cert.email"),
					QObject::tr("Enter a valid email address or leave it blank.") } };
			} else if (importError) {
				input.fieldValues = { { QStringLiteral("cert.mode"), QStringLiteral("import") },
					{ QStringLiteral("cert.importPath"), QStringLiteral("C:/missing/mumble-cert.p12") },
					{ QStringLiteral("cert.password"), QString() } };
				input.errors = { { QStringLiteral("cert.importPath"),
					QObject::tr("Choose a readable PKCS#12 certificate file.") } };
			} else {
				input.fieldValues = { { QStringLiteral("cert.mode"), QStringLiteral("export") },
					{ QStringLiteral("cert.exportPath"), QStringLiteral("C:/Users/You/Desktop/mumble-cert.p12") } };
			}
			return Mumble::ModernProductDialogs::certificateDialog(input);
		}
		if (variant == QLatin1String("audioStats") || variant == QLatin1String("audioStatsDisconnected")
			|| variant == QLatin1String("audioStatsNoInput")) {
			const bool disconnected = variant == QLatin1String("audioStatsDisconnected");
			const bool noInput      = variant == QLatin1String("audioStatsNoInput");

			QVariantMap meterPayload;
			meterPayload.insert(QStringLiteral("available"), !disconnected && !noInput);
			meterPayload.insert(QStringLiteral("connected"), !disconnected);
			meterPayload.insert(QStringLiteral("transmitting"), !disconnected && !noInput);
			if (!disconnected && !noInput) {
				meterPayload.insert(QStringLiteral("amplitude"), 74);
				meterPayload.insert(QStringLiteral("signalToNoise"), 74);
				meterPayload.insert(QStringLiteral("hybrid"), 74);
				meterPayload.insert(QStringLiteral("peakCleanMicDb"), -18);
			}

			QVariantMap voiceMeter =
				automationDialogField(QStringLiteral("audio.meter"), QObject::tr("Voice level"),
									  QStringLiteral("voiceMeter"), meterPayload);
			voiceMeter.insert(QStringLiteral("sourceLabel"),
							  disconnected ? QObject::tr("Not connected")
										   : (noInput ? QObject::tr("Microphone idle") : QStringLiteral("Yeti X")));
			voiceMeter.insert(QStringLiteral("vadSource"), 0);
			voiceMeter.insert(QStringLiteral("silenceThreshold"), 18);
			voiceMeter.insert(QStringLiteral("speechThreshold"), 62);
			voiceMeter.insert(QStringLiteral("active"), !disconnected);
			voiceMeter.insert(QStringLiteral("staticMeter"), true);

			QVariantList inputFields {
				voiceMeter,
				automationReadonlyField(QObject::tr("Audio bandwidth"),
										disconnected ? QObject::tr("Disconnected")
													 : (noInput ? QObject::tr("Waiting") : QObject::tr("72 kbit/s"))),
				automationReadonlyField(QObject::tr("Packets lost"),
										disconnected ? QObject::tr("Not reported")
													 : (noInput ? QObject::tr("0.0%") : QObject::tr("0.2%")))
			};
			if (disconnected) {
				inputFields.push_back(automationNoteField(
					QObject::tr("Connect to a server to show live input, packet, and network diagnostics.")));
			} else if (noInput) {
				inputFields.push_back(
					automationNoteField(QObject::tr("No microphone input has been observed yet.")));
			}

			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("audioStats"), QStringLiteral("info"), QObject::tr("Audio statistics"),
				disconnected ? QObject::tr("Audio diagnostics are waiting for a server connection.")
							 : (noInput ? QObject::tr("Audio diagnostics are waiting for microphone input.")
										: QObject::tr("Live input, packet, and jitter diagnostics for the selected user.")),
				QVariantList {
					automationSection(QObject::tr("Input"), inputFields),
					automationSection(QObject::tr("Network"),
									  QVariantList {
										  automationReadonlyField(QObject::tr("Ping"),
																  disconnected ? QObject::tr("Disconnected")
																			   : (noInput ? QObject::tr("Waiting")
																						  : QObject::tr("28 ms"))),
										  automationReadonlyField(QObject::tr("Jitter"),
																  disconnected ? QObject::tr("Disconnected")
																			   : (noInput ? QObject::tr("Waiting")
																						  : QObject::tr("2 ms"))),
										  automationReadonlyField(QObject::tr("Codec"), QObject::tr("Opus")) },
									  QStringLiteral("list")) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QString(), true),
							   automationDialogAction(QStringLiteral("resetStats"), QObject::tr("Reset stats"),
													  QString(), true) },
				QStringLiteral("resetStats"), QString(), QSize(700, 560));
			const QString signalText = disconnected ? QObject::tr("Disconnected")
													 : (noInput ? QObject::tr("Idle") : QStringLiteral("-18 dB"));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList {
							  automationHighlight(QObject::tr("User"), QObject::tr("You")),
							  automationHighlight(QObject::tr("Signal"), signalText,
												  (!disconnected && !noInput) ? QStringLiteral("good") : QString()),
							  automationHighlight(QObject::tr("Jitter"),
												  disconnected ? QObject::tr("Disconnected")
															   : (noInput ? QObject::tr("Waiting")
																		  : QObject::tr("2 ms"))) });
			return dialog;
		}
		if (variant == QLatin1String("firstRun")) {
			return automationDialogFromSections(
				QStringLiteral("firstRun:audioInput"), QStringLiteral("settings"), QObject::tr("Set up audio input"),
				QObject::tr("Choose a microphone before joining your first voice room."),
				QVariantList { automationSection(
					QObject::tr("Audio input"),
					QVariantList { automationSelectField(
						QStringLiteral("audio.input.device"), QObject::tr("Input device"), QStringLiteral("default"),
						QVariantList { automationSelectOption(QObject::tr("System default"), QStringLiteral("default")),
									   automationSelectOption(QObject::tr("Test microphone"), QStringLiteral("test")) }),
						automationReadonlyField(QObject::tr("Certificate"), QObject::tr("Modern certificate setup follows next.")) }) },
				QVariantList { automationDialogAction(QStringLiteral("skip"), QObject::tr("Not now"), QString(), true),
							   automationDialogAction(QStringLiteral("continue"), QObject::tr("Continue"), QStringLiteral("accent"), true) },
				QStringLiteral("continue"), QString(), QSize(720, 560));
		}
		if (variant == QLatin1String("plugins") || variant.startsWith(QLatin1String("plugins."))) {
			const QString failure = variant.section(QLatin1Char('.'), 1);
			QVariantMap pluginField;
			pluginField.insert(QStringLiteral("id"), QStringLiteral("plugins.installed"));
			pluginField.insert(QStringLiteral("type"), QStringLiteral("pluginEditor"));
			pluginField.insert(QStringLiteral("label"), QObject::tr("Installed plugins"));
			pluginField.insert(QStringLiteral("rows"), QVariantList { QVariantMap {
				{ QStringLiteral("id"), QStringLiteral("automation-plugin") },
				{ QStringLiteral("name"), QObject::tr("Automation positional plugin") },
				{ QStringLiteral("description"), QObject::tr("Typed plugin administration probe") },
				{ QStringLiteral("version"), QStringLiteral("1.0.0") }, { QStringLiteral("enabled"), true },
				{ QStringLiteral("loaded"), failure != QLatin1String("loadFailure") },
				{ QStringLiteral("positionalAvailable"), true }, { QStringLiteral("positionalEnabled"), true },
				{ QStringLiteral("keyboardMonitoringAllowed"), false },
				{ QStringLiteral("canConfigure"), true }, { QStringLiteral("canShowAbout"), true } } });
			QVariantList fields { pluginField };
			if (!failure.isEmpty()) {
				fields.push_back(automationNoteField(QObject::tr("Plugin operation result: %1").arg(failure)));
			}
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("settings:plugins"), QStringLiteral("settings"), QObject::tr("Plugins"),
				QObject::tr("Manage installed plugins and asynchronous update results."),
				QVariantList { automationSection(QObject::tr("Plugins"), fields) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QString(), true) },
				QStringLiteral("close"), QString(), QSize(920, 720));
			if (!failure.isEmpty()) {
				dialog.insert(QStringLiteral("statusMessage"), QObject::tr("Plugin probe completed with %1.").arg(failure));
				dialog.insert(QStringLiteral("tone"), failure == QLatin1String("partialSuccess") ? QStringLiteral("warning") : QStringLiteral("danger"));
			}
			return dialog;
		}
		return {};
	}

	QVariantMap automationDataStateDialogProbe(const QString &variant) {
		const QVariantMap close =
			automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QStringLiteral("accent"), true);
		const QVariantMap cancel =
			automationDialogAction(QStringLiteral("cancel"), QObject::tr("Cancel"), QString(), true);

		if (variant == QLatin1String("versionCheckAvailable")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("versionCheck"), QStringLiteral("update"), QObject::tr("Update available"),
				QObject::tr("A new mumble-forked build is ready."),
				QVariantList {
					automationSection(
						QObject::tr("Available update"),
						QVariantList {
							automationReadonlyField(QObject::tr("Latest build"),
													QObject::tr("1.7.1, build 42")),
							automationReadonlyField(QObject::tr("Published"),
													QObject::tr("May 30, 2026")),
							automationReadonlyField(QObject::tr("Commit"),
													QStringLiteral("abc1234")),
							automationReadonlyField(
								QObject::tr("Release"),
								QStringLiteral("https://github.com/dankmaster/mumble/releases/tag/mumble-forked")),
							automationReadonlyField(
								QObject::tr("Installer"),
								QStringLiteral("https://github.com/dankmaster/mumble/releases/download/mumble-forked/mumble-forked.msi")),
							automationReadonlyField(
								QObject::tr("SHA256"),
								QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")),
							automationHiddenField(
								QStringLiteral("update.releaseUrl"),
								QStringLiteral("https://github.com/dankmaster/mumble/releases/tag/mumble-forked")),
							automationHiddenField(
								QStringLiteral("update.installerUrl"),
								QStringLiteral("https://github.com/dankmaster/mumble/releases/download/mumble-forked/mumble-forked.msi")),
							automationHiddenField(
								QStringLiteral("update.sha256"),
								QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef")),
							automationHiddenField(QStringLiteral("update.version"), QStringLiteral("1.7.1")),
							automationHiddenField(QStringLiteral("update.build"), 42) },
						QStringLiteral("list"),
						QObject::tr("Security and Modern shell polish are included in this release.")),
					automationSection(QObject::tr("Installed build"),
									  QVariantList {
										  automationReadonlyField(QObject::tr("Current version"),
																  QStringLiteral("1.7.0")),
										  automationReadonlyField(QObject::tr("Current build"), 0),
										  automationReadonlyField(QObject::tr("Release channel"),
																  QStringLiteral("mumble-forked")) },
									  QStringLiteral("list")),
					automationSection(QObject::tr("Release notes"),
									  QVariantList { automationReadonlyField(
										  QObject::tr("Notes"),
										  QObject::tr("Modern dialogs now follow the hardened shell mockup and update checks use the in-app flow.")) },
									  QStringLiteral("list")) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QString(), true),
							   automationDialogAction(QStringLiteral("runVersionCheck"),
													  QObject::tr("Check again"), QString(), false),
							   automationDialogAction(QStringLiteral("openForkRelease"),
													  QObject::tr("Open releases"), QString(), false),
							   automationDialogAction(QStringLiteral("installForkUpdate"),
													  QObject::tr("Install update"), QStringLiteral("accent"),
													  false) },
				QStringLiteral("installForkUpdate"), QString(), QSize(660, 560));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Status"), QObject::tr("Available"),
															 QStringLiteral("warning")),
										 automationHighlight(QObject::tr("Current build"), 0),
										 automationHighlight(QObject::tr("Latest"),
															 QObject::tr("1.7.1, build 42")) });
			return dialog;
		}
		if (variant == QLatin1String("versionCheckCurrent")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("versionCheck"), QStringLiteral("update"), QObject::tr("You're up to date"),
				QObject::tr("This client matches the newest release information."),
				QVariantList {
					automationSection(QObject::tr("Latest release"),
									  QVariantList {
										  automationReadonlyField(QObject::tr("Latest build"),
																  QObject::tr("1.7.0, build 0")),
										  automationReadonlyField(QObject::tr("Published"),
																  QObject::tr("May 30, 2026")),
										  automationHiddenField(
											  QStringLiteral("update.releaseUrl"),
											  QStringLiteral("https://github.com/dankmaster/mumble/releases/tag/mumble-forked")) },
									  QStringLiteral("list"),
									  QObject::tr("No newer mumble-forked build was found.")),
					automationSection(QObject::tr("Installed build"),
									  QVariantList {
										  automationReadonlyField(QObject::tr("Current version"),
																  QStringLiteral("1.7.0")),
										  automationReadonlyField(QObject::tr("Current build"), 0),
										  automationReadonlyField(QObject::tr("Release channel"),
																  QStringLiteral("mumble-forked")) },
									  QStringLiteral("list")) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QString(), true),
							   automationDialogAction(QStringLiteral("runVersionCheck"),
													  QObject::tr("Check again"), QString(), false),
							   automationDialogAction(QStringLiteral("openForkRelease"),
													  QObject::tr("Open releases"), QStringLiteral("accent"),
													  false) },
				QStringLiteral("openForkRelease"), QString(), QSize(660, 520));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Status"), QObject::tr("Current"),
															 QStringLiteral("good")),
										 automationHighlight(QObject::tr("Current build"), 0),
										 automationHighlight(QObject::tr("Latest"),
															 QObject::tr("1.7.0, build 0")) });
			return dialog;
		}
		if (variant == QLatin1String("versionCheckError")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("versionCheck"), QStringLiteral("update"), QObject::tr("Update check failed"),
				QObject::tr("Mumble could not retrieve the latest mumble-forked release information."),
				QVariantList { automationSection(
					QObject::tr("Status"),
					QVariantList {
						automationNoteField(QObject::tr("Mumble failed to retrieve forked update information from GitHub: timeout.")),
						automationReadonlyField(QObject::tr("Current version"), QStringLiteral("1.7.0")),
						automationReadonlyField(QObject::tr("Current build"), 0),
						automationReadonlyField(QObject::tr("Release channel"),
												QStringLiteral("mumble-forked")) },
					QStringLiteral("list")) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QString(), true),
							   automationDialogAction(QStringLiteral("runVersionCheck"),
													  QObject::tr("Try again"), QStringLiteral("accent"),
													  false),
							   automationDialogAction(QStringLiteral("openForkRelease"),
													  QObject::tr("Open releases"), QString(), false) },
				QStringLiteral("runVersionCheck"), QStringLiteral("danger"), QSize(660, 480));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Status"), QObject::tr("Failed"),
															 QStringLiteral("warning")),
										 automationHighlight(QObject::tr("Build"), 0),
										 automationHighlight(QObject::tr("Channel"),
															 QStringLiteral("mumble-forked")) });
			return dialog;
		}
		if (variant == QLatin1String("feedbackBugReport")) {
			const QVariantList typeOptions {
				automationSelectOption(QObject::tr("Bug"), 0),
				automationSelectOption(QObject::tr("Suggestion"), 1),
				automationSelectOption(QObject::tr("Question"), 2)
			};
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("feedback"), QStringLiteral("feedback"), QObject::tr("Report feedback"),
				QObject::tr("Bug reports, suggestions, and questions for this fork."),
				QVariantList {
					automationSection(QObject::tr("Report"),
									  QVariantList {
										  automationSelectField(QStringLiteral("feedback.kind"),
																QObject::tr("Type"), 0, typeOptions),
										  automationDialogField(
											  QStringLiteral("feedback.title"), QObject::tr("Title"),
											  QStringLiteral("text"),
											  QObject::tr("Update window does not match mockup")),
										  automationDialogField(
											  QStringLiteral("feedback.description"), QObject::tr("Description"),
											  QStringLiteral("textarea"),
											  QObject::tr("The update available flow needs to use the same narrow Modern dialog style as the mockup.")),
										  automationDialogField(
											  QStringLiteral("feedback.steps"),
											  QObject::tr("Steps to reproduce"), QStringLiteral("textarea"),
											  QObject::tr("1. Open Help.\n2. Choose Check for updates.\n3. Compare the result dialog with the mockup.")) }),
					automationSection(QObject::tr("Evidence"),
									  QVariantList { automationDialogField(
										  QStringLiteral("feedback.evidence"),
										  QObject::tr("Pasted evidence"), QStringLiteral("textarea"),
										  QObject::tr("Mockup slide 37 and API capture need to match.")) }),
					automationSection(QObject::tr("Diagnostics"),
									  QVariantList {
										  automationDialogField(
											  QStringLiteral("feedback.includeDiagnostics"),
											  QObject::tr("Include diagnostics"), QStringLiteral("checkbox"),
											  true),
										  automationReadonlyField(
											  QObject::tr("Status"),
											  QObject::tr("Submit will use the GitHub fallback because server-side feedback submission is disabled.")),
										  automationReadonlyField(
											  QObject::tr("Diagnostics preview"),
											  QObject::tr("Client: Mumble 1.7.0\nQt: packaged runtime\nServer feedback: fallback")) }) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QString(), true),
							   automationDialogAction(QStringLiteral("toggleFeedbackCapture"),
													  QObject::tr("Start capture"), QString(), false),
							   automationDialogAction(QStringLiteral("copyFeedbackReport"),
													  QObject::tr("Copy report"), QString(), false),
							   automationDialogAction(QStringLiteral("openFeedbackGitHub"),
													  QObject::tr("Open GitHub"), QString(), false),
							   automationDialogAction(QStringLiteral("submitFeedback"), QObject::tr("Submit"),
													  QStringLiteral("accent"), false) },
				QStringLiteral("submitFeedback"), QString(), QSize(900, 720));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Server submit"),
															 QObject::tr("Fallback")),
										 automationHighlight(QObject::tr("Diagnostics"),
															 QObject::tr("Included")),
										 automationHighlight(QObject::tr("Max body"),
															 QObject::tr("58 KiB")) });
			return dialog;
		}
		if (variant == QLatin1String("feedbackFallbackResult")) {
			return automationDialogFromSections(
				QStringLiteral("feedbackResult"), QStringLiteral("feedback"),
				QObject::tr("Feedback fallback opened"),
				QObject::tr("Server submit is unavailable for this report."),
				QVariantList { automationSection(
					QObject::tr("Status"),
					QVariantList {
						automationNoteField(QObject::tr("A prefilled GitHub issue was opened in your browser.")),
						automationReadonlyField(QObject::tr("Title"),
												QObject::tr("Bug: Update window does not match mockup")),
						automationReadonlyField(QObject::tr("Report size"),
												QObject::tr("1420 bytes")) },
					QStringLiteral("list")) },
				QVariantList { automationDialogAction(QStringLiteral("editFeedbackReport"), QObject::tr("Back"),
													  QString(), false),
							   automationDialogAction(QStringLiteral("copyFeedbackFallbackReport"),
													  QObject::tr("Copy report"), QString(), false),
							   automationDialogAction(QStringLiteral("openFeedbackFallbackGitHub"),
													  QObject::tr("Open GitHub"), QStringLiteral("accent"),
													  false),
							   automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QString(),
													  true) },
				QStringLiteral("openFeedbackFallbackGitHub"), QString(), QSize(640, 420));
		}

		if (variant == QLatin1String("sslCertificateWarning")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("sslCertificateWarning"), QStringLiteral("warning"),
				QObject::tr("Server certificate warning"),
				QObject::tr("The server certificate could not be verified."),
				QVariantList {
					automationSection(QObject::tr("Server"),
									  QVariantList {
										  automationHiddenField(QStringLiteral("ssl.host"),
																QStringLiteral("mumble.quistify.com")),
										  automationHiddenField(QStringLiteral("ssl.port"), 64739),
										  automationHiddenField(QStringLiteral("ssl.digest"),
																QStringLiteral("b455f36c5e1d8188f8d2d927f278c1f29e5ad7d8")),
										  automationReadonlyField(QObject::tr("Server"),
																  QStringLiteral("mumble.quistify.com:64739")),
										  automationReadonlyField(
											  QObject::tr("Presented digest"),
											  QStringLiteral("b4:55:f3:6c:5e:1d:81:88:f8:d2:d9:27:f2:78:c1:f2:9e:5a:d7:d8")) },
									  QStringLiteral("list")),
					automationSection(QObject::tr("Certificate"),
									  QVariantList {
										  automationReadonlyField(QObject::tr("Subject"),
																  QStringLiteral("mumble.quistify.com")),
										  automationReadonlyField(QObject::tr("Issuer"),
																  QStringLiteral("Quistify Dev CA")),
										  automationReadonlyField(QObject::tr("Expires"),
																  QStringLiteral("2027-05-31T12:00:00")) },
									  QStringLiteral("list")),
					automationSection(QObject::tr("Verification errors"),
									  QVariantList { automationReadonlyField(
										  QObject::tr("Error 1"),
										  QObject::tr("The certificate is self-signed, and untrusted.")) },
									  QStringLiteral("list")) },
				QVariantList {
					automationDialogAction(QStringLiteral("rejectSslCertificate"), QObject::tr("Reject"), QString(),
										   true),
					automationDialogAction(QStringLiteral("viewSslCertificateDetails"),
										   QObject::tr("View details"), QString(), false),
					automationDialogAction(QStringLiteral("trustSslCertificate"),
										   QObject::tr("Accept certificate"), QStringLiteral("danger"), true) },
				QStringLiteral("rejectSslCertificate"), QStringLiteral("warning"), QSize(760, 620));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Server"),
															 QStringLiteral("mumble.quistify.com:64739")),
										 automationHighlight(QObject::tr("Trust"), QObject::tr("Unverified"),
															 QStringLiteral("warning")),
										 automationHighlight(QObject::tr("Errors"), 1,
															 QStringLiteral("warning")) });
			return dialog;
		}
		if (variant == QLatin1String("sslCertificateDetails")) {
			return automationDialogFromSections(
				QStringLiteral("sslCertificateDetails"), QStringLiteral("warning"),
				QObject::tr("Certificate details"),
				QObject::tr("Review the certificate before deciding whether to trust it for this server."),
				QVariantList { automationSection(
					QObject::tr("Certificate"),
					QVariantList {
						automationReadonlyField(QObject::tr("Server"),
												QStringLiteral("mumble.quistify.com:64739")),
						automationReadonlyField(QObject::tr("Subject"),
												QStringLiteral("mumble.quistify.com")),
						automationReadonlyField(QObject::tr("Issuer"),
												QStringLiteral("Quistify Dev CA")),
						automationReadonlyField(QObject::tr("Serial number"),
												QStringLiteral("00A17C5")),
						automationReadonlyField(QObject::tr("Valid from"),
												QStringLiteral("2026-05-31T12:00:00")),
						automationReadonlyField(QObject::tr("Expires"),
												QStringLiteral("2027-05-31T12:00:00")),
						automationReadonlyField(
							QObject::tr("Presented digest"),
							QStringLiteral("b4:55:f3:6c:5e:1d:81:88:f8:d2:d9:27:f2:78:c1:f2:9e:5a:d7:d8")),
						automationDialogField(
							QStringLiteral("ssl.errorDetails"), QObject::tr("Verification errors"),
							QStringLiteral("textarea"),
							QObject::tr("The certificate is self-signed, and untrusted."), false) },
					QStringLiteral("list")) },
				QVariantList { automationDialogAction(QStringLiteral("rejectSslCertificate"),
													  QObject::tr("Close"), QString(), true),
							   automationDialogAction(QStringLiteral("trustSslCertificate"),
													  QObject::tr("Accept certificate"),
													  QStringLiteral("danger"), true) },
				QStringLiteral("rejectSslCertificate"), QStringLiteral("warning"), QSize(760, 620));
		}
		if (variant == QLatin1String("sslHandshakeFailure")) {
			return automationDialogFromSections(
				QStringLiteral("sslHandshakeFailure"), QStringLiteral("warning"),
				QObject::tr("SSL error"),
				QObject::tr("Mumble is unable to establish a secure connection to the server."),
				QVariantList { automationSection(
					QObject::tr("Connection"),
					QVariantList {
						automationReadonlyField(QObject::tr("Reason"),
												QObject::tr("The TLS handshake failed.")),
						automationNoteField(QObject::tr(
							"This can happen when the client and server support different encryption standards, one side is using an old operating system, the address is not a Mumble server, or the selected port belongs to another service.")) }) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"),
													  QStringLiteral("accent"), true) },
				QStringLiteral("close"), QStringLiteral("warning"), QSize(720, 440));
		}

		if (variant == QLatin1String("dragUserConfirm")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("dragUserConfirm"), QStringLiteral("confirm"),
				QObject::tr("Move user?"),
				QObject::tr("Confirm this voice-room move before it is sent to the server."),
				QVariantList { automationSection(
					QObject::tr("Move"),
					QVariantList {
						automationHiddenField(QStringLiteral("dragUser.session"), 42),
						automationHiddenField(QStringLiteral("dragUser.targetScopeToken"),
											  QStringLiteral("0:100")),
						automationReadonlyField(QObject::tr("User"), QStringLiteral("Demo User")),
						automationReadonlyField(QObject::tr("Target room"),
												QStringLiteral("Root / Landing")),
						automationNoteField(QObject::tr(
							"Your User Dragging preference is set to ask before moving people between voice rooms.")) }) },
				QVariantList { automationDialogAction(QStringLiteral("cancel"), QObject::tr("Cancel"),
													  QString(), true),
							   automationDialogAction(QStringLiteral("confirmDragUser"),
													  QObject::tr("Move user"), QStringLiteral("warning"), true) },
				QStringLiteral("confirmDragUser"), QStringLiteral("warning"), QSize(560, 360));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("User"),
															 QStringLiteral("Demo User")),
										 automationHighlight(QObject::tr("Target"),
															 QStringLiteral("Root / Landing")) });
			return dialog;
		}
		if (variant == QLatin1String("dragChannelConfirm")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("dragChannelConfirm"), QStringLiteral("confirm"),
				QObject::tr("Move room?"),
				QObject::tr("Confirm this room move before it is sent to the server."),
				QVariantList { automationSection(
					QObject::tr("Move"),
					QVariantList {
						automationHiddenField(QStringLiteral("dragChannel.sourceScopeToken"),
											  QStringLiteral("0:101")),
						automationHiddenField(QStringLiteral("dragChannel.targetScopeToken"),
											  QStringLiteral("0:100")),
						automationHiddenField(QStringLiteral("dragChannel.placement"),
											  QStringLiteral("inside")),
						automationReadonlyField(QObject::tr("Room"),
												QStringLiteral("Root / Strategy")),
						automationReadonlyField(QObject::tr("Target"),
												QStringLiteral("Root / Landing")),
						automationReadonlyField(QObject::tr("Placement"), QStringLiteral("inside")),
						automationNoteField(QObject::tr(
							"Your Channel Dragging preference is set to ask before moving rooms.")) }) },
				QVariantList { automationDialogAction(QStringLiteral("cancel"), QObject::tr("Cancel"),
													  QString(), true),
							   automationDialogAction(QStringLiteral("confirmDragChannel"),
													  QObject::tr("Move room"), QStringLiteral("warning"), true) },
				QStringLiteral("confirmDragChannel"), QStringLiteral("warning"), QSize(580, 380));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Room"),
															 QStringLiteral("Root / Strategy")),
										 automationHighlight(QObject::tr("Target"),
															 QStringLiteral("Root / Landing")) });
			return dialog;
		}
		if (variant == QLatin1String("channelMoveUnavailable")) {
			return automationDialogFromSections(
				QStringLiteral("channelMoveUnavailable"), QStringLiteral("warning"),
				QObject::tr("Cannot move room"),
				QObject::tr("This room cannot be moved automatically."),
				QVariantList { automationSection(
					QObject::tr("Reason"),
					QVariantList { automationNoteField(QObject::tr(
						"Reset the numeric sorting indicators or adjust the room manually.")) }) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"),
													  QStringLiteral("accent"), true) },
				QStringLiteral("close"), QStringLiteral("warning"), QSize(560, 320));
		}

		if (variant == QLatin1String("aboutMumble")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("aboutMumbleProbe"), QStringLiteral("about"), QObject::tr("About Mumble"),
				QObject::tr("Version, license, and project information."),
				QVariantList {
					automationSection(QObject::tr("Project"),
									  QVariantList {
										  automationReadonlyField(QObject::tr("Version"),
																  QStringLiteral("1.7.0")),
										  automationReadonlyField(QObject::tr("Build"), 0),
										  automationReadonlyField(QObject::tr("Channel"),
																  QStringLiteral("mumble-forked")),
										  automationReadonlyField(QObject::tr("License"),
																  QStringLiteral("BSD-3-Clause")) },
									  QStringLiteral("list")),
					automationSection(QObject::tr("Links"),
									  QVariantList {
										  automationReadonlyField(QObject::tr("Website"),
																  QStringLiteral("www.mumble.info")),
										  automationReadonlyField(QObject::tr("Fork releases"),
																  QStringLiteral("GitHub releases")) },
									  QStringLiteral("list")) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QString(),
													  true),
							   automationDialogAction(QStringLiteral("openWebsite"), QObject::tr("Open website"),
													  QStringLiteral("accent"), false) },
				QStringLiteral("openWebsite"), QString(), QSize(640, 500));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Version"),
															 QStringLiteral("1.7.0")),
										 automationHighlight(QObject::tr("Build"), 0),
										 automationHighlight(QObject::tr("Runtime"),
															 QStringLiteral("Qt 6")) });
			return dialog;
		}
		if (variant == QLatin1String("aboutQt")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("aboutQtProbe"), QStringLiteral("about"), QObject::tr("About Qt"),
				QObject::tr("Qt runtime and licensing details."),
				QVariantList {
					automationSection(QObject::tr("Runtime"),
									  QVariantList {
										  automationReadonlyField(QObject::tr("Qt"),
																  QStringLiteral("Qt 6 packaged runtime")),
										  automationReadonlyField(QObject::tr("WebEngine"),
																  QObject::tr("Available")),
										  automationReadonlyField(QObject::tr("Platform"),
																  QStringLiteral("Windows x64")) },
									  QStringLiteral("list")),
					automationSection(QObject::tr("License"),
									  QVariantList {
										  automationReadonlyField(QObject::tr("Licensing"),
																  QObject::tr("Qt is available under LGPL/commercial terms.")),
										  automationReadonlyField(QObject::tr("Project"),
																  QStringLiteral("https://www.qt.io/")) },
									  QStringLiteral("list")) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QString(),
													  true),
							   automationDialogAction(QStringLiteral("openQtWebsite"),
													  QObject::tr("Open Qt"), QStringLiteral("accent"), false) },
				QStringLiteral("openQtWebsite"), QString(), QSize(640, 500));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Qt"),
															 QStringLiteral("6.x")),
										 automationHighlight(QObject::tr("WebEngine"),
															 QObject::tr("On")),
										 automationHighlight(QObject::tr("Platform"),
															 QStringLiteral("Windows")) });
			return dialog;
		}
		if (variant == QLatin1String("helpWindow")) {
			return automationDialogFromSections(
				QStringLiteral("help"), QStringLiteral("info"), QObject::tr("Help"),
				QObject::tr("Modern layout keeps contextual help inside the client shell."),
				QVariantList {
					automationSection(QObject::tr("Start"),
									  QVariantList {
										  automationReadonlyField(QObject::tr("Connect"),
																  QObject::tr("Open Server > Connect to choose or edit saved servers.")),
										  automationReadonlyField(QObject::tr("Rooms"),
																  QObject::tr("Use the left navigator for text rooms, voice rooms, and direct messages.")) },
									  QStringLiteral("list")),
					automationSection(QObject::tr("Windows"),
									  QVariantList {
										  automationReadonlyField(QObject::tr("Settings"),
																  QObject::tr("Configure audio, appearance, hotkeys, and certificates.")),
										  automationReadonlyField(QObject::tr("Feedback"),
																  QObject::tr("Report bugs or suggestions with optional diagnostics.")) },
									  QStringLiteral("list")) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QString(),
													  true),
							   automationDialogAction(QStringLiteral("openHelpDocs"),
													  QObject::tr("Open docs"), QStringLiteral("accent"), false) },
				QStringLiteral("openHelpDocs"), QString(), QSize(640, 520));
		}

		if (variant == QLatin1String("roomUnlink")) {
			return automationDialog(
				QStringLiteral("unlinkRoom:9001"), QStringLiteral("confirm"),
				QObject::tr("Unlink room"), QObject::tr("Remove the link between Lobby and Root."),
				QVariantList { automationHiddenField(QStringLiteral("source"), 9001),
							   automationHiddenField(QStringLiteral("target"), 0),
							   automationReadonlyField(QObject::tr("Current room"), QObject::tr("Lobby")),
							   automationReadonlyField(QObject::tr("Linked room"), QObject::tr("Root")) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("confirmUnlinkRoom"),
													  QObject::tr("Unlink"), QStringLiteral("danger"), true) },
				QStringLiteral("confirmUnlinkRoom"), QStringLiteral("danger"), QSize(520, 280));
		}
		if (variant == QLatin1String("roomUnlinkAll")) {
			return automationDialog(
				QStringLiteral("unlinkAllRooms:9001"), QStringLiteral("confirm"),
				QObject::tr("Unlink all rooms"),
				QObject::tr("Remove 1 permanent room link(s) from Lobby."),
				QVariantList { automationHiddenField(QStringLiteral("source"), 9001),
							   automationReadonlyField(QObject::tr("Current room"), QObject::tr("Lobby")),
							   automationReadonlyField(QObject::tr("Linked rooms"), QObject::tr("Root")) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("confirmUnlinkAllRooms"),
													  QObject::tr("Unlink all"), QStringLiteral("danger"), true) },
				QStringLiteral("confirmUnlinkAllRooms"), QStringLiteral("danger"), QSize(560, 300));
		}
		if (variant == QLatin1String("roomRemove")) {
			return automationDialog(
				QStringLiteral("removeRoomProbe:9001"), QStringLiteral("confirm"),
				QObject::tr("Remove room"), QObject::tr("Delete Lobby and all of its sub-rooms."),
				QVariantList { automationHiddenField(QStringLiteral("channel"), 9001),
							   automationReadonlyField(QObject::tr("Room"), QObject::tr("Lobby")) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("confirmRemoveRoomProbe"),
													  QObject::tr("Delete"), QStringLiteral("danger"), true) },
				QStringLiteral("confirmRemoveRoomProbe"), QStringLiteral("danger"), QSize(520, 280));
		}
		if (variant == QLatin1String("textRoomEdit")) {
			const QVariantList visibilityOptions {
				automationSelectOption(QObject::tr("Root"), 0),
				automationSelectOption(QObject::tr("Lobby"), 1),
				automationSelectOption(QObject::tr("Operations"), 2)
			};
			return automationDialogFromSections(
				QStringLiteral("editTextRoom:9001"), QStringLiteral("form"), QObject::tr("Edit text room"),
				QObject::tr("Update the persistent text room without leaving Modern layout."),
				QVariantList { automationSection(
					QObject::tr("Room"),
					QVariantList {
						automationHiddenField(QStringLiteral("text.id"), 9001),
						automationDialogField(QStringLiteral("text.name"), QObject::tr("Name"),
											  QStringLiteral("text"), QObject::tr("ops-briefing")),
						automationDialogField(
							QStringLiteral("text.description"), QObject::tr("Description"),
							QStringLiteral("textarea"),
							QObject::tr("Daily handoff notes, deploy status, and moderation follow-up.")),
						automationSelectField(QStringLiteral("text.visibility"),
											  QObject::tr("Visibility source"), 0, visibilityOptions),
						automationDialogField(QStringLiteral("text.position"), QObject::tr("Order"),
											  QStringLiteral("number"), 3) }) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("editTextRoomAcl"),
													  QObject::tr("Configure ACL"), QString(), false),
							   automationDialogAction(QStringLiteral("saveTextRoom"), QObject::tr("Save"),
													  QStringLiteral("accent"), false) },
				QStringLiteral("saveTextRoom"), QString(), QSize(680, 500));
		}
		if (variant == QLatin1String("textRoomDelete")) {
			return automationDialog(
				QStringLiteral("deleteTextRoom:9001"), QStringLiteral("confirm"),
				QObject::tr("Delete text room"), QObject::tr("Delete #ops-briefing?"),
				QVariantList {
					automationHiddenField(QStringLiteral("text.id"), 9001),
					automationReadonlyField(QObject::tr("Text room"), QStringLiteral("#ops-briefing")),
					automationReadonlyField(
						QObject::tr("Result"),
						QObject::tr("Existing history will no longer be visible in this room.")) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("confirmDeleteTextRoom"),
													  QObject::tr("Delete"), QStringLiteral("danger"), true) },
				QStringLiteral("confirmDeleteTextRoom"), QStringLiteral("danger"), QSize(620, 420));
		}

		if (variant == QLatin1String("usersLoading")) {
			return automationDialogFromSections(
				QStringLiteral("serverUserList"), QStringLiteral("info"), QObject::tr("Registered users"),
				QObject::tr("Requesting the registered user list from the server."),
				QVariantList { automationSection(
					QObject::tr("Status"), QVariantList { automationNoteField(QObject::tr("Loading users...")) },
					QStringLiteral("list")) },
				QVariantList { close }, QStringLiteral("close"), QString(), QSize(660, 460));
		}
		if (variant == QLatin1String("usersEmpty")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("serverUserList"), QStringLiteral("info"), QObject::tr("Registered users"),
				QObject::tr("Registered accounts on this server."),
				QVariantList { automationSection(
					QObject::tr("Users"),
					QVariantList { automationNoteField(QObject::tr("The server returned an empty user list.")) },
					QStringLiteral("records")) },
				QVariantList { close }, QStringLiteral("close"), QString(), QSize(720, 620));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Registered"), 0),
										 automationHighlight(QObject::tr("Shown"), 0),
										 automationHighlight(QObject::tr("Mode"), QObject::tr("Read only")) });
			return dialog;
		}
		if (variant == QLatin1String("usersPopulated")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("serverUserList"), QStringLiteral("info"), QObject::tr("Registered users"),
				QObject::tr("Registered accounts on this server."),
				QVariantList {
					automationSection(QObject::tr("Demo Admin"),
									  QVariantList { automationReadonlyField(QObject::tr("User ID"), 1),
													 automationReadonlyField(QObject::tr("Last seen"),
																			 QObject::tr("Online now")),
													 automationReadonlyField(QObject::tr("Certificate"),
																			 QObject::tr("Verified")) },
									  QStringLiteral("records")),
					automationSection(QObject::tr("Demo Designer"),
									  QVariantList { automationReadonlyField(QObject::tr("User ID"), 42),
													 automationReadonlyField(QObject::tr("Last seen"),
																			 QObject::tr("28 May, 02:00")),
													 automationReadonlyField(QObject::tr("Certificate"),
																			 QObject::tr("Verified")) },
									  QStringLiteral("records")) },
				QVariantList { close }, QStringLiteral("close"), QString(), QSize(720, 620));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Registered"), 2),
										 automationHighlight(QObject::tr("Shown"), 2),
										 automationHighlight(QObject::tr("Mode"), QObject::tr("Read only")) });
			return dialog;
		}
		if (variant == QLatin1String("bansLoading")) {
			return automationDialogFromSections(
				QStringLiteral("serverBanList"), QStringLiteral("info"), QObject::tr("Ban list"),
				QObject::tr("Requesting the ban list from the server."),
				QVariantList { automationSection(
					QObject::tr("Status"), QVariantList { automationNoteField(QObject::tr("Loading bans...")) }) },
				QVariantList { close }, QStringLiteral("close"), QString(), QSize(660, 460));
		}
		if (variant == QLatin1String("bansEmpty")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("serverBanList"), QStringLiteral("info"), QObject::tr("Ban list"),
				QObject::tr("Read-only Modern view of the server ban list."),
				QVariantList { automationSection(
					QObject::tr("Bans"),
					QVariantList { automationNoteField(QObject::tr("There are no active bans on this server.")) },
					QStringLiteral("records")) },
				QVariantList { close }, QStringLiteral("close"), QString(), QSize(760, 620));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Active bans"), 0),
										 automationHighlight(QObject::tr("Mode"), QObject::tr("Read only")) });
			return dialog;
		}
		if (variant == QLatin1String("bansPopulated")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("serverBanList"), QStringLiteral("info"), QObject::tr("Ban list"),
				QObject::tr("Read-only Modern view of the server ban list."),
				QVariantList {
					automationSection(QObject::tr("192.0.2.42"),
									  QVariantList { automationReadonlyField(QObject::tr("Username"),
																			 QObject::tr("Demo Spammer")),
													 automationReadonlyField(QObject::tr("Reason"),
																			 QObject::tr("Repeated channel spam")),
													 automationReadonlyField(QObject::tr("Expires"),
																			 QObject::tr("30 May, 18:00")) },
									  QStringLiteral("records")),
					automationSection(QObject::tr("2001:db8::7"),
									  QVariantList { automationReadonlyField(QObject::tr("Username"),
																			 QObject::tr("Test Abuse")),
													 automationReadonlyField(QObject::tr("Reason"),
																			 QObject::tr("Temporary moderation hold")),
													 automationReadonlyField(QObject::tr("Expires"),
																			 QObject::tr("Never")) },
									  QStringLiteral("records")) },
				QVariantList { close }, QStringLiteral("close"), QString(), QSize(760, 620));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Active bans"), 2),
										 automationHighlight(QObject::tr("Mode"), QObject::tr("Read only")) });
			return dialog;
		}
		if (variant == QLatin1String("serverInformation")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("serverInformation"), QStringLiteral("info"), QObject::tr("Server information"),
				QObject::tr("Current server details and advertised limits."),
				QVariantList {
					automationSection(QObject::tr("Server"),
									  QVariantList {
										  automationReadonlyField(QObject::tr("Name"),
																  QStringLiteral("192.168.50.200 DEV")),
										  automationReadonlyField(QObject::tr("Version"),
																  QStringLiteral("Mumble 1.7.0 DEV")),
										  automationReadonlyField(QObject::tr("Uptime"),
																  QObject::tr("3 days, 4 hours")) },
									  QStringLiteral("list")),
					automationSection(QObject::tr("Limits"),
									  QVariantList {
										  automationReadonlyField(QObject::tr("Users"), QObject::tr("1 / 100")),
										  automationReadonlyField(QObject::tr("Audio bandwidth"),
																  QObject::tr("128 kbit/s")),
										  automationReadonlyField(QObject::tr("Message length"),
																  QObject::tr("5000 characters")) },
									  QStringLiteral("list")) },
				QVariantList { close }, QStringLiteral("close"), QString(), QSize(700, 560));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Status"), QObject::tr("Connected")),
										 automationHighlight(QObject::tr("Codec"), QObject::tr("Opus")) });
			return dialog;
		}
		if (variant == QLatin1String("aclPopulated")) {
			QVariantMap aclModel;
			aclModel.insert(QStringLiteral("channelId"), 9001);
			aclModel.insert(QStringLiteral("inheritAcls"), true);
			aclModel.insert(QStringLiteral("password"), QStringLiteral("scrim-night"));
			aclModel.insert(QStringLiteral("activeTab"), QStringLiteral("rules"));
			aclModel.insert(QStringLiteral("selectedRuleIndex"), 1);
			aclModel.insert(QStringLiteral("permissions"),
							QVariantList { automationAclPermission(1, QObject::tr("Write")),
										   automationAclPermission(2, QObject::tr("Traverse")),
										   automationAclPermission(4, QObject::tr("Enter")),
										   automationAclPermission(8, QObject::tr("Speak")),
										   automationAclPermission(16, QObject::tr("Mute/deafen")),
										   automationAclPermission(32, QObject::tr("Move")) });
			aclModel.insert(QStringLiteral("groups"),
							QVariantList {
								automationAclGroup(QStringLiteral("auth"), true, true, true, QVariantList {},
												   QVariantList {}, QVariantList { 1, 2, 3 }),
								automationAclGroup(QStringLiteral("scrim-team"), false, true, false,
												   QVariantList { 1, 2, 4, 7 }, QVariantList {}) });
			aclModel.insert(QStringLiteral("acls"),
							QVariantList {
								automationAclRule(QStringLiteral("group"), QStringLiteral("all"), -1, true, true, true,
												  QVariantList { 2 }, QVariantList { 1 }),
								automationAclRule(QStringLiteral("group"), QStringLiteral("scrim-team"), -1, false,
												  true, false, QVariantList { 1, 2, 4, 8 }, QVariantList {}, true),
								automationAclRule(QStringLiteral("user"), QStringLiteral("Kira"), 2, false, true, true,
												  QVariantList { 16, 32 }, QVariantList {}) });
			aclModel.insert(QStringLiteral("userOptions"),
							QVariantList { automationSelectOption(QObject::tr("Kira (#2)"), 2),
										   automationSelectOption(QObject::tr("Nova (#7)"), 7) });

			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("acl"), QStringLiteral("form"), QObject::tr("Edit room"),
				QObject::tr("Manage room details, inherited groups, and explicit access rules."),
				QVariantList {
					automationSection(QObject::tr("Room details"),
									  QVariantList {
										  automationDialogField(QStringLiteral("channel.name"), QObject::tr("Name"),
																QStringLiteral("text"), QObject::tr("Lobby")),
										  automationDialogField(QStringLiteral("channel.description"),
																QObject::tr("Topic"), QStringLiteral("textarea"),
																QObject::tr("Main voice lobby.")),
										  automationDialogField(QStringLiteral("channel.position"),
																QObject::tr("Order"), QStringLiteral("number"), 0),
										  automationDialogField(QStringLiteral("channel.maxUsers"),
																QObject::tr("Max users"), QStringLiteral("number"), 0) }),
					automationSection(QObject::tr("Access control"),
									  QVariantList { automationDialogField(QStringLiteral("acl.model"),
																		   QObject::tr("ACL"),
																		   QStringLiteral("aclEditor"), aclModel) }) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("saveAcl"), QObject::tr("Save room"),
													  QStringLiteral("accent"), true) },
				QStringLiteral("saveAcl"), QString(), QSize(1040, 780));
			dialog.insert(QStringLiteral("tone"), QStringLiteral("wide"));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Room"), QObject::tr("Lobby")),
										 automationHighlight(QObject::tr("Rules"), 3),
										 automationHighlight(QObject::tr("Password"), QObject::tr("Set")) });
			return dialog;
		}
		if (variant == QLatin1String("aclLoading")) {
			return automationDialogFromSections(
				QStringLiteral("acl"), QStringLiteral("info"), QObject::tr("Edit room"),
				QObject::tr("Requesting room details and ACL data for Root."),
				QVariantList { automationSection(
					QObject::tr("Status"), QVariantList { automationNoteField(QObject::tr("Loading ACL...")) }) },
				QVariantList { close }, QStringLiteral("close"), QString(), QSize(720, 520));
		}
		if (variant == QLatin1String("aclError")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("acl"), QStringLiteral("info"), QObject::tr("Edit room"),
				QObject::tr("Room details and ACL data could not be loaded for Root."),
				QVariantList { automationSection(
					QObject::tr("Status"),
					QVariantList { automationNoteField(QObject::tr("The server did not return ACL data.")) }) },
				QVariantList { close }, QStringLiteral("close"), QStringLiteral("danger"), QSize(720, 520));
			dialog.insert(QStringLiteral("statusMessage"), QObject::tr("Unable to load ACL data."));
			return dialog;
		}
		if (variant == QLatin1String("searchHit")) {
			QVariantList results {
				automationSearchResult(QStringLiteral("channel"), 1, 0, QObject::tr("Relay Ops"),
									   QObject::tr("Root / Operations - room name match"), 0, 5,
									   QObject::tr("Open"), QObject::tr("Join")),
				automationSearchResult(QStringLiteral("textRoom"), 2, 1, QObject::tr("#relay"),
									   QObject::tr("Text room - 4 matching messages"), 1, 5,
									   QObject::tr("Open"), QString()),
				automationSearchResult(QStringLiteral("user"), 7, 2, QObject::tr("Relay_Bot"),
									   QObject::tr("User - Root / Operations"), 0, 5,
									   QObject::tr("Message"), QObject::tr("Select")),
				automationSearchResult(QStringLiteral("user"), 9, 3, QObject::tr("Kira Relay"),
									   QObject::tr("User - Root / Lobby"), 5, 5,
									   QObject::tr("Message"), QObject::tr("Select"))
			};
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("serverSearch"), QStringLiteral("form"), QObject::tr("Search"),
				QObject::tr("Find users and rooms on the current server."),
				QVariantList { automationSection(
					QObject::tr("Search"),
					QVariantList {
						automationDialogField(QStringLiteral("search.query"), QObject::tr("Search"),
											  QStringLiteral("text"), QStringLiteral("relay")),
						automationDialogField(QStringLiteral("search.users"), QObject::tr("Users"),
											  QStringLiteral("checkbox"), true),
						automationDialogField(QStringLiteral("search.channels"), QObject::tr("Rooms"),
											  QStringLiteral("checkbox"), true),
						automationDialogField(QStringLiteral("search.caseSensitive"), QObject::tr("Case sensitive"),
											  QStringLiteral("checkbox"), false),
						automationDialogField(QStringLiteral("search.regex"), QObject::tr("Regular expression"),
											  QStringLiteral("checkbox"), false),
						automationResultListField(QStringLiteral("search.results"), QObject::tr("Results"),
												  results, QObject::tr("No matching users or rooms.")) }) },
				QVariantList { close }, QStringLiteral("close"), QString(), QSize(820, 680));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Results"), results.size()),
										 automationHighlight(QObject::tr("Scope"), QObject::tr("Users + rooms")) });
			return dialog;
		}
		if (variant == QLatin1String("searchEmpty")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("serverSearch"), QStringLiteral("form"), QObject::tr("Search"),
				QObject::tr("Find users and rooms on the current server."),
				QVariantList { automationSection(
					QObject::tr("Search"),
					QVariantList {
						automationDialogField(QStringLiteral("search.query"), QObject::tr("Search"),
											  QStringLiteral("text"), QStringLiteral("zz-no-result")),
						automationDialogField(QStringLiteral("search.users"), QObject::tr("Users"),
											  QStringLiteral("checkbox"), true),
						automationDialogField(QStringLiteral("search.channels"), QObject::tr("Rooms"),
											  QStringLiteral("checkbox"), true),
						automationDialogField(QStringLiteral("search.caseSensitive"), QObject::tr("Case sensitive"),
											  QStringLiteral("checkbox"), false),
						automationDialogField(QStringLiteral("search.regex"), QObject::tr("Regular expression"),
											  QStringLiteral("checkbox"), false),
						automationResultListField(QStringLiteral("search.results"), QObject::tr("Results"),
												  QVariantList {}, QObject::tr("No matching users or rooms.")) }) },
				QVariantList { close }, QStringLiteral("close"), QString(), QSize(820, 680));
			return dialog;
		}
		if (variant == QLatin1String("searchRegexError")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("serverSearch"), QStringLiteral("form"), QObject::tr("Search"),
				QObject::tr("Find users and rooms on the current server."),
				QVariantList { automationSection(
					QObject::tr("Search"),
					QVariantList {
						automationDialogField(QStringLiteral("search.query"), QObject::tr("Search"),
											  QStringLiteral("text"), QStringLiteral("[")),
						automationDialogField(QStringLiteral("search.users"), QObject::tr("Users"),
											  QStringLiteral("checkbox"), true),
						automationDialogField(QStringLiteral("search.channels"), QObject::tr("Rooms"),
											  QStringLiteral("checkbox"), true),
						automationDialogField(QStringLiteral("search.caseSensitive"), QObject::tr("Case sensitive"),
											  QStringLiteral("checkbox"), false),
						automationDialogField(QStringLiteral("search.regex"), QObject::tr("Regular expression"),
											  QStringLiteral("checkbox"), true),
						automationResultListField(QStringLiteral("search.results"), QObject::tr("Results"),
												  QVariantList {}, QObject::tr("No matching users or rooms.")) }) },
				QVariantList { close }, QStringLiteral("close"), QString(), QSize(820, 680));
			dialog.insert(QStringLiteral("errors"),
						  QVariantMap { { QStringLiteral("search.query"),
										  QObject::tr("Invalid regular expression.") } });
			return dialog;
		}
		if (variant == QLatin1String("tokensEmpty")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("serverTokens"), QStringLiteral("form"), QObject::tr("Access tokens"),
				QObject::tr("Manage the temporary access tokens for the current server."),
				QVariantList { automationSection(
					QObject::tr("Tokens"),
					QVariantList { automationNoteField(QObject::tr("Access tokens are saved for this server and sent with future reconnects.")),
								   automationDialogField(QStringLiteral("token.0"), QObject::tr("Token 1"),
														 QStringLiteral("text"), QString()) },
					QStringLiteral("form")) },
				QVariantList { cancel, automationDialogAction(QStringLiteral("addToken"), QObject::tr("Add token")),
							   automationDialogAction(QStringLiteral("saveTokens"), QObject::tr("Save tokens"),
													  QStringLiteral("accent"), true) },
				QStringLiteral("saveTokens"), QString(), QSize(620, 460));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Saved tokens"), 0),
										 automationHighlight(QObject::tr("Scope"), QObject::tr("Current server")) });
			return dialog;
		}
		if (variant == QLatin1String("tokensPopulated")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("serverTokens"), QStringLiteral("form"), QObject::tr("Access tokens"),
				QObject::tr("Manage the temporary access tokens for the current server."),
				QVariantList { automationSection(
					QObject::tr("Tokens"),
					QVariantList { automationNoteField(QObject::tr("Access tokens are saved for this server and sent with future reconnects.")),
								   automationDialogField(QStringLiteral("token.0"), QObject::tr("Token 1"),
														 QStringLiteral("text"),
														 QStringLiteral("design-review-token")),
								   automationDialogField(QStringLiteral("token.1"), QObject::tr("Token 2"),
														 QStringLiteral("text"),
														 QStringLiteral("temporary-event-pass")) },
					QStringLiteral("form")) },
				QVariantList { cancel, automationDialogAction(QStringLiteral("addToken"), QObject::tr("Add token")),
							   automationDialogAction(QStringLiteral("saveTokens"), QObject::tr("Save tokens"),
													  QStringLiteral("accent"), true) },
				QStringLiteral("saveTokens"), QString(), QSize(620, 500));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Saved tokens"), 2),
										 automationHighlight(QObject::tr("Scope"), QObject::tr("Current server")) });
			return dialog;
		}
		if (variant == QLatin1String("serverSettings")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("serverSettings"), QStringLiteral("form"), QObject::tr("Server settings"),
				QObject::tr("Change connected server settings from the Modern layout."),
				QVariantList {
					automationSection(QObject::tr("Chat"),
									  QVariantList {
										  automationDialogField(QStringLiteral("server.welcomeText"),
																QObject::tr("Welcome text"),
																QStringLiteral("textarea"),
																QObject::tr("Welcome to the Modern dev server.")),
										  automationDialogField(QStringLiteral("server.allowHtml"),
																QObject::tr("Allow HTML"),
																QStringLiteral("checkbox"), true),
										  automationDialogField(QStringLiteral("server.persistentGlobalChat"),
																QObject::tr("Server-wide chat"),
																QStringLiteral("checkbox"), true) }),
					automationSection(QObject::tr("Limits"),
									  QVariantList {
										  automationDialogField(QStringLiteral("server.maxBandwidth"),
																QObject::tr("Audio bandwidth"),
																QStringLiteral("number"), 128000),
										  automationDialogField(QStringLiteral("server.maxUsers"),
																QObject::tr("Max users"),
																QStringLiteral("number"), 100),
										  automationDialogField(QStringLiteral("server.messageLength"),
																QObject::tr("Text message length"),
																QStringLiteral("number"), 5000),
										  automationDialogField(QStringLiteral("server.imageLength"),
																QObject::tr("Image message length"),
																QStringLiteral("number"), 1048576) }),
					automationSection(QObject::tr("Screen sharing"),
									  QVariantList {
										  automationDialogField(QStringLiteral("server.screenShareEnabled"),
																QObject::tr("Enabled"),
																QStringLiteral("checkbox"), true),
										  automationDialogField(QStringLiteral("server.screenShareRelay"),
																QObject::tr("Relay URL"),
																QStringLiteral("text"),
																QStringLiteral("wss://relay.example.test/share")) }) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("saveServerSettings"),
													  QObject::tr("Apply"), QStringLiteral("accent")) },
				QStringLiteral("saveServerSettings"), QString(), QSize(760, 700));
			dialog.insert(QStringLiteral("statusMessage"), QObject::tr("Server settings loaded from automation probe."));
			return dialog;
		}
		if (variant == QLatin1String("serverSettingsError")) {
			QVariantMap dialog = automationDialogFromSections(
				QStringLiteral("serverSettings"), QStringLiteral("form"), QObject::tr("Server settings"),
				QObject::tr("Change connected server settings from the Modern layout."),
				QVariantList {
					automationSection(QObject::tr("Chat"),
									  QVariantList {
										  automationDialogField(QStringLiteral("server.welcomeText"),
																QObject::tr("Welcome text"),
																QStringLiteral("textarea"), QString()),
										  automationDialogField(QStringLiteral("server.allowHtml"),
																QObject::tr("Allow HTML"),
																QStringLiteral("checkbox"), true),
										  automationDialogField(QStringLiteral("server.persistentGlobalChat"),
																QObject::tr("Server-wide chat"),
																QStringLiteral("checkbox"), true) }),
					automationSection(QObject::tr("Limits"),
									  QVariantList {
										  automationDialogField(QStringLiteral("server.maxBandwidth"),
																QObject::tr("Audio bandwidth"),
																QStringLiteral("number"), -1),
										  automationDialogField(QStringLiteral("server.maxUsers"),
																QObject::tr("Max users"),
																QStringLiteral("number"), 0),
										  automationDialogField(QStringLiteral("server.messageLength"),
																QObject::tr("Text message length"),
																QStringLiteral("number"), 0),
										  automationDialogField(QStringLiteral("server.imageLength"),
																QObject::tr("Image message length"),
																QStringLiteral("number"), 0) }),
					automationSection(QObject::tr("Screen sharing"),
									  QVariantList {
										  automationDialogField(QStringLiteral("server.screenShareEnabled"),
																QObject::tr("Enabled"),
																QStringLiteral("checkbox"), true),
										  automationDialogField(QStringLiteral("server.screenShareRelay"),
																QObject::tr("Relay URL"),
																QStringLiteral("text"), QStringLiteral("not-a-url")) }) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("saveServerSettings"),
													  QObject::tr("Apply"), QStringLiteral("accent")) },
				QStringLiteral("saveServerSettings"), QString(), QSize(760, 700));
			dialog.insert(QStringLiteral("errors"),
						  QVariantMap { { QStringLiteral("server.maxBandwidth"),
										  QObject::tr("Enter a non-negative bandwidth limit.") },
										{ QStringLiteral("server.screenShareRelay"),
										  QObject::tr("Enter a valid relay URL.") } });
			dialog.insert(QStringLiteral("statusMessage"), QObject::tr("Review the highlighted server settings."));
			return dialog;
		}

		return {};
	}

	QVariantMap automationStonksQuote(const double price, const double changePercent, const QString &currency,
									  const QString &sourceURL = QString()) {
		QVariantMap quote;
		quote.insert(QStringLiteral("ok"), true);
		quote.insert(QStringLiteral("pending"), false);
		quote.insert(QStringLiteral("price"), price);
		quote.insert(QStringLiteral("changePercent"), changePercent);
		quote.insert(QStringLiteral("currency"), currency);
		quote.insert(QStringLiteral("quoteTime"), QVariant::fromValue< qulonglong >(1779926400ULL));
		if (!sourceURL.isEmpty()) {
			quote.insert(QStringLiteral("quoteSourceUrl"), sourceURL);
		}
		return quote;
	}

	QVariantMap automationStonksQuoteError(const QString &message) {
		QVariantMap quote;
		quote.insert(QStringLiteral("ok"), false);
		quote.insert(QStringLiteral("pending"), false);
		quote.insert(QStringLiteral("error"), message);
		return quote;
	}

	QVariantMap automationStonksPosition(const QString &symbol, const QString &displayName, const double quantity,
										 const double price, const QString &currency, const QString &exchange) {
		QVariantMap position;
		position.insert(QStringLiteral("symbol"), symbol);
		position.insert(QStringLiteral("quantity"), quantity);
		position.insert(QStringLiteral("price"), price);
		position.insert(QStringLiteral("marketValue"), quantity * price);
		position.insert(QStringLiteral("currency"), currency);
		position.insert(QStringLiteral("displayName"), displayName);
		position.insert(QStringLiteral("providerId"), QStringLiteral("yahoo-finance"));
		position.insert(QStringLiteral("providerSymbol"), symbol);
		position.insert(QStringLiteral("exchange"), exchange);
		position.insert(QStringLiteral("quoteTime"), QVariant::fromValue< qulonglong >(1779926400ULL));
		position.insert(QStringLiteral("quoteSourceUrl"),
						QStringLiteral("https://finance.yahoo.com/quote/%1").arg(symbol));
		position.insert(QStringLiteral("quoteConfidence"), 1.0);
		return position;
	}

	QVariantMap automationStonksTicker(const QString &symbol, const QString &displayName, const unsigned int holders,
									   const double totalQuantity, const double totalMarketValue,
									   const QString &currency, const QString &exchange) {
		QVariantMap ticker;
		ticker.insert(QStringLiteral("symbol"), symbol);
		ticker.insert(QStringLiteral("displayName"), displayName);
		ticker.insert(QStringLiteral("holderCount"), holders);
		ticker.insert(QStringLiteral("totalQuantity"), totalQuantity);
		ticker.insert(QStringLiteral("totalMarketValue"), totalMarketValue);
		ticker.insert(QStringLiteral("currency"), currency);
		ticker.insert(QStringLiteral("providerId"), QStringLiteral("yahoo-finance"));
		ticker.insert(QStringLiteral("providerSymbol"), symbol);
		ticker.insert(QStringLiteral("exchange"), exchange);
		ticker.insert(QStringLiteral("quoteSourceUrl"),
					  QStringLiteral("https://finance.yahoo.com/quote/%1").arg(symbol));
		ticker.insert(QStringLiteral("latestUpdatedAt"), QVariant::fromValue< qulonglong >(1779926400ULL));
		return ticker;
	}

	QVariantMap automationStonksLeaderboardRow(const unsigned int rank, const unsigned int userID,
											   const QString &userName, const double returnPercent,
											   const bool followed = false) {
		QVariantMap row;
		row.insert(QStringLiteral("rank"), rank);
		row.insert(QStringLiteral("userId"), userID);
		row.insert(QStringLiteral("userName"), userName);
		row.insert(QStringLiteral("period"), QStringLiteral("30d"));
		row.insert(QStringLiteral("returnPercent"), returnPercent);
		row.insert(QStringLiteral("startValue"), 10000.0);
		row.insert(QStringLiteral("endValue"), 10000.0 + returnPercent * 100.0);
		row.insert(QStringLiteral("startSnapshotAt"), QVariant::fromValue< qulonglong >(1777334400ULL));
		row.insert(QStringLiteral("endSnapshotAt"), QVariant::fromValue< qulonglong >(1779926400ULL));
		row.insert(QStringLiteral("followed"), followed);
		row.insert(QStringLiteral("insufficientHistory"), false);
		return row;
	}

	QVariantMap automationStonksUser(const unsigned int userID, const QString &userName, const bool followed) {
		QVariantMap user;
		user.insert(QStringLiteral("userId"), userID);
		user.insert(QStringLiteral("userName"), userName);
		user.insert(QStringLiteral("followed"), followed);
		return user;
	}

	QVariantMap automationStonksStateProbe(const QString &variant) {
		const QVariantList periods { QStringLiteral("1d"), QStringLiteral("7d"), QStringLiteral("30d"),
									 QStringLiteral("ytd") };
		const QVariantList positions {
			automationStonksPosition(QStringLiteral("RKLB"), QObject::tr("Rocket Lab USA"), 42.0, 18.42,
									 QStringLiteral("USD"), QStringLiteral("Nasdaq")),
			automationStonksPosition(QStringLiteral("AMD"), QObject::tr("Advanced Micro Devices"), 12.0, 164.2,
									 QStringLiteral("USD"), QStringLiteral("Nasdaq")),
			automationStonksPosition(QStringLiteral("SAAB-B.ST"), QObject::tr("Saab AB"), 20.0, 292.4,
									 QStringLiteral("SEK"), QStringLiteral("Stockholm")) };
		QVariantMap snapshot;
		snapshot.insert(QStringLiteral("snapshotId"), 77u);
		snapshot.insert(QStringLiteral("userId"), 1u);
		snapshot.insert(QStringLiteral("userName"), QStringLiteral("dankmaster"));
		snapshot.insert(QStringLiteral("createdAt"), QVariant::fromValue< qulonglong >(1779926400ULL));
		snapshot.insert(QStringLiteral("currency"), QStringLiteral("USD"));
		snapshot.insert(QStringLiteral("totalValue"), 8582.64);
		snapshot.insert(QStringLiteral("note"), QObject::tr("Mockup probe portfolio with live-looking quote metadata."));
		snapshot.insert(QStringLiteral("positionsRedacted"), false);
		snapshot.insert(QStringLiteral("positions"), positions);

		const QVariantList popularTickers {
			automationStonksTicker(QStringLiteral("RKLB"), QObject::tr("Rocket Lab USA"), 4, 113.0, 2081.46,
								   QStringLiteral("USD"), QStringLiteral("Nasdaq")),
			automationStonksTicker(QStringLiteral("AMD"), QObject::tr("Advanced Micro Devices"), 3, 31.0, 5090.2,
								   QStringLiteral("USD"), QStringLiteral("Nasdaq")),
			automationStonksTicker(QStringLiteral("ERIC-B.ST"), QObject::tr("Ericsson B"), 2, 80.0, 6864.0,
								   QStringLiteral("SEK"), QStringLiteral("Stockholm")) };
		const QVariantList personalTickers {
			automationStonksTicker(QStringLiteral("RKLB"), QObject::tr("Rocket Lab USA"), 1, 42.0, 773.64,
								   QStringLiteral("USD"), QStringLiteral("Nasdaq")),
			automationStonksTicker(QStringLiteral("AMD"), QObject::tr("Advanced Micro Devices"), 1, 12.0, 1970.4,
								   QStringLiteral("USD"), QStringLiteral("Nasdaq")) };
		const QVariantList pinnedTickers {
			automationStonksTicker(QStringLiteral("RKLB"), QObject::tr("Rocket Lab USA"), 1, 42.0, 773.64,
								   QStringLiteral("USD"), QStringLiteral("Nasdaq")),
			automationStonksTicker(QStringLiteral("SAAB-B.ST"), QObject::tr("Saab AB"), 1, 20.0, 5848.0,
								   QStringLiteral("SEK"), QStringLiteral("Stockholm")) };
		const QVariantMap feedPreferences {
			{ QStringLiteral("showMine"), true },
			{ QStringLiteral("showPopular"), true },
			{ QStringLiteral("showPins"), true } };
		const QVariantList users { automationStonksUser(1, QStringLiteral("dankmaster"), false),
								   automationStonksUser(2, QStringLiteral("Trader Joe"), true),
								   automationStonksUser(3, QStringLiteral("Long Only"), false) };
		const QVariantList leaderboard {
			automationStonksLeaderboardRow(1, 2, QStringLiteral("Trader Joe"), 18.42, true),
			automationStonksLeaderboardRow(2, 1, QStringLiteral("dankmaster"), 9.75, false),
			automationStonksLeaderboardRow(3, 3, QStringLiteral("Long Only"), -2.35, false) };

		QVariantMap state;
		state.insert(QStringLiteral("supported"), true);
		state.insert(QStringLiteral("enabled"), true);
		state.insert(QStringLiteral("registered"), true);
		state.insert(QStringLiteral("canAdmin"), true);
		state.insert(QStringLiteral("selfUserId"), 1u);
		state.insert(QStringLiteral("selectedUserId"), 1u);
		state.insert(QStringLiteral("selectedUserName"), QStringLiteral("dankmaster"));
		state.insert(QStringLiteral("selectedPeriod"), QStringLiteral("30d"));
		state.insert(QStringLiteral("periods"), periods);
		state.insert(QStringLiteral("snapshots"), QVariantList { snapshot });
		state.insert(QStringLiteral("leaderboard"), leaderboard);
		state.insert(QStringLiteral("users"), users);
		state.insert(QStringLiteral("popularTickers"), popularTickers);
		state.insert(QStringLiteral("personalTickers"), personalTickers);
		state.insert(QStringLiteral("pinnedTickers"), pinnedTickers);
		state.insert(QStringLiteral("feedPreferences"), feedPreferences);
		state.insert(QStringLiteral("textChannelId"), 7u);
		state.insert(QStringLiteral("socialAnnouncementsEnabled"), true);
		state.insert(QStringLiteral("textChannels"),
					 QVariantList { QVariantMap { { QStringLiteral("textChannelId"), 7u },
												   { QStringLiteral("name"), QStringLiteral("stonks") } } });
		state.insert(QStringLiteral("leaderboardUpdatedAt"), QVariant::fromValue< qulonglong >(1779926400ULL));
		state.insert(QStringLiteral("leaderboardDescription"),
					 QObject::tr("Probe leaderboard comparing latest portfolio saves over 30 days."));
		state.insert(QStringLiteral("tickerBannerEnabled"), true);
		state.insert(QStringLiteral("tickerPlacement"), QStringLiteral("bottom"));
		state.insert(QStringLiteral("tickerDirection"), QStringLiteral("left"));
		state.insert(QStringLiteral("tickerSpeed"), QStringLiteral("normal"));
		state.insert(QStringLiteral("disableTickerAnimation"), true);
		state.insert(QStringLiteral("automationHeaderVisible"), true);
		state.insert(QStringLiteral("disableQuoteLookup"), true);

		const QString normalizedVariant = variant.trimmed().toLower();
		if (normalizedVariant == QLatin1String("loading") || normalizedVariant == QLatin1String("headerloading")) {
			state.insert(QStringLiteral("loading"), true);
			state.insert(QStringLiteral("status"), QObject::tr("Loading Stonks leaderboard and ticker quotes..."));
			state.insert(QStringLiteral("snapshots"), QVariantList());
			state.insert(QStringLiteral("leaderboard"), QVariantList());
			state.insert(QStringLiteral("tickerQuotes"), QVariantMap());
			return state;
		}
		if (normalizedVariant == QLatin1String("error") || normalizedVariant == QLatin1String("headererror")) {
			state.insert(QStringLiteral("status"), QObject::tr("Showing cached ticker symbols."));
			state.insert(QStringLiteral("error"), QObject::tr("Quote lookup is temporarily unavailable."));
			state.insert(QStringLiteral("tickerQuotes"),
						 QVariantMap { { QStringLiteral("RKLB"),
										 automationStonksQuoteError(QObject::tr("Provider timeout.")) },
									   { QStringLiteral("AMD"),
										 automationStonksQuoteError(QObject::tr("Provider timeout.")) },
									   { QStringLiteral("ERIC-B.ST"),
										 automationStonksQuoteError(QObject::tr("Provider timeout.")) } });
			return state;
		}
		if (normalizedVariant == QLatin1String("empty") || normalizedVariant == QLatin1String("headerempty")) {
			state.insert(QStringLiteral("status"), QObject::tr("No Stonks portfolio updates yet."));
			state.insert(QStringLiteral("snapshots"), QVariantList());
			state.insert(QStringLiteral("leaderboard"), QVariantList());
			state.insert(QStringLiteral("popularTickers"), QVariantList());
			state.insert(QStringLiteral("personalTickers"), QVariantList());
			state.insert(QStringLiteral("pinnedTickers"), QVariantList());
			state.insert(QStringLiteral("tickerQuotes"), QVariantMap());
			state.insert(QStringLiteral("leaderboardDescription"),
						 QObject::tr("No PnL rankings exist for the selected period yet."));
			return state;
		}
		if (normalizedVariant == QLatin1String("disabled") || normalizedVariant == QLatin1String("admindisabled")) {
			state.insert(QStringLiteral("enabled"), false);
			state.insert(QStringLiteral("status"), QObject::tr("Stonks is disabled for regular clients."));
			return state;
		}
		if (normalizedVariant == QLatin1String("nonadmin") || normalizedVariant == QLatin1String("readonly")) {
			state.insert(QStringLiteral("canAdmin"), false);
			state.insert(QStringLiteral("textChannels"), QVariantList());
			return state;
		}
		if (normalizedVariant == QLatin1String("disablednonadmin") || normalizedVariant == QLatin1String("readonlydisabled")) {
			state.insert(QStringLiteral("enabled"), false);
			state.insert(QStringLiteral("canAdmin"), false);
			state.insert(QStringLiteral("textChannels"), QVariantList());
			state.insert(QStringLiteral("status"), QObject::tr("Stonks is disabled on this server."));
			return state;
		}
		if (normalizedVariant.isEmpty() || normalizedVariant == QLatin1String("populated")
			|| normalizedVariant == QLatin1String("headerpopulated")) {
			state.insert(QStringLiteral("status"), QObject::tr("Updated from Modern automation probe data."));
			state.insert(QStringLiteral("tickerQuotes"),
						 QVariantMap { { QStringLiteral("RKLB"),
										 automationStonksQuote(18.42, 4.7, QStringLiteral("USD")) },
									   { QStringLiteral("AMD"),
										 automationStonksQuote(164.20, -1.3, QStringLiteral("USD")) },
									   { QStringLiteral("ERIC-B.ST"),
										 automationStonksQuote(85.80, 0.1, QStringLiteral("SEK")) },
									   { QStringLiteral("SAAB-B.ST"),
										 automationStonksQuote(292.40, 2.6, QStringLiteral("SEK")) } });
			return state;
		}

		return {};
	}

	QVariantMap automationStonksDialogProbe(const QString &variant) {
		const QVariantMap stonks = automationStonksStateProbe(variant);
		if (stonks.isEmpty()) {
			return {};
		}

		QVariantMap dialog;
		dialog.insert(QStringLiteral("id"), QStringLiteral("stonks"));
		dialog.insert(QStringLiteral("kind"), QStringLiteral("stonks"));
		dialog.insert(QStringLiteral("title"), QObject::tr("Stonks"));
		dialog.insert(QStringLiteral("subtitle"),
					  QObject::tr("Portfolio, leaderboard, following, and server settings."));
		dialog.insert(QStringLiteral("primaryActionId"), QStringLiteral("close"));
		dialog.insert(QStringLiteral("tone"), QStringLiteral("wide"));
		dialog.insert(QStringLiteral("width"), 760);
		dialog.insert(QStringLiteral("height"), 620);
		dialog.insert(QStringLiteral("stonks"), stonks);
		return dialog;
	}

	QVariantMap automationStonksActionSummary(const QString &actionID, const QVariantMap &payload,
											  const QString &selectedPeriod, const bool defaultEnabled,
											  const bool defaultSocialAnnouncementsEnabled) {
		const QString action = actionID.trimmed();
		const QString period =
			selectedPeriod.trimmed().isEmpty() ? QStringLiteral("30d") : selectedPeriod.trimmed();

		QVariantMap summary;
		summary.insert(QStringLiteral("recognized"), true);
		summary.insert(QStringLiteral("actionId"), action);
		summary.insert(QStringLiteral("period"), period);
		summary.insert(QStringLiteral("wouldSendServerMessage"), true);

		const auto targetUserID = [&payload]() {
			bool ok = false;
			const uint value = payload.value(QStringLiteral("userId")).toUInt(&ok);
			return ok && value > 0 ? value : 0u;
		};
		const auto applyTargetUser = [&summary, &targetUserID]() {
			const uint userID = targetUserID();
			summary.insert(QStringLiteral("hasTargetUserId"), userID > 0);
			if (userID > 0) {
				summary.insert(QStringLiteral("targetUserId"), userID);
			}
		};

		if (action == QLatin1String("savePortfolio") || action == QLatin1String("submitSnapshot")) {
			summary.insert(QStringLiteral("serverAction"), QStringLiteral("submitSnapshot"));
			summary.insert(QStringLiteral("protoAction"), QStringLiteral("StonksActionSubmitSnapshot"));
			applyTargetUser();

			const QVariantList positionValues = payload.value(QStringLiteral("positions")).toList();
			QVariantList positions;
			double totalMarketValue = 0.0;
			for (const QVariant &positionValue : positionValues) {
				const QVariantMap positionMap = positionValue.toMap();
				const double marketValue      = positionMap.value(QStringLiteral("marketValue")).toDouble();
				totalMarketValue += marketValue;

				QVariantMap position;
				position.insert(QStringLiteral("symbol"), positionMap.value(QStringLiteral("symbol")).toString());
				position.insert(QStringLiteral("quantity"), positionMap.value(QStringLiteral("quantity")).toDouble());
				position.insert(QStringLiteral("price"), positionMap.value(QStringLiteral("price")).toDouble());
				position.insert(QStringLiteral("marketValue"), marketValue);
				position.insert(QStringLiteral("currency"),
								positionMap.value(QStringLiteral("currency"), QStringLiteral("USD")).toString());
				position.insert(QStringLiteral("displayName"),
								positionMap.value(QStringLiteral("displayName")).toString());
				position.insert(QStringLiteral("providerId"),
								positionMap.value(QStringLiteral("providerId")).toString());
				position.insert(QStringLiteral("providerSymbol"),
								positionMap.value(QStringLiteral("providerSymbol")).toString());
				position.insert(QStringLiteral("exchange"), positionMap.value(QStringLiteral("exchange")).toString());
				position.insert(QStringLiteral("quoteTime"),
								QVariant::fromValue< qulonglong >(
									positionMap.value(QStringLiteral("quoteTime")).toULongLong()));
				position.insert(QStringLiteral("quoteSourceUrl"),
								positionMap.value(QStringLiteral("quoteSourceUrl")).toString());
				position.insert(QStringLiteral("quoteConfidence"),
								positionMap.value(QStringLiteral("quoteConfidence")).toDouble());
				positions.push_back(position);
			}

			QVariantMap snapshot;
			snapshot.insert(QStringLiteral("currency"),
							payload.value(QStringLiteral("currency"), QStringLiteral("USD")).toString());
			snapshot.insert(QStringLiteral("note"), payload.value(QStringLiteral("note")).toString());
			snapshot.insert(QStringLiteral("positionsCount"), positionValues.size());
			snapshot.insert(QStringLiteral("totalMarketValue"), totalMarketValue);
			snapshot.insert(QStringLiteral("positions"), positions);
			summary.insert(QStringLiteral("snapshot"), snapshot);
			return summary;
		}

		if (action == QLatin1String("clearPortfolio")) {
			summary.insert(QStringLiteral("serverAction"), QStringLiteral("clearPortfolio"));
			summary.insert(QStringLiteral("protoAction"), QStringLiteral("StonksActionClearPortfolio"));
			applyTargetUser();
			QVariantMap snapshot;
			snapshot.insert(QStringLiteral("currency"),
							payload.value(QStringLiteral("currency"), QStringLiteral("USD")).toString());
			snapshot.insert(QStringLiteral("note"),
							payload.value(QStringLiteral("note"), QObject::tr("Ledger cleared")).toString());
			summary.insert(QStringLiteral("snapshot"), snapshot);
			return summary;
		}

		if (action == QLatin1String("deleteSnapshot")) {
			summary.insert(QStringLiteral("serverAction"), QStringLiteral("deleteSnapshot"));
			summary.insert(QStringLiteral("protoAction"), QStringLiteral("StonksActionDeleteSnapshot"));
			applyTargetUser();
			const uint snapshotID = payload.value(QStringLiteral("snapshotId")).toUInt();
			summary.insert(QStringLiteral("hasSnapshotId"), snapshotID > 0);
			if (snapshotID > 0) {
				summary.insert(QStringLiteral("snapshotId"), snapshotID);
			}
			return summary;
		}

		if (action == QLatin1String("follow") || action == QLatin1String("unfollow")) {
			summary.insert(QStringLiteral("serverAction"), action);
			summary.insert(QStringLiteral("protoAction"),
						   action == QLatin1String("follow") ? QStringLiteral("StonksActionFollow")
															  : QStringLiteral("StonksActionUnfollow"));
			const uint userID = targetUserID();
			summary.insert(QStringLiteral("hasTargetUserId"), userID > 0);
			if (userID > 0) {
				summary.insert(QStringLiteral("targetUserId"), userID);
			} else {
				summary.insert(QStringLiteral("targetName"),
							   payload.value(QStringLiteral("userName")).toString());
			}
			return summary;
		}

		if (action == QLatin1String("configure")) {
			summary.insert(QStringLiteral("serverAction"), QStringLiteral("configure"));
			summary.insert(QStringLiteral("protoAction"), QStringLiteral("StonksActionConfigure"));
			summary.insert(QStringLiteral("enabled"),
						   payload.value(QStringLiteral("enabled"), defaultEnabled).toBool());
			summary.insert(QStringLiteral("textChannelId"),
						   payload.value(QStringLiteral("textChannelId")).toUInt());
			summary.insert(QStringLiteral("socialAnnouncementsEnabled"),
						   payload.value(QStringLiteral("socialAnnouncementsEnabled"),
										 defaultSocialAnnouncementsEnabled)
							   .toBool());
			return summary;
		}

		if (action == QLatin1String("setTickerPin")) {
			summary.insert(QStringLiteral("serverAction"), QStringLiteral("setTickerPin"));
			summary.insert(QStringLiteral("protoAction"), QStringLiteral("StonksActionSetTickerPin"));
			const QVariantMap ticker = payload.value(QStringLiteral("ticker")).toMap();
			const bool pinned        = payload.value(QStringLiteral("pinned"), ticker.value(QStringLiteral("pinned"), true))
								   .toBool();
			summary.insert(QStringLiteral("pinned"), pinned);
			summary.insert(QStringLiteral("displayOrder"),
						   payload.value(QStringLiteral("displayOrder"),
										 ticker.value(QStringLiteral("displayOrder"), 0))
							   .toUInt());
			QVariantMap tickerSummary;
			tickerSummary.insert(QStringLiteral("symbol"), ticker.value(QStringLiteral("symbol")).toString().trimmed());
			tickerSummary.insert(QStringLiteral("displayName"),
								 ticker.value(QStringLiteral("displayName")).toString().trimmed());
			tickerSummary.insert(QStringLiteral("providerId"),
								 ticker.value(QStringLiteral("providerId")).toString().trimmed());
			tickerSummary.insert(QStringLiteral("providerSymbol"),
								 ticker.value(QStringLiteral("providerSymbol"),
											  ticker.value(QStringLiteral("symbol")).toString())
									 .toString()
									 .trimmed());
			tickerSummary.insert(QStringLiteral("exchange"),
								 ticker.value(QStringLiteral("exchange")).toString().trimmed());
			tickerSummary.insert(QStringLiteral("quoteSourceUrl"),
								 ticker.value(QStringLiteral("quoteSourceUrl")).toString().trimmed());
			summary.insert(QStringLiteral("ticker"), tickerSummary);
			return summary;
		}

		if (action == QLatin1String("setFeedPreferences")) {
			summary.insert(QStringLiteral("serverAction"), QStringLiteral("setFeedPreferences"));
			summary.insert(QStringLiteral("protoAction"), QStringLiteral("StonksActionSetFeedPreferences"));
			const QVariantMap preferences = payload.value(QStringLiteral("feedPreferences")).toMap();
			QVariantMap preferenceSummary;
			preferenceSummary.insert(QStringLiteral("showMine"),
									 preferences.value(QStringLiteral("showMine"), true).toBool());
			preferenceSummary.insert(QStringLiteral("showPopular"),
									 preferences.value(QStringLiteral("showPopular"), true).toBool());
			preferenceSummary.insert(QStringLiteral("showPins"),
									 preferences.value(QStringLiteral("showPins"), true).toBool());
			summary.insert(QStringLiteral("feedPreferences"), preferenceSummary);
			return summary;
		}

		if (action == QLatin1String("setTickerPresentation")) {
			summary.insert(QStringLiteral("serverAction"), QStringLiteral("setTickerPresentation"));
			summary.insert(QStringLiteral("protoAction"), QStringLiteral("localSettings"));
			summary.insert(QStringLiteral("tickerBannerEnabled"),
						   payload.value(QStringLiteral("tickerBannerEnabled")));
			summary.insert(QStringLiteral("tickerPlacement"), payload.value(QStringLiteral("tickerPlacement")));
			summary.insert(QStringLiteral("tickerDirection"), payload.value(QStringLiteral("tickerDirection")));
			summary.insert(QStringLiteral("tickerSpeed"), payload.value(QStringLiteral("tickerSpeed")));
			return summary;
		}

		summary.insert(QStringLiteral("recognized"), false);
		summary.insert(QStringLiteral("wouldSendServerMessage"), false);
		return summary;
	}

	QString automationCertificateFingerprint(const Settings::KeyPair &keyPair) {
		if (!CertService::validate(keyPair)) {
			return QString();
		}

		return QString::fromLatin1(keyPair.first.constFirst().digest(QCryptographicHash::Sha1).toHex(':'));
	}

	QVariantMap automationCertificateRoundTripProbe(const QString &exportPath, const QString &name,
													 const QString &email) {
		const Settings::KeyPair original = Global::get().s.kpCertificate;

		QVariantMap result;
		result.insert(QStringLiteral("originalValid"), CertService::validate(original));
		result.insert(QStringLiteral("originalFingerprint"), automationCertificateFingerprint(original));
		result.insert(QStringLiteral("profileMutated"), false);

		const Settings::KeyPair generated = CertService::generate(name, email);
		const bool generatedValid         = CertService::validate(generated);
		const QString generatedFingerprint = automationCertificateFingerprint(generated);
		result.insert(QStringLiteral("generatedValid"), generatedValid);
		result.insert(QStringLiteral("generatedFingerprint"), generatedFingerprint);
		if (!generatedValid) {
			result.insert(QStringLiteral("error"), QObject::tr("Certificate generation failed."));
			return result;
		}

		QByteArray exported = CertService::exportPkcs12(generated);
		result.insert(QStringLiteral("exportBytes"), exported.size());
		if (exported.isEmpty()) {
			result.insert(QStringLiteral("error"), QObject::tr("Certificate export returned no bytes."));
			return result;
		}

		QString normalizedExportPath = QDir::fromNativeSeparators(exportPath.trimmed());
		if (normalizedExportPath.isEmpty()) {
			normalizedExportPath = QDir::temp().absoluteFilePath(QStringLiteral("mumble-modern-cert-probe.p12"));
		}
		const QFileInfo exportInfo(normalizedExportPath);
		if (!exportInfo.absoluteDir().exists() && !QDir().mkpath(exportInfo.absolutePath())) {
			result.insert(QStringLiteral("error"), QObject::tr("Certificate export directory could not be created."));
			return result;
		}

		QFile exportFile(normalizedExportPath);
		if (!exportFile.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Unbuffered)) {
			result.insert(QStringLiteral("error"), QObject::tr("Certificate export file could not be opened."));
			return result;
		}
		const qint64 written = exportFile.write(exported);
		exportFile.close();
		if (written != exported.size()) {
			result.insert(QStringLiteral("error"), QObject::tr("Certificate export file could not be written."));
			return result;
		}
		QFile::setPermissions(normalizedExportPath, QFile::ReadOwner | QFile::WriteOwner);

		const QFileInfo writtenInfo(normalizedExportPath);
		result.insert(QStringLiteral("exportPath"), QDir::toNativeSeparators(writtenInfo.absoluteFilePath()));
		result.insert(QStringLiteral("exportFileExists"), writtenInfo.exists());
		result.insert(QStringLiteral("exportFileSize"), writtenInfo.size());

		QFile importFile(normalizedExportPath);
		if (!importFile.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
			result.insert(QStringLiteral("error"), QObject::tr("Certificate export file could not be read."));
			return result;
		}
		const QByteArray importBytes = importFile.readAll();
		importFile.close();
		const Settings::KeyPair imported = CertService::importPkcs12(importBytes, QString());
		const QString importedFingerprint = automationCertificateFingerprint(imported);
		result.insert(QStringLiteral("importedValid"), CertService::validate(imported));
		result.insert(QStringLiteral("importedFingerprint"), importedFingerprint);
		result.insert(QStringLiteral("importMatchesGenerated"), importedFingerprint == generatedFingerprint);

		const Settings::KeyPair secondGenerated =
			CertService::generate(QStringLiteral("Mumble Modern Certificate Probe 2"), QString());
		const QString secondFingerprint = automationCertificateFingerprint(secondGenerated);
		result.insert(QStringLiteral("secondGeneratedValid"), CertService::validate(secondGenerated));
		result.insert(QStringLiteral("secondGeneratedFingerprint"), secondFingerprint);
		result.insert(QStringLiteral("secondDiffersFromFirst"),
					  !secondFingerprint.isEmpty() && secondFingerprint != generatedFingerprint);

		const Settings::KeyPair invalidImport =
			CertService::importPkcs12(QByteArray("not a pkcs12 certificate"), QString());
		result.insert(QStringLiteral("invalidImportRejected"), !CertService::validate(invalidImport));
		result.insert(QStringLiteral("restoredFingerprint"), automationCertificateFingerprint(Global::get().s.kpCertificate));
		result.insert(QStringLiteral("originalUnchanged"),
					  automationCertificateFingerprint(Global::get().s.kpCertificate)
						  == automationCertificateFingerprint(original));
		return result;
	}

	QString shortcutButtonEngineID(const QVariant &button) {
		const char *metaTypeName = button.metaType().name();
		const QString typeName  = metaTypeName ? QString::fromLatin1(metaTypeName).toLower() : QString();
		if (typeName.contains(QStringLiteral("inputkeyboard"))) {
			return QStringLiteral("keyboard");
		}
		if (typeName.contains(QStringLiteral("inputmouse"))) {
			return QStringLiteral("mouse");
		}
		if (typeName.contains(QStringLiteral("inputhid"))) {
			return QStringLiteral("rawInput");
		}
		if (typeName.contains(QStringLiteral("inputxinput"))) {
			return QStringLiteral("xinput");
		}
		if (typeName.contains(QStringLiteral("inputgkey"))) {
			return QStringLiteral("gkey");
		}
		return QStringLiteral("unknown");
	}

	QVariantMap shortcutButtonDiagnostic(const QVariant &button) {
		QVariantMap diagnostic;
		diagnostic.insert(QStringLiteral("engine"), shortcutButtonEngineID(button));
		const char *metaTypeName = button.metaType().name();
		diagnostic.insert(QStringLiteral("typeName"), metaTypeName ? QString::fromLatin1(metaTypeName) : QString());

		if (GlobalShortcutEngine::engine) {
			const GlobalShortcutEngine::ButtonInfo info = GlobalShortcutEngine::engine->buttonInfo(button);
			diagnostic.insert(QStringLiteral("device"), info.device);
			diagnostic.insert(QStringLiteral("devicePrefix"), info.devicePrefix);
			diagnostic.insert(QStringLiteral("name"), info.name);
			const QString label =
				!info.device.trimmed().isEmpty() && info.device.trimmed() != info.name.trimmed()
					? QObject::tr("%1: %2").arg(info.device.trimmed(), info.name.trimmed().isEmpty()
																		   ? QObject::tr("Unknown")
																		   : info.name.trimmed())
					: (info.name.trimmed().isEmpty() ? QObject::tr("Unknown") : info.name.trimmed());
			diagnostic.insert(QStringLiteral("label"), label);
		}

		return diagnostic;
	}

	QVariantMap shortcutEngineRow(const QString &id, const QString &label, const QString &settingsFlag,
								  const bool compiled, const bool userEnabled, const bool captureSupported,
								  const bool hardwareOptional) {
		QVariantMap row;
		row.insert(QStringLiteral("id"), id);
		row.insert(QStringLiteral("label"), label);
		row.insert(QStringLiteral("settingsFlag"), settingsFlag);
		row.insert(QStringLiteral("compiled"), compiled);
		row.insert(QStringLiteral("userEnabled"), userEnabled);
		row.insert(QStringLiteral("captureSupported"), captureSupported);
		row.insert(QStringLiteral("hardwareOptional"), hardwareOptional);
		row.insert(QStringLiteral("configuredButtonCount"), 0);
		row.insert(QStringLiteral("runtimeButtonCount"), 0);
		row.insert(QStringLiteral("sampleLabels"), QVariantList());
		return row;
	}

	void recordShortcutButtonForEngine(QVariantMap &row, const QVariantMap &buttonDiagnostic, const QString &countKey) {
		row.insert(countKey, row.value(countKey).toInt() + 1);
		QVariantList samples = row.value(QStringLiteral("sampleLabels")).toList();
		const QString label  = buttonDiagnostic.value(QStringLiteral("label")).toString().trimmed();
		if (!label.isEmpty() && !samples.contains(label) && samples.size() < 6) {
			samples.push_back(label);
			row.insert(QStringLiteral("sampleLabels"), samples);
		}
	}

	QVariantMap automationShortcutEngineDiagnostics() {
		const Settings &settings = Global::get().s;
		GlobalShortcutEngine *engine = GlobalShortcutEngine::engine;

		QVariantMap result;
		result.insert(QStringLiteral("globalShortcutsEnabled"), settings.bShortcutEnable);
		result.insert(QStringLiteral("enginePresent"), engine != nullptr);
		result.insert(QStringLiteral("engineRunning"), engine ? engine->isRunning() : false);
		result.insert(QStringLiteral("canCapture"), engine != nullptr);
		result.insert(QStringLiteral("canSuppress"), engine ? engine->canSuppress() : false);
		result.insert(QStringLiteral("canDisable"), engine ? engine->canDisable() : false);
		result.insert(QStringLiteral("engineEnabled"), engine ? engine->enabled() : false);
		result.insert(QStringLiteral("configuredShortcutCount"), settings.qlShortcuts.size());

		QVariantMap build;
#ifdef Q_OS_WIN
		build.insert(QStringLiteral("windows"), true);
#else
		build.insert(QStringLiteral("windows"), false);
#endif
#ifdef USE_GKEY
		build.insert(QStringLiteral("gkeyCompiled"), true);
#else
		build.insert(QStringLiteral("gkeyCompiled"), false);
#endif
#ifdef USE_XBOXINPUT
		build.insert(QStringLiteral("xinputCompiled"), true);
#else
		build.insert(QStringLiteral("xinputCompiled"), false);
#endif
		result.insert(QStringLiteral("build"), build);

		QVariantMap engineRows;
		engineRows.insert(QStringLiteral("keyboard"),
						  shortcutEngineRow(QStringLiteral("keyboard"), QObject::tr("Keyboard"),
											QStringLiteral("shortcut/enable"), true, settings.bShortcutEnable,
											engine != nullptr, false));
		engineRows.insert(QStringLiteral("mouse"),
						  shortcutEngineRow(QStringLiteral("mouse"), QObject::tr("Mouse"),
											QStringLiteral("shortcut/enable"), true, settings.bShortcutEnable,
											engine != nullptr, false));
		engineRows.insert(QStringLiteral("rawInput"),
						  shortcutEngineRow(QStringLiteral("rawInput"), QObject::tr("Raw input / HID"),
											QStringLiteral("shortcut/enable"), build.value(QStringLiteral("windows")).toBool(),
											settings.bShortcutEnable, engine != nullptr, true));
		engineRows.insert(QStringLiteral("uiAccess"),
						  shortcutEngineRow(QStringLiteral("uiAccess"),
											QObject::tr("Privileged applications"),
											QStringLiteral("shortcut/windows/uiaccess/enable"),
											build.value(QStringLiteral("windows")).toBool(),
											settings.bEnableUIAccess, engine != nullptr, true));
		engineRows.insert(QStringLiteral("gkey"),
						  shortcutEngineRow(QStringLiteral("gkey"), QObject::tr("GKey"),
											QStringLiteral("shortcut/gkey"),
											build.value(QStringLiteral("gkeyCompiled")).toBool(),
											settings.bEnableGKey, engine != nullptr, true));
		engineRows.insert(QStringLiteral("xinput"),
						  shortcutEngineRow(QStringLiteral("xinput"), QObject::tr("XInput"),
											QStringLiteral("shortcut/windows/xbox/enable"),
											build.value(QStringLiteral("xinputCompiled")).toBool(),
											settings.bEnableXboxInput, engine != nullptr, true));

		int assignedShortcutCount = 0;
		QVariantList configuredButtons;
		for (int shortcutIndex = 0; shortcutIndex < settings.qlShortcuts.size(); ++shortcutIndex) {
			const Shortcut &shortcut = settings.qlShortcuts.at(shortcutIndex);
			if (!shortcut.qlButtons.isEmpty()) {
				++assignedShortcutCount;
			}
			for (const QVariant &button : shortcut.qlButtons) {
				QVariantMap buttonDiagnostic = shortcutButtonDiagnostic(button);
				buttonDiagnostic.insert(QStringLiteral("shortcutIndex"), shortcutIndex);
				buttonDiagnostic.insert(QStringLiteral("actionIndex"), shortcut.iIndex);
				configuredButtons.push_back(buttonDiagnostic);

				const QString engineID = buttonDiagnostic.value(QStringLiteral("engine")).toString();
				if (engineRows.contains(engineID)) {
					QVariantMap row = engineRows.value(engineID).toMap();
					recordShortcutButtonForEngine(row, buttonDiagnostic, QStringLiteral("configuredButtonCount"));
					engineRows.insert(engineID, row);
				}
			}
		}
		result.insert(QStringLiteral("assignedShortcutCount"), assignedShortcutCount);
		result.insert(QStringLiteral("configuredButtons"), configuredButtons);

		QVariantList runtimeButtons;
		if (engine) {
			for (const QVariant &button : engine->qlButtonList) {
				QVariantMap buttonDiagnostic = shortcutButtonDiagnostic(button);
				runtimeButtons.push_back(buttonDiagnostic);
				const QString engineID = buttonDiagnostic.value(QStringLiteral("engine")).toString();
				if (engineRows.contains(engineID)) {
					QVariantMap row = engineRows.value(engineID).toMap();
					recordShortcutButtonForEngine(row, buttonDiagnostic, QStringLiteral("runtimeButtonCount"));
					engineRows.insert(engineID, row);
				}
			}
		}
		result.insert(QStringLiteral("runtimeButtons"), runtimeButtons);

		QVariantList engines;
		for (const QString &id : { QStringLiteral("keyboard"), QStringLiteral("mouse"), QStringLiteral("rawInput"),
								   QStringLiteral("uiAccess"), QStringLiteral("gkey"), QStringLiteral("xinput") }) {
			engines.push_back(engineRows.value(id).toMap());
		}
		result.insert(QStringLiteral("engines"), engines);
		result.insert(QStringLiteral("expectedModernControlLabels"),
					  QVariantList { QObject::tr("Enable global shortcuts"),
									 QObject::tr("Enable shortcuts in privileged applications"),
									 QObject::tr("Enable GKey"), QObject::tr("Enable XInput"),
									 QObject::tr("Start capture"), QObject::tr("Suppress") });
		result.insert(QStringLiteral("hardwareAbsenceIsNonFatal"), true);
		return result;
	}

	QString automationFeedbackBuildArchitecture() {
#ifdef MUMBLE_TARGET_ARCH
		return QString::fromUtf8(MUMBLE_TARGET_ARCH);
#else
		return OSInfo::getArchitecture(true);
#endif
	}

	QVariantMap automationFeedbackSubmitDryRun() {
		Mumble::Feedback::ReportFields fields;
		fields.kind = MumbleProto::FeedbackReportBug;
		fields.title = QObject::tr("Update window does not match mockup");
		fields.description =
			QObject::tr("The update available flow should use the same Modern shell dialog style as the mockup.");
		fields.reproductionSteps =
			QObject::tr("1. Open Help.\n2. Choose Check for updates.\n3. Compare the result dialog with the mockup.");
		fields.pastedEvidence = QObject::tr("Mockup slide 37 and API capture should match.");
		fields.diagnosticsIncluded = true;
		fields.diagnostics =
			QStringLiteral("Authorization: Bearer secret-token\n"
						   "Remote endpoint 192.168.50.200 should be redacted.\n"
						   "Visible diagnostic line.");
		fields.clientRelease = Version::getRelease();
		fields.clientArch = automationFeedbackBuildArchitecture();
		fields.clientOS = OSInfo::getOSDisplayableVersion();
		fields.clientQt = QString::fromLatin1(qVersion());
		fields.serverCapabilitySummary =
			QObject::tr("connected=no; feature=no; server-submit=no");

		const QString issueTitle = Mumble::Feedback::issueTitle(fields);
		const QString issueBody = Mumble::Feedback::issueBody(fields, Mumble::Feedback::DEFAULT_MAX_BODY_BYTES,
															   Mumble::Feedback::DEFAULT_MAX_LOG_BYTES);
		const QUrl fallbackUrl = Mumble::Feedback::fallbackIssueUrl(issueTitle, issueBody, fields.kind);
		const QUrlQuery query(fallbackUrl);

		QVariantMap result;
		result.insert(QStringLiteral("wouldOpenFallbackUrl"), true);
		result.insert(QStringLiteral("desktopOpenSuppressed"), true);
		result.insert(QStringLiteral("issueTitle"), issueTitle);
		result.insert(QStringLiteral("issueBodyBytes"), issueBody.toUtf8().size());
		result.insert(QStringLiteral("fallbackUrl"), fallbackUrl.toString(QUrl::FullyEncoded));
		result.insert(QStringLiteral("urlScheme"), fallbackUrl.scheme());
		result.insert(QStringLiteral("urlHost"), fallbackUrl.host());
		result.insert(QStringLiteral("urlPath"), fallbackUrl.path());
		result.insert(QStringLiteral("queryTitle"), query.queryItemValue(QStringLiteral("title")));
		result.insert(QStringLiteral("queryLabels"), query.queryItemValue(QStringLiteral("labels")));
		result.insert(QStringLiteral("queryHasBody"), !query.queryItemValue(QStringLiteral("body")).isEmpty());
		result.insert(QStringLiteral("bodyHasDescription"), issueBody.contains(QStringLiteral("### Description")));
		result.insert(QStringLiteral("bodyHasSteps"), issueBody.contains(QStringLiteral("### Steps to reproduce")));
		result.insert(QStringLiteral("bodyHasEvidence"), issueBody.contains(QStringLiteral("### Pasted evidence")));
		result.insert(QStringLiteral("bodyHasEnvironment"), issueBody.contains(QStringLiteral("### Client environment")));
		result.insert(QStringLiteral("diagnosticsRedacted"),
					  !issueBody.contains(QStringLiteral("secret-token"))
						  && issueBody.contains(QStringLiteral("[redacted diagnostic line]"))
						  && issueBody.contains(QStringLiteral("[redacted-ip]")));
		return result;
	}

	QString automationMotdContentSignature(const QString &value) {
		const QString text = value.trimmed();
		if (text.isEmpty()) {
			return QString();
		}

		quint32 hash = 2166136261u;
		for (const QChar ch : text) {
			hash ^= static_cast< quint32 >(ch.unicode());
			hash *= 16777619u;
		}
		return QStringLiteral("v1:%1:%2").arg(text.length()).arg(QString::number(hash, 16));
	}

	QString automationMotdServerStateKey() {
		QByteArray digest;
		QString host;
		QString username;
		QString password;
		unsigned short port = 0;
		if (Global::get().sh) {
			digest = Global::get().sh->serverDigest();
			Global::get().sh->getConnectionInfo(host, port, username, password);
		}
		return ModernMotd::serverStateKey(digest, host, port);
	}

	QVariantMap automationMotdSettingsState(MainWindow *window) {
		QVariantMap state = ModernMotd::serverViewState(
			Global::get().s.qsModernShellMotdServerStates, automationMotdServerStateKey());

		if (window && window->qmlShellHost()) {
			ClientSessionController *session = window->qmlShellHost()->sessionController();
			const QString motdHtml = session->motdHtml();
			const QString signature = automationMotdContentSignature(motdHtml);
			state.insert(QStringLiteral("motdHtmlPresent"), !motdHtml.trimmed().isEmpty());
			state.insert(QStringLiteral("motdSignature"), signature);
			state.insert(QStringLiteral("motdSummary"), session->motdSummary());
		}

		return state;
	}

	void restoreAutomationMotdSettings(MainWindow *window, const QString &serializedStates) {
		Global::get().s.qsModernShellMotdServerStates = serializedStates;
		Global::get().s.save();
		if (window && window->qmlShellHost()) {
			const QVariantMap state = automationMotdSettingsState(window);
			ClientSessionController *session = window->qmlShellHost()->sessionController();
			session->setMotdExpanded(state.value(QStringLiteral("expanded")).toBool());
			session->setMotdDismissedSignature(
				state.value(QStringLiteral("dismissedSignature")).toString());
			session->setMotdLastSeenSignature(
				state.value(QStringLiteral("lastSeenSignature")).toString());
		}
	}

	QVariantMap automationMotdActionDryRun(MainWindow *window) {
		QVariantMap result;
		if (!window) {
			result.insert(QStringLiteral("error"), QObject::tr("Main window is not available."));
			return result;
		}

		const QString originalStates = Global::get().s.qsModernShellMotdServerStates;
		const QString signature = QStringLiteral("automation-motd-persistence");

		const auto restoreOriginal = [&]() {
			restoreAutomationMotdSettings(window, originalStates);
		};

		const bool hideHandled = window->handleModernShellAppAction(QStringLiteral("motd.hide"));
		result.insert(QStringLiteral("hideHandled"), hideHandled);
		result.insert(QStringLiteral("hidePersisted"),
			!automationMotdSettingsState(window).value(QStringLiteral("expanded")).toBool());

		const bool showHandled = window->handleModernShellAppAction(QStringLiteral("motd.show"));
		result.insert(QStringLiteral("showHandled"), showHandled);
		result.insert(QStringLiteral("showPersisted"),
			automationMotdSettingsState(window).value(QStringLiteral("expanded")).toBool());

		const QVariantMap payload { { QStringLiteral("signature"), signature } };
		const bool dismissHandled = window->handleModernShellAppActionPayload(QStringLiteral("motd.dismiss"), payload);
		result.insert(QStringLiteral("dismissHandled"), dismissHandled);
		const QVariantMap dismissedState = automationMotdSettingsState(window);
		result.insert(QStringLiteral("dismissPersisted"),
					  dismissedState.value(QStringLiteral("dismissedSignature")).toString() == signature);
		result.insert(QStringLiteral("dismissMarksSeen"),
					  dismissedState.value(QStringLiteral("lastSeenSignature")).toString() == signature);

		const bool restoreHandled = window->handleModernShellAppActionPayload(QStringLiteral("motd.restore"), payload);
		result.insert(QStringLiteral("restoreHandled"), restoreHandled);
		result.insert(QStringLiteral("restorePersisted"),
					  automationMotdSettingsState(window)
						  .value(QStringLiteral("dismissedSignature")).toString().isEmpty());

		const bool markSeenHandled = window->handleModernShellAppActionPayload(QStringLiteral("motd.markSeen"), payload);
		result.insert(QStringLiteral("markSeenHandled"), markSeenHandled);
		result.insert(QStringLiteral("markSeenPersisted"),
					  automationMotdSettingsState(window)
						  .value(QStringLiteral("lastSeenSignature")).toString() == signature);

		restoreOriginal();
		result.insert(QStringLiteral("restoredOriginal"),
					  Global::get().s.qsModernShellMotdServerStates == originalStates);
		return result;
	}

	QVariantMap automationUpdateHandoffDryRun() {
		const QString releaseUrlText =
			QStringLiteral("https://github.com/dankmaster/mumble/releases/tag/mumble-forked");
		const QString installerUrlText =
			QStringLiteral("https://github.com/dankmaster/mumble/releases/download/mumble-forked/mumble-forked.msi");
		const QString validSha256 =
			QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

		QJsonObject installableInfo;
		installableInfo.insert(QStringLiteral("releaseUrl"), releaseUrlText);
		installableInfo.insert(QStringLiteral("installerUrl"), installerUrlText);
		installableInfo.insert(QStringLiteral("sha256"), validSha256);
		installableInfo.insert(QStringLiteral("version"), QStringLiteral("1.7.1"));
		installableInfo.insert(QStringLiteral("build"), 42);

		QJsonObject fallbackInfo = installableInfo;
		fallbackInfo.remove(QStringLiteral("sha256"));

		const QVariantMap fallbackSummary = VersionCheck::describeUpdateHandoff(fallbackInfo).toVariantMap();

		bool temporaryInstallerCreated = false;
		QString preparedInstallerSuffix;
		QVariantMap installableSummary;
		{
			QTemporaryDir tempDir;
			if (tempDir.isValid()) {
				const QString preparedInstallerPath = tempDir.filePath(QStringLiteral("mumble-forked.msi"));
				QFile preparedInstaller(preparedInstallerPath);
				if (preparedInstaller.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
					temporaryInstallerCreated = preparedInstaller.write("modern-update-dry-run") > 0;
					preparedInstaller.close();
					const QFileInfo preparedInfo(preparedInstallerPath);
					preparedInstallerSuffix = preparedInfo.suffix();
					installableSummary = VersionCheck::describeUpdateHandoff(installableInfo, preparedInstallerPath).toVariantMap();
				}
			}
		}
		if (installableSummary.isEmpty()) {
			installableSummary = VersionCheck::describeUpdateHandoff(installableInfo).toVariantMap();
		}

		const bool installableCanInstall =
			installableSummary.value(QStringLiteral("canInstallUpdate")).toBool();
		const bool fallbackCanInstall = fallbackSummary.value(QStringLiteral("canInstallUpdate")).toBool();

		QVariantMap result;
		result.insert(QStringLiteral("installableSummary"), installableSummary);
		result.insert(QStringLiteral("fallbackSummary"), fallbackSummary);
		result.insert(QStringLiteral("desktopOpenSuppressed"), true);
		result.insert(QStringLiteral("networkDownloadSuppressed"), true);
		result.insert(QStringLiteral("processLaunchSuppressed"), true);
		result.insert(QStringLiteral("installableCanInstall"), installableCanInstall);
		result.insert(QStringLiteral("fallbackCanInstall"), fallbackCanInstall);
		result.insert(QStringLiteral("installerTrusted"),
					  installableSummary.value(QStringLiteral("installerUrlTrusted")).toBool());
		result.insert(QStringLiteral("sha256Valid"), installableSummary.value(QStringLiteral("sha256Valid")).toBool());
		result.insert(QStringLiteral("validSha256Length"),
					  installableSummary.value(QStringLiteral("sha256Length")).toInt());
		result.insert(QStringLiteral("installerFileName"),
					  installableSummary.value(QStringLiteral("installerFileName")).toString());
		result.insert(QStringLiteral("maxRedirects"), installableSummary.value(QStringLiteral("maxRedirects")).toInt());
		result.insert(QStringLiteral("launchMode"), installableSummary.value(QStringLiteral("launchMode")).toString());
		result.insert(QStringLiteral("directLaunchMode"),
					  installableSummary.value(QStringLiteral("directLaunchMode")).toString());
		result.insert(QStringLiteral("releaseUrl"),
					  installableSummary.value(QStringLiteral("releaseUrl")).toString());
		result.insert(QStringLiteral("releaseUrlScheme"),
					  installableSummary.value(QStringLiteral("releaseUrlScheme")).toString());
		result.insert(QStringLiteral("releaseUrlHost"),
					  installableSummary.value(QStringLiteral("releaseUrlHost")).toString());
		result.insert(QStringLiteral("releaseUrlPath"),
					  installableSummary.value(QStringLiteral("releaseUrlPath")).toString());
		result.insert(QStringLiteral("installerUrl"),
					  installableSummary.value(QStringLiteral("installerUrl")).toString());
		result.insert(QStringLiteral("installerUrlScheme"),
					  installableSummary.value(QStringLiteral("installerUrlScheme")).toString());
		result.insert(QStringLiteral("installerUrlHost"),
					  installableSummary.value(QStringLiteral("installerUrlHost")).toString());
		result.insert(QStringLiteral("installerUrlPath"),
					  installableSummary.value(QStringLiteral("installerUrlPath")).toString());
		result.insert(QStringLiteral("openReleaseActionId"), QStringLiteral("openForkRelease"));
		result.insert(QStringLiteral("openInstallerActionId"), QStringLiteral("openForkInstaller"));
		result.insert(QStringLiteral("installActionId"), QStringLiteral("installForkUpdate"));
		result.insert(QStringLiteral("toastDownloadActionId"), QStringLiteral("app.update.download"));
		result.insert(QStringLiteral("toastRestartActionId"), QStringLiteral("app.update.restart"));
		result.insert(QStringLiteral("primaryActionForInstallable"), QStringLiteral("installForkUpdate"));
		result.insert(QStringLiteral("primaryActionForFallback"), QStringLiteral("openForkInstaller"));
		result.insert(QStringLiteral("wouldOpenReleaseUrl"),
					  installableSummary.value(QStringLiteral("wouldOpenReleaseUrl")).toBool());
		result.insert(QStringLiteral("wouldOpenInstallerUrl"),
					  installableSummary.value(QStringLiteral("wouldOpenInstallerUrl")).toBool());
		result.insert(QStringLiteral("downloadFallbackUrl"),
					  fallbackSummary.value(QStringLiteral("fallbackOpenUrl")).toString());
		result.insert(QStringLiteral("wouldStartVerifiedDownload"),
					  installableSummary.value(QStringLiteral("wouldStartVerifiedDownload")).toBool());
		result.insert(QStringLiteral("wouldFallbackToBrowserDownload"),
					  fallbackSummary.value(QStringLiteral("wouldFallbackToBrowserDownload")).toBool());
		result.insert(QStringLiteral("temporaryInstallerCreated"), temporaryInstallerCreated);
		result.insert(QStringLiteral("preparedInstallerSuffix"), preparedInstallerSuffix);
		result.insert(QStringLiteral("preparedInstallerAccepted"),
					  installableSummary.value(QStringLiteral("preparedInstallerAccepted")).toBool());
		result.insert(QStringLiteral("bundledUpdaterArguments"),
					  installableSummary.value(QStringLiteral("bundledUpdaterArguments")));
		result.insert(QStringLiteral("directMsiexecArguments"),
					  installableSummary.value(QStringLiteral("directMsiexecArguments")));
		result.insert(QStringLiteral("launchPreparedUpdateSuppressed"), true);
		result.insert(QStringLiteral("restartAfterInstallDefault"), true);
		return result;
	}

	QString automationPreviewImageDataUrl(const QString &title, const QString &subtitle, const QString &accent,
										  const QString &accent2 = QStringLiteral("#78b7d9")) {
		const QColor startColor(accent.trimmed().isEmpty() ? QStringLiteral("#51c8b3") : accent.trimmed());
		const QColor endColor(accent2.trimmed().isEmpty() ? QStringLiteral("#78b7d9") : accent2.trimmed());
		QImage image(QSize(960, 540), QImage::Format_ARGB32_Premultiplied);
		image.fill(QColor(QStringLiteral("#101823")));
		QPainter painter(&image);
		painter.setRenderHint(QPainter::Antialiasing, true);
		QLinearGradient gradient(QPointF(32, 32), QPointF(928, 508));
		gradient.setColorAt(0.0, startColor.isValid() ? startColor : QColor(QStringLiteral("#51c8b3")));
		gradient.setColorAt(1.0, endColor.isValid() ? endColor : QColor(QStringLiteral("#78b7d9")));
		painter.setPen(Qt::NoPen);
		painter.setBrush(gradient);
		painter.drawRoundedRect(QRectF(32, 32, 896, 476), 28, 28);
		painter.setBrush(QColor(255, 255, 255, 36));
		painter.drawEllipse(QPointF(774, 128), 86, 86);
		painter.setBrush(QColor(0, 0, 0, 45));
		painter.drawEllipse(QPointF(166, 418), 118, 118);
		painter.setPen(Qt::white);
		QFont titleFont(QStringLiteral("Segoe UI"), 40, QFont::Bold);
		painter.setFont(titleFont);
		painter.drawText(QRectF(72, 190, 816, 90), Qt::AlignLeft | Qt::AlignVCenter, title.left(80));
		painter.setPen(QColor(QStringLiteral("#dff8ff")));
		painter.setFont(QFont(QStringLiteral("Segoe UI"), 20));
		painter.drawText(QRectF(76, 278, 808, 68), Qt::AlignLeft | Qt::AlignVCenter, subtitle.left(120));
		painter.end();

		QByteArray bytes;
		QBuffer buffer(&bytes);
		if (!buffer.open(QIODevice::WriteOnly) || !image.save(&buffer, "PNG")) return {};
		return QStringLiteral("data:image/png;base64,") + QString::fromLatin1(bytes.toBase64());
	}

	QVariant automationRegisterPreviewImageValue(const QVariant &value,
											 const std::shared_ptr< QmlImagePipeline > &pipeline,
											 const QString &stablePath,
											 QHash< QString, QString > &registeredDataUrls) {
		if (value.typeId() == QMetaType::QString) {
			const QString source = value.toString();
			if (!source.startsWith(QLatin1String("data:image/"), Qt::CaseInsensitive)) return value;
			if (registeredDataUrls.contains(source)) return registeredDataUrls.value(source);
			const QString providerUrl = pipeline ? pipeline->registerDataUrl(source, stablePath) : QString();
			registeredDataUrls.insert(source, providerUrl);
			// A fixture must never leak a data URL into product QML, even if registration failed.
			return providerUrl;
		}

		if (value.typeId() == QMetaType::QVariantMap) {
			QVariantMap result;
			const QVariantMap source = value.toMap();
			for (auto it = source.cbegin(); it != source.cend(); ++it) {
				result.insert(it.key(), automationRegisterPreviewImageValue(
					it.value(), pipeline, stablePath + QLatin1Char(':') + it.key(), registeredDataUrls));
			}
			return result;
		}

		if (value.typeId() == QMetaType::QVariantList) {
			QVariantList result;
			const QVariantList source = value.toList();
			result.reserve(source.size());
			for (int index = 0; index < source.size(); ++index) {
				result.push_back(automationRegisterPreviewImageValue(
					source.at(index), pipeline, stablePath + QStringLiteral(":%1").arg(index), registeredDataUrls));
			}
			return result;
		}

		return value;
	}

	QVariantList automationRegisterPreviewImages(const QVariantList &messages,
											 const std::shared_ptr< QmlImagePipeline > &pipeline,
											 const QString &variant = QString()) {
		QHash< QString, QString > registeredDataUrls;
		QVariantList result;
		result.reserve(messages.size());
		for (int messageIndex = 0; messageIndex < messages.size(); ++messageIndex) {
			QVariantMap message = messages.at(messageIndex).toMap();
			const QString keyPrefix = QStringLiteral("automation:rich-preview:%1:%2")
									  .arg(variant.trimmed().toLower(), QString::number(messageIndex));
			message.insert(QStringLiteral("preview"),
				automationRegisterPreviewImageValue(message.value(QStringLiteral("preview")), pipeline,
					keyPrefix, registeredDataUrls));
			result.push_back(message);
		}
		return result;
	}

	void automationCollectPreviewImageSources(const QVariant &value, QStringList &providerUrls,
											 int &dataImageSourceCount) {
		if (value.typeId() == QMetaType::QString) {
			const QString source = value.toString().trimmed();
			if (source.startsWith(QLatin1String("image://mumble/"), Qt::CaseInsensitive)
				&& !providerUrls.contains(source)) {
				providerUrls.push_back(source);
			} else if (source.startsWith(QLatin1String("data:image/"), Qt::CaseInsensitive)) {
				++dataImageSourceCount;
			}
			return;
		}
		if (value.typeId() == QMetaType::QVariantMap) {
			const QVariantMap map = value.toMap();
			for (auto it = map.cbegin(); it != map.cend(); ++it) {
				automationCollectPreviewImageSources(it.value(), providerUrls, dataImageSourceCount);
			}
			return;
		}
		if (value.typeId() == QMetaType::QVariantList) {
			for (const QVariant &item : value.toList()) {
				automationCollectPreviewImageSources(item, providerUrls, dataImageSourceCount);
			}
		}
	}

	QVariantMap automationPreviewImageItem(const QString &source, const QString &title) {
		return QVariantMap { { QStringLiteral("kind"), QStringLiteral("image") },
			{ QStringLiteral("mime"), QStringLiteral("image/png") },
			{ QStringLiteral("url"), source }, { QStringLiteral("title"), title } };
	}

	QVariantMap automationPreviewSpec(const QString &label, const QString &value) {
		return QVariantMap { { QStringLiteral("label"), label }, { QStringLiteral("value"), value } };
	}

	QVariantMap automationRichPreviewMessage(const qulonglong messageID, const QString &actor,
											 const QString &bodyText, const QVariantMap &preview) {
		QVariantMap message;
		message.insert(QStringLiteral("messageId"), QVariant::fromValue< qulonglong >(messageID));
		message.insert(QStringLiteral("threadId"), QVariant::fromValue< qulonglong >(1));
		message.insert(QStringLiteral("createdAtMs"),
					   QVariant::fromValue< qulonglong >(1779926400000ULL + (messageID * 1000ULL)));
		message.insert(QStringLiteral("actor"), actor);
		message.insert(QStringLiteral("actorKey"), actor.toLower());
		message.insert(QStringLiteral("avatarUrl"), QString());
		message.insert(QStringLiteral("timeLabel"), QStringLiteral("14:20"));
		message.insert(QStringLiteral("scopeLabel"), QString());
		message.insert(QStringLiteral("bodyText"), bodyText);
		message.insert(QStringLiteral("bodyHtml"), bodyText.toHtmlEscaped());
		message.insert(QStringLiteral("own"), false);
		message.insert(QStringLiteral("system"), false);
		message.insert(QStringLiteral("deleted"), false);
		message.insert(QStringLiteral("canReply"), true);
		message.insert(QStringLiteral("canReact"), true);
		message.insert(QStringLiteral("canDelete"), true);
		message.insert(QStringLiteral("reactions"), QVariantList());
		message.insert(QStringLiteral("preview"), preview);
		return message;
	}

	QVariantMap automationDeliveryMessage(const qulonglong messageID, const QString &bodyText,
										   const QString &deliveryState = QString(),
										   const QString &deliveryLabel = QString()) {
		QVariantMap message;
		message.insert(QStringLiteral("messageId"), QVariant::fromValue< qulonglong >(messageID));
		message.insert(QStringLiteral("threadId"), QVariant::fromValue< qulonglong >(1));
		message.insert(QStringLiteral("createdAtMs"),
					   QVariant::fromValue< qulonglong >(1779926500000ULL + (messageID * 1000ULL)));
		message.insert(QStringLiteral("actor"), QObject::tr("You"));
		message.insert(QStringLiteral("actorKey"), QStringLiteral("self"));
		message.insert(QStringLiteral("avatarUrl"), QString());
		message.insert(QStringLiteral("timeLabel"), QStringLiteral("14:22"));
		message.insert(QStringLiteral("scopeLabel"), QString());
		message.insert(QStringLiteral("bodyText"), bodyText);
		message.insert(QStringLiteral("bodyHtml"), bodyText.toHtmlEscaped());
		message.insert(QStringLiteral("own"), true);
		message.insert(QStringLiteral("system"), false);
		message.insert(QStringLiteral("deleted"), false);
		message.insert(QStringLiteral("canReply"), false);
		message.insert(QStringLiteral("canReact"), false);
		message.insert(QStringLiteral("canDelete"), false);
		message.insert(QStringLiteral("reactions"), QVariantList());
		message.insert(QStringLiteral("clientMessageId"), QStringLiteral("delivery-probe-%1").arg(messageID));
		if (!deliveryState.trimmed().isEmpty()) {
			message.insert(QStringLiteral("deliveryState"), deliveryState.trimmed());
			message.insert(QStringLiteral("deliveryLabel"),
						   deliveryLabel.trimmed().isEmpty()
							   ? (deliveryState == QLatin1String("sending") ? QObject::tr("Sending...")
																			 : QObject::tr("Not delivered"))
							   : deliveryLabel.trimmed());
			message.insert(QStringLiteral("deliveryCanRetry"), deliveryState == QLatin1String("failed"));
			message.insert(QStringLiteral("deliveryRetryLabel"), QObject::tr("Retry"));
		}
		return message;
	}

	QVariantList automationMessageDeliveryProbeMessages() {
		return QVariantList {
			automationDeliveryMessage(8401, QObject::tr("This message was delivered normally.")),
			automationDeliveryMessage(8402, QObject::tr("Uploading this voice-room note now."),
									  QStringLiteral("sending"), QObject::tr("Sending...")),
			automationDeliveryMessage(8403, QObject::tr("The network dropped before this message reached the server."),
									  QStringLiteral("failed"), QObject::tr("Not delivered"))
		};
	}

	QVariantMap automationRichPreviewBase(const QString &url, const QString &title, const QString &subtitle,
										  const QString &description) {
		QVariantMap preview;
		preview.insert(QStringLiteral("kind"), QStringLiteral("link"));
		preview.insert(QStringLiteral("url"), url);
		preview.insert(QStringLiteral("title"), title);
		preview.insert(QStringLiteral("subtitle"), subtitle);
		preview.insert(QStringLiteral("description"), description);
		preview.insert(QStringLiteral("openLabel"), QObject::tr("Open link"));
		preview.insert(QStringLiteral("loading"), false);
		preview.insert(QStringLiteral("failed"), false);
		return preview;
	}

	void automationApplyPreviewSize(QVariantMap &preview, const QString &size) {
		const QString normalized = size.trimmed().toLower();
		if (normalized == QLatin1String("expanded")) {
			preview.insert(QStringLiteral("previewSize"), QStringLiteral("large"));
			return;
		}
		if (normalized == QLatin1String("compact") || normalized == QLatin1String("large")
			|| normalized == QLatin1String("default")) {
			preview.insert(QStringLiteral("previewSize"), normalized);
		}
	}

	QVariantMap automationProviderPreview(const QString &url, const QString &title, const QString &subtitle,
										 const QString &description, const QString &openLabel,
										 const QVariantMap &metadata, const QVariantList &mediaItems,
										 const QString &size) {
		QVariantMap preview = automationRichPreviewBase(url, title, subtitle, description);
		preview.insert(QStringLiteral("openLabel"), openLabel);
		preview.insert(QStringLiteral("metadata"), metadata);
		if (!mediaItems.isEmpty()) {
			preview.insert(QStringLiteral("mediaItems"), mediaItems);
			preview.insert(QStringLiteral("thumbnailUrl"),
				mediaItems.first().toMap().value(QStringLiteral("url")));
		}
		automationApplyPreviewSize(preview, size);
		return preview;
	}

	QVariantList automationProviderRichPreviewProbeMessages(const QString &variant, const QString &size) {
		const QString normalized = variant.trimmed().toLower();
		const QString actor = QStringLiteral("preview-bot");
		const auto fixtureImage = [](const QString &title, const QString &subtitle, const QString &accent,
									 const QString &accent2) {
			return automationPreviewImageDataUrl(title, subtitle, accent, accent2);
		};

		if (normalized == QLatin1String("finance")) {
			const QVariantList sparkline {
				QVariantMap { { QStringLiteral("timestamp"), QVariant::fromValue< qlonglong >(1779753600) },
					{ QStringLiteral("close"), 435.95 } },
				QVariantMap { { QStringLiteral("timestamp"), QVariant::fromValue< qlonglong >(1779840000) },
					{ QStringLiteral("close"), 438.10 } },
				QVariantMap { { QStringLiteral("timestamp"), QVariant::fromValue< qlonglong >(1779926400) },
					{ QStringLiteral("close"), 436.82 } },
				QVariantMap { { QStringLiteral("timestamp"), QVariant::fromValue< qlonglong >(1780012800) },
					{ QStringLiteral("close"), 442.35 } },
				QVariantMap { { QStringLiteral("timestamp"), QVariant::fromValue< qlonglong >(1780099200) },
					{ QStringLiteral("close"), 440.70 } },
				QVariantMap { { QStringLiteral("timestamp"), QVariant::fromValue< qlonglong >(1780185600) },
					{ QStringLiteral("close"), 445.18 } },
				QVariantMap { { QStringLiteral("timestamp"), QVariant::fromValue< qlonglong >(1780272000) },
					{ QStringLiteral("close"), 448.37 } }
			};
			const QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("yahoo-finance") },
				{ QStringLiteral("previewProvider"), QStringLiteral("yahoo-finance") },
				{ QStringLiteral("providerName"), QStringLiteral("Yahoo Finance") },
				{ QStringLiteral("tickerSymbol"), QStringLiteral("MSFT") },
				{ QStringLiteral("financeName"), QStringLiteral("Microsoft Corporation") },
				{ QStringLiteral("financeExchange"), QStringLiteral("NasdaqGS") },
				{ QStringLiteral("financeInstrument"), QStringLiteral("EQUITY") },
				{ QStringLiteral("financeCurrency"), QStringLiteral("USD") },
				{ QStringLiteral("financePrice"), QStringLiteral("448.37") },
				{ QStringLiteral("financeDayChange"), QStringLiteral("+5.21") },
				{ QStringLiteral("financeDayChangePercent"), QStringLiteral("+1.18%") },
				{ QStringLiteral("financeDayTrend"), QStringLiteral("up") },
				{ QStringLiteral("statusLabel"), QStringLiteral("1D +1.18%") },
				{ QStringLiteral("financeRangeLabel"), QStringLiteral("1M") },
				{ QStringLiteral("financeRangeChange"), QStringLiteral("+12.42") },
				{ QStringLiteral("financeRangeChangePercent"), QStringLiteral("+2.85%") },
				{ QStringLiteral("financeRangeTrend"), QStringLiteral("up") },
				{ QStringLiteral("financeUpdatedAt"), QStringLiteral("2026-05-28T14:20:00Z") },
				{ QStringLiteral("financeSparkline"), sparkline }
			};
			const QString url = QStringLiteral("https://finance.yahoo.com/quote/MSFT");
			const QVariantMap preview = automationProviderPreview(
				url, QStringLiteral("Microsoft Corporation (MSFT)"), QStringLiteral("Yahoo Finance"),
				QStringLiteral("448.37 USD · +5.21 (+1.18%)"), QStringLiteral("Open on Yahoo Finance"),
				metadata, {}, size);
			return { automationRichPreviewMessage(4294967396ULL, actor, url, preview) };
		}

		if (normalized == QLatin1String("product")) {
			const QString hero = fixtureImage(QStringLiteral("Logitech G Pro X Superlight 2"),
				QStringLiteral("Inet product fixture"), QStringLiteral("#086f83"), QStringLiteral("#49c5b6"));
			const QString side = fixtureImage(QStringLiteral("Side view"), QStringLiteral("Product gallery"),
				QStringLiteral("#283048"), QStringLiteral("#859398"));
			const QVariantList images { automationPreviewImageItem(hero, QStringLiteral("Product")),
				automationPreviewImageItem(side, QStringLiteral("Side view")) };
			const QVariantList specs { automationPreviewSpec(QStringLiteral("DPI"), QStringLiteral("44 000")),
				automationPreviewSpec(QStringLiteral("Anslutning"), QStringLiteral("LIGHTSPEED / USB-C")),
				automationPreviewSpec(QStringLiteral("Vikt"), QStringLiteral("60 g")) };
			const QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("inet") },
				{ QStringLiteral("previewProvider"), QStringLiteral("inet") },
				{ QStringLiteral("previewKind"), QStringLiteral("product") },
				{ QStringLiteral("swedishPreviewKind"), QStringLiteral("product") },
				{ QStringLiteral("providerName"), QStringLiteral("Inet") },
				{ QStringLiteral("richPreviewMetadataVersion"), 10 },
				{ QStringLiteral("productProvider"), QStringLiteral("Inet") },
				{ QStringLiteral("productId"), QStringLiteral("8901234") },
				{ QStringLiteral("productTitle"), QStringLiteral("Logitech G Pro X Superlight 2") },
				{ QStringLiteral("productDescription"),
				  QStringLiteral("Trådlös gamingmus med optisk HERO 2-sensor och låg vikt.") },
				{ QStringLiteral("productPrice"), QStringLiteral("1 499 kr") },
				{ QStringLiteral("productAvailability"), QStringLiteral("I lager online") },
				{ QStringLiteral("productSku"), QStringLiteral("910-006630") },
				{ QStringLiteral("productRating"), QStringLiteral("4.7/5") },
				{ QStringLiteral("productReviewCount"), QStringLiteral("128 recensioner") },
				{ QStringLiteral("productImage"), hero },
				{ QStringLiteral("productSpecs"), specs },
				{ QStringLiteral("productImages"), images },
				{ QStringLiteral("productMedia"), images }
			};
			const QString url = QStringLiteral("https://www.inet.se/produkt/8901234");
			const QVariantMap preview = automationProviderPreview(
				url, QStringLiteral("Logitech G Pro X Superlight 2"), QStringLiteral("Inet"),
				QStringLiteral("Trådlös gamingmus · 60 g · LIGHTSPEED"), QStringLiteral("Open on Inet"),
				metadata, images, size);
			return { automationRichPreviewMessage(4294967400ULL, actor, url, preview) };
		}

		if (normalized == QLatin1String("game-store") || normalized == QLatin1String("gamestore")) {
			const QString hero = fixtureImage(QStringLiteral("Hades II"), QStringLiteral("Steam store fixture"),
				QStringLiteral("#5b247a"), QStringLiteral("#d17c45"));
			const QString gameplay = fixtureImage(QStringLiteral("Gameplay"), QStringLiteral("Store gallery"),
				QStringLiteral("#16222a"), QStringLiteral("#3a6073"));
			const QVariantList images { automationPreviewImageItem(hero, QStringLiteral("Key art")),
				automationPreviewImageItem(gameplay, QStringLiteral("Gameplay")) };
			const QVariantList tags { QStringLiteral("Action Roguelike"), QStringLiteral("Mythology"),
				QStringLiteral("Singleplayer"), QStringLiteral("Early Access") };
			const QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("game-store") },
				{ QStringLiteral("previewProvider"), QStringLiteral("game-store") },
				{ QStringLiteral("previewKind"), QStringLiteral("gameStoreProduct") },
				{ QStringLiteral("gameStoreProvider"), QStringLiteral("steam") },
				{ QStringLiteral("gameStoreName"), QStringLiteral("Steam") },
				{ QStringLiteral("gameStoreProductTitle"), QStringLiteral("Hades II") },
				{ QStringLiteral("gameStoreDescription"),
				  QStringLiteral("Battle beyond the Underworld using dark sorcery and Olympian might.") },
				{ QStringLiteral("gameStorePrice"), QStringLiteral("29,99 €") },
				{ QStringLiteral("gameStoreOriginalPrice"), QStringLiteral("39,99 €") },
				{ QStringLiteral("gameStoreDiscount"), QStringLiteral("-25%") },
				{ QStringLiteral("gameStoreAvailability"), QStringLiteral("In stock") },
				{ QStringLiteral("gameStoreBrand"), QStringLiteral("Supergiant Games") },
				{ QStringLiteral("gameStoreSku"), QStringLiteral("1145350") },
				{ QStringLiteral("gameStoreRating"), QStringLiteral("Overwhelmingly Positive") },
				{ QStringLiteral("gameStoreReviewCount"), QStringLiteral("58,420") },
				{ QStringLiteral("gameStorePlatform"), QStringLiteral("Steam") },
				{ QStringLiteral("gameStoreImage"), hero },
				{ QStringLiteral("gameStoreTags"), tags },
				{ QStringLiteral("gameStoreImages"), images },
				{ QStringLiteral("gameStoreMedia"), images },
				{ QStringLiteral("gameStoreMediaItems"), images }
			};
			const QString url = QStringLiteral("https://store.steampowered.com/app/1145350/Hades_II/");
			const QVariantMap preview = automationProviderPreview(
				url, QStringLiteral("Hades II"), QStringLiteral("Steam"),
				QStringLiteral("Action roguelike · Early Access"), QStringLiteral("Open on Steam"), metadata,
				images, size);
			return { automationRichPreviewMessage(4294967401ULL, actor, url, preview) };
		}

		if (normalized == QLatin1String("marketplace")) {
			const QString hero = fixtureImage(QStringLiteral("Herman Miller Aeron"),
				QStringLiteral("Blocket listing fixture"), QStringLiteral("#1b5e20"), QStringLiteral("#66bb6a"));
			const QString detail = fixtureImage(QStringLiteral("Controls"), QStringLiteral("Listing detail"),
				QStringLiteral("#37474f"), QStringLiteral("#90a4ae"));
			const QVariantList images { automationPreviewImageItem(hero, QStringLiteral("Listing")),
				automationPreviewImageItem(detail, QStringLiteral("Controls")) };
			const QVariantList specs {
				automationPreviewSpec(QStringLiteral("Skick"), QStringLiteral("Begagnat – mycket gott")),
				automationPreviewSpec(QStringLiteral("Färg"), QStringLiteral("Graphite")),
				automationPreviewSpec(QStringLiteral("Leverans"), QStringLiteral("Hämtas"))
			};
			const QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("blocket") },
				{ QStringLiteral("previewProvider"), QStringLiteral("blocket") },
				{ QStringLiteral("previewKind"), QStringLiteral("marketplaceListing") },
				{ QStringLiteral("swedishPreviewKind"), QStringLiteral("marketplaceListing") },
				{ QStringLiteral("providerName"), QStringLiteral("Blocket") },
				{ QStringLiteral("richPreviewMetadataVersion"), 10 },
				{ QStringLiteral("marketplaceProvider"), QStringLiteral("blocket") },
				{ QStringLiteral("listingId"), QStringLiteral("1401234567") },
				{ QStringLiteral("listingTitle"), QStringLiteral("Herman Miller Aeron, storlek B") },
				{ QStringLiteral("listingPrice"), QStringLiteral("8 500 kr") },
				{ QStringLiteral("listingDescription"),
				  QStringLiteral("Varsamt använd kontorsstol med fullt fungerande reglage.") },
				{ QStringLiteral("listingLocation"), QStringLiteral("Stockholm") },
				{ QStringLiteral("listingSpecs"), specs },
				{ QStringLiteral("listingImages"), images }
			};
			const QString url = QStringLiteral("https://www.blocket.se/annons/1401234567");
			const QVariantMap preview = automationProviderPreview(
				url, QStringLiteral("Herman Miller Aeron, storlek B"), QStringLiteral("Blocket"),
				QStringLiteral("8 500 kr · Stockholm"), QStringLiteral("Open on Blocket"), metadata, images, size);
			return { automationRichPreviewMessage(4294967410ULL, actor, url, preview) };
		}

		if (normalized == QLatin1String("vehicle")) {
			const QString front = fixtureImage(QStringLiteral("Volvo EX30"), QStringLiteral("Bytbil exterior"),
				QStringLiteral("#b59f00"), QStringLiteral("#f2d64b"));
			const QString interior = fixtureImage(QStringLiteral("Interior"), QStringLiteral("Bytbil gallery"),
				QStringLiteral("#263238"), QStringLiteral("#78909c"));
			const QVariantList images { automationPreviewImageItem(front, QStringLiteral("Exterior")),
				automationPreviewImageItem(interior, QStringLiteral("Interior")) };
			const QVariantList specs {
				automationPreviewSpec(QStringLiteral("Årsmodell"), QStringLiteral("2025")),
				automationPreviewSpec(QStringLiteral("Miltal"), QStringLiteral("1 240 mil")),
				automationPreviewSpec(QStringLiteral("Drivmedel"), QStringLiteral("El")),
				automationPreviewSpec(QStringLiteral("Växellåda"), QStringLiteral("Automat")),
				automationPreviewSpec(QStringLiteral("Räckvidd (WLTP)"), QStringLiteral("450 km"))
			};
			const QVariantList highlights { QStringLiteral("Panoramatak"), QStringLiteral("360° kamera"),
				QStringLiteral("Harman Kardon"), QStringLiteral("Dragkrok") };
			const QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("bytbil") },
				{ QStringLiteral("previewProvider"), QStringLiteral("bytbil") },
				{ QStringLiteral("previewKind"), QStringLiteral("vehicleListing") },
				{ QStringLiteral("swedishPreviewKind"), QStringLiteral("vehicleListing") },
				{ QStringLiteral("providerName"), QStringLiteral("Bytbil") },
				{ QStringLiteral("vehicleProvider"), QStringLiteral("Bytbil") },
				{ QStringLiteral("richPreviewMetadataVersion"), 10 },
				{ QStringLiteral("listingId"), QStringLiteral("123456789") },
				{ QStringLiteral("vehicleListingId"), QStringLiteral("123456789") },
				{ QStringLiteral("vehicleKind"), QStringLiteral("Personbil") },
				{ QStringLiteral("vehicleTitle"), QStringLiteral("Volvo EX30 Twin Motor Performance Ultra") },
				{ QStringLiteral("listingTitle"), QStringLiteral("Volvo EX30 Twin Motor Performance Ultra") },
				{ QStringLiteral("vehiclePrice"), QStringLiteral("529 900 kr") },
				{ QStringLiteral("listingPrice"), QStringLiteral("529 900 kr") },
				{ QStringLiteral("vehicleDescription"),
				  QStringLiteral("Svensksåld elbil med fyrhjulsdrift och panoramatak.") },
				{ QStringLiteral("listingDescription"),
				  QStringLiteral("Svensksåld elbil med fyrhjulsdrift och panoramatak.") },
				{ QStringLiteral("vehicleDealer"), QStringLiteral("Fixture Bil AB") },
				{ QStringLiteral("vehicleLocation"), QStringLiteral("Göteborg") },
				{ QStringLiteral("listingLocation"), QStringLiteral("Göteborg") },
				{ QStringLiteral("vehicleYear"), QStringLiteral("2025") },
				{ QStringLiteral("vehicleMileage"), QStringLiteral("1 240 mil") },
				{ QStringLiteral("vehicleFuel"), QStringLiteral("El") },
				{ QStringLiteral("vehicleTransmission"), QStringLiteral("Automat") },
				{ QStringLiteral("vehicleDrivetrain"), QStringLiteral("Fyrhjulsdrift") },
				{ QStringLiteral("vehicleBody"), QStringLiteral("SUV") },
				{ QStringLiteral("vehicleColor"), QStringLiteral("Moss Yellow") },
				{ QStringLiteral("vehicleRegNo"), QStringLiteral("ABC12D") },
				{ QStringLiteral("vehicleSeats"), QStringLiteral("5") },
				{ QStringLiteral("vehicleRange"), QStringLiteral("450 km") },
				{ QStringLiteral("vehiclePower"), QStringLiteral("428 hk") },
				{ QStringLiteral("vehicleSpecs"), specs },
				{ QStringLiteral("listingSpecs"), specs },
				{ QStringLiteral("vehicleHighlights"), highlights },
				{ QStringLiteral("vehicleImages"), images },
				{ QStringLiteral("listingImages"), images },
				{ QStringLiteral("vehicleImage"), front }
			};
			const QString url = QStringLiteral("https://www.bytbil.com/personbil-volvo-ex30-123456789");
			const QVariantMap preview = automationProviderPreview(
				url, QStringLiteral("Volvo EX30 Twin Motor Performance Ultra"), QStringLiteral("Bytbil"),
				QStringLiteral("529 900 kr · 2025 · 1 240 mil · Göteborg"), QStringLiteral("Open on Bytbil"),
				metadata, images, size);
			return { automationRichPreviewMessage(4294967420ULL, actor, url, preview) };
		}

		if (normalized == QLatin1String("real-estate") || normalized == QLatin1String("realestate")) {
			const QString exterior = fixtureImage(QStringLiteral("Ljus trea med balkong"),
				QStringLiteral("Hemnet fixture"), QStringLiteral("#6d4c41"), QStringLiteral("#bcaaa4"));
			const QString plan = fixtureImage(QStringLiteral("Floor plan"), QStringLiteral("78 m² · 3 rum"),
				QStringLiteral("#455a64"), QStringLiteral("#cfd8dc"));
			const QVariantList images { automationPreviewImageItem(exterior, QStringLiteral("Exterior")),
				automationPreviewImageItem(plan, QStringLiteral("Floor plan")) };
			const QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("hemnet") },
				{ QStringLiteral("previewProvider"), QStringLiteral("hemnet") },
				{ QStringLiteral("previewKind"), QStringLiteral("realEstate") },
				{ QStringLiteral("swedishPreviewKind"), QStringLiteral("realEstate") },
				{ QStringLiteral("providerName"), QStringLiteral("Hemnet") },
				{ QStringLiteral("richPreviewMetadataVersion"), 10 },
				{ QStringLiteral("listingPrice"), QStringLiteral("4 995 000 kr") },
				{ QStringLiteral("realEstateArea"), QStringLiteral("78 m²") },
				{ QStringLiteral("realEstateRooms"), QStringLiteral("3 rum") },
				{ QStringLiteral("realEstateFee"), QStringLiteral("4 218 kr/mån") }
			};
			const QString url = QStringLiteral("https://www.hemnet.se/bostad/fixture-12345");
			const QVariantMap preview = automationProviderPreview(
				url, QStringLiteral("Ljus trea med balkong · Södermalm"), QStringLiteral("Hemnet"),
				QStringLiteral("78 m² · 3 rum · våning 4"), QStringLiteral("Open on Hemnet"), metadata,
				images, size);
			return { automationRichPreviewMessage(4294967430ULL, actor, url, preview) };
		}

		if (normalized == QLatin1String("editorial")) {
			const QString articleImage = fixtureImage(QStringLiteral("Datacenter i Sverige"),
				QStringLiteral("SVT article fixture"), QStringLiteral("#8b1538"), QStringLiteral("#d94f70"));
			const QVariantList articleImages {
				automationPreviewImageItem(articleImage, QStringLiteral("Datacenter illustration"))
			};
			const QVariantMap articleMetadata {
				{ QStringLiteral("provider"), QStringLiteral("svt") },
				{ QStringLiteral("previewProvider"), QStringLiteral("svt") },
				{ QStringLiteral("previewKind"), QStringLiteral("article") },
				{ QStringLiteral("swedishPreviewKind"), QStringLiteral("article") },
				{ QStringLiteral("providerName"), QStringLiteral("SVT") },
				{ QStringLiteral("richPreviewMetadataVersion"), 10 },
				{ QStringLiteral("articlePublisher"), QStringLiteral("SVT") },
				{ QStringLiteral("articleTitle"),
				  QStringLiteral("Ny metod minskar energianvändningen i svenska datacenter") },
				{ QStringLiteral("articleDescription"),
				  QStringLiteral("Forskare visar hur last kan flyttas utan att svarstider försämras.") },
				{ QStringLiteral("articleSection"), QStringLiteral("Teknik") },
				{ QStringLiteral("articlePublishedAt"), QStringLiteral("2026-05-28T09:15:00Z") },
				{ QStringLiteral("articleModifiedAt"), QStringLiteral("2026-05-28T11:42:00Z") },
				{ QStringLiteral("articleAuthor"), QStringLiteral("Alex Nilsson") },
				{ QStringLiteral("articleImage"), articleImage },
				{ QStringLiteral("articleImages"), articleImages }
			};
			const QString articleUrl = QStringLiteral("https://www.svt.se/nyheter/inrikes/datacenter-fixture");
			const QVariantMap articlePreview = automationProviderPreview(articleUrl,
				QStringLiteral("Ny metod minskar energianvändningen i svenska datacenter"),
				QStringLiteral("SVT · Teknik"),
				QStringLiteral("Forskare visar hur last kan flyttas utan att svarstider försämras."),
				QStringLiteral("Open on SVT"), articleMetadata, articleImages, size);

			const QVariantMap forumMetadata {
				{ QStringLiteral("provider"), QStringLiteral("flashback") },
				{ QStringLiteral("previewProvider"), QStringLiteral("flashback") },
				{ QStringLiteral("previewKind"), QStringLiteral("forum") },
				{ QStringLiteral("swedishPreviewKind"), QStringLiteral("forum") },
				{ QStringLiteral("providerName"), QStringLiteral("Flashback") },
				{ QStringLiteral("richPreviewMetadataVersion"), 10 },
				{ QStringLiteral("threadId"), QStringLiteral("4829101") },
				{ QStringLiteral("forumProvider"), QStringLiteral("Flashback") },
				{ QStringLiteral("forumThreadId"), QStringLiteral("4829101") },
				{ QStringLiteral("forumThreadTitle"), QStringLiteral("Qt Quick-prestanda och renderloopar") },
				{ QStringLiteral("forumLinkKind"), QStringLiteral("post") },
				{ QStringLiteral("postId"), QStringLiteral("91345012") },
				{ QStringLiteral("forumLinkedPostId"), QStringLiteral("91345012") },
				{ QStringLiteral("forumThreadPostUrl"), QStringLiteral("https://www.flashback.org/p91345012") },
				{ QStringLiteral("forumCategory"), QStringLiteral("Dator och IT") },
				{ QStringLiteral("forumName"), QStringLiteral("Programmering") },
				{ QStringLiteral("forumPage"), QStringLiteral("42") },
				{ QStringLiteral("forumPageCount"), QStringLiteral("87") },
				{ QStringLiteral("forumPostCount"), QStringLiteral("1294") },
				{ QStringLiteral("forumPostId"), QStringLiteral("91345012") },
				{ QStringLiteral("forumPostNumber"), QStringLiteral("#628") },
				{ QStringLiteral("forumPostAuthor"), QStringLiteral("rendernisse") },
				{ QStringLiteral("forumPostTime"), QStringLiteral("2026-05-28 13:37") },
				{ QStringLiteral("forumPostExcerpt"),
				  QStringLiteral("Frame pacing blev stabilare när täta presence-signaler batchades per render frame.") }
			};
			const QString forumUrl = QStringLiteral("https://www.flashback.org/p91345012");
			const QVariantMap forumPreview = automationProviderPreview(forumUrl,
				QStringLiteral("Qt Quick-prestanda och renderloopar"),
				QStringLiteral("Flashback · Dator och IT"),
				QStringLiteral("Frame pacing blev stabilare när täta presence-signaler batchades per render frame."),
				QStringLiteral("Open discussion"), forumMetadata, {}, size);

			return { automationRichPreviewMessage(4294967440ULL, actor, articleUrl, articlePreview),
				automationRichPreviewMessage(4294967441ULL, actor, forumUrl, forumPreview) };
		}

		if (normalized == QLatin1String("audio")) {
			const QString artwork = fixtureImage(QStringLiteral("Vetenskapsradion"),
				QStringLiteral("Sveriges Radio audio fixture"), QStringLiteral("#e65100"), QStringLiteral("#ffb74d"));
			const QVariantList images { automationPreviewImageItem(artwork, QStringLiteral("Program artwork")) };
			const QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("sverigesradio") },
				{ QStringLiteral("previewProvider"), QStringLiteral("sverigesradio") },
				{ QStringLiteral("previewKind"), QStringLiteral("audio") },
				{ QStringLiteral("swedishPreviewKind"), QStringLiteral("audio") },
				{ QStringLiteral("providerName"), QStringLiteral("Sveriges Radio") },
				{ QStringLiteral("richPreviewMetadataVersion"), 10 },
				{ QStringLiteral("audioProvider"), QStringLiteral("Sveriges Radio") },
				{ QStringLiteral("audioProgram"), QStringLiteral("Vetenskapsradion") },
				{ QStringLiteral("articlePublishedAt"), QStringLiteral("2026-05-28T06:00:00Z") }
			};
			const QString url = QStringLiteral("https://sverigesradio.se/avsnitt/fixture-vetenskapsradion");
			const QVariantMap preview = automationProviderPreview(url,
				QStringLiteral("Så blir framtidens datacenter mer energieffektiva"),
				QStringLiteral("Vetenskapsradion · Sveriges Radio"),
				QStringLiteral("Ett samtal om lastbalansering, kylning och mätbar prestanda."),
				QStringLiteral("Open on Sveriges Radio"), metadata, images, size);
			return { automationRichPreviewMessage(4294967450ULL, actor, url, preview) };
		}

		if (normalized == QLatin1String("social-code") || normalized == QLatin1String("socialcode")) {
			const QString xAvatar = fixtureImage(QStringLiteral("MD"), QStringLiteral("Mumble Design avatar"),
				QStringLiteral("#111827"), QStringLiteral("#1d9bf0"));
			const QString xMedia = fixtureImage(QStringLiteral("Stable frame pacing"),
				QStringLiteral("Qt Quick render timeline"), QStringLiteral("#14171a"), QStringLiteral("#1d9bf0"));
			const QVariantMap xMetadata {
				{ QStringLiteral("provider"), QStringLiteral("x") },
				{ QStringLiteral("previewProvider"), QStringLiteral("x") },
				{ QStringLiteral("providerName"), QStringLiteral("X") },
				{ QStringLiteral("xDisplayName"), QStringLiteral("Mumble Design") },
				{ QStringLiteral("xHandle"), QStringLiteral("@mumbledesign") },
				{ QStringLiteral("xAvatarUrl"), xAvatar },
				{ QStringLiteral("xVerified"), true },
				{ QStringLiteral("xCreatedAt"), QStringLiteral("2026-05-28T18:30:00Z") },
				{ QStringLiteral("xReplyCount"), 757 },
				{ QStringLiteral("xRepostCount"), 12000 },
				{ QStringLiteral("xQuoteCount"), 420 },
				{ QStringLiteral("xLikeCount"), 362000 },
				{ QStringLiteral("xViewCount"), 8100000 },
				{ QStringLiteral("xBookmarkCount"), 9000 },
				{ QStringLiteral("xStatsFetchedAt"), QStringLiteral("2026-05-28T18:31:00.000Z") },
				{ QStringLiteral("xReplyContext"),
				  QVariantList { QVariantMap {
					  { QStringLiteral("displayName"), QStringLiteral("Qt Quick Notes") },
					  { QStringLiteral("handle"), QStringLiteral("@quicknotes") },
					  { QStringLiteral("verified"), true },
					  { QStringLiteral("createdAt"), QStringLiteral("2026-05-28T17:58:00Z") },
					  { QStringLiteral("text"), QStringLiteral("Stable IDs make delegate reuse predictable.") },
					  { QStringLiteral("likeCount"), 2048 } } } },
				{ QStringLiteral("xQuotedPost"),
				  QVariantMap { { QStringLiteral("displayName"), QStringLiteral("Designer Notes") },
					  { QStringLiteral("handle"), QStringLiteral("@designnotes") },
					  { QStringLiteral("verified"), true },
					  { QStringLiteral("createdAt"), QStringLiteral("2026-05-27T12:00:00Z") },
					  { QStringLiteral("text"),
						QStringLiteral("Keep loading, empty and failure states deliberate.") },
					  { QStringLiteral("likeCount"), 42000 } } }
			};
			const QString xUrl = QStringLiteral("https://x.com/mumbledesign/status/1795880000000000000");
			const QVariantList xMediaItems { automationPreviewImageItem(xMedia, QStringLiteral("Post media")) };
			const QVariantMap xPreview = automationProviderPreview(xUrl,
				QStringLiteral("Mumble Design (@mumbledesign)"), QStringLiteral("X · verified"),
				QStringLiteral("Frame pacing is a feature: stable IDs, bounded delegates, and no model resets."),
				QStringLiteral("Open on X"), xMetadata, xMediaItems, size);

			const QString githubAvatar = fixtureImage(QStringLiteral("DM"), QStringLiteral("Repository owner"),
				QStringLiteral("#24292f"), QStringLiteral("#6e7781"));
			const QVariantMap githubMetadata {
				{ QStringLiteral("provider"), QStringLiteral("github") },
				{ QStringLiteral("previewProvider"), QStringLiteral("github") },
				{ QStringLiteral("providerName"), QStringLiteral("GitHub") },
				{ QStringLiteral("githubOwner"), QStringLiteral("dankmaster") },
				{ QStringLiteral("githubRepo"), QStringLiteral("mumble") },
				{ QStringLiteral("githubFullName"), QStringLiteral("dankmaster/mumble") },
				{ QStringLiteral("githubHtmlUrl"), QStringLiteral("https://github.com/dankmaster/mumble") },
				{ QStringLiteral("githubDescription"),
				  QStringLiteral("A performance-focused Mumble fork with a native Qt Quick client.") },
				{ QStringLiteral("githubLanguage"), QStringLiteral("C++") },
				{ QStringLiteral("githubDefaultBranch"), QStringLiteral("master") },
				{ QStringLiteral("githubPushedAt"), QStringLiteral("2026-05-28T18:00:00Z") },
				{ QStringLiteral("githubOwnerLogin"), QStringLiteral("dankmaster") },
				{ QStringLiteral("githubOwnerAvatarUrl"), githubAvatar },
				{ QStringLiteral("githubLicense"), QStringLiteral("BSD-3-Clause") },
				{ QStringLiteral("githubStars"), 4200 },
				{ QStringLiteral("githubForks"), 318 },
				{ QStringLiteral("githubOpenIssues"), 27 },
				{ QStringLiteral("githubTopics"),
				  QVariantList { QStringLiteral("mumble"), QStringLiteral("qt-quick"),
					  QStringLiteral("voice-chat"), QStringLiteral("qml") } },
				{ QStringLiteral("githubLatestReleaseTag"), QStringLiteral("mumble-forked-2026.05") },
				{ QStringLiteral("githubLatestReleaseName"), QStringLiteral("May client polish") },
				{ QStringLiteral("githubLatestReleaseUrl"),
				  QStringLiteral("https://github.com/dankmaster/mumble/releases/tag/mumble-forked-2026.05") },
				{ QStringLiteral("githubLatestReleasePublishedAt"), QStringLiteral("2026-05-26T20:00:00Z") },
				{ QStringLiteral("githubLatestReleaseNotes"),
				  QStringLiteral("Native preview cards, smoother chat and improved accessibility.") },
				{ QStringLiteral("githubLatestReleaseAssetName"), QStringLiteral("mumble-forked-x64.msi") },
				{ QStringLiteral("githubLatestReleaseAssetUrl"),
				  QStringLiteral("https://github.com/dankmaster/mumble/releases/download/mumble-forked-2026.05/mumble-forked-x64.msi") },
				{ QStringLiteral("githubLatestReleaseAssetCount"), 2 },
				{ QStringLiteral("githubLatestReleaseDownloadCount"), 18420 }
			};
			const QString githubUrl = QStringLiteral("https://github.com/dankmaster/mumble");
			const QVariantList githubMediaItems {
				automationPreviewImageItem(githubAvatar, QStringLiteral("Repository owner"))
			};
			const QVariantMap githubPreview = automationProviderPreview(githubUrl,
				QStringLiteral("dankmaster/mumble"), QStringLiteral("GitHub · C++"),
				QStringLiteral("A performance-focused Mumble fork with a native Qt Quick client."),
				QStringLiteral("Open on GitHub"), githubMetadata, githubMediaItems, size);

			return { automationRichPreviewMessage(4294967460ULL, actor, xUrl, xPreview),
				automationRichPreviewMessage(4294967461ULL, actor, githubUrl, githubPreview) };
		}

		if (normalized == QLatin1String("twitch-live") || normalized == QLatin1String("twitch-offline")
			|| normalized == QLatin1String("twitch-rerun") || normalized == QLatin1String("twitch-error")
			|| normalized == QLatin1String("twitch-embed")) {
			const bool isLive = normalized == QLatin1String("twitch-live");
			const bool isOffline = normalized == QLatin1String("twitch-offline");
			const bool isRerun = normalized == QLatin1String("twitch-rerun");
			const bool isError = normalized == QLatin1String("twitch-error");
			const bool isEmbed = normalized == QLatin1String("twitch-embed");
			const QString channel = isEmbed ? QStringLiteral("mumbleclips") : QStringLiteral("mumbledev");
			const QString url = isEmbed ? QStringLiteral("https://www.twitch.tv/mumbleclips/clip/StableFramePacing")
										: QStringLiteral("https://www.twitch.tv/mumbledev");
			const QString thumbnail = fixtureImage(
				isLive ? QStringLiteral("LIVE · Mumble Dev")
					   : (isOffline ? QStringLiteral("Latest VOD")
								: (isRerun ? QStringLiteral("RERUN · UI review")
										   : (isEmbed ? QStringLiteral("Featured clip")
														: QStringLiteral("Twitch unavailable")))),
				isLive ? QStringLiteral("Native Qt Quick client polish")
					   : (isOffline ? QStringLiteral("Offline · recorded 2 hours ago")
								: (isRerun ? QStringLiteral("Accessibility and focus pass")
										   : (isEmbed ? QStringLiteral("Clip · 00:42")
														: QStringLiteral("Deterministic failure fixture")))),
				isError ? QStringLiteral("#374151") : QStringLiteral("#6441a5"),
				isLive ? QStringLiteral("#e91916") : QStringLiteral("#9147ff"));
			const QString liveState = isLive ? QStringLiteral("live")
				: (isOffline || isEmbed ? QStringLiteral("offline")
					: (isRerun ? QStringLiteral("rerun") : QStringLiteral("unavailable")));
			const QString badge = isLive ? QStringLiteral("Live")
				: (isOffline ? QStringLiteral("Offline")
					: (isRerun ? QStringLiteral("Rerun")
						: (isEmbed ? QStringLiteral("Clip") : QStringLiteral("Unavailable"))));
			const QString embedMode = isLive ? QStringLiteral("live")
				: (isOffline ? QStringLiteral("latest-vod")
					: (isRerun ? QStringLiteral("rerun")
						: (isEmbed ? QStringLiteral("clip") : QStringLiteral("offline-channel"))));
			QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("twitch") },
				{ QStringLiteral("previewProvider"), QStringLiteral("twitch") },
				{ QStringLiteral("previewKind"), QStringLiteral("video") },
				{ QStringLiteral("providerName"), QStringLiteral("Twitch") },
				{ QStringLiteral("twitchMetadataVersion"), 3 },
				{ QStringLiteral("twitchKind"), isEmbed ? QStringLiteral("clip") : QStringLiteral("channel") },
				{ QStringLiteral("twitchBadge"), badge },
				{ QStringLiteral("twitchLiveState"), liveState },
				{ QStringLiteral("twitchDisplayName"), isError ? QStringLiteral("Mumble Dev")
																		: (isEmbed ? QStringLiteral("Mumble Clips")
																					   : QStringLiteral("Mumble Dev")) },
				{ QStringLiteral("twitchChannel"), channel },
				{ QStringLiteral("twitchEmbedMode"), embedMode },
				{ QStringLiteral("twitchPlaybackNote"),
				  isEmbed ? QStringLiteral("Playback opens only after an explicit user action.")
						  : QStringLiteral("Playback state is reported by Twitch and can be delayed.") },
				{ QStringLiteral("twitchDisclaimer"),
				  QStringLiteral("Status is a deterministic style fixture; no Twitch request was made.") },
				{ QStringLiteral("twitchThumbnailUrl"), thumbnail }
			};
			if (isLive || isRerun || isOffline) {
				metadata.insert(QStringLiteral("twitchGame"), QStringLiteral("Software and Game Development"));
			}
			if (isLive) {
				metadata.insert(QStringLiteral("twitchStreamType"), QStringLiteral("live"));
				metadata.insert(QStringLiteral("twitchViewerCount"), 1842);
			} else if (isRerun) {
				metadata.insert(QStringLiteral("twitchStreamType"), QStringLiteral("rerun"));
				metadata.insert(QStringLiteral("twitchViewerCount"), 318);
			} else if (isOffline) {
				metadata.insert(QStringLiteral("twitchSuggestedVideoId"), QStringLiteral("2489000123"));
			} else if (isEmbed) {
				metadata.insert(QStringLiteral("twitchClipSlug"), QStringLiteral("StableFramePacing"));
				metadata.insert(QStringLiteral("twitchSuggestedClipSlug"), QStringLiteral("StableFramePacing"));
			} else {
				metadata.insert(QStringLiteral("twitchStateFailure"),
					QStringLiteral("Could not read Twitch channel state."));
				metadata.insert(QStringLiteral("twitchMetadataFailure"),
					QStringLiteral("Preview metadata was unavailable."));
			}
			const QString embedUrl = isEmbed
				? QStringLiteral("https://clips.twitch.tv/embed?clip=StableFramePacing&parent=localhost")
				: QStringLiteral("https://player.twitch.tv/?channel=mumbledev&parent=localhost");
			metadata.insert(QStringLiteral("twitchSuggestedEmbedUrl"), embedUrl);
			const QVariantList mediaItems = isError ? QVariantList()
				: QVariantList { automationPreviewImageItem(thumbnail, QStringLiteral("Twitch preview")) };
			QVariantMap preview = automationProviderPreview(
				url,
				isLive ? QStringLiteral("Native Qt Quick client polish")
					   : (isOffline ? QStringLiteral("Mumble Dev is offline")
								: (isRerun ? QStringLiteral("Rerun: accessible client walkthrough")
										   : (isEmbed ? QStringLiteral("Stable frame pacing")
														: QStringLiteral("Mumble Dev on Twitch")))),
				QStringLiteral("Twitch · %1").arg(badge),
				isError ? QStringLiteral("This Twitch channel could not be loaded.")
						: (isEmbed ? QStringLiteral("Featured clip · 00:42 · explicit playback")
								   : QStringLiteral("Software and Game Development · %1").arg(badge)),
				QStringLiteral("Open on Twitch"), metadata, mediaItems, size);
			if (!isError) {
				preview.insert(QStringLiteral("embedKind"), QStringLiteral("twitch"));
				preview.insert(QStringLiteral("embedUrl"), embedUrl);
				preview.insert(QStringLiteral("embedAspect"), QStringLiteral("wide"));
			}
			const qulonglong messageID = isLive ? 4294967490ULL
				: (isOffline ? 4294967491ULL
					: (isRerun ? 4294967492ULL : (isError ? 4294967493ULL : 4294967494ULL)));
			return { automationRichPreviewMessage(messageID, actor, url, preview) };
		}

		if (normalized == QLatin1String("github-release")
			|| normalized == QLatin1String("github-release-loading")
			|| normalized == QLatin1String("github-release-missing")
			|| normalized == QLatin1String("github-release-assets")) {
			const bool isLoading = normalized == QLatin1String("github-release-loading");
			const bool isMissing = normalized == QLatin1String("github-release-missing");
			const bool isAssets = normalized == QLatin1String("github-release-assets");
			const QString avatar = fixtureImage(QStringLiteral("DM"), QStringLiteral("Repository owner"),
				QStringLiteral("#24292f"), QStringLiteral("#6e7781"));
			QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("github") },
				{ QStringLiteral("previewProvider"), QStringLiteral("github") },
				{ QStringLiteral("previewKind"), QStringLiteral("github") },
				{ QStringLiteral("providerName"), QStringLiteral("GitHub") },
				{ QStringLiteral("githubOwner"), QStringLiteral("dankmaster") },
				{ QStringLiteral("githubRepo"), QStringLiteral("mumble") },
				{ QStringLiteral("githubFullName"), QStringLiteral("dankmaster/mumble") },
				{ QStringLiteral("githubDescription"),
				  QStringLiteral("A performance-focused Mumble fork with a native Qt Quick client.") },
				{ QStringLiteral("githubLanguage"), QStringLiteral("C++") },
				{ QStringLiteral("githubDefaultBranch"), QStringLiteral("master") },
				{ QStringLiteral("githubPushedAt"), QStringLiteral("2026-05-28T18:00:00Z") },
				{ QStringLiteral("githubOwnerLogin"), QStringLiteral("dankmaster") },
				{ QStringLiteral("githubOwnerAvatarUrl"), avatar },
				{ QStringLiteral("githubLicense"), QStringLiteral("BSD-3-Clause") },
				{ QStringLiteral("githubStars"), 4200 },
				{ QStringLiteral("githubForks"), 318 },
				{ QStringLiteral("githubOpenIssues"), 27 },
				{ QStringLiteral("githubTopics"),
				  QVariantList { QStringLiteral("mumble"), QStringLiteral("qt-quick"),
					  QStringLiteral("voice-chat"), QStringLiteral("qml") } }
			};
			if (isLoading) {
				metadata.insert(QStringLiteral("githubLatestReleaseLoading"), true);
			} else if (isMissing) {
				metadata.insert(QStringLiteral("githubLatestReleaseMissing"), true);
			} else {
				metadata.insert(QStringLiteral("githubLatestReleaseTag"), QStringLiteral("mumble-forked-2026.05"));
				metadata.insert(QStringLiteral("githubLatestReleaseName"),
					isAssets ? QStringLiteral("Windows client artifacts") : QStringLiteral("May client polish"));
				metadata.insert(QStringLiteral("githubLatestReleaseUrl"),
					QStringLiteral("https://github.com/dankmaster/mumble/releases/tag/mumble-forked-2026.05"));
				metadata.insert(QStringLiteral("githubLatestReleasePublishedAt"),
					QStringLiteral("2026-05-26T20:00:00Z"));
				metadata.insert(QStringLiteral("githubLatestReleaseNotes"),
					isAssets
						? QStringLiteral("Signed MSI, portable package and symbols for the Windows client.")
						: QStringLiteral("Native provider cards, smoother chat and improved accessibility."));
				metadata.insert(QStringLiteral("githubLatestReleaseAssetName"),
					isAssets ? QStringLiteral("mumble-forked-x64.msi") : QStringLiteral("mumble-forked.zip"));
				metadata.insert(QStringLiteral("githubLatestReleaseAssetUrl"),
					QStringLiteral("https://github.com/dankmaster/mumble/releases/download/"
								   "mumble-forked-2026.05/mumble-forked-x64.msi"));
				metadata.insert(QStringLiteral("githubLatestReleaseAssetCount"), isAssets ? 3 : 2);
				metadata.insert(QStringLiteral("githubLatestReleaseDownloadCount"), isAssets ? 18420 : 4217);
				if (isAssets) {
					metadata.insert(QStringLiteral("githubLatestReleasePrerelease"), true);
				}
			}
			const QString url = QStringLiteral("https://github.com/dankmaster/mumble");
			const QVariantMap preview = automationProviderPreview(
				url, QStringLiteral("dankmaster/mumble"), QStringLiteral("GitHub · C++"),
				isLoading ? QStringLiteral("Checking the latest release…")
					: (isMissing ? QStringLiteral("No published release was found.")
							 : (isAssets ? QStringLiteral("3 Windows assets · 18.4K downloads")
										: QStringLiteral("Latest release: May client polish"))),
				QStringLiteral("Open on GitHub"), metadata,
				QVariantList { automationPreviewImageItem(avatar, QStringLiteral("Repository owner")) }, size);
			const qulonglong messageID = isLoading ? 4294967501ULL
				: (isMissing ? 4294967502ULL : (isAssets ? 4294967503ULL : 4294967500ULL));
			return { automationRichPreviewMessage(messageID, actor, url, preview) };
		}

		if (normalized == QLatin1String("flashback-post")
			|| normalized == QLatin1String("flashback-context")) {
			const bool sparse = normalized == QLatin1String("flashback-context");
			const QString avatar = fixtureImage(QStringLiteral("RN"), QStringLiteral("Flashback avatar fixture"),
				QStringLiteral("#374151"), QStringLiteral("#6b7280"));
			QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("flashback") },
				{ QStringLiteral("previewProvider"), QStringLiteral("flashback") },
				{ QStringLiteral("previewKind"), QStringLiteral("forum") },
				{ QStringLiteral("swedishPreviewKind"), QStringLiteral("forum") },
				{ QStringLiteral("providerName"), QStringLiteral("Flashback") },
				{ QStringLiteral("forumProvider"), QStringLiteral("Flashback") },
				{ QStringLiteral("forumThreadId"), QStringLiteral("3650123") },
				{ QStringLiteral("forumThreadTitle"), QStringLiteral("Qt Quick-prestanda och renderloopar") },
				{ QStringLiteral("forumCategory"), QStringLiteral("Dator och IT") },
				{ QStringLiteral("forumName"), QStringLiteral("Programmering och utveckling") },
				{ QStringLiteral("forumPage"), sparse ? QStringLiteral("1") : QStringLiteral("42") },
				{ QStringLiteral("forumPageCount"), QStringLiteral("42") },
				{ QStringLiteral("forumPostCount"), sparse ? QStringLiteral("1") : QStringLiteral("628") }
			};
			if (sparse) {
				metadata.insert(QStringLiteral("forumLinkKind"), QStringLiteral("thread"));
				metadata.insert(QStringLiteral("forumFirstPostId"), QStringLiteral("91200101"));
				metadata.insert(QStringLiteral("forumFirstPostTime"), QStringLiteral("2026-04-20 09:15"));
				metadata.insert(QStringLiteral("forumFirstPostNumber"), QStringLiteral("#1"));
				metadata.insert(QStringLiteral("forumFirstPostAuthor"), QStringLiteral("qmlprofilen"));
				metadata.insert(QStringLiteral("forumFirstPostAuthorAvatarUrl"), avatar);
				metadata.insert(QStringLiteral("forumFirstPostAuthorTitle"), QStringLiteral("Medlem"));
				metadata.insert(QStringLiteral("forumFirstPostAuthorRegistered"), QStringLiteral("2019-02"));
				metadata.insert(QStringLiteral("forumFirstPostAuthorPosts"), QStringLiteral("843"));
				metadata.insert(QStringLiteral("forumFirstPostExcerpt"),
					QStringLiteral("Hur håller ni Qt Quick-listor mjuka när talk-state uppdateras ofta?"));
			} else {
				metadata.insert(QStringLiteral("forumLinkKind"), QStringLiteral("post"));
				metadata.insert(QStringLiteral("forumLinkedPostId"), QStringLiteral("91345012"));
				metadata.insert(QStringLiteral("forumPostId"), QStringLiteral("91345012"));
				metadata.insert(QStringLiteral("forumPostNumber"), QStringLiteral("#628"));
				metadata.insert(QStringLiteral("forumPostAuthor"), QStringLiteral("rendernisse"));
				metadata.insert(QStringLiteral("forumPostAuthorAvatarUrl"), avatar);
				metadata.insert(QStringLiteral("forumPostAuthorTitle"), QStringLiteral("Medlem"));
				metadata.insert(QStringLiteral("forumPostAuthorRegistered"), QStringLiteral("2021-09"));
				metadata.insert(QStringLiteral("forumPostAuthorPosts"), QStringLiteral("2 418"));
				metadata.insert(QStringLiteral("forumPostTime"), QStringLiteral("2026-05-28 13:37"));
				metadata.insert(QStringLiteral("forumPostExcerpt"),
					QStringLiteral("Frame pacing blev stabilare när täta presence-signaler batchades per render frame."));
				metadata.insert(QStringLiteral("forumQuoteAuthor"), QStringLiteral("qmlprofilen"));
				metadata.insert(QStringLiteral("forumQuoteExcerpt"),
					QStringLiteral("Behåll stabila ID:n och låt aldrig modellen resetta under talk-state."));
				metadata.insert(QStringLiteral("forumQuotePostUrl"),
					QStringLiteral("https://www.flashback.org/p91344990"));
				metadata.insert(QStringLiteral("forumQuotePostId"), QStringLiteral("91344990"));
				metadata.insert(QStringLiteral("forumQuotePostNumber"), QStringLiteral("#627"));
			}
			const QString url = sparse ? QStringLiteral("https://www.flashback.org/t3650123")
									   : QStringLiteral("https://www.flashback.org/p91345012");
			const QVariantMap preview = automationProviderPreview(
				url, QStringLiteral("Qt Quick-prestanda och renderloopar"),
				QStringLiteral("Flashback · Dator och IT"),
				sparse ? QStringLiteral("Thread context with intentionally sparse post metadata.")
						: QStringLiteral("Frame pacing blev stabilare när presence-signaler batchades per render frame."),
				sparse ? QStringLiteral("Open discussion") : QStringLiteral("Open in thread"), metadata,
				sparse ? QVariantList() : QVariantList { automationPreviewImageItem(avatar, QStringLiteral("Post author")) },
				size);
			return { automationRichPreviewMessage(sparse ? 4294967511ULL : 4294967510ULL, actor, url, preview) };
		}

		if (normalized == QLatin1String("existenz-digest")
			|| normalized == QLatin1String("existenz-warning")) {
			const bool warning = normalized == QLatin1String("existenz-warning");
			const QString thumbnail = fixtureImage(
				warning ? QStringLiteral("Content notice") : QStringLiteral("Evening link digest"),
				warning ? QStringLiteral("Explicit reveal required") : QStringLiteral("Curated technology links"),
				warning ? QStringLiteral("#512da8") : QStringLiteral("#1f6f8b"),
				warning ? QStringLiteral("#b39ddb") : QStringLiteral("#7dd3fc"));
			QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("existenz") },
				{ QStringLiteral("previewProvider"), QStringLiteral("existenz") },
				{ QStringLiteral("previewKind"), QStringLiteral("linkDigest") },
				{ QStringLiteral("swedishPreviewKind"), QStringLiteral("linkDigest") },
				{ QStringLiteral("providerName"), QStringLiteral("Existenz") },
				{ QStringLiteral("linkDigestTitle"), warning ? QStringLiteral("Sensitive link fixture")
																	 : QStringLiteral("Evening technology digest") },
				{ QStringLiteral("linkDigestCaption"),
				  warning ? QStringLiteral("A harmless generated image verifies the reveal state.")
						  : QStringLiteral("Native UI, frame pacing and accessible chat surfaces.") },
				{ QStringLiteral("linkDigestSource"), QStringLiteral("Existenz") },
				{ QStringLiteral("thumbnailBlur"), warning }
			};
			if (warning) {
				metadata.insert(QStringLiteral("contentWarning"), QStringLiteral("NSFW"));
			}
			const QString url = warning ? QStringLiteral("https://existenz.se/out.php?id=fixture-warning")
										: QStringLiteral("https://existenz.se/out.php?id=fixture-digest");
			const QVariantList images {
				automationPreviewImageItem(thumbnail, warning ? QStringLiteral("Sensitive preview fixture")
																	: QStringLiteral("Link digest"))
			};
			const QVariantMap preview = automationProviderPreview(
				url, warning ? QStringLiteral("Sensitive link fixture") : QStringLiteral("Evening technology digest"),
				QStringLiteral("Existenz · link digest"),
				warning ? QStringLiteral("A harmless generated image used to verify explicit reveal behavior.")
						: QStringLiteral("Native UI, frame pacing and accessible chat surfaces."),
				QStringLiteral("Open on Existenz"), metadata, images, size);
			return { automationRichPreviewMessage(warning ? 4294967521ULL : 4294967520ULL, actor, url, preview) };
		}

		if (normalized == QLatin1String("google-search") || normalized == QLatin1String("google-images")
			|| normalized == QLatin1String("google-shopping")) {
			const bool images = normalized == QLatin1String("google-images");
			const bool shopping = normalized == QLatin1String("google-shopping");
			const QString mode = images ? QStringLiteral("images")
								  : (shopping ? QStringLiteral("shopping") : QStringLiteral("search"));
			const QString modeLabel = images ? QStringLiteral("Google Images")
										 : (shopping ? QStringLiteral("Google Shopping")
													 : QStringLiteral("Google Search"));
			const QString query = shopping ? QStringLiteral("ergonomic keyboard Stockholm")
									 : QStringLiteral("Qt Quick frame pacing Mumble");
			const QString url = shopping
				? QStringLiteral("https://www.google.com/search?q=ergonomic+keyboard+Stockholm&tbm=shop")
				: (images ? QStringLiteral("https://www.google.com/search?q=Qt+Quick+frame+pacing+Mumble&tbm=isch")
						  : QStringLiteral("https://www.google.com/search?q=Qt+Quick+frame+pacing+Mumble"));
			const QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("google-search") },
				{ QStringLiteral("previewProvider"), QStringLiteral("google-search") },
				{ QStringLiteral("previewKind"), QStringLiteral("search") },
				{ QStringLiteral("googleSearchQuery"), query },
				{ QStringLiteral("googleSearchModeLabel"), modeLabel }
			};
			const QVariantMap preview = automationProviderPreview(
				url, modeLabel, QStringLiteral("Google · %1").arg(mode), query,
				QStringLiteral("Open %1").arg(modeLabel), metadata, {}, size);
			const qulonglong messageID = images ? 4294967531ULL : (shopping ? 4294967532ULL : 4294967530ULL);
			return { automationRichPreviewMessage(messageID, actor, url, preview) };
		}

		if (normalized == QLatin1String("instagram-identity")
			|| normalized == QLatin1String("instagram-avatar")
			|| normalized == QLatin1String("instagram-caption")) {
			const bool avatarOnly = normalized == QLatin1String("instagram-avatar");
			const bool captionOnly = normalized == QLatin1String("instagram-caption");
			const QString avatar = fixtureImage(QStringLiteral("MQ"), QStringLiteral("Instagram identity fixture"),
				QStringLiteral("#833ab4"), QStringLiteral("#fd1d1d"));
			QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("instagram") },
				{ QStringLiteral("previewProvider"), QStringLiteral("instagram") },
				{ QStringLiteral("previewKind"), QStringLiteral("instagram") },
				{ QStringLiteral("providerName"), QStringLiteral("Instagram") },
				{ QStringLiteral("instagramMetadataVersion"), 2 },
				{ QStringLiteral("instagramMediaKind"), captionOnly ? QStringLiteral("reel")
																	   : QStringLiteral("post") }
			};
			if (!avatarOnly && !captionOnly) {
				metadata.insert(QStringLiteral("instagramDisplayName"), QStringLiteral("Mumble Qt Quick"));
				metadata.insert(QStringLiteral("instagramHandle"), QStringLiteral("@mumblequick"));
				metadata.insert(QStringLiteral("instagramOwnerUserId"), QStringLiteral("17841460000001234"));
			}
			if (captionOnly) {
				metadata.insert(QStringLiteral("instagramCaption"),
					QStringLiteral("Stable chat scrolling with bounded delegate reuse."));
				metadata.insert(QStringLiteral("instagramCreatedAt"), QStringLiteral("2026-05-28T18:30:00Z"));
				metadata.insert(QStringLiteral("instagramLikeCount"), 18420);
				metadata.insert(QStringLiteral("instagramCommentCount"), 318);
			}
			if (avatarOnly) {
				metadata.insert(QStringLiteral("instagramAvatarUrl"), avatar);
			}
			const QString slug = avatarOnly ? QStringLiteral("AvatarSparse")
									: (captionOnly ? QStringLiteral("CaptionSparse") : QStringLiteral("IdentitySparse"));
			const QString url = QStringLiteral("https://www.instagram.com/p/%1/").arg(slug);
			const QVariantList mediaItems = avatarOnly
				? QVariantList { automationPreviewImageItem(avatar, QStringLiteral("Instagram avatar")) }
				: QVariantList();
			const QVariantMap preview = automationProviderPreview(
				url,
				captionOnly ? QStringLiteral("Stable chat scrolling with bounded delegate reuse.")
							: (avatarOnly ? QStringLiteral("Post by @mumblequick")
											: QStringLiteral("Native preview cards now share one design language.")),
				captionOnly ? QStringLiteral("Instagram reel")
							: (avatarOnly ? QStringLiteral("Instagram")
										 : QStringLiteral("Mumble Qt Quick · @mumblequick")),
				captionOnly ? QStringLiteral("Sparse caption without identity or avatar metadata.")
							: (avatarOnly ? QStringLiteral("Sparse identity with a deterministic avatar.")
											: QStringLiteral("Identity, caption and activity metadata.")),
				QStringLiteral("Open on Instagram"), metadata, mediaItems, size);
			const qulonglong messageID = avatarOnly ? 4294967541ULL
				: (captionOnly ? 4294967542ULL : 4294967540ULL);
			return { automationRichPreviewMessage(messageID, actor, url, preview) };
		}

		if (normalized == QLatin1String("weather")) {
			const QString forecast = fixtureImage(QStringLiteral("Stockholm 12 °C"),
				QStringLiteral("Växlande molnighet"), QStringLiteral("#1565c0"), QStringLiteral("#90caf9"));
			const QVariantList images { automationPreviewImageItem(forecast, QStringLiteral("Forecast")) };
			const QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("smhi") },
				{ QStringLiteral("previewProvider"), QStringLiteral("smhi") },
				{ QStringLiteral("previewKind"), QStringLiteral("weather") },
				{ QStringLiteral("swedishPreviewKind"), QStringLiteral("weather") },
				{ QStringLiteral("providerName"), QStringLiteral("SMHI") },
				{ QStringLiteral("richPreviewMetadataVersion"), 10 },
				{ QStringLiteral("locationLabel"), QStringLiteral("Stockholm") },
				{ QStringLiteral("statusLabel"),
				  QStringLiteral("12 °C · växlande molnighet · svag sydvästlig vind") }
			};
			const QString url = QStringLiteral("https://www.smhi.se/vader/prognoser/ortsprognoser/stockholm");
			const QVariantMap preview = automationProviderPreview(url, QStringLiteral("Vädret i Stockholm"),
				QStringLiteral("SMHI"), QStringLiteral("12 °C · växlande molnighet · svag sydvästlig vind"),
				QStringLiteral("Open forecast"), metadata, images, size);
			return { automationRichPreviewMessage(4294967470ULL, actor, url, preview) };
		}

		if (normalized == QLatin1String("place-traffic") || normalized == QLatin1String("placetraffic")) {
			const QString placeImage = fixtureImage(QStringLiteral("Slussen"), QStringLiteral("Hitta place fixture"),
				QStringLiteral("#00695c"), QStringLiteral("#80cbc4"));
			const QVariantMap placeMetadata {
				{ QStringLiteral("provider"), QStringLiteral("hitta") },
				{ QStringLiteral("previewProvider"), QStringLiteral("hitta") },
				{ QStringLiteral("previewKind"), QStringLiteral("place") },
				{ QStringLiteral("swedishPreviewKind"), QStringLiteral("place") },
				{ QStringLiteral("providerName"), QStringLiteral("Hitta") },
				{ QStringLiteral("richPreviewMetadataVersion"), 10 },
				{ QStringLiteral("locationLabel"), QStringLiteral("Slussen, Stockholm") },
				{ QStringLiteral("statusLabel"), QStringLiteral("Torg · kollektivtrafik · öppet område") }
			};
			const QString placeUrl = QStringLiteral("https://www.hitta.se/slussen-stockholm");
			const QVariantList placeMedia {
				automationPreviewImageItem(placeImage, QStringLiteral("Map preview"))
			};
			const QVariantMap placePreview = automationProviderPreview(placeUrl, QStringLiteral("Slussen"),
				QStringLiteral("Hitta · Stockholm"), QStringLiteral("Torg · kollektivtrafik · öppet område"),
				QStringLiteral("Open place"), placeMetadata, placeMedia, size);

			const QString trafficImage = fixtureImage(QStringLiteral("Stockholm C → Göteborg C"),
				QStringLiteral("SJ traffic fixture"), QStringLiteral("#c62828"), QStringLiteral("#ef9a9a"));
			const QVariantMap trafficMetadata {
				{ QStringLiteral("provider"), QStringLiteral("sj") },
				{ QStringLiteral("previewProvider"), QStringLiteral("sj") },
				{ QStringLiteral("previewKind"), QStringLiteral("traffic") },
				{ QStringLiteral("swedishPreviewKind"), QStringLiteral("traffic") },
				{ QStringLiteral("providerName"), QStringLiteral("SJ") },
				{ QStringLiteral("richPreviewMetadataVersion"), 10 },
				{ QStringLiteral("locationLabel"), QStringLiteral("Stockholm C → Göteborg C") },
				{ QStringLiteral("statusLabel"), QStringLiteral("Avgång 17:34 · spår 10 · i tid") }
			};
			const QString trafficUrl = QStringLiteral("https://www.sj.se/trafikinformation/fixture");
			const QVariantList trafficMedia {
				automationPreviewImageItem(trafficImage, QStringLiteral("Train status"))
			};
			const QVariantMap trafficPreview = automationProviderPreview(trafficUrl,
				QStringLiteral("Stockholm C → Göteborg C"), QStringLiteral("SJ · trafikläge"),
				QStringLiteral("Avgång 17:34 · spår 10 · i tid"), QStringLiteral("Open traffic status"),
				trafficMetadata, trafficMedia, size);

			return { automationRichPreviewMessage(4294967471ULL, actor, placeUrl, placePreview),
				automationRichPreviewMessage(4294967472ULL, actor, trafficUrl, trafficPreview) };
		}

		if (normalized == QLatin1String("content-warning") || normalized == QLatin1String("contentwarning")) {
			const QString warningImage = fixtureImage(QStringLiteral("Sensitive preview fixture"),
				QStringLiteral("Harmless reveal-state test"), QStringLiteral("#512da8"), QStringLiteral("#b39ddb"));
			const QVariantMap metadata {
				{ QStringLiteral("provider"), QStringLiteral("existenz") },
				{ QStringLiteral("previewProvider"), QStringLiteral("existenz") },
				{ QStringLiteral("previewKind"), QStringLiteral("linkDigest") },
				{ QStringLiteral("swedishPreviewKind"), QStringLiteral("linkDigest") },
				{ QStringLiteral("providerName"), QStringLiteral("Existenz") },
				{ QStringLiteral("richPreviewMetadataVersion"), 10 },
				{ QStringLiteral("thumbnailBlur"), true },
				{ QStringLiteral("contentWarning"), QStringLiteral("NSFW") }
			};
			const QString url = QStringLiteral("https://existenz.se/out.php?id=fixture");
			const QVariantList images {
				automationPreviewImageItem(warningImage, QStringLiteral("Sensitive preview fixture"))
			};
			const QVariantMap preview = automationProviderPreview(url, QStringLiteral("Content warning fixture"),
				QStringLiteral("Existenz · content notice"),
				QStringLiteral("A harmless generated image used to verify explicit reveal behavior."),
				QStringLiteral("Open link"), metadata, images, size);
			return { automationRichPreviewMessage(4294967480ULL, actor, url, preview) };
		}

		return {};
	}

	QVariantList automationRichPreviewProbeMessages(const QString &variant, const QString &requestedSize) {
		const QString normalizedVariant = variant.trimmed().toLower();
		const QVariantList providerMessages =
			automationProviderRichPreviewProbeMessages(normalizedVariant, requestedSize);
		if (!providerMessages.isEmpty()) {
			return providerMessages;
		}
		const QString imageDataUrl =
			automationPreviewImageDataUrl(QObject::tr("Inline image"), QObject::tr("Local chat attachment"),
										  QStringLiteral("#43c6ac"), QStringLiteral("#2e86de"));
		const QString xImageDataUrl =
			automationPreviewImageDataUrl(QObject::tr("X preview"), QObject::tr("Social post media"),
										  QStringLiteral("#14171a"), QStringLiteral("#1d9bf0"));
		const QString cardImageDataUrl =
			automationPreviewImageDataUrl(QObject::tr("Preview card"), QObject::tr("Responsive media state"),
										  QStringLiteral("#7c3aed"), QStringLiteral("#06b6d4"));
		QString size = requestedSize.trimmed().toLower();
		if (normalizedVariant == QLatin1String("compact")) {
			size = QStringLiteral("compact");
		} else if (normalizedVariant == QLatin1String("expanded") || normalizedVariant == QLatin1String("large")) {
			size = QStringLiteral("large");
		}

		QVariantMap preview;
		QString bodyText;
		QString actor = QObject::tr("preview-bot");
		qulonglong messageID = 4294967296ULL;

		if (normalizedVariant == QLatin1String("youtube")) {
			bodyText = QStringLiteral("https://www.youtube.com/watch?v=dQw4w9WgXcQ");
			preview = automationRichPreviewBase(bodyText, QObject::tr("YouTube embed preview"),
												QObject::tr("YouTube"),
												QObject::tr("Embedded video card with playback controls."));
			preview.insert(QStringLiteral("embedKind"), QStringLiteral("youtube"));
			preview.insert(QStringLiteral("embedUrl"),
						   QStringLiteral("https://www.youtube.com/embed/dQw4w9WgXcQ"));
			preview.insert(QStringLiteral("embedAspect"), QStringLiteral("wide"));
			preview.insert(QStringLiteral("openLabel"), QObject::tr("Open on YouTube"));
			automationApplyPreviewSize(preview, size);
			return QVariantList { automationRichPreviewMessage(messageID, actor, bodyText, preview) };
		}

		if (normalizedVariant == QLatin1String("x") || normalizedVariant == QLatin1String("twitter")) {
			bodyText = QStringLiteral("https://x.com/dankpreview/status/1790000000000000000");
			preview = automationRichPreviewBase(bodyText, QObject::tr("Mockup previews should feel native in chat"),
												QObject::tr("dankpreview"),
												QObject::tr("A social card with post text, media, metrics, and source chrome."));
			preview.insert(QStringLiteral("thumbnailUrl"), xImageDataUrl);
			preview.insert(QStringLiteral("openLabel"), QObject::tr("Open on X"));
			QVariantMap quoted;
			quoted.insert(QStringLiteral("displayName"), QObject::tr("Designer Notes"));
			quoted.insert(QStringLiteral("handle"), QStringLiteral("@designnotes"));
			quoted.insert(QStringLiteral("text"), QObject::tr("Keep enough structure to design loading and failure states."));
			quoted.insert(QStringLiteral("verified"), true);
			QVariantMap metadata;
			metadata.insert(QStringLiteral("xHandle"), QStringLiteral("@dankpreview"));
			metadata.insert(QStringLiteral("xDisplayName"), QObject::tr("Dank Preview"));
			metadata.insert(QStringLiteral("xVerified"), true);
			metadata.insert(QStringLiteral("xCreatedAt"), QStringLiteral("2026-05-28T18:30:00Z"));
			metadata.insert(QStringLiteral("xReplyCount"), 757);
			metadata.insert(QStringLiteral("xRepostCount"), 12000);
			metadata.insert(QStringLiteral("xQuoteCount"), 420);
			metadata.insert(QStringLiteral("xLikeCount"), 362000);
			metadata.insert(QStringLiteral("xViewCount"), 8100000);
			metadata.insert(QStringLiteral("xBookmarkCount"), 9000);
			metadata.insert(QStringLiteral("xQuotedPost"), quoted);
			preview.insert(QStringLiteral("metadata"), metadata);
			automationApplyPreviewSize(preview, size);
			return QVariantList { automationRichPreviewMessage(messageID + 1, actor, bodyText, preview) };
		}

		if (normalizedVariant == QLatin1String("inlineimage") || normalizedVariant == QLatin1String("image")) {
			bodyText = QObject::tr("Attached image from chat composer");
			preview = automationRichPreviewBase(QStringLiteral("mumble-chat://inline-data-image/mockup-rich-preview"),
												QObject::tr("Inline image attachment"),
												QObject::tr("Mumble chat"),
												QObject::tr("Image sent directly in persistent chat."));
			preview.insert(QStringLiteral("kind"), QStringLiteral("image"));
			preview.insert(QStringLiteral("mediaUrl"), imageDataUrl);
			preview.insert(QStringLiteral("mediaMime"), QStringLiteral("image/png"));
			preview.insert(QStringLiteral("openLabel"), QObject::tr("Open image"));
			automationApplyPreviewSize(preview, size);
			return QVariantList { automationRichPreviewMessage(messageID + 2, actor, bodyText, preview) };
		}

		if (normalizedVariant == QLatin1String("loading")) {
			bodyText = QStringLiteral("https://example.com/live-preview-loading");
			preview = automationRichPreviewBase(bodyText, QObject::tr("Fetching link preview"),
												QObject::tr("example.com"), QString());
			preview.insert(QStringLiteral("loading"), true);
			preview.insert(QStringLiteral("openLabel"), QObject::tr("Open link"));
			automationApplyPreviewSize(preview, size);
			return QVariantList { automationRichPreviewMessage(messageID + 3, actor, bodyText, preview) };
		}

		if (normalizedVariant == QLatin1String("failed") || normalizedVariant == QLatin1String("error")) {
			bodyText = QStringLiteral("https://example.com/preview-unavailable");
			preview = automationRichPreviewBase(bodyText, QObject::tr("Preview unavailable"),
												QObject::tr("example.com"), QString());
			preview.insert(QStringLiteral("failed"), true);
			preview.insert(QStringLiteral("openLabel"), QObject::tr("Open link"));
			automationApplyPreviewSize(preview, size);
			return QVariantList { automationRichPreviewMessage(messageID + 4, actor, bodyText, preview) };
		}

		if (normalizedVariant == QLatin1String("compact") || normalizedVariant == QLatin1String("expanded")
			|| normalizedVariant == QLatin1String("large")) {
			bodyText = QStringLiteral("https://example.com/responsive-preview-card");
			preview = automationRichPreviewBase(bodyText,
												size == QLatin1String("compact")
													? QObject::tr("Compact preview card")
													: QObject::tr("Expanded preview card"),
												QObject::tr("example.com"),
												QObject::tr("Media card captured with the requested preview size."));
			preview.insert(QStringLiteral("thumbnailUrl"), cardImageDataUrl);
			preview.insert(QStringLiteral("mediaUrl"), cardImageDataUrl);
			preview.insert(QStringLiteral("mediaMime"), QStringLiteral("image/png"));
			preview.insert(QStringLiteral("openLabel"), QObject::tr("Open link"));
			automationApplyPreviewSize(preview, size);
			return QVariantList { automationRichPreviewMessage(messageID + 5, actor, bodyText, preview) };
		}

		return {};
	}

	QStringList automationRichPreviewMessageIds(const QVariantList &messages) {
		QStringList ids;
		ids.reserve(messages.size());
		for (const QVariant &value : messages) {
			const QString id = value.toMap().value(QStringLiteral("messageId")).toString().trimmed();
			if (!id.isEmpty() && !ids.contains(id)) {
				ids.push_back(id);
			}
		}
		return ids;
	}

	QList< QQuickItem * > automationQuickItemSubtree(QQuickItem *root) {
		QList< QQuickItem * > items;
		if (!root) {
			return items;
		}

		QList< QQuickItem * > pending { root };
		while (!pending.isEmpty()) {
			QQuickItem *item = pending.takeLast();
			if (!item) {
				continue;
			}
			items.push_back(item);
			pending.append(item->childItems());
		}
		return items;
	}

	QObject *automationFindQuickItemByObjectName(QQuickItem *root, const QString &objectName) {
		for (QQuickItem *candidate : automationQuickItemSubtree(root)) {
			if (candidate->objectName() == objectName) {
				return candidate;
			}
		}
		return nullptr;
	}

	bool automationQuickItemHasVisibleAncestry(QQuickItem *item) {
		if (!item || !item->window()) {
			return false;
		}
		for (QQuickItem *current = item; current; current = current->parentItem()) {
			if (!current->isVisible() || current->opacity() <= 0.0) {
				return false;
			}
		}
		return true;
	}

	QObject *automationFindMediaSurface(QmlShellHost *host, const QString &surfaceId) {
		if (!host || !host->window() || surfaceId.isEmpty()) {
			return nullptr;
		}

		if (surfaceId == QLatin1String("mediaSession.window")) {
			QWindow *fallback = nullptr;
			for (QWindow *candidate : QGuiApplication::topLevelWindows()) {
				if (!candidate || candidate->property("surfaceId").toString() != surfaceId) {
					continue;
				}
				if (!fallback) {
					fallback = candidate;
				}
				if (candidate->isVisible() && candidate->isExposed()) {
					return candidate;
				}
			}
			return fallback;
		}

		QQuickItem *fallback = nullptr;
		for (QQuickItem *candidate : automationQuickItemSubtree(host->window()->contentItem())) {
			if (!candidate || candidate->property("surfaceId").toString() != surfaceId) {
				continue;
			}
			if (!fallback) {
				fallback = candidate;
			}
			if (automationQuickItemHasVisibleAncestry(candidate)) {
				return candidate;
			}
		}
		return fallback;
	}

	QVariantList automationModelRows(StableListModel *model, const bool unwrapSource) {
		QVariantList rows;
		if (!model) return rows;
		rows.reserve(model->rowCount());
		for (int row = 0; row < model->rowCount(); ++row) {
			const QVariantMap modelRow = model->get(row);
			QVariantMap source = unwrapSource ? modelRow.value(QStringLiteral("source")).toMap() : QVariantMap();
			if (!source.isEmpty()) {
				// Automation must be able to route the rendered row back through the
				// typed controller even when the protocol DTO uses a different ID.
				source.insert(QStringLiteral("stableId"), modelRow.value(QStringLiteral("id")));
			}
			rows.push_back(source.isEmpty() ? modelRow : source);
		}
		return rows;
	}

	QVariantMap automationDirectMessageState(DirectMessageController *directMessages) {
		if (!directMessages) return {};
		return QVariantMap {
			{ QStringLiteral("available"), directMessages->available() },
			{ QStringLiteral("title"), directMessages->title() },
			{ QStringLiteral("description"), directMessages->description() },
			{ QStringLiteral("unreadTotal"), directMessages->unreadTotal() },
			{ QStringLiteral("hasUnread"), directMessages->hasUnread() },
			{ QStringLiteral("trayOpen"), directMessages->trayOpen() },
			{ QStringLiteral("conversationOpen"), directMessages->conversationOpen() },
			{ QStringLiteral("activeSessionId"), directMessages->activeSessionId() },
			{ QStringLiteral("activeScopeToken"), directMessages->activeScopeToken() },
			{ QStringLiteral("activeLabel"), directMessages->activeLabel() },
			{ QStringLiteral("activeSubtitle"), directMessages->activeSubtitle() },
			{ QStringLiteral("activeAvatarUrl"), directMessages->activeAvatarUrl() },
			{ QStringLiteral("activeUnreadCount"), directMessages->activeUnreadCount() },
			{ QStringLiteral("canSend"), directMessages->canSend() },
			{ QStringLiteral("mode"), directMessages->mode() },
			{ QStringLiteral("persistentHistoryAvailable"), directMessages->persistentHistoryAvailable() },
			{ QStringLiteral("historyLoading"), directMessages->historyLoading() },
			{ QStringLiteral("historyError"), directMessages->historyError() },
			{ QStringLiteral("canAttachImages"), directMessages->canAttachImages() },
			{ QStringLiteral("canAttachFiles"), directMessages->canAttachFiles() },
			{ QStringLiteral("hasPendingReply"), directMessages->hasPendingReply() },
			{ QStringLiteral("pendingReplyMessageId"), directMessages->pendingReplyMessageId() },
			{ QStringLiteral("pendingReplyActor"), directMessages->pendingReplyActor() },
			{ QStringLiteral("pendingReplySnippet"), directMessages->pendingReplySnippet() },
			{ QStringLiteral("draft"), directMessages->draft() },
			{ QStringLiteral("draftAttachments"), directMessages->draftAttachments() },
			{ QStringLiteral("windowDocked"), directMessages->windowDocked() },
			{ QStringLiteral("windowMinimized"), directMessages->windowMinimized() },
			{ QStringLiteral("conversations"), automationModelRows(directMessages->summaryModel(), true) },
			{ QStringLiteral("messages"), automationModelRows(directMessages->timelineModel(), true) }
		};
	}

	QVariantMap automationToastState(ToastController *toast) {
		if (!toast) return QVariantMap { { QStringLiteral("available"), false } };
		return QVariantMap {
			{ QStringLiteral("available"), true },
			{ QStringLiteral("visible"), toast->visible() },
			{ QStringLiteral("tone"), toast->tone() },
			{ QStringLiteral("title"), toast->title() },
			{ QStringLiteral("message"), toast->message() },
			{ QStringLiteral("actionId"), toast->actionId() },
			{ QStringLiteral("actionLabel"), toast->actionLabel() },
			{ QStringLiteral("repeatCount"), toast->repeatCount() },
			{ QStringLiteral("revision"), QVariant::fromValue< qulonglong >(toast->revision()) }
		};
	}

	QVariantMap automationMediaControllerState(MediaSessionBackend *media) {
		const bool active   = media && media->active();
		const bool detached = active && media->detached();
		QVariantMap state;
		state.insert(QStringLiteral("active"), active);
		state.insert(QStringLiteral("state"), media ? media->state() : QStringLiteral("idle"));
		state.insert(QStringLiteral("errorCode"), media ? media->errorCode() : QString());
		state.insert(QStringLiteral("error"), media ? media->error() : QString());
		state.insert(QStringLiteral("detached"), detached);
		state.insert(QStringLiteral("provider"), media ? media->provider() : QString());
		state.insert(QStringLiteral("url"), media ? media->url() : QUrl());
		state.insert(QStringLiteral("audioUrl"), media ? media->audioUrl() : QUrl());
		state.insert(QStringLiteral("mediaMime"), media ? media->mediaMime() : QString());
		state.insert(QStringLiteral("audioMime"), media ? media->audioMime() : QString());
		state.insert(QStringLiteral("sessionId"), media ? media->sessionId() : QString());
		state.insert(QStringLiteral("position"), media ? media->position() : 0.0);
		state.insert(QStringLiteral("duration"), media ? media->duration() : 0.0);
		state.insert(QStringLiteral("syncGeneration"), media ? media->syncGeneration() : 0);
		state.insert(QStringLiteral("loadProgress"), media ? media->loadProgress() : 0);
		state.insert(QStringLiteral("playbackControllable"), media && media->playbackControllable());
		state.insert(QStringLiteral("playbackControlAllowed"), media && media->playbackControlAllowed());
		state.insert(QStringLiteral("sharedAvailable"), media && media->sharedAvailable());
		state.insert(QStringLiteral("sharedJoined"), media && media->sharedJoined());
		state.insert(QStringLiteral("sharedHost"), media && media->sharedHost());
		state.insert(QStringLiteral("sharedTitle"), media ? media->sharedTitle() : QString());
		state.insert(QStringLiteral("sharedSessionId"), media ? media->sharedSessionId() : QString());
		state.insert(QStringLiteral("sharedScopeId"), media ? media->sharedScopeId() : 0);
		state.insert(QStringLiteral("sharedHostSession"), media ? media->sharedHostSession() : 0);
		state.insert(QStringLiteral("sharedParticipantCount"), media ? media->sharedParticipantCount() : 0);
		state.insert(QStringLiteral("sharedParticipantSessions"),
			media ? media->sharedParticipantSessions() : QVariantList());
		state.insert(QStringLiteral("sharedOperationStatus"),
			media ? media->sharedOperationStatus() : QStringLiteral("idle"));
		state.insert(QStringLiteral("sharedOperationError"),
			media ? media->sharedOperationError() : QString());
		state.insert(QStringLiteral("presentation"),
			active ? (detached ? QStringLiteral("window") : QStringLiteral("inline")) : QStringLiteral("none"));
		return state;
	}

	QVariantMap automationMediaLifecycleState(QmlShellHost *host) {
		MediaSessionBackend *media = host ? host->mediaSession() : nullptr;
		const bool active          = media && media->active();
		const bool detached        = active && media->detached();
		const bool componentFailed = host && host->window()
			&& host->window()->property("mediaSessionWindowComponentFailed").toBool();

		QObject *inlineSurface = automationFindMediaSurface(host, QStringLiteral("mediaSession.inline"));
		QObject *windowSurface = automationFindMediaSurface(host, QStringLiteral("mediaSession.window"));
		QObject *surface       = active ? (detached ? windowSurface : inlineSurface)
									: (windowSurface ? windowSurface : inlineSurface);

		const bool rendererPresent = surface != nullptr;
		const bool webSurfaceActive = rendererPresent && surface->property("webSurfaceActive").toBool();
		const bool nativeSurfaceActive = rendererPresent
			&& surface->property("nativeSurfaceActive").toBool();
		const bool rendererActive = webSurfaceActive || nativeSurfaceActive;
		const QVariant backendValue = rendererPresent ? surface->property("rendererBackend") : QVariant();
		const QString rendererBackend = backendValue.isValid() && !backendValue.toString().isEmpty()
			? backendValue.toString()
			: nativeSurfaceActive ? QStringLiteral("native")
			: webSurfaceActive ? QStringLiteral("webengine") : QStringLiteral("none");
		const QVariant rendererValue = rendererPresent ? surface->property("rendererState") : QVariant();
		const QString rendererState  = rendererValue.isValid() && !rendererValue.toString().isEmpty()
			? rendererValue.toString()
			: componentFailed ? QStringLiteral("component-error")
			: active && media ? media->state() : QStringLiteral("inactive");
		const QVariant healthyValue = rendererPresent ? surface->property("rendererHealthy") : QVariant();
		const bool rendererHealthy  = rendererPresent
			&& (healthyValue.isValid() ? healthyValue.toBool() : rendererState == QLatin1String("active"));
		const QVariant documentValue = rendererPresent ? surface->property("documentReady") : QVariant();
		const bool documentReady     = rendererPresent
			&& (documentValue.isValid() ? documentValue.toBool() : rendererState == QLatin1String("active"));
		const bool rendererReady = active && rendererActive && rendererHealthy && documentReady
			&& rendererState == QLatin1String("active");

		QWindow *presentationWindow = nullptr;
		QString windowKind           = QStringLiteral("none");
		if (active && detached) {
			presentationWindow = qobject_cast< QWindow * >(windowSurface);
			windowKind         = QStringLiteral("detached");
		} else if (active) {
			presentationWindow = host ? host->window() : nullptr;
			windowKind         = QStringLiteral("main");
		} else if (windowSurface) {
			// Keep a detached surface observable after close so the lifecycle gate
			// can fail on a window that outlives its backend session.
			presentationWindow = qobject_cast< QWindow * >(windowSurface);
			windowKind         = QStringLiteral("detached");
		}

		QVariantMap state = automationMediaControllerState(media);
		state.insert(QStringLiteral("rendererExpected"), active);
		state.insert(QStringLiteral("rendererPresent"), rendererPresent);
		state.insert(QStringLiteral("rendererActive"), rendererActive);
		state.insert(QStringLiteral("rendererBackend"), rendererBackend);
		state.insert(QStringLiteral("webSurfaceActive"), webSurfaceActive);
		state.insert(QStringLiteral("nativeSurfaceActive"), nativeSurfaceActive);
		state.insert(QStringLiteral("rendererState"), rendererState);
		state.insert(QStringLiteral("rendererHealthy"), rendererHealthy);
		state.insert(QStringLiteral("rendererReady"), rendererReady);
		state.insert(QStringLiteral("rendererProbeAttempts"),
			rendererPresent ? surface->property("documentReadyProbeAttempts").toInt() : 0);
		state.insert(QStringLiteral("rendererProbeState"),
			rendererPresent ? surface->property("documentReadyProbeState").toString() : QString());
		state.insert(QStringLiteral("rendererSurfaceId"),
			rendererPresent ? surface->property("surfaceId").toString() : QString());
		state.insert(QStringLiteral("windowRequired"), active);
		state.insert(QStringLiteral("windowKind"), windowKind);
		state.insert(QStringLiteral("windowPresent"), presentationWindow != nullptr);
		state.insert(QStringLiteral("windowVisible"), presentationWindow && presentationWindow->isVisible());
		state.insert(QStringLiteral("windowExposed"), presentationWindow && presentationWindow->isExposed());
		state.insert(QStringLiteral("windowReady"), active && presentationWindow
			&& presentationWindow->isVisible() && presentationWindow->isExposed());
		state.insert(QStringLiteral("windowComponentFailed"), componentFailed);
		return state;
	}

	struct AutomationScreenShareViewer {
		QQuickWindow *window = nullptr;
		ScreenShareViewBackend *backend = nullptr;
	};

	QList< AutomationScreenShareViewer > automationScreenShareViewers() {
		QList< AutomationScreenShareViewer > viewers;
		for (QWindow *topLevel : QGuiApplication::topLevelWindows()) {
			QQuickWindow *window = qobject_cast< QQuickWindow * >(topLevel);
			if (!window || window->property("surfaceId").toString() != QLatin1String("screenShare.viewer")) continue;
			QObject *backendObject = window->property("backend").value< QObject * >();
			ScreenShareViewBackend *backend = qobject_cast< ScreenShareViewBackend * >(backendObject);
			if (backend) viewers.push_back({ window, backend });
		}
		return viewers;
	}

	AutomationScreenShareViewer automationScreenShareViewer(const QString &streamId) {
		const QString requestedId = streamId.trimmed();
		const QList< AutomationScreenShareViewer > viewers = automationScreenShareViewers();
		if (requestedId.isEmpty()) return viewers.size() == 1 ? viewers.constFirst() : AutomationScreenShareViewer {};
		for (const AutomationScreenShareViewer &viewer : viewers) {
			if (viewer.backend && viewer.backend->streamId() == requestedId) return viewer;
		}
		return {};
	}

	QVariantMap automationScreenShareViewerState(const AutomationScreenShareViewer &viewer) {
		QVariantMap state { { QStringLiteral("available"), viewer.window && viewer.backend } };
		if (!viewer.window || !viewer.backend) return state;
		ScreenShareViewBackend *backend = viewer.backend;
		QQuickWindow *window = viewer.window;
		state.insert(QStringLiteral("streamId"), backend->streamId());
		state.insert(QStringLiteral("title"), backend->title());
		state.insert(QStringLiteral("detail"), backend->detail());
		state.insert(QStringLiteral("status"), backend->status());
		state.insert(QStringLiteral("paused"), backend->paused());
		state.insert(QStringLiteral("audioAvailable"), backend->audioAvailable());
		state.insert(QStringLiteral("audioMuted"), backend->audioMuted());
		state.insert(QStringLiteral("audioVolume"), backend->audioVolume());
		state.insert(QStringLiteral("processId"), backend->processId());
		state.insert(QStringLiteral("renderTransport"), backend->renderTransport());
		state.insert(QStringLiteral("nativeFrameTransportAvailable"), backend->nativeFrameTransportAvailable());
		state.insert(QStringLiteral("nativeFrameTransportBlocker"), backend->nativeFrameTransportBlocker());
		state.insert(QStringLiteral("nativeFrameActive"), backend->nativeFrameActive());
		state.insert(QStringLiteral("hasCurrentFrame"), backend->hasCurrentFrame());
		state.insert(QStringLiteral("externalVideoWindowPresent"), backend->videoWindow() != nullptr);
		state.insert(QStringLiteral("operationStatus"), backend->operationStatus());
		state.insert(QStringLiteral("operationError"), backend->operationError());
		state.insert(QStringLiteral("operationCancellable"), backend->operationCancellable());
		state.insert(QStringLiteral("displayState"), window->property("displayState"));
		state.insert(QStringLiteral("playbackSurfaceReady"), window->property("playbackSurfaceReady"));
		state.insert(QStringLiteral("hasShownLiveFrame"), window->property("hasShownLiveFrame"));
		state.insert(QStringLiteral("windowVisible"), window->isVisible());
		state.insert(QStringLiteral("windowExposed"), window->isExposed());
		state.insert(QStringLiteral("windowReady"), window->isVisible() && window->isExposed());
		state.insert(QStringLiteral("windowFullscreen"), window->visibility() == QWindow::FullScreen);
		return state;
	}

	QVariantList automationScreenShareViewerStates() {
		QVariantList states;
		const QList< AutomationScreenShareViewer > viewers = automationScreenShareViewers();
		states.reserve(viewers.size());
		for (const AutomationScreenShareViewer &viewer : viewers) {
			states.push_back(automationScreenShareViewerState(viewer));
		}
		return states;
	}

	QObject *automationFindRichPreviewCard(QQuickWindow *window, const QString &messageId) {
		if (!window || messageId.trimmed().isEmpty()) {
			return nullptr;
		}
		const QString prefix = messageId.trimmed() + QLatin1Char('|');
		// QML delegates are parented through the visual item tree. Their QObject
		// ownership does not have to descend from QQuickWindow, so QObject::findChildren
		// misses live, rendered cards after ListView delegate reuse.
		for (QQuickItem *candidate : automationQuickItemSubtree(window->contentItem())) {
			const QVariant identityValue = candidate->property("previewIdentity");
			if (identityValue.isValid() && identityValue.toString().startsWith(prefix)
				&& candidate->property("renderActive").toBool()
				&& automationQuickItemHasVisibleAncestry(candidate)) {
				return candidate;
			}
		}
		return nullptr;
	}

	QRectF automationQuickItemSceneRect(QQuickItem *item) {
		if (!item || item->width() <= 0.0 || item->height() <= 0.0) {
			return {};
		}
		return item->mapRectToScene(QRectF(0.0, 0.0, item->width(), item->height())).normalized();
	}

	QVariantMap automationSceneRectState(const QRectF &rect) {
		return QVariantMap { { QStringLiteral("x"), rect.x() }, { QStringLiteral("y"), rect.y() },
			{ QStringLiteral("width"), rect.width() }, { QStringLiteral("height"), rect.height() } };
	}

	QString automationImageStatusName(int status) {
		switch (status) {
			case 1:
				return QStringLiteral("ready");
			case 2:
				return QStringLiteral("loading");
			case 3:
				return QStringLiteral("error");
			default:
				return QStringLiteral("null");
		}
	}

	bool automationEffectiveImageRects(QQuickItem *item, QQuickItem *card, QRectF *sceneRect,
									   QRectF *visibleSceneRect) {
		if (!item || !card || item->window() != card->window()) {
			return false;
		}

		const QRectF itemSceneRect = automationQuickItemSceneRect(item);
		const QRectF cardSceneRect = automationQuickItemSceneRect(card);
		if (itemSceneRect.isEmpty() || cardSceneRect.isEmpty()) {
			return false;
		}

		QRectF effectiveRect = itemSceneRect.intersected(cardSceneRect);
		bool descendsFromCard = false;
		for (QQuickItem *current = item; current; current = current->parentItem()) {
			if (!current->isVisible() || current->opacity() <= 0.0) {
				return false;
			}
			if (current == card) {
				descendsFromCard = true;
			}
			if (current->clip()) {
				effectiveRect = effectiveRect.intersected(automationQuickItemSceneRect(current));
				if (effectiveRect.isEmpty()) {
					return false;
				}
			}
		}

		if (!descendsFromCard || effectiveRect.isEmpty()) {
			return false;
		}
		if (sceneRect) {
			*sceneRect = itemSceneRect;
		}
		if (visibleSceneRect) {
			*visibleSceneRect = effectiveRect;
		}
		return true;
	}

	QVariantMap automationRichPreviewCardState(QQuickWindow *window, const QString &messageId) {
		QVariantMap state;
		state.insert(QStringLiteral("messageId"), messageId);
		QObject *card = automationFindRichPreviewCard(window, messageId);
		state.insert(QStringLiteral("rendered"), card != nullptr);
		if (!card) {
			state.insert(QStringLiteral("compact"), false);
			state.insert(QStringLiteral("playAccessibilityName"), QString());
			state.insert(QStringLiteral("providerDetailsVisible"), false);
			state.insert(QStringLiteral("providerVariant"), QString());
			state.insert(QStringLiteral("providerToken"), QString());
			state.insert(QStringLiteral("providerFamily"), QString());
			state.insert(QStringLiteral("providerPresentation"), QString());
			state.insert(QStringLiteral("visibleImages"), QVariantList());
			state.insert(QStringLiteral("visibleImageCount"), 0);
			state.insert(QStringLiteral("imageSources"), QStringList());
			state.insert(QStringLiteral("imageSourceCount"), 0);
			state.insert(QStringLiteral("imageReadyCount"), 0);
			state.insert(QStringLiteral("imageLoadingCount"), 0);
			state.insert(QStringLiteral("imageErrorCount"), 0);
			return state;
		}

		state.insert(QStringLiteral("compact"), card->property("compact").toBool());
		state.insert(QStringLiteral("expanded"), card->property("expanded").toBool());
		state.insert(QStringLiteral("userExpanded"), card->property("userExpanded").toBool());
		state.insert(QStringLiteral("sensitiveMediaRevealed"),
			card->property("sensitiveMediaRevealed").toBool());
		state.insert(QStringLiteral("mediaRequiresReveal"), card->property("mediaRequiresReveal").toBool());
		state.insert(QStringLiteral("previewState"), card->property("previewState").toString());
		state.insert(QStringLiteral("renderActive"), card->property("renderActive").toBool());
		state.insert(QStringLiteral("inlinePlaybackActive"), card->property("inlinePlaybackActive").toBool());
		state.insert(QStringLiteral("localPlaybackSupported"), card->property("localPlaybackSupported").toBool());
		state.insert(QStringLiteral("mediaSessionId"), card->property("mediaSessionId").toString());
		state.insert(QStringLiteral("embedPosterSource"), card->property("embedPosterSource").toString());
		state.insert(QStringLiteral("playAccessibilityName"),
			card->property("playAccessibilityName").toString());
		QQuickItem *cardItem = qobject_cast< QQuickItem * >(card);
		const auto appendSceneRect = [&state](const QString &prefix, QQuickItem *item) {
			state.insert(prefix + QStringLiteral("Visible"), item && item->isVisible());
			if (!item) {
				state.insert(prefix + QStringLiteral("X"), 0.0);
				state.insert(prefix + QStringLiteral("Y"), 0.0);
				state.insert(prefix + QStringLiteral("Width"), 0.0);
				state.insert(prefix + QStringLiteral("Height"), 0.0);
				return;
			}
			const QPointF scenePosition = item->mapToScene(QPointF(0, 0));
			state.insert(prefix + QStringLiteral("X"), scenePosition.x());
			state.insert(prefix + QStringLiteral("Y"), scenePosition.y());
			state.insert(prefix + QStringLiteral("Width"), item->width());
			state.insert(prefix + QStringLiteral("Height"), item->height());
		};
		appendSceneRect(QStringLiteral("card"), cardItem);
		QQuickItem *timelineItem = window && window->contentItem()
			? qobject_cast< QQuickItem * >(automationFindQuickItemByObjectName(
				window->contentItem(), QStringLiteral("chatTimeline"))) : nullptr;
		appendSceneRect(QStringLiteral("timeline"), timelineItem);
		QQuickItem *mediaPanel = cardItem
			? qobject_cast< QQuickItem * >(automationFindQuickItemByObjectName(
				cardItem, QStringLiteral("previewEmbedMediaPanel"))) : nullptr;
		appendSceneRect(QStringLiteral("media"), mediaPanel);
		QObject *playButton = cardItem
			? automationFindQuickItemByObjectName(cardItem, QStringLiteral("previewPlayButton")) : nullptr;
		QObject *embedPoster = cardItem
			? automationFindQuickItemByObjectName(cardItem, QStringLiteral("previewEmbedPoster")) : nullptr;
		QObject *openSurface = cardItem
			? automationFindQuickItemByObjectName(cardItem, QStringLiteral("previewCardOpenSurface")) : nullptr;
		QObject *providerDetails = cardItem
			? automationFindQuickItemByObjectName(cardItem, QStringLiteral("providerDetails")) : nullptr;
		state.insert(QStringLiteral("playVisible"), playButton && playButton->property("visible").toBool());
		state.insert(QStringLiteral("embedPosterStatus"),
			embedPoster ? embedPoster->property("status").toInt() : -1);
		state.insert(QStringLiteral("embedPosterRenderedSource"),
			embedPoster ? embedPoster->property("source").toString() : QString());
		state.insert(QStringLiteral("openSurfaceVisible"),
			openSurface && openSurface->property("visible").toBool());
		state.insert(QStringLiteral("providerDetailsVisible"),
			providerDetails && providerDetails->property("visible").toBool());
		state.insert(QStringLiteral("providerVariant"),
			providerDetails ? providerDetails->property("variant").toString() : QString());
		state.insert(QStringLiteral("providerToken"),
			providerDetails ? providerDetails->property("providerToken").toString() : QString());
		state.insert(QStringLiteral("providerFamily"),
			providerDetails ? providerDetails->property("family").toString() : QString());
		state.insert(QStringLiteral("providerPresentation"),
			providerDetails ? providerDetails->property("presentation").toString() : QString());

		bool focused = false;
		if (window) {
			for (QQuickItem *item = window->activeFocusItem(); item; item = item->parentItem()) {
				if (item == cardItem) {
					focused = true;
					break;
				}
			}
		}
		state.insert(QStringLiteral("focused"), focused);

		int imageSourceCount  = 0;
		int imageReadyCount   = 0;
		int imageLoadingCount = 0;
		int imageErrorCount   = 0;
		QStringList renderedImageSources;
		QVariantList visibleImages;
		QList< QQuickItem * > imageCandidates;
		if (cardItem) {
			imageCandidates = automationQuickItemSubtree(cardItem);
		}
		for (QQuickItem *candidate : imageCandidates) {
			const QVariant sourceValue = candidate->property("source");
			if (!sourceValue.isValid()) {
				continue;
			}
			const QString source = sourceValue.toString().trimmed();
			if (!source.startsWith(QLatin1String("image://mumble/"), Qt::CaseInsensitive)) {
				continue;
			}
			QRectF sceneRect;
			QRectF visibleSceneRect;
			if (!automationEffectiveImageRects(candidate, cardItem, &sceneRect, &visibleSceneRect)) {
				continue;
			}
			++imageSourceCount;
			if (!renderedImageSources.contains(source)) {
				renderedImageSources.push_back(source);
			}
			const int status = candidate->property("status").toInt();
			visibleImages.push_back(QVariantMap {
				{ QStringLiteral("source"), source },
				{ QStringLiteral("objectName"), candidate->objectName() },
				{ QStringLiteral("status"), status },
				{ QStringLiteral("statusName"), automationImageStatusName(status) },
				{ QStringLiteral("effectiveVisible"), true },
				{ QStringLiteral("intersectsCard"), true },
				{ QStringLiteral("sceneRect"), automationSceneRectState(sceneRect) },
				{ QStringLiteral("visibleSceneRect"), automationSceneRectState(visibleSceneRect) }
			});
			if (status == 1) {
				++imageReadyCount;
			} else if (status == 2) {
				++imageLoadingCount;
			} else if (status == 3) {
				++imageErrorCount;
			}
		}
		state.insert(QStringLiteral("visibleImages"), visibleImages);
		state.insert(QStringLiteral("visibleImageCount"), visibleImages.size());
		state.insert(QStringLiteral("imageSources"), renderedImageSources);
		state.insert(QStringLiteral("imageSourceCount"), imageSourceCount);
		state.insert(QStringLiteral("imageReadyCount"), imageReadyCount);
		state.insert(QStringLiteral("imageLoadingCount"), imageLoadingCount);
		state.insert(QStringLiteral("imageErrorCount"), imageErrorCount);
		return state;
	}

	QVariantMap automationLiveRichPreviewState(QmlShellHost *host, const QString &messageId) {
		const QString stableId = messageId.trimmed();
		QVariantMap state {
			{ QStringLiteral("messageId"), stableId },
			{ QStringLiteral("modelPresent"), false },
			{ QStringLiteral("rowIndex"), -1 },
			{ QStringLiteral("previewState"), QStringLiteral("none") },
			{ QStringLiteral("preview"), QVariantMap() },
			{ QStringLiteral("card"), QVariantMap() }
		};
		if (!host || stableId.isEmpty()) return state;

		ChatTimelineModel *timeline = host->chatModel();
		const int row = timeline ? timeline->rowForStableId(stableId) : -1;
		state.insert(QStringLiteral("rowIndex"), row);
		if (row < 0) return state;

		const QVariantMap modelRow = timeline->get(row);
		const QVariantMap preview = modelRow.value(QStringLiteral("preview")).toMap();
		QString previewState = preview.value(QStringLiteral("state")).toString().trimmed().toLower();
		if (previewState.isEmpty()) {
			previewState = preview.value(QStringLiteral("failed")).toBool() ? QStringLiteral("error")
				: preview.value(QStringLiteral("loading")).toBool() ? QStringLiteral("loading")
				: preview.isEmpty() ? QStringLiteral("none") : QStringLiteral("ready");
		}
		state.insert(QStringLiteral("modelPresent"), true);
		state.insert(QStringLiteral("actor"), modelRow.value(QStringLiteral("title")));
		state.insert(QStringLiteral("bodyText"), modelRow.value(QStringLiteral("subtitle")));
		state.insert(QStringLiteral("deliveryState"), modelRow.value(QStringLiteral("status")));
		state.insert(QStringLiteral("timestamp"), modelRow.value(QStringLiteral("timestamp")));
		state.insert(QStringLiteral("previewState"), previewState);
		state.insert(QStringLiteral("preview"), preview);
		state.insert(QStringLiteral("card"), automationRichPreviewCardState(host->window(), stableId));
		return state;
	}

	QVariantMap automationRichMessageBodyState(QmlShellHost *host, QQuickWindow *window,
											 const QString &messageId) {
		QVariantMap state {
			{ QStringLiteral("messageId"), messageId },
			{ QStringLiteral("modelPresent"), false },
			{ QStringLiteral("modelSegmentCount"), 0 },
			{ QStringLiteral("modelImageCount"), 0 },
			{ QStringLiteral("modelImages"), QVariantList() },
			{ QStringLiteral("rendered"), false },
			{ QStringLiteral("cardVisible"), false },
			{ QStringLiteral("cardX"), 0.0 },
			{ QStringLiteral("cardY"), 0.0 },
			{ QStringLiteral("cardWidth"), 0.0 },
			{ QStringLiteral("cardHeight"), 0.0 },
			{ QStringLiteral("cardHref"), QString() },
			{ QStringLiteral("cardLabel"), QString() },
			{ QStringLiteral("timelineVisible"), false },
			{ QStringLiteral("timelineX"), 0.0 },
			{ QStringLiteral("timelineY"), 0.0 },
			{ QStringLiteral("timelineWidth"), 0.0 },
			{ QStringLiteral("timelineHeight"), 0.0 },
			{ QStringLiteral("imagePresent"), false },
			{ QStringLiteral("imageEffectiveVisible"), false },
			{ QStringLiteral("imageSource"), QString() },
			{ QStringLiteral("imageStatus"), 0 },
			{ QStringLiteral("imageStatusName"), QStringLiteral("null") },
			{ QStringLiteral("imageSceneRect"), automationSceneRectState({}) },
			{ QStringLiteral("imageVisibleSceneRect"), automationSceneRectState({}) }
		};

		if (host && host->chatModel()) {
			const int row = host->chatModel()->rowForStableId(messageId);
			if (row >= 0) {
				const QVariantList segments = host->chatModel()->get(row)
					.value(QStringLiteral("bodySegments")).toList();
				QVariantList modelImages;
				for (const QVariant &entry : segments) {
					const QVariantMap segment = entry.toMap();
					if (segment.value(QStringLiteral("kind")).toString() == QLatin1String("image")) {
						modelImages.push_back(segment);
					}
				}
				state.insert(QStringLiteral("modelPresent"), true);
				state.insert(QStringLiteral("modelSegmentCount"), segments.size());
				state.insert(QStringLiteral("modelImageCount"), modelImages.size());
				state.insert(QStringLiteral("modelImages"), modelImages);
			}
		}

		if (!window || !window->contentItem()) return state;
		const auto belongsToMessage = [&messageId](QQuickItem *item) {
			for (QQuickItem *current = item; current; current = current->parentItem()) {
				const QVariant stableId = current->property("stableId");
				if (stableId.isValid() && stableId.toString() == messageId) return true;
			}
			return false;
		};
		QQuickItem *card = nullptr;
		for (QQuickItem *candidate : automationQuickItemSubtree(window->contentItem())) {
			if (candidate->objectName().startsWith(QLatin1String("richMessageImageCard_"))
				&& belongsToMessage(candidate) && automationQuickItemHasVisibleAncestry(candidate)) {
				card = candidate;
				break;
			}
		}

		const auto appendSceneRect = [&state](const QString &prefix, QQuickItem *item) {
			state.insert(prefix + QStringLiteral("Visible"),
				item && automationQuickItemHasVisibleAncestry(item));
			const QRectF rect = automationQuickItemSceneRect(item);
			state.insert(prefix + QStringLiteral("X"), rect.x());
			state.insert(prefix + QStringLiteral("Y"), rect.y());
			state.insert(prefix + QStringLiteral("Width"), rect.width());
			state.insert(prefix + QStringLiteral("Height"), rect.height());
		};
		appendSceneRect(QStringLiteral("card"), card);
		QQuickItem *timeline = qobject_cast< QQuickItem * >(automationFindQuickItemByObjectName(
			window->contentItem(), QStringLiteral("chatTimeline")));
		appendSceneRect(QStringLiteral("timeline"), timeline);
		state.insert(QStringLiteral("rendered"), card != nullptr);
		if (!card) return state;
		state.insert(QStringLiteral("cardHref"), card->property("safeHref").toString());
		state.insert(QStringLiteral("cardLabel"), card->property("accessibleLabel").toString());

		QQuickItem *image = nullptr;
		for (QQuickItem *candidate : automationQuickItemSubtree(card)) {
			if (candidate->objectName().startsWith(QLatin1String("richMessageInlineImage_"))) {
				image = candidate;
				break;
			}
		}
		state.insert(QStringLiteral("imagePresent"), image != nullptr);
		if (!image) return state;
		QRectF imageSceneRect;
		QRectF imageVisibleSceneRect;
		const bool imageEffectiveVisible = automationEffectiveImageRects(
			image, card, &imageSceneRect, &imageVisibleSceneRect);
		const int imageStatus = image->property("status").toInt();
		state.insert(QStringLiteral("imageEffectiveVisible"), imageEffectiveVisible);
		state.insert(QStringLiteral("imageSource"), image->property("source").toString());
		state.insert(QStringLiteral("imageStatus"), imageStatus);
		state.insert(QStringLiteral("imageStatusName"), automationImageStatusName(imageStatus));
		state.insert(QStringLiteral("imageSceneRect"), automationSceneRectState(imageSceneRect));
		state.insert(QStringLiteral("imageVisibleSceneRect"),
			automationSceneRectState(imageVisibleSceneRect));
		return state;
	}

} // namespace

ModernUiAutomationServer::ModernUiAutomationServer(MainWindow *mainWindow, QObject *parent)
	: QObject(parent), m_mainWindow(mainWindow) {
}

ModernUiAutomationServer::~ModernUiAutomationServer() {
	if (m_chatPerformanceWorkload.active && m_mainWindow) {
		finalizeChatPerformanceWorkload(m_mainWindow->qmlShellHost());
	}
	if (m_talkPerformanceWorkload.active && m_mainWindow) {
		finalizeTalkPerformanceWorkload(m_mainWindow->qmlShellHost());
	}
}

QVariantMap ModernUiAutomationServer::finalizeChatPerformanceWorkload(QmlShellHost *host) {
	QVariantMap response = okResponse();
	if (!m_chatPerformanceWorkload.active) {
		response.insert(QStringLiteral("restored"), true);
		return response;
	}
	QObject::disconnect(m_chatPerformanceWorkload.frameConnection);
	m_chatPerformanceWorkload.running = false;
	if (!host || !host->chatModel()) {
		response.insert(QStringLiteral("ok"), false);
		response.insert(QStringLiteral("error"), tr("The Qt Quick chat fixture host disappeared before restore."));
		m_chatPerformanceWorkload = {};
		return response;
	}
	if (host->window()) {
		QMetaObject::invokeMethod(host->window(), "completePerformanceChatScrollWorkload");
	}
	host->setVisualFixtureMutationActive(true);
	host->chatModel()->replaceMessages(m_chatPerformanceWorkload.liveMessages);
	host->setVisualFixtureMutationActive(false);
	host->setVisualFixtureOverrideActive(m_chatPerformanceWorkload.previousFixtureOverride);
	response.insert(QStringLiteral("restored"), host->chatModel()->messages() == m_chatPerformanceWorkload.liveMessages);
	response.insert(QStringLiteral("restoredMessageCount"), host->chatModel()->rowCount());
	m_chatPerformanceWorkload = {};
	return response;
}

void ModernUiAutomationServer::advanceChatPerformanceWorkload() {
	if (!m_chatPerformanceWorkload.active || !m_chatPerformanceWorkload.running || !m_mainWindow) return;
	QmlShellHost *host = m_mainWindow->qmlShellHost();
	if (!host || !host->window() || !host->performanceMonitor()) {
		QObject::disconnect(m_chatPerformanceWorkload.frameConnection);
		m_chatPerformanceWorkload.running = false;
		m_chatPerformanceWorkload.failureReason =
			tr("The Qt Quick chat performance host disappeared while scrolling.");
		return;
	}

	QmlPerformanceMonitor *monitor = host->performanceMonitor();
	if (!m_chatPerformanceWorkload.primed) {
		// Establish one presented-frame baseline before the first scroll input.
		// Each following frame then retires exactly one input and schedules the
		// next real ListView contentY mutation, independent of automation/TCP
		// cadence and timer coalescing.
		m_chatPerformanceWorkload.primed = true;
		m_chatPerformanceWorkload.presentedFramesBeforeRun =
			monitor->snapshot().value(QStringLiteral("presentedFrameCount")).toInt();
	}
	if (m_chatPerformanceWorkload.stepCount >= m_chatPerformanceWorkload.targetSteps) {
		QObject::disconnect(m_chatPerformanceWorkload.frameConnection);
		m_chatPerformanceWorkload.running = false;
		if (!QMetaObject::invokeMethod(host->window(), "completePerformanceChatScrollWorkload")) {
			m_chatPerformanceWorkload.failureReason =
				tr("The Qt Quick root could not finish the chat-scroll workload.");
		}
		return;
	}

	const int nextStep = m_chatPerformanceWorkload.stepCount + 1;
	monitor->markInput(QStringLiteral("chat-scroll:%1").arg(++m_performanceInputSequence));
	QVariant advanced;
	if (!QMetaObject::invokeMethod(host->window(), "advancePerformanceChatScrollWorkload",
			Q_RETURN_ARG(QVariant, advanced), Q_ARG(QVariant, nextStep),
			Q_ARG(QVariant, m_chatPerformanceWorkload.targetSteps))
		|| !advanced.toMap().value(QStringLiteral("advanced")).toBool()) {
		QObject::disconnect(m_chatPerformanceWorkload.frameConnection);
		m_chatPerformanceWorkload.running = false;
		m_chatPerformanceWorkload.failureReason =
			tr("The Qt Quick root could not advance chat-scroll step %1.").arg(nextStep);
		return;
	}

	m_chatPerformanceWorkload.stepCount = nextStep;
	host->window()->update();
}

QVariantMap ModernUiAutomationServer::finalizeTalkPerformanceWorkload(QmlShellHost *host) {
	QVariantMap response = okResponse();
	if (!m_talkPerformanceWorkload.active) {
		response.insert(QStringLiteral("restored"), true);
		return response;
	}
	QObject::disconnect(m_talkPerformanceWorkload.frameConnection);
	m_talkPerformanceWorkload.running = false;
	if (!host || !host->participantModel()) {
		response.insert(QStringLiteral("ok"), false);
		response.insert(QStringLiteral("error"), tr("The Qt Quick talk fixture host disappeared before restore."));
		m_talkPerformanceWorkload = {};
		return response;
	}
	host->setVisualFixtureMutationActive(true);
	host->participantModel()->replaceParticipantStates(m_talkPerformanceWorkload.liveParticipants);
	host->setVisualFixtureMutationActive(false);
	host->setVisualFixtureOverrideActive(m_talkPerformanceWorkload.previousFixtureOverride);
	response.insert(QStringLiteral("restored"),
					host->participantModel()->participantStates() == m_talkPerformanceWorkload.liveParticipants);
	response.insert(QStringLiteral("restoredParticipantCount"), host->participantModel()->rowCount());
	response.insert(QStringLiteral("transitionCount"), m_talkPerformanceWorkload.transitionCount);
	m_talkPerformanceWorkload = {};
	return response;
}

void ModernUiAutomationServer::advanceTalkPerformanceWorkload() {
	if (!m_talkPerformanceWorkload.active || !m_talkPerformanceWorkload.running || !m_mainWindow) return;
	QmlShellHost *host = m_mainWindow->qmlShellHost();
	if (!host || !host->window() || !host->participantModel() || !host->performanceMonitor()) {
		QObject::disconnect(m_talkPerformanceWorkload.frameConnection);
		m_talkPerformanceWorkload.running = false;
		return;
	}

	QmlPerformanceMonitor *monitor = host->performanceMonitor();
	if (!m_talkPerformanceWorkload.primed) {
		// The first presented frame establishes the interval baseline. Every
		// subsequent frame completes exactly one typed talk-state transition, so
		// N transitions yield N input samples and N frame-interval samples without
		// depending on automation transport or PowerShell timer cadence.
		m_talkPerformanceWorkload.primed = true;
		m_talkPerformanceWorkload.presentedFramesBefore =
			monitor->snapshot().value(QStringLiteral("presentedFrameCount")).toInt();
	}
	if (m_talkPerformanceWorkload.transitionCount >= m_talkPerformanceWorkload.targetTransitions) {
		QObject::disconnect(m_talkPerformanceWorkload.frameConnection);
		m_talkPerformanceWorkload.running = false;
		return;
	}

	m_talkPerformanceWorkload.talking = !m_talkPerformanceWorkload.talking;
	const bool talking = m_talkPerformanceWorkload.talking;
	monitor->markInput(QStringLiteral("talk-state:%1").arg(++m_performanceInputSequence));
	{
		mumble::chatperf::ScopedDuration trace("qml.participant.talk_state_update");
		host->participantModel()->updatePresence(
			m_talkPerformanceWorkload.sessionId,
			talking ? QStringLiteral("talking") : QStringLiteral("passive"),
			talking ? tr("Talking") : tr("Listening"),
			talking ? QStringLiteral("accent") : QString(), talking, false, {}, {});
	}
	++m_talkPerformanceWorkload.transitionCount;
	host->window()->update();
}

bool ModernUiAutomationServer::start(QString *errorMessage) {
	if (m_server && m_server->isListening()) {
		return true;
	}

	const QString portText = qEnvironmentVariable("MUMBLE_MODERN_AUTOMATION_PORT").trimmed();
	if (portText.isEmpty()) {
		return true;
	}

	bool parsedPort = false;
	const uint requestedPort = portText.toUInt(&parsedPort);
	if (!parsedPort || requestedPort > 65535) {
		if (errorMessage) {
			*errorMessage = QObject::tr("Invalid MUMBLE_MODERN_AUTOMATION_PORT value '%1'.").arg(portText);
		}
		return false;
	}

	m_token = qEnvironmentVariable("MUMBLE_MODERN_AUTOMATION_TOKEN");
	m_server = new QTcpServer(this);
	connect(m_server, &QTcpServer::newConnection, this, &ModernUiAutomationServer::handleNewConnection);

	if (!m_server->listen(QHostAddress::LocalHost, static_cast< quint16 >(requestedPort))) {
		if (errorMessage) {
			*errorMessage = QObject::tr("Unable to start Modern UI automation server on 127.0.0.1:%1: %2")
								.arg(requestedPort)
								.arg(m_server->errorString());
		}
		m_server->deleteLater();
		m_server = nullptr;
		return false;
	}

	installAutomationOffscreenFilter();
	return true;
}

bool ModernUiAutomationServer::isListening() const {
	return m_server && m_server->isListening();
}

QmlVisualFixtureController *ModernUiAutomationServer::visualFixtureController() {
	QmlShellHost *host = m_mainWindow ? m_mainWindow->qmlShellHost() : nullptr;
	if (!m_visualFixtureController) {
		m_visualFixtureController = std::make_unique< QmlVisualFixtureController >(host);
	} else {
		m_visualFixtureController->setHost(host);
	}
	return m_visualFixtureController.get();
}

quint16 ModernUiAutomationServer::port() const {
	return isListening() ? m_server->serverPort() : 0;
}

bool ModernUiAutomationServer::eventFilter(QObject *watched, QEvent *event) {
	if (automationOffscreenModeEnabled() && watched && event) {
		switch (event->type()) {
			case QEvent::Polish:
			case QEvent::Show:
			case QEvent::WinIdChange:
			case QEvent::WindowActivate: {
				if (QWidget *widget = qobject_cast< QWidget * >(watched); widget && widget->isWindow()) {
					prepareTopLevelWidgetForAutomation(widget);
					if (event->type() == QEvent::WindowActivate) {
						QPointer< QWidget > guardedWidget(widget);
						QTimer::singleShot(0, widget, [guardedWidget]() {
							if (guardedWidget) {
								guardedWidget->clearFocus();
								guardedWidget->move(-32000, -32000);
							}
						});
					}
				}
				break;
			}
			default:
				break;
		}
	}

	return QObject::eventFilter(watched, event);
}

bool ModernUiAutomationServer::automationOffscreenModeEnabled() const {
	return qEnvironmentVariableIsSet("MUMBLE_MODERN_AUTOMATION_OFFSCREEN");
}

void ModernUiAutomationServer::installAutomationOffscreenFilter() {
	if (!automationOffscreenModeEnabled() || m_offscreenFilterInstalled || !qApp) {
		return;
	}

	qApp->installEventFilter(this);
	m_offscreenFilterInstalled = true;
	for (QWidget *widget : QApplication::topLevelWidgets()) {
		prepareTopLevelWidgetForAutomation(widget);
	}
}

void ModernUiAutomationServer::prepareTopLevelWidgetForAutomation(QWidget *widget) const {
	if (!widget || !widget->isWindow()) {
		return;
	}

	widget->setAttribute(Qt::WA_ShowWithoutActivating, true);
	if (!widget->isVisible()) {
		widget->setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
	}
	widget->move(-32000, -32000);
}

void ModernUiAutomationServer::handleNewConnection() {
	if (!m_server) {
		return;
	}

	while (QTcpSocket *socket = m_server->nextPendingConnection()) {
		socket->setParent(this);
		connect(socket, &QTcpSocket::readyRead, this, [this, socket]() { handleReadyRead(socket); });
		connect(socket, &QTcpSocket::disconnected, socket, &QTcpSocket::deleteLater);
	}
}

void ModernUiAutomationServer::handleReadyRead(QTcpSocket *socket) {
	if (!socket) {
		return;
	}

	while (socket->canReadLine()) {
		const QByteArray line = socket->readLine().trimmed();
		if (line.isEmpty()) {
			continue;
		}

		QJsonParseError parseError;
		const QJsonDocument document = QJsonDocument::fromJson(line, &parseError);
		if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
			writeResponse(socket, errorResponse(tr("Invalid JSON request: %1").arg(parseError.errorString())));
			continue;
		}

		const QVariantMap request = document.object().toVariantMap();
		QVariantMap response;
		if (!authorizeRequest(request, response)) {
			writeResponse(socket, response);
			continue;
		}

		writeResponse(socket, handleRequest(request));
	}
}

QVariantMap ModernUiAutomationServer::handleRequest(const QVariantMap &request) {
	if (!m_mainWindow) {
		return errorResponse(tr("Main window is no longer available."));
	}

	const QString command = request.value(QStringLiteral("command")).toString().trimmed();
	if (command.isEmpty()) {
		return errorResponse(tr("Missing automation command."));
	}

	if (command == QLatin1String("ping")) {
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("command"), QStringLiteral("pong"));
		response.insert(QStringLiteral("pid"), static_cast< qlonglong >(QCoreApplication::applicationPid()));
		response.insert(QStringLiteral("port"), port());
		return response;
	}

	if (command == QLatin1String("snapshot")) {
		return buildStateResponse();
	}

	if (command == QLatin1String("toastState") || command == QLatin1String("publishToast")
		|| command == QLatin1String("dismissToast")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host || !host->toastController()) {
			return errorResponse(tr("The typed toast controller is not active."));
		}
		ToastController *toast = host->toastController();
		if (command == QLatin1String("publishToast")) {
			const QString title = request.value(QStringLiteral("title")).toString().left(512);
			const QString message = request.value(QStringLiteral("message")).toString().left(2048);
			if (title.trimmed().isEmpty() && message.trimmed().isEmpty()) {
				return errorResponse(tr("A toast title or message is required."));
			}
			toast->publish(request.value(QStringLiteral("tone"), QStringLiteral("info")).toString(),
				title, message, request.value(QStringLiteral("actionId")).toString().left(256),
				request.value(QStringLiteral("actionLabel")).toString().left(256),
				qBound(250, request.value(QStringLiteral("timeoutMs"), 4500).toInt(), 60000));
		} else if (command == QLatin1String("dismissToast")) {
			toast->dismiss();
		}
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("toast"), automationToastState(toast));
		return response;
	}

	const QStringList richDirectMessageCommands {
		QStringLiteral("directMessageReply"), QStringLiteral("directMessageCancelReply"),
		QStringLiteral("directMessageRetry"), QStringLiteral("directMessageDelete"),
		QStringLiteral("directMessageToggleReaction"), QStringLiteral("directMessageChooseAttachment"),
		QStringLiteral("directMessageRemoveAttachment"), QStringLiteral("directMessageRetryAttachment"),
		QStringLiteral("directMessageOpenAttachment"), QStringLiteral("directMessageDownloadAttachment"),
		QStringLiteral("directMessageHydrateContent")
	};
	if (richDirectMessageCommands.contains(command)) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		DirectMessageController *directMessages = host ? host->directMessageController() : nullptr;
		if (!directMessages || !directMessages->conversationOpen()) {
			return errorResponse(tr("Open a direct-message conversation before invoking a rich-message action."));
		}

		const QString stableId = request.value(QStringLiteral("messageId"),
			request.value(QStringLiteral("stableId"))).toString().trimmed();
		const int rowIndex = stableId.isEmpty() ? -1
			: directMessages->timelineModel()->rowForStableId(stableId);
		const QVariantMap row = rowIndex >= 0 ? directMessages->timelineModel()->get(rowIndex) : QVariantMap();
		const QString attachmentId = request.value(QStringLiteral("attachmentId")).toString().trimmed();
		bool accepted = false;
		bool requiresNativeDialog = false;

		if (command == QLatin1String("directMessageReply")) {
			accepted = !row.isEmpty() && row.value(QStringLiteral("canReply")).toBool();
			if (accepted) {
				directMessages->replyToMessage(stableId);
				accepted = directMessages->hasPendingReply();
			}
		} else if (command == QLatin1String("directMessageCancelReply")) {
			accepted = directMessages->hasPendingReply();
			directMessages->cancelPendingReply();
		} else if (command == QLatin1String("directMessageRetry")) {
			accepted = !row.isEmpty() && row.value(QStringLiteral("source")).toMap()
				.value(QStringLiteral("deliveryCanRetry")).toBool();
			if (accepted) directMessages->retryMessage(stableId);
		} else if (command == QLatin1String("directMessageDelete")) {
			accepted = !row.isEmpty() && row.value(QStringLiteral("canDelete")).toBool();
			if (accepted) directMessages->deleteMessage(stableId);
		} else if (command == QLatin1String("directMessageToggleReaction")) {
			const QString emoji = request.value(QStringLiteral("emoji")).toString().trimmed().left(64);
			accepted = !emoji.isEmpty() && !row.isEmpty() && row.value(QStringLiteral("canReact")).toBool();
			if (accepted) directMessages->toggleMessageReaction(stableId, emoji);
		} else if (command == QLatin1String("directMessageChooseAttachment")) {
			requiresNativeDialog = true;
			accepted = request.value(QStringLiteral("allowNativeDialog")).toBool()
				&& (directMessages->canAttachImages() || directMessages->canAttachFiles());
			if (accepted) directMessages->chooseAttachment();
		} else if (command == QLatin1String("directMessageRemoveAttachment")
			|| command == QLatin1String("directMessageRetryAttachment")) {
			for (const QVariant &value : directMessages->draftAttachments()) {
				const QVariantMap attachment = value.toMap();
				if (attachment.value(QStringLiteral("id")).toString() == attachmentId) {
					accepted = true;
					break;
				}
			}
			if (accepted && command == QLatin1String("directMessageRemoveAttachment"))
				directMessages->removeDraftAttachment(attachmentId);
			else if (accepted)
				directMessages->retryDraftAttachment(attachmentId);
		} else if (command == QLatin1String("directMessageOpenAttachment")
			|| command == QLatin1String("directMessageDownloadAttachment")) {
			requiresNativeDialog = command == QLatin1String("directMessageDownloadAttachment");
			const QString assetId = request.value(QStringLiteral("assetId")).toString().trimmed();
			const QString fileName = request.value(QStringLiteral("fileName")).toString();
			accepted = !assetId.isEmpty() && (!requiresNativeDialog
				|| request.value(QStringLiteral("allowNativeDialog")).toBool());
			if (accepted && command == QLatin1String("directMessageOpenAttachment"))
				directMessages->openAttachment(assetId, fileName);
			else if (accepted)
				directMessages->downloadAttachment(assetId, fileName);
		} else if (command == QLatin1String("directMessageHydrateContent")) {
			accepted = !row.isEmpty();
			if (accepted) directMessages->requestContentHydration(stableId,
				request.value(QStringLiteral("highPriority")).toBool());
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("accepted"), accepted);
		response.insert(QStringLiteral("requiresNativeDialog"), requiresNativeDialog);
		response.insert(QStringLiteral("directMessages"), automationDirectMessageState(directMessages));
		return response;
	}

	if (command == QLatin1String("recorderAction")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host || !host->recorderController()) {
			return errorResponse(tr("The typed recorder controller is not active."));
		}
		Mumble::ModernRecorderController *recorder = host->recorderController();
		const QString action = request.value(QStringLiteral("action")).toString().trimmed();
		bool accepted = false;
		if (action == QLatin1String("start")) accepted = recorder->start();
		else if (action == QLatin1String("pause")) accepted = recorder->pause();
		else if (action == QLatin1String("resume")) accepted = recorder->resume();
		else if (action == QLatin1String("stop")) accepted = recorder->stop();
		else if (action == QLatin1String("clearError")) {
			recorder->clearError();
			accepted = true;
		} else if (action == QLatin1String("refresh")) {
			recorder->refreshCapabilities();
			recorder->refreshElapsed();
			accepted = true;
		} else {
			return errorResponse(tr("Unknown recorder action '%1'.").arg(action));
		}
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("accepted"), accepted);
		response.insert(QStringLiteral("state"), recorder->state());
		response.insert(QStringLiteral("operationId"), recorder->operationId());
		response.insert(QStringLiteral("operationStatus"), recorder->operationStatus());
		response.insert(QStringLiteral("errorCode"), recorder->errorCode());
		response.insert(QStringLiteral("errorMessage"), recorder->errorMessage());
		return response;
	}

	if (command.startsWith(QLatin1String("qmlPerformance"))) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host || !host->performanceMonitor())
			return errorResponse(tr("The Qt Quick performance monitor is not active."));
		QmlPerformanceMonitor *monitor = host->performanceMonitor();
		if (command == QLatin1String("qmlPerformanceReset")) {
			monitor->reset();
		} else if (command == QLatin1String("qmlPerformanceBegin")) {
			monitor->beginFrameSampling();
		} else if (command == QLatin1String("qmlPerformanceChatSeedStart")) {
			if (m_chatPerformanceWorkload.active)
				return errorResponse(tr("A chat performance fixture is already active."));
			if (m_talkPerformanceWorkload.active)
				return errorResponse(tr("A talk-state performance fixture is already active."));
			if (host->mediaSession()->active())
				return errorResponse(tr("Close the active media session before starting the chat performance fixture."));
			ChatTimelineModel *chat = host->chatModel();
			m_chatPerformanceWorkload.liveMessages = chat->messages();
			m_chatPerformanceWorkload.previousFixtureOverride = host->visualFixtureOverrideActive();
			host->setVisualFixtureOverrideActive(true);
			m_chatPerformanceWorkload.active = true;
			QVariantList messages;
			messages.reserve(96);
			const auto dormantMediaPreview = [](const int fixtureIndex) {
				const QString previewSize = fixtureIndex % 3 == 0 ? QStringLiteral("compact")
					: fixtureIndex % 3 == 1 ? QStringLiteral("default") : QStringLiteral("large");
				if (fixtureIndex % 2 == 0) {
					return QVariantMap {
						{ QStringLiteral("kind"), QStringLiteral("link") },
						{ QStringLiteral("url"), QStringLiteral("https://www.youtube.com/watch?v=qmlperf0001") },
						{ QStringLiteral("title"), QStringLiteral("Deterministic YouTube performance fixture") },
						{ QStringLiteral("subtitle"), QStringLiteral("YouTube") },
						{ QStringLiteral("description"), QStringLiteral("Dormant inline-playback card; no provider hydration.") },
						{ QStringLiteral("openLabel"), QStringLiteral("Open on YouTube") },
						{ QStringLiteral("embedKind"), QStringLiteral("youtube") },
						{ QStringLiteral("embedUrl"), QStringLiteral("https://www.youtube-nocookie.com/embed/qmlperf0001") },
						{ QStringLiteral("embedAspect"), QStringLiteral("wide") },
						{ QStringLiteral("previewSize"), previewSize },
						{ QStringLiteral("loading"), false },
						{ QStringLiteral("failed"), false },
						{ QStringLiteral("autoplay"), false }
					};
				}

				return QVariantMap {
					{ QStringLiteral("kind"), QStringLiteral("link") },
					{ QStringLiteral("url"), QStringLiteral("mumble://performance-fixture/local-audio") },
					{ QStringLiteral("title"), QStringLiteral("Deterministic local audio performance fixture") },
					{ QStringLiteral("subtitle"), QStringLiteral("Local audio") },
					{ QStringLiteral("description"), QStringLiteral("Dormant data-URL audio card; no network source.") },
					{ QStringLiteral("openLabel"), QStringLiteral("Open audio") },
					{ QStringLiteral("mediaKind"), QStringLiteral("audio") },
					{ QStringLiteral("mediaMime"), QStringLiteral("audio/wav") },
					{ QStringLiteral("mediaUrl"), QStringLiteral("data:audio/wav;base64,UklGRiQAAABXQVZFZm10IBAAAAABAAEAQB8AAIA+AAACABAAZGF0YQAAAAA=") },
					{ QStringLiteral("embedAspect"), QStringLiteral("compact-audio") },
					{ QStringLiteral("previewSize"), previewSize },
					{ QStringLiteral("loading"), false },
					{ QStringLiteral("failed"), false },
					{ QStringLiteral("autoplay"), false }
				};
			};
			for (int index = 0; index < 96; ++index) {
				QVariantMap message {
					{ QStringLiteral("messageKey"), QStringLiteral("qml-perf-message-%1").arg(index) },
					{ QStringLiteral("actor"), index % 2 ? QStringLiteral("Performance peer") : QStringLiteral("Performance self") },
					{ QStringLiteral("bodyText"), QStringLiteral("Deterministic chat workload row %1 %2").arg(index).arg(QString(80, QLatin1Char('x'))) },
					{ QStringLiteral("timeLabel"), QStringLiteral("12:%1").arg(index % 60, 2, 10, QLatin1Char('0')) },
					{ QStringLiteral("deliveryState"), QStringLiteral("sent") }, { QStringLiteral("own"), index % 2 == 0 }
				};
				if (index % 16 == 15) message.insert(QStringLiteral("preview"), dormantMediaPreview(index / 16));
				messages.push_back(message);
			}
			host->setVisualFixtureMutationActive(true);
			chat->replaceMessages(messages);
			host->setVisualFixtureMutationActive(false);
			m_chatPerformanceWorkload.presentedFramesBeforeSeed = monitor->snapshot().value(QStringLiteral("presentedFrameCount")).toInt();
			host->window()->update();
			QVariantMap response = okResponse();
			response.insert(QStringLiteral("modelCount"), chat->rowCount());
			response.insert(QStringLiteral("presentedFramesBefore"), m_chatPerformanceWorkload.presentedFramesBeforeSeed);
			return response;
		} else if (command == QLatin1String("qmlPerformanceChatSeedStatus")) {
			if (!m_chatPerformanceWorkload.active) return errorResponse(tr("No chat performance fixture is active."));
			QVariant qmlState;
			const bool invoked = QMetaObject::invokeMethod(host->window(), "performanceChatFixtureState", Q_RETURN_ARG(QVariant, qmlState));
			const QVariantMap layout = qmlState.toMap();
			const int frames = monitor->snapshot().value(QStringLiteral("presentedFrameCount")).toInt();
			QVariantMap response = okResponse();
			response.insert(QStringLiteral("modelCount"), host->chatModel()->rowCount());
			response.insert(QStringLiteral("qml"), layout);
			response.insert(QStringLiteral("presentedFramesBefore"), m_chatPerformanceWorkload.presentedFramesBeforeSeed);
			response.insert(QStringLiteral("presentedFramesAfter"), frames);
			response.insert(QStringLiteral("ready"), invoked && host->chatModel()->rowCount() == 96
									 && layout.value(QStringLiteral("count")).toInt() == 96
									 && layout.value(QStringLiteral("scrollable")).toBool()
									 && layout.value(QStringLiteral("settled")).toBool()
									 && !layout.value(QStringLiteral("firstVisibleId")).toString().isEmpty()
									 && frames > m_chatPerformanceWorkload.presentedFramesBeforeSeed);
			return response;
		} else if (command == QLatin1String("qmlPerformanceChatScrollRun")) {
			if (!m_chatPerformanceWorkload.active) return errorResponse(tr("No chat performance fixture is active."));
			if (m_chatPerformanceWorkload.running)
				return errorResponse(tr("The chat-scroll performance fixture is already running."));
			if (m_chatPerformanceWorkload.stepCount != 0)
				return errorResponse(tr("The chat-scroll performance fixture has already been consumed."));
			m_chatPerformanceWorkload.targetSteps =
				qBound(1, request.value(QStringLiteral("stepCount"), 20).toInt(), 240);
			QVariant started;
			if (!QMetaObject::invokeMethod(host->window(), "preparePerformanceChatScrollWorkload",
					Q_RETURN_ARG(QVariant, started),
					Q_ARG(QVariant, m_chatPerformanceWorkload.targetSteps))) {
				return errorResponse(tr("The Qt Quick root does not expose the chat-scroll workload."));
			}
			const QVariantMap startedState = started.toMap();
			if (!startedState.value(QStringLiteral("started")).toBool()) {
				return errorResponse(startedState.value(QStringLiteral("reason"),
					tr("The chat timeline is not scrollable.")).toString());
			}
			m_chatPerformanceWorkload.primed = false;
			m_chatPerformanceWorkload.running = true;
			m_chatPerformanceWorkload.failureReason.clear();
			m_chatPerformanceWorkload.frameConnection = QObject::connect(
				host->window(), &QQuickWindow::frameSwapped, this,
				[this]() { advanceChatPerformanceWorkload(); }, Qt::QueuedConnection);
			// Prime one presentation. The frame-driven continuation applies one
			// scroll input per subsequently requested scene-graph frame.
			host->window()->update();
			QVariantMap response = okResponse();
			response.insert(QStringLiteral("running"), true);
			response.insert(QStringLiteral("targetSteps"), m_chatPerformanceWorkload.targetSteps);
			response.insert(QStringLiteral("scroll"), startedState);
			return response;
		} else if (command == QLatin1String("qmlPerformanceChatScrollStatus")) {
			if (!m_chatPerformanceWorkload.active)
				return errorResponse(tr("No chat performance fixture is active."));
			QVariant state;
			if (!QMetaObject::invokeMethod(host->window(), "performanceChatScrollState", Q_RETURN_ARG(QVariant, state)))
				return errorResponse(tr("The Qt Quick root does not expose chat-scroll status."));
			const int frames = monitor->snapshot().value(QStringLiteral("presentedFrameCount")).toInt();
			QVariantMap response = okResponse();
			response.insert(QStringLiteral("scroll"), state.toMap());
			response.insert(QStringLiteral("stepCount"), m_chatPerformanceWorkload.stepCount);
			response.insert(QStringLiteral("targetSteps"), m_chatPerformanceWorkload.targetSteps);
			response.insert(QStringLiteral("running"), m_chatPerformanceWorkload.running);
			response.insert(QStringLiteral("primed"), m_chatPerformanceWorkload.primed);
			response.insert(QStringLiteral("failureReason"), m_chatPerformanceWorkload.failureReason);
			response.insert(QStringLiteral("presentedFramesBefore"),
				m_chatPerformanceWorkload.presentedFramesBeforeRun);
			response.insert(QStringLiteral("presentedFramesAfter"), frames);
			response.insert(QStringLiteral("presentedFrameDelta"),
				frames - m_chatPerformanceWorkload.presentedFramesBeforeRun);
			response.insert(QStringLiteral("performance"), monitor->snapshot());
			return response;
		} else if (command == QLatin1String("qmlPerformanceChatFinalize")) {
			return finalizeChatPerformanceWorkload(host);
		} else if (command == QLatin1String("qmlPerformanceTalkStart")) {
			if (m_talkPerformanceWorkload.active)
				return errorResponse(tr("A talk-state performance fixture is already active."));
			if (m_chatPerformanceWorkload.active)
				return errorResponse(tr("A chat performance fixture is already active."));
			ParticipantModel *participants = host->participantModel();
			m_talkPerformanceWorkload.liveParticipants = participants->participantStates();
			m_talkPerformanceWorkload.previousFixtureOverride = host->visualFixtureOverrideActive();
			m_talkPerformanceWorkload.sessionId = QStringLiteral("4294967001");
			m_talkPerformanceWorkload.presentedFramesBefore =
				monitor->snapshot().value(QStringLiteral("presentedFrameCount")).toInt();
			host->setVisualFixtureOverrideActive(true);
			m_talkPerformanceWorkload.active = true;
			host->setVisualFixtureMutationActive(true);
			participants->replaceParticipantStates(QVariantList { QVariantMap {
				{ QStringLiteral("session"), m_talkPerformanceWorkload.sessionId },
				{ QStringLiteral("name"), QStringLiteral("Performance participant") },
				{ QStringLiteral("statusLabel"), QStringLiteral("Listening") },
				{ QStringLiteral("talkState"), QStringLiteral("passive") },
				{ QStringLiteral("talking"), false }, { QStringLiteral("isSelf"), false },
				{ QStringLiteral("badges"), QVariantList() }, { QStringLiteral("statuses"), QVariantList() }
			} });
			host->setVisualFixtureMutationActive(false);
			host->window()->update();
			QVariantMap response = okResponse();
			response.insert(QStringLiteral("session"), m_talkPerformanceWorkload.sessionId);
			response.insert(QStringLiteral("presentedFramesBefore"), m_talkPerformanceWorkload.presentedFramesBefore);
			return response;
		} else if (command == QLatin1String("qmlPerformanceTalkRun")) {
			if (!m_talkPerformanceWorkload.active)
				return errorResponse(tr("No talk-state performance fixture is active."));
			if (m_talkPerformanceWorkload.running)
				return errorResponse(tr("The talk-state performance fixture is already running."));
			if (m_talkPerformanceWorkload.transitionCount != 0)
				return errorResponse(tr("The talk-state performance fixture has already been consumed."));
			m_talkPerformanceWorkload.targetTransitions =
				qBound(1, request.value(QStringLiteral("transitionCount"), 40).toInt(), 240);
			m_talkPerformanceWorkload.primed = false;
			m_talkPerformanceWorkload.running = true;
			m_talkPerformanceWorkload.frameConnection = QObject::connect(
				host->window(), &QQuickWindow::frameSwapped, this,
				[this]() { advanceTalkPerformanceWorkload(); }, Qt::QueuedConnection);
			// Prime one measured presentation before the first state change. The
			// frame-driven continuation then applies exactly one transition per frame.
			host->window()->update();
			QVariantMap response = okResponse();
			response.insert(QStringLiteral("running"), true);
			response.insert(QStringLiteral("targetTransitions"), m_talkPerformanceWorkload.targetTransitions);
			return response;
		} else if (command == QLatin1String("qmlPerformanceTalkTransition")) {
			if (!m_talkPerformanceWorkload.active)
				return errorResponse(tr("No talk-state performance fixture is active."));
			if (m_talkPerformanceWorkload.running)
				return errorResponse(tr("The frame-driven talk-state performance fixture is running."));
			m_talkPerformanceWorkload.talking = !m_talkPerformanceWorkload.talking;
			const bool talking = m_talkPerformanceWorkload.talking;
			const QString operationId = monitor->markInput(
				QStringLiteral("talk-state:%1").arg(++m_performanceInputSequence));
			{
				mumble::chatperf::ScopedDuration trace("qml.participant.talk_state_update");
				host->participantModel()->updatePresence(
					m_talkPerformanceWorkload.sessionId,
					talking ? QStringLiteral("talking") : QStringLiteral("passive"),
					talking ? tr("Talking") : tr("Listening"),
					talking ? QStringLiteral("accent") : QString(), talking, false, {}, {});
			}
			++m_talkPerformanceWorkload.transitionCount;
			host->window()->update();
			QVariantMap response = okResponse();
			response.insert(QStringLiteral("operationId"), operationId);
			response.insert(QStringLiteral("transitionCount"), m_talkPerformanceWorkload.transitionCount);
			response.insert(QStringLiteral("talking"), talking);
			return response;
		} else if (command == QLatin1String("qmlPerformanceTalkStatus")) {
			if (!m_talkPerformanceWorkload.active)
				return errorResponse(tr("No talk-state performance fixture is active."));
			const int frames = monitor->snapshot().value(QStringLiteral("presentedFrameCount")).toInt();
			QVariantMap response = okResponse();
			response.insert(QStringLiteral("transitionCount"), m_talkPerformanceWorkload.transitionCount);
			response.insert(QStringLiteral("presentedFramesBefore"), m_talkPerformanceWorkload.presentedFramesBefore);
			response.insert(QStringLiteral("presentedFramesAfter"), frames);
			response.insert(QStringLiteral("presentedFrameDelta"), frames - m_talkPerformanceWorkload.presentedFramesBefore);
			response.insert(QStringLiteral("talking"), m_talkPerformanceWorkload.talking);
			response.insert(QStringLiteral("running"), m_talkPerformanceWorkload.running);
			response.insert(QStringLiteral("primed"), m_talkPerformanceWorkload.primed);
			response.insert(QStringLiteral("targetTransitions"), m_talkPerformanceWorkload.targetTransitions);
			response.insert(QStringLiteral("performance"), monitor->snapshot());
			return response;
		} else if (command == QLatin1String("qmlPerformanceTalkFinalize")) {
			return finalizeTalkPerformanceWorkload(host);
		} else if (command == QLatin1String("qmlPerformanceEnd")) {
			monitor->endFrameSampling();
		} else if (command == QLatin1String("qmlPerformanceMarkInput")) {
			QVariantMap response = okResponse();
			response.insert(QStringLiteral("operationId"),
							monitor->markInput(request.value(QStringLiteral("operationId")).toString()));
			return response;
		} else if (command == QLatin1String("qmlPerformanceSelectScope")) {
			const QString scopeToken = request.value(QStringLiteral("scopeToken")).toString().trimmed();
			if (scopeToken.isEmpty()) return errorResponse(tr("Missing scopeToken."));
			const QString operationId = monitor->markInput(request.value(QStringLiteral("operationId")).toString());
			const bool handled = m_mainWindow->handleModernShellScopeSelection(scopeToken);
			if (!handled) {
				monitor->markVisualComplete(operationId);
				return errorResponse(tr("The measured room selection was not handled."));
			}
			QVariantMap response = okResponse();
			response.insert(QStringLiteral("operationId"), operationId);
			response.insert(QStringLiteral("handled"), true);
			response.insert(QStringLiteral("scopeToken"), scopeToken);
			return response;
		} else if (command == QLatin1String("qmlPerformanceMarkVisual")) {
			const QString operationId = request.value(QStringLiteral("operationId")).toString().trimmed();
			if (operationId.isEmpty()) return errorResponse(tr("Missing performance scenario operation ID."));
			monitor->markVisualComplete(operationId);
		} else if (command != QLatin1String("qmlPerformanceSnapshot")) {
			return errorResponse(tr("Unknown Qt Quick performance command."));
		}
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("performance"), monitor->snapshot());
		return response;
	}

	if (command == QLatin1String("qmlVisualGateCapabilities")) {
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("capabilities"), visualFixtureController()->capabilities());
		return response;
	}

	if (command == QLatin1String("setQmlVisualGateState")) {
		QString fixtureError;
		const QVariantMap applied = visualFixtureController()->apply(request, &fixtureError);
		if (applied.isEmpty()) return errorResponse(fixtureError);
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("applied"), applied);
		return response;
	}

	if (command == QLatin1String("qmlAccessibilitySnapshot")) {
		QmlVisualFixtureController *fixture = visualFixtureController();
		bool parsedGeneration = false;
		const qulonglong requestedGeneration =
			unsignedLongLongValue(request.value(QStringLiteral("generation")), &parsedGeneration);
		if (!parsedGeneration || requestedGeneration == 0 || requestedGeneration != fixture->generation()) {
			return errorResponse(tr("The requested visual fixture generation is stale or invalid."));
		}
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host || !host->window()) return errorResponse(tr("The Qt Quick frontend is not active."));
		const QString windowId = request.value(QStringLiteral("window"), QStringLiteral("main"))
			.toString().trimmed();
		QString targetError;
		QQuickWindow *targetWindow = host->captureWindowTarget(windowId, &targetError);
		if (!targetWindow || !targetWindow->isVisible() || !targetWindow->isExposed()) {
			return errorResponse(targetError.isEmpty()
				? tr("The requested Qt Quick window is not ready for accessibility capture.") : targetError);
		}
		QString focusError;
		if (!fixture->ensureFocus(windowId, &focusError)) return errorResponse(focusError);
		const QVariantMap snapshot = QmlAccessibilitySnapshot::serialize(targetWindow);
		if (!snapshot.value(QStringLiteral("ok")).toBool()) {
			return errorResponse(snapshot.value(QStringLiteral("error")).toString());
		}
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("generation"), requestedGeneration);
		response.insert(QStringLiteral("window"), windowId.isEmpty() ? QStringLiteral("main") : windowId);
		response.insert(QStringLiteral("snapshot"), snapshot.value(QStringLiteral("tree")));
		response.insert(QStringLiteral("nodeCount"), snapshot.value(QStringLiteral("nodeCount")));
		response.insert(QStringLiteral("truncated"), snapshot.value(QStringLiteral("truncated")));
		return response;
	}

	if (command == QLatin1String("qmlVisualGateRichPreviewState")) {
		QmlVisualFixtureController *fixture = visualFixtureController();
		bool parsedGeneration = false;
		const qulonglong requestedGeneration =
			unsignedLongLongValue(request.value(QStringLiteral("generation")), &parsedGeneration);
		if (!parsedGeneration || requestedGeneration == 0 || requestedGeneration != fixture->generation()) {
			return errorResponse(tr("The requested visual fixture generation is stale or invalid."));
		}
		const QString messageId = request.value(QStringLiteral("messageId")).toString().trimmed();
		if (messageId.isEmpty()) return errorResponse(tr("Missing rich-preview message id."));
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host || !host->window()) return errorResponse(tr("The Qt Quick frontend is not active."));
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("generation"), requestedGeneration);
		response.insert(QStringLiteral("frontend"), QStringLiteral("qml"));
		response.insert(QStringLiteral("card"), automationRichPreviewCardState(host->window(), messageId));
		return response;
	}

	if (command == QLatin1String("qmlVisualGateRichBodyState")) {
		QmlVisualFixtureController *fixture = visualFixtureController();
		bool parsedGeneration = false;
		const qulonglong requestedGeneration =
			unsignedLongLongValue(request.value(QStringLiteral("generation")), &parsedGeneration);
		if (!parsedGeneration || requestedGeneration == 0 || requestedGeneration != fixture->generation()) {
			return errorResponse(tr("The requested visual fixture generation is stale or invalid."));
		}
		const QString messageId = request.value(QStringLiteral("messageId")).toString().trimmed();
		if (messageId.isEmpty()) return errorResponse(tr("Missing rich-message body id."));
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host || !host->window()) return errorResponse(tr("The Qt Quick frontend is not active."));
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("generation"), requestedGeneration);
		response.insert(QStringLiteral("frontend"), QStringLiteral("qml"));
		response.insert(QStringLiteral("body"),
			automationRichMessageBodyState(host, host->window(), messageId));
		return response;
	}

	if (command == QLatin1String("setHostViewport")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host || !host->window()) return errorResponse(tr("The Qt Quick frontend is not active."));

		const int width  = request.value(QStringLiteral("width")).toInt();
		const int height = request.value(QStringLiteral("height")).toInt();
		if (width < 420 || width > 7680 || height < 520 || height > 4320) {
			return errorResponse(tr("The requested viewport size is outside the supported range."));
		}

		QQuickWindow *window = host->window();
		window->resize(width, height);
		if (request.contains(QStringLiteral("railOpen"))) {
			const QVariant open = request.value(QStringLiteral("railOpen"));
			if (!QMetaObject::invokeMethod(window, "setAutomationNavigationOpen", Q_ARG(QVariant, open))) {
				return errorResponse(tr("The Qt Quick navigation drawer is unavailable."));
			}
		}
		window->update();

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("width"), window->width());
		response.insert(QStringLiteral("height"), window->height());
		response.insert(QStringLiteral("railOpen"), window->property("automationNavigationOpen").toBool());
		response.insert(QStringLiteral("railPosition"),
						window->property("automationNavigationPosition").toDouble());
		return response;
	}

	if (command == QLatin1String("captureQml")) {
		if (!m_mainWindow->m_qmlShellHost || !m_mainWindow->m_qmlShellHost->window()) {
			return errorResponse(tr("The Qt Quick frontend is not active."));
		}
		const QString path = request.value(QStringLiteral("path")).toString().trimmed();
		const QString windowId = request.value(QStringLiteral("window"), QStringLiteral("main")).toString().trimmed();
		if (path.isEmpty()) return errorResponse(tr("Missing capture path."));
		bool parsedGeneration = false;
		const qulonglong requestedGeneration =
			unsignedLongLongValue(request.value(QStringLiteral("generation")), &parsedGeneration);
		if (request.contains(QStringLiteral("generation"))
			&& (!parsedGeneration || requestedGeneration == 0
				|| requestedGeneration != visualFixtureController()->generation())) {
			return errorResponse(tr("The requested visual fixture generation is stale or invalid."));
		}
		if (request.contains(QStringLiteral("generation"))) {
			QString focusError;
			if (!visualFixtureController()->ensureFocus(windowId, &focusError)) return errorResponse(focusError);
		}
		QString captureError;
		if (!m_mainWindow->m_qmlShellHost->captureWindow(path, &captureError, windowId)) {
			return errorResponse(captureError);
		}
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("path"), QFileInfo(path).absoluteFilePath());
		response.insert(QStringLiteral("frontend"), QStringLiteral("qml"));
		response.insert(QStringLiteral("window"), windowId.isEmpty() ? QStringLiteral("main") : windowId);
		if (parsedGeneration) response.insert(QStringLiteral("generation"), requestedGeneration);
		return response;
	}

	if (command == QLatin1String("setQmlPttTool")) {
		if (!m_mainWindow->m_qmlShellHost || !m_mainWindow->m_qmlShellHost->window()) {
			return errorResponse(tr("The Qt Quick frontend is not active."));
		}
		m_mainWindow->m_qmlShellHost->showPttTool(request.value(QStringLiteral("visible")).toBool());
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("visible"), m_mainWindow->m_qmlShellHost->pttToolVisible());
		response.insert(QStringLiteral("captureReady"),
			m_mainWindow->m_qmlShellHost->captureWindowReady(QStringLiteral("ptt")));
		return response;
	}

	if (command == QLatin1String("setQmlPttPressed")) {
		if (!m_mainWindow->m_qmlShellHost || !m_mainWindow->m_qmlShellHost->window()) {
			return errorResponse(tr("The Qt Quick frontend is not active."));
		}
		m_mainWindow->m_qmlShellHost->commandController()->setPttPressed(
			request.value(QStringLiteral("pressed")).toBool());
		return okResponse();
	}

	if (command == QLatin1String("setQmlManualPluginTool")) {
#ifdef USE_MANUAL_PLUGIN
		if (!m_mainWindow->m_qmlShellHost || !m_mainWindow->m_qmlShellHost->window()) {
			return errorResponse(tr("The Qt Quick frontend is not active."));
		}
		m_mainWindow->m_qmlShellHost->showManualPluginTool(request.value(QStringLiteral("visible"), true).toBool());
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("visible"), m_mainWindow->m_qmlShellHost->manualPluginToolVisible());
		response.insert(QStringLiteral("captureReady"),
			m_mainWindow->m_qmlShellHost->captureWindowReady(QStringLiteral("manual-plugin")));
		return response;
#else
		return errorResponse(tr("The Manual Plugin is not available in this build."));
#endif
	}

	if (command == QLatin1String("openQmlMediaSession")) {
		if (!m_mainWindow->m_qmlShellHost || !m_mainWindow->m_qmlShellHost->window()) {
			return errorResponse(tr("The Qt Quick frontend is not active."));
		}
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		MediaSessionBackend *media = host->mediaSession();
		const QString presentation = request.value(QStringLiteral("presentation")).toString().trimmed().toLower();
		if (!presentation.isEmpty() && presentation != QLatin1String("detached")
			&& presentation != QLatin1String("inline")) {
			return errorResponse(tr("Unknown media presentation: %1").arg(presentation));
		}

		const QUrl url(request.value(QStringLiteral("url")).toString());
		const QString provider = request.value(QStringLiteral("provider")).toString();
		const QString sessionId = request.value(QStringLiteral("sessionId")).toString();
		const QString mediaMime = request.value(QStringLiteral("mediaMime")).toString().trimmed();
		const QUrl audioUrl(request.value(QStringLiteral("audioUrl")).toString());
		const QString audioMime = request.value(QStringLiteral("audioMime")).toString().trimmed();
		const bool inlinePresentation = presentation == QLatin1String("inline");

		bool opened = false;
		if (!mediaMime.isEmpty()) {
			opened = inlinePresentation ? media->openDirectInline(url, mediaMime, audioUrl, audioMime, sessionId)
										: media->openDirect(url, mediaMime, audioUrl, audioMime, sessionId);
		} else {
			opened = inlinePresentation ? media->openInline(url, provider, sessionId)
										: media->open(url, provider, sessionId);
		}
		if (!opened) return errorResponse(media->error());

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("media"), automationMediaLifecycleState(host));
		return response;
	}

	if (command == QLatin1String("closeQmlMediaSession")) {
		if (m_mainWindow->m_qmlShellHost) m_mainWindow->m_qmlShellHost->mediaSession()->close();
		return okResponse();
	}

	if (command == QLatin1String("watchTogetherState")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("media"), automationMediaLifecycleState(host));
		return response;
	}

	if (command == QLatin1String("startWatchTogether")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));
		const QUrl url(request.value(QStringLiteral("url")).toString().trimmed());
		const QString provider = request.value(QStringLiteral("provider")).toString().trimmed();
		const QString title = request.value(QStringLiteral("title")).toString().trimmed();
		if (url.isEmpty()) return errorResponse(tr("Missing watch-together URL."));

		MediaSessionBackend *media = host->mediaSession();
		if (!media->startShared(url, provider, title)) return errorResponse(media->error());
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), true);
		response.insert(QStringLiteral("media"), automationMediaLifecycleState(host));
		return response;
	}

	if (command == QLatin1String("watchTogetherAction")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));
		MediaSessionBackend *media = host->mediaSession();
		QString action = request.value(QStringLiteral("action")).toString().trimmed().toLower();
		action.replace(QLatin1Char('_'), QLatin1Char('-'));
		bool handled = true;

		if (action == QLatin1String("join")) {
			if (!media->sharedAvailable() || media->sharedJoined()) handled = false;
			else media->joinShared();
		} else if (action == QLatin1String("leave")) {
			if (!media->sharedJoined()) handled = false;
			else media->leaveShared();
		} else if (action == QLatin1String("end")) {
			if (!media->sharedHost()) handled = false;
			else media->endShared();
		} else if (action == QLatin1String("transfer-host")) {
			const QString sessionId = request.value(QStringLiteral("sessionId")).toString().trimmed();
			bool validSession = false;
			const qulonglong targetSession = sessionId.toULongLong(&validSession);
			if (!media->sharedHost() || !validSession || targetSession == 0
				|| targetSession > std::numeric_limits< unsigned int >::max()
				|| targetSession == media->sharedHostSession()) {
				handled = false;
			} else {
				media->transferSharedHost(sessionId);
			}
		} else if (action == QLatin1String("reopen") || action == QLatin1String("reopen-player")) {
			handled = media->reopenSharedPlayer();
		} else if (action == QLatin1String("retry")) {
			if (!media->active()) handled = false;
			else media->retry();
		} else if (action == QLatin1String("play")) {
			if (!media->active() || !media->playbackControlAllowed()) handled = false;
			else media->play();
		} else if (action == QLatin1String("pause")) {
			if (!media->active() || !media->playbackControlAllowed()) handled = false;
			else media->pause();
		} else if (action == QLatin1String("seek")) {
			bool validPosition = false;
			const double position = request.value(QStringLiteral("position")).toDouble(&validPosition);
			if (!media->active() || !media->playbackControlAllowed() || !validPosition
				|| !std::isfinite(position) || position < 0.0) {
				handled = false;
			} else {
				media->seek(position);
			}
		} else if (action == QLatin1String("detach")) {
			if (!media->active() || media->detached()) handled = false;
			else media->detach();
		} else if (action == QLatin1String("attach")) {
			if (!media->active() || !media->detached()) handled = false;
			else media->attach();
		} else if (action == QLatin1String("close-player")) {
			if (!media->active()) handled = false;
			else media->closePlayer();
		} else if (action == QLatin1String("close")) {
			if (!media->active() && !media->sharedAvailable()) handled = false;
			else media->close();
		} else {
			return errorResponse(tr("Unknown watch-together action: %1").arg(action));
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), handled);
		response.insert(QStringLiteral("media"), automationMediaLifecycleState(host));
		return response;
	}

	if (command == QLatin1String("qmlReadinessState")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host || !host->window()) return errorResponse(tr("The Qt Quick frontend is not active."));
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("frontend"), QStringLiteral("qml"));
		response.insert(QStringLiteral("windowReady"), true);
		response.insert(QStringLiteral("windowVisible"), host->window()->isVisible());
		response.insert(QStringLiteral("windowExposed"), host->window()->isExposed());
		response.insert(QStringLiteral("windowVisibility"), static_cast< int >(host->window()->visibility()));
		response.insert(QStringLiteral("connected"), host->sessionController()->connected());
		response.insert(QStringLiteral("activeScopeToken"), host->activeScopeController()->scopeToken());
		response.insert(QStringLiteral("roomCount"), host->roomModel()->rowCount());
		response.insert(QStringLiteral("participantCount"), host->participantModel()->rowCount());
		response.insert(QStringLiteral("messageCount"), host->chatModel()->rowCount());
		response.insert(QStringLiteral("dialogOpen"), host->dialogController()->open());
		response.insert(QStringLiteral("pttPressed"), host->commandController()->pttPressed());
		response.insert(QStringLiteral("pttToolVisible"), host->pttToolVisible());
		response.insert(QStringLiteral("mainCaptureReady"), host->captureWindowReady(QStringLiteral("main")));
		response.insert(QStringLiteral("pttToolCaptureReady"), host->captureWindowReady(QStringLiteral("ptt")));
#ifdef USE_MANUAL_PLUGIN
		response.insert(QStringLiteral("manualPluginToolVisible"), host->manualPluginToolVisible());
		response.insert(QStringLiteral("manualPluginToolCaptureReady"),
			host->captureWindowReady(QStringLiteral("manual-plugin")));
#endif
		response.insert(QStringLiteral("mediaActive"), host->mediaSession()->active());
		response.insert(QStringLiteral("media"), automationMediaLifecycleState(host));
		return response;
	}

	if (command == QLatin1String("qmlTimelinePresentationState")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host || !host->window()) return errorResponse(tr("The Qt Quick frontend is not active."));
		QVariant qmlState;
		if (!QMetaObject::invokeMethod(host->window(), "timelinePresentationState",
				Q_RETURN_ARG(QVariant, qmlState))) {
			return errorResponse(tr("The chat timeline presentation state is unavailable."));
		}
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("timeline"), qmlState.toMap());
		return response;
	}

	if (command == QLatin1String("pttLifecycleProbe")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));
		UiCommandController *commands = host->commandController();
		commands->setPttPressed(true);
		const bool pressed = commands->pttPressed();
		commands->releasePtt();
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("pressedObserved"), pressed);
		response.insert(QStringLiteral("released"), !commands->pttPressed());
		return response;
	}

	if (command == QLatin1String("watchTogetherLifecycleProbe")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));

		// Exercise the typed backend in isolation. The allowlisted embed is never attached
		// to a QML loader, so this probe cannot create a renderer or perform network I/O.
		MediaSessionBackend media;
		const QUrl url(QStringLiteral("https://www.youtube-nocookie.com/embed/automation-lifecycle"));
		const QString provider = QStringLiteral("youtube");
		const QString sessionID = QStringLiteral("automation-room");
		const qulonglong baselineGeneration = media.syncGeneration();
		if (!media.openInline(url, provider, sessionID)) return errorResponse(media.error());
		const qulonglong openedGeneration = media.syncGeneration();
		const bool opened = media.active();
		const bool inlinePresentation = !media.detached();
		const QString canonicalProvider = media.provider();
		media.applyRemoteState(url, provider, sessionID, 12.5, false, openedGeneration + 1);
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("opened"), opened);
		response.insert(QStringLiteral("inlinePresentation"), inlinePresentation);
		response.insert(QStringLiteral("rendererActivated"), false);
		response.insert(QStringLiteral("provider"), canonicalProvider);
		response.insert(QStringLiteral("url"), media.url());
		response.insert(QStringLiteral("remoteApplied"), media.state() == QLatin1String("playing"));
		response.insert(QStringLiteral("state"), media.state());
		response.insert(QStringLiteral("position"), media.position());
		response.insert(QStringLiteral("generationAdvanced"),
						 media.syncGeneration() > openedGeneration && openedGeneration > baselineGeneration);
		media.close();
		response.insert(QStringLiteral("closed"), !media.active());
		response.insert(QStringLiteral("closedState"), media.state());
		return response;
	}

	if (command == QLatin1String("attachmentModelProbe")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));
		ChatTimelineModel *chat = host->chatModel();
		const QString key = QStringLiteral("automation:attachment");
		const QVariantMap attachment { { QStringLiteral("id"), QStringLiteral("asset:1") },
								   { QStringLiteral("name"), QStringLiteral("automation.png") },
								   { QStringLiteral("mime"), QStringLiteral("image/png") },
								   { QStringLiteral("size"), 128 } };
		const bool applied = chat->upsertMessage(
			{ { QStringLiteral("messageKey"), key }, { QStringLiteral("actor"), QStringLiteral("Automation") },
			  { QStringLiteral("bodyText"), QStringLiteral("Attachment lifecycle probe") },
			  { QStringLiteral("attachments"), QVariantList { attachment } } });
		QVariantMap source;
		for (int row = 0; row < chat->rowCount(); ++row) {
			if (chat->get(row).value(QStringLiteral("id")).toString() == key)
				source = chat->get(row).value(QStringLiteral("source")).toMap();
		}
		chat->removeRow(key);
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("applied"), applied);
		response.insert(QStringLiteral("attachmentCount"), source.value(QStringLiteral("attachments")).toList().size());
		response.insert(QStringLiteral("removed"), true);
		return response;
	}

	if (command == QLatin1String("pluginOperationMatrixProbe")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));
		AsyncOperationModel *operations = host->operationModel();
		operations->clear();
		const QList< QPair< QString, QString > > failures {
			{ QStringLiteral("incompatible"), QStringLiteral("incompatible-package") },
			{ QStringLiteral("manifest"), QStringLiteral("broken-manifest") },
			{ QStringLiteral("overwrite"), QStringLiteral("overwrite-denied") },
			{ QStringLiteral("load"), QStringLiteral("load-failed") },
			{ QStringLiteral("network"), QStringLiteral("network-error") },
			{ QStringLiteral("cancelled"), QStringLiteral("cancelled") }
		};
		for (const auto &failure : failures) {
			const QString id = QStringLiteral("plugin-probe:%1").arg(failure.first);
			operations->startOperation(id, failure.first, tr("Plugin operation probe"), true);
			operations->finishOperation(id, false, failure.second, failure.second);
		}
		operations->startOperation(QStringLiteral("plugin-probe:partial-ok"), tr("Updated plugin"), QString(), false);
		operations->finishOperation(QStringLiteral("plugin-probe:partial-ok"), true, QString(), tr("Updated"));
		QVariantList results;
		for (int row = 0; row < operations->rowCount(); ++row) results.push_back(operations->get(row));
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("results"), results);
		response.insert(QStringLiteral("failureCount"), failures.size());
		response.insert(QStringLiteral("partialSuccess"), true);
		return response;
	}

	const bool async = request.value(QStringLiteral("async"), true).toBool();
	const auto scheduleAction = [this](auto action) {
		QPointer< MainWindow > guardedWindow(m_mainWindow);
		QTimer::singleShot(0, m_mainWindow, [guardedWindow, action]() {
			if (guardedWindow) {
				action(guardedWindow);
			}
		});
	};
	const auto asyncResponse = []() {
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("scheduled"), true);
		return response;
	};

	if (command == QLatin1String("selectScope")) {
		const QString scopeToken = request.value(QStringLiteral("scopeToken")).toString().trimmed();
		if (scopeToken.isEmpty()) {
			return errorResponse(tr("Missing scopeToken."));
		}
		const QString railKind = request.value(QStringLiteral("railKind")).toString().trimmed();

		if (async) {
			scheduleAction([scopeToken, railKind](MainWindow *window) {
				if (railKind.isEmpty()) {
					window->handleModernShellScopeSelection(scopeToken);
				} else {
					window->handleModernShellScopeRailSelection(scopeToken, railKind);
				}
			});
			return asyncResponse();
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"),
						railKind.isEmpty() ? m_mainWindow->handleModernShellScopeSelection(scopeToken)
										   : m_mainWindow->handleModernShellScopeRailSelection(scopeToken, railKind));
		return response;
	}

	if (command == QLatin1String("joinVoice")) {
		const QString scopeToken = request.value(QStringLiteral("scopeToken")).toString().trimmed();
		if (scopeToken.isEmpty()) {
			return errorResponse(tr("Missing scopeToken."));
		}

		if (async) {
			scheduleAction([scopeToken](MainWindow *window) { window->handleModernShellVoiceJoin(scopeToken); });
			return asyncResponse();
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), m_mainWindow->handleModernShellVoiceJoin(scopeToken));
		return response;
	}

	if (command == QLatin1String("selectParticipant") || command == QLatin1String("openDirectMessage")) {
		bool validSession = false;
		const unsigned int session = request.value(QStringLiteral("sessionId")).toString().toUInt(&validSession);
		if (!validSession || session == 0) return errorResponse(tr("Missing or invalid sessionId."));
		const bool openConversation = command == QLatin1String("openDirectMessage");
		if (async) {
			scheduleAction([session, openConversation](MainWindow *window) {
				window->handleModernShellParticipantSelection(session, openConversation);
			});
			return asyncResponse();
		}
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"),
						m_mainWindow->handleModernShellParticipantSelection(session, openConversation));
		return response;
	}

	if (command == QLatin1String("directMessageState")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));
		QVariantMap directMessageState = automationDirectMessageState(host->directMessageController());
		directMessageState.insert(QStringLiteral("windowCaptureReady"),
			host->captureWindowReady(QStringLiteral("direct-message")));
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("directMessages"), directMessageState);
		return response;
	}

	if (command == QLatin1String("sendDirectMessage")) {
		const QString message = request.value(QStringLiteral("message")).toString();
		if (message.trimmed().isEmpty()) return errorResponse(tr("Missing message."));
		const QString requestedSession = request.value(QStringLiteral("sessionId")).toString().trimmed();
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));
		DirectMessageController *directMessages = host->directMessageController();
		if (!directMessages->conversationOpen())
			return errorResponse(tr("Open a direct-message conversation before sending."));
		if (!requestedSession.isEmpty() && requestedSession != directMessages->activeSessionId())
			return errorResponse(tr("The requested direct-message conversation is not active."));
		if (!directMessages->canSend())
			return errorResponse(tr("The active direct-message conversation cannot send messages."));
		directMessages->setDraft(message);
		directMessages->sendDraft();
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), true);
		response.insert(QStringLiteral("sessionId"), directMessages->activeSessionId());
		return response;
	}

	if (command == QLatin1String("closeDirectMessage")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));
		DirectMessageController *directMessages = host->directMessageController();
		const bool handled = directMessages->conversationOpen();
		if (handled) directMessages->closeConversation();
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), handled);
		return response;
	}

	if (command == QLatin1String("setDirectMessageMode")) {
		const QString mode = request.value(QStringLiteral("mode")).toString().trimmed().toLower();
		if (mode != QLatin1String("history") && mode != QLatin1String("private"))
			return errorResponse(tr("Direct-message mode must be history or private."));
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));
		DirectMessageController *directMessages = host->directMessageController();
		if (!directMessages->conversationOpen())
			return errorResponse(tr("Open a direct-message conversation before changing its mode."));
		directMessages->setMode(mode);
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), true);
		return response;
	}

	if (command == QLatin1String("markDirectMessageRead")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));
		DirectMessageController *directMessages = host->directMessageController();
		const QString session = request.value(QStringLiteral("sessionId"), directMessages->activeSessionId())
			.toString().trimmed();
		if (session.isEmpty()) return errorResponse(tr("Missing direct-message sessionId."));
		directMessages->markRead(session);
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), true);
		return response;
	}

	if (command == QLatin1String("requestPreviewHydration")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));
		QString scopeToken = request.value(QStringLiteral("scopeToken")).toString().trimmed();
		if (scopeToken.isEmpty()) scopeToken = host->activeScopeController()->scopeToken();
		if (scopeToken.isEmpty()) return errorResponse(tr("Missing preview hydration scopeToken."));

		QVariantList requestedIds = request.value(QStringLiteral("messageIds")).toList();
		if (requestedIds.isEmpty() && request.contains(QStringLiteral("messageId"))) {
			requestedIds.push_back(request.value(QStringLiteral("messageId")));
		}
		QVariantList messageIds;
		QSet< qulonglong > seenIds;
		for (const QVariant &requestedId : requestedIds) {
			bool validId = false;
			const qulonglong messageId = unsignedLongLongValue(requestedId, &validId);
			if (!validId || messageId == 0 || seenIds.contains(messageId)) continue;
			seenIds.insert(messageId);
			messageIds.push_back(QVariant::fromValue(messageId));
		}
		if (messageIds.isEmpty()) return errorResponse(tr("Missing valid preview messageIds."));

		const bool highPriority = request.value(QStringLiteral("highPriority"), true).toBool();
		host->commandController()->requestPreviewHydration(scopeToken, messageIds, highPriority);
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), true);
		response.insert(QStringLiteral("scopeToken"), scopeToken);
		response.insert(QStringLiteral("messageIds"), messageIds);
		response.insert(QStringLiteral("highPriority"), highPriority);
		return response;
	}

	if (command == QLatin1String("richPreviewState")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host) return errorResponse(tr("The Qt Quick frontend is not active."));
		const QString messageId = request.value(QStringLiteral("messageId")).toString().trimmed();
		if (messageId.isEmpty()) return errorResponse(tr("Missing preview messageId."));
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("richPreview"), automationLiveRichPreviewState(host, messageId));
		return response;
	}

	if (command == QLatin1String("invokeRichPreviewAction")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host || !host->window()) return errorResponse(tr("The Qt Quick frontend is not active."));
		const QString messageId = request.value(QStringLiteral("messageId")).toString().trimmed();
		QString action = request.value(QStringLiteral("action")).toString().trimmed().toLower();
		action.replace(QLatin1Char('_'), QLatin1Char('-'));
		if (messageId.isEmpty()) return errorResponse(tr("Missing preview messageId."));
		if (action.isEmpty()) return errorResponse(tr("Missing preview action."));
		QObject *card = automationFindRichPreviewCard(host->window(), messageId);
		if (!card) return errorResponse(tr("The requested preview card is not currently rendered."));

		bool handled = false;
		if (action == QLatin1String("inline") || action == QLatin1String("play-inline")) {
			const char *method = card->property("hasEmbedPreview").toBool()
				? "requestInlinePlaybackWithFocus" : "requestCurrentMediaWithFocus";
			handled = QMetaObject::invokeMethod(card, method, Qt::DirectConnection);
		} else if (action == QLatin1String("popout") || action == QLatin1String("open-popout")) {
			if (card->property("hasEmbedPreview").toBool()) {
				const QString url = card->property("safeEmbedUrl").toString();
				const QString provider = card->property("safeEmbedProvider").toString();
				handled = !url.isEmpty() && !provider.isEmpty()
					&& QMetaObject::invokeMethod(card, "popoutPlayRequested", Qt::DirectConnection,
						Q_ARG(QString, url), Q_ARG(QString, provider));
			} else {
				handled = QMetaObject::invokeMethod(card, "requestCurrentDirectMediaPopout", Qt::DirectConnection);
			}
		} else if (action == QLatin1String("watch-together")) {
			const QString url = card->property("safeEmbedUrl").toString();
			const QString provider = card->property("safeEmbedProvider").toString();
			const QString title = card->property("displayTitle").toString();
			handled = card->property("sharedPlaybackSupported").toBool()
				&& card->property("watchTogetherAvailable").toBool()
				&& QMetaObject::invokeMethod(card, "watchTogetherRequested", Qt::DirectConnection,
					Q_ARG(QString, url), Q_ARG(QString, provider), Q_ARG(QString, title));
		} else if (action == QLatin1String("open") || action == QLatin1String("open-external")) {
			const QString url = card->property("originalProviderUrl").toString();
			const QUrl parsedUrl(url);
			handled = parsedUrl.isValid() && parsedUrl.scheme() == QLatin1String("https")
				&& parsedUrl.userInfo().isEmpty()
				&& QMetaObject::invokeMethod(card, "externalOpenRequested", Qt::DirectConnection,
					Q_ARG(QString, url));
		} else if (action == QLatin1String("reveal")) {
			handled = card->property("mediaRequiresReveal").toBool()
				&& card->setProperty("sensitiveMediaRevealed", true);
		} else if (action == QLatin1String("expand")) {
			handled = card->setProperty("userExpanded", true);
		} else if (action == QLatin1String("collapse")) {
			handled = card->setProperty("userExpanded", false);
		} else if (action == QLatin1String("size-compact") || action == QLatin1String("size-default")
			|| action == QLatin1String("size-large")) {
			const QVariant preset(action.mid(5));
			handled = QMetaObject::invokeMethod(card, "setSizePreset", Qt::DirectConnection,
				Q_ARG(QVariant, preset));
		} else {
			return errorResponse(tr("Unknown preview action: %1").arg(action));
		}
		if (!handled) return errorResponse(tr("The requested preview action is not available."));

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), true);
		response.insert(QStringLiteral("richPreview"), automationLiveRichPreviewState(host, messageId));
		response.insert(QStringLiteral("media"), automationMediaLifecycleState(host));
		return response;
	}

	if (command == QLatin1String("screenShareViewerState")) {
		const QString streamId = request.value(QStringLiteral("streamId")).toString().trimmed();
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("viewer"),
			automationScreenShareViewerState(automationScreenShareViewer(streamId)));
		response.insert(QStringLiteral("viewers"), automationScreenShareViewerStates());
		return response;
	}

	if (command == QLatin1String("screenShareViewerAction")) {
		const QString streamId = request.value(QStringLiteral("streamId")).toString().trimmed();
		QString action = request.value(QStringLiteral("action")).toString().trimmed().toLower();
		action.replace(QLatin1Char('_'), QLatin1Char('-'));
		if (action.isEmpty()) return errorResponse(tr("Missing screen-share viewer action."));
		const AutomationScreenShareViewer viewer = automationScreenShareViewer(streamId);
		QPointer< ScreenShareViewBackend > backend = viewer.backend;
		QPointer< QQuickWindow > window = viewer.window;
		if (!backend || !window) return errorResponse(tr("The requested screen-share viewer is not open."));

		if (action == QLatin1String("pause")) {
			backend->setPaused(true);
		} else if (action == QLatin1String("resume")) {
			backend->setPaused(false);
		} else if (action == QLatin1String("set-paused")) {
			backend->setPaused(request.value(QStringLiteral("paused")).toBool());
		} else if (action == QLatin1String("mute")) {
			if (!backend->audioAvailable()) return errorResponse(tr("This screen share has no audio stream."));
			backend->setAudioMuted(true);
		} else if (action == QLatin1String("unmute")) {
			if (!backend->audioAvailable()) return errorResponse(tr("This screen share has no audio stream."));
			backend->setAudioMuted(false);
		} else if (action == QLatin1String("set-muted")) {
			if (!backend->audioAvailable()) return errorResponse(tr("This screen share has no audio stream."));
			backend->setAudioMuted(request.value(QStringLiteral("muted")).toBool());
		} else if (action == QLatin1String("set-volume")) {
			bool validVolume = false;
			const int volume = request.value(QStringLiteral("volume")).toInt(&validVolume);
			if (!backend->audioAvailable()) return errorResponse(tr("This screen share has no audio stream."));
			if (!validVolume || volume < 0 || volume > 100)
				return errorResponse(tr("Screen-share volume must be between 0 and 100."));
			backend->setAudioVolume(volume);
		} else if (action == QLatin1String("retry")) {
			backend->requestRetry();
		} else if (action == QLatin1String("stop")) {
			backend->requestStop();
		} else if (action == QLatin1String("close")) {
			backend->requestClose();
		} else if (action == QLatin1String("fullscreen")) {
			window->showFullScreen();
		} else if (action == QLatin1String("restore")) {
			window->showNormal();
		} else {
			return errorResponse(tr("Unknown screen-share viewer action: %1").arg(action));
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), true);
		response.insert(QStringLiteral("streamId"), streamId.isEmpty() && backend ? backend->streamId() : streamId);
		response.insert(QStringLiteral("viewer"),
			automationScreenShareViewerState(automationScreenShareViewer(streamId)));
		response.insert(QStringLiteral("viewers"), automationScreenShareViewerStates());
		return response;
	}

	if (command == QLatin1String("invokeAppAction")) {
		const QString actionID = request.value(QStringLiteral("actionId")).toString().trimmed();
		if (actionID.isEmpty()) {
			return errorResponse(tr("Missing actionId."));
		}

		if (async) {
			scheduleAction([actionID](MainWindow *window) { window->handleModernShellAppAction(actionID); });
			return asyncResponse();
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), m_mainWindow->handleModernShellAppAction(actionID));
		return response;
	}

	if (command == QLatin1String("invokeAppActionPayload")) {
		const QString actionID = request.value(QStringLiteral("actionId")).toString().trimmed();
		const QVariantMap payload = request.value(QStringLiteral("payload")).toMap();
		if (actionID.isEmpty()) {
			return errorResponse(tr("Missing actionId."));
		}

		if (async) {
			scheduleAction([actionID, payload](MainWindow *window) {
				window->handleModernShellAppActionPayload(actionID, payload);
			});
			return asyncResponse();
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), m_mainWindow->handleModernShellAppActionPayload(actionID, payload));
		return response;
	}

	if (command == QLatin1String("runMotdUiProbe")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host || !host->window()) {
			return errorResponse(tr("The Qt Quick frontend is not active."));
		}

		const QString action = request.value(QStringLiteral("action")).toString().trimmed().toLower();
		const QString signature = request.value(QStringLiteral("signature")).toString().trimmed();
		if (action.isEmpty()) {
			return errorResponse(tr("Missing MOTD action."));
		}

		const auto invokeProbe = [action, signature](MainWindow *window) {
			QmlShellHost *qmlHost = window ? window->qmlShellHost() : nullptr;
			if (!qmlHost || !qmlHost->window()) return QVariantMap();
			QVariant result;
			const bool invoked = QMetaObject::invokeMethod(
				qmlHost->window(), "runMotdUiProbe", Q_RETURN_ARG(QVariant, result),
				Q_ARG(QVariant, QVariant::fromValue(action)),
				Q_ARG(QVariant, QVariant::fromValue(signature)));
			return invoked ? result.toMap() : QVariantMap();
		};

		if (async) {
			scheduleAction([invokeProbe](MainWindow *window) { invokeProbe(window); });
			return asyncResponse();
		}

		const QVariantMap result = invokeProbe(m_mainWindow);
		if (result.isEmpty()) {
			return errorResponse(tr("The Qt Quick root does not expose the MOTD automation probe."));
		}
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("result"), result);
		response.insert(QStringLiteral("handled"), result.value(QStringLiteral("handled")).toBool());
		response.insert(QStringLiteral("action"), result.value(QStringLiteral("action"), action));
		response.insert(QStringLiteral("visible"), result.value(QStringLiteral("visible")).toBool());
		return response;
	}

	if (command == QLatin1String("openMenuProbe")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host || !host->window()) {
			return errorResponse(tr("The Qt Quick frontend is not active."));
		}

		const QString requestedVariant = request.value(QStringLiteral("variant")).toString().trimmed();
		const QString normalizedVariant = requestedVariant.toLower();
		static const QSet< QString > knownVariants {
			QStringLiteral("app"), QStringLiteral("self"), QStringLiteral("profile"),
			QStringLiteral("room"), QStringLiteral("member"), QStringLiteral("participant"),
			QStringLiteral("message"), QStringLiteral("textroom"),
			QStringLiteral("textroomreal"), QStringLiteral("chat"), QStringLiteral("background"),
			QStringLiteral("chatbackground")
		};
		if (!knownVariants.contains(normalizedVariant)) {
			return errorResponse(tr("Unknown menu probe '%1'.").arg(requestedVariant));
		}
		QString variant = normalizedVariant;
		if (variant == QLatin1String("textroom")) variant = QStringLiteral("textRoom");
		else if (variant == QLatin1String("textroomreal")) variant = QStringLiteral("textRoomReal");

		const auto invokeProbe = [variant](MainWindow *window) {
			QmlShellHost *qmlHost = window ? window->qmlShellHost() : nullptr;
			if (!qmlHost || !qmlHost->window()) return QVariantMap();
			QVariant result;
			const bool invoked = QMetaObject::invokeMethod(
				qmlHost->window(), "openAutomationMenuProbe", Q_RETURN_ARG(QVariant, result),
				Q_ARG(QVariant, QVariant::fromValue(variant)));
			return invoked ? result.toMap() : QVariantMap();
		};

		if (async) {
			scheduleAction([invokeProbe](MainWindow *window) { invokeProbe(window); });
			QVariantMap response = asyncResponse();
			response.insert(QStringLiteral("variant"), variant);
			return response;
		}

		const QVariantMap result = invokeProbe(m_mainWindow);
		if (result.isEmpty()) {
			return errorResponse(tr("The Qt Quick root does not expose the menu automation probe."));
		}
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("result"), result);
		response.insert(QStringLiteral("handled"), result.value(QStringLiteral("handled")).toBool());
		response.insert(QStringLiteral("variant"), result.value(QStringLiteral("variant"), variant));
		response.insert(QStringLiteral("open"), result.value(QStringLiteral("open")).toBool());
		response.insert(QStringLiteral("visible"), result.value(QStringLiteral("visible")).toBool());
		response.insert(QStringLiteral("surfaceId"), result.value(QStringLiteral("surfaceId")).toString());
		response.insert(QStringLiteral("objectName"), result.value(QStringLiteral("objectName")).toString());
		response.insert(QStringLiteral("captureRect"), result.value(QStringLiteral("captureRect")).toMap());
		response.insert(QStringLiteral("viewportWidth"), result.value(QStringLiteral("viewportWidth")).toInt());
		response.insert(QStringLiteral("viewportHeight"), result.value(QStringLiteral("viewportHeight")).toInt());
		response.insert(QStringLiteral("labels"), result.value(QStringLiteral("labels")).toList());
		response.insert(QStringLiteral("fixtureUsed"), result.value(QStringLiteral("fixtureUsed")).toBool());
		return response;
	}


	if (command == QLatin1String("invokeScopeAction")) {
		const QString scopeToken = request.value(QStringLiteral("scopeToken")).toString().trimmed();
		const QString actionID   = request.value(QStringLiteral("actionId")).toString().trimmed();
		if (scopeToken.isEmpty() || actionID.isEmpty()) {
			return errorResponse(tr("Missing scopeToken or actionId."));
		}

		if (async) {
			scheduleAction([scopeToken, actionID](MainWindow *window) {
				window->handleModernShellScopeAction(scopeToken, actionID);
			});
			return asyncResponse();
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), m_mainWindow->handleModernShellScopeAction(scopeToken, actionID));
		return response;
	}

	if (command == QLatin1String("invokeParticipantAction")) {
		bool parsedSession = false;
		const qulonglong session =
			unsignedLongLongValue(request.value(QStringLiteral("session")), &parsedSession);
		const QString actionID = request.value(QStringLiteral("actionId")).toString().trimmed();
		if (!parsedSession || session == 0 || actionID.isEmpty()) {
			return errorResponse(tr("Missing session or actionId."));
		}

		if (async) {
			scheduleAction([session, actionID](MainWindow *window) {
				window->handleModernShellParticipantAction(session, actionID);
			});
			return asyncResponse();
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), m_mainWindow->handleModernShellParticipantAction(session, actionID));
		return response;
	}

	if (command == QLatin1String("openDialog")) {
		const QString dialogID  = request.value(QStringLiteral("dialogId")).toString().trimmed();
		const QVariantMap context = request.value(QStringLiteral("context")).toMap();
		if (dialogID.isEmpty()) {
			return errorResponse(tr("Missing dialogId."));
		}

		m_automationDialogDraftDialogId.clear();
		m_automationDialogDraftFieldId.clear();
		m_automationDialogDraftValue.clear();
		m_automationDialogDraftActive = false;
		if (async) {
			scheduleAction([dialogID, context](MainWindow *window) { window->handleModernDialogOpen(dialogID, context); });
			return asyncResponse();
		}

		m_mainWindow->handleModernDialogOpen(dialogID, context);
		return okResponse();
	}

	if (command == QLatin1String("closeDialog")) {
		const QString dialogID = request.value(QStringLiteral("dialogId")).toString();
		m_automationDialogDraftDialogId.clear();
		m_automationDialogDraftFieldId.clear();
		m_automationDialogDraftValue.clear();
		m_automationDialogDraftActive = false;
		if (async) {
			scheduleAction([dialogID](MainWindow *window) { window->handleModernDialogClose(dialogID); });
			return asyncResponse();
		}

		m_mainWindow->handleModernDialogClose(dialogID);
		return okResponse();
	}

	if (command == QLatin1String("openMuteCueNoticeProbe")) {
		const auto openProbe = [](MainWindow *window) {
			const bool previousShown      = Global::get().s.muteCueShown;
			const bool previousInConfigUI = Global::get().inConfigUI;
			Global::get().s.muteCueShown  = false;
			Global::get().inConfigUI      = false;
			window->showMuteCuePopup();
			Global::get().s.muteCueShown = previousShown;
			Global::get().inConfigUI     = previousInConfigUI;
		};

		if (async) {
			scheduleAction(openProbe);
			return asyncResponse();
		}

		openProbe(m_mainWindow);
		return okResponse();
	}


	if (command == QLatin1String("modernDialogFieldState")) {
		const QString fieldID = request.value(QStringLiteral("fieldId")).toString().trimmed();
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("field"), buildAutomationDialogFieldState(fieldID));
		return response;
	}

	if (command == QLatin1String("modernDialogDomState")) {
		const QVariantMap state = m_mainWindow->m_modernDialogController ? m_mainWindow->m_modernDialogController->state() : QVariantMap();
		QVariantMap result; result.insert(QStringLiteral("exists"), state.value(QStringLiteral("open")));
		result.insert(QStringLiteral("ready"), state.value(QStringLiteral("open")));
		result.insert(QStringLiteral("title"), state.value(QStringLiteral("title")));
		result.insert(QStringLiteral("eyebrow"), state.value(QStringLiteral("eyebrow")));
		result.insert(QStringLiteral("subtitle"), state.value(QStringLiteral("subtitle")));
		result.insert(QStringLiteral("state"), state);
		QVariantMap response = okResponse(); response.insert(QStringLiteral("state"), result); return response;
	}

	if (command == QLatin1String("setModernDialogFieldValue")) {
		const QString fieldID = request.value(QStringLiteral("fieldId")).toString().trimmed();
		if (fieldID.isEmpty() || !m_mainWindow->m_modernDialogController) return errorResponse(tr("Dialog field is not available."));
		const QVariantMap state = m_mainWindow->m_modernDialogController->state();
		QVariantMap fieldState = buildAutomationDialogFieldState(fieldID);
		if (fieldState.value(QStringLiteral("exists")).toBool()) {
			m_automationDialogDraftDialogId = state.value(QStringLiteral("id")).toString();
			m_automationDialogDraftFieldId = fieldID;
			m_automationDialogDraftValue = request.value(QStringLiteral("value"));
			const QString fieldType = fieldState.value(QStringLiteral("type")).toString();
			m_automationDialogDraftActive = request.value(QStringLiteral("focus"), true).toBool()
				&& fieldState.value(QStringLiteral("enabled"), true).toBool()
				&& fieldType != QLatin1String("hidden") && fieldType != QLatin1String("readonly")
				&& fieldType != QLatin1String("note");
			m_mainWindow->handleModernDialogFieldUpdate(
				m_automationDialogDraftDialogId, fieldID, m_automationDialogDraftValue);
			fieldState = buildAutomationDialogFieldState(fieldID);
		}
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("fieldId"), fieldID);
		response.insert(QStringLiteral("field"), fieldState);
		return response;
	}

	if (command == QLatin1String("setClipboardText")) {
		QClipboard *clipboard = QApplication::clipboard();
		if (!clipboard) {
			return errorResponse(tr("Clipboard is not available."));
		}

		clipboard->setText(request.value(QStringLiteral("text")).toString());
		return okResponse();
	}

	if (command == QLatin1String("clipboardText")) {
		QVariantMap response = okResponse();
		if (const QClipboard *clipboard = QApplication::clipboard()) {
			response.insert(QStringLiteral("text"), clipboard->text());
		} else {
			response.insert(QStringLiteral("text"), QString());
		}
		return response;
	}

	if (command == QLatin1String("updateDialogField")) {
		const QString dialogID = request.value(QStringLiteral("dialogId")).toString().trimmed();
		const QString fieldID  = request.value(QStringLiteral("fieldId")).toString().trimmed();
		const QVariant value   = request.value(QStringLiteral("value"));
		if (dialogID.isEmpty() || fieldID.isEmpty()) {
			return errorResponse(tr("Missing dialogId or fieldId."));
		}

		if (async) {
			scheduleAction([dialogID, fieldID, value](MainWindow *window) {
				window->handleModernDialogFieldUpdate(dialogID, fieldID, value);
			});
			return asyncResponse();
		}

		m_mainWindow->handleModernDialogFieldUpdate(dialogID, fieldID, value);
		return okResponse();
	}

	if (command == QLatin1String("dialogAction")) {
		const QString dialogID    = request.value(QStringLiteral("dialogId")).toString().trimmed();
		const QString actionID    = request.value(QStringLiteral("actionId")).toString().trimmed();
		const QVariantMap payload = request.value(QStringLiteral("payload")).toMap();
		if (dialogID.isEmpty() || actionID.isEmpty()) {
			return errorResponse(tr("Missing dialogId or actionId."));
		}

		if (async) {
			scheduleAction([dialogID, actionID, payload](MainWindow *window) {
				window->handleModernDialogAction(dialogID, actionID, payload);
			});
			return asyncResponse();
		}

		m_mainWindow->handleModernDialogAction(dialogID, actionID, payload);
		return okResponse();
	}

	if (command == QLatin1String("openDisconnectProbe")) {
		const QString serverLabel = request.value(QStringLiteral("serverLabel"), tr("Current server")).toString();

		const auto openProbe = [serverLabel](MainWindow *window) {
			if (!window->m_modernDialogController) {
				window->m_modernDialogController = std::make_unique< ModernDialogController >();
			}
			window->publishModernDialogState(window->m_modernDialogController->openDisconnectConfirmation(serverLabel));
		};

		if (async) {
			scheduleAction(openProbe);
			return asyncResponse();
		}

		openProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("openQuitProbe")) {
		const bool connected     = request.value(QStringLiteral("connected"), true).toBool();
		const bool allowMinimize = request.value(QStringLiteral("allowMinimize"), false).toBool();

		const auto openProbe = [connected, allowMinimize](MainWindow *window) {
			if (!window->m_modernDialogController) {
				window->m_modernDialogController = std::make_unique< ModernDialogController >();
			}
			window->publishModernDialogState(
				window->m_modernDialogController->openQuitConfirmation(connected, allowMinimize));
		};

		if (async) {
			scheduleAction(openProbe);
			return asyncResponse();
		}

		openProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("openDeleteMessageProbe")) {
		bool parsedMessageID = false;
		const qulonglong messageID =
			unsignedLongLongValue(request.value(QStringLiteral("messageId"), 42), &parsedMessageID);
		const QString conversationLabel =
			request.value(QStringLiteral("conversationLabel"), QStringLiteral("#general")).toString();
		if (!parsedMessageID || messageID == 0) {
			return errorResponse(tr("Missing messageId."));
		}

		const auto openProbe = [messageID, conversationLabel](MainWindow *window) {
			if (!window->m_modernDialogController) {
				window->m_modernDialogController = std::make_unique< ModernDialogController >();
			}
			window->publishModernDialogState(
				window->m_modernDialogController->openDeleteMessageConfirmation(messageID, conversationLabel));
		};

		if (async) {
			scheduleAction(openProbe);
			return asyncResponse();
		}

		openProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("openAppDialogProbe")) {
		const QString variant  = request.value(QStringLiteral("variant")).toString().trimmed();
		const QString userName = request.value(QStringLiteral("userName")).toString().trimmed();
		if (variant.isEmpty()) {
			return errorResponse(tr("Missing variant."));
		}
		const QVariantMap dialog = automationAppDialogProbe(variant, userName);
		if (dialog.isEmpty()) {
			return errorResponse(tr("Unknown app dialog probe '%1'.").arg(variant));
		}

		const auto openProbe = [dialog, variant](MainWindow *window) {
			if (QmlShellHost *host = window->qmlShellHost()) {
				Mumble::ModernRecorderController *recorder = host->recorderController();
				if (variant == QLatin1String("voiceRecorder")
					|| variant == QLatin1String("voiceRecorderActive")) {
					const bool active = variant == QLatin1String("voiceRecorderActive");
					int format = 0;
					if (!recorder->formatOptions().isEmpty()) {
						format = recorder->formatOptions().constFirst().toMap().value(QStringLiteral("value")).toInt();
					}
					recorder->applyVisualFixtureState(
						active ? QStringLiteral("recording") : QStringLiteral("idle"), active ? 222000 : 0,
						QStringLiteral("C:/Recordings"), QStringLiteral("%user-%date"), format,
						Mumble::ModernRecorderController::Mixdown, false);
				} else {
					recorder->clearVisualFixtureState();
				}
			}
			if (!window->m_modernDialogController) {
				window->m_modernDialogController = std::make_unique< ModernDialogController >();
			}
			window->publishModernDialogState(window->m_modernDialogController->openGenericDialog(dialog));
		};

		if (async) {
			scheduleAction(openProbe);
			return asyncResponse();
		}

		openProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("openUserDialogProbe")) {
		const QString variant = request.value(QStringLiteral("variant")).toString().trimmed();
		bool parsedSession   = false;
		const qulonglong session =
			unsignedLongLongValue(request.value(QStringLiteral("session"), 7), &parsedSession);
		const QString userName = request.value(QStringLiteral("userName"), tr("Demo User")).toString().trimmed();
		if (variant.isEmpty()) {
			return errorResponse(tr("Missing variant."));
		}
		if (!parsedSession || session == 0) {
			return errorResponse(tr("Missing session."));
		}
		const QVariantMap dialog =
			automationUserDialogProbe(variant, static_cast< unsigned int >(session), userName);
		if (dialog.isEmpty()) {
			return errorResponse(tr("Unknown user dialog probe '%1'.").arg(variant));
		}

		const auto openProbe = [dialog](MainWindow *window) {
			if (!window->m_modernDialogController) {
				window->m_modernDialogController = std::make_unique< ModernDialogController >();
			}
			window->publishModernDialogState(window->m_modernDialogController->openGenericDialog(dialog));
		};

		if (async) {
			scheduleAction(openProbe);
			return asyncResponse();
		}

		openProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("openDataStateProbe")) {
		const QString variant = request.value(QStringLiteral("variant")).toString().trimmed();
		if (variant.isEmpty()) {
			return errorResponse(tr("Missing variant."));
		}
		const QVariantMap dialog = automationDataStateDialogProbe(variant);
		if (dialog.isEmpty()) {
			return errorResponse(tr("Unknown data state probe '%1'.").arg(variant));
		}

		const auto openProbe = [dialog](MainWindow *window) {
			if (!window->m_modernDialogController) {
				window->m_modernDialogController = std::make_unique< ModernDialogController >();
			}
			window->publishModernDialogState(window->m_modernDialogController->openGenericDialog(dialog));
		};

		if (async) {
			scheduleAction(openProbe);
			return asyncResponse();
		}

		openProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("openUrlMissingUsernameProbe")) {
		QString host = request.value(QStringLiteral("host"), QStringLiteral("missing-user.invalid")).toString().trimmed();
		if (host.isEmpty()) {
			host = QStringLiteral("missing-user.invalid");
		}
		int requestedPort = request.value(QStringLiteral("port"), DEFAULT_MUMBLE_PORT).toInt();
		if (requestedPort <= 0 || requestedPort > 65535) {
			requestedPort = DEFAULT_MUMBLE_PORT;
		}
		QString path = request.value(QStringLiteral("path"), QStringLiteral("/Lobby")).toString().trimmed();
		if (path.isEmpty()) {
			path = QStringLiteral("/");
		}
		if (!path.startsWith(QLatin1Char('/'))) {
			path.prepend(QLatin1Char('/'));
		}

		QUrl url;
		url.setScheme(QLatin1String("mumble"));
		url.setHost(host);
		url.setPort(requestedPort);
		url.setPath(path);
		QUrlQuery query;
		const QString title = request.value(QStringLiteral("title"), QStringLiteral("Invite from Modern test")).toString();
		if (!title.trimmed().isEmpty()) {
			query.addQueryItem(QStringLiteral("title"), title.trimmed());
		}
		url.setQuery(query);

		const auto openProbe = [url](MainWindow *window) {
			window->openUrl(url);
		};

		if (async) {
			scheduleAction(openProbe);
			return asyncResponse();
		}

		openProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("setStonksHeaderProbe")) {
		const QString variant = request.value(QStringLiteral("variant"), QStringLiteral("populated"))
									.toString()
									.trimmed();
		const QVariantMap state = automationStonksStateProbe(variant);
		if (state.isEmpty()) {
			return errorResponse(tr("Unknown Stonks header probe '%1'.").arg(variant));
		}

		const auto applyProbe = [state](MainWindow *window) {
			window->applyQmlStonksProbeState(state);
		};

		if (async) {
			scheduleAction(applyProbe);
			QVariantMap response = asyncResponse();
			response.insert(QStringLiteral("variant"), variant);
			response.insert(QStringLiteral("tickerBannerEnabled"),
							state.value(QStringLiteral("tickerBannerEnabled")).toBool());
			response.insert(QStringLiteral("automationHeaderVisible"),
							state.value(QStringLiteral("automationHeaderVisible")).toBool());
			return response;
		}

		applyProbe(m_mainWindow);
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("variant"), variant);
		response.insert(QStringLiteral("tickerBannerEnabled"),
						state.value(QStringLiteral("tickerBannerEnabled")).toBool());
		response.insert(QStringLiteral("automationHeaderVisible"),
						state.value(QStringLiteral("automationHeaderVisible")).toBool());
		return response;
	}

	if (command == QLatin1String("clearStonksProbe")) {
		const auto clearProbe = [](MainWindow *window) {
			window->applyQmlStonksProbeState({});
		};

		if (async) {
			scheduleAction(clearProbe);
			return asyncResponse();
		}

		clearProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("setConnectionStateProbe")) {
		const QString variant = request.value(QStringLiteral("variant")).toString().trimmed();
		const QVariantMap state = automationConnectionStateProbe(variant);
		if (state.isEmpty()) {
			return errorResponse(tr("Unknown connection-state probe '%1'.").arg(variant));
		}

		const auto applyProbe = [state](MainWindow *window) {
			window->applyQmlConnectionStateProbe(state);
		};

		if (async) {
			scheduleAction(applyProbe);
			return asyncResponse();
		}

		applyProbe(m_mainWindow);
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("variant"), variant);
		return response;
	}

	if (command == QLatin1String("clearConnectionStateProbe")) {
		const auto clearProbe = [](MainWindow *window) {
			window->applyQmlConnectionStateProbe({});
		};

		if (async) {
			scheduleAction(clearProbe);
			return asyncResponse();
		}

		clearProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("setScreenShareProbe")) {
		const QString variant    = request.value(QStringLiteral("variant")).toString().trimmed();
		const QString scopeToken = request.value(QStringLiteral("scopeToken")).toString().trimmed();
		const QVariantMap state  = automationScreenShareStateProbe(variant, scopeToken);
		if (state.isEmpty()) {
			return errorResponse(tr("Unknown screen-share probe '%1' or missing scope token.").arg(variant));
		}

		const auto applyProbe = [state](MainWindow *window) {
			window->applyQmlScreenShareStateProbe(state);
		};

		if (async) {
			scheduleAction(applyProbe);
			return asyncResponse();
		}

		applyProbe(m_mainWindow);
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("variant"), variant);
		response.insert(QStringLiteral("scopeToken"), scopeToken);
		return response;
	}

	if (command == QLatin1String("clearScreenShareProbe")) {
		const auto clearProbe = [](MainWindow *window) {
			window->applyQmlScreenShareStateProbe({});
		};

		if (async) {
			scheduleAction(clearProbe);
			return asyncResponse();
		}

		clearProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("setRichPreviewProbe")
		|| command == QLatin1String("setQmlRichPreviewProbe")) {
		const QString variant = request.value(QStringLiteral("variant")).toString().trimmed();
		const QString size    = request.value(QStringLiteral("size")).toString().trimmed();
		if (variant.isEmpty()) {
			return errorResponse(tr("Missing variant."));
		}
		QVariantList messages = automationRichPreviewProbeMessages(variant, size);
		if (messages.isEmpty()) {
			return errorResponse(tr("Unknown rich preview probe '%1'.").arg(variant));
		}

		const QString focusedMessageId = request.value(QStringLiteral("focusMessageId")).toString().trimmed();
		if (!focusedMessageId.isEmpty()) {
			QVariantList focusedMessages;
			for (const QVariant &value : messages) {
				if (value.toMap().value(QStringLiteral("messageId")).toString() == focusedMessageId) {
					focusedMessages.push_back(value);
					break;
				}
			}
			if (focusedMessages.isEmpty()) {
				return errorResponse(tr("Rich preview probe '%1' has no message '%2'.")
					.arg(variant, focusedMessageId));
			}
			messages = focusedMessages;
		}

		QmlShellHost *host = m_mainWindow ? m_mainWindow->qmlShellHost() : nullptr;
		if (!host || !host->window() || !host->chatModel() || !host->imagePipeline()) {
			return errorResponse(tr("The Qt Quick rich preview fixture host is unavailable."));
		}

		const auto applyProbe = [messages, variant](MainWindow *window) {
			QmlShellHost *host = window ? window->qmlShellHost() : nullptr;
			if (!window || !host || !host->imagePipeline()) {
				return;
			}
			window->applyQmlRichPreviewProbeMessages(
				automationRegisterPreviewImages(messages, host->imagePipeline(), variant));
		};

		if (async) {
			scheduleAction(applyProbe);
			return asyncResponse();
		}

		applyProbe(m_mainWindow);
		QStringList imageSources;
		int dataImageSourceCount = 0;
		automationCollectPreviewImageSources(
			m_mainWindow->m_modernRichPreviewProbeMessages, imageSources, dataImageSourceCount);
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("variant"), variant.trimmed().toLower());
		const QString normalizedSize = size.compare(QLatin1String("expanded"), Qt::CaseInsensitive) == 0
			? QStringLiteral("large") : size.trimmed().toLower();
		response.insert(QStringLiteral("size"), normalizedSize);
		response.insert(QStringLiteral("messageCount"), m_mainWindow->m_modernRichPreviewProbeMessages.size());
		response.insert(QStringLiteral("messageIds"),
			automationRichPreviewMessageIds(m_mainWindow->m_modernRichPreviewProbeMessages));
		response.insert(QStringLiteral("imageSources"), imageSources);
		response.insert(QStringLiteral("dataImageSourceCount"), dataImageSourceCount);
		response.insert(QStringLiteral("ready"), false);
		if (!focusedMessageId.isEmpty()) {
			response.insert(QStringLiteral("focusedMessageId"), focusedMessageId);
		}
		return response;
	}

	if (command == QLatin1String("getQmlRichPreviewProbeState")) {
		QmlShellHost *host = m_mainWindow ? m_mainWindow->qmlShellHost() : nullptr;
		if (!host || !host->window() || !host->chatModel() || !host->imagePipeline()) {
			return errorResponse(tr("The Qt Quick rich preview fixture host is unavailable."));
		}

		const QVariantList expectedMessages = m_mainWindow->m_modernRichPreviewProbeMessages;
		const QStringList expectedIds = automationRichPreviewMessageIds(expectedMessages);
		const bool modelReady = !expectedIds.isEmpty()
			&& host->chatModel()->messages() == expectedMessages;

		QStringList imageSources;
		int dataImageSourceCount = 0;
		automationCollectPreviewImageSources(expectedMessages, imageSources, dataImageSourceCount);
		int registeredImageSourceCount = 0;
		for (const QString &source : imageSources) {
			if (host->imagePipeline()->containsSource(source)) {
				++registeredImageSourceCount;
			}
		}

		QVariantList cardStates;
		int renderedCardCount = 0;
		int imageReadyCount = 0;
		int imageLoadingCount = 0;
		int imageErrorCount = 0;
		bool renderedImagesReady = true;
		for (const QString &id : expectedIds) {
			const QVariantMap cardState = automationRichPreviewCardState(host->window(), id);
			cardStates.push_back(cardState);
			if (cardState.value(QStringLiteral("rendered")).toBool()) {
				++renderedCardCount;
			}
			const int cardImageSources = cardState.value(QStringLiteral("imageSourceCount")).toInt();
			const int cardImageReady = cardState.value(QStringLiteral("imageReadyCount")).toInt();
			imageReadyCount += cardImageReady;
			imageLoadingCount += cardState.value(QStringLiteral("imageLoadingCount")).toInt();
			imageErrorCount += cardState.value(QStringLiteral("imageErrorCount")).toInt();
			if (cardImageSources > 0 && cardImageReady == 0) {
				renderedImagesReady = false;
			}
		}

		const bool imagesRegistered = dataImageSourceCount == 0
			&& registeredImageSourceCount == imageSources.size();
		const bool ready = modelReady && renderedCardCount == expectedIds.size() && imagesRegistered
			&& renderedImagesReady && imageLoadingCount == 0 && imageErrorCount == 0;
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("active"), !expectedIds.isEmpty());
		response.insert(QStringLiteral("ready"), ready);
		response.insert(QStringLiteral("modelReady"), modelReady);
		response.insert(QStringLiteral("messageCount"), expectedIds.size());
		response.insert(QStringLiteral("messageIds"), expectedIds);
		response.insert(QStringLiteral("renderedCardCount"), renderedCardCount);
		response.insert(QStringLiteral("cards"), cardStates);
		response.insert(QStringLiteral("imageSources"), imageSources);
		response.insert(QStringLiteral("registeredImageSourceCount"), registeredImageSourceCount);
		response.insert(QStringLiteral("dataImageSourceCount"), dataImageSourceCount);
		response.insert(QStringLiteral("imageReadyCount"), imageReadyCount);
		response.insert(QStringLiteral("imageLoadingCount"), imageLoadingCount);
		response.insert(QStringLiteral("imageErrorCount"), imageErrorCount);
		return response;
	}

	if (command == QLatin1String("setQmlRichPreviewProbeCardState")) {
		QmlShellHost *host = m_mainWindow ? m_mainWindow->qmlShellHost() : nullptr;
		if (!host || !host->window()) {
			return errorResponse(tr("The Qt Quick rich preview fixture host is unavailable."));
		}
		const QString messageId = request.value(QStringLiteral("messageId")).toString().trimmed();
		if (messageId.isEmpty()) {
			return errorResponse(tr("Missing messageId."));
		}
		if (!automationRichPreviewMessageIds(m_mainWindow->m_modernRichPreviewProbeMessages).contains(messageId)) {
			return errorResponse(tr("Message '%1' is not part of the active rich preview probe.").arg(messageId));
		}
		QObject *card = automationFindRichPreviewCard(host->window(), messageId);
		if (!card) {
			QVariantMap response = okResponse();
			response.insert(QStringLiteral("ready"), false);
			response.insert(QStringLiteral("messageId"), messageId);
			return response;
		}
		if (request.contains(QStringLiteral("expanded"))) {
			card->setProperty("userExpanded", request.value(QStringLiteral("expanded")).toBool());
		}
		if (request.contains(QStringLiteral("revealed"))) {
			card->setProperty("sensitiveMediaRevealed", request.value(QStringLiteral("revealed")).toBool());
		}
		if (request.value(QStringLiteral("focus")).toBool()) {
			QQuickItem *cardItem = qobject_cast< QQuickItem * >(card);
			auto findFocusTarget = [card, cardItem](const QString &objectName) -> QObject * {
				if (cardItem) {
					if (QObject *target = automationFindQuickItemByObjectName(cardItem, objectName)) {
						return target;
					}
				}
				return card->findChild< QObject * >(objectName);
			};
			QObject *focusTarget = findFocusTarget(QStringLiteral("previewOpenButton"));
			if (card->property("mediaRequiresReveal").toBool()) {
				QObject *revealTarget = findFocusTarget(QStringLiteral("previewExpandedRevealButton"));
				if (!revealTarget) {
					revealTarget = findFocusTarget(QStringLiteral("previewRevealButton"));
				}
				if (revealTarget) {
					focusTarget = revealTarget;
				}
			}
			if (QQuickItem *item = qobject_cast< QQuickItem * >(focusTarget ? focusTarget : card)) {
				item->forceActiveFocus(Qt::OtherFocusReason);
			}
		}
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("ready"), true);
		response.insert(QStringLiteral("card"), automationRichPreviewCardState(host->window(), messageId));
		return response;
	}

	if (command == QLatin1String("clearRichPreviewProbe")
		|| command == QLatin1String("clearQmlRichPreviewProbe")) {
		const auto clearProbe = [](MainWindow *window) {
			window->applyQmlRichPreviewProbeMessages({});
		};

		if (async) {
			scheduleAction(clearProbe);
			return asyncResponse();
		}

		clearProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("setMessageDeliveryProbe")) {
		const QVariantList messages = automationMessageDeliveryProbeMessages();
		const auto applyProbe = [messages](MainWindow *window) {
			window->applyQmlMessageDeliveryProbeMessages(messages);
		};

		if (async) {
			scheduleAction(applyProbe);
			return asyncResponse();
		}

		applyProbe(m_mainWindow);
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("messageCount"), messages.size());
		return response;
	}

	if (command == QLatin1String("clearMessageDeliveryProbe")) {
		const auto clearProbe = [](MainWindow *window) {
			window->applyQmlMessageDeliveryProbeMessages({});
		};

		if (async) {
			scheduleAction(clearProbe);
			return asyncResponse();
		}

		clearProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("setDirectMessageProbe")) {
		const QString variant = request.value(QStringLiteral("variant"), QStringLiteral("main"))
									.toString()
									.trimmed()
									.toLower();
		if (variant != QLatin1String("main") && variant != QLatin1String("tray")
			&& variant != QLatin1String("private") && variant != QLatin1String("window")) {
			return errorResponse(tr("Unknown direct-message probe '%1'.").arg(variant));
		}

		bool parsedSession = false;
		const qulonglong requestedSession =
			unsignedLongLongValue(request.value(QStringLiteral("session")), &parsedSession);
		if (parsedSession && requestedSession > std::numeric_limits< unsigned int >::max()) {
			return errorResponse(tr("Direct-message session is out of range."));
		}

		const unsigned int fallbackSyntheticSession = 9001;
		unsigned int session =
			parsedSession && requestedSession > 0 ? static_cast< unsigned int >(requestedSession)
												  : firstAutomationDirectMessagePeerSession();
		if (session == 0) {
			session = fallbackSyntheticSession;
		}
		if (session == 0 || session == Global::get().uiSession) {
			return errorResponse(tr("Direct-message probe session is not usable."));
		}

		const QString requestedLabel = request.value(QStringLiteral("label")).toString().trimmed();
		const QString probeLabel = requestedLabel.isEmpty() ? tr("Kira Mockup") : requestedLabel;
		const QString requestedSubtitle = request.value(QStringLiteral("subtitle")).toString().trimmed();
		const QString probeSubtitle =
			requestedSubtitle.isEmpty() ? tr("Automation direct-message probe") : requestedSubtitle;
		const bool syntheticPeer = ClientUser::get(session) == nullptr;

		const auto applyProbe = [session, variant, probeLabel, probeSubtitle](MainWindow *window) -> bool {
			ClientUser *peer = ClientUser::get(session);
			if (session == Global::get().uiSession) {
				return false;
			}

			MainWindow::ModernDirectMessageConversation &conversation =
				window->m_modernDirectMessageConversations[session];
			conversation = MainWindow::ModernDirectMessageConversation();
			conversation.peerSession = session;
			if (peer) {
				conversation.peerUserID = window->persistentUserIDForClientUser(peer).value_or(0);
				conversation.label      = peer->qsName;
				conversation.subtitle   = peer->cChannel ? QObject::tr("In %1").arg(peer->cChannel->qsName)
														 : QObject::tr("No active channel");
				conversation.persistentHistory =
					variant != QLatin1String("private") && window->modernDirectMessagePersistentHistoryAvailable(peer);
			} else {
				conversation.label             = probeLabel;
				conversation.subtitle          = probeSubtitle;
				conversation.persistentHistory = false;
			}
			conversation.historyLoaded = conversation.persistentHistory;

			window->appendModernDirectMessage(
				session, QObject::tr("Can you check the reconnect dialog against the mockup?"), false);
			window->appendModernDirectMessage(
				session, QObject::tr("Yes. The new menus need to match the compact shell chrome."), true);
			window->appendModernDirectMessage(
				session, QObject::tr("I'll keep this as a focused direct-message thread."), false);

			auto it = window->m_modernDirectMessageConversations.find(session);
			if (it == window->m_modernDirectMessageConversations.end()) {
				return false;
			}

			it->open                = variant == QLatin1String("private") || variant == QLatin1String("window");
			it->unreadCount         = variant == QLatin1String("tray") ? 2 : 0;
			it->persistentHistory   = variant != QLatin1String("private") && it->persistentHistory;
			it->historyLoading      = false;
			it->historyLoaded       = it->persistentHistory;
			it->historyError        = QString();
			it->lastActivityAtMs    = it->messages.empty() ? 0 : it->messages.back().createdAtMs;
			window->m_modernDirectMessageTrayOpenProbe = variant == QLatin1String("tray");
			const QString scopeToken = variant == QLatin1String("tray")
									   ? QStringLiteral("-1:0")
									   : QStringLiteral("-2:%1").arg(static_cast< qulonglong >(session));
			// QmlSelectionState validates tokens against RoomModel. Materialize the synthetic/direct row
			// before selecting it so the typed scope is accepted on the first attempt.
			window->publishQmlDirectMessagesState();
			const bool selected = window->handleModernShellScopeSelection(scopeToken);
			window->publishQmlDirectMessagesState();
			window->publishQmlActiveScopeState();
			return selected;
		};
		const auto inspectProbeSurface = [variant](MainWindow *window) {
			QmlShellHost *qmlHost = window ? window->qmlShellHost() : nullptr;
			if (!qmlHost || !qmlHost->window()) return QVariantMap();
			QVariant result;
			const bool invoked = QMetaObject::invokeMethod(
				qmlHost->window(), "directMessageAutomationSurfaceState", Q_RETURN_ARG(QVariant, result),
				Q_ARG(QVariant, QVariant::fromValue(variant)));
			return invoked ? result.toMap() : QVariantMap();
		};

		if (async) {
			scheduleAction([applyProbe](MainWindow *window) { applyProbe(window); });
			QVariantMap response = asyncResponse();
			response.insert(QStringLiteral("session"), static_cast< qulonglong >(session));
			response.insert(QStringLiteral("variant"), variant);
			response.insert(QStringLiteral("syntheticPeer"), syntheticPeer);
			return response;
		}

		const bool backendHandled = applyProbe(m_mainWindow);
		const QVariantMap surface = backendHandled ? inspectProbeSurface(m_mainWindow) : QVariantMap();
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("result"), surface);
		response.insert(QStringLiteral("handled"),
			backendHandled && surface.value(QStringLiteral("handled")).toBool());
		response.insert(QStringLiteral("session"), static_cast< qulonglong >(session));
		response.insert(QStringLiteral("variant"), variant);
		response.insert(QStringLiteral("syntheticPeer"), syntheticPeer);
		response.insert(QStringLiteral("visible"), surface.value(QStringLiteral("visible")).toBool());
		response.insert(QStringLiteral("surfaceId"), surface.value(QStringLiteral("surfaceId")).toString());
		response.insert(QStringLiteral("objectName"), surface.value(QStringLiteral("objectName")).toString());
		response.insert(QStringLiteral("windowId"), surface.value(QStringLiteral("windowId")).toString());
		response.insert(QStringLiteral("captureRect"), surface.value(QStringLiteral("captureRect")).toMap());
		response.insert(QStringLiteral("viewportWidth"), surface.value(QStringLiteral("viewportWidth")).toInt());
		response.insert(QStringLiteral("viewportHeight"), surface.value(QStringLiteral("viewportHeight")).toInt());
		return response;
	}

	if (command == QLatin1String("clearDirectMessageProbe")) {
		bool parsedSession = false;
		const qulonglong requestedSession =
			unsignedLongLongValue(request.value(QStringLiteral("session")), &parsedSession);
		if (parsedSession && requestedSession > std::numeric_limits< unsigned int >::max()) {
			return errorResponse(tr("Direct-message session is out of range."));
		}
		const unsigned int session = parsedSession ? static_cast< unsigned int >(requestedSession) : 0;

		const auto clearProbe = [parsedSession, session](MainWindow *window) {
			window->m_modernDirectMessageTrayOpenProbe = false;

			QList< unsigned int > removeSessions;
			for (auto it = window->m_modernDirectMessageConversations.cbegin();
				 it != window->m_modernDirectMessageConversations.cend(); ++it) {
				const bool syntheticPeer = ClientUser::get(it.key()) == nullptr;
				if ((parsedSession && it.key() == session) || (!parsedSession && syntheticPeer)) {
					removeSessions.push_back(it.key());
				}
			}
			for (const unsigned int removeSession : removeSessions) {
				window->m_modernDirectMessageConversations.remove(removeSession);
			}
			for (auto it = window->m_modernDirectMessageConversations.begin();
				 it != window->m_modernDirectMessageConversations.end(); ++it) {
				it->open        = false;
				it->unreadCount = 0;
			}

			window->handleModernShellScopeSelection(QStringLiteral("-1:0"));
			window->publishQmlDirectMessagesState();
		};

		if (async) {
			scheduleAction(clearProbe);
			return asyncResponse();
		}

		clearProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("openStonksDialogProbe")) {
		const QString variant = request.value(QStringLiteral("variant"), QStringLiteral("populated"))
									.toString()
									.trimmed();
		const QVariantMap dialog = automationStonksDialogProbe(variant);
		if (dialog.isEmpty()) {
			return errorResponse(tr("Unknown Stonks dialog probe '%1'.").arg(variant));
		}

		const auto openProbe = [dialog](MainWindow *window) {
			if (!window->m_modernDialogController) {
				window->m_modernDialogController = std::make_unique< ModernDialogController >();
			}
			window->publishModernDialogState(window->m_modernDialogController->openGenericDialog(dialog));
		};

		if (async) {
			scheduleAction(openProbe);
			return asyncResponse();
		}

		openProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("stonksActionDryRun")) {
		const QString actionID = request.value(QStringLiteral("actionId")).toString().trimmed();
		if (actionID.isEmpty()) {
			return errorResponse(tr("Missing Stonks actionId."));
		}

		const QString requestedPeriod = request.value(QStringLiteral("period")).toString().trimmed();
		const QString selectedPeriod  = requestedPeriod.isEmpty() ? m_mainWindow->m_stonksSelectedPeriod
																  : requestedPeriod;
		const QVariantMap summary = automationStonksActionSummary(
			actionID, request.value(QStringLiteral("payload")).toMap(), selectedPeriod,
			Global::get().bStonksEnabled, Global::get().bStonksSocialAnnouncementsEnabled);
		if (!summary.value(QStringLiteral("recognized")).toBool()) {
			return errorResponse(tr("Unknown Stonks action '%1'.").arg(actionID));
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("summary"), summary);
		return response;
	}

	if (command == QLatin1String("certificateRoundTripDryRun")) {
		const QString exportPath = request.value(QStringLiteral("exportPath")).toString();
		const QString name = request.value(QStringLiteral("name"),
										   tr("Mumble Modern Certificate Probe"))
								 .toString();
		const QString email = request.value(QStringLiteral("email"),
											QStringLiteral("modern-probe@example.invalid"))
								  .toString();
		const QVariantMap result = automationCertificateRoundTripProbe(exportPath, name, email);
		const QString error = result.value(QStringLiteral("error")).toString().trimmed();
		if (!error.isEmpty()) {
			return errorResponse(error);
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("result"), result);
		return response;
	}

	if (command == QLatin1String("feedbackSubmitDryRun")) {
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("result"), automationFeedbackSubmitDryRun());
		return response;
	}

	if (command == QLatin1String("motdActionDryRun")) {
		const QVariantMap result = automationMotdActionDryRun(m_mainWindow);
		const QString error = result.value(QStringLiteral("error")).toString().trimmed();
		if (!error.isEmpty()) {
			return errorResponse(error);
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("result"), result);
		return response;
	}

	if (command == QLatin1String("motdSettingsState")) {
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("state"), automationMotdSettingsState(m_mainWindow));
		return response;
	}

	if (command == QLatin1String("setMotdSettingsState")) {
		const QVariantMap state = request.value(QStringLiteral("state")).toMap();
		if (!state.contains(QStringLiteral("expanded"))) {
			return errorResponse(tr("Missing MOTD expanded state."));
		}
		const QString serializedStates = ModernMotd::withServerViewState(
			Global::get().s.qsModernShellMotdServerStates, automationMotdServerStateKey(), state);
		restoreAutomationMotdSettings(m_mainWindow, serializedStates);

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("state"), automationMotdSettingsState(m_mainWindow));
		return response;
	}

	if (command == QLatin1String("updateHandoffDryRun")) {
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("result"), automationUpdateHandoffDryRun());
		return response;
	}

	if (command == QLatin1String("versionCheckModernRoutingDryRun")) {
		const QString releaseUrlText =
			QStringLiteral("https://github.com/dankmaster/mumble/releases/tag/mumble-forked");
		const QString installerUrlText =
			QStringLiteral("https://github.com/dankmaster/mumble/releases/download/mumble-forked/mumble-forked.msi");
		const QString validSha256 =
			QStringLiteral("0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef");

		QJsonObject installableInfo;
		installableInfo.insert(QStringLiteral("releaseUrl"), releaseUrlText);
		installableInfo.insert(QStringLiteral("installerUrl"), installerUrlText);
		installableInfo.insert(QStringLiteral("sha256"), validSha256);
		installableInfo.insert(QStringLiteral("version"), QStringLiteral("1.7.1"));
		installableInfo.insert(QStringLiteral("build"), 42);

		QJsonObject fallbackInfo = installableInfo;
		fallbackInfo.remove(QStringLiteral("sha256"));

		const auto dialogSummary = [this]() {
			QVariantMap summary;
			const QVariantMap state =
				m_mainWindow && m_mainWindow->m_modernDialogController
					? m_mainWindow->m_modernDialogController->state()
					: QVariantMap();
			summary.insert(QStringLiteral("open"), state.value(QStringLiteral("open")).toBool());
			summary.insert(QStringLiteral("id"), state.value(QStringLiteral("id")).toString());
			summary.insert(QStringLiteral("kind"), state.value(QStringLiteral("kind")).toString());
			summary.insert(QStringLiteral("title"), state.value(QStringLiteral("title")).toString());
			summary.insert(QStringLiteral("tone"), state.value(QStringLiteral("tone")).toString());
			summary.insert(QStringLiteral("primaryActionId"), state.value(QStringLiteral("primaryActionId")).toString());
			return summary;
		};

		m_mainWindow->handleModernDialogClose(QString());
		const bool availableHandled =
			m_mainWindow->handleModernVersionCheckResult(installableInfo, true, false);
		const QVariantMap availableDialog = dialogSummary();
		m_mainWindow->handleModernDialogClose(QString());

		const bool currentHandled =
			m_mainWindow->handleModernVersionCheckResult(installableInfo, false, false);
		const QVariantMap currentDialog = dialogSummary();
		m_mainWindow->handleModernDialogClose(QString());

		const bool failureHandled =
			m_mainWindow->handleModernVersionCheckFailure(tr("Automation update failure"), false);
		const QVariantMap failureDialog = dialogSummary();
		m_mainWindow->handleModernDialogClose(QString());

		const bool autocheckAvailableHandled =
			m_mainWindow->handleModernVersionCheckResult(installableInfo, true, true);
		const bool autocheckCurrentHandled =
			m_mainWindow->handleModernVersionCheckResult(installableInfo, false, true);
		const bool installFallbackHandled = m_mainWindow->startModernForkUpdateDownload(fallbackInfo);

		QVariantMap result;
		result.insert(QStringLiteral("availableHandled"), availableHandled);
		result.insert(QStringLiteral("availableDialog"), availableDialog);
		result.insert(QStringLiteral("currentHandled"), currentHandled);
		result.insert(QStringLiteral("currentDialog"), currentDialog);
		result.insert(QStringLiteral("failureHandled"), failureHandled);
		result.insert(QStringLiteral("failureDialog"), failureDialog);
		result.insert(QStringLiteral("autocheckAvailableHandled"), autocheckAvailableHandled);
		result.insert(QStringLiteral("autocheckCurrentHandled"), autocheckCurrentHandled);
		result.insert(QStringLiteral("installFallbackHandled"), installFallbackHandled);
		result.insert(QStringLiteral("installFallbackStartedDownload"), m_mainWindow->m_modernUpdateDownloadInProgress);
		result.insert(QStringLiteral("installFallbackCanInstall"),
					  VersionCheck::canInstallUpdate(m_mainWindow->m_modernVersionCheckInfo));
		result.insert(QStringLiteral("desktopOpenSuppressed"), true);
		result.insert(QStringLiteral("networkDownloadSuppressed"), true);
		result.insert(QStringLiteral("processLaunchSuppressed"), true);
		result.insert(QStringLiteral("nativeDialogExpected"), false);

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("result"), result);
		return response;
	}

	if (command == QLatin1String("shortcutEngineDiagnostics")) {
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("result"), automationShortcutEngineDiagnostics());
		return response;
	}

	if (command == QLatin1String("openChangeAvatarProbe")) {
		bool parsedSession = false;
		const qulonglong session =
			unsignedLongLongValue(request.value(QStringLiteral("session"), 1), &parsedSession);
		const QString userName = request.value(QStringLiteral("userName"), tr("Current user")).toString().trimmed();
		const QVariantMap fieldValues = request.value(QStringLiteral("fieldValues")).toMap();
		const QVariantMap errors      = request.value(QStringLiteral("errors")).toMap();
		if (!parsedSession || session == 0) {
			return errorResponse(tr("Missing session."));
		}

		const auto openProbe = [session, userName, fieldValues, errors](MainWindow *window) {
			if (!window->m_modernDialogController) {
				window->m_modernDialogController = std::make_unique< ModernDialogController >();
			}
			window->publishModernDialogState(window->m_modernDialogController->openChangeAvatar(
				static_cast< unsigned int >(session), userName.isEmpty() ? QObject::tr("Current user") : userName,
				fieldValues, errors));
		};

		if (async) {
			scheduleAction(openProbe);
			return asyncResponse();
		}

		openProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("openFailedConnectionProbe")) {
		bool parsedPort = false;
		const qulonglong port =
			unsignedLongLongValue(request.value(QStringLiteral("port"), 64738), &parsedPort);
		const QString type = request.value(QStringLiteral("type"), QStringLiteral("authenticationFailure"))
								 .toString()
								 .trimmed();
		if (!parsedPort || port > 65535 || type.isEmpty()) {
			return errorResponse(tr("Missing failed connection type or port."));
		}

		QVariantMap context;
		context.insert(QStringLiteral("type"), type);
		context.insert(QStringLiteral("host"),
					   request.value(QStringLiteral("host"), QStringLiteral("voice.example.test")).toString());
		context.insert(QStringLiteral("port"), static_cast< int >(port));
		context.insert(QStringLiteral("username"),
					   request.value(QStringLiteral("username"), QStringLiteral("demo-user")).toString());
		context.insert(QStringLiteral("password"),
					   request.value(QStringLiteral("password"), QStringLiteral("demo-password")).toString());

		const auto openProbe = [context](MainWindow *window) {
			if (!window->m_modernDialogController) {
				window->m_modernDialogController = std::make_unique< ModernDialogController >();
			}
			window->publishModernDialogState(window->m_modernDialogController->openFailedConnection(context));
		};

		if (async) {
			scheduleAction(openProbe);
			return asyncResponse();
		}

		openProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("deleteMessage")) {
		bool parsedMessageID = false;
		const qulonglong messageID =
			unsignedLongLongValue(request.value(QStringLiteral("messageId")), &parsedMessageID);
		if (!parsedMessageID || messageID == 0) {
			return errorResponse(tr("Missing messageId."));
		}

		if (async) {
			scheduleAction([messageID](MainWindow *window) { window->handleModernShellMessageDelete(messageID); });
			return asyncResponse();
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), m_mainWindow->handleModernShellMessageDelete(messageID));
		return response;
	}

	if (command == QLatin1String("sendMessage")) {
		const QString message = request.value(QStringLiteral("message")).toString();
		if (message.trimmed().isEmpty()) {
			return errorResponse(tr("Missing message."));
		}

		if (async) {
			scheduleAction([message](MainWindow *window) { window->sendModernShellMessage(message); });
			return asyncResponse();
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), m_mainWindow->sendModernShellMessage(message));
		return response;
	}

	return errorResponse(tr("Unknown automation command '%1'.").arg(command));
}

QVariantMap ModernUiAutomationServer::buildAutomationDialogFieldState(const QString &fieldId) const {
	QVariantMap result;
	const QString requestedFieldId = fieldId.trimmed();
	const QVariantMap dialog = m_mainWindow && m_mainWindow->m_modernDialogController
		? m_mainWindow->m_modernDialogController->state()
		: QVariantMap();
	const QString dialogId = dialog.value(QStringLiteral("id")).toString();
	QVariantMap field;
	QVariantList availableFieldIds;
	for (const QVariant &sectionValue : dialog.value(QStringLiteral("sections")).toList()) {
		for (const QVariant &fieldValue : sectionValue.toMap().value(QStringLiteral("fields")).toList()) {
			const QVariantMap candidate = fieldValue.toMap();
			const QString candidateId = candidate.value(QStringLiteral("id")).toString().trimmed();
			if (candidateId.isEmpty()) continue;
			availableFieldIds.push_back(candidateId);
			if (candidateId == requestedFieldId) field = candidate;
		}
	}

	const bool exists = dialog.value(QStringLiteral("open")).toBool() && !field.isEmpty();
	const bool hasDraft = exists && dialogId == m_automationDialogDraftDialogId
		&& requestedFieldId == m_automationDialogDraftFieldId;
	result.insert(QStringLiteral("dialogId"), dialogId);
	result.insert(QStringLiteral("fieldId"), requestedFieldId);
	result.insert(QStringLiteral("exists"), exists);
	result.insert(QStringLiteral("active"), hasDraft && m_automationDialogDraftActive);
	result.insert(QStringLiteral("type"), field.value(QStringLiteral("type")).toString());
	result.insert(QStringLiteral("enabled"), field.value(QStringLiteral("enabled"), true).toBool());
	result.insert(QStringLiteral("value"), hasDraft ? m_automationDialogDraftValue
												 : field.value(QStringLiteral("value")));
	result.insert(QStringLiteral("availableFieldIds"), availableFieldIds);
	result.insert(QStringLiteral("stateValue"), field.value(QStringLiteral("value")));
	return result;
}

QVariantMap ModernUiAutomationServer::buildStateResponse() const {
	QVariantMap response = okResponse();
	QmlShellHost *host = m_mainWindow ? m_mainWindow->m_qmlShellHost.get() : nullptr;
	QVariantMap state;
#if defined(MUMBLE_HAS_SPEECH_CLEANUP_E2E)
	const bool speechCleanupE2eHeadless =
		qEnvironmentVariable("MUMBLE_SPEECH_CLEANUP_E2E_ENABLE") == QLatin1String("1")
		&& qEnvironmentVariable("MUMBLE_SPEECH_CLEANUP_E2E_HEADLESS") == QLatin1String("1")
		&& !qEnvironmentVariable("MUMBLE_SPEECH_CLEANUP_E2E_TOKEN").isEmpty();
	if (speechCleanupE2eHeadless && host && !host->window()) {
		// The headless E2E path deliberately does not materialize QML models. Build
		// the same app/room/participant DTOs directly from live protocol state so
		// connection and peer synchronization remain strictly observable.
		state = m_mainWindow->buildQmlRoomState();
		QVariantMap appState = state.value(QStringLiteral("app")).toMap();
		appState.insert(QStringLiteral("connected"),
						appState.value(QStringLiteral("connectionState")) == QLatin1String("connected"));
		state.insert(QStringLiteral("app"), appState);
	} else
#endif
	if (host) {
		ClientSessionController *session = host->sessionController();
		UiCommandController *commands = host->commandController();
		ActiveScopeController *scope = host->activeScopeController();
		QmlSelectionState *selection = host->selectionState();
		state.insert(QStringLiteral("app"),
					 QVariantMap { { QStringLiteral("serverTitle"), session->serverName() },
								   { QStringLiteral("selfName"), session->selfName() },
								   { QStringLiteral("selfStatusLabel"), session->selfStatusLabel() },
								   { QStringLiteral("selfMuted"), session->selfMuted() },
								   { QStringLiteral("selfDeafened"), session->selfDeafened() },
								   { QStringLiteral("connected"), session->connected() },
								   { QStringLiteral("connectionState"), session->connectionState() },
								   { QStringLiteral("connectionLabel"), session->connectionLabel() },
								   { QStringLiteral("connectionTone"), session->connectionTone() },
								   { QStringLiteral("connectionTooltip"), session->connectionDetail() },
								   { QStringLiteral("connectionRetryRemainingMs"),
									 session->connectionRetryRemainingMs() },
								   { QStringLiteral("canConnect"), session->canConnect() },
								   { QStringLiteral("canCancelConnection"), session->canCancel() },
								   { QStringLiteral("updateBanner"), session->updateBanner() },
								   { QStringLiteral("motdHtml"), session->motdHtml() },
								   { QStringLiteral("motdSummary"), session->motdSummary() },
								   { QStringLiteral("hasMotd"), session->hasMotd() },
								   { QStringLiteral("motdExpanded"), session->motdExpanded() },
								   { QStringLiteral("motdDismissed"), session->motdDismissed() },
								   { QStringLiteral("motdSignature"), session->motdSignature() },
								   { QStringLiteral("motdDismissedSignature"),
									 session->motdDismissedSignature() },
								   { QStringLiteral("motdLastSeenSignature"),
									 session->motdLastSeenSignature() },
								   { QStringLiteral("motdChanged"), session->motdChanged() },
								   { QStringLiteral("motdActions"), session->motdActions() },
								   { QStringLiteral("menus"), session->appMenus() },
								   { QStringLiteral("selfMenu"), session->selfMenu() },
								   { QStringLiteral("pttPressed"), commands->pttPressed() } });
		QVariantMap activeScopeState { { QStringLiteral("scopeToken"), scope->scopeToken() },
									{ QStringLiteral("label"), scope->label() },
									{ QStringLiteral("description"), scope->description() },
									{ QStringLiteral("kindLabel"), scope->kindLabel() },
									{ QStringLiteral("composerPlaceholder"), scope->composerPlaceholder() },
									{ QStringLiteral("composerHint"), scope->composerHint() },
									{ QStringLiteral("canSend"), scope->canSend() },
									{ QStringLiteral("canLoadOlder"), scope->canLoadOlder() },
									{ QStringLiteral("loading"), scope->loading() },
									{ QStringLiteral("loadingState"), scope->loadingState() },
									{ QStringLiteral("screenShare"), scope->screenShare() } };
		if (m_mainWindow && m_mainWindow->m_persistentChatController) {
			const PersistentChatScopeKey controllerScope = m_mainWindow->m_persistentChatController->activeScope();
			const PersistentChatScopeStateSnapshot controllerSnapshot =
				m_mainWindow->m_persistentChatController->activeSnapshot();
			const MainWindow::PersistentChatTarget target = m_mainWindow->currentPersistentChatTarget();
			activeScopeState.insert(QStringLiteral("controllerScopeValid"), controllerScope.valid);
			activeScopeState.insert(QStringLiteral("controllerScope"), static_cast< int >(controllerScope.scope));
			activeScopeState.insert(QStringLiteral("controllerScopeId"), controllerScope.scopeID);
			activeScopeState.insert(QStringLiteral("controllerMessageCount"), controllerSnapshot.messages.size());
			activeScopeState.insert(QStringLiteral("controllerUnreadCount"), controllerSnapshot.unreadCount);
			activeScopeState.insert(QStringLiteral("controllerInitialLoaded"), controllerSnapshot.initialLoaded);
			activeScopeState.insert(QStringLiteral("canViewHistory"),
				m_mainWindow->canViewPersistentChatHistory(target, false));
		}
		state.insert(QStringLiteral("selection"),
					 QVariantMap { { QStringLiteral("scopeToken"), selection->scopeToken() },
								   { QStringLiteral("selectedUserSession"), selection->selectedUserSession() },
								   { QStringLiteral("selectedVoiceChannelId"), selection->selectedVoiceChannelId() } });

		QVariantList voiceRooms;
		QVariantList textRooms;
		RoomModel *rooms = host->roomModel();
		for (int row = 0; row < rooms->rowCount(); ++row) {
			const QVariantMap modelRow = rooms->get(row);
			// RoomModel intentionally keeps only the bounded action payload in its source role. Rebuild the
			// automation DTO from the typed row instead of treating the internal stable row ID (for example
			// "voice:4:0") as a protocol scope token. The latter cannot be passed back to selectScope and also
			// drops the room label/path whenever context actions are present.
			QVariantMap item = modelRow.value(QStringLiteral("source")).toMap();
			item.insert(QStringLiteral("token"), modelRow.value(QStringLiteral("scopeToken")));
			item.insert(QStringLiteral("label"), modelRow.value(QStringLiteral("title")));
			item.insert(QStringLiteral("description"), modelRow.value(QStringLiteral("subtitle")));
			item.insert(QStringLiteral("pathLabel"), modelRow.value(QStringLiteral("pathLabel")));
			item.insert(QStringLiteral("kindLabel"), modelRow.value(QStringLiteral("kindLabel")));
			item.insert(QStringLiteral("selected"), modelRow.value(QStringLiteral("selected")));
			item.insert(QStringLiteral("joined"), modelRow.value(QStringLiteral("joined")));
			item.insert(QStringLiteral("canJoin"), modelRow.value(QStringLiteral("canJoin")));
			item.insert(QStringLiteral("depth"), modelRow.value(QStringLiteral("depth")));
			item.insert(QStringLiteral("unreadCount"), modelRow.value(QStringLiteral("unreadCount")));
			item.insert(QStringLiteral("badges"), modelRow.value(QStringLiteral("badges")));
			item.insert(QStringLiteral("screenShare"), modelRow.value(QStringLiteral("screenShare")));
			(modelRow.value(QStringLiteral("kind")).toString() == QLatin1String("voice") ? voiceRooms : textRooms)
				.push_back(item);
			// The active-scope controller is the authoritative typed state. Only fall back to the room row
			// when an older producer did not publish a screen-share payload on the active scope itself.
			if (activeScopeState.value(QStringLiteral("screenShare")).toMap().isEmpty()
				&& item.value(QStringLiteral("token")).toString() == scope->scopeToken()
				&& item.contains(QStringLiteral("screenShare"))) {
				activeScopeState.insert(QStringLiteral("screenShare"), item.value(QStringLiteral("screenShare")));
			}
		}
		state.insert(QStringLiteral("activeScope"), activeScopeState);
		state.insert(QStringLiteral("voiceRooms"), voiceRooms);
		state.insert(QStringLiteral("textRooms"), textRooms);

		state.insert(QStringLiteral("participants"), automationModelRows(host->participantModel(), true));
		state.insert(QStringLiteral("messages"), automationModelRows(host->chatModel(), true));
		state.insert(QStringLiteral("actions"), automationModelRows(host->actionModel(), false));
		QVariantMap directMessages = automationDirectMessageState(host->directMessageController());
		directMessages.insert(QStringLiteral("windowCaptureReady"),
			host->captureWindowReady(QStringLiteral("direct-message")));
		state.insert(QStringLiteral("directMessages"), directMessages);
		state.insert(QStringLiteral("toast"), automationToastState(host->toastController()));
		state.insert(QStringLiteral("media"), automationMediaControllerState(host->mediaSession()));
		state.insert(QStringLiteral("screenShareViewers"), automationScreenShareViewerStates());
		const Mumble::ModernRecorderController *recorder = host->recorderController();
		state.insert(QStringLiteral("recorder"), QVariantMap {
			{ QStringLiteral("state"), recorder->state() },
			{ QStringLiteral("busy"), recorder->busy() },
			{ QStringLiteral("canEdit"), recorder->canEdit() },
			{ QStringLiteral("canStart"), recorder->canStart() },
			{ QStringLiteral("canPause"), recorder->canPause() },
			{ QStringLiteral("canResume"), recorder->canResume() },
			{ QStringLiteral("canStop"), recorder->canStop() },
			{ QStringLiteral("transportSupported"), recorder->transportSupported() },
			{ QStringLiteral("elapsedMilliseconds"), recorder->elapsedMilliseconds() },
			{ QStringLiteral("elapsedText"), recorder->elapsedText() },
			{ QStringLiteral("outputDirectory"), recorder->outputDirectory() },
			{ QStringLiteral("fileName"), recorder->fileName() },
			{ QStringLiteral("resolvedOutputPath"), recorder->resolvedOutputPath() },
			{ QStringLiteral("format"), recorder->format() },
			{ QStringLiteral("mode"), recorder->mode() },
			{ QStringLiteral("errorCode"), recorder->errorCode() },
			{ QStringLiteral("errorMessage"), recorder->errorMessage() },
			{ QStringLiteral("operationId"), recorder->operationId() },
			{ QStringLiteral("operationAction"), recorder->operationAction() },
			{ QStringLiteral("operationStatus"), recorder->operationStatus() },
			{ QStringLiteral("operationPhase"), recorder->operationPhase() }
		});
		state.insert(QStringLiteral("dialog"), host->dialogController()->state());
		const QVariantMap themeState = host->themeController() ? host->themeController()->state() : QVariantMap();
		state.insert(QStringLiteral("themeState"), themeState);
		// Preserve the established automation wire key while sourcing it from the typed theme controller.
		state.insert(QStringLiteral("uiTweaks"), themeState);
		QVariantMap appState = state.value(QStringLiteral("app")).toMap();
		appState.insert(QStringLiteral("uiTweaks"), themeState);
		state.insert(QStringLiteral("app"), appState);
	}
	// Keep the wire key and command ID stable for existing automation clients;
	// the payload is now composed directly from typed QML controllers and models.
	response.insert(QStringLiteral("snapshot"), state);
	response.insert(QStringLiteral("modernDialog"),
					m_mainWindow && m_mainWindow->m_modernDialogController
						? m_mainWindow->m_modernDialogController->state()
						: QVariantMap { { QStringLiteral("open"), false } });
	response.insert(QStringLiteral("usesModernShell"), m_mainWindow && true);
	response.insert(QStringLiteral("frontend"), QStringLiteral("qml"));
	response.insert(QStringLiteral("listening"), isListening());
	response.insert(QStringLiteral("port"), port());
	return response;
}

void ModernUiAutomationServer::writeResponse(QTcpSocket *socket, const QVariantMap &response) const {
	if (!socket) {
		return;
	}

	QJsonObject object = QJsonObject::fromVariantMap(response);
	const QByteArray payload = QJsonDocument(object).toJson(QJsonDocument::Compact) + '\n';
	socket->write(payload);
	socket->flush();
}

bool ModernUiAutomationServer::authorizeRequest(const QVariantMap &request, QVariantMap &response) const {
	if (m_token.isEmpty()) {
		return true;
	}

	if (request.value(QStringLiteral("token")).toString() == m_token) {
		return true;
	}

	response = errorResponse(tr("Unauthorized automation request."));
	return false;
}
