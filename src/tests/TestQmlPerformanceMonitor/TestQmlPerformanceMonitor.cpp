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
	void recordsRenderThreadFrameDuration();
	void computesInputPercentiles();
	void observesRealInputAndBoundsPendingQueue();
};

void TestQmlPerformanceMonitor::computesFramePercentiles() {
	QmlPerformanceMonitor monitor;
	monitor.beginFrameSampling();
	for (int sample = 1; sample <= 100; ++sample) {
		monitor.recordFrameDuration(sample);
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
	monitor.recordHeartbeatAt(66000000);
	QCOMPARE(stallSpy.count(), 0);
	monitor.recordHeartbeatAt(132000001);
	QCOMPARE(stallSpy.count(), 1);
	QCOMPARE(monitor.uiStallCount(), 1);
	QVERIFY(monitor.maxUiStallMs() > 50.0);
}

void TestQmlPerformanceMonitor::resetClearsAutomationSnapshot() {
	QmlPerformanceMonitor monitor;
	monitor.beginFrameSampling();
	monitor.recordFrameDuration(17.0);
	monitor.recordInputAt(QStringLiteral("send"), 0);
	monitor.recordVisualAt(QStringLiteral("send"), 20000000);
	monitor.recordHeartbeatAt(0);
	monitor.recordHeartbeatAt(80000000);
	QCOMPARE(monitor.uiStallCount(), 1);
	monitor.reset();
	const QVariantMap snapshot = monitor.snapshot();
	QCOMPARE(snapshot.value(QStringLiteral("frameSampleCount")).toInt(), 0);
	QCOMPARE(snapshot.value(QStringLiteral("inputSampleCount")).toInt(), 0);
	QCOMPARE(snapshot.value(QStringLiteral("uiStallCount")).toInt(), 0);
}

void TestQmlPerformanceMonitor::recordsRenderThreadFrameDuration() {
	QmlPerformanceMonitor monitor;
	monitor.beginFrameSampling();
	monitor.markFrameRenderingStarted();
	QTest::qSleep(2);
	monitor.markFrameRenderingFinished();
	QTRY_COMPARE(monitor.snapshot().value(QStringLiteral("frameSampleCount")).toInt(), 1);
	QVERIFY(monitor.p95FrameMs() >= 2.0);
}

void TestQmlPerformanceMonitor::computesInputPercentiles() {
	QmlPerformanceMonitor monitor;
	for (int sample = 1; sample <= 100; ++sample) {
		const QString id = QStringLiteral("input:%1").arg(sample);
		monitor.recordInputAt(id, 0);
		monitor.recordVisualAt(id, static_cast< qint64 >(sample) * 1000000);
	}
	QCOMPARE(monitor.p95InputLatencyMs(), 95.0);
	QCOMPARE(monitor.p99InputLatencyMs(), 99.0);
	const QVariantMap snapshot = monitor.snapshot();
	QCOMPARE(snapshot.value(QStringLiteral("inputSampleCount")).toInt(), 100);
	QVERIFY(!snapshot.value(QStringLiteral("gates")).toMap()
				 .value(QStringLiteral("inputP95Passed"))
				 .toBool());
}

void TestQmlPerformanceMonitor::observesRealInputAndBoundsPendingQueue() {
	QmlPerformanceMonitor monitor;
	QObject inputTarget;
	monitor.installInputObserver(&inputTarget);
	for (int input = 0; input < 70; ++input) {
		QEvent event(QEvent::KeyPress);
		QCoreApplication::sendEvent(&inputTarget, &event);
	}
	QCOMPARE(monitor.snapshot().value(QStringLiteral("pendingInputCount")).toInt(), 64);
	monitor.markFramePresented();
	const QVariantMap snapshot = monitor.snapshot();
	QCOMPARE(snapshot.value(QStringLiteral("pendingInputCount")).toInt(), 0);
	QCOMPARE(snapshot.value(QStringLiteral("inputSampleCount")).toInt(), 64);
}

QTEST_GUILESS_MAIN(TestQmlPerformanceMonitor)
#include "TestQmlPerformanceMonitor.moc"
