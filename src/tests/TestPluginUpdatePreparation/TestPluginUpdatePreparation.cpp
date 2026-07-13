#include "PluginUpdatePreparation.h"
#include "PluginManifest.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtTest/QtTest>

#include <sstream>

class TestPluginUpdatePreparation : public QObject {
	Q_OBJECT
private slots:
	void writesUniqueTemporaryPackage();
	void reportsEmptyAndCancellation();
	void commitsOverwriteAndPreservesDestinationOnCancel();
	void rejectsOverwriteAndHashMismatchWithoutMutation();
	void validatesUpdateUrlPolicy();
	void bindsUpdateDestinationToInstalledPlugin();
	void bindsRawLibraryUpdateToInstalledBasename();
	void discoversLibraryRestoredByRecovery();
	void fingerprintsOverwriteIdentity();
	void recoversOnlyExactUuidTransactions();
	void skipsRecoveryWhenAlreadyCancelled();
	void ignoresAmbiguousTransactionsWithoutReplacingDestination();
	void isolatesRecoveryByDestination();
	void rejectsMalformedPluginManifest_data();
	void rejectsMalformedPluginManifest();
	void detectsPackageWithoutCurrentRuntime();
};

void TestPluginUpdatePreparation::rejectsMalformedPluginManifest_data() {
	QTest::addColumn< QByteArray >("manifestXml");

	QTest::newRow("malformed-xml")
		<< QByteArray("<bundle version=\"1.0.0\"><assets>");
	QTest::newRow("wrong-root")
		<< QByteArray("<plugin version=\"1.0.0\"><assets/></plugin>");
	QTest::newRow("unknown-format")
		<< QByteArray("<bundle version=\"2.0.0\"><assets/></bundle>");
	QTest::newRow("missing-assets")
		<< QByteArray("<bundle version=\"1.0.0\"><name>Example</name><version>1.2.3</version></bundle>");
	QTest::newRow("missing-runtime-architecture")
		<< QByteArray("<bundle version=\"1.0.0\"><assets><plugin os=\"windows\">plugin.dll</plugin></assets>"
					  "<name>Example</name><version>1.2.3</version></bundle>");
	QTest::newRow("invalid-plugin-version")
		<< QByteArray("<bundle version=\"1.0.0\"><assets><plugin os=\"windows\" arch=\"x86_64\">"
					  "plugin.dll</plugin></assets><name>Example</name><version>latest</version></bundle>");
}

void TestPluginUpdatePreparation::rejectsMalformedPluginManifest() {
	QFETCH(QByteArray, manifestXml);
	std::istringstream input(std::string(manifestXml.constData(), static_cast< std::size_t >(manifestXml.size())));
	PluginManifest manifest;
	QVERIFY_THROWS_EXCEPTION(PluginManifestException, manifest.parse(input));
}

void TestPluginUpdatePreparation::detectsPackageWithoutCurrentRuntime() {
	const std::string unsupportedOS = std::string(MUMBLE_TARGET_OS) + "-unsupported";
	const std::string xml = std::string("<bundle version=\"1.0.0\"><assets><plugin os=\"")
		+ unsupportedOS + "\" arch=\"" + MUMBLE_TARGET_ARCH
		+ "\">plugin.bin</plugin></assets><name>Other platform</name><version>1.2.3</version></bundle>";
	std::istringstream input(xml);
	PluginManifest manifest;
	manifest.parse(input);

	QVERIFY(manifest.specifiesPluginPath(unsupportedOS, MUMBLE_TARGET_ARCH));
	QVERIFY(!manifest.specifiesPluginPath(MUMBLE_TARGET_OS, MUMBLE_TARGET_ARCH));
	QCOMPARE(QString::fromStdString(manifest.getPluginPath(unsupportedOS, MUMBLE_TARGET_ARCH)),
		QStringLiteral("plugin.bin"));
}

void TestPluginUpdatePreparation::skipsRecoveryWhenAlreadyCancelled() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString token = QStringLiteral("12345678-1234-4abc-8def-123456789abc");
	const QString backupPath = directory.filePath(QStringLiteral("plugin.dll.backup-") + token);
	const QString pendingPath = directory.filePath(QStringLiteral("orphan.dll.pending-") + token);
	for (const QString &path : { backupPath, pendingPath }) {
		QFile file(path);
		QVERIFY(file.open(QIODevice::WriteOnly));
		QCOMPARE(file.write("plugin"), 6);
	}

	std::atomic< bool > cancelled(true);
	const PluginTransactionRecoveryResult result =
		recoverPluginFileTransactions(directory.path(), &cancelled);
	QVERIFY(result.cancelled);
	QCOMPARE(result.backupsRestored, 0);
	QCOMPARE(result.pendingRemoved, 0);
	QVERIFY(QFileInfo::exists(backupPath));
	QVERIFY(QFileInfo::exists(pendingPath));
	QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("plugin.dll"))));
}

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

