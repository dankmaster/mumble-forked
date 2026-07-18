// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNCONNECTDISCOVERYSERVICE_H_
#define MUMBLE_MUMBLE_MODERNCONNECTDISCOVERYSERVICE_H_

#include "ModernConnectController.h"

#include <QtCore/QByteArray>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>

#include <memory>

class QNetworkAccessManager;
class QNetworkReply;
class QTimer;
class Zeroconf;

/// Frontend-neutral asynchronous discovery backend for the Modern Connect flow.
///
/// The service deliberately publishes typed server rows and operation generations.
/// It does not own dialog state and does not depend on QML or QWidget surfaces.
class ModernConnectDiscoveryService final : public QObject {
	Q_OBJECT
	Q_DISABLE_COPY(ModernConnectDiscoveryService)

public:
	struct PublicListParseResult {
		QList< ModernConnectController::ServerEntry > servers;
		QString error;
		bool ok = false;
	};

	explicit ModernConnectDiscoveryService(QNetworkAccessManager *networkManager, Zeroconf *zeroconf,
									   QObject *parent = nullptr);
	~ModernConnectDiscoveryService() override;

	void setServicePrefix(const QString &servicePrefix);
	void start(const QString &sourceID, quint64 generation);
	void cancel(const QString &sourceID);
	void cancelAll();

	static PublicListParseResult parsePublicServerList(const QByteArray &data);

signals:
	void serversReady(const QString &sourceID, quint64 generation,
					  const QList< ModernConnectController::ServerEntry > &servers);
	void sourceError(const QString &sourceID, quint64 generation, const QString &message, bool retryable);
	void sourceUnavailable(const QString &sourceID, quint64 generation, const QString &message);
	void servicePrefixSuggested(const QString &servicePrefix);

private:
	enum class PublicAbortReason { None, Cancelled, Timeout, PayloadTooLarge };

	struct LanState;

	void startPublic(quint64 generation);
	void issuePublicRequest(bool useServicePrefix);
	void finishPublicRequest(QNetworkReply *reply, quint64 generation, bool usedServicePrefix);
	void clearPublicRequest(PublicAbortReason reason);
	void startLan(quint64 generation);
	void cancelLan();
	void publishLanServers();

	QNetworkAccessManager *m_networkManager = nullptr;
	QPointer< QNetworkReply > m_publicReply;
	QTimer *m_publicTimeout = nullptr;
	quint64 m_publicGeneration = 0;
	QString m_servicePrefix;
	PublicAbortReason m_publicAbortReason = PublicAbortReason::None;
	std::unique_ptr< LanState > m_lan;
};

#endif // MUMBLE_MUMBLE_MODERNCONNECTDISCOVERYSERVICE_H_
