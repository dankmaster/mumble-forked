// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "PluginUpdater.h"

#include "Global.h"
#include "Log.h"
#include "PluginInstallService.h"
#include "PluginManager.h"
#include "PluginOperation.h"
#include "PluginUpdatePreparation.h"

#include <QNetworkRequest>
#include <QtConcurrent>
#include <QtCore/QByteArray>
#include <QtCore/QCoreApplication>
#include <QtCore/QDir>
#include <QtCore/QElapsedTimer>
#include <QtCore/QEventLoop>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QFutureWatcher>
#include <QtCore/QMetaEnum>
#include <QtCore/QScopeGuard>
#include <QtCore/QSemaphore>
#include <QtCore/QTimer>
#include <QtCore/QThread>

#include <algorithm>
#include <exception>
#include <functional>
#include <iterator>
#include <optional>
#include <utility>

namespace {
constexpr qsizetype MaximumPluginDownloadBytes = 160 * 1024 * 1024;
constexpr qint64 PluginAbiBudgetMilliseconds   = 50;

struct PreparedUpdate {
	PluginUpdateFileResult downloaded;
	std::optional< PluginInstallService::PreparedPackage > package;
};

struct PluginAbiMeasurement {
	QString phase;
	qint64 elapsedMilliseconds = 0;
};

struct UpdateWorkerResult {
	bool success   = false;
	bool cancelled = false;
	QString errorCode;
	QString message;
	QVector< PluginAbiMeasurement > measurements;
};

QString pluginItemID(const plugin_id_t pluginID) {
	return QString::number(static_cast< qulonglong >(pluginID));
}

QNetworkRequest pluginUpdateNetworkRequest(const QUrl &url) {
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::ManualRedirectPolicy);
	return request;
}

bool clearManagedPluginAtPath(PluginManager *manager, const QString &path) {
	if (!manager) {
		return false;
	}
	for (const PluginDescriptor &plugin : manager->pluginDescriptors()) {
		if (pluginUpdateDestinationMatchesInstalledPath(plugin.path, path)) {
			return manager->clearPlugin(plugin.id);
		}
	}
	return false;
}

void removePreparedArtifacts(const QString &downloadPath, const QString &temporaryDirectoryPath) {
	if (!downloadPath.isEmpty()) {
		QFile::remove(downloadPath);
	}
	if (!temporaryDirectoryPath.isEmpty()) {
		QDir(temporaryDirectoryPath).removeRecursively();
	}
}
}

PluginUpdater::PluginUpdater(QObject *parent)
	: QObject(parent), m_dataMutex(), m_pluginsToUpdate(), m_networkManager() {
	connect(&m_networkManager, &QNetworkAccessManager::finished, this, &PluginUpdater::on_updateDownloaded);
}

PluginUpdater::~PluginUpdater() {
	shutdownAndWait();
}

void PluginUpdater::shutdownAndWait(const int timeoutMilliseconds) {
	if (m_shuttingDown) return;
	Q_ASSERT(timeoutMilliseconds > 0);
	m_shuttingDown = true;
	if (m_checkOperation) {
		m_checkOperation->requestCancellation();
	}
	if (m_updateOperation) {
		m_updateOperation->requestCancellation();
	}
	for (QNetworkReply *reply : m_networkManager.findChildren< QNetworkReply * >()) {
		reply->abort();
	}
	for (QFutureWatcherBase *watcher : std::as_const(m_prepareWatchers)) {
		if (watcher) watcher->cancel();
	}
	QElapsedTimer shutdownDeadline;
	shutdownDeadline.start();
	while (!m_prepareWatchers.isEmpty()) {
		// Preparation is pure file/validation work. Its finished callback removes the watcher and, while shutting
		// down, only schedules artifact cleanup; it is not allowed to admit another ABI transaction.
		QCoreApplication::processEvents(QEventLoop::ExcludeUserInputEvents, 5);
		if (shutdownDeadline.elapsed() >= timeoutMilliseconds) {
			qFatal("Plugin file preparation exceeded the 5 s process-shutdown boundary");
		}
		QThread::msleep(1);
	}
	const qint64 remaining = static_cast< qint64 >(timeoutMilliseconds) - shutdownDeadline.elapsed();
	if (remaining <= 0 || !sharedPluginAbiWorker().waitForDone(static_cast< int >(remaining))) {
		// Accepted jobs retain PluginUpdater/PluginManager state until they return.
		// Continuing object destruction would be a use-after-free, so fail fast at
		// the explicit process-exit boundary instead of hanging indefinitely.
		qFatal("Plugin ABI work exceeded the 5 s process-shutdown boundary");
	}
	m_updateQueue.clear();
	m_downloadEntries.clear();
	m_downloadBuffers.clear();
	m_oversizedDownloads.clear();
	m_currentReply.clear();
	m_checkOperation.reset();
	m_updateOperation.reset();
	m_checkInProgress  = false;
	m_updateInProgress = false;
}

