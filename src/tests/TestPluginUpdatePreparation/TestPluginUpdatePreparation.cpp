#include "PluginUpdatePreparation.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtTest/QtTest>

class TestPluginUpdatePreparation : public QObject {
	Q_OBJECT
private slots:
	void writesUniqueTemporaryPackage();
	void reportsEmptyAndCancellation();
	void commitsOverwriteAndPreservesDestinationOnCancel();
	void recoversAbandonedTransactionsConservatively();
};

void TestPluginUpdatePreparation::writesUniqueTemporaryPackage() {
	const QByteArray payload("plugin-package");
	const PluginUpdateFileResult first = preparePluginUpdateFile(payload, QStringLiteral("update.mumble_plugin"));
	const PluginUpdateFileResult second = preparePluginUpdateFile(payload, QStringLiteral("update.mumble_plugin"));
	QVERIFY(first.errorCode.isEmpty());
	QVERIFY(second.errorCode.isEmpty());
	QVERIFY(first.path != second.path);
	QFile file(first.path);
	QVERIFY(file.open(QIODevice::ReadOnly));
	QCOMPARE(file.readAll(), payload);
	file.close();
	QFile::remove(first.path);
	QFile::remove(second.path);
}

void TestPluginUpdatePreparation::recoversAbandonedTransactionsConservatively() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const auto write = [&directory](const QString &name, const QByteArray &bytes) {
		QFile file(directory.filePath(name));
		if (!file.open(QIODevice::WriteOnly)) return false;
		return file.write(bytes) == bytes.size();
	};
	QVERIFY(write(QStringLiteral("orphan.dll.pending-token"), QByteArray("pending")));
	QVERIFY(write(QStringLiteral("restore.dll.backup-token"), QByteArray("old")));
	QVERIFY(write(QStringLiteral("keep.dll"), QByteArray("new")));
	QVERIFY(write(QStringLiteral("keep.dll.backup-token"), QByteArray("old")));
	const PluginTransactionRecoveryResult result = recoverPluginFileTransactions(directory.path());
	QCOMPARE(result.pendingRemoved, 1);
	QCOMPARE(result.backupsRestored, 2);
	QCOMPARE(result.backupsRemoved, 0);
	QVERIFY(result.errors.isEmpty());
	QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("orphan.dll.pending-token"))));
	QFile restored(directory.filePath(QStringLiteral("restore.dll")));
	QVERIFY(restored.open(QIODevice::ReadOnly));
	QCOMPARE(restored.readAll(), QByteArray("old"));
	QFile kept(directory.filePath(QStringLiteral("keep.dll")));
	QVERIFY(kept.open(QIODevice::ReadOnly));
	QCOMPARE(kept.readAll(), QByteArray("old"));
}

void TestPluginUpdatePreparation::commitsOverwriteAndPreservesDestinationOnCancel() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString sourcePath = directory.filePath(QStringLiteral("source.dll"));
	const QString destinationPath = directory.filePath(QStringLiteral("plugin.dll"));
	for (const auto &[path, bytes] : { std::pair(sourcePath, QByteArray("new")),
									 std::pair(destinationPath, QByteArray("old")) }) {
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QCOMPARE(file.write(bytes), bytes.size());
	}
	const PluginFileCommitResult overwrite = commitPreparedPluginFile(sourcePath, destinationPath, true);
	QVERIFY(overwrite.success);
	QVERIFY(!overwrite.backupPath.isEmpty());
	QVERIFY(QFileInfo::exists(overwrite.backupPath));
	QFile installed(destinationPath);
	QVERIFY(installed.open(QIODevice::ReadOnly));
	QCOMPARE(installed.readAll(), QByteArray("new"));
	installed.close();
	QVERIFY(rollbackPluginFileCommit(overwrite));
	QVERIFY(installed.open(QIODevice::ReadOnly));
	QCOMPARE(installed.readAll(), QByteArray("old"));
	installed.close();
	const PluginFileCommitResult finalizedCommit = commitPreparedPluginFile(sourcePath, destinationPath, true);
	QVERIFY(finalizedCommit.success);
	QVERIFY(finalizePluginFileCommit(finalizedCommit));
	QVERIFY(!QFileInfo::exists(finalizedCommit.backupPath));
	std::atomic< bool > cancelAfterBackup(false);
	const PluginFileCommitResult postBackupCancel = commitPreparedPluginFile(
		sourcePath, destinationPath, true, &cancelAfterBackup, [&cancelAfterBackup]() { cancelAfterBackup.store(true); });
	QVERIFY(postBackupCancel.cancelled);
	QVERIFY(postBackupCancel.rolledBack);
	QVERIFY(installed.open(QIODevice::ReadOnly));
	QCOMPARE(installed.readAll(), QByteArray("new"));
	installed.close();
	QFile replacement(sourcePath);
	QVERIFY(replacement.open(QIODevice::WriteOnly | QIODevice::Truncate));
	QCOMPARE(replacement.write("next"), 4);
	replacement.close();
	std::atomic< bool > cancelled(true);
	const PluginFileCommitResult stopped = commitPreparedPluginFile(sourcePath, destinationPath, true, &cancelled);
	QVERIFY(stopped.cancelled);
	QVERIFY(installed.open(QIODevice::ReadOnly));
	QCOMPARE(installed.readAll(), QByteArray("new"));
}

void TestPluginUpdatePreparation::reportsEmptyAndCancellation() {
	const PluginUpdateFileResult empty = preparePluginUpdateFile({}, QStringLiteral("empty.mumble_plugin"));
	QCOMPARE(empty.errorCode, QStringLiteral("empty-download"));
	std::atomic< bool > cancelled(true);
	const PluginUpdateFileResult stopped =
		preparePluginUpdateFile(QByteArray("payload"), QStringLiteral("cancelled.dll"), &cancelled);
	QVERIFY(stopped.cancelled);
	QCOMPARE(stopped.errorCode, QStringLiteral("cancelled"));
	QVERIFY(stopped.path.isEmpty());
}

QTEST_GUILESS_MAIN(TestPluginUpdatePreparation)
#include "TestPluginUpdatePreparation.moc"
