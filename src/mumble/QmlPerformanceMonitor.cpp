#include "QmlPerformanceMonitor.h"

#include "ChatPerfTrace.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <QEvent>
#include <QThread>

namespace {
constexpr qint64 HeartbeatIntervalMs = 16;
constexpr double StallThresholdMs = 50.0;
constexpr qsizetype MaximumSamples = 600;
constexpr qsizetype MaximumPendingInputs = 64;
constexpr std::array< const char *, 6 > StableModelNames {
	"room", "navigation", "participant", "chat", "operation", "action"
};
constexpr std::array< const char *, 3 > SyncUiOperationCategories { "network", "plugin", "file" };
}

QmlPerformanceMonitor::QmlPerformanceMonitor(QObject *parent) : QObject(parent) {
	m_clock.start();
	for (const char *modelName : StableModelNames) {
		m_modelResetCounts.insert(QString::fromLatin1(modelName), 0);
	}
	for (const char *category : SyncUiOperationCategories) {
		m_syncUiOperationViolationCounts.insert(QString::fromLatin1(category), 0);
	}
	m_heartbeat.setInterval(HeartbeatIntervalMs);
	m_heartbeat.setTimerType(Qt::PreciseTimer);
	connect(&m_heartbeat, &QTimer::timeout, this, [this]() { recordHeartbeatAt(nowNs()); });
	m_heartbeat.start();
}

double QmlPerformanceMonitor::p95FrameMs() const { return percentile(m_frameIntervalsMs, 0.95); }
double QmlPerformanceMonitor::p99FrameMs() const { return percentile(m_frameIntervalsMs, 0.99); }
double QmlPerformanceMonitor::p95RenderDurationMs() const { return percentile(m_renderDurationsMs, 0.95); }
double QmlPerformanceMonitor::p99RenderDurationMs() const { return percentile(m_renderDurationsMs, 0.99); }
bool QmlPerformanceMonitor::frameSampling() const { return m_frameSampling.load(); }
double QmlPerformanceMonitor::lastInputLatencyMs() const { return m_lastInputLatencyMs; }
double QmlPerformanceMonitor::maxInputLatencyMs() const { return m_maxInputLatencyMs; }
double QmlPerformanceMonitor::p95InputLatencyMs() const { return percentile(m_inputLatenciesMs, 0.95); }
double QmlPerformanceMonitor::p99InputLatencyMs() const { return percentile(m_inputLatenciesMs, 0.99); }
int QmlPerformanceMonitor::uiStallCount() const { return m_uiStallCount; }
double QmlPerformanceMonitor::maxUiStallMs() const { return m_maxUiStallMs; }

