#ifndef MUMBLE_MUMBLE_QMLPERFORMANCEMONITOR_H_
#define MUMBLE_MUMBLE_QMLPERFORMANCEMONITOR_H_

#include <QElapsedTimer>
#include <QHash>
#include <QObject>
#include <QPointer>
#include <QQueue>
#include <QTimer>
#include <QVariantMap>
#include <QVector>

#include <atomic>

class QEvent;

class QmlPerformanceMonitor final : public QObject {
	Q_OBJECT
	Q_PROPERTY(double p95FrameMs READ p95FrameMs NOTIFY metricsChanged)
	Q_PROPERTY(double p99FrameMs READ p99FrameMs NOTIFY metricsChanged)
	Q_PROPERTY(double p95RenderDurationMs READ p95RenderDurationMs NOTIFY metricsChanged)
	Q_PROPERTY(double p99RenderDurationMs READ p99RenderDurationMs NOTIFY metricsChanged)
	Q_PROPERTY(bool frameSampling READ frameSampling NOTIFY frameSamplingChanged)
	Q_PROPERTY(double lastInputLatencyMs READ lastInputLatencyMs NOTIFY metricsChanged)
	Q_PROPERTY(double maxInputLatencyMs READ maxInputLatencyMs NOTIFY metricsChanged)
	Q_PROPERTY(double p95InputLatencyMs READ p95InputLatencyMs NOTIFY metricsChanged)
	Q_PROPERTY(double p99InputLatencyMs READ p99InputLatencyMs NOTIFY metricsChanged)
	Q_PROPERTY(int uiStallCount READ uiStallCount NOTIFY metricsChanged)
	Q_PROPERTY(double maxUiStallMs READ maxUiStallMs NOTIFY metricsChanged)
	Q_PROPERTY(QVariantMap snapshot READ snapshot NOTIFY metricsChanged)

public:
	explicit QmlPerformanceMonitor(QObject *parent = nullptr);

	double p95FrameMs() const;
	double p99FrameMs() const;
	double p95RenderDurationMs() const;
	double p99RenderDurationMs() const;
	bool frameSampling() const;
	double lastInputLatencyMs() const;
	double maxInputLatencyMs() const;
	double p95InputLatencyMs() const;
	double p99InputLatencyMs() const;
	int uiStallCount() const;
	double maxUiStallMs() const;
	QVariantMap snapshot() const;

	void markFrameRenderingStarted();
	void markFrameRenderingFinished();
	Q_INVOKABLE void markFramePresented();
	Q_INVOKABLE void beginFrameSampling();
	Q_INVOKABLE void endFrameSampling();
	Q_INVOKABLE QString markInput(const QString &operationId = {});
	Q_INVOKABLE void markVisualComplete(const QString &operationId);
	Q_INVOKABLE void installInputObserver(QObject *target);
	Q_INVOKABLE void reset();

	// Deterministic hooks used by tests and non-QML automation adapters.
	void recordFramePresentedAt(qint64 timestampNs);
	void recordFrameDuration(double durationMs);
	void recordHeartbeatAt(qint64 timestampNs);
	void recordInputAt(const QString &operationId, qint64 timestampNs);
	void recordVisualAt(const QString &operationId, qint64 timestampNs);
	void recordModelReset(const QString &modelName);
	void recordSyncUiOperationViolation(const QString &category);

signals:
	void metricsChanged();
	void frameSamplingChanged();
	void uiStallObserved(double durationMs);
	void inputLatencyObserved(const QString &operationId, double durationMs);

private:
	bool eventFilter(QObject *watched, QEvent *event) override;
	static double percentile(const QVector< double > &values, double fraction);
	static void appendBounded(QVector< double > &values, double value);
	void recordFramePresentedAt(qint64 timestampNs, bool sampledAtPresentation, qulonglong samplingGeneration);
	qint64 nowNs() const;

	QElapsedTimer m_clock;
	QTimer m_heartbeat;
	std::atomic< qint64 > m_frameRenderingStartedNs = -1;
	std::atomic< bool > m_frameSampling = false;
	std::atomic< qulonglong > m_samplingGeneration = 0;
	qint64 m_lastHeartbeatNs = -1;
	qint64 m_lastPresentedFrameNs = -1;
	QVector< double > m_frameIntervalsMs;
	QVector< double > m_renderDurationsMs;
	QVector< double > m_inputLatenciesMs;
	QHash< QString, int > m_modelResetCounts;
	QHash< QString, int > m_syncUiOperationViolationCounts;
	QHash< QString, qint64 > m_pendingInputs;
	QQueue< QString > m_pendingInputOrder;
	QPointer< QObject > m_inputTarget;
	double m_lastInputLatencyMs = 0.0;
	double m_maxInputLatencyMs = 0.0;
	int m_uiStallCount = 0;
	int m_presentedFrameCount = 0;
	double m_maxUiStallMs = 0.0;
	qulonglong m_generatedOperationId = 0;
};

#endif
