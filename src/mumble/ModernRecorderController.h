// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNRECORDERCONTROLLER_H_
#define MUMBLE_MUMBLE_MODERNRECORDERCONTROLLER_H_

#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QString>
#include <QtCore/QTimer>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>

namespace Mumble {

struct ModernRecorderConfiguration {
	QString outputDirectory;
	QString fileName;
	QString resolvedOutputPath;
	int format = 0;
	int mode   = 0;
	int sampleRate = 0;
	bool mixDown = true;
	bool transportEnabled = false;
};

struct ModernRecorderRuntimeResult {
	bool success = true;
	QString errorCode;
	QString errorMessage;

	static ModernRecorderRuntimeResult failure(const QString &code, const QString &message);
};

/// Frontend-neutral lifetime wrapper for a recording backend. The production
/// implementation below forwards the existing VoiceRecorder signals without
/// making the QML-facing controller depend on MainWindow or ServerHandler.
class ModernRecorderSession : public QObject {
	Q_OBJECT

public:
	using QObject::QObject;
	~ModernRecorderSession() override = default;

	virtual void start() = 0;
	virtual void stop(bool force = false) = 0;
	virtual quint64 elapsedMicroseconds() const = 0;

signals:
	void started();
	void stopped();
	void failed(const QString &errorCode, const QString &message);
};

/// Small runtime seam implemented by the application composition root. It is
/// deliberately free of QWidget and QML types, which also makes recorder state
/// deterministic to test without a server or audio device.
class ModernRecorderRuntime {
public:
	virtual ~ModernRecorderRuntime() = default;
	virtual bool transportSupported() const = 0;
	virtual QVariantList formatOptions() const = 0;
	virtual QString defaultExtension(int format) const = 0;
	virtual ModernRecorderRuntimeResult preflight(const ModernRecorderConfiguration &configuration) const = 0;
	virtual ModernRecorderSession *createSession(const ModernRecorderConfiguration &configuration,
										 QObject *parent, ModernRecorderRuntimeResult *result) = 0;
	virtual ModernRecorderRuntimeResult attach(ModernRecorderSession *session) = 0;
	virtual ModernRecorderRuntimeResult detach(ModernRecorderSession *session) = 0;
	virtual void announceRecordingState(ModernRecorderSession *session, bool recording) = 0;
	virtual void persistConfiguration(const ModernRecorderConfiguration &configuration) = 0;
};

/// Typed, incrementally updating state for the native recorder surface.
/// Elapsed time is the only periodic property and updates at most once per
/// second; no dialog DTO or field model is rebuilt while recording.
class ModernRecorderController final : public QObject {
	Q_OBJECT
	Q_PROPERTY(QString state READ state NOTIFY stateChanged)
	Q_PROPERTY(bool busy READ busy NOTIFY operationChanged)
	Q_PROPERTY(bool canEdit READ canEdit NOTIFY capabilitiesChanged)
	Q_PROPERTY(bool canStart READ canStart NOTIFY capabilitiesChanged)
	Q_PROPERTY(bool canPause READ canPause NOTIFY capabilitiesChanged)
	Q_PROPERTY(bool canResume READ canResume NOTIFY capabilitiesChanged)
	Q_PROPERTY(bool canStop READ canStop NOTIFY capabilitiesChanged)
	Q_PROPERTY(bool transportSupported READ transportSupported NOTIFY optionsChanged)
	Q_PROPERTY(qint64 elapsedMilliseconds READ elapsedMilliseconds NOTIFY elapsedChanged)
	Q_PROPERTY(QString elapsedText READ elapsedText NOTIFY elapsedChanged)
	Q_PROPERTY(QString outputDirectory READ outputDirectory WRITE setOutputDirectory NOTIFY configurationChanged)
	Q_PROPERTY(QString fileName READ fileName WRITE setFileName NOTIFY configurationChanged)
	Q_PROPERTY(QString resolvedOutputPath READ resolvedOutputPath NOTIFY configurationChanged)
	Q_PROPERTY(int format READ format WRITE setFormat NOTIFY configurationChanged)
	Q_PROPERTY(int mode READ mode WRITE setMode NOTIFY configurationChanged)
	Q_PROPERTY(QVariantList formatOptions READ formatOptions NOTIFY optionsChanged)
	Q_PROPERTY(QVariantList modeOptions READ modeOptions NOTIFY optionsChanged)
	Q_PROPERTY(QVariantMap fieldErrors READ fieldErrors NOTIFY errorChanged)
	Q_PROPERTY(QString errorCode READ errorCode NOTIFY errorChanged)
	Q_PROPERTY(QString errorMessage READ errorMessage NOTIFY errorChanged)
	Q_PROPERTY(QString operationId READ operationId NOTIFY operationChanged)
	Q_PROPERTY(QString operationAction READ operationAction NOTIFY operationChanged)
	Q_PROPERTY(QString operationStatus READ operationStatus NOTIFY operationChanged)
	Q_PROPERTY(QString operationPhase READ operationPhase NOTIFY operationChanged)
	Q_PROPERTY(bool operationCancellable READ operationCancellable NOTIFY operationChanged)

public:
	enum RecordingMode {
		Mixdown = 0,
		Multichannel = 1,
		MultichannelAndTransport = 2,
		TransportOnly = 3
	};
	Q_ENUM(RecordingMode)