QVariantMap QmlPerformanceMonitor::snapshot() const {
	const bool hasFrameSamples = !m_frameIntervalsMs.isEmpty();
	const bool hasInputSamples = !m_inputLatenciesMs.isEmpty();
	QVariantMap modelResetCounts;
	int modelResetCount = 0;
	for (const char *modelName : StableModelNames) {
		const QString name = QString::fromLatin1(modelName);
		const int count = m_modelResetCounts.value(name);
		modelResetCounts.insert(name, count);
		modelResetCount += count;
	}
	QVariantMap syncUiOperationViolationCounts;
	int syncUiOperationViolationCount = 0;
	for (const char *category : SyncUiOperationCategories) {
		const QString name = QString::fromLatin1(category);
		const int count = m_syncUiOperationViolationCounts.value(name);
		syncUiOperationViolationCounts.insert(name, count);
		syncUiOperationViolationCount += count;
	}
	const bool noSyncUiOperationsPassed = syncUiOperationViolationCount == 0;
	const QVariantMap gates { { QStringLiteral("frameP95Passed"), hasFrameSamples && p95FrameMs() <= 16.7 },
						 { QStringLiteral("frameP99Passed"), hasFrameSamples && p99FrameMs() <= 33.3 },
						 { QStringLiteral("inputP95Passed"), hasInputSamples && p95InputLatencyMs() <= 50.0 },
						 { QStringLiteral("noUiStallsPassed"), m_uiStallCount == 0 },
						 { QStringLiteral("noModelResetsPassed"), modelResetCount == 0 },
						 { QStringLiteral("noSyncUiOperationsPassed"), noSyncUiOperationsPassed } };
	return { { QStringLiteral("frameSampling"), m_frameSampling.load() },
			 { QStringLiteral("frameSampleCount"), m_frameIntervalsMs.size() },
			 { QStringLiteral("presentedFrameCount"), m_presentedFrameCount },
			 { QStringLiteral("p95FrameMs"), p95FrameMs() }, { QStringLiteral("p99FrameMs"), p99FrameMs() },
			 { QStringLiteral("renderSampleCount"), m_renderDurationsMs.size() },
			 { QStringLiteral("p95RenderDurationMs"), p95RenderDurationMs() },
			 { QStringLiteral("p99RenderDurationMs"), p99RenderDurationMs() },
			 { QStringLiteral("inputSampleCount"), m_inputLatenciesMs.size() },
			 { QStringLiteral("lastInputLatencyMs"), m_lastInputLatencyMs },
			 { QStringLiteral("maxInputLatencyMs"), m_maxInputLatencyMs },
			 { QStringLiteral("p95InputLatencyMs"), p95InputLatencyMs() },
			 { QStringLiteral("p99InputLatencyMs"), p99InputLatencyMs() },
			 { QStringLiteral("uiStallCount"), m_uiStallCount },
			 { QStringLiteral("maxUiStallMs"), m_maxUiStallMs },
			 { QStringLiteral("modelResetCount"), modelResetCount },
			 { QStringLiteral("modelResetCounts"), modelResetCounts },
			 { QStringLiteral("syncUiOperationViolationCount"), syncUiOperationViolationCount },
			 { QStringLiteral("syncUiOperationViolationCounts"), syncUiOperationViolationCounts },
			 { QStringLiteral("noSyncUiOperationsPassed"), noSyncUiOperationsPassed },
			 { QStringLiteral("pendingInputCount"), m_pendingInputs.size() },
			 { QStringLiteral("thresholds"),
			   QVariantMap { { QStringLiteral("frameP95Ms"), 16.7 }, { QStringLiteral("frameP99Ms"), 33.3 },
						 { QStringLiteral("inputP95Ms"), 50.0 }, { QStringLiteral("uiStallMs"), 50.0 } } },
			 { QStringLiteral("gates"), gates } };
}

void QmlPerformanceMonitor::markFrameRenderingStarted() {
	if (!m_frameSampling.load()) return;
	m_frameRenderingStartedNs.store(nowNs());
}

void QmlPerformanceMonitor::markFrameRenderingFinished() {
	if (!m_frameSampling.load()) return;
	const qint64 startedNs = m_frameRenderingStartedNs.exchange(-1);
	const qint64 finishedNs = nowNs();
	if (startedNs < 0 || finishedNs <= startedNs) return;
	const double durationMs = (finishedNs - startedNs) / 1000000.0;
	QMetaObject::invokeMethod(this, [this, durationMs]() { recordFrameDuration(durationMs); }, Qt::QueuedConnection);
}

void QmlPerformanceMonitor::markFramePresented() {
	const qint64 timestampNs = nowNs();
	const qulonglong samplingGeneration = m_samplingGeneration.load();
	const bool sampledAtPresentation = m_frameSampling.load();
	if (QThread::currentThread() == thread()) {
		recordFramePresentedAt(timestampNs, sampledAtPresentation, samplingGeneration);
		return;
	}
	QMetaObject::invokeMethod(
		this,
		[this, timestampNs, sampledAtPresentation, samplingGeneration]() {
			recordFramePresentedAt(timestampNs, sampledAtPresentation, samplingGeneration);
		},
		Qt::QueuedConnection);
}

