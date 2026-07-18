// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <limits>

#include "LegacyPlugin.h"
#include "PluginManager.h"
#include <QByteArray>
#include <QChar>
#include <QDir>
#include <QFileInfo>
#include <QFileInfoList>
#include <QHashIterator>
#include <QKeyEvent>
#include <QMutexLocker>
#include <QPointer>
#include <QElapsedTimer>
#include <QReadLocker>
#include <QScopeGuard>
#include <QThread>
#include <QTimer>
#include <QUuid>
#include <QVector>
#include <QWriteLocker>

#include "API.h"
#include "Log.h"
#include "MainWindow.h"
#include "PluginInstallService.h"
#include "PluginOperation.h"
#include "PluginUpdater.h"
#include "PluginUpdatePreparation.h"
#include "ProcessResolver.h"
#include "ServerHandler.h"
#include "Global.h"

#ifdef USE_MANUAL_PLUGIN
#	include "ManualPlugin.h"
#endif

#include <cassert>
#include <cstdint>
#include <memory>
#include <vector>
#include <utility>

#ifdef Q_OS_WIN
#	include <tlhelp32.h>
#	include <string>
#endif

#ifdef Q_OS_LINUX
#	include <QtCore/QStringList>
#endif

namespace {
constexpr int PluginShutdownDeadlineMilliseconds = 5000;

QString pluginSettingsKeyForPath(const QString &path) {
	return QLatin1String(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex());
}

QString pluginPermissionKeyForPath(const QString &path) {
	QString key = path.trimmed().isEmpty() ? QString() : QDir::cleanPath(QFileInfo(path).absoluteFilePath());
#ifdef Q_OS_WIN
	key = key.toCaseFolded();
#endif
	return key;
}

PluginSetting pluginSettingFromSnapshot(const QHash< QString, PluginSetting > &settings,
										const QString &path, PluginSetting fallback = {}) {
	fallback.path = path;
	const auto exact = settings.constFind(pluginSettingsKeyForPath(path));
	if (exact != settings.cend()) {
		PluginSetting saved = exact.value();
		saved.path = path;
		return saved;
	}
	for (auto it = settings.cbegin(); it != settings.cend(); ++it) {
		if (!it->path.trimmed().isEmpty()
			&& pluginUpdateDestinationMatchesInstalledPath(it->path, path)) {
			PluginSetting saved = it.value();
			saved.path = path;
			return saved;
		}
	}
	return fallback;
}

template< typename T > std::shared_ptr< T > workerOwnedPlugin(T *plugin) {
	return std::shared_ptr< T >(plugin, [](T *instance) {
		if (!instance) return;
		if (sharedPluginAbiWorker().isCurrentThread()) {
			delete instance;
			return;
		}
		if (!sharedPluginAbiWorker().enqueue([instance]() { delete instance; })) {
			// Only reachable during process-static teardown after the normal manager drain.
			delete instance;
		}
	});
}
} // namespace

PluginManager::PluginManager(QSet< QString > *additionalSearchPaths, QObject *p)
	: QObject(p), m_pluginCollectionLock(QReadWriteLock::NonRecursive), m_pluginHashMap(), m_positionalData(),
	  m_positionalAbiMutex(), m_serverSyncTimer(), m_positionalDataCheckTimer(), m_sentDataMutex(), m_sentData(),
	  m_activePosDataPluginLock(QReadWriteLock::NonRecursive), m_activePositionalDataPlugin(), m_updater() {
	qRegisterMetaType< mumble_plugin_id_t >("mumble_plugin_id_t");

	std::vector< QString > pluginPaths;

	// Setup search-paths
	if (additionalSearchPaths) {
		pluginPaths.insert(pluginPaths.end(), additionalSearchPaths->begin(), additionalSearchPaths->end());
	}

#ifdef Q_OS_MAC
	// Path to plugins inside AppBundle
	pluginPaths.push_back(QString::fromLatin1("%1/../Plugins").arg(qApp->applicationDirPath()));
#endif

#ifdef MUMBLE_PLUGIN_PATH
	// Path to where plugins are/will be installed on the system
	pluginPaths.push_back(QString::fromLatin1(MUMTEXT(MUMBLE_PLUGIN_PATH)));
#endif

	// Path to "plugins" dir right next to the executable's location. This is the case for when Mumble
	// is run after compilation without having installed it anywhere special
	pluginPaths.push_back(
		QString::fromLatin1("%1/plugins").arg(MumbleApplication::instance()->applicationVersionRootPath()));

	// Path to where the plugin installer will write plugins
	pluginPaths.push_back(PluginInstallService::installDirectory());

	for (const QString &currentPath : pluginPaths) {
		// Transform currentPath to an absolute, canonical path and only then add it to m_pluginSearchPaths in order
		// to ensure that each path is contained only once.
		QDir dir(currentPath);

		if (dir.exists()) {
			m_pluginSearchPaths.insert(dir.canonicalPath());
		}
	}

#ifdef Q_OS_WIN
	// According to MS KB Q131065, we need this to OpenProcess()

	m_hToken = nullptr;

	if (!OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &m_hToken)) {
		if (GetLastError() == ERROR_NO_TOKEN) {
			ImpersonateSelf(SecurityImpersonation);
			OpenThreadToken(GetCurrentThread(), TOKEN_ADJUST_PRIVILEGES | TOKEN_QUERY, FALSE, &m_hToken);
		}
	}

	TOKEN_PRIVILEGES tp;
	LUID luid;
	m_cbPrevious = sizeof(TOKEN_PRIVILEGES);

	LookupPrivilegeValue(NULL, SE_DEBUG_NAME, &luid);

	tp.PrivilegeCount           = 1;
	tp.Privileges[0].Luid       = luid;
	tp.Privileges[0].Attributes = SE_PRIVILEGE_ENABLED;

	AdjustTokenPrivileges(m_hToken, FALSE, &tp, sizeof(TOKEN_PRIVILEGES), &m_tpPrevious, &m_cbPrevious);
#endif

	// Synchronize the positional data in a regular interval
	QObject::connect(&m_serverSyncTimer, &QTimer::timeout, this, &PluginManager::on_syncPositionalData);
	m_serverSyncTimer.start(POSITIONAL_SERVER_SYNC_INTERVAL);

	// Install this manager as a global eventFilter in order to get notified about all keypresses
	if (QCoreApplication::instance()) {
		QCoreApplication::instance()->installEventFilter(this);
	}

	// Set up the timer for regularly checking for available positional data plugins
	m_positionalDataCheckTimer.setInterval(POSITIONAL_DATA_CHECK_INTERVAL);
	m_positionalDataCheckTimer.start();
	QObject::connect(&m_positionalDataCheckTimer, &QTimer::timeout, this,
					 &PluginManager::checkForAvailablePositionalDataPlugin);

	QObject::connect(&m_updater, &PluginUpdater::updatesAvailable, this, &PluginManager::on_updatesAvailable);
	QObject::connect(&m_updater, &PluginUpdater::updateStarted, this,
					 [this](const qulonglong pluginID, const QString &fileName) {
						 emit pluginUpdateStarted(pluginID, fileName);
					 });
	QObject::connect(&m_updater, &PluginUpdater::updateProgress, this, &PluginManager::pluginUpdateProgress);
	QObject::connect(&m_updater, &PluginUpdater::updateResult, this, &PluginManager::pluginUpdateResult);
	QObject::connect(&m_updater, &PluginUpdater::updatingFinished, this, &PluginManager::pluginUpdatesFinished);
	QObject::connect(&m_updater, &PluginUpdater::updateInterrupted, this, &PluginManager::pluginUpdatesInterrupted);
	QObject::connect(&m_updater, &PluginUpdater::operationStarted, this, &PluginManager::pluginOperationStarted);
	QObject::connect(&m_updater, &PluginUpdater::operationProgress, this, &PluginManager::pluginOperationProgress);
	QObject::connect(&m_updater, &PluginUpdater::operationItemResult, this,
					 &PluginManager::pluginOperationItemResult);
	QObject::connect(&m_updater, &PluginUpdater::operationFinished, this, &PluginManager::pluginOperationFinished);
	QObject::connect(&m_updater, &PluginUpdater::abiStepMeasured, this, &PluginManager::pluginAbiStepMeasured);
	QObject::connect(this, &PluginManager::keyEvent, this, &PluginManager::on_keyEvent);
	QObject::connect(this, &PluginManager::pluginLostLink, this, &PluginManager::reportLostLink);
	QObject::connect(this, &PluginManager::pluginLinked, this, &PluginManager::reportPluginLinked);
	QObject::connect(this, &PluginManager::pluginEncounteredPermanentError, this, &PluginManager::reportPermanentError);
}

PluginManager::~PluginManager() {
	QElapsedTimer shutdownDeadline;
	shutdownDeadline.start();
	m_shuttingDown.store(true);
	m_serverSyncTimer.stop();
	m_positionalDataCheckTimer.stop();
	if (QCoreApplication::instance()) {
		QCoreApplication::instance()->removeEventFilter(this);
	}
	if (m_asyncRescanCancellation) {
		m_asyncRescanCancellation->requestCancellation();
	}
	const qint64 updaterBudget = PluginShutdownDeadlineMilliseconds - shutdownDeadline.elapsed();
	if (updaterBudget <= 0) {
		qFatal("Plugin updater teardown exceeded the 5 s process-shutdown boundary");
	}
	m_updater.shutdownAndWait(static_cast< int >(updaterBudget));
	// Producer admission is now closed: the rescan and update preparation futures are drained, timers/event filters
	// are stopped and updater networking is cancelled. Atomically close the shared ABI queue while appending the
	// final collection cleanup, so no late callback can slip behind the teardown barrier.
	const bool cleanupQueued = sharedPluginAbiWorker().shutdown([this]() { clearPlugins(); });
	const qint64 remaining = PluginShutdownDeadlineMilliseconds - shutdownDeadline.elapsed();
	if (remaining <= 0 || !sharedPluginAbiWorker().waitForDone(static_cast< int >(remaining))) {
		qFatal("Plugin collection cleanup exceeded the 5 s process-shutdown boundary");
	}
	if (!cleanupQueued) {
		// Only possible for an abnormal second manager teardown after the process-wide worker has already closed.
		Q_ASSERT_X(false, "PluginManager::~PluginManager", "plugin ABI worker was closed before manager cleanup");
		clearPlugins();
	}

#ifdef Q_OS_WIN
	AdjustTokenPrivileges(m_hToken, FALSE, &m_tpPrevious, m_cbPrevious, NULL, NULL);
	CloseHandle(m_hToken);
#endif
}

bool PluginManager::eventFilter(QObject *target, QEvent *event) {
	if (event->type() == QEvent::KeyPress || event->type() == QEvent::KeyRelease) {
		static QVector< QKeyEvent * > processedEvents;

		QKeyEvent *kEvent = static_cast< QKeyEvent * >(event);

		// We have to keep track of which events we have processed already as
		// the same event might be sent to multiple targets and since this is
		// installed as a global event filter, we get notified about each of
		// them. However we want to process each event only once.
		if (!kEvent->isAutoRepeat() && !processedEvents.contains(kEvent)) {
			// Fire event
			emit keyEvent(static_cast< unsigned int >(kEvent->key()), kEvent->modifiers(),
						  kEvent->type() == QEvent::KeyPress);

			processedEvents << kEvent;

			if (processedEvents.size() == 1) {
				// Make sure to clear the list of processed events after each iteration
				// of the event loop (we don't want to let the vector grow to infinity
				// over time. Firing the timer only when the size of processedEvents is
				// exactly 1, we avoid adding multiple timers in a single iteration.
				QTimer::singleShot(0, []() { processedEvents.clear(); });
			}
		}
	}

	// standard event processing
	return QObject::eventFilter(target, event);
}

void PluginManager::unloadPlugins() {
	QVector< plugin_ptr_t > plugins;
	{
		QReadLocker lock(&m_pluginCollectionLock);
		plugins.reserve(m_pluginHashMap.size());
		for (auto it = m_pluginHashMap.cbegin(); it != m_pluginHashMap.cend(); ++it)
			plugins.push_back(it.value());
	}
	for (const plugin_ptr_t &plugin : std::as_const(plugins)) {
		if (!plugin) continue;
		try {
			unloadPlugin(*plugin);
		} catch (const std::exception &exception) {
			Log::logOrDefer(Log::Warning,
				PluginManager::tr("Plugin shutdown failed: %1").arg(QString::fromUtf8(exception.what())));
		} catch (...) {
			Log::logOrDefer(Log::Warning, PluginManager::tr("Plugin shutdown failed with an unknown exception"));
		}
	}
}

void PluginManager::clearPlugins() {
	// The final shutdown task is serialized on the ABI worker, but runtime/audio callbacks admitted before
	// m_shuttingDown was raised may still be in flight. Hold the outer lifecycle gate across active and staged
	// shutdown plus wrapper release so no QLibrary can unload underneath one of those callbacks.
	beginPluginLifecycleCall();
	const auto lifecycleGuard = qScopeGuard([this]() { endPluginLifecycleCall(); });
	m_activePositionalPermissionAllowed.store(false);
	m_positionalSampleValid.store(false);
	plugin_ptr_t detachedActivePlugin;
	{
		QWriteLocker lock(&m_activePosDataPluginLock);
		detachedActivePlugin.swap(m_activePositionalDataPlugin);
	}
	{
		QWriteLocker posDataLock(&m_positionalData.m_lock);
		m_positionalSampleValid.store(false);
		m_positionalData.reset();
	}
	// Unload plugins so that they aren't implicitly unloaded once they go out of scope after having been
	// removed from the pluginHashMap.
	// This could lead to one of the plugins making an API call in its shutdown function which then would try
	// to verify the plugin's ID. For that it'll ask this PluginManager for a plugin with that ID. To check
	// that it will have to acquire a read-lock for the pluginHashMap which is impossible after we acquire the
	// write-lock in this function leading to a deadlock.
	unloadPlugins();
	QVector< plugin_ptr_t > stagedPlugins;
	{
		QReadLocker lock(&m_pluginCollectionLock);
		stagedPlugins.reserve(m_pluginStagingHashMap.size());
		for (auto it = m_pluginStagingHashMap.cbegin(); it != m_pluginStagingHashMap.cend(); ++it)
			stagedPlugins.push_back(it.value());
	}
	for (const plugin_ptr_t &plugin : stagedPlugins) {
		try {
			if (plugin) unloadPlugin(*plugin);
		} catch (...) {
		}
	}

	QHash< plugin_id_t, plugin_ptr_t > plugins;
	QHash< plugin_id_t, plugin_ptr_t > stagingPlugins;
	{
		QWriteLocker lock(&m_pluginCollectionLock);
		plugins.swap(m_pluginHashMap);
		stagingPlugins.swap(m_pluginStagingHashMap);
		if (!plugins.isEmpty()) ++m_pluginCollectionGeneration;
	}
	{
		QWriteLocker lock(&m_pluginDescriptorLock);
		m_pluginDescriptors.clear();
		m_pluginStagingDescriptors.clear();
	}
	// Release worker-owned plugin wrappers without holding either manager lock. A plugin destructor may call back
	// into the API while its library is being detached.
	detachedActivePlugin.reset();
	plugins.clear();
	stagingPlugins.clear();
}

