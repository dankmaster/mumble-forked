// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_PLUGINUPDATER_H_
#define MUMBLE_MUMBLE_PLUGINUPDATER_H_

#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QObject>
#include <QString>
#include <QtCore/QMutex>
#include <QtCore/QUrl>
#include <QtCore/QVector>
#include <QtCore/QSet>

#include <atomic>
#include <limits>

#include "Plugin.h"

/// A helper struct to store a pair of a plugin ID  and an URL corresponding to
/// the same plugin.
struct UpdateEntry {
	plugin_id_t pluginID = std::numeric_limits< plugin_id_t >::max();
	QUrl updateURL;
	QString fileName;
	int redirects = 0;

	UpdateEntry() = default;
	explicit UpdateEntry(plugin_id_t id, const QUrl &url, const QString &name, int redirectCount = 0)
		: pluginID(id), updateURL(url), fileName(name), redirects(redirectCount) {}
};

/// A class designed for managing plugin updates. At the same time this also represents
/// a Dialog that can be used to prompt the user whether certain updates should be updated.
class PluginUpdater : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(PluginUpdater)

protected:
	/// An atomic flag indicating whether the plugin update has been interrupted. It is used
	/// to exit some loops in different threads before they are done.
	std::atomic< bool > m_wasInterrupted;
	/// A mutex for m_pluginsToUpdate.
	QMutex m_dataMutex;
	/// A vector holding plugins that can be updated by storing a pluginID and the download URL
	/// in form of an UpdateEntry.
	QVector< UpdateEntry > m_pluginsToUpdate;
	/// The NetworkManager used to perform the downloading of plugins.
	QNetworkAccessManager m_networkManager;
public:
	/// Constructor
	///
	/// @param parent A pointer to the QWidget parent of this object
	PluginUpdater(QObject *parent = nullptr);
	/// Destructor
	~PluginUpdater();

	// The maximum number of redirects to allow
	static constexpr int MAX_REDIRECTS = 10;

	/// Triggers an update check for all plugins that are currently recognized by Mumble. This is done
	/// in a non-blocking fashion (in another thread). Once all plugins have been checked and if there
	/// are updates available, the updatesAvailable signal is emitted.
	void checkForUpdates();
	/// Starts the update process of the plugins. This is done asynchronously.
	void update();
	QVector< UpdateEntry > availableUpdates();
	void updateSelected(const QSet< plugin_id_t > &pluginIDs);
public slots:
	/// Slot that can be triggered to ask for the update process to be interrupted.
	void interrupt();
protected slots:
	/// Slot triggered once an update for a plugin has been downloaded.
	void on_updateDownloaded(QNetworkReply *reply);

private:
	void finishEntry(const UpdateEntry &entry, bool success, const QString &errorCode, const QString &message);

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
};

#endif // MUMBLE_MUMBLE_PLUGINUPDATER_H_
