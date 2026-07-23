// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "DBWrapper.h"
#include "Meta.h"
#include "MumbleConstants.h"
#include "ServerLogRedaction.h"
#include "database/SQLiteConnectionParameter.h"

#include <QFile>
#include <QTemporaryDir>
#include <QtTest>

#include <memory>

class TestMurmurCredentialHandling : public QObject {
	Q_OBJECT

private slots:
	void generatedSuperUserPasswordIsNeverPersistedInLogs();
};

void TestMurmurCredentialHandling::generatedSuperUserPasswordIsNeverPersistedInLogs() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());

	struct MetaParamsScope {
		MetaParamsScope() {
			Meta::mp                     = std::make_unique< MetaParams >();
			Meta::mp->legacyPasswordHash = false;
			Meta::mp->kdfIterations      = 1000;
		}

		~MetaParamsScope() { Meta::mp.reset(); }
	} metaParamsScope;
	Q_UNUSED(metaParamsScope);

	const mumble::db::SQLiteConnectionParameter connection(
		directory.filePath(QStringLiteral("credentials.sqlite")).toStdString(), false);
	{
		DBWrapper wrapper(connection);
		const unsigned int serverID                                     = wrapper.addServer();
		const std::vector< mumble::server::db::DBLogEntry > initialLogs = wrapper.getLogs(serverID);

		QCOMPARE(initialLogs.size(), static_cast< std::size_t >(1));
		QCOMPARE(QString::fromStdString(initialLogs.front().message),
				 Mumble::ServerLog::superUserBootstrapNotice(serverID));

		const std::string sentinel = "explicit-password-must-not-be-logged";
		wrapper.setSuperUserPassword(serverID, sentinel);
		const std::vector< mumble::server::db::DBLogEntry > updatedLogs = wrapper.getLogs(serverID);
		QCOMPARE(updatedLogs.size(), initialLogs.size());
		for (const mumble::server::db::DBLogEntry &entry : updatedLogs) {
			QVERIFY(!QString::fromStdString(entry.message).contains(QString::fromStdString(sentinel)));
		}
	}

	QFile databaseFile(directory.filePath(QStringLiteral("credentials.sqlite")));
	QVERIFY(databaseFile.open(QIODevice::ReadOnly));
	const QByteArray databaseBytes = databaseFile.readAll();
	QVERIFY(!databaseBytes.contains("Initialized 'SuperUser' password on server"));
	QVERIFY(!databaseBytes.contains("explicit-password-must-not-be-logged"));
	QVERIFY(databaseBytes.contains("generated password omitted"));
}

QTEST_GUILESS_MAIN(TestMurmurCredentialHandling)
#include "TestMurmurCredentialHandling.moc"
