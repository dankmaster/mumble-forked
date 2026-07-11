// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PluginUpdater.h"
#include "Log.h"
#include "PluginInstallService.h"
#include "PluginManager.h"
#include "Global.h"

#include <QNetworkRequest>
#include <QtConcurrent>
#include <QtCore/QByteArray>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QHashIterator>

#include <algorithm>
#include <utility>

PluginUpdater::PluginUpdater(QObject *parent)
	: QObject(parent), m_wasInterrupted(false), m_dataMutex(), m_pluginsToUpdate(), m_networkManager() {
	QObject::connect(&m_networkManager, &QNetworkAccessManager::finished, this, &PluginUpdater::on_updateDownloaded);
}

PluginUpdater::~PluginUpdater() {
	m_wasInterrupted.store(true);
}

void PluginUpdater::checkForUpdates() {
	// Dispatch a thread in which each plugin can check for updates
	std::ignore = QtConcurrent::run([this]() {
		QMutexLocker lock(&m_dataMutex);

		const QVector< const_plugin_ptr_t > plugins = Global::get().pluginManager->getPlugins();

		for (int i = 0; i < plugins.size(); i++) {
			const_plugin_ptr_t plugin = plugins[i];

			if (plugin->hasUpdate()) {
				QUrl updateURL = plugin->getUpdateDownloadURL();

				if (updateURL.isValid() && !updateURL.isEmpty() && !updateURL.fileName().isEmpty()) {
					m_pluginsToUpdate.append(UpdateEntry(plugin->getID(), updateURL, updateURL.fileName()));
				}
			}

			// if the update has been asked to be interrupted, exit here
			if (m_wasInterrupted.load()) {
				emit updateInterrupted();
				return;
			}
		}

		if (!m_pluginsToUpdate.isEmpty()) {
			emit updatesAvailable();
		}
	});
}

void PluginUpdater::update() {
	QMutexLocker l(&m_dataMutex);

	for (int i = 0; i < m_pluginsToUpdate.size(); i++) {
		UpdateEntry currentEntry = m_pluginsToUpdate[i];

		// The network manager will be emit a signal once the request has finished processing.
		// Thus we can ignore the returned QNetworkReply* here.
		m_networkManager.get(QNetworkRequest(currentEntry.updateURL));
	}
}

QVector< UpdateEntry > PluginUpdater::availableUpdates() {
	QMutexLocker lock(&m_dataMutex);
	return m_pluginsToUpdate;
}

void PluginUpdater::updateSelected(const QSet< plugin_id_t > &pluginIDs) {
	{
		QMutexLocker lock(&m_dataMutex);
		m_pluginsToUpdate.erase(
			std::remove_if(m_pluginsToUpdate.begin(), m_pluginsToUpdate.end(), [&pluginIDs](const UpdateEntry &entry) {
				return !pluginIDs.contains(entry.pluginID);
			}),
			m_pluginsToUpdate.end());
	}
	if (pluginIDs.isEmpty()) {
		emit updateInterrupted();
		return;
	}
	update();
}

void PluginUpdater::interrupt() {
	m_wasInterrupted.store(true);
}

