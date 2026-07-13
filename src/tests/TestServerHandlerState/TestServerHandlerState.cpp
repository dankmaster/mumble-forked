// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "Connection.h"
#include "Database.h"
#include "Global.h"
#include "ServerHandler.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryDir>
#include <QtCore/QUuid>
#include <QtNetwork/QSslSocket>
#include <QtSql/QSqlDatabase>
#include <QtTest>

#include <atomic>
#include <cmath>
#include <memory>
#include <thread>

class TestServerHandlerState : public QObject {
	Q_OBJECT

private slots:
	void initTestCase();
	void cleanupTestCase();
	void publishesProtocolAndRecorderStateSafely();
	void publishesConnectionAndTlsSnapshotsSafely();
	void publishesAtomicConnectionStateTransitions();
	void publishesPingAndCryptStatsSafely();
	void publishesIdentityDetailsSafely();
	void invalidWorkerDatabaseDoesNotFallbackAndRemovesConnection();

private:
	std::unique_ptr< QTemporaryDir > m_temporaryDirectory;
};

void TestServerHandlerState::initTestCase() {
	m_temporaryDirectory = std::make_unique< QTemporaryDir >();
	QVERIFY(m_temporaryDirectory->isValid());
	QVERIFY(!Global::g_global_struct);
	Global::g_global_struct = new Global(m_temporaryDirectory->filePath(QStringLiteral("settings.json")));
}

void TestServerHandlerState::cleanupTestCase() {
	delete Global::g_global_struct;
	Global::g_global_struct = nullptr;
}

void TestServerHandlerState::publishesProtocolAndRecorderStateSafely() {
	ServerHandler handler;
	QCOMPARE(handler.protocolVersion(), Version::UNKNOWN);

	const Version::full_t version = Version::fromComponents(1, 6, 42);
	handler.setProtocolVersion(version);
	QCOMPARE(handler.protocolVersion(), version);

	const std::shared_ptr< int > owner = std::make_shared< int >(42);
	VoiceRecorder *const recorderToken = reinterpret_cast< VoiceRecorder * >(owner.get());
	const std::shared_ptr< VoiceRecorder > recorder(owner, recorderToken);
	handler.setVoiceRecorder(recorder);
	QCOMPARE(handler.voiceRecorder().get(), recorderToken);
	QVERIFY(!handler.clearVoiceRecorder(reinterpret_cast< VoiceRecorder * >(quintptr(1))));
	QVERIFY(handler.clearVoiceRecorder(recorderToken));
	QVERIFY(!handler.voiceRecorder());
}