void PluginUpdater::emitAbiMeasurement(const QString &operationID, const QString &phase,
									   const plugin_id_t pluginID, const qint64 elapsedMilliseconds) {
	const bool budgetExceeded = elapsedMilliseconds > PluginAbiBudgetMilliseconds;
	emit abiStepMeasured(operationID, phase, static_cast< qulonglong >(pluginID), elapsedMilliseconds,
						 budgetExceeded);
	if (budgetExceeded) {
		qWarning().noquote() << QStringLiteral("Plugin ABI step exceeded UI budget: operation=%1 phase=%2 plugin=%3 "
										 "duration=%4ms. In-process plugin calls cannot be force-terminated safely.")
								.arg(operationID, phase)
								.arg(static_cast< qulonglong >(pluginID))
								.arg(elapsedMilliseconds);
	}
}

void PluginUpdater::trackDownload(QNetworkReply *reply, const UpdateEntry &entry) {
	if (!reply) {
		completeUpdateEntry(entry, false, QStringLiteral("network-error"), tr("Unable to start the download"));
		return;
	}
	m_currentReply = reply;
	m_downloadEntries.insert(reply, entry);
	m_downloadBuffers.insert(reply, {});
	connect(reply, &QIODevice::readyRead, this, [this, reply]() {
		if (!m_updateOperation || m_updateOperation->isCancellationRequested()
			|| m_oversizedDownloads.contains(reply)) {
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
			if (chunk.isEmpty()) {
				break;
			}
			buffer.append(chunk);
			if (buffer.size() > MaximumPluginDownloadBytes) {
				m_oversizedDownloads.insert(reply);
				reply->abort();
				return;
			}
		}
	});
	connect(reply, &QNetworkReply::downloadProgress, this,
			[this, reply, pluginID = entry.pluginID](const qint64 received, const qint64 total) {
				if (total > MaximumPluginDownloadBytes || received > MaximumPluginDownloadBytes) {
					m_oversizedDownloads.insert(reply);
					reply->abort();
				}
				emit updateProgress(static_cast< qulonglong >(pluginID), received, total);
				if (m_updateOperation) {
					const PluginOperation::Summary summary = m_updateOperation->summary();
					emit operationProgress(summary.operationID, QStringLiteral("download"), summary.completedItems,
										   summary.totalItems, static_cast< qulonglong >(pluginID), received, total);
				}
			});
}

