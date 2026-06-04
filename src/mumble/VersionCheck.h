// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_VERSIONCHECK_H_
#define MUMBLE_MUMBLE_VERSIONCHECK_H_

#include <QtCore/QByteArray>
#include <QtCore/QJsonObject>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QUrl>

#include <functional>

class QNetworkReply;
class QWidget;

class VersionCheck : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(VersionCheck)

	enum class RequestKind { Release, Manifest };

	QUrl m_requestURL;
	QNetworkReply *m_reply = nullptr;
	RequestKind m_requestKind = RequestKind::Release;
	bool m_autocheck         = false;
	bool m_emitResultsOnly   = false;
	int m_redirectCount      = 0;

	void request(const QUrl &url, RequestKind kind);
	void finishWithInfo(const QJsonObject &info);
	void finishWithFailure(const QString &message);
protected slots:
	void performRequest();
	void replyFinished();
signals:
	void updateInfoReceived(const QJsonObject &info, bool updateAvailable);
	void updateCheckFailed(const QString &message);
public slots:
	void fetched(QByteArray data, QUrl url);

public:
	VersionCheck(bool autocheck, QObject *parent = nullptr, bool focus = false, bool emitResultsOnly = false);
	static QString updateModeForInfo(const QJsonObject &info);
	static QString expectedUpdateSha256ForInfo(const QJsonObject &info);
	static bool canInstallUpdate(const QJsonObject &info);
	static void installUpdateFromInfo(const QJsonObject &info, QWidget *parent = nullptr);
	static void downloadUpdateFromInfo(const QJsonObject &info, QWidget *parent, bool showProgress,
									   std::function< void(const QString &) > readyCallback = {},
									   std::function< void(const QString &) > failureCallback = {},
									   std::function< void() > cancelledCallback = {},
									   std::function< void(qint64, qint64) > progressCallback = {});
	static QJsonObject describeUpdateHandoff(const QJsonObject &info,
											 const QString &preparedInstallerPath = QString());
	static QString preparedFallbackInstallerPathForInfo(const QJsonObject &info);
	static bool canLaunchPreparedUpdate(const QString &updatePath, const QString &updateMode = QString());
	static bool launchPreparedUpdate(const QString &updatePath, const QString &updateMode = QString(),
									 bool passive = true, bool restartAfterInstall = true,
									 const QString &fallbackInstallerPath = QString(),
									 const QString &expectedUpdateSha256 = QString());
};

#endif