void QmlPerformanceMonitor::beginFrameSampling() {
	if (m_frameSampling.load()) return;
	m_samplingGeneration.fetch_add(1);
	m_frameSampling.store(true);
	m_frameRenderingStartedNs.store(-1);
	m_lastHeartbeatNs = -1;
	m_lastPresentedFrameNs = -1;
	emit frameSamplingChanged();
}

void QmlPerformanceMonitor::endFrameSampling() {
	if (!m_frameSampling.exchange(false)) return;
	m_frameRenderingStartedNs.store(-1);
	m_lastHeartbeatNs = -1;
	m_lastPresentedFrameNs = -1;
	emit frameSamplingChanged();
}

QString QmlPerformanceMonitor::markInput(const QString &operationId) {
	QString id = operationId.trimmed();
	if (id.isEmpty()) id = QStringLiteral("input:%1").arg(++m_generatedOperationId);
	recordInputAt(id, nowNs());
	return id;
}

void QmlPerformanceMonitor::markVisualComplete(const QString &operationId) {
	recordVisualAt(operationId, nowNs());
}

void QmlPerformanceMonitor::installInputObserver(QObject *target) {
	if (m_inputTarget == target) return;
	if (m_inputTarget) m_inputTarget->removeEventFilter(this);
	m_inputTarget = target;
	if (m_inputTarget) m_inputTarget->installEventFilter(this);
}

void QmlPerformanceMonitor::reset() {
	m_frameRenderingStartedNs.store(-1);
	const bool wasSampling = m_frameSampling.exchange(false);
	m_samplingGeneration.fetch_add(1);
	m_lastHeartbeatNs = -1;
	m_lastPresentedFrameNs = -1;
	m_frameIntervalsMs.clear();
	m_renderDurationsMs.clear();
	m_inputLatenciesMs.clear();
	for (auto it = m_modelResetCounts.begin(); it != m_modelResetCounts.end(); ++it) {
		it.value() = 0;
	}
	for (auto it = m_syncUiOperationViolationCounts.begin(); it != m_syncUiOperationViolationCounts.end(); ++it) {
		it.value() = 0;
	}
	m_pendingInputs.clear();
	m_pendingInputOrder.clear();
	m_lastInputLatencyMs = 0.0;
	m_maxInputLatencyMs = 0.0;
	m_uiStallCount = 0;
	m_presentedFrameCount = 0;
	m_maxUiStallMs = 0.0;
	if (wasSampling) emit frameSamplingChanged();
	emit metricsChanged();
}

void QmlPerformanceMonitor::recordFrameDuration(const double durationMs) {
	if (!m_frameSampling.load() || durationMs < 0.0) return;
	appendBounded(m_renderDurationsMs, durationMs);
	emit metricsChanged();
}

void QmlPerformanceMonitor::recordFramePresentedAt(const qint64 timestampNs) {
	recordFramePresentedAt(timestampNs, m_frameSampling.load(), m_samplingGeneration.load());
}

void QmlPerformanceMonitor::recordFramePresentedAt(const qint64 timestampNs, const bool sampledAtPresentation,
													 const qulonglong samplingGeneration) {
	if (samplingGeneration != m_samplingGeneration.load()) return;
	bool frameMetricsChanged = false;
	if (sampledAtPresentation) {
		++m_presentedFrameCount;
		frameMetricsChanged = true;
		if (m_lastPresentedFrameNs >= 0 && timestampNs > m_lastPresentedFrameNs) {
			appendBounded(m_frameIntervalsMs, (timestampNs - m_lastPresentedFrameNs) / 1000000.0);
		}
		if (timestampNs >= 0) m_lastPresentedFrameNs = timestampNs;
	}
	const QQueue< QString > pending = m_pendingInputOrder;
	for (const QString &operationId : pending) recordVisualAt(operationId, timestampNs);
	if (frameMetricsChanged) emit metricsChanged();
}

