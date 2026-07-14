// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernDialogController.h"

#include "ModernShellMenuSerializer.h"

#include <QtCore/QObject>

namespace {
	QVariantMap closedState() {
		QVariantMap state;
		state.insert(QStringLiteral("open"), false);
		return state;
	}

	QVariantMap fieldItem(const QString &id, const QString &label, const QString &type, const QVariant &value) {
		QVariantMap field;
		field.insert(QStringLiteral("id"), id);
		field.insert(QStringLiteral("label"), label);
		field.insert(QStringLiteral("type"), type);
		field.insert(QStringLiteral("value"), value);
		return field;
	}

	QVariantMap sectionItem(const QString &title, const QVariantList &fields) {
		QVariantMap section;
		section.insert(QStringLiteral("title"), title);
		section.insert(QStringLiteral("fields"), fields);
		return section;
	}

	QVariantMap noteItem(const QString &text) {
		QVariantMap field;
		field.insert(QStringLiteral("type"), QStringLiteral("note"));
		field.insert(QStringLiteral("text"), text);
		return field;
	}

	QVariantMap hiddenItem(const QString &id, const QVariant &value) {
		return fieldItem(id, QString(), QStringLiteral("hidden"), value);
	}

	QVariantMap readonlyItem(const QString &label, const QVariant &value) {
		QVariantMap field = fieldItem(QString(), label, QStringLiteral("readonly"), value);
		field.insert(QStringLiteral("enabled"), false);
		return field;
	}

	QVariantMap checkboxItem(const QString &id, const QString &label, const bool value) {
		QVariantMap field = fieldItem(id, label, QStringLiteral("checkbox"), value);
		field.insert(QStringLiteral("enabled"), true);
		return field;
	}

	QVariantMap pathPickerItem(const QString &id, const QString &label, const QString &value,
							   const QString &browseActionID, const QString &browseLabel) {
		QVariantMap field = fieldItem(id, label, QStringLiteral("pathPicker"), value);
		field.insert(QStringLiteral("enabled"), true);
		field.insert(QStringLiteral("browseActionId"), browseActionID);
		field.insert(QStringLiteral("browseLabel"), browseLabel);
		return field;
	}

