// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PLUGINUPDATER_H_
#define MUMBLE_MUMBLE_PLUGINUPDATER_H_

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QByteArray>
#include <QHash>
#include <QElapsedTimer>
#include <QObject>
#include <QPointer>
#include <QString>
#include <QtCore/QMutex>
#include <QtCore/QUrl>
#include <QtCore/QVector>
#include <QtCore/QSet>

#include <atomic>
#include <functional>
#include <limits>
#include <memory>
#include <utility>

#include "Plugin.h"
#include "PluginAbiWorker.h"

class PluginOperation;
class QFutureWatcherBase;

/// A helper struct to store a pair of a plugin ID  and an URL corresponding to
/// the same plugin.
struct UpdateEntry {
	plugin_id_t pluginID = std::numeric_limits< plugin_id_t >::max();
	QUrl updateURL;
	QString fileName;
	QString displayName;
	QString pluginPath;
	int redirects = 0;

	UpdateEntry() = default;
	explicit UpdateEntry(plugin_id_t id, const QUrl &url, const QString &name, int redirectCount = 0,
						 QString label = {}, QString path = {})
		: pluginID(id), updateURL(url), fileName(name), displayName(std::move(label)),
		  pluginPath(std::move(path)), redirects(redirectCount) {}
};

/// A class designed for managing plugin updates. At the same time this also represents
/// a Dialog that can be used to prompt the user whether certain updates should be updated.
class PluginUpdater : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PluginUpdater)

protected:
	/// A mutex for m_pluginsToUpdate.
	QMutex m_dataMutex;
	/// A vector holding plugins that can be updated by storing a pluginID and the download URL
	/// in form of an UpdateEntry.
	QVector< UpdateEntry > m_pluginsToUpdate;
	/// The NetworkManager used to perform the downloading of plugins.
	QNetworkAccessManager m_networkManager;
	QHash< QNetworkReply *, QByteArray > m_downloadBuffers;
	QHash< QNetworkReply *, UpdateEntry > m_downloadEntries;
	QSet< QNetworkReply * > m_oversizedDownloads;
	QVector< UpdateEntry > m_updateQueue;
	std::unique_ptr< PluginOperation > m_checkOperation;
	std::unique_ptr< PluginOperation > m_updateOperation;
	QPointer< QNetworkReply > m_currentReply;
	bool m_checkInProgress = false;
	bool m_updateInProgress = false;
	bool m_shuttingDown = false;
	int m_pendingPreparations = 0;
	QSet< QFutureWatcherBase * > m_prepareWatchers;
public:
	/// Constructor
	///
	/// @param parent QObject that owns this updater
	PluginUpdater(QObject *parent = nullptr);
	/// Destructor
	~PluginUpdater();

	// The maximum number of redirects to allow
	static constexpr int MAX_REDIRECTS = 10;

	/// Triggers a non-blocking update check for all recognized plugins. Network and file work is asynchronous and
	/// third-party ABI queries run on the shared serial plugin worker.
	/// Once all plugins have been checked and if there are updates available, updatesAvailable is emitted.
	QString checkForUpdates();
	/// Starts the update process of the plugins. This is done asynchronously.
	QString update();
	QVector< UpdateEntry > availableUpdates();
	QString updateSelected(const QSet< plugin_id_t > &pluginIDs);
	/// Process-exit-only teardown barrier. Stops producer admission, cancels network/file preparation and drains every
	/// accepted ABI task within a strict deadline; interactive cancellation never enters this path.
	void shutdownAndWait(int timeoutMilliseconds = 5000);
public slots:
	/// Slot that can be triggered to ask for the update process to be interrupted.
	void interrupt(const QString &operationID = {});
protected slots:
	/// Slot triggered once an update for a plugin has been downloaded.
	void on_updateDownloaded(QNetworkReply *reply);

private:
	void completeUpdateEntry(const UpdateEntry &entry, bool success, const QString &errorCode, const QString &message,
							 bool cancelled = false);
	void finishCheckOperation(bool cancelled = false);
	void finishUpdateOperation();
	void startNextDownload();
	void trackDownload(QNetworkReply *reply, const UpdateEntry &entry);
	void emitAbiMeasurement(const QString &operationID, const QString &phase, plugin_id_t pluginID,
						qint64 elapsedMilliseconds);

signals:
	/// This signal is emitted once it has been determined that there are plugin updates available.
	void updatesAvailable();
	/// This signal is emitted once all plugin updates have been downloaded and processed.
	void updatingFinished();
	/// This signal is emitted every time the update process has been interrupted.
	void updateInterrupted();
	void updateStarted(qulonglong pluginID, const QString &fileName);
	void updateProgress(qulonglong pluginID, qint64 bytesReceived, qint64 bytesTotal);
	void updateResult(qulonglong pluginID, bool success, const QString &errorCode, const QString &message);
	void operationStarted(const QString &operationID, const QString &kind, int itemCount, bool cancellable);
	void operationProgress(const QString &operationID, const QString &phase, int completedItems, int totalItems,
						   qulonglong pluginID, qint64 bytesReceived, qint64 bytesTotal);
	void operationItemResult(const QString &operationID, const QString &itemID, qulonglong pluginID, bool success,
							 bool cancelled, const QString &errorCode, const QString &message);
	void operationFinished(const QString &operationID, const QString &kind, const QString &status,
						   int successfulItems, int failedItems, int cancelledItems);
	/// Emitted after a worker-thread ABI call returns. A duration over 50 ms is recorded for diagnostics.
	/// The call is never force-terminated because doing so can corrupt an in-process third-party plugin.
	void abiStepMeasured(const QString &operationID, const QString &phase, qulonglong pluginID,
						 qint64 elapsedMilliseconds, bool budgetExceeded);
};

#endif // MUMBLE_MUMBLE_PLUGINUPDATER_H_