void PluginManager::beginPluginLifecycleCall() const {
	QMutexLocker lock(&m_pluginAbiGateMutex);
	const Qt::HANDLE currentThread = QThread::currentThreadId();
	if (m_pluginLifecycleCallActive && m_pluginLifecycleThread == currentThread) {
		++m_pluginLifecycleDepth;
		return;
	}
	// Plugin-owned dialogs and worker metadata inspection may be arbitrarily long. Wait without advertising a
	// pending lifecycle writer so audio/event callbacks continue to flow until the protected call actually ends.
	while (m_pluginProtectedCallActive) {
		m_pluginAbiGateChanged.wait(&m_pluginAbiGateMutex);
	}
	++m_waitingPluginLifecycleCalls;
	while (m_pluginLifecycleCallActive || m_activePluginRuntimeCalls > 0) {
		m_pluginAbiGateChanged.wait(&m_pluginAbiGateMutex);
	}
	--m_waitingPluginLifecycleCalls;
	m_pluginLifecycleCallActive = true;
	m_pluginLifecycleThread     = currentThread;
	m_pluginLifecycleDepth      = 1;
}

void PluginManager::endPluginLifecycleCall() const {
	QMutexLocker lock(&m_pluginAbiGateMutex);
	Q_ASSERT(m_pluginLifecycleCallActive);
	Q_ASSERT(m_pluginLifecycleThread == QThread::currentThreadId());
	if (--m_pluginLifecycleDepth == 0) {
		m_pluginLifecycleCallActive = false;
		m_pluginLifecycleThread     = nullptr;
		m_pluginAbiGateChanged.wakeAll();
	}
}

PluginManager::PluginRuntimeCallAdmission PluginManager::beginPluginRuntimeCall() const {
	if (m_shuttingDown.load()) return PluginRuntimeCallAdmission::Rejected;
	QMutexLocker lock(&m_pluginAbiGateMutex);
	const Qt::HANDLE currentThread = QThread::currentThreadId();
	if (m_pluginLifecycleCallActive && m_pluginLifecycleThread == currentThread) {
		// A plugin may call back into the API during init/shutdown. Re-entry on the exclusive worker thread is safe
		// and must not wait on itself.
		return PluginRuntimeCallAdmission::Reentrant;
	}
	if (m_pluginLifecycleCallActive || m_waitingPluginLifecycleCalls > 0) {
		return PluginRuntimeCallAdmission::Rejected;
	}
	++m_activePluginRuntimeCalls;
	return PluginRuntimeCallAdmission::Counted;
}

void PluginManager::endPluginRuntimeCall(const PluginRuntimeCallAdmission admission) const {
	if (admission != PluginRuntimeCallAdmission::Counted) return;
	QMutexLocker lock(&m_pluginAbiGateMutex);
	Q_ASSERT(m_activePluginRuntimeCalls > 0);
	if (--m_activePluginRuntimeCalls == 0) m_pluginAbiGateChanged.wakeAll();
}

bool PluginManager::beginPluginProtectedWorkerCall() const {
	QMutexLocker lock(&m_pluginAbiGateMutex);
	if (m_pluginLifecycleCallActive && m_pluginLifecycleThread == QThread::currentThreadId()) {
		return false;
	}
	while (m_pluginProtectedCallActive || m_pluginLifecycleCallActive) {
		m_pluginAbiGateChanged.wait(&m_pluginAbiGateMutex);
	}
	m_pluginProtectedCallActive = true;
	return true;
}

bool PluginManager::tryBeginPluginProtectedCall() const {
	QMutexLocker lock(&m_pluginAbiGateMutex);
	if (m_pluginProtectedCallActive || m_pluginLifecycleCallActive || m_waitingPluginLifecycleCalls > 0) return false;
	m_pluginProtectedCallActive = true;
	return true;
}

void PluginManager::endPluginProtectedCall(const bool counted) const {
	if (!counted) return;
	QMutexLocker lock(&m_pluginAbiGateMutex);
	Q_ASSERT(m_pluginProtectedCallActive);
	m_pluginProtectedCallActive = false;
	m_pluginAbiGateChanged.wakeAll();
}

void PluginManager::refreshPluginDescriptor(const plugin_ptr_t &plugin, const bool staging) const {
	if (!plugin) return;
	Q_ASSERT(sharedPluginAbiWorker().isCurrentThread());
	const bool protectedCall = beginPluginProtectedWorkerCall();
	const auto protectedGuard = qScopeGuard([this, protectedCall]() { endPluginProtectedCall(protectedCall); });

	PluginDescriptor descriptor;
	descriptor.id                        = plugin->getID();
	descriptor.path                      = plugin->getFilePath();
	descriptor.loaded                    = plugin->isLoaded();
	descriptor.positionalDataEnabled     = plugin->isPositionalDataEnabled();
	descriptor.keyboardMonitoringAllowed = plugin->isKeyboardMonitoringAllowed();
	descriptor.builtIn                   = plugin->isBuiltInPlugin();
	bool metadataFailed                  = false;
	try {
		descriptor.name = plugin->getName();
	} catch (...) {
		metadataFailed = true;
	}
	try {
		descriptor.description = plugin->getDescription();
	} catch (...) {
		metadataFailed = true;
	}
	try {
		const mumble_version_t version = plugin->getVersion();
		descriptor.version = version == MUMBLE_VERSION_UNKNOWN ? PluginManager::tr("Unknown")
													 : static_cast< QString >(version);
	} catch (...) {
		metadataFailed = true;
	}
	try {
		descriptor.author = plugin->getAuthor();
	} catch (...) {
		metadataFailed = true;
	}
	try {
		descriptor.features = plugin->getFeatures();
	} catch (...) {
		metadataFailed = true;
	}
	try {
		descriptor.canConfigure = plugin->providesConfigDialog();
	} catch (...) {
		metadataFailed = true;
	}
	try {
		descriptor.canShowAbout = plugin->providesAboutDialog();
	} catch (...) {
		metadataFailed = true;
	}

	if (descriptor.name.trimmed().isEmpty()) {
		descriptor.name = QFileInfo(descriptor.path).completeBaseName();
		if (descriptor.name.isEmpty()) descriptor.name = PluginManager::tr("Unknown plugin");
	}
	const auto metadataIsUnknown = [](const QString &value) {
		const QString normalized = value.trimmed();
		return normalized.isEmpty()
			|| normalized.compare(QStringLiteral("Unknown"), Qt::CaseInsensitive) == 0
			|| normalized.compare(PluginManager::tr("Unknown"), Qt::CaseInsensitive) == 0;
	};
	if (descriptor.builtIn) {
		// Legacy built-ins do not expose author/version callbacks. Present their
		// packaging status instead of publishing two misleading Unknown values to
		// every frontend consuming the descriptor snapshot.
		if (metadataIsUnknown(descriptor.version)) descriptor.version = PluginManager::tr("Built in");
		if (metadataIsUnknown(descriptor.author)) descriptor.author.clear();
	}
	if (descriptor.version.isEmpty()) descriptor.version = PluginManager::tr("Unknown");
	if (descriptor.author.isEmpty() && !descriptor.builtIn) descriptor.author = PluginManager::tr("Unknown");

	{
		QWriteLocker lock(&m_pluginDescriptorLock);
		(staging ? m_pluginStagingDescriptors : m_pluginDescriptors).insert(descriptor.id, descriptor);
	}
	if (metadataFailed) {
		Log::logOrDefer(Log::Warning,
			PluginManager::tr("Plugin metadata could not be read completely for %1").arg(descriptor.name));
	}
}

void PluginManager::setPluginDescriptorPositionalEnabled(const plugin_id_t pluginID, const bool enabled) const {
	QWriteLocker lock(&m_pluginDescriptorLock);
	auto it = m_pluginDescriptors.find(pluginID);
	if (it != m_pluginDescriptors.end()) it->positionalDataEnabled = enabled;
}

void PluginManager::setPluginDescriptorKeyboardMonitoringAllowed(const plugin_id_t pluginID,
																 const bool allowed) const {
	QWriteLocker lock(&m_pluginDescriptorLock);
	auto it = m_pluginDescriptors.find(pluginID);
	if (it != m_pluginDescriptors.end()) it->keyboardMonitoringAllowed = allowed;
}

bool PluginManager::isPluginPositionalPermissionGranted(const QString &path) const {
	if (!m_hasPositionalDeniedPlugins.load()) return true;
	QReadLocker lock(&m_pluginPermissionLock);
	return !m_positionalDeniedPluginPaths.contains(pluginPermissionKeyForPath(path));
}

bool PluginManager::isPluginKeyboardPermissionGranted(const QString &path) const {
	QReadLocker lock(&m_pluginPermissionLock);
	return !m_keyboardDeniedPluginPaths.contains(pluginPermissionKeyForPath(path));
}

bool PluginManager::isPluginRuntimeEnabled(const QString &path) const {
	if (!m_hasRuntimeDeniedPlugins.load()) return true;
	QReadLocker lock(&m_pluginPermissionLock);
	return !m_runtimeDeniedPluginPaths.contains(pluginPermissionKeyForPath(path));
}

PluginSetting PluginManager::desiredPluginSettingForPath(const QString &path,
											 const PluginSetting &fallback) const {
	PluginSetting effective = fallback;
	effective.path = path;
	QReadLocker lock(&m_pluginPermissionLock);
	const auto desired = m_desiredPluginPermissions.constFind(pluginPermissionKeyForPath(path));
	if (desired != m_desiredPluginPermissions.cend()) {
		effective = *desired;
		effective.path = path;
	}
	return effective;
}

PluginSetting PluginManager::currentPluginSettingForPath(const QString &path) const {
	PluginSetting setting;
	setting.path = QFileInfo(path).absoluteFilePath();
	for (const PluginDescriptor &descriptor : pluginDescriptors()) {
		if (!pluginUpdateDestinationMatchesInstalledPath(descriptor.path, setting.path)) continue;
		setting.path                    = descriptor.path;
		setting.enabled                 = descriptor.loaded;
		setting.positionalDataEnabled   = descriptor.positionalDataEnabled;
		setting.allowKeyboardMonitoring = descriptor.keyboardMonitoringAllowed;
		break;
	}

	return pluginSettingFromSnapshot(Global::get().s.qhPluginSettings, setting.path, setting);
}

void PluginManager::recordAppliedPluginPermissions(const QString &path, const PluginSetting &setting,
											 const bool operationSucceeded) {
	const QString key = pluginPermissionKeyForPath(path);
	QWriteLocker lock(&m_pluginPermissionLock);
	const auto desired = m_desiredPluginPermissions.constFind(key);
	if (desired != m_desiredPluginPermissions.cend()
		&& (desired->enabled != setting.enabled
			|| desired->positionalDataEnabled != setting.positionalDataEnabled
			|| desired->allowKeyboardMonitoring != setting.allowKeyboardMonitoring)) {
		// An older rescan/settings transaction reached the worker after a newer desired state was published.
		// Never lift the newer fail-closed denial until that exact newer grant has actually been applied.
		return;
	}
	const bool runtimeEnabled = operationSucceeded && setting.enabled;
	if (runtimeEnabled && setting.positionalDataEnabled)
		m_positionalDeniedPluginPaths.remove(key);
	else
		m_positionalDeniedPluginPaths.insert(key);
	m_hasPositionalDeniedPlugins.store(!m_positionalDeniedPluginPaths.isEmpty());
	if (runtimeEnabled && setting.allowKeyboardMonitoring)
		m_keyboardDeniedPluginPaths.remove(key);
	else
		m_keyboardDeniedPluginPaths.insert(key);
	if (runtimeEnabled)
		m_runtimeDeniedPluginPaths.remove(key);
	else
		m_runtimeDeniedPluginPaths.insert(key);
	m_hasRuntimeDeniedPlugins.store(!m_runtimeDeniedPluginPaths.isEmpty());
}

void PluginManager::applyImmediatePluginPermissionRevocations(
	const QHash< QString, PluginSetting > &settings) {
	QSet< QString > positionalRevocations;
	QSet< QString > keyboardRevocations;
	QSet< QString > runtimeRevocations;
	QHash< QString, PluginSetting > desiredUpdates;
	for (const PluginDescriptor &descriptor : pluginDescriptors()) {
		const QString settingsKey = pluginSettingsKeyForPath(descriptor.path);
		if (!settings.contains(settingsKey)) continue;
		PluginSetting setting = settings.value(settingsKey);
		setting.path = descriptor.path;
		const QString key = pluginPermissionKeyForPath(setting.path);
		if (!setting.enabled || !setting.positionalDataEnabled) positionalRevocations.insert(key);
		if (!setting.enabled || !setting.allowKeyboardMonitoring) keyboardRevocations.insert(key);
		if (!setting.enabled) runtimeRevocations.insert(key);
		desiredUpdates.insert(key, setting);
	}
	{
		QWriteLocker lock(&m_pluginPermissionLock);
		for (auto it = desiredUpdates.cbegin(); it != desiredUpdates.cend(); ++it)
			m_desiredPluginPermissions.insert(it.key(), it.value());
		m_positionalDeniedPluginPaths.unite(positionalRevocations);
		m_hasPositionalDeniedPlugins.store(!m_positionalDeniedPluginPaths.isEmpty());
		m_keyboardDeniedPluginPaths.unite(keyboardRevocations);
		m_runtimeDeniedPluginPaths.unite(runtimeRevocations);
		m_hasRuntimeDeniedPlugins.store(!m_runtimeDeniedPluginPaths.isEmpty());
	}
	{
		QWriteLocker lock(&m_pluginDescriptorLock);
		for (auto it = m_pluginDescriptors.begin(); it != m_pluginDescriptors.end(); ++it) {
			const QString key = pluginPermissionKeyForPath(it->path);
			if (positionalRevocations.contains(key)) it->positionalDataEnabled = false;
			if (keyboardRevocations.contains(key)) it->keyboardMonitoringAllowed = false;
		}
	}
	bool activePositionalRevoked = false;
	{
		QReadLocker activeLock(&m_activePosDataPluginLock);
		if (m_activePositionalDataPlugin
			&& positionalRevocations.contains(pluginPermissionKeyForPath(m_activePositionalDataPlugin->getFilePath()))) {
			m_activePositionalPermissionAllowed.store(false);
			activePositionalRevoked = true;
		}
	}
	if (activePositionalRevoked) {
		QWriteLocker posDataLock(&m_positionalData.m_lock);
		m_positionalSampleValid.store(false);
		m_positionalData.reset();
	}
}

