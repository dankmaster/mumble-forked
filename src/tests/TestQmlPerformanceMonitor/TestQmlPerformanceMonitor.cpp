#include "QmlPerformanceMonitor.h"

#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

class TestQmlPerformanceMonitor : public QObject {
	Q_OBJECT

private slots:
	void computesFramePercentiles();
	void correlatesInputWithVisualCompletion();
	void reportsOnlyStallsAboveThreshold();
	void ignoresHeartbeatOutsideMeasuredPhase();
	void countsPresentedFramesOnlyInsideMeasuredPhase();
	void resetClearsAutomationSnapshot();
	void recordsRenderThreadFrameDuration();
	void computesInputPercentiles();
	void observesRealInputAndBoundsPendingQueue();
	void countsStableModelResetsOnlyInsideMeasuredPhase();
	void countsSyncUiOperationViolationsFailClosed();
};

void TestQmlPerformanceMonitor::computesFramePercentiles() {
	QmlPerformanceMonitor monitor;
	monitor.beginFrameSampling();
	qint64 presentedAtNs = 0;
	monitor.recordFramePresentedAt(presentedAtNs);
	for (int sample = 1; sample <= 100; ++sample) {
		presentedAtNs += static_cast< qint64 >(sample) * 1000000;
		monitor.recordFramePresentedAt(presentedAtNs);
	}
	QCOMPARE(monitor.snapshot().value(QStringLiteral("frameSampleCount")).toInt(), 100);
	QCOMPARE(monitor.snapshot().value(QStringLiteral("presentedFrameCount")).toInt(), 101);
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
	monitor.beginFrameSampling();
	monitor.recordHeartbeatAt(0);
	monitor.recordHeartbeatAt(50000000);
	QCOMPARE(stallSpy.count(), 0);
	monitor.recordHeartbeatAt(101000000);
	QCOMPARE(stallSpy.count(), 1);
	QCOMPARE(monitor.uiStallCount(), 1);
	QCOMPARE(monitor.maxUiStallMs(), 51.0);
}

void TestQmlPerformanceMonitor::ignoresHeartbeatOutsideMeasuredPhase() {
	QmlPerformanceMonitor monitor;
	monitor.recordHeartbeatAt(0);
	monitor.recordHeartbeatAt(500000000);
	QCOMPARE(monitor.uiStallCount(), 0);
	monitor.beginFrameSampling();
	monitor.recordHeartbeatAt(500000000);
	monitor.recordHeartbeatAt(551000000);
	QCOMPARE(monitor.uiStallCount(), 1);
	monitor.endFrameSampling();
	monitor.recordHeartbeatAt(1000000000);
	QCOMPARE(monitor.uiStallCount(), 1);
}

void TestQmlPerformanceMonitor::countsPresentedFramesOnlyInsideMeasuredPhase() {
	QmlPerformanceMonitor monitor;
	monitor.markFramePresented();
	QCOMPARE(monitor.snapshot().value(QStringLiteral("presentedFrameCount")).toInt(), 0);
	monitor.beginFrameSampling();
	monitor.markFramePresented();
	QCOMPARE(monitor.snapshot().value(QStringLiteral("presentedFrameCount")).toInt(), 1);
	monitor.endFrameSampling();
	monitor.markFramePresented();
	QCOMPARE(monitor.snapshot().value(QStringLiteral("presentedFrameCount")).toInt(), 1);
	monitor.reset();
	QCOMPARE(monitor.snapshot().value(QStringLiteral("presentedFrameCount")).toInt(), 0);
}

void TestQmlPerformanceMonitor::resetClearsAutomationSnapshot() {
	QmlPerformanceMonitor monitor;
	monitor.beginFrameSampling();
	monitor.recordFrameDuration(17.0);
	monitor.recordFramePresentedAt(0);
	monitor.recordFramePresentedAt(17000000);
	monitor.recordInputAt(QStringLiteral("send"), 0);
	monitor.recordVisualAt(QStringLiteral("send"), 20000000);
	monitor.recordHeartbeatAt(0);
	monitor.recordHeartbeatAt(80000000);
	monitor.recordModelReset(QStringLiteral("chat"));
	QCOMPARE(monitor.uiStallCount(), 1);
	monitor.reset();
	const QVariantMap snapshot = monitor.snapshot();
	QCOMPARE(snapshot.value(QStringLiteral("frameSampleCount")).toInt(), 0);
	QCOMPARE(snapshot.value(QStringLiteral("renderSampleCount")).toInt(), 0);
	QCOMPARE(snapshot.value(QStringLiteral("inputSampleCount")).toInt(), 0);
	QCOMPARE(snapshot.value(QStringLiteral("uiStallCount")).toInt(), 0);
	QCOMPARE(snapshot.value(QStringLiteral("modelResetCount")).toInt(), 0);
}

