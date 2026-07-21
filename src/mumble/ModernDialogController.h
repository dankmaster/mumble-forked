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
#include <QtCore/QVector>

#include <optional>

class ModernDialogController {
public:
	struct ActionResult {
		bool stateChanged = true;
		bool closeDialog  = false;
		std::optional< ModernConnectController::ConnectionRequest > connectionRequest;
		std::optional< QList< FavoriteServer > > favoritesToSave;
		std::optional< ModernConnectController::SourceOperationRequest > connectSourceOperation;
		std::optional< bool > publicListDisabledSetting;
		std::optional< Settings > settingsToApply;
		std::optional< ModernSettingsController::AppearancePreview > appearanceToPreview;
		bool settingsAccepted = false;
		bool announceSettingsApply = true;
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
	QVariantMap openSettings(const Settings &settings, const QString &pageName = QString(),
						 bool audioInputOnboarding = false,
						 const QVariantMap &stonksContext = QVariantMap(),
						 const QVariantMap &motdContext = QVariantMap());
	bool setSettingsStonksContext(const QVariantMap &stonksContext);
	bool setSettingsMotdContext(const QVariantMap &motdContext);
	bool setSettingsMotdPreview(const QString &sourceHtml, const QVariantList &blocks, const QString &summary);
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
	quint64 beginConnectSourceRefresh(const QString &sourceID);
	bool applyConnectSourceServers(const QString &sourceID, quint64 generation,
								   const QList< ModernConnectController::ServerEntry > &servers);
	bool applyConnectSourceError(const QString &sourceID, quint64 generation, const QString &message,
								 bool retryable = true);
	bool applyConnectSourceUnavailable(const QString &sourceID, quint64 generation, const QString &message);
	bool setConnectSourceUnavailable(const QString &sourceID, const QString &message);
	bool requestConnectPublicListConsent(quint64 generation);

	QString activeDialogID() const;

private:
	struct DialogFrame {
		QString activeDialogID;
		QVariantMap genericDialog;
	};

	ModernConnectController m_connect;
	ModernSettingsController m_settings;
	QString m_activeDialogID;
	QVariantMap m_failedConnection;
	QVariantMap m_genericDialog;
	QVector< DialogFrame > m_parentDialogs;

	QVariantMap failedConnectionState() const;
	QVariantMap genericDialogState() const;
	void beginRootDialog(const QString &dialogID);
	void dismissActiveDialog();
	ActionResult invokeFailedConnectionAction(const QString &actionID);
	ActionResult invokeGenericDialogAction(const QString &actionID, const QVariantMap &payload) const;
};

#endif // MUMBLE_MUMBLE_MODERNDIALOGCONTROLLER_H_