void TestServerHandlerState::publishesConnectionAndTlsSnapshotsSafely() {
	ServerHandler handler;

	const std::shared_ptr< int > owner = std::make_shared< int >(7);
	Connection *const connectionToken = reinterpret_cast< Connection * >(owner.get());
	const ConnectionPtr connection(owner, connectionToken);
	handler.publishConnection(connection);
	QCOMPARE(handler.connectionSnapshot().get(), connectionToken);

	const QSslCertificate certificate;
	const QSslError verificationError(QSslError::HostNameMismatch, certificate);
	handler.setTlsVerificationDetails({ certificate }, { verificationError });
	handler.setTlsSessionDetails({ certificate }, QSslCipher(), QSsl::TlsV1_3, true);

	const ServerTlsDetails tlsDetails = handler.tlsDetailsSnapshot();
	QCOMPARE(tlsDetails.certificates.size(), 1);
	QCOMPARE(tlsDetails.errors.size(), 1);
	QCOMPARE(tlsDetails.errors.first().error(), QSslError::HostNameMismatch);
	QCOMPARE(tlsDetails.protocol, QSsl::TlsV1_3);
	QVERIFY(tlsDetails.perfectForwardSecrecy);

	std::atomic_bool invalidSnapshot = false;
	std::thread reader([&]() {
		for (int i = 0; i < 2000; ++i) {
			const ConnectionPtr currentConnection = handler.connectionSnapshot();
			if (currentConnection && currentConnection.get() != connectionToken) {
				invalidSnapshot.store(true, std::memory_order_relaxed);
			}
			const ServerTlsDetails currentTlsDetails = handler.tlsDetailsSnapshot();
			if (currentTlsDetails.certificates.size() > 1 || currentTlsDetails.errors.size() > 1) {
				invalidSnapshot.store(true, std::memory_order_relaxed);
			}
		}
	});
	for (int i = 0; i < 2000; ++i) {
		if ((i % 2) == 0) {
			handler.publishConnection(connection);
			handler.setTlsVerificationDetails({ certificate }, { verificationError });
			handler.setTlsSessionDetails({ certificate }, QSslCipher(), QSsl::TlsV1_3, true);
		} else {
			handler.takeConnection().reset();
			handler.clearTlsDetails();
		}
	}
	reader.join();
	QVERIFY(!invalidSnapshot.load(std::memory_order_relaxed));

	// A retained caller snapshot remains valid after the handler atomically
	// unpublishes its connection during stopped-thread teardown.
	handler.publishConnection(connection);
	handler.setTlsVerificationDetails({ certificate }, { verificationError });
	const ConnectionPtr retainedConnection = handler.connectionSnapshot();
	handler.finalizeThreadResources();
	QVERIFY(!handler.connectionSnapshot());
	QCOMPARE(retainedConnection.get(), connectionToken);
	QCOMPARE(handler.tlsDetailsSnapshot().errors.size(), 1);
}

void TestServerHandlerState::publishesAtomicConnectionStateTransitions() {
	ServerHandler handler;
	QCOMPARE(handler.stateSnapshot(), ServerHandlerState::Idle);
	QVERIFY(handler.isUdpEnabled());
	QVERIFY(!handler.isStrong());
	QVERIFY(!handler.hasSynchronized());

	handler.setUdpEnabled(false);
	handler.setStrongConnection(true);
	handler.setServerSynchronized(true);
	QVERIFY(!handler.isUdpEnabled());
	QVERIFY(handler.isStrong());
	QVERIFY(handler.hasSynchronized());

	handler.disconnect();
	QCOMPARE(handler.stateSnapshot(), ServerHandlerState::Aborted);
	handler.changeState(ServerHandlerState::ConnectionEstablished);
	QCOMPARE(handler.stateSnapshot(), ServerHandlerState::Aborted);

	handler.refreshStartConfiguration();
	QCOMPARE(handler.stateSnapshot(), ServerHandlerState::Idle);
	handler.setServerSynchronized(false);
	QVERIFY(!handler.hasSynchronized());
}