void TestPluginUpdatePreparation::recoversOnlyExactUuidTransactions() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const auto write = [&directory](const QString &name, const QByteArray &bytes) {
		QFile file(directory.filePath(name));
		if (!file.open(QIODevice::WriteOnly)) return false;
		return file.write(bytes) == bytes.size();
	};
	const QString token = QStringLiteral("1f2e3d4c-5b6a-4789-8abc-0123456789ab");
	QVERIFY(write(QStringLiteral("orphan.dll.pending-") + token, QByteArray("pending")));
	QVERIFY(write(QStringLiteral("restore.dll.pending-") + token, QByteArray("pending")));
	QVERIFY(write(QStringLiteral("restore.dll.backup-") + token, QByteArray("old")));
	QVERIFY(write(QStringLiteral("lookalike.dll.pending-token"), QByteArray("keep")));
	QVERIFY(write(QStringLiteral("trailing.dll.backup-") + token + QStringLiteral(".extra"), QByteArray("keep")));
	QVERIFY(write(QStringLiteral("null.dll.pending-00000000-0000-0000-0000-000000000000"), QByteArray("keep")));
	QVERIFY(write(QStringLiteral("non-v4.dll.pending-10000000-0000-3000-8000-000000000001"),
				  QByteArray("keep")));
	const PluginTransactionRecoveryResult result = recoverPluginFileTransactions(directory.path());
	QCOMPARE(result.pendingRemoved, 2);
	QCOMPARE(result.backupsRestored, 1);
	QCOMPARE(result.backupsRemoved, 0);
	QCOMPARE(result.ambiguousArtifactsIgnored, 0);
	QCOMPARE(result.ambiguousGroups, 0);
	QVERIFY(result.errors.isEmpty());
	QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("orphan.dll.pending-") + token)));
	QFile restored(directory.filePath(QStringLiteral("restore.dll")));
	QVERIFY(restored.open(QIODevice::ReadOnly));
	QCOMPARE(restored.readAll(), QByteArray("old"));
	QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("lookalike.dll.pending-token"))));
	QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("trailing.dll.backup-") + token
												 + QStringLiteral(".extra"))));
	QVERIFY(QFileInfo::exists(
		directory.filePath(QStringLiteral("null.dll.pending-00000000-0000-0000-0000-000000000000"))));
	QVERIFY(QFileInfo::exists(
		directory.filePath(QStringLiteral("non-v4.dll.pending-10000000-0000-3000-8000-000000000001"))));
}

void TestPluginUpdatePreparation::ignoresAmbiguousTransactionsWithoutReplacingDestination() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const auto write = [&directory](const QString &name, const QByteArray &bytes) {
		QFile file(directory.filePath(name));
		return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
	};
	const QString firstToken  = QStringLiteral("11111111-2222-4333-8444-555555555555");
	const QString secondToken = QStringLiteral("aaaaaaaa-bbbb-4ccc-8ddd-eeeeeeeeeeee");
	QVERIFY(write(QStringLiteral("keep.dll"), QByteArray("known-good")));
	QVERIFY(write(QStringLiteral("keep.dll.backup-") + firstToken, QByteArray("old")));
	QVERIFY(write(QStringLiteral("missing.dll.backup-") + firstToken, QByteArray("older")));
	QVERIFY(write(QStringLiteral("missing.dll.backup-") + secondToken, QByteArray("newer")));

	const PluginTransactionRecoveryResult result = recoverPluginFileTransactions(directory.path());
	QCOMPARE(result.backupsRestored, 0);
	QCOMPARE(result.ambiguousGroups, 2);
	QCOMPARE(result.ambiguousArtifactsIgnored, 3);
	QCOMPARE(result.errors.size(), 2);
	QFile installed(directory.filePath(QStringLiteral("keep.dll")));
	QVERIFY(installed.open(QIODevice::ReadOnly));
	QCOMPARE(installed.readAll(), QByteArray("known-good"));
	QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("missing.dll"))));
	QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("keep.dll.backup-") + firstToken)));
	QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("missing.dll.backup-") + firstToken)));
	QVERIFY(QFileInfo::exists(directory.filePath(QStringLiteral("missing.dll.backup-") + secondToken)));
	const PluginTransactionRecoveryResult secondPass = recoverPluginFileTransactions(directory.path());
	QCOMPARE(secondPass.ambiguousGroups, 2);
	QCOMPARE(secondPass.ambiguousArtifactsIgnored, 3);
	QCOMPARE(secondPass.errors.size(), 2);
}