bool PluginManager::applySavedPluginSettings(const plugin_ptr_t &plugin, const PluginSetting &setting,
											  const bool publishDescriptor) {
	if (!plugin) {
		return false;
	}
	const QString pluginPath       = plugin->getFilePath();
	const PluginSetting effective = desiredPluginSettingForPath(pluginPath, setting);
	beginPluginLifecycleCall();
	const auto lifecycleGuard = qScopeGuard([this]() { endPluginLifecycleCall(); });
	if (!effective.positionalDataEnabled) {
		bool isActive = false;
		{
			QReadLocker lock(&m_activePosDataPluginLock);
			isActive = m_activePositionalDataPlugin && m_activePositionalDataPlugin.get() == plugin.get();
		}
		if (isActive) unlinkPositionalData();
	}

	bool loadSucceeded = true;
	if (effective.enabled && !plugin->isLoaded()) {
		loadSucceeded = plugin->init() == MUMBLE_STATUS_OK;
	}

	if (!effective.positionalDataEnabled && plugin->isLoaded()
		&& (plugin->getFeatures() & MUMBLE_FEATURE_POSITIONAL)) {
		plugin->deactivateFeatures(MUMBLE_FEATURE_POSITIONAL);
	}
	plugin->enablePositionalData(effective.positionalDataEnabled);
	plugin->allowKeyboardMonitoring(effective.allowKeyboardMonitoring);
	if (publishDescriptor) refreshPluginDescriptor(plugin);
	recordAppliedPluginPermissions(pluginPath, effective, loadSucceeded);

	return loadSucceeded;
}

bool PluginManager::selectActivePositionalDataPlugin() {
	if (m_shuttingDown.load()) return false;
	if (!sharedPluginAbiWorker().isCurrentThread()) {
		if (m_positionalSelectionPending.exchange(true)) return false;
		m_transmitPositionForSelection.store(Global::get().s.bTransmitPosition);
		const bool queued = sharedPluginAbiWorker().enqueue([this]() {
			const auto pendingGuard = qScopeGuard([this]() { m_positionalSelectionPending.store(false); });
			if (m_shuttingDown.load()) return;
			try {
				std::ignore = selectActivePositionalDataPlugin();
			} catch (const std::exception &exception) {
				if (!m_shuttingDown.load()) Log::logOrDefer(Log::Warning,
					PluginManager::tr("Positional plugin initialization failed: %1")
						.arg(QString::fromUtf8(exception.what())));
			} catch (...) {
				if (!m_shuttingDown.load()) Log::logOrDefer(Log::Warning,
					PluginManager::tr("Positional plugin initialization failed with an unknown exception"));
			}
		});
		if (!queued) m_positionalSelectionPending.store(false);
		return false;
	}
	beginPluginLifecycleCall();
	const auto lifecycleGuard = qScopeGuard([this]() { endPluginLifecycleCall(); });
	m_activePositionalPermissionAllowed.store(false);
	m_positionalSampleValid.store(false);
	{
		QWriteLocker posDataLock(&m_positionalData.m_lock);
		m_positionalSampleValid.store(false);
		m_positionalData.reset();
	}
	QVector< plugin_ptr_t > plugins;
	{
		QReadLocker pluginLock(&m_pluginCollectionLock);
		plugins.reserve(m_pluginHashMap.size());
		for (auto it = m_pluginHashMap.cbegin(); it != m_pluginHashMap.cend(); ++it)
			plugins.push_back(it.value());
	}

	if (!m_transmitPositionForSelection.load()) {
		// According to the settings the position shall not be transmitted meaning that we don't have to select any
		// plugin for positional data
		QWriteLocker activePluginLock(&m_activePosDataPluginLock);
		m_activePositionalDataPlugin.reset();

		return false;
	}

	const ProcessResolver procRes(true);
	const ProcessResolver::ProcessMap &map = procRes.getProcessMap();

	// We require 2 separate arrays holding the names and the PIDs -> create them from the given map
	std::vector< uint64_t > pids;
	std::vector< const char * > names;
	pids.reserve(procRes.amountOfProcesses());
	names.reserve(procRes.amountOfProcesses());
	for (const std::pair< const uint64_t, std::unique_ptr< char[] > > &currentEntry : map) {
		pids.push_back(currentEntry.first);
		names.push_back(currentEntry.second.get());
	}

	// We assume that there is only one (enabled) plugin for the currently played game so we don't have to remember
	// which plugin was active last
	for (const plugin_ptr_t &currentPlugin : std::as_const(plugins)) {
		if (m_shuttingDown.load()) break;

		const std::optional< PluginDescriptor > descriptor = pluginDescriptor(currentPlugin->getID());
		if (descriptor && descriptor->positionalDataEnabled
			&& isPluginPositionalPermissionGranted(descriptor->path) && currentPlugin->isPositionalDataEnabled()
			&& currentPlugin->isLoaded()) {
				switch (currentPlugin->initPositionalData(names.data(), pids.data(), procRes.amountOfProcesses())) {
				case MUMBLE_PDEC_OK: {
					// A Settings revoke can arrive while the third-party init call is still running. Recheck the
					// path-keyed desired state before publishing this plugin as active.
					if (m_shuttingDown.load()
						|| !isPluginPositionalPermissionGranted(currentPlugin->getFilePath())) {
						currentPlugin->shutdownPositionalData();
						break;
					}
					// the plugin is ready to provide positional data
					{
						QWriteLocker activePluginLock(&m_activePosDataPluginLock);
						m_activePositionalDataPlugin = currentPlugin;
					}
					m_activePositionalPermissionAllowed.store(true);
					// Publish the pointer before the final permission check. An immediate revoke either observes the
					// active pointer and clears the atomic itself, or is observed here before any data can be fetched.
					if (m_shuttingDown.load()
						|| !isPluginPositionalPermissionGranted(currentPlugin->getFilePath())) {
						m_activePositionalPermissionAllowed.store(false);
						m_positionalSampleValid.store(false);
						{
							QWriteLocker activePluginLock(&m_activePosDataPluginLock);
							if (m_activePositionalDataPlugin == currentPlugin)
								m_activePositionalDataPlugin.reset();
						}
						currentPlugin->shutdownPositionalData();
						break;
					}

					const plugin_id_t pluginID = currentPlugin->getID();
					const QPointer< PluginManager > owner(this);
					QMetaObject::invokeMethod(owner, [owner, pluginID]() {
						if (owner && !owner->m_shuttingDown.load()) emit owner->pluginLinked(pluginID);
					}, Qt::QueuedConnection);

					return true;
				}

				case MUMBLE_PDEC_ERROR_PERM: {
					if (m_shuttingDown.load()) break;
					// the plugin encountered a permanent error -> disable it
					const plugin_id_t pluginID = currentPlugin->getID();
					const QPointer< PluginManager > owner(this);
					QMetaObject::invokeMethod(owner, [owner, pluginID]() {
						if (owner && !owner->m_shuttingDown.load())
							emit owner->pluginEncounteredPermanentError(pluginID);
					}, Qt::QueuedConnection);

					currentPlugin->enablePositionalData(false);
					setPluginDescriptorPositionalEnabled(pluginID, false);
					break;
				}

				case MUMBLE_PDEC_ERROR_TEMP:
					// The plugin encountered a temporary error -> skip it for now (that is: do nothing)
					break;
			}
		}

	}

	{
		QWriteLocker activePluginLock(&m_activePosDataPluginLock);
		m_activePositionalDataPlugin.reset();
	}

	return false;
}

#define LOG_FOUND(plugin, path, legacyStr)                                                                       \
	qDebug("Found %splugin '%s' at \"%s\"", legacyStr, qUtf8Printable(plugin->getName()), qUtf8Printable(path)); \
	qDebug() << "Its description:" << qUtf8Printable(plugin->getDescription())
#define LOG_FOUND_PLUGIN(plugin, path) LOG_FOUND(plugin, path, "")
#define LOG_FOUND_LEGACY_PLUGIN(plugin, path) LOG_FOUND(plugin, path, "legacy ")
#define LOG_FOUND_BUILTIN(plugin) LOG_FOUND(plugin, QString::fromLatin1("<builtin>"), "built-in ")
void PluginManager::rescanPlugins() {
	std::ignore = rescanPluginsAsync();
}