QString PluginUpdater::checkForUpdates() {
	if (m_shuttingDown) return {};
	if (m_checkInProgress && m_checkOperation) {
		return m_checkOperation->operationID();
	}
	if (m_updateInProgress) {
		return {};
	}

	PluginManager *manager = Global::get().pluginManager;
	const QVector< PluginDescriptor > descriptors = manager ? manager->pluginDescriptors() : QVector< PluginDescriptor > {};
	const quint64 collectionGeneration = manager ? manager->m_pluginCollectionGeneration.load() : 0;
	QVector< plugin_id_t > pluginIDs;
	QHash< plugin_id_t, QString > pluginNames;
	QHash< plugin_id_t, QString > pluginPaths;
	QStringList itemIDs;
	for (const PluginDescriptor &descriptor : descriptors) {
		pluginIDs.push_back(descriptor.id);
		pluginNames.insert(descriptor.id, descriptor.name);
		pluginPaths.insert(descriptor.id, descriptor.path);
		itemIDs.push_back(pluginItemID(descriptor.id));
	}
	m_checkOperation = std::make_unique< PluginOperation >(QStringLiteral("plugin-update-check"));
	m_checkOperation->setItems(itemIDs);
	m_checkInProgress = true;
	{
		QMutexLocker lock(&m_dataMutex);
		m_pluginsToUpdate.clear();
	}
	emit operationStarted(m_checkOperation->operationID(), m_checkOperation->kind(), itemIDs.size(), true);

	const QString operationID = m_checkOperation->operationID();
	const auto cancelToken     = m_checkOperation->cancellationToken();
	const QPointer< PluginUpdater > owner(this);
	const bool queued = sharedPluginAbiWorker().enqueue(
		[owner, manager, pluginIDs, pluginNames, pluginPaths, collectionGeneration, operationID, cancelToken]() {
		for (const plugin_id_t requestedPluginID : pluginIDs) {
			if (cancelToken->load()) {
				break;
			}
			const plugin_id_t pluginID = requestedPluginID;
			const QString itemID       = pluginItemID(pluginID);
			bool success               = true;
			QString errorCode;
			QString message = PluginUpdater::tr("No update available");
			QUrl updateURL;
			const QString displayName = pluginNames.value(pluginID);
			const QString pluginPath  = pluginPaths.value(pluginID);
			QElapsedTimer timer;
			timer.start();
			try {
				if (!manager || manager->m_pluginCollectionGeneration.load() != collectionGeneration) {
					success   = false;
					errorCode = QStringLiteral("plugin-set-changed");
					message   = PluginUpdater::tr("The plugin set changed; run the update check again");
				} else {
					const const_plugin_ptr_t plugin = manager->getPlugin(pluginID);
					if (!plugin) {
						success   = false;
						errorCode = QStringLiteral("plugin-missing");
						message   = PluginUpdater::tr("The plugin is no longer installed");
					} else {
						const bool protectedCall = manager->beginPluginProtectedWorkerCall();
						const auto protectedGuard = qScopeGuard([manager, protectedCall]() {
							manager->endPluginProtectedCall(protectedCall);
						});
						if (plugin->hasUpdate()) {
							updateURL = plugin->getUpdateDownloadURL();
							const PluginUpdateUrlValidationResult validation = validatePluginUpdateUrl(updateURL);
							if (!validation.accepted || updateURL.fileName().isEmpty()) {
								success   = false;
								errorCode = validation.accepted ? QStringLiteral("invalid-update-url") : validation.errorCode;
								message   = validation.accepted
									? PluginUpdater::tr("The plugin returned an update URL without a file name")
									: validation.message;
							} else {
								message = PluginUpdater::tr("Update available");
							}
						}
					}
				}
			} catch (const std::exception &exception) {
				success   = false;
				errorCode = QStringLiteral("plugin-exception");
				message   = QString::fromUtf8(exception.what());
			} catch (...) {
				success   = false;
				errorCode = QStringLiteral("plugin-exception");
				message   = PluginUpdater::tr("The plugin failed while checking for updates");
			}
			const qint64 elapsed = timer.elapsed();
			QMetaObject::invokeMethod(owner, [owner, operationID, pluginID, itemID, success, errorCode,
											 message, updateURL, displayName, pluginPath, elapsed]() {
				if (!owner || !owner->m_checkInProgress || !owner->m_checkOperation
					|| owner->m_checkOperation->operationID() != operationID) {
					return;
				}
				owner->emitAbiMeasurement(operationID, QStringLiteral("update-check"), pluginID, elapsed);
				if (success && !updateURL.isEmpty()) {
					QMutexLocker lock(&owner->m_dataMutex);
					owner->m_pluginsToUpdate.append(
						UpdateEntry(pluginID, updateURL, updateURL.fileName(), 0, displayName,
							pluginPath));
				}
				const PluginOperation::ItemResult result { itemID, static_cast< qulonglong >(pluginID), success,
					false, errorCode, message };
				if (owner->m_checkOperation->completeItem(result)) {
					emit owner->operationItemResult(operationID, itemID, static_cast< qulonglong >(pluginID),
						 success, false, errorCode, message);
				}
				const PluginOperation::Summary summary = owner->m_checkOperation->summary();
				emit owner->operationProgress(operationID, QStringLiteral("inspect"), summary.completedItems,
					 summary.totalItems, static_cast< qulonglong >(pluginID), -1, -1);
			}, Qt::QueuedConnection);
		}
		QMetaObject::invokeMethod(owner, [owner, operationID, cancelled = cancelToken->load()]() {
			if (!owner || !owner->m_checkInProgress || !owner->m_checkOperation
				|| owner->m_checkOperation->operationID() != operationID) {
				return;
			}
			if (cancelled || owner->m_checkOperation->isCancellationRequested()) {
				for (const QString &itemID : owner->m_checkOperation->pendingItems()) {
					const qulonglong pluginID = itemID.toULongLong();
					const PluginOperation::ItemResult result { itemID, pluginID, false, true,
						QStringLiteral("cancelled"), PluginUpdater::tr("Update check cancelled") };
					if (owner->m_checkOperation->completeItem(result)) {
						emit owner->operationItemResult(operationID, itemID, pluginID, false, true,
							QStringLiteral("cancelled"), result.message);
					}
				}
				owner->finishCheckOperation(true);
			} else {
				owner->finishCheckOperation(false);
			}
		}, Qt::QueuedConnection);
	});
	if (!queued) {
		for (const QString &itemID : m_checkOperation->pendingItems()) {
			const qulonglong pluginID = itemID.toULongLong();
			const PluginOperation::ItemResult result { itemID, pluginID, false, false,
				QStringLiteral("worker-unavailable"), tr("The plugin worker is unavailable") };
			m_checkOperation->completeItem(result);
			emit operationItemResult(operationID, itemID, pluginID, false, false, result.errorCode, result.message);
		}
		finishCheckOperation(false);
	}
	return operationID;
}

void PluginUpdater::finishCheckOperation(const bool cancelled) {
	if (!m_checkInProgress || !m_checkOperation) {
		return;
	}
	if (cancelled) {
		m_checkOperation->requestCancellation();
	}
	const PluginOperation::Summary summary = m_checkOperation->summary();
	const QString operationID              = summary.operationID;
	const QString kind                     = summary.kind;
	m_checkInProgress                      = false;
	emit operationFinished(operationID, kind,
						 summary.totalItems == 0 && !cancelled ? QStringLiteral("succeeded") : summary.status,
						 summary.successfulItems, summary.failedItems, summary.cancelledItems);
	if (!cancelled && !availableUpdates().isEmpty()) {
		emit updatesAvailable();
	}
	m_checkOperation.reset();
}

QString PluginUpdater::update() {
	if (m_shuttingDown) return {};
	QSet< plugin_id_t > pluginIDs;
	for (const UpdateEntry &entry : availableUpdates()) {
		pluginIDs.insert(entry.pluginID);
	}
	return updateSelected(pluginIDs);
}

