#include "QmlPerformanceMonitor.h"

#include <algorithm>
#include <cmath>
#include <QEvent>

namespace {
constexpr qint64 HeartbeatIntervalMs = 16;
constexpr double StallThresholdMs = 50.0;
constexpr qsizetype MaximumSamples = 600;
constexpr qsizetype MaximumPendingInputs = 64;
}

QmlPerformanceMonitor::QmlPerformanceMonitor(QObject *parent) : QObject(parent) {
	m_clock.start();
	m_heartbeat.setInterval(HeartbeatIntervalMs);
	m_heartbeat.setTimerType(Qt::PreciseTimer);
	connect(&m_heartbeat, &QTimer::timeout, this, [this]() { recordHeartbeatAt(nowNs()); });
	m_heartbeat.start();
}

double QmlPerformanceMonitor::p95FrameMs() const { return percentile(m_frameDurationsMs, 0.95); }
double QmlPerformanceMonitor::p99FrameMs() const { return percentile(m_frameDurationsMs, 0.99); }
bool QmlPerformanceMonitor::frameSampling() const { return m_frameSampling.load(); }
double QmlPerformanceMonitor::lastInputLatencyMs() const { return m_lastInputLatencyMs; }
double QmlPerformanceMonitor::maxInputLatencyMs() const { return m_maxInputLatencyMs; }
double QmlPerformanceMonitor::p95InputLatencyMs() const { return percentile(m_inputLatenciesMs, 0.95); }
double QmlPerformanceMonitor::p99InputLatencyMs() const { return percentile(m_inputLatenciesMs, 0.99); }
int QmlPerformanceMonitor::uiStallCount() const { return m_uiStallCount; }
double QmlPerformanceMonitor::maxUiStallMs() const { return m_maxUiStallMs; }

QVariantMap QmlPerformanceMonitor::snapshot() const {
	const bool hasFrameSamples = !m_frameDurationsMs.isEmpty();
	const bool hasInputSamples = !m_inputLatenciesMs.isEmpty();
	const QVariantMap gates { { QStringLiteral("frameP95Passed"), hasFrameSamples && p95FrameMs() <= 16.7 },
						 { QStringLiteral("frameP99Passed"), hasFrameSamples && p99FrameMs() <= 33.3 },
						 { QStringLiteral("inputP95Passed"), hasInputSamples && p95InputLatencyMs() <= 50.0 },
						 { QStringLiteral("noUiStallsPassed"), m_uiStallCount == 0 } };
	return { { QStringLiteral("frameSampling"), m_frameSampling.load() },
			 { QStringLiteral("frameSampleCount"), m_frameDurationsMs.size() },
			 { QStringLiteral("presentedFrameCount"), m_presentedFrameCount },
			 { QStringLiteral("p95FrameMs"), p95FrameMs() }, { QStringLiteral("p99FrameMs"), p99FrameMs() },
			 { QStringLiteral("inputSampleCount"), m_inputLatenciesMs.size() },
			 { QStringLiteral("lastInputLatencyMs"), m_lastInputLatencyMs },
			 { QStringLiteral("maxInputLatencyMs"), m_maxInputLatencyMs },
			 { QStringLiteral("p95InputLatencyMs"), p95InputLatencyMs() },
			 { QStringLiteral("p99InputLatencyMs"), p99InputLatencyMs() },
			 { QStringLiteral("uiStallCount"), m_uiStallCount },
			 { QStringLiteral("maxUiStallMs"), m_maxUiStallMs },
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
	if (m_frameSampling.load()) ++m_presentedFrameCount;
	const qint64 timestampNs = nowNs();
	const QQueue< QString > pending = m_pendingInputOrder;
	for (const QString &operationId : pending) recordVisualAt(operationId, timestampNs);
}

void QmlPerformanceMonitor::beginFrameSampling() {
	if (m_frameSampling.exchange(true)) return;
	m_frameRenderingStartedNs.store(-1);
	m_lastHeartbeatNs = -1;
	emit frameSamplingChanged();
}

void QmlPerformanceMonitor::endFrameSampling() {
	if (!m_frameSampling.exchange(false)) return;
	m_frameRenderingStartedNs.store(-1);
	m_lastHeartbeatNs = -1;
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
	m_lastHeartbeatNs = -1;
	m_frameDurationsMs.clear();
	m_inputLatenciesMs.clear();
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
	appendBounded(m_frameDurationsMs, durationMs);
	emit metricsChanged();
}

void QmlPerformanceMonitor::recordHeartbeatAt(const qint64 timestampNs) {
	if (!m_frameSampling.load()) {
		m_lastHeartbeatNs = -1;
		return;
	}
	if (m_lastHeartbeatNs >= 0 && timestampNs > m_lastHeartbeatNs) {
		const double intervalMs = (timestampNs - m_lastHeartbeatNs) / 1000000.0;
		const double stallMs = std::max(0.0, intervalMs - static_cast< double >(HeartbeatIntervalMs));
		if (stallMs > StallThresholdMs) {
			++m_uiStallCount;
			m_maxUiStallMs = std::max(m_maxUiStallMs, stallMs);
			emit uiStallObserved(stallMs);
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