void TestQmlPerformanceMonitor::recordsRenderThreadFrameDuration() {
	QmlPerformanceMonitor monitor;
	monitor.beginFrameSampling();
	monitor.markFrameRenderingStarted();
	QTest::qSleep(2);
	monitor.markFrameRenderingFinished();
	QTRY_COMPARE(monitor.snapshot().value(QStringLiteral("renderSampleCount")).toInt(), 1);
	QCOMPARE(monitor.snapshot().value(QStringLiteral("frameSampleCount")).toInt(), 0);
	QVERIFY(monitor.p95RenderDurationMs() >= 2.0);
	QCOMPARE(monitor.p95FrameMs(), 0.0);
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

void TestQmlPerformanceMonitor::countsStableModelResetsOnlyInsideMeasuredPhase() {
	QmlPerformanceMonitor monitor;
	monitor.recordModelReset(QStringLiteral("chat"));
	QCOMPARE(monitor.snapshot().value(QStringLiteral("modelResetCount")).toInt(), 0);

	monitor.beginFrameSampling();
	monitor.recordModelReset(QStringLiteral("chat"));
	monitor.recordModelReset(QStringLiteral("chat"));
	monitor.recordModelReset(QStringLiteral("room"));
	monitor.recordModelReset(QStringLiteral("not-a-stable-model"));
	monitor.endFrameSampling();
	monitor.recordModelReset(QStringLiteral("participant"));

	const QVariantMap snapshot = monitor.snapshot();
	const QVariantMap counts = snapshot.value(QStringLiteral("modelResetCounts")).toMap();
	QCOMPARE(snapshot.value(QStringLiteral("modelResetCount")).toInt(), 3);
	QCOMPARE(counts.value(QStringLiteral("chat")).toInt(), 2);
	QCOMPARE(counts.value(QStringLiteral("room")).toInt(), 1);
	QCOMPARE(counts.value(QStringLiteral("participant")).toInt(), 0);
	QVERIFY(!counts.contains(QStringLiteral("not-a-stable-model")));
	QVERIFY(!snapshot.value(QStringLiteral("gates")).toMap()
				 .value(QStringLiteral("noModelResetsPassed"))
				 .toBool());

	monitor.reset();
	const QVariantMap resetCounts = monitor.snapshot().value(QStringLiteral("modelResetCounts")).toMap();
	for (auto it = resetCounts.cbegin(); it != resetCounts.cend(); ++it) {
		QCOMPARE(it.value().toInt(), 0);
	}
}

void TestQmlPerformanceMonitor::countsSyncUiOperationViolationsFailClosed() {
	QmlPerformanceMonitor monitor;
	QVariantMap snapshot = monitor.snapshot();
	QVariantMap counts = snapshot.value(QStringLiteral("syncUiOperationViolationCounts")).toMap();
	QCOMPARE(counts.value(QStringLiteral("network")).toInt(), 0);
	QCOMPARE(counts.value(QStringLiteral("plugin")).toInt(), 0);
	QCOMPARE(counts.value(QStringLiteral("file")).toInt(), 0);
	QVERIFY(snapshot.value(QStringLiteral("noSyncUiOperationsPassed")).toBool());

	monitor.recordSyncUiOperationViolation(QStringLiteral("file"));
	QCOMPARE(monitor.snapshot().value(QStringLiteral("syncUiOperationViolationCount")).toInt(), 0);

	monitor.beginFrameSampling();
	monitor.recordSyncUiOperationViolation(QStringLiteral("network"));
	monitor.recordSyncUiOperationViolation(QStringLiteral("plugin"));
	monitor.recordSyncUiOperationViolation(QStringLiteral("file"));
	monitor.recordSyncUiOperationViolation(QStringLiteral("unknown"));
	monitor.endFrameSampling();

	snapshot = monitor.snapshot();
	counts = snapshot.value(QStringLiteral("syncUiOperationViolationCounts")).toMap();
	QCOMPARE(snapshot.value(QStringLiteral("syncUiOperationViolationCount")).toInt(), 3);
	QCOMPARE(counts.value(QStringLiteral("network")).toInt(), 1);
	QCOMPARE(counts.value(QStringLiteral("plugin")).toInt(), 1);
	QCOMPARE(counts.value(QStringLiteral("file")).toInt(), 1);
	QVERIFY(!counts.contains(QStringLiteral("unknown")));
	QVERIFY(!snapshot.value(QStringLiteral("noSyncUiOperationsPassed")).toBool());
	QVERIFY(!snapshot.value(QStringLiteral("gates")).toMap()
				 .value(QStringLiteral("noSyncUiOperationsPassed"))
				 .toBool());

	monitor.reset();
	QVERIFY(monitor.snapshot().value(QStringLiteral("noSyncUiOperationsPassed")).toBool());
}

QTEST_GUILESS_MAIN(TestQmlPerformanceMonitor)
#include "TestQmlPerformanceMonitor.moc"