QString PluginManager::rescanPluginsAsync() {
	if (m_shuttingDown.load()) return {};
	if (m_asyncRescanInProgress) return m_asyncRescanOperationID;
	m_asyncRescanInProgress = true;
	m_asyncRescanOperationID = QStringLiteral("plugin-rescan:%1")
		.arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	m_asyncRescanCancellation = std::make_shared< PluginCancellationGate >();
	const QString operationID = m_asyncRescanOperationID;
	const auto cancelGate = m_asyncRescanCancellation;
	const auto cancelToken = cancelGate->cancellationToken();
	emit pluginOperationStarted(operationID, QStringLiteral("plugin-rescan"), -1, true);
	const QStringList searchPaths = m_pluginSearchPaths.values();
	const QString installDirectory = QDir(PluginInstallService::installDirectory()).absolutePath();
	// Snapshot GUI-owned settings before leaving the owner thread. A later Settings apply is itself queued by path
	// and therefore deterministically runs after this rescan if it was requested while discovery was in progress.
	const QHash< QString, PluginSetting > settingsSnapshot = Global::get().s.qhPluginSettings;
	const QPointer< PluginManager > owner(this);
	const bool queued = sharedPluginAbiWorker().enqueue(
		[this, owner, operationID, cancelGate, cancelToken, searchPaths, installDirectory, settingsSnapshot]() {
		const auto cancellationRequested = [this, &cancelGate, &cancelToken]() {
			return !cancelGate || cancelGate->isCancellationRequested() || cancelToken->load()
				|| m_shuttingDown.load();
		};
		const auto finishCancelledBeforeDiscovery = [owner, operationID]() {
			QMetaObject::invokeMethod(owner, [owner, operationID]() {
				if (!owner || owner->m_asyncRescanOperationID != operationID) return;
				owner->m_asyncRescanInProgress = false;
				owner->m_asyncRescanCancellation.reset();
				owner->m_asyncRescanOperationID.clear();
				emit owner->pluginOperationFinished(operationID, QStringLiteral("plugin-rescan"),
					QStringLiteral("cancelled"), 0, 0, 0);
				emit owner->pluginRescanFinished(false);
			}, Qt::QueuedConnection);
		};
		// The serial ABI queue may delay this job. If cancellation or teardown
		// already won, do not repair transaction files or scan directories after
		// the operation has been cancelled.
		if (cancellationRequested()) {
			finishCancelledBeforeDiscovery();
			return;
		}
		// Repair interrupted file transactions before discovery. A restored destination must participate in this
		// same rescan instead of remaining absent until a second scan or restart.
		QStringList recoveryErrors;
		for (const QString &searchPath : searchPaths) {
			if (cancellationRequested()) {
				finishCancelledBeforeDiscovery();
				return;
			}
			if (!pluginUpdateDestinationMatchesInstalledPath(searchPath, installDirectory)) continue;
			const PluginTransactionRecoveryResult recovery =
				recoverPluginFileTransactions(searchPath, cancelToken.get());
			recoveryErrors.append(recovery.errors);
		}
		if (cancellationRequested()) {
			finishCancelledBeforeDiscovery();
			return;
		}
		const QStringList discovered = discoverPluginLibraryPaths(searchPaths, cancelToken.get());
		QStringList pluginItemIDs = discovered;
#ifdef USE_MANUAL_PLUGIN
		pluginItemIDs.push_back(QStringLiteral("builtin:manual-plugin"));
#endif
		const int pluginItems = pluginItemIDs.size();
		if (cancelToken->load() || m_shuttingDown.load()) {
			QMetaObject::invokeMethod(owner, [owner, operationID, pluginItemIDs]() {
				if (!owner || owner->m_asyncRescanOperationID != operationID) return;
				for (int index = 0; index < pluginItemIDs.size(); ++index) {
					emit owner->pluginOperationItemResult(operationID, pluginItemIDs.at(index), 0, false, true,
						QStringLiteral("cancelled"), PluginManager::tr("Plugin rescan cancelled"));
					emit owner->pluginOperationProgress(operationID, QStringLiteral("cancelled"), index + 1,
						pluginItemIDs.size(), 0, -1, -1);
				}
				owner->m_asyncRescanInProgress = false;
				owner->m_asyncRescanCancellation.reset();
				owner->m_asyncRescanOperationID.clear();
				emit owner->pluginOperationFinished(operationID, QStringLiteral("plugin-rescan"),
					QStringLiteral("cancelled"), 0, 0, pluginItemIDs.size());
				emit owner->pluginRescanFinished(false);
			}, Qt::QueuedConnection);
			return;
		}
		// Once old libraries start shutting down, cancellation is deferred until the consistent replacement set
		// has been installed. Stopping halfway would leave settings and runtime callbacks pointing at a partial set.
		QStringList allItemIDs;
		allItemIDs.reserve(pluginItems + recoveryErrors.size());
		for (int index = 0; index < recoveryErrors.size(); ++index)
			allItemIDs.push_back(QStringLiteral("transaction-recovery:%1").arg(index + 1));
		allItemIDs.append(pluginItemIDs);
		const int totalItems = allItemIDs.size();
		int succeeded = 0;
		int failed    = 0;
		int cancelled = 0;
		bool collectionCommitted = false;
		bool oldCollectionUnloaded = false;
		bool stagingRollbackCompleted = false;
		QVector< plugin_ptr_t > retiredPlugins;
			{
				QWriteLocker lock(&m_pluginCollectionLock);
				m_pluginStagingHashMap.clear();
			}
			{
				QWriteLocker lock(&m_pluginDescriptorLock);
				m_pluginStagingDescriptors.clear();
			}
			const auto rollbackStaging = [&]() {
				if (collectionCommitted || stagingRollbackCompleted) return;
				QVector< plugin_ptr_t > stagedPlugins;
				{
					QReadLocker lock(&m_pluginCollectionLock);
					stagedPlugins.reserve(m_pluginStagingHashMap.size());
					for (auto it = m_pluginStagingHashMap.cbegin(); it != m_pluginStagingHashMap.cend(); ++it)
						stagedPlugins.push_back(it.value());
				}
				for (const plugin_ptr_t &plugin : stagedPlugins) {
					try {
						if (plugin) unloadPlugin(*plugin);
					} catch (...) {
					}
				}
				{
					QWriteLocker lock(&m_pluginCollectionLock);
					m_pluginStagingHashMap.clear();
				}
				{
					QWriteLocker lock(&m_pluginDescriptorLock);
					m_pluginStagingDescriptors.clear();
				}
				if (oldCollectionUnloaded) {
					for (const plugin_ptr_t &plugin : retiredPlugins) {
						if (!plugin) continue;
						try {
							const PluginSetting setting =
								pluginSettingFromSnapshot(settingsSnapshot, plugin->getFilePath());
							applySavedPluginSettings(plugin, setting);
						} catch (...) {
							Log::logOrDefer(Log::Warning,
								PluginManager::tr("A previous plugin could not be restored after rescan rollback"));
						}
					}
				}
				stagingRollbackCompleted = true;
			};
			const auto stagingGuard = qScopeGuard([&]() { rollbackStaging(); });
			bool rescanLifecycleHeld = false;
			const auto rescanLifecycleGuard = qScopeGuard([&]() {
				if (rescanLifecycleHeld) endPluginLifecycleCall();
			});
			bool terminalQueued = false;
			const auto publishMeasurement = [owner, operationID](const QString &phase, const plugin_id_t pluginID,
																 const qint64 elapsed) {
				QMetaObject::invokeMethod(owner, [owner, operationID, phase, pluginID, elapsed]() {
					if (owner) emit owner->pluginAbiStepMeasured(operationID, phase,
						static_cast< qulonglong >(pluginID), elapsed, elapsed > 50);
				}, Qt::QueuedConnection);
			};
			QSet< QString > completedItemIDs;
			const auto publishResult = [owner, operationID, totalItems, &completedItemIDs, &succeeded, &failed,
				&cancelled](const QString &itemID, const plugin_id_t pluginID, const bool success,
				const bool wasCancelled, const QString &errorCode, const QString &message) {
				if (completedItemIDs.contains(itemID)) return false;
				completedItemIDs.insert(itemID);
				if (success) ++succeeded;
				else if (wasCancelled) ++cancelled;
				else ++failed;
				const int completed = completedItemIDs.size();
				QMetaObject::invokeMethod(owner,
					[owner, operationID, itemID, pluginID, success, wasCancelled, errorCode, message, completed,
						totalItems]() {
						if (!owner) return;
						emit owner->pluginOperationItemResult(operationID, itemID,
							static_cast< qulonglong >(pluginID), success, wasCancelled, errorCode, message);
						emit owner->pluginOperationProgress(operationID, QStringLiteral("apply-noncancellable"),
							completed, totalItems, static_cast< qulonglong >(pluginID), -1, -1);
				}, Qt::QueuedConnection);
				return true;
			};
			const auto terminalGuard = qScopeGuard([&]() {
				if (terminalQueued) return;
				rollbackStaging();
				for (const QString &itemID : allItemIDs) {
					publishResult(itemID, 0, false, false, QStringLiteral("plugin-exception"),
						PluginManager::tr("A plugin failed during rescan lifecycle processing"));
				}
				QMetaObject::invokeMethod(owner, [owner, operationID, succeeded, failed, cancelled]() {
					if (!owner || owner->m_asyncRescanOperationID != operationID) return;
					owner->m_asyncRescanInProgress = false;
					owner->m_asyncRescanCancellation.reset();
					owner->m_asyncRescanOperationID.clear();
					emit owner->pluginOperationFinished(operationID, QStringLiteral("plugin-rescan"),
						QStringLiteral("failed"), succeeded, failed, cancelled);
					emit owner->pluginRescanFinished(false);
				}, Qt::QueuedConnection);
			});
			const auto finishCancelled = [&]() {
				rollbackStaging();
				for (const QString &itemID : allItemIDs) {
					publishResult(itemID, 0, false, true, QStringLiteral("cancelled"),
						PluginManager::tr("Plugin rescan cancelled"));
				}
				terminalQueued = true;
				QMetaObject::invokeMethod(owner, [owner, operationID, succeeded, failed, cancelled]() {
					if (!owner || owner->m_asyncRescanOperationID != operationID) return;
					owner->m_asyncRescanInProgress = false;
					owner->m_asyncRescanCancellation.reset();
					owner->m_asyncRescanOperationID.clear();
					emit owner->pluginOperationFinished(operationID, QStringLiteral("plugin-rescan"),
						failed > 0 || succeeded > 0 ? QStringLiteral("partial") : QStringLiteral("cancelled"),
						succeeded, failed, cancelled);
					emit owner->pluginRescanFinished(false);
				}, Qt::QueuedConnection);
			};
			for (int index = 0; index < recoveryErrors.size(); ++index) {
				const QString error = recoveryErrors.at(index);
				Log::logOrDefer(Log::Warning,
					PluginManager::tr("Plugin transaction recovery failed: %1").arg(error));
				publishResult(QStringLiteral("transaction-recovery:%1").arg(index + 1), 0, false, false,
					QStringLiteral("transaction-recovery-failed"), error);
			}
			if (cancelToken->load() || m_shuttingDown.load()) {
				finishCancelled();
				return;
			}

			struct StagedPluginResult {
				QString itemID;
				plugin_ptr_t plugin;
				PluginSetting setting;
				plugin_id_t pluginID = 0;
				bool success = true;
				QString errorCode;
				QString message;
				QString loadPhase;
			};
			QVector< StagedPluginResult > stagedResults;
			const auto stagePlugin = [this, &settingsSnapshot](StagedPluginResult &result) {
				result.pluginID = result.plugin->getID();
				result.setting = pluginSettingFromSnapshot(settingsSnapshot, result.plugin->getFilePath());
				{
					QWriteLocker lock(&m_pluginCollectionLock);
					m_pluginStagingHashMap.insert(result.pluginID, result.plugin);
				}
				// Permission flags are manager-owned state and are safe to stage before the old instance is stopped. The
				// plugin init ABI is deliberately deferred until after every old instance has shut down.
				result.plugin->enablePositionalData(result.setting.positionalDataEnabled);
				result.plugin->allowKeyboardMonitoring(result.setting.allowKeyboardMonitoring);
				refreshPluginDescriptor(result.plugin, true);
			};
			const auto discardStagedPlugin = [this](StagedPluginResult &result) {
				if (!result.plugin) return;
				{
					QWriteLocker lock(&m_pluginCollectionLock);
					m_pluginStagingHashMap.remove(result.pluginID);
				}
				{
					QWriteLocker lock(&m_pluginDescriptorLock);
					m_pluginStagingDescriptors.remove(result.pluginID);
				}
				result.plugin.reset();
			};
			for (const QString &path : discovered) {
				if (cancelToken->load() || m_shuttingDown.load()) {
					finishCancelled();
					return;
				}
				StagedPluginResult result;
				result.itemID   = path;
				result.message  = PluginManager::tr("Plugin discovered");
				result.loadPhase = QStringLiteral("rescan-load");
				QElapsedTimer timer;
				timer.start();
				try {
					try {
						result.plugin = workerOwnedPlugin(Plugin::createNew< Plugin >(path));
					} catch (const PluginError &) {
						result.plugin = workerOwnedPlugin(Plugin::createNew< LegacyPlugin >(path));
					}
					stagePlugin(result);
				} catch (const std::exception &exception) {
					result.success   = false;
					result.errorCode = QStringLiteral("invalid-plugin");
					result.message   = PluginManager::tr("The plugin failed during validation: %1")
						.arg(QString::fromUtf8(exception.what()));
				} catch (...) {
					result.success   = false;
					result.errorCode = QStringLiteral("plugin-exception");
					result.message   = PluginManager::tr("The plugin failed during validation");
				}
				if (!result.success) discardStagedPlugin(result);
				publishMeasurement(QStringLiteral("rescan-validate"), result.pluginID, timer.elapsed());
				stagedResults.push_back(std::move(result));
			}
			if (cancelToken->load() || m_shuttingDown.load()) {
				finishCancelled();
				return;
			}
#ifdef USE_MANUAL_PLUGIN
			{
				StagedPluginResult result;
				result.itemID    = QStringLiteral("builtin:manual-plugin");
				result.message   = PluginManager::tr("Manual plugin discovered");
				result.loadPhase = QStringLiteral("rescan-manual-load");
				QElapsedTimer timer;
				timer.start();
				try {
					result.plugin = workerOwnedPlugin(Plugin::createNew< ManualPlugin >());
					stagePlugin(result);
#	if defined(MUMBLE_PLUGIN_DEBUG)
					LOG_FOUND_BUILTIN(result.plugin);
#	endif
				} catch (const std::exception &exception) {
					result.success   = false;
					result.errorCode = QStringLiteral("invalid-plugin");
					result.message   = PluginManager::tr("The manual plugin could not be initialized: %1")
						.arg(QString::fromUtf8(exception.what()));
				} catch (...) {
					result.success   = false;
					result.errorCode = QStringLiteral("plugin-exception");
					result.message   = PluginManager::tr("The manual plugin could not be initialized");
				}
				if (!result.success) discardStagedPlugin(result);
				publishMeasurement(QStringLiteral("rescan-manual-validate"), result.pluginID, timer.elapsed());
				stagedResults.push_back(std::move(result));
			}
#endif
			if (cancelToken->load() || m_shuttingDown.load()) {
				finishCancelled();
				return;
			}
			// Atomically cross the cancellation boundary before touching any loaded
			// library. Progress is queued rather than blocking the serial ABI worker
			// on the GUI thread, which also keeps shutdown deadlock-free.
			if (!cancelGate->seal() || m_shuttingDown.load()) {
				finishCancelled();
				return;
			}
			QMetaObject::invokeMethod(owner,
				[owner, operationID, completed = completedItemIDs.size(), totalItems]() {
					if (owner) emit owner->pluginOperationProgress(operationID,
						QStringLiteral("apply-noncancellable"), completed, totalItems, 0, -1, -1);
				}, Qt::QueuedConnection);
			// From the first old-plugin shutdown until either rollback has restored the old set or the staged set has
			// committed, runtime/audio callbacks must remain excluded. Per-plugin nested lifecycle guards are reentrant;
			// this outer guard prevents a half-shutdown plugin from becoming observable if shutdown throws.
			beginPluginLifecycleCall();
			rescanLifecycleHeld = true;
			if (cancelToken->load() || m_shuttingDown.load()) {
				finishCancelled();
				return;
			}
			{
				QReadLocker lock(&m_pluginCollectionLock);
				retiredPlugins.reserve(m_pluginHashMap.size());
				for (auto it = m_pluginHashMap.cbegin(); it != m_pluginHashMap.cend(); ++it)
					retiredPlugins.push_back(it.value());
			}
			oldCollectionUnloaded = true;
			bool oldUnloadFailed  = false;
			for (const plugin_ptr_t &retired : retiredPlugins) {
				if (!retired) continue;
				QElapsedTimer timer;
				timer.start();
				try {
					unloadPlugin(*retired);
				} catch (const std::exception &exception) {
					oldUnloadFailed = true;
					Log::logOrDefer(Log::Warning, PluginManager::tr("Plugin shutdown during rescan failed: %1")
						.arg(QString::fromUtf8(exception.what())));
				} catch (...) {
					oldUnloadFailed = true;
					Log::logOrDefer(Log::Warning,
						PluginManager::tr("Plugin shutdown during rescan failed with an unknown exception"));
				}
				if (retired->isLoaded()) oldUnloadFailed = true;
				publishMeasurement(QStringLiteral("rescan-unload"), retired->getID(), timer.elapsed());
			}
			if (oldUnloadFailed) return;
			for (StagedPluginResult &result : stagedResults) {
				if (result.success && result.plugin) {
					QElapsedTimer timer;
					timer.start();
					try {
						if (!applySavedPluginSettings(result.plugin, result.setting, false)) {
							result.success   = false;
							result.errorCode = QStringLiteral("load-error");
							result.message   = PluginManager::tr(
								"The plugin was discovered but failed to load");
						}
						refreshPluginDescriptor(result.plugin, true);
					} catch (const std::exception &exception) {
						result.success   = false;
						result.errorCode = QStringLiteral("plugin-exception");
						result.message   = PluginManager::tr("The plugin failed during initialization: %1")
							.arg(QString::fromUtf8(exception.what()));
					} catch (...) {
						result.success   = false;
						result.errorCode = QStringLiteral("plugin-exception");
						result.message   = PluginManager::tr("The plugin failed during initialization");
					}
					publishMeasurement(result.loadPhase, result.pluginID, timer.elapsed());
					if (!result.success && result.errorCode != QLatin1String("load-error")) {
						try {
							unloadPlugin(*result.plugin);
						} catch (...) {
						}
						{
							QWriteLocker lock(&m_pluginCollectionLock);
							m_pluginStagingHashMap.remove(result.pluginID);
						}
						{
							QWriteLocker lock(&m_pluginDescriptorLock);
							m_pluginStagingDescriptors.remove(result.pluginID);
						}
						result.plugin.reset();
					}
				}
				publishResult(result.itemID, result.pluginID, result.success, false, result.errorCode, result.message);
			}
			QHash< plugin_id_t, PluginDescriptor > retiredDescriptors;
			{
				QWriteLocker collectionLock(&m_pluginCollectionLock);
				QWriteLocker descriptorLock(&m_pluginDescriptorLock);
				QHash< plugin_id_t, plugin_ptr_t > committedPlugins;
				committedPlugins.swap(m_pluginStagingHashMap);
				m_pluginHashMap.swap(committedPlugins);
				retiredDescriptors.swap(m_pluginDescriptors);
				m_pluginDescriptors.swap(m_pluginStagingDescriptors);
				++m_pluginCollectionGeneration;
			}
			collectionCommitted = true;
			retiredPlugins.clear();
			const QString status = failed == 0 ? QStringLiteral("succeeded")
				: (succeeded > 0 ? QStringLiteral("partial") : QStringLiteral("failed"));
			terminalQueued = true;
			QMetaObject::invokeMethod(owner, [owner, operationID, status, succeeded, failed]() {
				if (!owner || owner->m_asyncRescanOperationID != operationID) return;
				owner->m_asyncRescanInProgress = false;
				owner->m_asyncRescanCancellation.reset();
				owner->m_asyncRescanOperationID.clear();
				emit owner->pluginOperationFinished(operationID, QStringLiteral("plugin-rescan"), status,
					succeeded, failed, 0);
				emit owner->pluginRescanFinished(true);
			}, Qt::QueuedConnection);
	});
	if (!queued) {
		m_asyncRescanInProgress = false;
		m_asyncRescanCancellation.reset();
		m_asyncRescanOperationID.clear();
		emit pluginOperationFinished(operationID, QStringLiteral("plugin-rescan"), QStringLiteral("failed"),
			0, 0, 0);
		emit pluginRescanFinished(false);
	}
	return operationID;
}

