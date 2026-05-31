// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNDIALOGCONTROLLER_H_
#define MUMBLE_MUMBLE_MODERNDIALOGCONTROLLER_H_

#include "ModernConnectController.h"
#include "ModernSettingsController.h"

#include <QtCore/QString>
#include <QtCore/QVariant>

#include <optional>

class ModernDialogController {
public:
	struct ActionResult {
		bool stateChanged = true;
		bool closeDialog  = false;
		std::optional< ModernConnectController::ConnectionRequest > connectionRequest;
		std::optional< QList< FavoriteServer > > favoritesToSave;
		std::optional< Settings > settingsToApply;
		bool settingsAccepted = false;
		bool openCertificateWizard = false;
		struct GenericAction {
			QString dialogID;
			QString actionID;
			QVariantMap fieldValues;
			QVariantMap payload;
		};
		std::optional< GenericAction > genericAction;
	};

	QVariantMap openConnect(const QList< FavoriteServer > &favorites, const Settings &settings,
							const QMap< UnresolvedServerAddress, unsigned int > &pingCache = {});
	QVariantMap openSettings(const Settings &settings, const QString &pageName = QString());
	QVariantMap openFailedConnection(const QVariantMap &context);
	QVariantMap openDisconnectConfirmation(const QString &serverLabel = QString());
	QVariantMap openQuitConfirmation(bool connected, bool allowMinimize);
	QVariantMap openDeleteMessageConfirmation(qulonglong messageID, const QString &conversationLabel = QString());
	QVariantMap openChangeAvatar(unsigned int session, const QString &userName,
								 const QVariantMap &fieldValues = QVariantMap(),
								 const QVariantMap &errors = QVariantMap());
	QVariantMap openMigrationNotice(const QString &dialogID, const QString &title, const QString &message);
	QVariantMap openGenericDialog(const QVariantMap &dialog);
	QVariantMap close(const QString &dialogID = QString());
	QVariantMap updateField(const QString &dialogID, const QString &fieldID, const QVariant &value);
	ActionResult invokeAction(const QString &dialogID, const QString &actionID, const QVariantMap &payload);
	QVariantMap state() const;
	bool setConnectFavoritePing(const QString &host, unsigned short port, quint32 ping,
								std::optional< quint32 > users = std::nullopt,
								std::optional< quint32 > maxUsers = std::nullopt);

	QString activeDialogID() const;

private:
	ModernConnectController m_connect;
	ModernSettingsController m_settings;
	QString m_activeDialogID;
	QVariantMap m_failedConnection;
	QVariantMap m_genericDialog;

	QVariantMap failedConnectionState() const;
	QVariantMap genericDialogState() const;
	ActionResult invokeFailedConnectionAction(const QString &actionID);
	ActionResult invokeGenericDialogAction(const QString &actionID, const QVariantMap &payload) const;
};

#endif // MUMBLE_MUMBLE_MODERNDIALOGCONTROLLER_H_