void TestPluginUpdatePreparation::isolatesRecoveryByDestination() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const auto write = [&directory](const QString &name, const QByteArray &bytes) {
		QFile file(directory.filePath(name));
		return file.open(QIODevice::WriteOnly) && file.write(bytes) == bytes.size();
	};
	const QString firstToken  = QStringLiteral("10000000-0000-4000-8000-000000000001");
	const QString secondToken = QStringLiteral("20000000-0000-4000-8000-000000000002");
	QVERIFY(write(QStringLiteral("ambiguous.dll.backup-") + firstToken, QByteArray("first")));
	QVERIFY(write(QStringLiteral("ambiguous.dll.pending-") + secondToken, QByteArray("pending")));
	QVERIFY(write(QStringLiteral("recover.dll.backup-") + firstToken, QByteArray("recover")));

	const PluginTransactionRecoveryResult result = recoverPluginFileTransactions(directory.path());
	QCOMPARE(result.backupsRestored, 1);
	QCOMPARE(result.ambiguousGroups, 1);
	QCOMPARE(result.ambiguousArtifactsIgnored, 2);
	QCOMPARE(result.errors.size(), 1);
	QFile recovered(directory.filePath(QStringLiteral("recover.dll")));
	QVERIFY(recovered.open(QIODevice::ReadOnly));
	QCOMPARE(recovered.readAll(), QByteArray("recover"));
	QVERIFY(!QFileInfo::exists(directory.filePath(QStringLiteral("ambiguous.dll"))));
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
	QCOMPARE(postBackupCancel.errorCode, QStringLiteral("cancelled"));
	QCOMPARE(postBackupCancel.destinationPath, QFileInfo(destinationPath).absoluteFilePath());
	QVERIFY(!postBackupCancel.backupPath.isEmpty());
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

void TestPluginUpdatePreparation::rejectsOverwriteAndHashMismatchWithoutMutation() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString sourcePath      = directory.filePath(QStringLiteral("source.dll"));
	const QString destinationPath = directory.filePath(QStringLiteral("plugin.dll"));
	const auto write = [](const QString &path, const QByteArray &content) {
		QFile file(path);
		return file.open(QIODevice::WriteOnly) && file.write(content) == content.size();
	};
	QVERIFY(write(sourcePath, QByteArray("new")));
	QVERIFY(write(destinationPath, QByteArray("old")));

	const PluginFileCommitResult overwriteDenied =
		commitPreparedPluginFile(sourcePath, destinationPath, false);
	QVERIFY(!overwriteDenied.success);
	QCOMPARE(overwriteDenied.errorCode, QStringLiteral("overwrite-required"));
	QFile destination(destinationPath);
	QVERIFY(destination.open(QIODevice::ReadOnly));
	QCOMPARE(destination.readAll(), QByteArray("old"));
	destination.close();

	const PluginFileCommitResult hashMismatch = commitPreparedPluginFile(
		sourcePath, destinationPath, true, nullptr, {}, QByteArray(32, '\x01'));
	QVERIFY(!hashMismatch.success);
	QCOMPARE(hashMismatch.errorCode, QStringLiteral("hash-mismatch"));
	QVERIFY(destination.open(QIODevice::ReadOnly));
	QCOMPARE(destination.readAll(), QByteArray("old"));
	QVERIFY(QDir(directory.path()).entryList({ QStringLiteral("*.pending-*") }, QDir::Files).isEmpty());
	QVERIFY(QDir(directory.path()).entryList({ QStringLiteral("*.backup-*") }, QDir::Files).isEmpty());
}

void TestPluginUpdatePreparation::validatesUpdateUrlPolicy() {
	QVERIFY(validatePluginUpdateUrl(QUrl(QStringLiteral("https://plugins.example/update.dll"))).accepted);
	QCOMPARE(validatePluginUpdateUrl(QUrl(QStringLiteral("http://plugins.example/update.dll"))).errorCode,
			 QStringLiteral("insecure-update-url"));
	QCOMPARE(validatePluginUpdateUrl(QUrl::fromLocalFile(QStringLiteral("C:/tmp/update.dll"))).errorCode,
			 QStringLiteral("unsupported-update-scheme"));
	QCOMPARE(validatePluginUpdateUrl(QUrl(QStringLiteral("ftp://plugins.example/update.dll"))).errorCode,
			 QStringLiteral("unsupported-update-scheme"));
	QCOMPARE(validatePluginUpdateUrl(QUrl(QStringLiteral("https://user:secret@plugins.example/update.dll"))).errorCode,
			 QStringLiteral("update-url-credentials"));
	QCOMPARE(validatePluginUpdateUrl(QUrl(QStringLiteral("http://cdn.example/update.dll")),
								  QUrl(QStringLiteral("https://plugins.example/update.dll")))
			 .errorCode,
			 QStringLiteral("insecure-redirect"));
	QVERIFY(validatePluginUpdateUrl(QUrl(QStringLiteral("https://cdn.example/update.dll")),
								 QUrl(QStringLiteral("https://plugins.example/update.dll")))
				.accepted);
	QCOMPARE(validatePluginUpdateUrl(QUrl(QStringLiteral("https://cdn.example/update.dll")),
								  QUrl(QStringLiteral("http://plugins.example/update.dll")))
				 .errorCode,
				 QStringLiteral("insecure-redirect"));
}