void PluginManager::cancelPluginOperation(const QString &operationID) {
	if (m_asyncRescanInProgress && m_asyncRescanCancellation && operationID == m_asyncRescanOperationID) {
		m_asyncRescanCancellation->requestCancellation();
		return;
	}
	m_updater.interrupt(operationID);
}

bool PluginManager::reloadPluginPath(const QString &path, const PluginSetting &setting) {
	Q_ASSERT(sharedPluginAbiWorker().isCurrentThread());
	beginPluginLifecycleCall();
	const auto lifecycleGuard = qScopeGuard([this]() { endPluginLifecycleCall(); });
	const QString absolutePath = QFileInfo(path).absoluteFilePath();
	for (const PluginDescriptor &existing : pluginDescriptors()) {
		if (pluginPermissionKeyForPath(existing.path) == pluginPermissionKeyForPath(absolutePath)) {
			clearPlugin(existing.id);
			break;
		}
	}
	try {
		plugin_ptr_t plugin;
		try {
			plugin = workerOwnedPlugin(Plugin::createNew< Plugin >(absolutePath));
		} catch (const PluginError &) {
			plugin = workerOwnedPlugin(Plugin::createNew< LegacyPlugin >(absolutePath));
		}
		const plugin_id_t pluginID = plugin->getID();
		bool keepPlugin            = false;
		const auto failedReloadGuard = qScopeGuard([this, &plugin, pluginID, &keepPlugin]() {
			if (keepPlugin || !plugin) return;
			try {
				unloadPlugin(*plugin);
			} catch (...) {
			}
			{
				QWriteLocker lock(&m_pluginCollectionLock);
				const auto current = m_pluginHashMap.constFind(pluginID);
				if (current != m_pluginHashMap.cend() && current.value() == plugin) {
					m_pluginHashMap.remove(pluginID);
					++m_pluginCollectionGeneration;
				}
			}
			{
				QWriteLocker lock(&m_pluginDescriptorLock);
				m_pluginDescriptors.remove(pluginID);
			}
			plugin.reset();
		});
		bool idCollision = false;
		{
			QWriteLocker lock(&m_pluginCollectionLock);
			idCollision = m_pluginHashMap.contains(pluginID);
			if (!idCollision) {
				m_pluginHashMap.insert(pluginID, plugin);
				++m_pluginCollectionGeneration;
			}
		}
		if (idCollision) {
			Log::logOrDefer(Log::Warning,
				tr("Unable to reload plugin %1 because its ID is already active").arg(absolutePath));
			return false;
		}
		// Publish a safe cached identity before init: plugins are allowed to use the API (including logging) from
		// their init callback, and API validation must not require reading metadata through the third-party ABI.
		refreshPluginDescriptor(plugin);
		keepPlugin = plugin->isValid() && applySavedPluginSettings(plugin, setting);
		return keepPlugin;
	} catch (const PluginError &error) {
		Log::logOrDefer(Log::Warning, tr("Unable to reload plugin %1 (%2)").arg(absolutePath, QString::fromUtf8(error.what())));
		return false;
	} catch (const std::exception &error) {
		Log::logOrDefer(Log::Warning,
			tr("Unable to reload plugin %1 (%2)").arg(absolutePath, QString::fromUtf8(error.what())));
		return false;
	} catch (...) {
		Log::logOrDefer(Log::Warning, tr("Unable to reload plugin %1").arg(absolutePath));
		return false;
	}
}

const_plugin_ptr_t PluginManager::getPlugin(plugin_id_t pluginID) const {
	QReadLocker lock(&m_pluginCollectionLock);
	const plugin_ptr_t active = m_pluginHashMap.value(pluginID);
	return active ? active : m_pluginStagingHashMap.value(pluginID);
}

QString PluginManager::checkForPluginUpdates() {
	return m_updater.checkForUpdates();
}

QVariantList PluginManager::availablePluginUpdates() {
	QVariantList result;
	for (const UpdateEntry &entry : m_updater.availableUpdates()) {
		QVariantMap item;
		item.insert(QStringLiteral("id"), static_cast< qulonglong >(entry.pluginID));
		item.insert(QStringLiteral("url"), entry.updateURL.toString());
		item.insert(QStringLiteral("fileName"), entry.fileName);
		item.insert(QStringLiteral("name"), entry.displayName.isEmpty() ? entry.fileName : entry.displayName);
		result.push_back(item);
	}
	return result;
}

QString PluginManager::updatePlugins(const QSet< plugin_id_t > &pluginIDs) {
	return m_updater.updateSelected(pluginIDs);
}

void PluginManager::interruptPluginUpdates(const QString &operationID) {
	m_updater.interrupt(operationID);
}

bool PluginManager::fetchPositionalData() {
	if (m_shuttingDown.load()) return false;
	if (Global::get().bPosTest) {
		// This is for testing purposes only and does not execute plugin code.
		QWriteLocker posDataLock(&m_positionalData.m_lock);
		m_positionalData.reset();
		m_positionalData.m_playerDir.z  = 1.0f;
		m_positionalData.m_playerAxis.y = 1.0f;
		m_positionalData.m_cameraDir.z  = 1.0f;
		m_positionalData.m_cameraAxis.y = 1.0f;
		m_positionalSampleValid.store(true);
		return true;
	}
	if (!m_activePositionalPermissionAllowed.load()) {
		m_positionalSampleValid.store(false);
		QWriteLocker posDataLock(&m_positionalData.m_lock);
		m_positionalData.reset();
		return false;
	}
	const PluginRuntimeCallAdmission admission = beginPluginRuntimeCall();
	if (admission == PluginRuntimeCallAdmission::Rejected) return false;
	const auto runtimeGuard = qScopeGuard([this, admission]() {
		endPluginRuntimeCall(admission);
	});
	if (!m_positionalAbiMutex.tryLock()) {
		// Audio input/output and the sync timer may sample concurrently. Reuse the last complete sample instead of
		// blocking a real-time caller behind arbitrary third-party code.
		return !m_shuttingDown.load() && m_activePositionalPermissionAllowed.load()
			&& m_positionalSampleValid.load();
	}
	const auto positionalAbiGuard = qScopeGuard([this]() { m_positionalAbiMutex.unlock(); });
	plugin_ptr_t activePlugin;
	{
		QReadLocker activePluginLock(&m_activePosDataPluginLock);
		activePlugin = m_activePositionalDataPlugin;
	}

	if (!activePlugin) {
		m_activePositionalPermissionAllowed.store(false);
		m_positionalSampleValid.store(false);
		// It appears as if there is currently no plugin capable of delivering positional audio
		// Set positional data to zero-values
		QWriteLocker posDataLock(&m_positionalData.m_lock);
		m_positionalData.reset();

		return false;
	}
	const QString pluginPath = activePlugin->getFilePath();
	if (!isPluginPositionalPermissionGranted(pluginPath)) {
		m_activePositionalPermissionAllowed.store(false);
		m_positionalSampleValid.store(false);
		QWriteLocker posDataLock(&m_positionalData.m_lock);
		m_positionalData.reset();
		return false;
	}

	Position3D playerPos;
	Vector3D playerDir;
	Vector3D playerAxis;
	Position3D cameraPos;
	Vector3D cameraDir;
	Vector3D cameraAxis;
	QString context;
	QString identity;
	bool retStatus = false;
	try {
		retStatus = activePlugin->fetchPositionalData(
			playerPos, playerDir, playerAxis, cameraPos, cameraDir, cameraAxis, context, identity);
		if (retStatus && !context.isEmpty()) {
			context = activePlugin->getPositionalDataContextPrefix() + QChar::Null + context;
		}
	} catch (const std::exception &exception) {
		qWarning("Positional plugin callback failed: %s", exception.what());
		retStatus = false;
	} catch (...) {
		qWarning("Positional plugin callback failed with an unknown exception");
		retStatus = false;
	}
	if (!m_activePositionalPermissionAllowed.load()
		|| !isPluginPositionalPermissionGranted(pluginPath)) {
		// A revoke can arrive while untrusted in-process fetch code is still executing. Discard that final result
		// so neither the audio packet nor the timer path can publish it afterwards.
		m_positionalSampleValid.store(false);
		QWriteLocker posDataLock(&m_positionalData.m_lock);
		m_positionalData.reset();
		return false;
	}

	if (!retStatus) {
		m_activePositionalPermissionAllowed.store(false);
		m_positionalSampleValid.store(false);
		{
			QWriteLocker lock(&m_activePosDataPluginLock);
			if (m_activePositionalDataPlugin == activePlugin) m_activePositionalDataPlugin.reset();
		}
		{
			QWriteLocker posDataLock(&m_positionalData.m_lock);
			m_positionalData.reset();
		}
		// Never hold the active-plugin lock while executing third-party code: plugin callbacks may query manager state.
		try {
			activePlugin->shutdownPositionalData();
		} catch (...) {
			if (!m_shuttingDown.load()) Log::logOrDefer(Log::Warning,
				PluginManager::tr("The positional plugin failed while releasing a lost link"));
		}
		if (!m_shuttingDown.load()) emit pluginLostLink(activePlugin->getID());

		selectActivePositionalDataPlugin();
	} else {
		// If the return-status doesn't indicate an error, we can assume that positional data is available
		// The remaining problematic case is, if the player is exactly at position (0,0,0) as this is used as an
		// indicator for the absence of positional data in the mix() function in AudioOutput.cpp Thus we have to make
		// sure that this position is never set if positional data is actually available. We solve this problem by
		// shifting the player a minimal amount on the z-axis
		if (playerPos == Position3D(0.0f, 0.0f, 0.0f)) {
			playerPos = { 0.0f, 0.0f, std::numeric_limits< float >::min() };
		}
		if (cameraPos == Position3D(0.0f, 0.0f, 0.0f)) {
			cameraPos = { 0.0f, 0.0f, std::numeric_limits< float >::min() };
		}
		QWriteLocker posDataLock(&m_positionalData.m_lock);
		if (!m_activePositionalPermissionAllowed.load()
			|| !isPluginPositionalPermissionGranted(pluginPath)) {
			m_positionalSampleValid.store(false);
			m_positionalData.reset();
			return false;
		}
		m_positionalData.m_playerPos   = playerPos;
		m_positionalData.m_playerDir   = playerDir;
		m_positionalData.m_playerAxis  = playerAxis;
		m_positionalData.m_cameraPos   = cameraPos;
		m_positionalData.m_cameraDir   = cameraDir;
		m_positionalData.m_cameraAxis  = cameraAxis;
		m_positionalData.m_context     = context;
		m_positionalData.m_identity    = identity;
		m_positionalSampleValid.store(true);
	}

	return retStatus;
}

void PluginManager::unlinkPositionalData() {
	// Privacy revocation must take effect before a potentially long plugin task ahead of the queued ABI shutdown.
	m_activePositionalPermissionAllowed.store(false);
	m_positionalSampleValid.store(false);
	{
		QWriteLocker posDataLock(&m_positionalData.m_lock);
		m_positionalSampleValid.store(false);
		m_positionalData.reset();
	}
	if (!sharedPluginAbiWorker().isCurrentThread()) {
		std::ignore = sharedPluginAbiWorker().enqueue([this]() { unlinkPositionalData(); });
		return;
	}
	beginPluginLifecycleCall();
	const auto lifecycleGuard = qScopeGuard([this]() { endPluginLifecycleCall(); });
	plugin_ptr_t activePlugin;
	{
		QWriteLocker lock(&m_activePosDataPluginLock);
		activePlugin.swap(m_activePositionalDataPlugin);
	}
	if (activePlugin) {
		const plugin_id_t pluginID = activePlugin->getID();
		const QPointer< PluginManager > owner(this);
		const auto lostGuard = qScopeGuard([owner, pluginID]() {
			QMetaObject::invokeMethod(owner, [owner, pluginID]() {
				if (owner) emit owner->pluginLostLink(pluginID);
			}, Qt::QueuedConnection);
		});
		activePlugin->shutdownPositionalData();
	}
}

bool PluginManager::isPositionalDataAvailable() const {
	QReadLocker lock(&m_activePosDataPluginLock);

	return m_activePositionalDataPlugin != nullptr;
}

const PositionalData &PluginManager::getPositionalData() const {
	return m_positionalData;
}

void PluginManager::enablePositionalDataFor(plugin_id_t pluginID, bool enable) const {
	QReadLocker lock(&m_pluginCollectionLock);

	plugin_ptr_t plugin = m_pluginHashMap.value(pluginID);

	if (plugin) {
		plugin->enablePositionalData(enable);
		setPluginDescriptorPositionalEnabled(pluginID, enable);
	}
}

const QVector< const_plugin_ptr_t > PluginManager::getPlugins(bool sorted) const {
	QVector< const_plugin_ptr_t > pluginList;
	{
		QReadLocker lock(&m_pluginCollectionLock);
		pluginList.reserve(m_pluginHashMap.size());
		for (auto it = m_pluginHashMap.constBegin(); it != m_pluginHashMap.constEnd(); ++it) {
			pluginList.append(it.value());
		}
	}
	if (sorted) {
		QHash< plugin_id_t, QString > names;
		{
			QReadLocker lock(&m_pluginDescriptorLock);
			for (auto it = m_pluginDescriptors.constBegin(); it != m_pluginDescriptors.constEnd(); ++it) {
				names.insert(it.key(), it->name);
			}
		}
		std::sort(pluginList.begin(), pluginList.end(), [&names](const const_plugin_ptr_t &first,
																		 const const_plugin_ptr_t &second) {
			const plugin_id_t firstID  = first ? first->getID() : 0;
			const plugin_id_t secondID = second ? second->getID() : 0;
			const int nameOrder = QString::compare(names.value(firstID), names.value(secondID), Qt::CaseInsensitive);
			return nameOrder == 0 ? firstID < secondID : nameOrder < 0;
		});
	}

	return pluginList;
}

