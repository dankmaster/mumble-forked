#include "PluginAbiWorker.h"

#include <QtCore/QCoreApplication>
#include <QtCore/QFile>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QSemaphore>
#include <QtCore/QThread>
#include <QtTest/QtTest>

#include <atomic>

class TestPluginAbiWorker : public QObject {
	Q_OBJECT
private slots:
	void uiOriginatedWorkRunsOffThreadInFifoOrder();
	void latestStateDoesNotCrossBarrier();
	void runtimeFamiliesHaveIndependentBounds();
	void shutdownClosesAdmissionAndRunsFinalWorkLast();
	void managerDeepCopiesPointerPayloadsAndLeavesRealtimeAudioDirect();
};

void TestPluginAbiWorker::uiOriginatedWorkRunsOffThreadInFifoOrder() {
	PluginAbiWorker worker;
	QSemaphore blockerStarted;
	QSemaphore releaseBlocker;
	QMutex resultMutex;
	QVector< int > order;
	std::atomic_bool runtimeCalled { false };
	std::atomic_bool ranOnApplicationThread { true };

	QVERIFY(worker.enqueue([&]() {
		blockerStarted.release();
		releaseBlocker.acquire();
	}));
	QVERIFY(blockerStarted.tryAcquire(1, 2000));
	for (int value = 1; value <= 3; ++value) {
		QVERIFY(worker.enqueueRuntime([&, value]() {
			runtimeCalled.store(true);
			ranOnApplicationThread.store(QThread::currentThread() == QCoreApplication::instance()->thread());
			QMutexLocker lock(&resultMutex);
			order.push_back(value);
		}, PluginAbiWorker::RuntimeQueueClass::Key));
	}
	QVERIFY(!runtimeCalled.load());
	releaseBlocker.release();
	QVERIFY(worker.waitForDone(2000));
	QVERIFY(!ranOnApplicationThread.load());
	QCOMPARE(order, QVector< int >({ 1, 2, 3 }));
}

void TestPluginAbiWorker::latestStateDoesNotCrossBarrier() {
	PluginAbiWorker worker;
	QSemaphore blockerStarted;
	QSemaphore releaseBlocker;
	QMutex resultMutex;
	QStringList order;
	const auto record = [&](const QString &value) {
		QMutexLocker lock(&resultMutex);
		order.push_back(value);
	};

	QVERIFY(worker.enqueue([&]() {
		blockerStarted.release();
		releaseBlocker.acquire();
	}));
	QVERIFY(blockerStarted.tryAcquire(1, 2000));
	QVERIFY(worker.enqueueRuntime([&]() { record(QStringLiteral("before-1")); },
		PluginAbiWorker::RuntimeQueueClass::TalkState, QByteArrayLiteral("talk:7:42")));
	QVERIFY(worker.enqueueRuntime([&]() { record(QStringLiteral("before-2")); },
		PluginAbiWorker::RuntimeQueueClass::TalkState, QByteArrayLiteral("talk:7:42")));
	QVERIFY(worker.enqueue([&]() { record(QStringLiteral("barrier")); }));
	QVERIFY(worker.enqueueRuntime([&]() { record(QStringLiteral("after-1")); },
		PluginAbiWorker::RuntimeQueueClass::TalkState, QByteArrayLiteral("talk:7:42")));
	QVERIFY(worker.enqueueRuntime([&]() { record(QStringLiteral("after-2")); },
		PluginAbiWorker::RuntimeQueueClass::TalkState, QByteArrayLiteral("talk:7:42")));
	releaseBlocker.release();
	QVERIFY(worker.waitForDone(2000));
	QCOMPARE(order, QStringList({ QStringLiteral("before-2"), QStringLiteral("barrier"),
		QStringLiteral("after-2") }));
}