void TestPluginUpdatePreparation::bindsUpdateDestinationToInstalledPlugin() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString installedPath = directory.filePath(QStringLiteral("installed-plugin.dll"));
	QVERIFY(pluginUpdateDestinationMatchesInstalledPath(installedPath, installedPath));
	QVERIFY(pluginUpdateDestinationMatchesInstalledPath(
		directory.filePath(QStringLiteral("nested/../installed-plugin.dll")), installedPath));
	QVERIFY(!pluginUpdateDestinationMatchesInstalledPath(
		directory.filePath(QStringLiteral("replacement-plugin.dll")), installedPath));
	QVERIFY(!pluginUpdateDestinationMatchesInstalledPath({}, installedPath));
	QVERIFY(!pluginUpdateDestinationMatchesInstalledPath(installedPath, {}));
#ifdef Q_OS_WIN
	QVERIFY(pluginUpdateDestinationMatchesInstalledPath(installedPath.toUpper(), installedPath.toLower()));
#endif
}

void TestPluginUpdatePreparation::bindsRawLibraryUpdateToInstalledBasename() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString installedPath = directory.filePath(QStringLiteral("installed-plugin.dll"));
	const QString randomDownloadedPath =
		QDir::temp().filePath(QStringLiteral("mumble-plugin-update-a1b2c3.dll"));
	QVERIFY(QFileInfo(randomDownloadedPath).fileName() != QFileInfo(installedPath).fileName());
	QCOMPARE(boundPluginUpdateDestination(directory.path(), installedPath),
		QFileInfo(installedPath).absoluteFilePath());
	QVERIFY(boundPluginUpdateDestination(directory.path(),
		directory.filePath(QStringLiteral("nested/attacker-rename.dll"))).isEmpty());
	QVERIFY(boundPluginUpdateDestination(directory.path(), randomDownloadedPath).isEmpty());
#ifdef Q_OS_WIN
	QCOMPARE(boundPluginUpdateDestination(directory.path().toUpper(), installedPath.toLower()).toLower(),
		QFileInfo(installedPath).absoluteFilePath().toLower());
#endif
}

void TestPluginUpdatePreparation::discoversLibraryRestoredByRecovery() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString token = QStringLiteral("12345678-1234-4abc-8def-123456789abc");
	const QString backupPath = directory.filePath(QStringLiteral("recovered.dll.backup-") + token);
	QFile backup(backupPath);
	QVERIFY(backup.open(QIODevice::WriteOnly));
	QCOMPARE(backup.write("plugin"), 6);
	backup.close();
	QVERIFY(discoverPluginLibraryPaths({ directory.path() }).isEmpty());
	const PluginTransactionRecoveryResult recovery = recoverPluginFileTransactions(directory.path());
	QCOMPARE(recovery.backupsRestored, 1);
	QVERIFY(recovery.errors.isEmpty());
	const QString restoredPath = directory.filePath(QStringLiteral("recovered.dll"));
	QCOMPARE(discoverPluginLibraryPaths({ directory.path() }), QStringList { restoredPath });
}

void TestPluginUpdatePreparation::fingerprintsOverwriteIdentity() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("plugin.dll"));
	QFile file(path);
	QVERIFY(file.open(QIODevice::WriteOnly));
	QCOMPARE(file.write("first"), 5);
	file.close();
	bool firstOK = false;
	const QByteArray first = pluginFileSha256(path, &firstOK);
	QVERIFY(firstOK);
	QCOMPARE(first.size(), 32);
	QVERIFY(file.open(QIODevice::WriteOnly | QIODevice::Truncate));
	QCOMPARE(file.write("second"), 6);
	file.close();
	bool secondOK = false;
	const QByteArray second = pluginFileSha256(path, &secondOK);
	QVERIFY(secondOK);
	QVERIFY(first != second);
	bool missingOK = true;
	QVERIFY(pluginFileSha256(directory.filePath(QStringLiteral("missing.dll")), &missingOK).isEmpty());
	QVERIFY(!missingOK);
}

QTEST_GUILESS_MAIN(TestPluginUpdatePreparation)
#include "TestPluginUpdatePreparation.moc"
