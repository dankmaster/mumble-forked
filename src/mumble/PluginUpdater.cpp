// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PluginUpdater.h"
#include "Log.h"
#include "PluginInstallService.h"
#include "PluginManager.h"
#include "PluginUpdatePreparation.h"
#include "Global.h"

#include <QtConcurrent>
#include <QNetworkRequest>
#include <QtCore/QByteArray>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QHashIterator>
#include <QtCore/QFutureWatcher>
#include <QtCore/QMetaEnum>
#include <QtCore/QTimer>

#include <algorithm>
#include <functional>
#include <optional>
#include <utility>

namespace {
constexpr qsizetype MaximumPluginDownloadBytes = 160 * 1024 * 1024;
struct PreparedUpdate {
	PluginUpdateFileResult downloaded;
	std::optional< PluginInstallService::PreparedPackage > package;
};
}

void PluginUpdater::trackDownload(QNetworkReply *reply, const plugin_id_t pluginID) {
	if (!reply) return;
	m_downloadBuffers.insert(reply, {});
	connect(reply, &QIODevice::readyRead, this, [this, reply]() {
		if (m_wasInterrupted.load() || m_oversizedDownloads.contains(reply)) {
			reply->abort();
			return;
		}
		QByteArray &buffer = m_downloadBuffers[reply];
		while (reply->bytesAvailable() > 0) {
			const qint64 remaining = MaximumPluginDownloadBytes - buffer.size();
			if (remaining <= 0) {
				m_oversizedDownloads.insert(reply);
				reply->abort();
				return;
			}
			const QByteArray chunk = reply->read(qMin< qint64 >(1024 * 1024, remaining + 1));
			if (chunk.isEmpty()) break;
			buffer.append(chunk);
			if (buffer.size() > MaximumPluginDownloadBytes) {
				m_oversizedDownloads.insert(reply);
				reply->abort();
				return;
			}
		}
	});
	connect(reply, &QNetworkReply::downloadProgress, this,
			[this, reply, pluginID](const qint64 received, const qint64 total) {
				if (total > MaximumPluginDownloadBytes || received > MaximumPluginDownloadBytes) {
					m_oversizedDownloads.insert(reply);
					reply->abort();
				}
				emit updateProgress(static_cast< qulonglong >(pluginID), received, total);
			});
}

PluginUpdater::PluginUpdater(QObject *parent)
	: QObject(parent), m_wasInterrupted(false), m_cancelToken(std::make_shared< std::atomic< bool > >(false)),
	  m_dataMutex(), m_pluginsToUpdate(), m_networkManager() {
	QObject::connect(&m_networkManager, &QNetworkAccessManager::finished, this, &PluginUpdater::on_updateDownloaded);
}

PluginUpdater::~PluginUpdater() {
	m_wasInterrupted.store(true);
	m_cancelToken->store(true);
}

void PluginUpdater::checkForUpdates() {
	m_wasInterrupted.store(false);
	m_cancelToken->store(false);
	{
		QMutexLocker lock(&m_dataMutex);
		m_pluginsToUpdate.clear();
	}
	// Plugin ABI calls must stay on PluginUpdater/PluginManager's owner thread. Process one plugin per
	// event-loop turn so cancellation and UI updates can be handled between third-party calls.
	const QVector< const_plugin_ptr_t > plugins = Global::get().pluginManager->getPlugins();
	auto next = std::make_shared< std::function< void(int) > >();
	*next = [this, plugins, next](const int index) {
		if (m_wasInterrupted.load()) {
			emit updateInterrupted();
			*next = {};
			return;
		}
		if (index >= plugins.size()) {
			if (!availableUpdates().isEmpty()) emit updatesAvailable();
			*next = {};
			return;
		}
		const const_plugin_ptr_t &plugin = plugins.at(index);
		if (plugin && plugin->hasUpdate()) {
			const QUrl updateURL = plugin->getUpdateDownloadURL();
			if (updateURL.isValid() && !updateURL.isEmpty() && !updateURL.fileName().isEmpty()) {
				QMutexLocker lock(&m_dataMutex);
				m_pluginsToUpdate.append(UpdateEntry(plugin->getID(), updateURL, updateURL.fileName()));
			}
		}
		QTimer::singleShot(0, this, [next, index]() { (*next)(index + 1); });
	};
	QTimer::singleShot(0, this, [next]() { (*next)(0); });
}