QVector< PluginDescriptor > PluginManager::pluginDescriptors(const bool sorted) const {
	QVector< PluginDescriptor > descriptors;
	{
		QReadLocker lock(&m_pluginDescriptorLock);
		descriptors.reserve(m_pluginDescriptors.size());
		for (auto it = m_pluginDescriptors.constBegin(); it != m_pluginDescriptors.constEnd(); ++it) {
			descriptors.push_back(it.value());
		}
	}
	if (sorted) {
		std::sort(descriptors.begin(), descriptors.end(), [](const PluginDescriptor &first,
																		 const PluginDescriptor &second) {
			const int nameOrder = QString::compare(first.name, second.name, Qt::CaseInsensitive);
			return nameOrder == 0 ? first.id < second.id : nameOrder < 0;
		});
	}
	return descriptors;
}

std::optional< PluginDescriptor > PluginManager::pluginDescriptor(const plugin_id_t pluginID,
																	 const bool includeStaging) const {
	QReadLocker lock(&m_pluginDescriptorLock);
	const auto it = m_pluginDescriptors.constFind(pluginID);
	if (it != m_pluginDescriptors.constEnd()) return it.value();
	if (includeStaging) {
		const auto staged = m_pluginStagingDescriptors.constFind(pluginID);
		if (staged != m_pluginStagingDescriptors.constEnd()) return staged.value();
	}
	return std::nullopt;
}

bool PluginManager::loadPlugin(plugin_id_t pluginID) const {
	if (!sharedPluginAbiWorker().isCurrentThread()) {
		Q_ASSERT_X(false, "PluginManager::loadPlugin", "plugin lifecycle calls require the serial ABI worker");
		return false;
	}
	beginPluginLifecycleCall();
	const auto lifecycleGuard = qScopeGuard([this]() { endPluginLifecycleCall(); });
	plugin_ptr_t plugin;
	{
		QReadLocker lock(&m_pluginCollectionLock);
		plugin = m_pluginHashMap.value(pluginID);
	}

	if (plugin) {
		const auto descriptorGuard = qScopeGuard([this, plugin]() { refreshPluginDescriptor(plugin); });
		if (plugin->isLoaded()) {
			// Don't attempt to load a plugin if it already is loaded.
			// This can happen if the user clicks the apply button in the settings
			// before hitting ok.
			return true;
		}

		return plugin->init() == MUMBLE_STATUS_OK;
	}

	return false;
}

QString PluginManager::setPluginLoadedAsync(const plugin_id_t pluginID, const bool loaded) {
	if (m_shuttingDown.load()) return {};
	const std::optional< PluginDescriptor > requestedDescriptor = pluginDescriptor(pluginID);
	if (!loaded && requestedDescriptor) {
		PluginSetting revoke;
		revoke.path                    = requestedDescriptor->path;
		revoke.enabled                 = false;
		revoke.positionalDataEnabled   = requestedDescriptor->positionalDataEnabled;
		revoke.allowKeyboardMonitoring = requestedDescriptor->keyboardMonitoringAllowed;
		QHash< QString, PluginSetting > revocations;
		revocations.insert(pluginSettingsKeyForPath(requestedDescriptor->path), revoke);
		applyImmediatePluginPermissionRevocations(revocations);
	}
	const QString stablePath = requestedDescriptor ? requestedDescriptor->path : QString();
	const QString operationID = QStringLiteral("plugin-%1:%2:%3")
		.arg(loaded ? QStringLiteral("load") : QStringLiteral("unload"))
		.arg(static_cast< qulonglong >(pluginID))
		.arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	const QString kind = loaded ? QStringLiteral("plugin-load") : QStringLiteral("plugin-unload");
	emit pluginOperationStarted(operationID, kind, 1, false);
	const QPointer< PluginManager > owner(this);
	const bool queued = sharedPluginAbiWorker().enqueue(
		[this, owner, operationID, kind, requestedPluginID = pluginID, stablePath, loaded]() {
		QElapsedTimer timer;
		timer.start();
		plugin_id_t pluginID = requestedPluginID;
		if (!pluginExists(pluginID) && !stablePath.isEmpty()) {
			for (const PluginDescriptor &descriptor : pluginDescriptors()) {
				if (pluginPermissionKeyForPath(descriptor.path) == pluginPermissionKeyForPath(stablePath)) {
					pluginID = descriptor.id;
					break;
				}
			}
		}
		bool success = pluginExists(pluginID);
		QString errorCode;
		QString message;
		try {
			if (!success) {
				errorCode = QStringLiteral("plugin-missing");
				message   = PluginManager::tr("The plugin is no longer installed");
			} else if (loaded) {
				success = loadPlugin(pluginID);
				if (!success) {
					errorCode = QStringLiteral("load-error");
					message   = PluginManager::tr("The plugin failed to load");
				} else {
					message = PluginManager::tr("Plugin loaded");
				}
			} else {
				unloadPlugin(pluginID);
				message = PluginManager::tr("Plugin unloaded");
			}
		} catch (const std::exception &exception) {
			success   = false;
			errorCode = QStringLiteral("plugin-exception");
			message   = QString::fromUtf8(exception.what());
		} catch (...) {
			success   = false;
			errorCode = QStringLiteral("plugin-exception");
			message   = PluginManager::tr("The plugin failed during the lifecycle operation");
		}
		const qint64 elapsed = timer.elapsed();
		QMetaObject::invokeMethod(owner, [owner, operationID, kind, pluginID, loaded, success, errorCode,
											 message, elapsed]() {
			if (!owner) return;
			emit owner->pluginAbiStepMeasured(operationID,
				loaded ? QStringLiteral("load") : QStringLiteral("unload"),
				static_cast< qulonglong >(pluginID), elapsed, elapsed > 50);
			emit owner->pluginOperationItemResult(operationID,
				QString::number(static_cast< qulonglong >(pluginID)), static_cast< qulonglong >(pluginID),
				success, false, errorCode, message);
			emit owner->pluginOperationProgress(operationID,
				loaded ? QStringLiteral("load") : QStringLiteral("unload"), 1, 1,
				static_cast< qulonglong >(pluginID), -1, -1);
			emit owner->pluginOperationFinished(operationID, kind,
				success ? QStringLiteral("succeeded") : QStringLiteral("failed"),
				success ? 1 : 0, success ? 0 : 1, 0);
		}, Qt::QueuedConnection);
	});
	if (!queued) {
		const QString error = tr("The plugin worker is unavailable");
		emit pluginOperationItemResult(operationID, QString::number(static_cast< qulonglong >(pluginID)),
			static_cast< qulonglong >(pluginID), false, false, QStringLiteral("worker-unavailable"), error);
		emit pluginOperationProgress(operationID, loaded ? QStringLiteral("load") : QStringLiteral("unload"),
			1, 1, static_cast< qulonglong >(pluginID), -1, -1);
		emit pluginOperationFinished(operationID, kind, QStringLiteral("failed"), 0, 1, 0);
	}
	return operationID;
}

QString PluginManager::applyPluginSettingsAsync(const QHash< QString, PluginSetting > &settings) {
	if (m_shuttingDown.load()) return {};
	// Permission revocation is fail-closed and immediate. Enabling remains worker-serialized, but a plugin whose
	// revoke is queued behind slow third-party ABI work stops receiving keys/producing positional data right now.
	applyImmediatePluginPermissionRevocations(settings);
	const QString operationID = QStringLiteral("plugin-settings:%1")
		.arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	const QPointer< PluginManager > owner(this);
	const bool queued = sharedPluginAbiWorker().enqueue([this, owner, operationID, settings]() {
		const QVector< const_plugin_ptr_t > constPlugins = getPlugins();
		QVector< plugin_ptr_t > plugins;
		plugins.reserve(constPlugins.size());
		for (const const_plugin_ptr_t &plugin : constPlugins)
			plugins.push_back(std::const_pointer_cast< Plugin >(plugin));
		QMetaObject::invokeMethod(owner, [owner, operationID, count = plugins.size()]() {
			if (owner) emit owner->pluginOperationStarted(operationID, QStringLiteral("plugin-settings-apply"),
				count, false);
		}, Qt::QueuedConnection);

		int succeeded = 0;
		int failed    = 0;
		int completed = 0;
		for (const plugin_ptr_t &plugin : plugins) {
			if (!plugin) continue;
			const plugin_id_t pluginID = plugin->getID();
			const QString path          = plugin->getFilePath();
			const QString settingsKey   = pluginSettingsKeyForPath(path);
			PluginSetting setting;
			if (settings.contains(settingsKey)) {
				setting = settings.value(settingsKey);
			} else {
				setting.path                    = path;
				setting.enabled                 = plugin->isLoaded();
				setting.positionalDataEnabled   = plugin->isPositionalDataEnabled();
				setting.allowKeyboardMonitoring = plugin->isKeyboardMonitoringAllowed();
			}
			setting.path = path;
			setting      = desiredPluginSettingForPath(path, setting);
			bool success = true;
			QString errorCode;
			QString message = PluginManager::tr("Plugin settings applied");
			QElapsedTimer timer;
			timer.start();
			try {
				beginPluginLifecycleCall();
				const auto lifecycleGuard = qScopeGuard([this]() { endPluginLifecycleCall(); });
				if (setting.enabled) {
					success = applySavedPluginSettings(plugin, setting);
					if (!success) {
						errorCode = QStringLiteral("load-error");
						message   = PluginManager::tr("The plugin failed to load with the new settings");
					}
				} else {
					unloadPlugin(*plugin);
					plugin->enablePositionalData(setting.positionalDataEnabled);
					plugin->allowKeyboardMonitoring(setting.allowKeyboardMonitoring);
					refreshPluginDescriptor(plugin);
					recordAppliedPluginPermissions(path, setting);
				}
			} catch (const std::exception &exception) {
				success   = false;
				errorCode = QStringLiteral("plugin-exception");
				message   = QString::fromUtf8(exception.what());
			} catch (...) {
				success   = false;
				errorCode = QStringLiteral("plugin-exception");
				message   = PluginManager::tr("The plugin failed while applying settings");
			}
			if (!success) refreshPluginDescriptor(plugin);
			const qint64 elapsed = timer.elapsed();
			++completed;
			if (success) ++succeeded; else ++failed;
			QMetaObject::invokeMethod(owner,
				[owner, operationID, pluginID, path, success, errorCode, message, elapsed, completed,
				 total = plugins.size()]() {
					if (!owner) return;
					emit owner->pluginAbiStepMeasured(operationID, QStringLiteral("apply-settings"),
						static_cast< qulonglong >(pluginID), elapsed, elapsed > 50);
					emit owner->pluginOperationItemResult(operationID, path,
						static_cast< qulonglong >(pluginID), success, false, errorCode, message);
					emit owner->pluginOperationProgress(operationID, QStringLiteral("apply-settings"), completed,
						total, static_cast< qulonglong >(pluginID), -1, -1);
				}, Qt::QueuedConnection);
		}
		const QString status = failed == 0 ? QStringLiteral("succeeded")
			: (succeeded > 0 ? QStringLiteral("partial") : QStringLiteral("failed"));
		QMetaObject::invokeMethod(owner, [owner, operationID, status, succeeded, failed]() {
			if (owner) emit owner->pluginOperationFinished(operationID, QStringLiteral("plugin-settings-apply"),
				status, succeeded, failed, 0);
		}, Qt::QueuedConnection);
	});
	if (!queued) {
		emit pluginOperationStarted(operationID, QStringLiteral("plugin-settings-apply"), 0, false);
		emit pluginOperationFinished(operationID, QStringLiteral("plugin-settings-apply"),
			QStringLiteral("failed"), 0, 1, 0);
		return {};
	}
	return operationID;
}

void PluginManager::unloadPlugin(plugin_id_t pluginID) {
	if (!sharedPluginAbiWorker().isCurrentThread()) {
		Q_ASSERT_X(false, "PluginManager::unloadPlugin", "plugin lifecycle calls require the serial ABI worker");
		return;
	}
	plugin_ptr_t plugin;
	{
		QReadLocker lock(&m_pluginCollectionLock);

		plugin = m_pluginHashMap.value(pluginID);
	}

	if (plugin) {
		const auto descriptorGuard = qScopeGuard([this, plugin]() { refreshPluginDescriptor(plugin); });
		unloadPlugin(*plugin);
	}
}

void PluginManager::unloadPlugin(Plugin &plugin) {
	if (!sharedPluginAbiWorker().isCurrentThread()) {
		Q_ASSERT_X(false, "PluginManager::unloadPlugin", "plugin lifecycle calls require the serial ABI worker");
		return;
	}
	beginPluginLifecycleCall();
	const auto lifecycleGuard = qScopeGuard([this]() { endPluginLifecycleCall(); });
	if (plugin.isLoaded()) {
		// Only shut down loaded plugins

		bool isActivePosDataPlugin = false;
		{
			QReadLocker lock(&m_activePosDataPluginLock);
			isActivePosDataPlugin = &plugin == m_activePositionalDataPlugin.get();
		}

		if (isActivePosDataPlugin) {
			m_activePositionalPermissionAllowed.store(false);
			m_positionalSampleValid.store(false);
			{
				QWriteLocker lock(&m_activePosDataPluginLock);
				if (&plugin == m_activePositionalDataPlugin.get()) m_activePositionalDataPlugin.reset();
			}
			{
				QWriteLocker posDataLock(&m_positionalData.m_lock);
				m_positionalData.reset();
			}
			const plugin_id_t pluginID = plugin.getID();
			const QPointer< PluginManager > owner(this);
			QMetaObject::invokeMethod(owner, [owner, pluginID]() {
				if (owner) emit owner->pluginLostLink(pluginID);
			}, Qt::QueuedConnection);
			plugin.shutdownPositionalData();
		}

		plugin.shutdown();
	}
}

bool PluginManager::clearPlugin(plugin_id_t pluginID) {
	if (!sharedPluginAbiWorker().isCurrentThread()) {
		Q_ASSERT_X(false, "PluginManager::clearPlugin", "plugin lifecycle calls require the serial ABI worker");
		return false;
	}
	plugin_ptr_t plugin;
	{
		QReadLocker lock(&m_pluginCollectionLock);
		plugin = m_pluginHashMap.value(pluginID);
	}
	if (!plugin) return false;

	// Keep the plugin discoverable while shutdown callbacks use the API, but make collection removal unconditional.
	// A broken third-party shutdown must not strand a loaded wrapper/QLibrary and thereby block file rollback.
	QString shutdownError;
	try {
		unloadPlugin(*plugin);
	} catch (const std::exception &exception) {
		shutdownError = QString::fromUtf8(exception.what());
	} catch (...) {
		shutdownError = tr("unknown exception");
	}
	plugin_ptr_t removedPlugin;
	{
		QWriteLocker lock(&m_pluginCollectionLock);
		if (m_pluginHashMap.value(pluginID) == plugin) {
			removedPlugin = m_pluginHashMap.take(pluginID);
			++m_pluginCollectionGeneration;
		}
	}
	{
		QWriteLocker lock(&m_pluginDescriptorLock);
		m_pluginDescriptors.remove(pluginID);
	}

	const bool removed = removedPlugin != nullptr;
	removedPlugin.reset();
	if (!shutdownError.isEmpty()) {
		qWarning().noquote() << tr("Plugin shutdown failed while removing %1: %2")
			.arg(plugin->getFilePath(), shutdownError);
	}
	return removed;
}

