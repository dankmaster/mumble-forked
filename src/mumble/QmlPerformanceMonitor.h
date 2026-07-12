#ifndef MUMBLE_MUMBLE_QMLPERFORMANCEMONITOR_H_
#define MUMBLE_MUMBLE_QMLPERFORMANCEMONITOR_H_

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

class QmlPerformanceMonitor final : public QObject {
	Q_OBJECT
	Q_PROPERTY(double p95FrameMs READ p95FrameMs NOTIFY metricsChanged)
	Q_PROPERTY(double p99FrameMs READ p99FrameMs NOTIFY metricsChanged)
	Q_PROPERTY(bool frameSampling READ frameSampling NOTIFY frameSamplingChanged)
	Q_PROPERTY(double lastInputLatencyMs READ lastInputLatencyMs NOTIFY metricsChanged)
	Q_PROPERTY(double maxInputLatencyMs READ maxInputLatencyMs NOTIFY metricsChanged)
	Q_PROPERTY(int uiStallCount READ uiStallCount NOTIFY metricsChanged)
	Q_PROPERTY(double maxUiStallMs READ maxUiStallMs NOTIFY metricsChanged)
	Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY metricsChanged)

public:
	explicit QmlPerformanceMonitor(QObject *parent = nullptr);

	double p95FrameMs() const;
	double p99FrameMs() const;
	bool frameSampling() const;
	double lastInputLatencyMs() const;
	double maxInputLatencyMs() const;
	int uiStallCount() const;
	double maxUiStallMs() const;
	QVariantMap snapshot() const;

	Q_INVOKABLE void markFramePresented();
	Q_INVOKABLE void beginFrameSampling();
	Q_INVOKABLE void endFrameSampling();
	Q_INVOKABLE QString markInput(const QString &operationId = {});
	Q_INVOKABLE void markVisualComplete(const QString &operationId);
	Q_INVOKABLE void reset();

	// Deterministic hooks used by tests and non-QML automation adapters.
	void recordFrameAt(qint64 timestampNs);
	void recordHeartbeatAt(qint64 timestampNs);
	void recordInputAt(const QString &operationId, qint64 timestampNs);
	void recordVisualAt(const QString &operationId, qint64 timestampNs);

signals:
	void metricsChanged();
	void frameSamplingChanged();
	void uiStallObserved(double durationMs);
	void inputLatencyObserved(const QString &operationId, double durationMs);

private:
	static double percentile(const QVector< double > &values, double fraction);
	static void appendBounded(QVector< double > &values, double value);
	qint64 nowNs() const;

	QElapsedTimer m_clock;
	QTimer m_heartbeat;
	qint64 m_lastFrameNs = -1;
	bool m_frameSampling = false;
	qint64 m_lastHeartbeatNs = -1;
	QVector< double > m_frameIntervalsMs;
	QVector< double > m_inputLatenciesMs;
	QHash< QString, qint64 > m_pendingInputs;
	double m_lastInputLatencyMs = 0.0;
	double m_maxInputLatencyMs = 0.0;
	int m_uiStallCount = 0;
	double m_maxUiStallMs = 0.0;
	qulonglong m_generatedOperationId = 0;
};

#endif
