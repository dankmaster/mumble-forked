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