	QVariantMap dialogActionItem(const QString &id, const QString &label, const QString &tone = QString(),
								 const bool closesDialog = false) {
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

	void collectFieldValues(const QVariantList &sections, QVariantMap &values) {
		for (const QVariant &sectionValue : sections) {
			const QVariantMap section = sectionValue.toMap();
			for (const QVariant &fieldValue : section.value(QStringLiteral("fields")).toList()) {
				const QVariantMap field = fieldValue.toMap();
				const QString id        = field.value(QStringLiteral("id")).toString();
				if (!id.isEmpty()) {
					values.insert(id, field.value(QStringLiteral("value")));
				}
			}
		}
	}

	bool updateFieldValue(QVariantList &sections, const QString &fieldID, const QVariant &value) {
		for (int sectionIndex = 0; sectionIndex < sections.size(); ++sectionIndex) {
			QVariantMap section = sections.at(sectionIndex).toMap();
			QVariantList fields = section.value(QStringLiteral("fields")).toList();
			for (int fieldIndex = 0; fieldIndex < fields.size(); ++fieldIndex) {
				QVariantMap field = fields.at(fieldIndex).toMap();
				if (field.value(QStringLiteral("id")).toString() != fieldID) {
					continue;
				}

				field.insert(QStringLiteral("value"), value);
				fields[fieldIndex] = field;
				section.insert(QStringLiteral("fields"), fields);
				sections[sectionIndex] = section;
				return true;
			}
		}

		return false;
	}

	QVariantMap findAction(const QVariantList &actions, const QString &actionID) {
		for (const QVariant &actionValue : actions) {
			const QVariantMap action = actionValue.toMap();
			if (action.value(QStringLiteral("id")).toString() == actionID) {
				return action;
			}
		}

		return {};
	}

	QString failedConnectionTitle(const QString &type) {
		if (type == QLatin1String("invalidUsername")) {
			return QObject::tr("Username rejected");
		}
		if (type == QLatin1String("usernameInUse")) {
			return QObject::tr("Username already in use");
		}
		if (type == QLatin1String("authenticationFailure")) {
			return QObject::tr("Authentication failed");
		}
		if (type == QLatin1String("invalidServerPassword")) {
			return QObject::tr("Server password rejected");
		}
		return QObject::tr("Connection failed");
	}

	QString failedConnectionMessage(const QString &type) {
		if (type == QLatin1String("invalidUsername")) {
			return QObject::tr("The server rejected this username. Change it and try reconnecting.");
		}
		if (type == QLatin1String("usernameInUse")) {
			return QObject::tr("Another connected user is already using this name.");
		}
		if (type == QLatin1String("authenticationFailure")) {
			return QObject::tr("Update the username or password, then reconnect.");
		}
		if (type == QLatin1String("invalidServerPassword")) {
			return QObject::tr("Enter the server password and reconnect.");
		}
		return QObject::tr("Review the connection details and try again.");
	}
} // namespace

QVariantMap ModernDialogController::openConnect(const QList< FavoriteServer > &favorites, const Settings &settings,
												const QMap< UnresolvedServerAddress, unsigned int > &pingCache) {
	m_connect.open(favorites, settings, pingCache);
	m_activeDialogID = QStringLiteral("connect");
	return state();
}

QVariantMap ModernDialogController::openSettings(const Settings &settings, const QString &pageName) {
	m_settings.open(settings, pageName);
	m_activeDialogID = QStringLiteral("settings");
	return state();
}

QVariantMap ModernDialogController::openFailedConnection(const QVariantMap &context) {
	m_failedConnection = context;
	m_activeDialogID   = QStringLiteral("failedConnection");
	return state();
}

QVariantMap ModernDialogController::openDisconnectConfirmation(const QString &serverLabel) {
	QVariantList fields {
		noteItem(QObject::tr("Voice, text, screen sharing, and room state will leave the current server."))
	};
	if (!serverLabel.trimmed().isEmpty()) {
		fields.insert(0, readonlyItem(QObject::tr("Server"), serverLabel.trimmed()));
	}

	QVariantMap dialog;
	dialog.insert(QStringLiteral("id"), QStringLiteral("disconnectServer"));
	dialog.insert(QStringLiteral("kind"), QStringLiteral("confirm"));
	dialog.insert(QStringLiteral("title"), QObject::tr("Disconnect"));
	dialog.insert(QStringLiteral("subtitle"), QObject::tr("Disconnect from this server?"));
	dialog.insert(QStringLiteral("sections"),
				  QVariantList { sectionItem(QObject::tr("Confirmation"), fields) });
	dialog.insert(QStringLiteral("actions"),
				  QVariantList { dialogActionItem(QStringLiteral("cancel"), QObject::tr("Cancel"), QString(), true),
								 dialogActionItem(QStringLiteral("confirmDisconnect"), QObject::tr("Disconnect"),
												  QStringLiteral("danger"), true) });
	dialog.insert(QStringLiteral("primaryActionId"), QStringLiteral("confirmDisconnect"));
	dialog.insert(QStringLiteral("tone"), QStringLiteral("danger"));
	dialog.insert(QStringLiteral("width"), 520);
	dialog.insert(QStringLiteral("height"), 260);
	return openGenericDialog(dialog);
}

QVariantMap ModernDialogController::openQuitConfirmation(const bool connected, const bool allowMinimize) {
	QVariantList fields {
		hiddenItem(QStringLiteral("quit.connected"), connected),
		hiddenItem(QStringLiteral("quit.allowMinimize"), allowMinimize),
		noteItem(connected ? QObject::tr("Quitting will disconnect from the current server and close Mumble.")
						   : QObject::tr("Mumble will close and stop running."))
	};
	if (allowMinimize) {
		fields.push_back(checkboxItem(QStringLiteral("quit.remember"), QObject::tr("Remember this setting"), false));
	}

	QVariantList actions { dialogActionItem(QStringLiteral("cancel"), QObject::tr("Cancel"), QString(), true) };
	if (allowMinimize) {
		actions.push_back(dialogActionItem(QStringLiteral("minimizeMumble"), QObject::tr("Minimize"),
										   QStringLiteral("accent"), true));
	}
	actions.push_back(dialogActionItem(QStringLiteral("confirmQuit"), QObject::tr("Quit Mumble"),
									   QStringLiteral("danger"), true));

	QVariantMap dialog;
	dialog.insert(QStringLiteral("id"), QStringLiteral("quitMumble"));
	dialog.insert(QStringLiteral("kind"), QStringLiteral("confirm"));
	dialog.insert(QStringLiteral("title"), QObject::tr("Quit Mumble"));
	dialog.insert(QStringLiteral("subtitle"), allowMinimize
					 ? QObject::tr("Quit Mumble or keep it running in the background?")
					 : QObject::tr("Are you sure you want to quit Mumble?"));
	dialog.insert(QStringLiteral("sections"),
				  QVariantList { sectionItem(QObject::tr("Confirmation"), fields) });
	dialog.insert(QStringLiteral("actions"), actions);
	dialog.insert(QStringLiteral("primaryActionId"), QStringLiteral("confirmQuit"));
	dialog.insert(QStringLiteral("tone"), QStringLiteral("danger"));
	dialog.insert(QStringLiteral("width"), allowMinimize ? 560 : 520);
	dialog.insert(QStringLiteral("height"), allowMinimize ? 320 : 280);
	return openGenericDialog(dialog);
}

QVariantMap ModernDialogController::openDeleteMessageConfirmation(const qulonglong messageID,
																  const QString &conversationLabel) {
	QVariantList fields {
		hiddenItem(QStringLiteral("message.id"), messageID),
		noteItem(QObject::tr("The message body, attachments, link previews, and reactions will be removed."))
	};
	if (!conversationLabel.trimmed().isEmpty()) {
		fields.insert(1, readonlyItem(QObject::tr("Conversation"), conversationLabel.trimmed()));
	}

	QVariantMap dialog;
	dialog.insert(QStringLiteral("id"), QStringLiteral("deleteMessage:%1").arg(messageID));
	dialog.insert(QStringLiteral("kind"), QStringLiteral("confirm"));
	dialog.insert(QStringLiteral("title"), QObject::tr("Delete message"));
	dialog.insert(QStringLiteral("subtitle"), QObject::tr("Delete this message from chat history?"));
	dialog.insert(QStringLiteral("sections"),
				  QVariantList { sectionItem(QObject::tr("Confirmation"), fields) });
	dialog.insert(QStringLiteral("actions"),
				  QVariantList { dialogActionItem(QStringLiteral("cancel"), QObject::tr("Cancel"), QString(), true),
								 dialogActionItem(QStringLiteral("confirmDeleteMessage"), QObject::tr("Delete"),
												  QStringLiteral("danger"), true) });
	dialog.insert(QStringLiteral("primaryActionId"), QStringLiteral("confirmDeleteMessage"));
	dialog.insert(QStringLiteral("tone"), QStringLiteral("danger"));
	dialog.insert(QStringLiteral("width"), 430);
	dialog.insert(QStringLiteral("height"), 230);
	return openGenericDialog(dialog);
}

QVariantMap ModernDialogController::openChangeAvatar(const unsigned int session, const QString &userName,
													 const QVariantMap &fieldValues, const QVariantMap &errors) {
	const QString displayName = userName.trimmed().isEmpty() ? QObject::tr("this user") : userName.trimmed();
	QVariantList fields {
		hiddenItem(QStringLiteral("session"), session),
		readonlyItem(QObject::tr("User"), displayName),
		pathPickerItem(QStringLiteral("avatar.path"), QObject::tr("Image file or URL"),
					   fieldValues.value(QStringLiteral("avatar.path")).toString(),
					   QStringLiteral("browseAvatarImage"), QObject::tr("Browse")),
		noteItem(QObject::tr("Choose a PNG or JPEG image from this device, or paste a direct http(s) image URL."))
	};

	QVariantMap dialog;
	dialog.insert(QStringLiteral("id"), QStringLiteral("changeAvatar:%1").arg(session));
	dialog.insert(QStringLiteral("kind"), QStringLiteral("form"));
	dialog.insert(QStringLiteral("title"), QObject::tr("Change Avatar"));
	dialog.insert(QStringLiteral("subtitle"), QObject::tr("Choose a new server-side avatar for %1.").arg(displayName));
	dialog.insert(QStringLiteral("sections"), QVariantList { sectionItem(QObject::tr("Avatar"), fields) });
	dialog.insert(QStringLiteral("actions"),
				  QVariantList { dialogActionItem(QStringLiteral("cancel"), QObject::tr("Cancel"), QString(), true),
								 dialogActionItem(QStringLiteral("confirmChangeAvatar"), QObject::tr("Apply avatar"),
												  QStringLiteral("accent"), false) });
	dialog.insert(QStringLiteral("primaryActionId"), QStringLiteral("confirmChangeAvatar"));
	dialog.insert(QStringLiteral("width"), 680);
	dialog.insert(QStringLiteral("height"), 440);
	if (!errors.isEmpty()) {
		dialog.insert(QStringLiteral("errors"), errors);
	}
	return openGenericDialog(dialog);
}

QVariantMap ModernDialogController::openMigrationNotice(const QString &dialogID, const QString &title,
														const QString &message) {
	QVariantMap dialog;
	const QString id = dialogID.trimmed().isEmpty() ? QStringLiteral("migrationNotice") : dialogID.trimmed();
	dialog.insert(QStringLiteral("id"), id);
	dialog.insert(QStringLiteral("kind"), QStringLiteral("migrationNotice"));
	dialog.insert(QStringLiteral("title"), title.trimmed().isEmpty() ? QObject::tr("Modern dialog") : title.trimmed());
	dialog.insert(QStringLiteral("subtitle"), message);
	dialog.insert(QStringLiteral("sections"),
				  QVariantList { sectionItem(QObject::tr("Status"), QVariantList { noteItem(message) }) });
	dialog.insert(QStringLiteral("actions"),
				  QVariantList { dialogActionItem(QStringLiteral("close"), QObject::tr("Close"),
												  QStringLiteral("accent"), true) });
	dialog.insert(QStringLiteral("primaryActionId"), QStringLiteral("close"));
	dialog.insert(QStringLiteral("width"), 600);
	dialog.insert(QStringLiteral("height"), 380);
	return openGenericDialog(dialog);
}

QVariantMap ModernDialogController::openGenericDialog(const QVariantMap &dialog) {
	m_genericDialog = dialog;
	QString id      = m_genericDialog.value(QStringLiteral("id")).toString().trimmed();
	if (id.isEmpty()) {
		id = QStringLiteral("modernDialog");
	}
	m_genericDialog.insert(QStringLiteral("id"), id);
	m_genericDialog.insert(QStringLiteral("open"), true);
	if (m_genericDialog.value(QStringLiteral("kind")).toString().trimmed().isEmpty()) {
		m_genericDialog.insert(QStringLiteral("kind"), QStringLiteral("info"));
	}
	m_activeDialogID = id;
	return state();
}

QVariantMap ModernDialogController::close(const QString &dialogID) {
	if (dialogID.trimmed().isEmpty() || dialogID == m_activeDialogID) {
		m_activeDialogID.clear();
	}
	return state();
}

QVariantMap ModernDialogController::updateField(const QString &dialogID, const QString &fieldID, const QVariant &value) {
	if (dialogID != m_activeDialogID) {
		return state();
	}

	if (dialogID == QLatin1String("connect")) {
		m_connect.updateField(fieldID, value);
	} else if (dialogID == QLatin1String("settings")) {
		m_settings.updateField(fieldID, value);
	} else if (dialogID == QLatin1String("failedConnection")) {
		m_failedConnection.insert(fieldID, value);
	} else if (dialogID == m_genericDialog.value(QStringLiteral("id")).toString()) {
		QVariantList sections = m_genericDialog.value(QStringLiteral("sections")).toList();
		if (updateFieldValue(sections, fieldID, value)) {
			m_genericDialog.insert(QStringLiteral("sections"), sections);
		}
	}

	return state();
}

ModernDialogController::ActionResult ModernDialogController::invokeAction(const QString &dialogID,
																		 const QString &actionID,
																		 const QVariantMap &payload) {
	ActionResult result;
	if (dialogID != m_activeDialogID) {
		result.stateChanged = false;
		return result;
	}

	if (dialogID == QLatin1String("connect")) {
		const ModernConnectController::ActionResult connectResult = m_connect.invokeAction(actionID, payload);
		result.stateChanged                                      = connectResult.stateChanged;
		result.closeDialog                                       = connectResult.closeDialog;
		result.connectionRequest                                 = connectResult.connectionRequest;
		result.favoritesToSave                                   = connectResult.favoritesToSave;
	} else if (dialogID == QLatin1String("settings")) {
		const ModernSettingsController::ActionResult settingsResult = m_settings.invokeAction(actionID, payload);
		result.stateChanged                                         = settingsResult.stateChanged;
		result.closeDialog                                          = settingsResult.closeDialog;
		result.settingsToApply                                      = settingsResult.settingsToApply;
		result.appearanceToPreview                                  = settingsResult.appearanceToPreview;
		result.settingsAccepted                                     = settingsResult.accepted;
		result.announceSettingsApply                                = settingsResult.announceApply;
		if (!settingsResult.externalActionID.isEmpty()) {
			ActionResult::GenericAction action;
			action.dialogID = QStringLiteral("settings");
			action.actionID = settingsResult.externalActionID;
			action.payload  = settingsResult.externalActionPayload;
			result.genericAction = action;
		}
	} else if (dialogID == QLatin1String("failedConnection")) {
		result = invokeFailedConnectionAction(actionID);
	} else if (dialogID == m_genericDialog.value(QStringLiteral("id")).toString()) {
		result = invokeGenericDialogAction(actionID, payload);
	} else {
		result.stateChanged = false;
	}

	if (result.closeDialog) {
		m_activeDialogID.clear();
	}

	return result;
}

QVariantMap ModernDialogController::state() const {
	if (m_activeDialogID == QLatin1String("connect")) {
		return m_connect.state();
	}
	if (m_activeDialogID == QLatin1String("settings")) {
		return m_settings.state();
	}
	if (m_activeDialogID == QLatin1String("failedConnection")) {
		return failedConnectionState();
	}
	if (!m_activeDialogID.isEmpty() && m_activeDialogID == m_genericDialog.value(QStringLiteral("id")).toString()) {
		return genericDialogState();
	}
	return closedState();
}

QString ModernDialogController::activeDialogID() const {
	return m_activeDialogID;
}

bool ModernDialogController::setConnectFavoritePing(const QString &host, const unsigned short port,
													const quint32 ping, const std::optional< quint32 > users,
													const std::optional< quint32 > maxUsers) {
	if (m_activeDialogID != QLatin1String("connect")) {
		return false;
	}

	return m_connect.setFavoritePing(host, port, ping, users, maxUsers);
}

QVariantMap ModernDialogController::failedConnectionState() const {
	const QString type = m_failedConnection.value(QStringLiteral("type")).toString();
	QVariantMap dialog;
	dialog.insert(QStringLiteral("open"), true);
	dialog.insert(QStringLiteral("id"), QStringLiteral("failedConnection"));
	dialog.insert(QStringLiteral("kind"), QStringLiteral("failedConnection"));
	dialog.insert(QStringLiteral("title"), failedConnectionTitle(type));
	dialog.insert(QStringLiteral("subtitle"), failedConnectionMessage(type));
	dialog.insert(QStringLiteral("primaryActionId"), QStringLiteral("reconnect"));
	dialog.insert(QStringLiteral("tone"), QStringLiteral("danger"));

	QVariantList fields;
	fields.push_back(fieldItem(QStringLiteral("username"), QObject::tr("Username"), QStringLiteral("text"),
							   m_failedConnection.value(QStringLiteral("username"))));
	fields.push_back(fieldItem(QStringLiteral("password"), QObject::tr("Password"), QStringLiteral("password"),
							   m_failedConnection.value(QStringLiteral("password"))));
	dialog.insert(QStringLiteral("sections"),
				  QVariantList { sectionItem(QObject::tr("Reconnect details"), fields) });

	QVariantList actions;
	actions.push_back(ModernShellMenuSerializer::actionItem(QStringLiteral("cancel"), QObject::tr("Cancel"), true, false));
	actions.push_back(ModernShellMenuSerializer::actionItem(QStringLiteral("changeCertificate"),
															QObject::tr("Certificate wizard"),
															type == QLatin1String("authenticationFailure"), false));
	actions.push_back(ModernShellMenuSerializer::actionItem(QStringLiteral("reconnect"), QObject::tr("Reconnect"), true,
															false, QStringLiteral("accent")));
	dialog.insert(QStringLiteral("actions"), actions);
	return dialog;
}

QVariantMap ModernDialogController::genericDialogState() const {
	QVariantMap dialog = m_genericDialog;
	dialog.insert(QStringLiteral("open"), true);
	return dialog;
}

ModernDialogController::ActionResult ModernDialogController::invokeFailedConnectionAction(const QString &actionID) {
	ActionResult result;
	const QString action = actionID.trimmed();
	if (action == QLatin1String("cancel")) {
		result.closeDialog = true;
		return result;
	}

	if (action == QLatin1String("changeCertificate")) {
		result.closeDialog           = true;
		result.openCertificateWizard = true;
		return result;
	}

	if (action == QLatin1String("reconnect")) {
		ModernConnectController::ConnectionRequest request;
		request.host = m_failedConnection.value(QStringLiteral("host")).toString();
		request.port = static_cast< unsigned short >(m_failedConnection.value(QStringLiteral("port")).toInt());
		request.username = m_failedConnection.value(QStringLiteral("username")).toString().simplified();
		request.password = m_failedConnection.value(QStringLiteral("password")).toString();
		result.connectionRequest = request;
		result.closeDialog       = true;
		return result;
	}

	result.stateChanged = false;
	return result;
}

ModernDialogController::ActionResult ModernDialogController::invokeGenericDialogAction(
	const QString &actionID, const QVariantMap &payload) const {
	ActionResult result;
	const QString action = actionID.trimmed();
	if (action.isEmpty()) {
		result.stateChanged = false;
		return result;
	}

	ActionResult::GenericAction genericAction;
	genericAction.dialogID = m_genericDialog.value(QStringLiteral("id")).toString();
	genericAction.actionID = action;
	genericAction.payload  = payload;
	collectFieldValues(m_genericDialog.value(QStringLiteral("sections")).toList(), genericAction.fieldValues);
	result.genericAction = genericAction;

	const QVariantMap actionItem =
		findAction(m_genericDialog.value(QStringLiteral("actions")).toList(), action);
	result.closeDialog = action == QLatin1String("close") || action == QLatin1String("cancel")
						 || actionItem.value(QStringLiteral("closesDialog")).toBool();
	result.stateChanged = result.closeDialog;
	return result;
}