uint32_t PluginManager::deactivateFeaturesFor(plugin_id_t pluginID, uint32_t features) {
	if (!sharedPluginAbiWorker().isCurrentThread()) {
		const QString operationID = QStringLiteral("plugin-deactivate:%1:%2")
			.arg(static_cast< qulonglong >(pluginID))
			.arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
		emit pluginOperationStarted(operationID, QStringLiteral("plugin-capability"), 1, false);
		const QPointer< PluginManager > owner(this);
		const bool queued = sharedPluginAbiWorker().enqueue([this, owner, operationID, pluginID, features]() {
			QElapsedTimer timer;
			timer.start();
			uint32_t remaining = features;
			QString exceptionMessage;
			try {
				remaining = deactivateFeaturesFor(pluginID, features);
			} catch (const std::exception &exception) {
				exceptionMessage = QString::fromUtf8(exception.what());
			} catch (...) {
				exceptionMessage = PluginManager::tr("The plugin failed while disabling a capability");
			}
			const qint64 elapsed = timer.elapsed();
			QMetaObject::invokeMethod(owner,
				[owner, operationID, pluginID, remaining, elapsed, exceptionMessage]() {
				if (!owner) return;
				const bool success = exceptionMessage.isEmpty() && remaining == MUMBLE_FEATURE_NONE;
				const QString errorCode = success ? QString()
					: (exceptionMessage.isEmpty() ? QStringLiteral("capability-deactivate-error")
						: QStringLiteral("plugin-exception"));
				const QString message = !exceptionMessage.isEmpty() ? exceptionMessage
					: (success ? PluginManager::tr("Plugin capability disabled")
						: PluginManager::tr("The plugin did not disable every requested capability"));
				emit owner->pluginAbiStepMeasured(operationID, QStringLiteral("deactivate-features"),
					static_cast< qulonglong >(pluginID), elapsed, elapsed > 50);
				emit owner->pluginOperationItemResult(operationID,
					QString::number(static_cast< qulonglong >(pluginID)), static_cast< qulonglong >(pluginID),
					success, false, errorCode, message);
				emit owner->pluginOperationProgress(operationID, QStringLiteral("deactivate-features"), 1, 1,
					static_cast< qulonglong >(pluginID), -1, -1);
				emit owner->pluginOperationFinished(operationID, QStringLiteral("plugin-capability"),
					success ? QStringLiteral("succeeded") : QStringLiteral("failed"), success ? 1 : 0,
					success ? 0 : 1, 0);
				}, Qt::QueuedConnection);
		});
		if (!queued) {
			const QString message = tr("The plugin worker is unavailable");
			emit pluginOperationItemResult(operationID, QString::number(static_cast< qulonglong >(pluginID)),
				static_cast< qulonglong >(pluginID), false, false, QStringLiteral("worker-unavailable"), message);
			emit pluginOperationProgress(operationID, QStringLiteral("deactivate-features"), 1, 1,
				static_cast< qulonglong >(pluginID), -1, -1);
			emit pluginOperationFinished(operationID, QStringLiteral("plugin-capability"), QStringLiteral("failed"),
				0, 1, 0);
		}
		return queued ? MUMBLE_FEATURE_NONE : features;
	}
	beginPluginLifecycleCall();
	const auto lifecycleGuard = qScopeGuard([this]() { endPluginLifecycleCall(); });
	plugin_ptr_t plugin;
	{
		QReadLocker lock(&m_pluginCollectionLock);
		plugin = m_pluginHashMap.value(pluginID);
	}

	if (plugin) {
		return plugin->deactivateFeatures(features);
	}

	return MUMBLE_FEATURE_NONE;
}

void PluginManager::allowKeyboardMonitoringFor(plugin_id_t pluginID, bool allow) const {
	QReadLocker lock(&m_pluginCollectionLock);

	plugin_ptr_t plugin = m_pluginHashMap.value(pluginID);

	if (plugin) {
		plugin->allowKeyboardMonitoring(allow);
		setPluginDescriptorKeyboardMonitoringAllowed(pluginID, allow);
	}
}

PluginDialogOpenResult PluginManager::showConfigDialogFor(plugin_id_t pluginID, QWidget *parent) const {
	if (!tryBeginPluginProtectedCall()) return PluginDialogOpenResult::Busy;
	const auto protectedGuard = qScopeGuard([this]() { endPluginProtectedCall(); });
	plugin_ptr_t plugin;
	{
		QReadLocker lock(&m_pluginCollectionLock);
		plugin = m_pluginHashMap.value(pluginID);
	}
	if (!plugin) return PluginDialogOpenResult::Missing;
	if (!plugin->isLoaded()) return PluginDialogOpenResult::Unavailable;
	try {
		return plugin->showConfigDialog(parent) ? PluginDialogOpenResult::Opened
			: PluginDialogOpenResult::Unavailable;
	} catch (const std::exception &exception) {
		Log::logOrDefer(Log::Warning, tr("Plugin configuration dialog failed: %1")
			.arg(QString::fromUtf8(exception.what())));
	} catch (...) {
		Log::logOrDefer(Log::Warning, tr("Plugin configuration dialog failed with an unknown exception"));
	}
	return PluginDialogOpenResult::Failed;
}

PluginDialogOpenResult PluginManager::showAboutDialogFor(plugin_id_t pluginID, QWidget *parent) const {
	if (!tryBeginPluginProtectedCall()) return PluginDialogOpenResult::Busy;
	const auto protectedGuard = qScopeGuard([this]() { endPluginProtectedCall(); });
	plugin_ptr_t plugin;
	{
		QReadLocker lock(&m_pluginCollectionLock);
		plugin = m_pluginHashMap.value(pluginID);
	}
	if (!plugin) return PluginDialogOpenResult::Missing;
	if (!plugin->isLoaded()) return PluginDialogOpenResult::Unavailable;
	try {
		return plugin->showAboutDialog(parent) ? PluginDialogOpenResult::Opened
			: PluginDialogOpenResult::Unavailable;
	} catch (const std::exception &exception) {
		Log::logOrDefer(Log::Warning, tr("Plugin About dialog failed: %1")
			.arg(QString::fromUtf8(exception.what())));
	} catch (...) {
		Log::logOrDefer(Log::Warning, tr("Plugin About dialog failed with an unknown exception"));
	}
	return PluginDialogOpenResult::Failed;
}

bool PluginManager::pluginExists(plugin_id_t pluginID) const {
	QReadLocker lock(&m_pluginCollectionLock);
	return m_pluginHashMap.contains(pluginID) || m_pluginStagingHashMap.contains(pluginID);
}

void PluginManager::foreachPlugin(std::function< void(Plugin &) > pluginProcessor) const {
	if (m_shuttingDown.load()) return;
	const PluginRuntimeCallAdmission admission = beginPluginRuntimeCall();
	if (admission == PluginRuntimeCallAdmission::Rejected) return;
	const auto runtimeGuard = qScopeGuard([this, admission]() {
		endPluginRuntimeCall(admission);
	});
	QVector< plugin_ptr_t > plugins;
	{
		QReadLocker lock(&m_pluginCollectionLock);
		plugins.reserve(m_pluginHashMap.size());
		for (auto it = m_pluginHashMap.cbegin(); it != m_pluginHashMap.cend(); ++it)
			plugins.push_back(it.value());
	}
	for (const plugin_ptr_t &plugin : std::as_const(plugins)) {
		if (!plugin || !isPluginRuntimeEnabled(plugin->getFilePath())) continue;
		try {
			pluginProcessor(*plugin);
		} catch (const std::exception &exception) {
			qWarning("Plugin runtime callback failed: %s", exception.what());
		} catch (...) {
			qWarning("Plugin runtime callback failed with an unknown exception");
		}
	}
}

bool PluginManager::enqueuePluginRuntimeNotification(
	PluginRuntimeCallback callback, const std::optional< PluginAbiWorker::RuntimeQueueClass > boundedClass,
	QByteArray coalescingKey, const qsizetype payloadBytes) const {
	if (!callback || m_shuttingDown.load(std::memory_order_acquire)) return false;
	auto work = [this, callback = std::move(callback)]() mutable {
		if (m_shuttingDown.load(std::memory_order_acquire)) return;
		foreachPlugin(std::move(callback));
	};

	const bool queued = boundedClass
		? sharedPluginAbiWorker().enqueueRuntime(std::move(work), *boundedClass, std::move(coalescingKey),
			payloadBytes)
		: sharedPluginAbiWorker().enqueue(std::move(work));
	if (!queued && boundedClass && !m_shuttingDown.load(std::memory_order_acquire)) {
		reportPluginRuntimeNotificationOverflow(*boundedClass);
	}
	return queued;
}

void PluginManager::reportPluginRuntimeNotificationOverflow(
	const PluginAbiWorker::RuntimeQueueClass queueClass) const {
	if (queueClass == PluginAbiWorker::RuntimeQueueClass::Count) return;
	auto &reported = m_runtimeNotificationOverflowReported[static_cast< std::size_t >(queueClass)];
	if (reported.exchange(true, std::memory_order_acq_rel)) return;
	const char *family = "talk-state";
	switch (queueClass) {
		case PluginAbiWorker::RuntimeQueueClass::TalkState: family = "talk-state"; break;
		case PluginAbiWorker::RuntimeQueueClass::ChannelRename: family = "channel-rename"; break;
		case PluginAbiWorker::RuntimeQueueClass::Key: family = "keyboard"; break;
		case PluginAbiWorker::RuntimeQueueClass::Data: family = "data"; break;
		case PluginAbiWorker::RuntimeQueueClass::Count: return;
	}
	qWarning("Plugin %s notification queue reached its safety limit; newest non-coalescible event was dropped",
		family);
}

void PluginManager::on_serverConnected() const {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (m_shuttingDown.load() || !serverHandler) return;
	const mumble_connection_t connectionID = serverHandler->getConnectionID();

#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug("PluginManager: Connected to a server with connection ID %d", connectionID);
#endif

	enqueuePluginRuntimeNotification([connectionID](Plugin &plugin) {
		if (plugin.isLoaded()) {
			plugin.onServerConnected(connectionID);
		}
	});
}

void PluginManager::on_serverDisconnected() const {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (m_shuttingDown.load() || !serverHandler) return;
	const mumble_connection_t connectionID = serverHandler->getConnectionID();

#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug("PluginManager: Disconnected from a server with connection ID %d", connectionID);
#endif

	enqueuePluginRuntimeNotification([connectionID](Plugin &plugin) {
		if (plugin.isLoaded()) {
			plugin.onServerDisconnected(connectionID);
		}
	});
}

void PluginManager::on_channelEntered(const Channel *newChannel, const Channel *prevChannel, const User *user) const {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (m_shuttingDown.load() || !serverHandler || !newChannel || !user) return;
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug() << "PluginManager: User" << user->qsName << "entered channel" << newChannel->qsName
			 << "- ID:" << newChannel->iId;
#endif

	const mumble_connection_t connectionID = serverHandler->getConnectionID();

	const mumble_userid_t userID = user->uiSession;
	const mumble_channelid_t previousChannelID = prevChannel ? static_cast< int >(prevChannel->iId) : -1;
	const mumble_channelid_t newChannelID = static_cast< int >(newChannel->iId);
	enqueuePluginRuntimeNotification([userID, previousChannelID, newChannelID, connectionID](Plugin &plugin) {
		if (plugin.isLoaded()) {
			plugin.onChannelEntered(connectionID, userID, previousChannelID, newChannelID);
		}
	});
}

void PluginManager::on_channelExited(const Channel *channel, const User *user) const {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (m_shuttingDown.load() || !serverHandler || !channel || !user) return;
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug() << "PluginManager: User" << user->qsName << "left channel" << channel->qsName << "- ID:" << channel->iId;
#endif

	const mumble_connection_t connectionID = serverHandler->getConnectionID();

	const mumble_userid_t userID = user->uiSession;
	const mumble_channelid_t channelID = static_cast< int >(channel->iId);
	enqueuePluginRuntimeNotification([userID, channelID, connectionID](Plugin &plugin) {
		if (plugin.isLoaded()) {
			plugin.onChannelExited(connectionID, userID, channelID);
		}
	});
}

QString getTalkingStateStr(Settings::TalkState ts) {
	switch (ts) {
		case Settings::TalkState::Passive:
			return QString::fromLatin1("Passive");
		case Settings::TalkState::Talking:
			return QString::fromLatin1("Talking");
		case Settings::TalkState::Whispering:
			return QString::fromLatin1("Whispering");
		case Settings::TalkState::Shouting:
			return QString::fromLatin1("Shouting");
		case Settings::TalkState::MutedTalking:
			return QString::fromLatin1("MutedTalking");
	}

	return QString::fromLatin1("Unknown");
}

void PluginManager::on_userTalkingStateChanged() const {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (m_shuttingDown.load() || !serverHandler) return;
	const ClientUser *user = qobject_cast< ClientUser * >(QObject::sender());
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	if (user) {
		qDebug() << "PluginManager: User" << user->qsName << "changed talking state to"
				 << getTalkingStateStr(user->tsState);
	} else {
		qCritical() << "PluginManager: Unable to identify ClientUser";
	}
#endif

	if (user) {
		// Convert Mumble's talking state to the TalkingState used in the API
		mumble_talking_state_t ts = MUMBLE_TS_INVALID;

		switch (user->tsState) {
			case Settings::TalkState::Passive:
				ts = MUMBLE_TS_PASSIVE;
				break;
			case Settings::TalkState::Talking:
				ts = MUMBLE_TS_TALKING;
				break;
			case Settings::TalkState::Whispering:
				ts = MUMBLE_TS_WHISPERING;
				break;
			case Settings::TalkState::Shouting:
				ts = MUMBLE_TS_SHOUTING;
				break;
			case Settings::TalkState::MutedTalking:
				ts = MUMBLE_TS_TALKING_MUTED;
				break;
		}

		if (ts == MUMBLE_TS_INVALID) {
			qWarning("PluginManager.cpp: Invalid talking state encountered");
			// An error occurred
			return;
		}

		const mumble_connection_t connectionID = serverHandler->getConnectionID();

		const mumble_userid_t userID = user->uiSession;
		const QByteArray coalescingKey = QByteArrayLiteral("talk:")
			+ QByteArray::number(static_cast< qulonglong >(connectionID)) + ':'
			+ QByteArray::number(static_cast< qulonglong >(userID));
		enqueuePluginRuntimeNotification([userID, ts, connectionID](Plugin &plugin) {
			if (plugin.isLoaded()) {
				plugin.onUserTalkingStateChanged(connectionID, userID, ts);
			}
		}, PluginAbiWorker::RuntimeQueueClass::TalkState, coalescingKey);
	}
}