void PluginUpdater::update() {
	QMutexLocker l(&m_dataMutex);
	m_wasInterrupted.store(false);
	m_cancelToken->store(false);

	for (int i = 0; i < m_pluginsToUpdate.size(); i++) {
		UpdateEntry currentEntry = m_pluginsToUpdate[i];

		QNetworkReply *reply = m_networkManager.get(QNetworkRequest(currentEntry.updateURL));
		emit updateStarted(static_cast< qulonglong >(currentEntry.pluginID), currentEntry.fileName);
		trackDownload(reply, currentEntry.pluginID);
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
	if (m_wasInterrupted.exchange(true)) {
		return;
	}
	m_cancelToken->store(true);
	for (QNetworkReply *reply : m_networkManager.findChildren< QNetworkReply * >()) {
		reply->abort();
	}
	emit updateInterrupted();
}

void PluginUpdater::finishEntry(const UpdateEntry &entry, const bool success, const QString &errorCode,
								const QString &message) {
	emit updateResult(static_cast< qulonglong >(entry.pluginID), success, errorCode, message);
	QMutexLocker lock(&m_dataMutex);
	if (m_pluginsToUpdate.isEmpty() && m_pendingPreparations == 0) {
		emit updatingFinished();
	}
}

void PluginUpdater::on_updateDownloaded(QNetworkReply *reply) {
	if (reply) {
		// Schedule reply for deletion
		reply->deleteLater();
		QByteArray content = m_downloadBuffers.take(reply);
		bool oversized = m_oversizedDownloads.remove(reply);
		if (!oversized && reply->bytesAvailable() > 0) {
			const qint64 remaining = MaximumPluginDownloadBytes - content.size();
			const QByteArray tail = reply->read(qMax< qint64 >(0, remaining) + 1);
			content.append(tail);
			oversized = content.size() > MaximumPluginDownloadBytes;
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

		if (m_wasInterrupted.load()) {
			finishEntry(entry, false, QStringLiteral("cancelled"), tr("Update cancelled"));
			return;
		}
		if (oversized) {
			finishEntry(entry, false, QStringLiteral("download-too-large"),
						tr("The plugin update exceeds the %1 MiB download limit")
							.arg(MaximumPluginDownloadBytes / (1024 * 1024)));
			return;
		}

		// Now get a handle to that plugin
		const_plugin_ptr_t plugin = Global::get().pluginManager->getPlugin(entry.pluginID);

		if (!plugin) {
			// Can't find plugin with given ID
			qWarning() << "PluginUpdater: Got update for plugin with id" << entry.pluginID
					   << "but it doesn't seem to exist anymore!";
			finishEntry(entry, false, QStringLiteral("plugin-missing"), tr("The plugin is no longer installed"));
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
			finishEntry(entry, false, QStringLiteral("network-error"), reply->errorString());
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

				finishEntry(entry, false, QStringLiteral("too-many-redirects"),
						tr("The update redirected too many times"));
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
		QNetworkReply *redirectedReply = m_networkManager.get(QNetworkRequest(redirectedUrl));
			trackDownload(redirectedReply, entry.pluginID);

			return;
		}

		if (httpStatusCode < 200 || httpStatusCode >= 300) {
			// HTTP request has failed
			Log::logOrDefer(Log::Warning,
							tr("Unable to download plugin update for \"%1\" from \"%2\" (HTTP status code %3)")
								.arg(plugin->getName())
								.arg(reply->url().toString())
								.arg(httpStatusCode));

			finishEntry(entry, false, QStringLiteral("http-error"),
						tr("The server returned HTTP status %1").arg(httpStatusCode));
			return;
		}

		// Reply seems fine -> write file to disk and fire installer
		// Write the content to a file in the temp-dir
		if (content.isEmpty()) {
			qWarning() << "PluginUpdater: Update for" << plugin->getName() << "from" << reply->url().toString()
					   << "resulted in no content!";
			finishEntry(entry, false, QStringLiteral("empty-download"), tr("The downloaded update was empty"));
			return;
		}

		const QString pluginName = plugin->getName();
		const bool requireLoaded = plugin->isLoaded();
		const QString installDirectory = PluginInstallService::installDirectory();
		plugin.reset();
		++m_pendingPreparations;
		auto *writeWatcher = new QFutureWatcher< PreparedUpdate >(this);
		connect(writeWatcher, &QFutureWatcherBase::finished, this,
				[this, writeWatcher, entry, pluginName, requireLoaded]() {
			const PreparedUpdate result = writeWatcher->result();
			writeWatcher->deleteLater();
			if (!result.downloaded.errorCode.isEmpty() || !result.package) {
				--m_pendingPreparations;
				if (!result.downloaded.path.isEmpty()) QFile::remove(result.downloaded.path);
				finishEntry(entry, false, result.downloaded.errorCode, result.downloaded.message);
				return;
			}
			const QString temporaryPath = result.downloaded.path;
			if (m_wasInterrupted.load()) {
				--m_pendingPreparations;
				QFile::remove(temporaryPath);
				finishEntry(entry, false, QStringLiteral("cancelled"), tr("Update cancelled"));
				return;
			}

			PluginInstallService::PreparedPackage package = *result.package;
			try {
				// ABI validation and unload remain strictly on PluginManager's owner thread.
				PluginInstallService inspector(package);
				for (const const_plugin_ptr_t &existing : Global::get().pluginManager->getPlugins()) {
					if (existing && QFileInfo(existing->getFilePath()).absoluteFilePath()
								== QFileInfo(package.destinationPath).absoluteFilePath()) {
						Global::get().pluginManager->clearPlugin(existing->getID());
						break;
					}
				}
			} catch (const PluginInstallException &exception) {
				--m_pendingPreparations;
				QFile::remove(temporaryPath);
				finishEntry(entry, false, QStringLiteral("install-error"), exception.getMessage());
				return;
			}
			auto *commitWatcher = new QFutureWatcher< PluginFileCommitResult >(this);
			connect(commitWatcher, &QFutureWatcherBase::finished, this,
					[this, commitWatcher, entry, pluginName, requireLoaded, temporaryPath,
					 destinationPath = package.destinationPath]() {
						const PluginFileCommitResult committed = commitWatcher->result();
						commitWatcher->deleteLater();
						QFile::remove(temporaryPath);
						if (!committed.success) {
							--m_pendingPreparations;
							Global::get().pluginManager->rescanPlugins();
							finishEntry(entry, false, committed.errorCode, committed.message);
							return;
						}
						Global::get().pluginManager->rescanPlugins();
						bool loaded = false;
						for (const const_plugin_ptr_t &candidate : Global::get().pluginManager->getPlugins())
							if (candidate && candidate->isValid() && (!requireLoaded || candidate->isLoaded())
								&& QFileInfo(candidate->getFilePath()).absoluteFilePath()
										== QFileInfo(destinationPath).absoluteFilePath()) loaded = true;
						auto *finishWatcher = new QFutureWatcher< bool >(this);
						connect(finishWatcher, &QFutureWatcherBase::finished, this,
								[this, finishWatcher, entry, pluginName, loaded]() {
									const bool fileResult = finishWatcher->result();
									finishWatcher->deleteLater();
									--m_pendingPreparations;
									if (!loaded) Global::get().pluginManager->rescanPlugins();
									if (loaded && fileResult)
										Log::logOrDefer(Log::Information, tr("Successfully updated plugin \"%1\"").arg(pluginName));
									finishEntry(entry, loaded && fileResult,
												loaded ? QStringLiteral("finalize-error") : QStringLiteral("load-error"),
												loaded ? tr("Plugin updated successfully") : tr("The updated plugin failed to load; the previous file was restored"));
								});
						finishWatcher->setFuture(QtConcurrent::run([committed, loaded]() {
							return loaded ? finalizePluginFileCommit(committed) : rollbackPluginFileCommit(committed);
						}));
					});
			const auto cancelToken = m_cancelToken;
			commitWatcher->setFuture(QtConcurrent::run([package, cancelToken]() {
				return commitPreparedPluginFile(package.sourcePath, package.destinationPath, true, cancelToken.get(), {},
												package.sha256);
			}));
		});
		const auto cancelToken = m_cancelToken;
		writeWatcher->setFuture(QtConcurrent::run([content = std::move(content), fileName = entry.fileName,
												 installDirectory, cancelToken]() {
			PreparedUpdate result;
			result.downloaded = preparePluginUpdateFile(content, fileName, cancelToken.get());
			if (!result.downloaded.errorCode.isEmpty()) return result;
			try {
				result.package = PluginInstallService::prepare(result.downloaded.path, installDirectory, cancelToken.get());
			} catch (const PluginInstallException &exception) {
				result.downloaded.errorCode = QStringLiteral("package-error");
				result.downloaded.message = exception.getMessage();
			}
			return result;
		}));
	}
}
