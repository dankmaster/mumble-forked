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
#include "QmlClientModels.h"
#include "QmlAccessibilitySnapshot.h"
#include "QmlPerformanceMonitor.h"
#include "QmlShellHost.h"
#include "QmlVisualFixtureController.h"
#include "MumbleConstants.h"
#include "Net.h"
#include "OSInfo.h"
#include "PersistentChatController.h"
#include "Version.h"
#include "VersionCheck.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDir>
#include <QtCore/QEvent>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QPointer>
#include <QtCore/QReadLocker>
#include <QtCore/QTemporaryDir>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtGui/QClipboard>
#include <QtNetwork/QHostAddress>
#include <QtNetwork/QTcpServer>
#include <QtQuick/QQuickWindow>
#include <QtNetwork/QTcpSocket>
#include <QtWidgets/QApplication>
#include <QtWidgets/QWidget>

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
						automationReadonlyField(QObject::tr("User"), displayName),
						automationSelectField(QStringLiteral("history.scope"), QObject::tr("Scope"),
											  QStringLiteral("0:0"), scopeOptions, QStringLiteral("string")),
						automationSelectField(QStringLiteral("history.window"), QObject::tr("Window"), 5,
											  windowOptions),
						automationDialogField(QStringLiteral("history.customDays"), QObject::tr("Custom days"),
											  QStringLiteral("number"), 30) }) },
				QVariantList { cancel,
							   automationDialogAction(QStringLiteral("saveChatHistoryGrant"),
													  QObject::tr("Apply"), QStringLiteral("accent"), true) },
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
			const QVariantList formatOptions {
				automationSelectOption(QObject::tr("WAV"), 0),
				automationSelectOption(QObject::tr("FLAC"), 1),
				automationSelectOption(QObject::tr("Ogg Opus"), 2)
			};
			const QVariantList modeOptions {
				automationSelectOption(QObject::tr("Mixdown"), 0),
				automationSelectOption(QObject::tr("Multichannel"), 1),
				automationSelectOption(QObject::tr("Multichannel + transport"), 2, false),
				automationSelectOption(QObject::tr("Transport only"), 3, false)
			};
			QVariantMap formatField =
				automationSelectField(QStringLiteral("recording.format"), QObject::tr("Format"), 0, formatOptions);
			QVariantMap modeField =
				automationSelectField(QStringLiteral("recording.mode"), QObject::tr("Mode"), 0, modeOptions);
			if (active) {
				formatField.insert(QStringLiteral("enabled"), false);
				modeField.insert(QStringLiteral("enabled"), false);
			}
			QVariantList recorderFields;
			if (active) {
				recorderFields = QVariantList {
					automationReadonlyField(QObject::tr("Status"), QObject::tr("Recording")),
					automationReadonlyField(QObject::tr("Elapsed"), QStringLiteral("00:03:42")),
					automationReadonlyField(QObject::tr("Current file"),
											QStringLiteral("Landing-2026-05-31-033746.wav")),
					automationReadonlyField(QObject::tr("Size"), QObject::tr("18.4 MB")),
					automationNoteField(QObject::tr("Recording mixdown audio from the current voice session."))
				};
			} else {
				recorderFields = QVariantList {
					automationReadonlyField(QObject::tr("Status"), QObject::tr("Idle")),
					automationReadonlyField(QObject::tr("Elapsed"), QStringLiteral("00:00:00")),
					automationNoteField(QObject::tr("Unable to start recording. Not connected to a server."))
				};
			}
			QVariantList actions { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QString(),
														  true),
								   automationDialogAction(QStringLiteral("refreshRecorder"), QObject::tr("Refresh"),
														  QString(), false) };
			if (active) {
				actions.push_back(automationDialogAction(QStringLiteral("pauseRecording"), QObject::tr("Pause"),
														 QString(), false));
				actions.push_back(automationDialogAction(QStringLiteral("stopRecording"), QObject::tr("Stop"),
														 QStringLiteral("danger"), false));
			} else {
				actions.push_back(automationDialogAction(QStringLiteral("startRecording"), QObject::tr("Start"),
														 QStringLiteral("accent"), false));
			}
			return automationDialogFromSections(
				active ? QStringLiteral("voiceRecorderActive") : QStringLiteral("voiceRecorder"),
				QStringLiteral("form"), QObject::tr("Voice recorder"),
				active ? QObject::tr("Recording is in progress from the Modern shell.")
					   : QObject::tr("Record the current session from the Modern shell."),
				QVariantList {
					automationSection(QObject::tr("Recorder"), recorderFields),
					automationSection(
						QObject::tr("Output"),
						QVariantList {
							automationPathPickerField(QStringLiteral("recording.path"),
													  QObject::tr("Target directory"),
													  QStringLiteral("C:/Recordings"),
													  QStringLiteral("browseRecordingDirectory"),
													  QObject::tr("Browse"), !active),
							automationDialogField(QStringLiteral("recording.file"), QObject::tr("Filename"),
												  QStringLiteral("text"), QStringLiteral("%user-%date"), !active),
							formatField,
							modeField }) },
				actions, active ? QStringLiteral("stopRecording") : QStringLiteral("startRecording"), QString(),
				active ? QSize(760, 700) : QSize(760, 620));
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
			const QVariantList createModeOptions {
				automationSelectOption(QObject::tr("Create with name and email"), 0),
				automationSelectOption(QObject::tr("Quick create"), 1),
				automationSelectOption(QObject::tr("Import PKCS#12"), 2),
				automationSelectOption(QObject::tr("Export current certificate"), 3),
			};
			const QVariantList importModeOptions {
				automationSelectOption(QObject::tr("Import PKCS#12"), 0),
				automationSelectOption(QObject::tr("Quick create"), 1),
				automationSelectOption(QObject::tr("Create with name and email"), 2),
				automationSelectOption(QObject::tr("Export current certificate"), 3),
			};
			const QVariantList exportModeOptions {
				automationSelectOption(QObject::tr("Export current certificate"), 0),
				automationSelectOption(QObject::tr("Quick create"), 1),
				automationSelectOption(QObject::tr("Create with name and email"), 2),
				automationSelectOption(QObject::tr("Import PKCS#12"), 3),
			};

			QVariantMap fingerprint =
				automationDialogField(QStringLiteral("cert.fingerprint"),
									  QObject::tr("SHA-1 fingerprint"), QStringLiteral("readonly"),
									  QStringLiteral("7B:51:90:1E:3A:76:44:9C:DA:EE:20:78:33:8F:99:91:2D:14:5E:A4"),
									  false);
			fingerprint.insert(QStringLiteral("monospace"), true);

			QVariantList actionFields;
			if (emailError) {
				actionFields = QVariantList {
					automationSelectField(QStringLiteral("cert.mode"), QObject::tr("Action"),
										  0, createModeOptions),
					automationDialogField(QStringLiteral("cert.name"), QObject::tr("Name"),
										  QStringLiteral("text"), QObject::tr("Design Review")),
					automationDialogField(QStringLiteral("cert.email"), QObject::tr("Email"),
										  QStringLiteral("text"), QStringLiteral("not an email address"))
				};
			} else if (importError) {
				actionFields = QVariantList {
					automationSelectField(QStringLiteral("cert.mode"), QObject::tr("Action"),
										  0, importModeOptions),
					automationPathPickerField(QStringLiteral("cert.importPath"),
											  QObject::tr("Import file"),
											  QStringLiteral("C:/missing/mumble-cert.p12"),
											  QStringLiteral("browseCertificateImport"),
											  QObject::tr("Browse")),
					automationDialogField(QStringLiteral("cert.password"), QObject::tr("Import password"),
										  QStringLiteral("password"), QString())
				};
			} else {
				actionFields = QVariantList {
					automationSelectField(QStringLiteral("cert.mode"), QObject::tr("Action"),
										  0, exportModeOptions),
					automationPathPickerField(QStringLiteral("cert.exportPath"),
											  QObject::tr("Export file"),
											  QStringLiteral("C:/Users/You/Desktop/mumble-cert.p12"),
											  QStringLiteral("browseCertificateExport"),
											  QObject::tr("Browse"))
				};
			}

			const QString dialogID = emailError  ? QStringLiteral("certificateEmailError")
									 : importError ? QStringLiteral("certificateImportError")
												   : QStringLiteral("certificate");
			QVariantMap dialog = automationDialogFromSections(
				dialogID, QStringLiteral("certificate"), QObject::tr("Certificate"),
				emailError  ? QObject::tr("Review the certificate details before generating a new identity.")
				: importError ? QObject::tr("Choose a readable PKCS#12 certificate before importing.")
							  : QObject::tr("Manage the client certificate used for account identity and server authentication."),
				QVariantList {
					automationSection(
						QObject::tr("Current certificate"),
						QVariantList {
							automationDialogField(QStringLiteral("cert.status"), QObject::tr("Status"),
												  QStringLiteral("readonly"),
												  QObject::tr("A valid certificate is installed."), false),
							automationDialogField(QStringLiteral("cert.name"), QObject::tr("Name"),
												  QStringLiteral("readonly"), QObject::tr("You"), false),
							automationDialogField(QStringLiteral("cert.email"), QObject::tr("Email"),
												  QStringLiteral("readonly"), QObject::tr("None"), false),
							automationDialogField(QStringLiteral("cert.issuer"), QObject::tr("Issuer"),
												  QStringLiteral("readonly"), QObject::tr("You"), false),
							automationDialogField(QStringLiteral("cert.expires"), QObject::tr("Expires"),
												  QStringLiteral("readonly"),
												  QStringLiteral("2042-04-06T11:51:48"), false),
							fingerprint },
						QStringLiteral("certificate-current")),
					automationSection(
						QObject::tr("Certificate action"),
						actionFields,
						QStringLiteral("certificate-action"),
						QObject::tr("Choose one operation. The form only shows fields needed for that operation.")) },
				QVariantList { automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QString(), true),
							   automationDialogAction(QStringLiteral("applyCertificate"), QObject::tr("Apply"),
													  QStringLiteral("accent"), true) },
				QStringLiteral("applyCertificate"), QString(), QSize(820, 680));
			dialog.insert(QStringLiteral("highlights"),
						  QVariantList { automationHighlight(QObject::tr("Status"), QObject::tr("Installed"),
															 QStringLiteral("good")),
										 automationHighlight(QObject::tr("Action"), QObject::tr("Export")),
										 automationHighlight(QObject::tr("Expires"), QStringLiteral("2042-04-06")) });
			if (emailError) {
				dialog.insert(QStringLiteral("errors"),
							  QVariantMap { { QStringLiteral("cert.email"),
											  QObject::tr("Enter a valid email address or leave it blank.") } });
				dialog.insert(QStringLiteral("highlights"),
							  QVariantList { automationHighlight(QObject::tr("Status"), QObject::tr("Installed"),
																 QStringLiteral("good")),
											 automationHighlight(QObject::tr("Action"), QObject::tr("Create")),
											 automationHighlight(QObject::tr("Validation"), QObject::tr("Email"),
																 QStringLiteral("warning")) });
			} else if (importError) {
				dialog.insert(QStringLiteral("errors"),
							  QVariantMap { { QStringLiteral("cert.importPath"),
											  QObject::tr("Choose a readable PKCS#12 certificate file.") } });
				dialog.insert(QStringLiteral("highlights"),
							  QVariantList { automationHighlight(QObject::tr("Status"), QObject::tr("Installed"),
																 QStringLiteral("good")),
											 automationHighlight(QObject::tr("Action"), QObject::tr("Import")),
											 automationHighlight(QObject::tr("Validation"), QObject::tr("File"),
																 QStringLiteral("warning")) });
			}
			return dialog;
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
		if (variant == QLatin1String("manualPlugin")) {
			return automationDialogFromSections(
				QStringLiteral("manualPlugin"), QStringLiteral("form"), QObject::tr("Manual positional audio"),
				QObject::tr("Inspect and reset the manually supplied positional state."),
				QVariantList { automationSection(QObject::tr("Position"), QVariantList {
					automationDialogField(QStringLiteral("manual.x"), QStringLiteral("X"), QStringLiteral("number"), 1.25),
					automationDialogField(QStringLiteral("manual.y"), QStringLiteral("Y"), QStringLiteral("number"), 0.5),
					automationDialogField(QStringLiteral("manual.z"), QStringLiteral("Z"), QStringLiteral("number"), -2.0),
					automationReadonlyField(QObject::tr("Context"), QStringLiteral("automation-room")),
					automationReadonlyField(QObject::tr("Identity"), QStringLiteral("automation-user")),
					automationDialogField(QStringLiteral("manual.active"), QObject::tr("Active"), QStringLiteral("checkbox"), true),
					QVariantMap { { QStringLiteral("id"), QStringLiteral("manual.preview") },
								  { QStringLiteral("type"), QStringLiteral("manualPositionPreview") } } }) },
				QVariantList { automationDialogAction(QStringLiteral("reset"), QObject::tr("Reset"), QStringLiteral("warning"), false),
							   automationDialogAction(QStringLiteral("close"), QObject::tr("Close"), QString(), true) },
				QStringLiteral("close"), QString(), QSize(760, 660));
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

		if (action == QLatin1String("setTickerBannerAlwaysScroll")) {
			summary.insert(QStringLiteral("serverAction"), QStringLiteral("setTickerBannerAlwaysScroll"));
			summary.insert(QStringLiteral("protoAction"), QStringLiteral("localSettings"));
			summary.insert(QStringLiteral("tickerBannerAlwaysScroll"),
						   payload.value(QStringLiteral("tickerBannerAlwaysScroll")).toBool());
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

	QVariantMap automationMotdSettingsState(MainWindow *window) {
		QVariantMap state;
		state.insert(QStringLiteral("expanded"), Global::get().s.bModernShellMotdExpanded);
		state.insert(QStringLiteral("dismissedSignature"),
					 Global::get().s.qsModernShellMotdDismissedSignature);
		state.insert(QStringLiteral("lastSeenSignature"),
					 Global::get().s.qsModernShellMotdLastSeenSignature);

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

	void restoreAutomationMotdSettings(MainWindow *window, const bool expanded, const QString &dismissedSignature,
									   const QString &lastSeenSignature) {
		Global::get().s.bModernShellMotdExpanded = expanded;
		Global::get().s.qsModernShellMotdDismissedSignature = dismissedSignature;
		Global::get().s.qsModernShellMotdLastSeenSignature = lastSeenSignature;
		Global::get().s.save();
		if (window) {
			window->scheduleQmlShellStateSyncImmediate();
		}
	}

	QVariantMap automationMotdActionDryRun(MainWindow *window) {
		QVariantMap result;
		if (!window) {
			result.insert(QStringLiteral("error"), QObject::tr("Main window is not available."));
			return result;
		}

		const bool originalExpanded = Global::get().s.bModernShellMotdExpanded;
		const QString originalDismissed = Global::get().s.qsModernShellMotdDismissedSignature;
		const QString originalLastSeen = Global::get().s.qsModernShellMotdLastSeenSignature;
		const QString signature = QStringLiteral("automation-motd-persistence");

		const auto restoreOriginal = [&]() {
			restoreAutomationMotdSettings(window, originalExpanded, originalDismissed, originalLastSeen);
		};

		const bool hideHandled = window->handleModernShellAppAction(QStringLiteral("motd.hide"));
		result.insert(QStringLiteral("hideHandled"), hideHandled);
		result.insert(QStringLiteral("hidePersisted"), !Global::get().s.bModernShellMotdExpanded);

		const bool showHandled = window->handleModernShellAppAction(QStringLiteral("motd.show"));
		result.insert(QStringLiteral("showHandled"), showHandled);
		result.insert(QStringLiteral("showPersisted"), Global::get().s.bModernShellMotdExpanded);

		const QVariantMap payload { { QStringLiteral("signature"), signature } };
		const bool dismissHandled = window->handleModernShellAppActionPayload(QStringLiteral("motd.dismiss"), payload);
		result.insert(QStringLiteral("dismissHandled"), dismissHandled);
		result.insert(QStringLiteral("dismissPersisted"),
					  Global::get().s.qsModernShellMotdDismissedSignature == signature);
		result.insert(QStringLiteral("dismissMarksSeen"),
					  Global::get().s.qsModernShellMotdLastSeenSignature == signature);

		const bool restoreHandled = window->handleModernShellAppActionPayload(QStringLiteral("motd.restore"), payload);
		result.insert(QStringLiteral("restoreHandled"), restoreHandled);
		result.insert(QStringLiteral("restorePersisted"),
					  Global::get().s.qsModernShellMotdDismissedSignature.isEmpty());

		const bool markSeenHandled = window->handleModernShellAppActionPayload(QStringLiteral("motd.markSeen"), payload);
		result.insert(QStringLiteral("markSeenHandled"), markSeenHandled);
		result.insert(QStringLiteral("markSeenPersisted"),
					  Global::get().s.qsModernShellMotdLastSeenSignature == signature);

		restoreOriginal();
		result.insert(QStringLiteral("restoredOriginal"),
					  Global::get().s.bModernShellMotdExpanded == originalExpanded
						  && Global::get().s.qsModernShellMotdDismissedSignature == originalDismissed
						  && Global::get().s.qsModernShellMotdLastSeenSignature == originalLastSeen);
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
		const QString safeTitle = title.toHtmlEscaped();
		const QString safeSubtitle = subtitle.toHtmlEscaped();
		const QString safeAccent = accent.trimmed().isEmpty() ? QStringLiteral("#51c8b3") : accent.trimmed();
		const QString safeAccent2 = accent2.trimmed().isEmpty() ? QStringLiteral("#78b7d9") : accent2.trimmed();
		const QString svg =
			QStringLiteral(
				"<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"960\" height=\"540\" viewBox=\"0 0 960 540\">"
				"<defs><linearGradient id=\"g\" x1=\"0\" y1=\"0\" x2=\"1\" y2=\"1\">"
				"<stop offset=\"0\" stop-color=\"%1\"/><stop offset=\"1\" stop-color=\"%2\"/>"
				"</linearGradient></defs>"
				"<rect width=\"960\" height=\"540\" fill=\"#101823\"/>"
				"<rect x=\"32\" y=\"32\" width=\"896\" height=\"476\" rx=\"28\" fill=\"url(#g)\" opacity=\"0.9\"/>"
				"<circle cx=\"774\" cy=\"128\" r=\"86\" fill=\"#ffffff\" opacity=\"0.14\"/>"
				"<circle cx=\"166\" cy=\"418\" r=\"118\" fill=\"#000000\" opacity=\"0.18\"/>"
				"<text x=\"72\" y=\"266\" font-family=\"Segoe UI, Arial, sans-serif\" font-size=\"54\" "
				"font-weight=\"700\" fill=\"#ffffff\">%3</text>"
				"<text x=\"76\" y=\"326\" font-family=\"Segoe UI, Arial, sans-serif\" font-size=\"26\" "
				"fill=\"#dff8ff\" opacity=\"0.92\">%4</text>"
				"</svg>")
				.arg(safeAccent, safeAccent2, safeTitle, safeSubtitle);
		return QStringLiteral("data:image/svg+xml;charset=UTF-8,")
			   + QString::fromLatin1(QUrl::toPercentEncoding(svg));
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
		if (normalized == QLatin1String("compact") || normalized == QLatin1String("large")
			|| normalized == QLatin1String("default")) {
			preview.insert(QStringLiteral("previewSize"), normalized);
		}
	}

	QVariantList automationRichPreviewProbeMessages(const QString &variant, const QString &requestedSize) {
		const QString normalizedVariant = variant.trimmed().toLower();
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
			preview.insert(QStringLiteral("mediaMime"), QStringLiteral("image/svg+xml"));
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
			preview.insert(QStringLiteral("mediaMime"), QStringLiteral("image/svg+xml"));
			preview.insert(QStringLiteral("openLabel"), QObject::tr("Open link"));
			automationApplyPreviewSize(preview, size);
			return QVariantList { automationRichPreviewMessage(messageID + 5, actor, bodyText, preview) };
		}

		return {};
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
	if (!host || !host->chatModel()) {
		response.insert(QStringLiteral("ok"), false);
		response.insert(QStringLiteral("error"), tr("The Qt Quick chat fixture host disappeared before restore."));
		return response;
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

QVariantMap ModernUiAutomationServer::finalizeTalkPerformanceWorkload(QmlShellHost *host) {
	QVariantMap response = okResponse();
	if (!m_talkPerformanceWorkload.active) {
		response.insert(QStringLiteral("restored"), true);
		return response;
	}
	if (!host || !host->participantModel()) {
		response.insert(QStringLiteral("ok"), false);
		response.insert(QStringLiteral("error"), tr("The Qt Quick talk fixture host disappeared before restore."));
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
			ChatTimelineModel *chat = host->chatModel();
			m_chatPerformanceWorkload.liveMessages = chat->messages();
			m_chatPerformanceWorkload.previousFixtureOverride = host->visualFixtureOverrideActive();
			host->setVisualFixtureOverrideActive(true);
			m_chatPerformanceWorkload.active = true;
			QVariantList messages;
			messages.reserve(96);
			for (int index = 0; index < 96; ++index) {
				messages.push_back(QVariantMap {
					{ QStringLiteral("messageKey"), QStringLiteral("qml-perf-message-%1").arg(index) },
					{ QStringLiteral("actor"), index % 2 ? QStringLiteral("Performance peer") : QStringLiteral("Performance self") },
					{ QStringLiteral("bodyText"), QStringLiteral("Deterministic chat workload row %1 %2").arg(index).arg(QString(80, QLatin1Char('x'))) },
					{ QStringLiteral("timeLabel"), QStringLiteral("12:%1").arg(index % 60, 2, 10, QLatin1Char('0')) },
					{ QStringLiteral("deliveryState"), QStringLiteral("sent") }, { QStringLiteral("own"), index % 2 == 0 }
				});
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
									 && frames > m_chatPerformanceWorkload.presentedFramesBeforeSeed);
			return response;
		} else if (command == QLatin1String("qmlPerformanceChatScrollStart")) {
			if (!m_chatPerformanceWorkload.active) return errorResponse(tr("No chat performance fixture is active."));
			QVariant started;
			if (!QMetaObject::invokeMethod(host->window(), "runPerformanceChatScrollWorkload", Q_RETURN_ARG(QVariant, started)))
				return errorResponse(tr("The Qt Quick root does not expose the chat-scroll workload."));
			QVariantMap response = okResponse();
			response.insert(QStringLiteral("scroll"), started.toMap());
			return response;
		} else if (command == QLatin1String("qmlPerformanceChatScrollStatus")) {
			QVariant state;
			if (!QMetaObject::invokeMethod(host->window(), "performanceChatScrollState", Q_RETURN_ARG(QVariant, state)))
				return errorResponse(tr("The Qt Quick root does not expose chat-scroll status."));
			QVariantMap response = okResponse();
			response.insert(QStringLiteral("scroll"), state.toMap());
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
		} else if (command == QLatin1String("qmlPerformanceTalkTransition")) {
			if (!m_talkPerformanceWorkload.active)
				return errorResponse(tr("No talk-state performance fixture is active."));
			m_talkPerformanceWorkload.talking = !m_talkPerformanceWorkload.talking;
			const bool talking = m_talkPerformanceWorkload.talking;
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
		const QVariantMap snapshot = QmlAccessibilitySnapshot::serialize(host->window());
		if (!snapshot.value(QStringLiteral("ok")).toBool()) {
			return errorResponse(snapshot.value(QStringLiteral("error")).toString());
		}
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("generation"), requestedGeneration);
		response.insert(QStringLiteral("snapshot"), snapshot.value(QStringLiteral("tree")));
		response.insert(QStringLiteral("nodeCount"), snapshot.value(QStringLiteral("nodeCount")));
		response.insert(QStringLiteral("truncated"), snapshot.value(QStringLiteral("truncated")));
		return response;
	}

	if (command == QLatin1String("captureQml")) {
		if (!m_mainWindow->m_qmlShellHost || !m_mainWindow->m_qmlShellHost->window()) {
			return errorResponse(tr("The Qt Quick frontend is not active."));
		}
		const QString path = request.value(QStringLiteral("path")).toString().trimmed();
		if (path.isEmpty()) return errorResponse(tr("Missing capture path."));
		bool parsedGeneration = false;
		const qulonglong requestedGeneration =
			unsignedLongLongValue(request.value(QStringLiteral("generation")), &parsedGeneration);
		if (request.contains(QStringLiteral("generation"))
			&& (!parsedGeneration || requestedGeneration == 0
				|| requestedGeneration != visualFixtureController()->generation())) {
			return errorResponse(tr("The requested visual fixture generation is stale or invalid."));
		}
		QString captureError;
		if (!m_mainWindow->m_qmlShellHost->captureWindow(path, &captureError)) {
			return errorResponse(captureError);
		}
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("path"), QFileInfo(path).absoluteFilePath());
		response.insert(QStringLiteral("frontend"), QStringLiteral("qml"));
		if (parsedGeneration) response.insert(QStringLiteral("generation"), requestedGeneration);
		return response;
	}

	if (command == QLatin1String("setQmlPttTool")) {
		if (!m_mainWindow->m_qmlShellHost || !m_mainWindow->m_qmlShellHost->window()) {
			return errorResponse(tr("The Qt Quick frontend is not active."));
		}
		m_mainWindow->m_qmlShellHost->showPttTool(request.value(QStringLiteral("visible")).toBool());
		return okResponse();
	}

	if (command == QLatin1String("setQmlPttPressed")) {
		if (!m_mainWindow->m_qmlShellHost || !m_mainWindow->m_qmlShellHost->window()) {
			return errorResponse(tr("The Qt Quick frontend is not active."));
		}
		m_mainWindow->m_qmlShellHost->commandController()->setPttPressed(
			request.value(QStringLiteral("pressed")).toBool());
		return okResponse();
	}

	if (command == QLatin1String("openQmlMediaSession")) {
		if (!m_mainWindow->m_qmlShellHost || !m_mainWindow->m_qmlShellHost->window()) {
			return errorResponse(tr("The Qt Quick frontend is not active."));
		}
		const bool opened = m_mainWindow->m_qmlShellHost->mediaSession()->open(
			QUrl(request.value(QStringLiteral("url")).toString()),
			request.value(QStringLiteral("provider")).toString(),
			request.value(QStringLiteral("sessionId")).toString());
		return opened ? okResponse() : errorResponse(m_mainWindow->m_qmlShellHost->mediaSession()->error());
	}

	if (command == QLatin1String("closeQmlMediaSession")) {
		if (m_mainWindow->m_qmlShellHost) m_mainWindow->m_qmlShellHost->mediaSession()->close();
		return okResponse();
	}

	if (command == QLatin1String("qmlReadinessState")) {
		QmlShellHost *host = m_mainWindow->qmlShellHost();
		if (!host || !host->window()) return errorResponse(tr("The Qt Quick frontend is not active."));
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("frontend"), QStringLiteral("qml"));
		response.insert(QStringLiteral("windowReady"), true);
		response.insert(QStringLiteral("connected"), host->sessionController()->connected());
		response.insert(QStringLiteral("activeScopeToken"), host->activeScopeController()->scopeToken());
		response.insert(QStringLiteral("roomCount"), host->roomModel()->rowCount());
		response.insert(QStringLiteral("participantCount"), host->participantModel()->rowCount());
		response.insert(QStringLiteral("messageCount"), host->chatModel()->rowCount());
		response.insert(QStringLiteral("dialogOpen"), host->dialogController()->open());
		response.insert(QStringLiteral("pttPressed"), host->commandController()->pttPressed());
		response.insert(QStringLiteral("mediaActive"), host->mediaSession()->active());
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
		MediaSessionBackend *media = host->mediaSession();
		media->close();
		const QUrl url(QStringLiteral("https://example.com/watch-together"));
		if (!media->open(url, QStringLiteral("direct"), QStringLiteral("automation-room")))
			return errorResponse(media->error());
		const qulonglong openedGeneration = media->syncGeneration();
		media->applyRemoteState(url, QStringLiteral("direct"), QStringLiteral("automation-room"), 12.5, false,
							 openedGeneration + 1);
		QVariantMap response = okResponse();
		response.insert(QStringLiteral("opened"), media->active());
		response.insert(QStringLiteral("state"), media->state());
		response.insert(QStringLiteral("position"), media->position());
		response.insert(QStringLiteral("generationAdvanced"), media->syncGeneration() > openedGeneration);
		media->close();
		response.insert(QStringLiteral("closed"), !media->active());
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

		if (async) {
			scheduleAction([dialogID, context](MainWindow *window) { window->handleModernDialogOpen(dialogID, context); });
			return asyncResponse();
		}

		m_mainWindow->handleModernDialogOpen(dialogID, context);
		return okResponse();
	}

	if (command == QLatin1String("closeDialog")) {
		const QString dialogID = request.value(QStringLiteral("dialogId")).toString();
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
		const QVariantMap state = m_mainWindow->m_modernDialogController ? m_mainWindow->m_modernDialogController->state() : QVariantMap();
		QVariantMap fieldState;
		for (const QVariant &sectionValue : state.value(QStringLiteral("sections")).toList()) {
			for (const QVariant &fieldValue : sectionValue.toMap().value(QStringLiteral("fields")).toList()) {
				const QVariantMap field = fieldValue.toMap();
				if (field.value(QStringLiteral("id")).toString() == fieldID) { fieldState = field; break; }
			}
		}
		fieldState.insert(QStringLiteral("exists"), !fieldState.isEmpty());
		QVariantMap response = okResponse(); response.insert(QStringLiteral("field"), fieldState); return response;
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
		m_mainWindow->handleModernDialogFieldUpdate(state.value(QStringLiteral("id")).toString(), fieldID, request.value(QStringLiteral("value")));
		QVariantMap response = okResponse(); response.insert(QStringLiteral("fieldId"), fieldID); return response;
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

	if (command == QLatin1String("openAppDialogProbe") || command == QLatin1String("openLifecycleDialogProbe")) {
		const QString variant  = request.value(command == QLatin1String("openLifecycleDialogProbe")
												 ? QStringLiteral("flow") : QStringLiteral("variant")).toString().trimmed();
		const QString userName = request.value(QStringLiteral("userName")).toString().trimmed();
		if (variant.isEmpty()) {
			return errorResponse(tr("Missing variant."));
		}
		const QVariantMap dialog = automationAppDialogProbe(variant, userName);
		if (dialog.isEmpty()) {
			return errorResponse(tr("Unknown app dialog probe '%1'.").arg(variant));
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
			window->m_stonksState = state;
			window->scheduleQmlShellStateSyncImmediate();
		};

		if (async) {
			scheduleAction(applyProbe);
			return asyncResponse();
		}

		applyProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("clearStonksProbe")) {
		const auto clearProbe = [](MainWindow *window) {
			window->m_stonksState.clear();
			window->scheduleQmlShellStateSyncImmediate();
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
			window->m_modernConnectionStateProbe = state;
			window->scheduleQmlShellStateSyncImmediate();
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
			window->m_modernConnectionStateProbe.clear();
			window->scheduleQmlShellStateSyncImmediate();
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
			window->m_modernScreenShareStateProbe = state;
			window->scheduleQmlShellStateSyncImmediate();
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
			window->m_modernScreenShareStateProbe.clear();
			window->scheduleQmlShellStateSyncImmediate();
		};

		if (async) {
			scheduleAction(clearProbe);
			return asyncResponse();
		}

		clearProbe(m_mainWindow);
		return okResponse();
	}

	if (command == QLatin1String("setRichPreviewProbe")) {
		const QString variant = request.value(QStringLiteral("variant")).toString().trimmed();
		const QString size    = request.value(QStringLiteral("size")).toString().trimmed();
		if (variant.isEmpty()) {
			return errorResponse(tr("Missing variant."));
		}
		const QVariantList messages = automationRichPreviewProbeMessages(variant, size);
		if (messages.isEmpty()) {
			return errorResponse(tr("Unknown rich preview probe '%1'.").arg(variant));
		}

		const auto applyProbe = [messages](MainWindow *window) {
			window->m_modernRichPreviewProbeMessages = messages;
			window->scheduleQmlShellStateSyncImmediate();
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

	if (command == QLatin1String("clearRichPreviewProbe")) {
		const auto clearProbe = [](MainWindow *window) {
			window->m_modernRichPreviewProbeMessages.clear();
			window->scheduleQmlShellStateSyncImmediate();
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
			window->m_modernMessageDeliveryProbeMessages = messages;
			window->scheduleQmlShellStateSyncImmediate();
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
			window->m_modernMessageDeliveryProbeMessages.clear();
			window->scheduleQmlShellStateSyncImmediate();
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
			const bool selected = window->handleModernShellScopeSelection(scopeToken);
			window->publishQmlDirectMessagesState();
			window->publishQmlActiveScopeState();
			window->scheduleQmlShellStateSyncImmediate();
			return selected;
		};

		if (async) {
			scheduleAction([applyProbe](MainWindow *window) { applyProbe(window); });
			QVariantMap response = asyncResponse();
			response.insert(QStringLiteral("session"), static_cast< qulonglong >(session));
			response.insert(QStringLiteral("variant"), variant);
			response.insert(QStringLiteral("syntheticPeer"), syntheticPeer);
			return response;
		}

		QVariantMap response = okResponse();
		response.insert(QStringLiteral("handled"), applyProbe(m_mainWindow));
		response.insert(QStringLiteral("session"), static_cast< qulonglong >(session));
		response.insert(QStringLiteral("variant"), variant);
		response.insert(QStringLiteral("syntheticPeer"), syntheticPeer);
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
			window->scheduleQmlShellStateSyncImmediate();
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
		restoreAutomationMotdSettings(
			m_mainWindow, state.value(QStringLiteral("expanded")).toBool(),
			state.value(QStringLiteral("dismissedSignature")).toString(),
			state.value(QStringLiteral("lastSeenSignature")).toString());

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

QVariantMap ModernUiAutomationServer::buildStateResponse() const {
	QVariantMap response = okResponse();
	QmlShellHost *host = m_mainWindow ? m_mainWindow->m_qmlShellHost.get() : nullptr;
	QVariantMap state;
	if (host) {
		ClientSessionController *session = host->sessionController();
		UiCommandController *commands = host->commandController();
		ActiveScopeController *scope = host->activeScopeController();
		QmlSelectionState *selection = host->selectionState();
		state.insert(QStringLiteral("app"),
					 QVariantMap { { QStringLiteral("serverTitle"), session->serverName() },
								   { QStringLiteral("selfName"), session->selfName() },
								   { QStringLiteral("selfStatusLabel"), session->connectionLabel() },
								   { QStringLiteral("selfMuted"), session->selfMuted() },
								   { QStringLiteral("selfDeafened"), session->selfDeafened() },
								   { QStringLiteral("connected"), session->connected() },
								   { QStringLiteral("updateBanner"), session->updateBanner() },
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
									{ QStringLiteral("loadingState"), scope->loadingState() } };
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
		state.insert(QStringLiteral("activeScope"), activeScopeState);
		state.insert(QStringLiteral("selection"),
					 QVariantMap { { QStringLiteral("scopeToken"), selection->scopeToken() },
								   { QStringLiteral("selectedUserSession"), selection->selectedUserSession() },
								   { QStringLiteral("selectedVoiceChannelId"), selection->selectedVoiceChannelId() } });

		QVariantList voiceRooms;
		QVariantList textRooms;
		RoomModel *rooms = host->roomModel();
		for (int row = 0; row < rooms->rowCount(); ++row) {
			const QVariantMap modelRow = rooms->get(row);
			QVariantMap item = modelRow.value(QStringLiteral("source")).toMap();
			if (item.isEmpty()) item = modelRow;
			if (!item.contains(QStringLiteral("token"))) item.insert(QStringLiteral("token"), modelRow.value(QStringLiteral("id")));
			(modelRow.value(QStringLiteral("kind")).toString() == QLatin1String("voice") ? voiceRooms : textRooms)
				.push_back(item);
		}
		state.insert(QStringLiteral("voiceRooms"), voiceRooms);
		state.insert(QStringLiteral("textRooms"), textRooms);

		const auto modelRows = [](StableListModel *model, const bool unwrapSource) {
			QVariantList rows;
			for (int row = 0; row < model->rowCount(); ++row) {
				const QVariantMap modelRow = model->get(row);
				const QVariantMap source = unwrapSource ? modelRow.value(QStringLiteral("source")).toMap() : QVariantMap();
				rows.push_back(source.isEmpty() ? modelRow : source);
			}
			return rows;
		};
		state.insert(QStringLiteral("participants"), modelRows(host->participantModel(), true));
		state.insert(QStringLiteral("messages"), modelRows(host->chatModel(), true));
		state.insert(QStringLiteral("actions"), modelRows(host->actionModel(), false));
		state.insert(QStringLiteral("dialog"), host->dialogController()->state());
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