	explicit ModernRecorderController(QObject *parent = nullptr);
	~ModernRecorderController() override;

	void setRuntime(ModernRecorderRuntime *runtime);
	ModernRecorderRuntime *runtime() const;
	void setInitialConfiguration(const QString &outputDirectory, const QString &fileName, int format, int mode);
	bool applyVisualFixtureState(const QString &state, qint64 elapsedMilliseconds,
							 const QString &outputDirectory, const QString &fileName,
							 int format, int mode, bool transportSupported);
	void clearVisualFixtureState();

	QString state() const;
	bool busy() const;
	bool canEdit() const;
	bool canStart() const;
	bool canPause() const;
	bool canResume() const;
	bool canStop() const;
	bool transportSupported() const;
	qint64 elapsedMilliseconds() const;
	QString elapsedText() const;
	QString outputDirectory() const;
	QString fileName() const;
	QString resolvedOutputPath() const;
	int format() const;
	int mode() const;
	QVariantList formatOptions() const;
	QVariantList modeOptions() const;
	QVariantMap fieldErrors() const;
	QString errorCode() const;
	QString errorMessage() const;
	QString operationId() const;
	QString operationAction() const;
	QString operationStatus() const;
	QString operationPhase() const;
	bool operationCancellable() const;

	void setOutputDirectory(const QString &outputDirectory);
	void setFileName(const QString &fileName);
	void setFormat(int format);
	void setMode(int mode);

	Q_INVOKABLE bool start();
	Q_INVOKABLE bool pause();
	Q_INVOKABLE bool resume();
	Q_INVOKABLE bool stop();
	Q_INVOKABLE void clearError();
	Q_INVOKABLE void refreshCapabilities();
	Q_INVOKABLE void refreshElapsed();

signals:
	void stateChanged();
	void capabilitiesChanged();
	void elapsedChanged();
	void configurationChanged();
	void optionsChanged();
	void errorChanged();
	void operationChanged();
	void operationStarted(const QString &operationId, const QString &action);
	void operationFinished(const QString &operationId, const QString &status,
						   const QString &errorCode, const QString &message);

private:
	ModernRecorderConfiguration configuration(QVariantMap *validationErrors = nullptr) const;
	void setState(const QString &state);
	void setError(const QString &code, const QString &message, const QVariantMap &fieldErrors = {});
	void resetErrorData();
	void beginOperation(const QString &action, const QString &phase, bool cancellable = false);
	void completeOperation(const QString &status, const QString &code = {}, const QString &message = {});
	bool failOperation(const ModernRecorderRuntimeResult &result, bool fatal);
	void connectSession(ModernRecorderSession *session, qulonglong generation);
	void retireSession();
	void updateElapsedFromSession();
	QString defaultExtension() const;
	bool formatAvailable(int format) const;
	bool modeUsesTransport(int mode) const;
	bool modeUsesMixdown(int mode) const;

	ModernRecorderRuntime *m_runtime = nullptr;
	QPointer< ModernRecorderSession > m_session;
	QTimer m_elapsedTimer;
	QString m_state = QStringLiteral("idle");
	QString m_outputDirectory;
	QString m_fileName = QStringLiteral("%user");
	int m_format = 0;
	int m_mode = Mixdown;
	qint64 m_elapsedMilliseconds = 0;
	quint64 m_excludedBackendMicroseconds = 0;
	quint64 m_pauseStartedAtBackendMicroseconds = 0;
	QVariantMap m_fieldErrors;
	QString m_errorCode;
	QString m_errorMessage;
	QString m_operationId;
	QString m_operationAction;
	QString m_operationStatus = QStringLiteral("idle");
	QString m_operationPhase;
	bool m_operationCancellable = false;
	qulonglong m_nextOperationId = 1;
	qulonglong m_sessionGeneration = 0;
	bool m_announcedRecording = false;
	bool m_sessionAttached = false;
	bool m_visualFixtureActive = false;
	bool m_visualFixtureTransportSupported = false;
};

} // namespace Mumble

#endif // MUMBLE_MUMBLE_MODERNRECORDERCONTROLLER_H_