void PluginManager::on_audioInput(short *inputPCM, unsigned int sampleCount, unsigned int channelCount,
								  unsigned int sampleRate, bool isSpeech) const {
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug() << "PluginManager: AudioInput with" << channelCount << "channels and" << sampleCount
			 << "samples per channel. IsSpeech:" << isSpeech;
#endif

	foreachPlugin([inputPCM, sampleCount, channelCount, sampleRate, isSpeech](Plugin &plugin) {
		if (plugin.isLoaded()) {
			plugin.onAudioInput(inputPCM, sampleCount, static_cast< std::uint16_t >(channelCount), sampleRate,
								isSpeech);
		}
	});
}

void PluginManager::on_audioSourceFetched(float *outputPCM, unsigned int sampleCount, unsigned int channelCount,
										  unsigned int sampleRate, bool isSpeech, const ClientUser *user) const {
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug() << "PluginManager: AudioSource with" << channelCount << "channels and" << sampleCount
			 << "samples per channel fetched. IsSpeech:" << isSpeech;
	if (user != nullptr) {
		qDebug() << "Sender-ID:" << user->uiSession;
	}
#endif

	foreachPlugin([outputPCM, sampleCount, channelCount, sampleRate, isSpeech, user](Plugin &plugin) {
		if (plugin.isLoaded()) {
			plugin.onAudioSourceFetched(outputPCM, sampleCount, static_cast< std::uint16_t >(channelCount), sampleRate,
										isSpeech, user ? user->uiSession : static_cast< unsigned int >(-1));
		}
	});
}

void PluginManager::on_audioOutputAboutToPlay(float *outputPCM, unsigned int sampleCount, unsigned int channelCount,
											  unsigned int sampleRate, bool *modifiedAudio) const {
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug() << "PluginManager: AudioOutput with" << channelCount << "channels and" << sampleCount
			 << "samples per channel";
#endif
	foreachPlugin([outputPCM, sampleCount, channelCount, sampleRate, modifiedAudio](Plugin &plugin) {
		if (plugin.isLoaded()) {
			if (plugin.onAudioOutputAboutToPlay(outputPCM, sampleCount, static_cast< std::uint16_t >(channelCount),
												sampleRate)) {
				*modifiedAudio = true;
			}
		}
	});
}

void PluginManager::on_receiveData(const ClientUser *sender, const uint8_t *data, size_t dataLength,
								   const char *dataID) const {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (m_shuttingDown.load() || !serverHandler || !sender) return;
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug() << "PluginManager: Data with ID" << dataID << "and length" << dataLength
			 << "received. Sender-ID:" << sender->uiSession;
#endif

	if ((!data && dataLength > 0)
		|| dataLength > static_cast< size_t >(PluginAbiWorker::MaximumPendingDataBytes)) {
		reportPluginRuntimeNotificationOverflow(PluginAbiWorker::RuntimeQueueClass::Data);
		return;
	}
	const mumble_connection_t connectionID = serverHandler->getConnectionID();
	const mumble_userid_t senderID = sender->uiSession;
	QByteArray payload(reinterpret_cast< const char * >(data), static_cast< qsizetype >(dataLength));
	QByteArray identifier(dataID ? dataID : "");
	const qsizetype payloadBytes = payload.size() + identifier.size();

	enqueuePluginRuntimeNotification(
		[senderID, payload = std::move(payload), identifier = std::move(identifier), connectionID](Plugin &plugin) {
		if (plugin.isLoaded()) {
			plugin.onReceiveData(connectionID, senderID,
				reinterpret_cast< const uint8_t * >(payload.constData()), static_cast< size_t >(payload.size()),
				identifier.constData());
		}
	}, PluginAbiWorker::RuntimeQueueClass::Data, {}, payloadBytes);
}

void PluginManager::on_serverSynchronized() const {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (m_shuttingDown.load() || !serverHandler) return;
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug() << "PluginManager: Server synchronized";
#endif

	const mumble_connection_t connectionID = serverHandler->getConnectionID();

	enqueuePluginRuntimeNotification([connectionID](Plugin &plugin) {
		if (plugin.isLoaded()) {
			plugin.onServerSynchronized(connectionID);
		}
	});
}

void PluginManager::on_userAdded(mumble_userid_t userID) const {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (m_shuttingDown.load() || !serverHandler) return;
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug() << "PluginManager: Added user with ID" << userID;
#endif

	const mumble_connection_t connectionID = serverHandler->getConnectionID();

	enqueuePluginRuntimeNotification([userID, connectionID](Plugin &plugin) {
		if (plugin.isLoaded()) {
			plugin.onUserAdded(connectionID, userID);
		};
	});
}

void PluginManager::on_userRemoved(mumble_userid_t userID) const {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (m_shuttingDown.load() || !serverHandler) return;
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug() << "PluginManager: Removed user with ID" << userID;
#endif

	const mumble_connection_t connectionID = serverHandler->getConnectionID();

	enqueuePluginRuntimeNotification([userID, connectionID](Plugin &plugin) {
		if (plugin.isLoaded()) {
			plugin.onUserRemoved(connectionID, userID);
		};
	});
}

void PluginManager::on_channelAdded(mumble_channelid_t channelID) const {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (m_shuttingDown.load() || !serverHandler) return;
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug() << "PluginManager: Added channel with ID" << channelID;
#endif

	const mumble_connection_t connectionID = serverHandler->getConnectionID();

	enqueuePluginRuntimeNotification([channelID, connectionID](Plugin &plugin) {
		if (plugin.isLoaded()) {
			plugin.onChannelAdded(connectionID, channelID);
		};
	});
}

void PluginManager::on_channelRemoved(mumble_channelid_t channelID) const {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (m_shuttingDown.load() || !serverHandler) return;
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug() << "PluginManager: Removed channel with ID" << channelID;
#endif

	const mumble_connection_t connectionID = serverHandler->getConnectionID();

	enqueuePluginRuntimeNotification([channelID, connectionID](Plugin &plugin) {
		if (plugin.isLoaded()) {
			plugin.onChannelRemoved(connectionID, channelID);
		};
	});
}

void PluginManager::on_channelRenamed(int channelID) const {
	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (m_shuttingDown.load() || !serverHandler) return;
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug() << "PluginManager: Renamed channel with ID" << channelID;
#endif

	const mumble_connection_t connectionID = serverHandler->getConnectionID();

	const QByteArray coalescingKey = QByteArrayLiteral("rename:")
		+ QByteArray::number(static_cast< qulonglong >(connectionID)) + ':'
		+ QByteArray::number(static_cast< qlonglong >(channelID));
	enqueuePluginRuntimeNotification([channelID, connectionID](Plugin &plugin) {
		if (plugin.isLoaded()) {
			plugin.onChannelRenamed(connectionID, channelID);
		};
	}, PluginAbiWorker::RuntimeQueueClass::ChannelRename, coalescingKey);
}

void PluginManager::on_keyEvent(unsigned int key, Qt::KeyboardModifiers modifiers, bool isPress) const {
#ifdef MUMBLE_PLUGIN_CALLBACK_DEBUG
	qDebug() << "PluginManager: Key event detected: keyCode =" << key << "modifiers:" << modifiers
			 << "isPress =" << isPress;
#else
	Q_UNUSED(modifiers);
#endif

	// Convert from Qt encoding to our own encoding
	mumble_keycode_t keyCode = API::qtKeyCodeToAPIKeyCode(key);

	enqueuePluginRuntimeNotification([this, keyCode, isPress](Plugin &plugin) {
		const std::optional< PluginDescriptor > descriptor = pluginDescriptor(plugin.getID());
		if (plugin.isLoaded() && descriptor && descriptor->keyboardMonitoringAllowed
			&& isPluginKeyboardPermissionGranted(descriptor->path)) {
			plugin.onKeyEvent(keyCode, isPress);
		}
	}, PluginAbiWorker::RuntimeQueueClass::Key);
}

void PluginManager::publishPositionalDataSnapshot(const bool valid, const QString &context,
											  const QString &identity) {
	Q_ASSERT(QThread::currentThread() == thread());
	if (m_shuttingDown.load()) return;
	const bool mayPublish = Global::get().s.bTransmitPosition
		&& (Global::get().bPosTest || m_activePositionalPermissionAllowed.load());
	if (!valid || !mayPublish) {
		QMutexLocker lock(&m_sentDataMutex);
		if (m_sentData.identity.isEmpty() && m_sentData.context.isEmpty()) return;
		MumbleProto::UserState state;
		if (Global::get().uiSession) state.set_session(Global::get().uiSession);
		state.set_plugin_context("");
		state.set_plugin_identity("");
		const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
		if (serverHandler) serverHandler->sendMessage(state);
		m_sentData.identity.clear();
		m_sentData.context.clear();
		return;
	}

	if (!Global::get().uiSession) {
		// For some reason the local session ID is not set -> clear all data sent to the server in order to
		// guarantee a re-send once the session is restored and there is data available.
		QMutexLocker lock(&m_sentDataMutex);
		m_sentData.context.clear();
		m_sentData.identity.clear();
		return;
	}

	QMutexLocker lock(&m_sentDataMutex);
	if (m_sentData.context == context && m_sentData.identity == identity) return;

	MumbleProto::UserState state;
	state.set_session(Global::get().uiSession);
	if (m_sentData.context != context) {
		m_sentData.context = context;
		state.set_plugin_context(m_sentData.context.toUtf8().constData(),
								 static_cast< std::size_t >(m_sentData.context.size()));
	}
	if (m_sentData.identity != identity) {
		m_sentData.identity = identity;
		state.set_plugin_identity(m_sentData.identity.toUtf8().constData());
	}

	const ServerHandlerPtr serverHandler = Global::get().serverHandlerSnapshot();
	if (serverHandler) serverHandler->sendMessage(state);
}

void PluginManager::on_syncPositionalData() {
	Q_ASSERT(QThread::currentThread() == thread());
	if (m_shuttingDown.load()) return;
	if (!Global::get().s.bTransmitPosition
		|| (!Global::get().bPosTest && !m_activePositionalPermissionAllowed.load())) {
		publishPositionalDataSnapshot(false, {}, {});
		return;
	}
	if (m_positionalServerSyncPending.exchange(true)) return;

	const QPointer< PluginManager > manager(this);
	const bool queued = sharedPluginAbiWorker().enqueue([this, manager]() mutable {
		bool posted = false;
		const auto pendingGuard = qScopeGuard([this, &posted]() {
			if (!posted) m_positionalServerSyncPending.store(false);
		});
		if (m_shuttingDown.load()) return;

		const bool valid = fetchPositionalData();
		QString context;
		QString identity;
		if (valid) {
			QReadLocker lock(&m_positionalData.m_lock);
			context  = m_positionalData.m_context;
			identity = m_positionalData.m_identity;
		}
		if (m_shuttingDown.load()) return;

		posted = QMetaObject::invokeMethod(this,
			[manager, valid, context = std::move(context), identity = std::move(identity)]() {
				if (!manager) return;
				manager->m_positionalServerSyncPending.store(false);
				if (manager->m_shuttingDown.load()) return;
				manager->publishPositionalDataSnapshot(valid, context, identity);
			}, Qt::QueuedConnection);
	});
	if (!queued) m_positionalServerSyncPending.store(false);
}

void PluginManager::on_updatesAvailable() {
	if (m_shuttingDown.load()) return;
	if (Global::get().s.bPluginAutoUpdate) {
		m_updater.update();
	} else if (Global::get().mw) {
		Global::get().mw->openModernPluginUpdateDialog(availablePluginUpdates());
	}
}

void PluginManager::checkForAvailablePositionalDataPlugin() {
	if (m_shuttingDown.load()) return;
	bool performSearch = false;
	{
		QReadLocker activePluginLock(&m_activePosDataPluginLock);

		performSearch = !m_activePositionalDataPlugin;
	}

	if (performSearch) {
		selectActivePositionalDataPlugin();
	}
}

void PluginManager::reportLostLink(mumble_plugin_id_t pluginID) {
	// We are calling GUI code, so we must only execute this function from the GUI (main) thread - which we assume is
	// where the plugin manager object is living in.
	assert(this->thread() == QThread::currentThread());

	if (m_shuttingDown.load()) return;
	const std::optional< PluginDescriptor > plugin = pluginDescriptor(pluginID);

	// Need to check for the presence of Global::get().l in case we are currently
	// shutting down Mumble in which case the Log might already have been deleted.
	if (plugin && Global::get().l) {
		Global::get().l->log(Log::Information,
							 PluginManager::tr("%1 lost link").arg(plugin->name.toHtmlEscaped()));
	}
}

void PluginManager::reportPluginLinked(mumble_plugin_id_t pluginID) {
	// We are calling GUI code, so we must only execute this function from the GUI (main) thread - which we assume is
	// where the plugin manager object is living in.
	assert(this->thread() == QThread::currentThread());

	if (m_shuttingDown.load() || !Global::get().l) return;
	const std::optional< PluginDescriptor > plugin = pluginDescriptor(pluginID);

	if (plugin) {
		Global::get().l->log(Log::Information, tr("%1 linked").arg(plugin->name.toHtmlEscaped()));
	}
}

void PluginManager::reportPermanentError(mumble_plugin_id_t pluginID) {
	// We are calling GUI code, so we must only execute this function from the GUI (main) thread - which we assume is
	// where the plugin manager object is living in.
	assert(this->thread() == QThread::currentThread());

	if (m_shuttingDown.load() || !Global::get().l) return;
	const std::optional< PluginDescriptor > plugin = pluginDescriptor(pluginID);

	if (plugin) {
		Global::get().l->log(
			Log::Warning,
			tr("Plugin \"%1\" encountered a permanent error in positional data gathering").arg(plugin->name));
	}
}
