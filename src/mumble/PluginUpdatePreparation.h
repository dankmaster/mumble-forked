#ifndef MUMBLE_MUMBLE_PLUGINUPDATEPREPARATION_H_
#define MUMBLE_MUMBLE_PLUGINUPDATEPREPARATION_H_

#include <QtCore/QByteArray>
#include <QtCore/QString>
#include <QtCore/QStringList>

#include <atomic>
#include <functional>

struct PluginUpdateFileResult {
	QString path;
	QString errorCode;
	QString message;
	bool cancelled = false;
};

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
	int pendingRemoved = 0;
	int backupsRestored = 0;
	int backupsRemoved = 0;
	QStringList errors;
};
PluginTransactionRecoveryResult recoverPluginFileTransactions(const QString &directoryPath);

#endif