void PluginUpdater::on_updateDownloaded(QNetworkReply *reply) {
	if (reply) {
		// Schedule reply for deletion
		reply->deleteLater();

		if (m_wasInterrupted.load()) {
			emit updateInterrupted();
			return;
		}

		// Find the ID of the plugin this update is for by comparing the URLs
		UpdateEntry entry;
		bool foundID = false;
		{
			QMutexLocker l(&m_dataMutex);

			for (int i = 0; i < m_pluginsToUpdate.size(); i++) {
				if (m_pluginsToUpdate[i].updateURL == reply->url()) {
					foundID = true;

					// remove that entry from the vector as it is being updated right here
					entry = m_pluginsToUpdate.takeAt(i);
					break;
				}
			}
		}

		if (!foundID) {
			// Can't match the URL to a pluginID
			qWarning() << "PluginUpdater: Requested update for plugin from" << reply->url()
					   << "but didn't find corresponding plugin again!";
			return;
		}

		// Now get a handle to that plugin
		const_plugin_ptr_t plugin = Global::get().pluginManager->getPlugin(entry.pluginID);

		if (!plugin) {
			// Can't find plugin with given ID
			qWarning() << "PluginUpdater: Got update for plugin with id" << entry.pluginID
					   << "but it doesn't seem to exist anymore!";
			return;
		}

		// We can start actually checking the reply here
		if (reply->error() != QNetworkReply::NoError) {
			// There was an error during this request. Report it
			Log::logOrDefer(Log::Warning,
							tr("Unable to download plugin update for \"%1\" from \"%2\" (%3)")
								.arg(plugin->getName())
								.arg(reply->url().toString())
								.arg(QString::fromLatin1(
									QMetaEnum::fromType< QNetworkReply::NetworkError >().valueToKey(reply->error()))));
			return;
		}

		// Check HTTP status code (just because the request was successful, doesn't
		// mean the data was downloaded successfully
		int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();

		if (httpStatusCode >= 300 && httpStatusCode < 400) {
			// We have been redirected
			if (entry.redirects >= MAX_REDIRECTS - 1) {
				// Maximum redirect count exceeded
				Log::logOrDefer(Log::Warning,
								tr("Update for plugin \"%1\" failed due to too many redirects").arg(plugin->getName()));

				return;
			}

			QUrl redirectedUrl = reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl();
			// Because the redirection url can be relative,
			// we have to use the previous one to resolve it
			redirectedUrl = reply->url().resolved(redirectedUrl);

			// Re-insert the current plugin into the list of updating plugins (using the
			// new URL so that it will be associated with that instead of the old one)
			entry.updateURL = redirectedUrl;
			entry.redirects++;
			{
				QMutexLocker l(&m_dataMutex);

				m_pluginsToUpdate.append(entry);
			}

			// Post a new request for the file to the new URL
			m_networkManager.get(QNetworkRequest(redirectedUrl));

			return;
		}

		if (httpStatusCode < 200 || httpStatusCode >= 300) {
			// HTTP request has failed
			Log::logOrDefer(Log::Warning,
							tr("Unable to download plugin update for \"%1\" from \"%2\" (HTTP status code %3)")
								.arg(plugin->getName())
								.arg(reply->url().toString())
								.arg(httpStatusCode));

			return;
		}

		// Reply seems fine -> write file to disk and fire installer
		QByteArray content = reply->readAll();

		// Write the content to a file in the temp-dir
		if (content.isEmpty()) {
			qWarning() << "PluginUpdater: Update for" << plugin->getName() << "from" << reply->url().toString()
					   << "resulted in no content!";
			return;
		}

		QFile file(QDir::temp().filePath(entry.fileName));
		if (!file.open(QIODevice::WriteOnly)) {
			qWarning() << "PluginUpdater: Can't open" << file.fileName() << "for writing!";
			return;
		}

		file.write(content);
		file.close();

		try {
			const QString pluginName = plugin->getName();
			// We have to release the plugin handle here by resetting the smart-pointer in order to make sure the
			// installer can really unload the plugin in order to overwrite it.
			plugin.reset();

			PluginInstallService installer(file.fileName());
			installer.install(true);

			Log::logOrDefer(Log::Information, tr("Successfully updated plugin \"%1\"").arg(pluginName));

			// Make sure Mumble won't use the old version of the plugin
			Global::get().pluginManager->rescanPlugins();
		} catch (const PluginInstallException &e) {
			Log::logOrDefer(Log::CriticalError, e.getMessage());
		}

		{
			QMutexLocker l(&m_dataMutex);

			if (m_pluginsToUpdate.isEmpty()) {
				emit updatingFinished();
			}
		}
	}
}
