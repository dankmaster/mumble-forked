#include "QmlPerformanceMonitor.h"

#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

class TestQmlPerformanceMonitor : public QObject {
	Q_OBJECT

private slots:
	void computesFramePercentiles();
	void correlatesInputWithVisualCompletion();
	void reportsOnlyStallsAboveThreshold();
	void resetClearsAutomationSnapshot();
	void acceptsQueuedFramePresentationFromHost();
};

void TestQmlPerformanceMonitor::computesFramePercentiles() {
	QmlPerformanceMonitor monitor;
	monitor.beginFrameSampling();
	qint64 timestampNs = 0;
	monitor.recordFrameAt(timestampNs);
	for (int sample = 1; sample <= 100; ++sample) {
		timestampNs += static_cast< qint64 >(sample) * 1000000;
		monitor.recordFrameAt(timestampNs);
	}
	QCOMPARE(monitor.snapshot().value(QStringLiteral("frameSampleCount")).toInt(), 100);
	QCOMPARE(monitor.p95FrameMs(), 95.0);
	QCOMPARE(monitor.p99FrameMs(), 99.0);
}

void TestQmlPerformanceMonitor::correlatesInputWithVisualCompletion() {
	QmlPerformanceMonitor monitor;
	QSignalSpy latencySpy(&monitor, &QmlPerformanceMonitor::inputLatencyObserved);
	monitor.recordInputAt(QStringLiteral("room:42"), 1000000);
	monitor.recordVisualAt(QStringLiteral("unknown"), 4000000);
	QCOMPARE(latencySpy.count(), 0);
	monitor.recordVisualAt(QStringLiteral("room:42"), 26000000);
	QCOMPARE(monitor.lastInputLatencyMs(), 25.0);
	QCOMPARE(monitor.maxInputLatencyMs(), 25.0);
	QCOMPARE(latencySpy.takeFirst().at(0).toString(), QStringLiteral("room:42"));
	QCOMPARE(monitor.snapshot().value(QStringLiteral("pendingInputCount")).toInt(), 0);
}

void TestQmlPerformanceMonitor::reportsOnlyStallsAboveThreshold() {
	QmlPerformanceMonitor monitor;
	QSignalSpy stallSpy(&monitor, &QmlPerformanceMonitor::uiStallObserved);
	monitor.recordHeartbeatAt(0);
	monitor.recordHeartbeatAt(50000000);
	QCOMPARE(stallSpy.count(), 0);
	monitor.recordHeartbeatAt(100000001);
	QCOMPARE(stallSpy.count(), 1);
	QCOMPARE(monitor.uiStallCount(), 1);
	QVERIFY(monitor.maxUiStallMs() > 50.0);
}

void TestQmlPerformanceMonitor::resetClearsAutomationSnapshot() {
	QmlPerformanceMonitor monitor;
	monitor.recordFrameAt(0);
	monitor.recordFrameAt(17000000);
	monitor.recordInputAt(QStringLiteral("send"), 0);
	monitor.recordVisualAt(QStringLiteral("send"), 20000000);
	monitor.recordHeartbeatAt(0);
	monitor.recordHeartbeatAt(60000000);
	monitor.reset();
	const QVariantMap snapshot = monitor.snapshot();
	QCOMPARE(snapshot.value(QStringLiteral("frameSampleCount")).toInt(), 0);
	QCOMPARE(snapshot.value(QStringLiteral("inputSampleCount")).toInt(), 0);
	QCOMPARE(snapshot.value(QStringLiteral("uiStallCount")).toInt(), 0);
}

void TestQmlPerformanceMonitor::acceptsQueuedFramePresentationFromHost() {
	QmlPerformanceMonitor monitor;
	monitor.beginFrameSampling();
	QVERIFY(QMetaObject::invokeMethod(&monitor, "markFramePresented", Qt::QueuedConnection));
	QVERIFY(QMetaObject::invokeMethod(&monitor, "markFramePresented", Qt::QueuedConnection));
	QTRY_COMPARE(monitor.snapshot().value(QStringLiteral("frameSampleCount")).toInt(), 1);
}

QTEST_GUILESS_MAIN(TestQmlPerformanceMonitor)
#include "TestQmlPerformanceMonitor.moc"