QString PluginUpdater::updateSelected(const QSet< plugin_id_t > &pluginIDs) {
	if (m_shuttingDown) return {};
	if (m_updateInProgress && m_updateOperation) {
		return m_updateOperation->operationID();
	}
	if (m_checkInProgress) {
		return {};
	}

	QStringList itemIDs;
	for (const plugin_id_t pluginID : pluginIDs) {
		itemIDs.push_back(pluginItemID(pluginID));
	}
	m_updateOperation = std::make_unique< PluginOperation >(QStringLiteral("plugin-update"));
	m_updateOperation->setItems(itemIDs);
	m_updateInProgress    = true;
	m_pendingPreparations = 0;
	m_updateQueue.clear();

	const QVector< UpdateEntry > available = availableUpdates();
	QSet< plugin_id_t > queuedIDs;
	for (const UpdateEntry &entry : available) {
		if (pluginIDs.contains(entry.pluginID)) {
			m_updateQueue.push_back(entry);
			queuedIDs.insert(entry.pluginID);
		}
	}
	emit operationStarted(m_updateOperation->operationID(), m_updateOperation->kind(), itemIDs.size(), true);
	for (const plugin_id_t pluginID : pluginIDs) {
		if (queuedIDs.contains(pluginID)) {
			continue;
		}
		completeUpdateEntry(UpdateEntry(pluginID, {}, {}), false, QStringLiteral("update-not-found"),
							tr("No checked update is available for this plugin"));
	}
	QTimer::singleShot(0, this, &PluginUpdater::startNextDownload);
	return m_updateOperation ? m_updateOperation->operationID() : QString();
}

QVector< UpdateEntry > PluginUpdater::availableUpdates() {
	QMutexLocker lock(&m_dataMutex);
	PluginManager *manager = Global::get().pluginManager;
	if (manager) {
		m_pluginsToUpdate.erase(
			std::remove_if(m_pluginsToUpdate.begin(), m_pluginsToUpdate.end(), [manager](const UpdateEntry &entry) {
				const std::optional< PluginDescriptor > descriptor = manager->pluginDescriptor(entry.pluginID);
				return !descriptor
					|| !pluginUpdateDestinationMatchesInstalledPath(descriptor->path, entry.pluginPath);
			}),
			m_pluginsToUpdate.end());
	}
	return m_pluginsToUpdate;
}

void PluginUpdater::startNextDownload() {
	if (m_shuttingDown) return;
	if (!m_updateInProgress || !m_updateOperation || m_currentReply || m_pendingPreparations > 0) {
		return;
	}
	if (m_updateOperation->isCancellationRequested()) {
		const QVector< UpdateEntry > queued = std::exchange(m_updateQueue, {});
		for (const UpdateEntry &entry : queued) {
			completeUpdateEntry(entry, false, QStringLiteral("cancelled"), tr("Update cancelled"), true);
		}
	}
	if (m_updateQueue.isEmpty()) {
		finishUpdateOperation();
		return;
	}

	const UpdateEntry entry = m_updateQueue.takeFirst();
	const PluginUpdateUrlValidationResult validation = validatePluginUpdateUrl(entry.updateURL);
	if (!validation.accepted) {
		completeUpdateEntry(entry, false, validation.errorCode, validation.message);
		return;
	}
	QNetworkReply *reply = m_networkManager.get(pluginUpdateNetworkRequest(entry.updateURL));
	emit updateStarted(static_cast< qulonglong >(entry.pluginID),
		entry.displayName.isEmpty() ? entry.fileName : entry.displayName);
	trackDownload(reply, entry);
}

void PluginUpdater::interrupt(const QString &operationID) {
	const bool cancelUpdate = m_updateInProgress && m_updateOperation
		&& (operationID.isEmpty() || operationID == m_updateOperation->operationID());
	const bool cancelCheck = m_checkInProgress && m_checkOperation
		&& (operationID.isEmpty() || operationID == m_checkOperation->operationID());
	if (!cancelUpdate && !cancelCheck) {
		return;
	}
	bool newlyCancelled = false;
	if (cancelUpdate) {
		newlyCancelled = m_updateOperation->requestCancellation() || newlyCancelled;
		const QVector< UpdateEntry > queued = std::exchange(m_updateQueue, {});
		for (const UpdateEntry &entry : queued) {
			completeUpdateEntry(entry, false, QStringLiteral("cancelled"), tr("Update cancelled"), true);
		}
		if (m_currentReply) {
			m_currentReply->abort();
		}
	}
	if (cancelCheck) {
		newlyCancelled = m_checkOperation->requestCancellation() || newlyCancelled;
	}
	if (newlyCancelled) {
		emit updateInterrupted();
	}
}

void PluginUpdater::completeUpdateEntry(const UpdateEntry &entry, const bool success, const QString &errorCode,
										const QString &message, const bool cancelled) {
	if (!m_updateInProgress || !m_updateOperation) {
		return;
	}
	const QString itemID = pluginItemID(entry.pluginID);
	const PluginOperation::ItemResult result { itemID, static_cast< qulonglong >(entry.pluginID), success,
												   cancelled, errorCode, message };
	if (!m_updateOperation->completeItem(result)) {
		return;
	}
	emit updateResult(static_cast< qulonglong >(entry.pluginID), success, errorCode, message);
	emit operationItemResult(m_updateOperation->operationID(), itemID,
						 static_cast< qulonglong >(entry.pluginID), success, cancelled, errorCode, message);
	const PluginOperation::Summary summary = m_updateOperation->summary();
	emit operationProgress(summary.operationID, QStringLiteral("result"), summary.completedItems,
						   summary.totalItems, static_cast< qulonglong >(entry.pluginID), -1, -1);
	if (success) {
		QMutexLocker lock(&m_dataMutex);
		m_pluginsToUpdate.erase(
			std::remove_if(m_pluginsToUpdate.begin(), m_pluginsToUpdate.end(), [entry](const UpdateEntry &available) {
				return available.pluginID == entry.pluginID;
			}),
			m_pluginsToUpdate.end());
	}
	if (!m_currentReply && m_pendingPreparations == 0) {
		QTimer::singleShot(0, this, &PluginUpdater::startNextDownload);
	}
}