void TestServerHandlerState::publishesPingAndCryptStatsSafely() {
	ServerHandler handler;
	handler.recordTcpPing(10.0);
	handler.recordTcpPing(20.0);
	handler.recordUdpPing(5.0);

	ServerPingStats pingStats = handler.pingStatsSnapshot();
	QCOMPARE(pingStats.tcp.sampleCount, quint64(2));
	QCOMPARE(pingStats.tcp.meanMs, 15.0);
	QCOMPARE(pingStats.tcp.varianceMs2, 25.0);
	QCOMPARE(pingStats.udp.sampleCount, quint64(1));
	QCOMPARE(pingStats.udp.meanMs, 5.0);

	std::atomic_bool invalidPingSnapshot = false;
	std::thread pingReader([&]() {
		for (int i = 0; i < 2000; ++i) {
			const ServerPingStats current = handler.pingStatsSnapshot();
			if (current.tcp.sampleCount > 2002 || !std::isfinite(current.tcp.meanMs)
				|| !std::isfinite(current.tcp.varianceMs2)) {
				invalidPingSnapshot.store(true, std::memory_order_relaxed);
			}
		}
	});
	for (int i = 0; i < 2000; ++i) {
		handler.recordTcpPing(static_cast< double >(i % 50));
	}
	pingReader.join();
	QVERIFY(!invalidPingSnapshot.load(std::memory_order_relaxed));

	ConnectionPtr connection(new Connection(nullptr, new QSslSocket));
	{
		QMutexLocker cryptLock(&handler.qmUdp);
		connection->csCrypt->m_statsLocal  = { 100, 2, 3, 4 };
		connection->csCrypt->m_statsRemote = { 90, 5, 6, 7 };
	}
	handler.publishConnection(connection);
	const ServerCryptStats cryptStats = handler.cryptStatsSnapshot();
	QVERIFY(cryptStats.available);
	QCOMPARE(cryptStats.local.good, 100U);
	QCOMPARE(cryptStats.local.lost, 3U);
	QCOMPARE(cryptStats.remote.good, 90U);
	QCOMPARE(cryptStats.remote.resync, 7U);

	handler.takeConnection().reset();
	connection.reset();
	QVERIFY(!handler.cryptStatsSnapshot().available);
	handler.resetPingStats();
	pingStats = handler.pingStatsSnapshot();
	QCOMPARE(pingStats.tcp.sampleCount, quint64(0));
	QCOMPARE(pingStats.udp.sampleCount, quint64(0));
}

void TestServerHandlerState::publishesIdentityDetailsSafely() {
	ServerHandler handler;
	handler.setServerIdentityDetails(QStringLiteral("1.6.0"), QStringLiteral("Windows"),
									 QStringLiteral("11"));
	ServerIdentityDetails identity = handler.identityDetailsSnapshot();
	QCOMPARE(identity.release, QStringLiteral("1.6.0"));
	QCOMPARE(identity.os, QStringLiteral("Windows"));
	QCOMPARE(identity.osVersion, QStringLiteral("11"));

	std::atomic_bool invalidSnapshot = false;
	std::thread reader([&]() {
		for (int i = 0; i < 2000; ++i) {
			const ServerIdentityDetails current = handler.identityDetailsSnapshot();
			const bool empty = current.release.isEmpty() && current.os.isEmpty() && current.osVersion.isEmpty();
			const bool complete = current.release == QStringLiteral("1.6.0") && current.os == QStringLiteral("Windows")
				&& current.osVersion == QStringLiteral("11");
			if (!empty && !complete) {
				invalidSnapshot.store(true, std::memory_order_relaxed);
			}
		}
	});
	for (int i = 0; i < 2000; ++i) {
		if ((i % 2) == 0) {
			handler.setServerIdentityDetails(QStringLiteral("1.6.0"), QStringLiteral("Windows"),
										 QStringLiteral("11"));
		} else {
			handler.clearIdentityDetails();
		}
	}
	reader.join();
	QVERIFY(!invalidSnapshot.load(std::memory_order_relaxed));

	handler.clearIdentityDetails();
	identity = handler.identityDetailsSnapshot();
	QVERIFY(identity.release.isEmpty());
	QVERIFY(identity.os.isEmpty());
	QVERIFY(identity.osVersion.isEmpty());
}

void TestServerHandlerState::invalidWorkerDatabaseDoesNotFallbackAndRemovesConnection() {
	const QString connectionName = QStringLiteral("ServerHandlerTest-%1")
		.arg(QUuid::createUuid().toString(QUuid::WithoutBraces));
	const QString missingPath = m_temporaryDirectory->filePath(QStringLiteral("missing/mumble.sqlite"));
	{
		Database database(connectionName, missingPath, false);
		QVERIFY(!database.isValid());
		QVERIFY(!database.initializationError().isEmpty());
		QVERIFY(QSqlDatabase::contains(connectionName));
	}
	QVERIFY(!QSqlDatabase::contains(connectionName));
	QVERIFY(!QFileInfo::exists(missingPath));
}

QTEST_GUILESS_MAIN(TestServerHandlerState)

#include "TestServerHandlerState.moc"
