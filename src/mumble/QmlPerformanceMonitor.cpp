#include "QmlPerformanceMonitor.h"

#include <algorithm>
#include <cmath>

namespace {
constexpr qint64 HeartbeatIntervalMs = 16;
constexpr double StallThresholdMs = 50.0;
constexpr qsizetype MaximumSamples = 600;
}

QmlPerformanceMonitor::QmlPerformanceMonitor(QObject *parent) : QObject(parent) {
	m_clock.start();
	m_heartbeat.setInterval(HeartbeatIntervalMs);
	m_heartbeat.setTimerType(Qt::PreciseTimer);
	connect(&m_heartbeat, &QTimer::timeout, this, [this]() { recordHeartbeatAt(nowNs()); });
	m_heartbeat.start();
}

double QmlPerformanceMonitor::p95FrameMs() const { return percentile(m_frameIntervalsMs, 0.95); }
double QmlPerformanceMonitor::p99FrameMs() const { return percentile(m_frameIntervalsMs, 0.99); }
bool QmlPerformanceMonitor::frameSampling() const { return m_frameSampling; }
double QmlPerformanceMonitor::lastInputLatencyMs() const { return m_lastInputLatencyMs; }
double QmlPerformanceMonitor::maxInputLatencyMs() const { return m_maxInputLatencyMs; }
int QmlPerformanceMonitor::uiStallCount() const { return m_uiStallCount; }
double QmlPerformanceMonitor::maxUiStallMs() const { return m_maxUiStallMs; }

QVariantMap QmlPerformanceMonitor::snapshot() const {
	return { { QStringLiteral("frameSampling"), m_frameSampling },
			 { QStringLiteral("frameSampleCount"), m_frameIntervalsMs.size() },
			 { QStringLiteral("p95FrameMs"), p95FrameMs() }, { QStringLiteral("p99FrameMs"), p99FrameMs() },
			 { QStringLiteral("inputSampleCount"), m_inputLatenciesMs.size() },
			 { QStringLiteral("lastInputLatencyMs"), m_lastInputLatencyMs },
			 { QStringLiteral("maxInputLatencyMs"), m_maxInputLatencyMs },
			 { QStringLiteral("uiStallCount"), m_uiStallCount },
			 { QStringLiteral("maxUiStallMs"), m_maxUiStallMs },
			 { QStringLiteral("pendingInputCount"), m_pendingInputs.size() } };
}

void QmlPerformanceMonitor::markFramePresented() { recordFrameAt(nowNs()); }

void QmlPerformanceMonitor::beginFrameSampling() {
	if (m_frameSampling) return;
	m_frameSampling = true;
	m_lastFrameNs = -1;
	emit frameSamplingChanged();
}

void QmlPerformanceMonitor::endFrameSampling() {
	if (!m_frameSampling) return;
	m_frameSampling = false;
	m_lastFrameNs = -1;
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

void QmlPerformanceMonitor::reset() {
	m_lastFrameNs = -1;
	const bool wasSampling = m_frameSampling;
	m_frameSampling = false;
	m_lastHeartbeatNs = -1;
	m_frameIntervalsMs.clear();
	m_inputLatenciesMs.clear();
	m_pendingInputs.clear();
	m_lastInputLatencyMs = 0.0;
	m_maxInputLatencyMs = 0.0;
	m_uiStallCount = 0;
	m_maxUiStallMs = 0.0;
	if (wasSampling) emit frameSamplingChanged();
	emit metricsChanged();
}

void QmlPerformanceMonitor::recordFrameAt(const qint64 timestampNs) {
	if (!m_frameSampling) return;
	if (m_lastFrameNs >= 0 && timestampNs > m_lastFrameNs) {
		appendBounded(m_frameIntervalsMs, (timestampNs - m_lastFrameNs) / 1000000.0);
		emit metricsChanged();
	}
	m_lastFrameNs = timestampNs;
}

void QmlPerformanceMonitor::recordHeartbeatAt(const qint64 timestampNs) {
	if (m_lastHeartbeatNs >= 0 && timestampNs > m_lastHeartbeatNs) {
		const double delayMs = (timestampNs - m_lastHeartbeatNs) / 1000000.0;
		if (delayMs > StallThresholdMs) {
			++m_uiStallCount;
			m_maxUiStallMs = std::max(m_maxUiStallMs, delayMs);
			emit uiStallObserved(delayMs);
			emit metricsChanged();
		}
	}
	m_lastHeartbeatNs = timestampNs;
}

void QmlPerformanceMonitor::recordInputAt(const QString &operationId, const qint64 timestampNs) {
	const QString id = operationId.trimmed();
	if (!id.isEmpty()) m_pendingInputs.insert(id, timestampNs);
}

void QmlPerformanceMonitor::recordVisualAt(const QString &operationId, const qint64 timestampNs) {
	const QString id = operationId.trimmed();
	const auto found = m_pendingInputs.constFind(id);
	if (found == m_pendingInputs.cend() || timestampNs < found.value()) return;
	const double latencyMs = (timestampNs - found.value()) / 1000000.0;
	m_pendingInputs.remove(id);
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