void PluginUpdater::finishUpdateOperation() {
	if (!m_updateInProgress || !m_updateOperation || m_currentReply || m_pendingPreparations > 0
		|| !m_updateQueue.isEmpty()) {
		return;
	}
	const PluginOperation::Summary summary = m_updateOperation->summary();
	if (summary.completedItems < summary.totalItems) {
		return;
	}
	const QString operationID = summary.operationID;
	const QString kind        = summary.kind;
	m_updateInProgress        = false;
	emit operationFinished(operationID, kind, summary.status, summary.successfulItems, summary.failedItems,
						 summary.cancelledItems);
	emit updatingFinished();
	m_updateOperation.reset();
}

void PluginUpdater::on_updateDownloaded(QNetworkReply *reply) {
	if (!reply || !m_downloadEntries.contains(reply)) {
		return;
	}
	reply->deleteLater();
	if (m_currentReply == reply) {
		m_currentReply.clear();
	}
	QByteArray content       = m_downloadBuffers.take(reply);
	const UpdateEntry entry  = m_downloadEntries.take(reply);
	bool oversized           = m_oversizedDownloads.remove(reply);
	if (m_shuttingDown) {
		content.clear();
		return;
	}
	if (!oversized && reply->bytesAvailable() > 0) {
		const qint64 remaining = MaximumPluginDownloadBytes - content.size();
		content.append(reply->read(qMax< qint64 >(0, remaining) + 1));
		oversized = content.size() > MaximumPluginDownloadBytes;
	}

	if (!m_updateOperation || m_updateOperation->isCancellationRequested()) {
		completeUpdateEntry(entry, false, QStringLiteral("cancelled"), tr("Update cancelled"), true);
		return;
	}
	if (oversized) {
		completeUpdateEntry(entry, false, QStringLiteral("download-too-large"),
							tr("The plugin update exceeds the %1 MiB download limit")
								.arg(MaximumPluginDownloadBytes / (1024 * 1024)));
		return;
	}
	if (reply->error() != QNetworkReply::NoError) {
		completeUpdateEntry(entry, false, QStringLiteral("network-error"), reply->errorString());
		return;
	}

	const int httpStatusCode = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute).toInt();
	if (httpStatusCode >= 300 && httpStatusCode < 400) {
		if (entry.redirects >= MAX_REDIRECTS - 1) {
			completeUpdateEntry(entry, false, QStringLiteral("too-many-redirects"),
								tr("The update redirected too many times"));
			return;
		}
		UpdateEntry redirected = entry;
		redirected.updateURL = reply->url().resolved(
			reply->attribute(QNetworkRequest::RedirectionTargetAttribute).toUrl());
		++redirected.redirects;
		const PluginUpdateUrlValidationResult validation =
			validatePluginUpdateUrl(redirected.updateURL, reply->url());
		if (!validation.accepted) {
			completeUpdateEntry(entry, false, validation.errorCode, validation.message);
			return;
		}
		trackDownload(m_networkManager.get(pluginUpdateNetworkRequest(redirected.updateURL)), redirected);
		return;
	}
	if (httpStatusCode < 200 || httpStatusCode >= 300) {
		completeUpdateEntry(entry, false, QStringLiteral("http-error"),
							tr("The server returned HTTP status %1").arg(httpStatusCode));
		return;
	}
	if (content.isEmpty()) {
		completeUpdateEntry(entry, false, QStringLiteral("empty-download"), tr("The downloaded update was empty"));
		return;
	}

	const QString installDirectory = PluginInstallService::installDirectory();
	const auto cancelToken         = m_updateOperation->cancellationToken();
	++m_pendingPreparations;
	auto *prepareWatcher = new QFutureWatcher< PreparedUpdate >(this);
	m_prepareWatchers.insert(prepareWatcher);
	connect(prepareWatcher, &QFutureWatcherBase::finished, this,
			[this, prepareWatcher, entry, cancelToken]() {
				PreparedUpdate result = prepareWatcher->result();
				m_prepareWatchers.remove(prepareWatcher);
				prepareWatcher->deleteLater();
				if (m_shuttingDown) {
					--m_pendingPreparations;
					QString temporaryDirectoryPath;
					if (result.package && result.package->temporaryDirectory) {
						temporaryDirectoryPath = result.package->temporaryDirectory->path();
						result.package->temporaryDirectory->setAutoRemove(false);
					}
					const QString temporaryPath = result.downloaded.path;
					std::ignore = QtConcurrent::run([temporaryPath, temporaryDirectoryPath]() {
						removePreparedArtifacts(temporaryPath, temporaryDirectoryPath);
					});
					return;
				}
				if (!result.downloaded.errorCode.isEmpty() || !result.package) {
					--m_pendingPreparations;
					if (!result.downloaded.path.isEmpty()) {
						const QString path = result.downloaded.path;
						std::ignore        = QtConcurrent::run([path]() { QFile::remove(path); });
					}
					completeUpdateEntry(entry, false,
						result.downloaded.errorCode.isEmpty() ? QStringLiteral("package-error")
														   : result.downloaded.errorCode,
						result.downloaded.message, result.downloaded.cancelled || cancelToken->load());
					return;
				}
				const QString temporaryPath = result.downloaded.path;
				PluginInstallService::PreparedPackage package = *result.package;
				QString temporaryDirectoryPath;
				if (package.temporaryDirectory) {
					temporaryDirectoryPath = package.temporaryDirectory->path();
					// Recursive deletion of a large extracted package must not run from QTemporaryDir's
					// destructor on the GUI thread. Every terminal path below cleans it on a worker.
					package.temporaryDirectory->setAutoRemove(false);
				}
				if (cancelToken->load()) {
					--m_pendingPreparations;
					std::ignore = QtConcurrent::run([temporaryPath, temporaryDirectoryPath]() {
						removePreparedArtifacts(temporaryPath, temporaryDirectoryPath);
					});
					completeUpdateEntry(entry, false, QStringLiteral("cancelled"), tr("Update cancelled"), true);
					return;
				}
				if (!pluginUpdateDestinationMatchesInstalledPath(package.destinationPath, entry.pluginPath)) {
					--m_pendingPreparations;
					std::ignore = QtConcurrent::run([temporaryPath, temporaryDirectoryPath]() {
						removePreparedArtifacts(temporaryPath, temporaryDirectoryPath);
					});
					completeUpdateEntry(entry, false, QStringLiteral("update-path-mismatch"),
						tr("The update package does not replace the plugin that was checked"));
					return;
				}

				const QString operationID = m_updateOperation ? m_updateOperation->operationID() : QString();
				PluginOperation *const operation = m_updateOperation.get();
				const QPointer< PluginUpdater > owner(this);
				const QPointer< PluginManager > manager(Global::get().pluginManager);
				const PluginSetting pluginSetting = manager
					? manager->currentPluginSettingForPath(entry.pluginPath) : PluginSetting {};
				const bool queued = sharedPluginAbiWorker().enqueue(
					[owner, operationID, operation, entry, package, temporaryPath, temporaryDirectoryPath, cancelToken,
					 manager, pluginSetting]() {
						UpdateWorkerResult outcome;
						bool clearedExisting   = false;
						bool terminalQueued    = false;
						bool loaded             = false;
						bool fileSettled         = false;
						bool restorationAttempted = false;
						bool restorationLoaded   = false;
						bool terminalSealed      = false;
						std::optional< PluginFileCommitResult > committedState;
						const auto sealTerminal = [&]() {
							if (terminalSealed) return;
							terminalSealed = true;
							if (operation) {
								const QString itemID = pluginItemID(entry.pluginID);
								const QStringList pending = operation->pendingItems();
								if (pending.size() == 1 && pending.constFirst() == itemID && owner) {
									const PluginOperation::Summary summary = operation->summary();
									QMetaObject::invokeMethod(owner,
										[owner, operationID, summary, pluginID = entry.pluginID]() {
											if (owner) emit owner->operationProgress(operationID,
												QStringLiteral("finalize-noncancellable"), summary.completedItems,
												summary.totalItems, static_cast< qulonglong >(pluginID), -1, -1);
										}, Qt::QueuedConnection);
								}
								operation->sealItem(itemID);
							}
						};
						const auto terminalGuard = qScopeGuard([&]() {
							if (terminalQueued) return;
							sealTerminal();
							try {
								if (committedState && committedState->success && !fileSettled) {
									clearManagedPluginAtPath(manager, package.destinationPath);
									const bool rolledBack = rollbackPluginFileCommit(*committedState);
									if (rolledBack && !committedState->backupPath.isEmpty()
										&& manager && !restorationAttempted) {
										restorationAttempted = true;
										manager->reloadPluginPath(package.destinationPath, pluginSetting);
									}
								} else if (clearedExisting && manager && !restorationAttempted) {
									restorationAttempted = true;
									manager->reloadPluginPath(package.destinationPath, pluginSetting);
								}
							} catch (...) {
							}
							removePreparedArtifacts(temporaryPath, temporaryDirectoryPath);
							QMetaObject::invokeMethod(owner, [owner, operationID, entry]() {
								if (!owner || !owner->m_updateOperation
									|| owner->m_updateOperation->operationID() != operationID) return;
								--owner->m_pendingPreparations;
								owner->completeUpdateEntry(entry, false, QStringLiteral("plugin-exception"),
									PluginUpdater::tr("The plugin failed during update lifecycle processing"));
							}, Qt::QueuedConnection);
						});
						auto measure = [&outcome](const QString &phase, const std::function< void() > &call) {
							QElapsedTimer timer;
							timer.start();
							call();
							outcome.measurements.push_back({ phase, timer.elapsed() });
						};
						// Cancellation can win while this transaction waits behind another
						// plugin ABI call. Respect it before loading the downloaded library or
						// invoking any of its metadata entry points.
						if (cancelToken->load()) {
							outcome.cancelled = true;
							outcome.errorCode = QStringLiteral("cancelled");
							outcome.message   = PluginUpdater::tr("Update cancelled");
							removePreparedArtifacts(temporaryPath, temporaryDirectoryPath);
							sealTerminal();
							terminalQueued = true;
							QMetaObject::invokeMethod(owner, [owner, operationID, entry, outcome]() {
								if (!owner || !owner->m_updateOperation
									|| owner->m_updateOperation->operationID() != operationID) return;
								--owner->m_pendingPreparations;
								owner->completeUpdateEntry(entry, false, outcome.errorCode, outcome.message, true);
							}, Qt::QueuedConnection);
							return;
						}
						if (!pluginUpdateDestinationMatchesInstalledPath(package.destinationPath, entry.pluginPath)) {
							outcome.errorCode = QStringLiteral("update-path-mismatch");
							outcome.message = PluginUpdater::tr(
								"The update package does not replace the plugin that was checked");
							removePreparedArtifacts(temporaryPath, temporaryDirectoryPath);
							sealTerminal();
							terminalQueued = true;
							QMetaObject::invokeMethod(owner, [owner, operationID, entry, outcome]() {
								if (!owner || !owner->m_updateOperation
									|| owner->m_updateOperation->operationID() != operationID) return;
								--owner->m_pendingPreparations;
								owner->completeUpdateEntry(entry, false, outcome.errorCode, outcome.message);
							}, Qt::QueuedConnection);
							return;
						}
						const std::optional< PluginDescriptor > currentDescriptor = manager
							? manager->pluginDescriptor(entry.pluginID) : std::nullopt;
						if (!currentDescriptor
							|| !pluginUpdateDestinationMatchesInstalledPath(currentDescriptor->path,
								entry.pluginPath)) {
							outcome.errorCode = QStringLiteral("plugin-set-changed");
							outcome.message = PluginUpdater::tr(
								"The installed plugin changed after the update check; check for updates again");
							removePreparedArtifacts(temporaryPath, temporaryDirectoryPath);
							sealTerminal();
							terminalQueued = true;
							QMetaObject::invokeMethod(owner, [owner, operationID, entry, outcome]() {
								if (!owner || !owner->m_updateOperation
									|| owner->m_updateOperation->operationID() != operationID) return;
								--owner->m_pendingPreparations;
								owner->completeUpdateEntry(entry, false, outcome.errorCode, outcome.message);
							}, Qt::QueuedConnection);
							return;
						}
						try {
							measure(QStringLiteral("validate-unload"), [&]() {
								{
									const bool protectedCall = manager && manager->beginPluginProtectedWorkerCall();
									const auto protectedGuard = qScopeGuard([manager, protectedCall]() {
										if (manager) manager->endPluginProtectedCall(protectedCall);
									});
									PluginInstallService inspector(package);
								}
								if (cancelToken->load()) {
									return;
								}
								{
									if (manager) manager->beginPluginLifecycleCall();
									const auto lifecycleGuard = qScopeGuard([manager]() {
										if (manager) manager->endPluginLifecycleCall();
									});
									clearedExisting = clearManagedPluginAtPath(manager, package.destinationPath);
								}
							});
						} catch (const PluginInstallException &exception) {
							outcome.errorCode = QStringLiteral("install-error");
							outcome.message   = exception.getMessage();
							removePreparedArtifacts(temporaryPath, temporaryDirectoryPath);
							sealTerminal();
							terminalQueued = true;
							QMetaObject::invokeMethod(owner, [owner, operationID, entry, outcome]() {
								if (!owner || !owner->m_updateOperation
									|| owner->m_updateOperation->operationID() != operationID) return;
								--owner->m_pendingPreparations;
								for (const PluginAbiMeasurement &measurement : outcome.measurements)
									owner->emitAbiMeasurement(operationID, measurement.phase, entry.pluginID,
										measurement.elapsedMilliseconds);
								owner->completeUpdateEntry(entry, false, outcome.errorCode, outcome.message);
							}, Qt::QueuedConnection);
							return;
						}

						if (cancelToken->load()) {
							if (clearedExisting && manager) {
								restorationAttempted = true;
								measure(QStringLiteral("cancel-reload"), [&]() {
									restorationLoaded = manager->reloadPluginPath(package.destinationPath, pluginSetting);
								});
							}
							outcome.cancelled = true;
							outcome.errorCode = !clearedExisting || restorationLoaded
								? QStringLiteral("cancelled") : QStringLiteral("runtime-restore-error");
							outcome.message = !clearedExisting || restorationLoaded
								? PluginUpdater::tr("Update cancelled")
								: PluginUpdater::tr("Update cancelled, but the previous plugin could not be reloaded");
						} else {
							committedState = commitPreparedPluginFile(
								package.sourcePath, package.destinationPath, true, cancelToken.get(), {}, package.sha256);
							const PluginFileCommitResult &committed = *committedState;
							if (!committed.success) {
								if (clearedExisting && manager) {
									restorationAttempted = true;
									measure(QStringLiteral("rollback-reload"), [&]() {
										restorationLoaded = manager->reloadPluginPath(package.destinationPath, pluginSetting);
									});
								}
								outcome.cancelled = committed.cancelled;
								const bool rollbackFailed = committed.errorCode == QLatin1String("rollback-error");
								outcome.errorCode = rollbackFailed ? committed.errorCode
									: (clearedExisting && !restorationLoaded
										? QStringLiteral("runtime-restore-error") : committed.errorCode);
								outcome.message = rollbackFailed ? committed.message
									: (clearedExisting && !restorationLoaded
										? PluginUpdater::tr("The previous plugin file was restored but could not be reloaded")
										: committed.message);
							} else {
								const bool cancelledBeforeLoad = cancelToken->load();
								if (!cancelledBeforeLoad && manager) {
									measure(QStringLiteral("load"), [&]() {
										loaded = manager->reloadPluginPath(package.destinationPath, pluginSetting);
									});
								}
								sealTerminal();
								const bool cancelledAfterCommit = cancelToken->load();
								if (loaded && cancelledAfterCommit) {
									measure(QStringLiteral("cancel-unload"), [&]() {
										clearManagedPluginAtPath(manager, package.destinationPath);
									});
									loaded = false;
								} else if (!loaded && !cancelledAfterCommit) {
									measure(QStringLiteral("failed-load-unload"), [&]() {
										clearManagedPluginAtPath(manager, package.destinationPath);
									});
								}
								const bool hadBackup = !committed.backupPath.isEmpty();
								const bool fileResult = loaded ? finalizePluginFileCommit(committed)
									: rollbackPluginFileCommit(committed);
								fileSettled = true;
								if (!loaded && fileResult && hadBackup && manager) {
									restorationAttempted = true;
									measure(QStringLiteral("restore"), [&]() {
										restorationLoaded = manager->reloadPluginPath(package.destinationPath, pluginSetting);
									});
								}
								if (cancelledAfterCommit) {
									outcome.cancelled = true;
									const bool runtimeRestored = !hadBackup || restorationLoaded;
									outcome.errorCode = !fileResult ? QStringLiteral("rollback-error")
										: (runtimeRestored ? QStringLiteral("cancelled")
											: QStringLiteral("runtime-restore-error"));
									outcome.message = !fileResult
										? PluginUpdater::tr("Update cancellation could not restore the previous plugin")
										: (runtimeRestored
											? PluginUpdater::tr("Update cancelled; the previous plugin was restored")
											: PluginUpdater::tr("Update cancelled; the previous file was restored but could not be reloaded"));
								} else if (loaded && fileResult) {
									outcome.success = true;
									outcome.message = PluginUpdater::tr("Plugin updated successfully");
								} else if (!loaded) {
									outcome.errorCode = !fileResult ? QStringLiteral("rollback-error")
										: (hadBackup && !restorationLoaded ? QStringLiteral("runtime-restore-error")
											: QStringLiteral("load-error"));
									outcome.message = !fileResult
										? PluginUpdater::tr("The updated plugin failed to load and rollback failed")
										: (hadBackup && !restorationLoaded
											? PluginUpdater::tr("The previous plugin file was restored but could not be reloaded")
											: PluginUpdater::tr("The updated plugin failed to load; the previous file was restored"));
								} else {
									outcome.success   = true;
									outcome.errorCode = QStringLiteral("backup-cleanup-warning");
									outcome.message = PluginUpdater::tr(
										"The plugin was updated and is running, but its backup could not be removed");
								}
							}
						}
						sealTerminal();
						removePreparedArtifacts(temporaryPath, temporaryDirectoryPath);
						terminalQueued = true;
						QMetaObject::invokeMethod(owner, [owner, operationID, entry, outcome]() {
							if (!owner || !owner->m_updateOperation
								|| owner->m_updateOperation->operationID() != operationID) return;
							--owner->m_pendingPreparations;
							for (const PluginAbiMeasurement &measurement : outcome.measurements)
								owner->emitAbiMeasurement(operationID, measurement.phase, entry.pluginID,
									measurement.elapsedMilliseconds);
							if (outcome.success) Log::logOrDefer(Log::Information, PluginUpdater::tr("Successfully updated plugin"));
							owner->completeUpdateEntry(entry, outcome.success, outcome.errorCode, outcome.message,
								outcome.cancelled);
						}, Qt::QueuedConnection);
					});
				if (!queued) {
					--m_pendingPreparations;
					std::ignore = QtConcurrent::run([temporaryPath, temporaryDirectoryPath]() {
						removePreparedArtifacts(temporaryPath, temporaryDirectoryPath);
					});
					completeUpdateEntry(entry, false, QStringLiteral("worker-unavailable"),
						tr("The plugin worker is unavailable"));
				}
			});
	prepareWatcher->setFuture(QtConcurrent::run(
		[content = std::move(content), fileName = entry.fileName, installedPath = entry.pluginPath,
		 installDirectory, cancelToken]() mutable {
			PreparedUpdate result;
			result.downloaded = preparePluginUpdateFile(content, fileName, cancelToken.get());
			content.clear();
			content.squeeze();
			if (!result.downloaded.errorCode.isEmpty()) {
				return result;
			}
			try {
				result.package = PluginInstallService::prepare(
					result.downloaded.path, installDirectory, cancelToken.get(), installedPath);
			} catch (const PluginInstallException &exception) {
				result.downloaded.errorCode = cancelToken->load() ? QStringLiteral("cancelled")
															 : QStringLiteral("package-error");
				result.downloaded.cancelled = cancelToken->load();
				result.downloaded.message   = exception.getMessage();
			}
			return result;
		}));
}
