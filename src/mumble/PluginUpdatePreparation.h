#ifndef MUMBLE_MUMBLE_PLUGINUPDATEPREPARATION_H_
#define MUMBLE_MUMBLE_PLUGINUPDATEPREPARATION_H_

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QUrl>

#include <atomic>
#include <functional>

struct PluginUpdateFileResult {
	QString path;
	QString errorCode;
	QString message;
	bool cancelled = false;
};

struct PluginUpdateUrlValidationResult {
	bool accepted = false;
	QString errorCode;
	QString message;
};

/// Accepts only absolute HTTPS update URLs. Every redirect target is validated with the same policy.
PluginUpdateUrlValidationResult validatePluginUpdateUrl(const QUrl &url, const QUrl &previousUrl = {});

/// Returns whether an update package is bound to the exact installed plugin path it was checked for.
/// Windows paths are compared case-insensitively, matching the platform's normal filesystem semantics.
bool pluginUpdateDestinationMatchesInstalledPath(const QString &destinationPath, const QString &installedPath);

/// Resolves a checked installed plugin path as an update destination, but only when it is a direct child of the
/// configured plugin directory. This binds raw-library downloads to the installed basename instead of a random
/// temporary filename or an update-provider-controlled rename.
QString boundPluginUpdateDestination(const QString &destinationDirectory, const QString &installedPath);

/// Enumerates readable platform-library candidates after any transaction recovery has completed.
QStringList discoverPluginLibraryPaths(const QStringList &searchPaths,
										 const std::atomic< bool > *cancelled = nullptr);

/// Hashes a plugin file without loading it. `ok` is false for missing/read-error files.
QByteArray pluginFileSha256(const QString &path, bool *ok = nullptr,
							  const std::atomic< bool > *cancelled = nullptr);

struct PluginFileCommitResult {
	bool success = false;
	bool cancelled = false;
	bool rolledBack = false;
	QString errorCode;
	QString message;
	QString destinationPath;
	QString backupPath;
};

PluginUpdateFileResult preparePluginUpdateFile(const QByteArray &content, const QString &fileName,
										   const std::atomic< bool > *cancelled = nullptr);
PluginFileCommitResult commitPreparedPluginFile(const QString &sourcePath, const QString &destinationPath,
											bool allowOverwrite, const std::atomic< bool > *cancelled = nullptr,
											const std::function< void() > &afterBackup = {},
											const QByteArray &expectedSha256 = {});
bool finalizePluginFileCommit(const PluginFileCommitResult &commit);
bool rollbackPluginFileCommit(const PluginFileCommitResult &commit);
struct PluginTransactionRecoveryResult {
	bool cancelled = false;
	int pendingRemoved = 0;
	int backupsRestored = 0;
	int backupsRemoved = 0;
	int ambiguousArtifactsIgnored = 0;
	int ambiguousGroups = 0;
	QStringList errors;
};
PluginTransactionRecoveryResult recoverPluginFileTransactions(
	const QString &directoryPath, const std::atomic< bool > *cancelled = nullptr);

#endif