void TestPluginAbiWorker::runtimeFamiliesHaveIndependentBounds() {
	PluginAbiWorker worker;
	QSemaphore blockerStarted;
	QSemaphore releaseBlocker;
	std::atomic_int structuralCalls { 0 };

	QVERIFY(worker.enqueue([&]() {
		blockerStarted.release();
		releaseBlocker.acquire();
	}));
	QVERIFY(blockerStarted.tryAcquire(1, 2000));
	for (qsizetype i = 0; i < PluginAbiWorker::MaximumPendingKeyCalls; ++i) {
		QVERIFY(worker.enqueueRuntime([]() {}, PluginAbiWorker::RuntimeQueueClass::Key));
	}
	QVERIFY(!worker.enqueueRuntime([]() {}, PluginAbiWorker::RuntimeQueueClass::Key));

	for (qsizetype i = 0; i < PluginAbiWorker::MaximumPendingDataCalls; ++i) {
		QVERIFY(worker.enqueueRuntime([]() {}, PluginAbiWorker::RuntimeQueueClass::Data, {}, 1));
	}
	QVERIFY(!worker.enqueueRuntime([]() {}, PluginAbiWorker::RuntimeQueueClass::Data, {}, 1));

	// Structural notifications use the exact FIFO and remain admissible even when every noisy family is full.
	QVERIFY(worker.enqueue([&]() { ++structuralCalls; }));
	for (qsizetype i = 0; i < PluginAbiWorker::MaximumPendingRenameCalls; ++i) {
		QVERIFY(worker.enqueueRuntime([]() {}, PluginAbiWorker::RuntimeQueueClass::ChannelRename,
			QByteArrayLiteral("rename:") + QByteArray::number(static_cast< qlonglong >(i))));
	}
	for (qsizetype i = 0; i < PluginAbiWorker::MaximumPendingTalkStateCalls + 32; ++i) {
		QVERIFY(worker.enqueueRuntime([]() {}, PluginAbiWorker::RuntimeQueueClass::TalkState,
			QByteArrayLiteral("talk:") + QByteArray::number(static_cast< qlonglong >(i))));
	}
	releaseBlocker.release();
	QVERIFY(worker.waitForDone(5000));
	QCOMPARE(structuralCalls.load(), 1);
}

void TestPluginAbiWorker::shutdownClosesAdmissionAndRunsFinalWorkLast() {
	PluginAbiWorker worker;
	QSemaphore blockerStarted;
	QSemaphore releaseBlocker;
	QMutex resultMutex;
	QStringList order;
	const auto record = [&](const QString &value) {
		QMutexLocker lock(&resultMutex);
		order.push_back(value);
	};
	QVERIFY(worker.enqueue([&]() {
		blockerStarted.release();
		releaseBlocker.acquire();
	}));
	QVERIFY(blockerStarted.tryAcquire(1, 2000));
	QVERIFY(worker.enqueueRuntime([&]() { record(QStringLiteral("runtime")); },
		PluginAbiWorker::RuntimeQueueClass::TalkState, QByteArrayLiteral("talk:1:2")));
	QVERIFY(worker.enqueue([&]() { record(QStringLiteral("lifecycle")); }));
	QVERIFY(worker.shutdown([&]() { record(QStringLiteral("final")); }));
	QVERIFY(!worker.enqueue([]() {}));
	QVERIFY(!worker.enqueueRuntime([]() {}, PluginAbiWorker::RuntimeQueueClass::Key));
	releaseBlocker.release();
	QVERIFY(worker.waitForDone(2000));
	QCOMPARE(order, QStringList({ QStringLiteral("runtime"), QStringLiteral("lifecycle"),
		QStringLiteral("final") }));
}

void TestPluginAbiWorker::managerDeepCopiesPointerPayloadsAndLeavesRealtimeAudioDirect() {
	QFile source(QString::fromUtf8(PLUGIN_MANAGER_SOURCE_PATH));
	QVERIFY2(source.open(QFile::ReadOnly), qPrintable(source.errorString()));
	const QByteArray code = source.readAll();
	QVERIFY(code.contains("QByteArray payload(reinterpret_cast< const char * >(data)"));
	QVERIFY(code.contains("payload = std::move(payload)"));
	QVERIFY(code.contains("identifier = std::move(identifier)"));
	QVERIFY(code.contains("enqueuePluginRuntimeNotification([connectionID]"));
	QVERIFY(code.contains("PluginAbiWorker::RuntimeQueueClass::Key"));

	const qsizetype audioStart = code.indexOf("void PluginManager::on_audioInput");
	const qsizetype dataStart  = code.indexOf("void PluginManager::on_receiveData", audioStart);
	QVERIFY(audioStart >= 0 && dataStart > audioStart);
	const QByteArray audioCallbacks = code.mid(audioStart, dataStart - audioStart);
	QVERIFY(audioCallbacks.contains("foreachPlugin("));
	QVERIFY(!audioCallbacks.contains("enqueuePluginRuntimeNotification"));
}

QTEST_MAIN(TestPluginAbiWorker)
#include "TestPluginAbiWorker.moc"