void QmlPerformanceMonitor::recordHeartbeatAt(const qint64 timestampNs) {
	if (!m_frameSampling.load()) {
		m_lastHeartbeatNs = -1;
		return;
	}
	if (m_lastHeartbeatNs >= 0 && timestampNs > m_lastHeartbeatNs) {
		const double intervalMs = (timestampNs - m_lastHeartbeatNs) / 1000000.0;
		if (intervalMs > StallThresholdMs) {
			mumble::chatperf::recordNote("qml.ui.stall",
				QStringLiteral("interval_ms=%1").arg(intervalMs, 0, 'f', 3));
			++m_uiStallCount;
			m_maxUiStallMs = std::max(m_maxUiStallMs, intervalMs);
			emit uiStallObserved(intervalMs);
			emit metricsChanged();
		}
	}
	m_lastHeartbeatNs = timestampNs;
}

void QmlPerformanceMonitor::recordInputAt(const QString &operationId, const qint64 timestampNs) {
	const QString id = operationId.trimmed();
	if (id.isEmpty()) return;
	m_pendingInputOrder.removeAll(id);
	m_pendingInputs.insert(id, timestampNs);
	m_pendingInputOrder.enqueue(id);
	while (m_pendingInputOrder.size() > MaximumPendingInputs) {
		m_pendingInputs.remove(m_pendingInputOrder.dequeue());
	}
}

void QmlPerformanceMonitor::recordVisualAt(const QString &operationId, const qint64 timestampNs) {
	const QString id = operationId.trimmed();
	const auto found = m_pendingInputs.constFind(id);
	if (found == m_pendingInputs.cend() || timestampNs < found.value()) return;
	const double latencyMs = (timestampNs - found.value()) / 1000000.0;
	m_pendingInputs.remove(id);
	m_pendingInputOrder.removeAll(id);
	appendBounded(m_inputLatenciesMs, latencyMs);
	m_lastInputLatencyMs = latencyMs;
	m_maxInputLatencyMs = std::max(m_maxInputLatencyMs, latencyMs);
	emit inputLatencyObserved(id, latencyMs);
	emit metricsChanged();
}

void QmlPerformanceMonitor::recordModelReset(const QString &modelName) {
	if (!m_frameSampling.load()) return;
	const QString normalized = modelName.trimmed().toLower();
	auto found = m_modelResetCounts.find(normalized);
	if (found == m_modelResetCounts.end()) return;
	++found.value();
	emit metricsChanged();
}

void QmlPerformanceMonitor::recordSyncUiOperationViolation(const QString &category) {
	if (!m_frameSampling.load()) return;
	const QString normalized = category.trimmed().toLower();
	auto found = m_syncUiOperationViolationCounts.find(normalized);
	if (found == m_syncUiOperationViolationCounts.end()) return;
	++found.value();
	emit metricsChanged();
}

double QmlPerformanceMonitor::percentile(const QVector< double > &values, const double fraction) {
	if (values.isEmpty()) return 0.0;
	QVector< double > sorted = values;
	std::sort(sorted.begin(), sorted.end());
	const qsizetype index = static_cast< qsizetype >(std::ceil(fraction * sorted.size())) - 1;
	return sorted.at(std::clamp< qsizetype >(index, 0, sorted.size() - 1));
}

void QmlPerformanceMonitor::appendBounded(QVector< double > &values, const double value) {
	if (values.size() == MaximumSamples) values.removeFirst();
	values.push_back(value);
}

qint64 QmlPerformanceMonitor::nowNs() const { return m_clock.nsecsElapsed(); }

bool QmlPerformanceMonitor::eventFilter(QObject *watched, QEvent *event) {
	if (watched == m_inputTarget && event) {
		switch (event->type()) {
			case QEvent::KeyPress:
			case QEvent::MouseButtonPress:
			case QEvent::TouchBegin:
			case QEvent::Wheel:
				recordInputAt(QStringLiteral("event:%1").arg(++m_generatedOperationId), nowNs());
				break;
			default: break;
		}
	}
	return QObject::eventFilter(watched, event);
}
