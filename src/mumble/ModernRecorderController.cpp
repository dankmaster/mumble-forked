// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernRecorderController.h"

#include <QtCore/QDir>
#include <QtCore/QFileInfo>

#include <algorithm>

namespace Mumble {

namespace {
	QVariantMap option(const QString &label, const int value, const bool enabled = true) {
		return { { QStringLiteral("label"), label }, { QStringLiteral("value"), value },
				 { QStringLiteral("enabled"), enabled } };
	}

	QString operationFailureMessage(const ModernRecorderRuntimeResult &result) {
		return result.errorMessage.trimmed().isEmpty()
			? ModernRecorderController::tr("The recorder operation could not be completed.")
			: result.errorMessage.trimmed();
	}
} // namespace

ModernRecorderRuntimeResult ModernRecorderRuntimeResult::failure(const QString &code, const QString &message) {
	ModernRecorderRuntimeResult result;
	result.success      = false;
	result.errorCode    = code.trimmed();
	result.errorMessage = message.trimmed();
	return result;
}

ModernRecorderController::ModernRecorderController(QObject *parent) : QObject(parent) {
	m_elapsedTimer.setInterval(1000);
	m_elapsedTimer.setTimerType(Qt::VeryCoarseTimer);
	connect(&m_elapsedTimer, &QTimer::timeout, this, &ModernRecorderController::refreshElapsed);
}

ModernRecorderController::~ModernRecorderController() {
	m_elapsedTimer.stop();
	if (m_session) {
		if (m_runtime && m_sessionAttached) m_runtime->detach(m_session);
		m_session->stop(true);
	}
	if (m_announcedRecording && m_runtime) m_runtime->announceRecordingState(m_session, false);
}

void ModernRecorderController::setRuntime(ModernRecorderRuntime *runtime) {
	if (m_runtime == runtime) return;
	Q_ASSERT(!m_session);
	m_runtime = runtime;
	refreshCapabilities();
}

ModernRecorderRuntime *ModernRecorderController::runtime() const { return m_runtime; }

void ModernRecorderController::setInitialConfiguration(const QString &outputDirectory, const QString &fileName,
												const int format, const int mode) {
	if (!canEdit()) return;
	m_outputDirectory = QDir::toNativeSeparators(outputDirectory.trimmed());
	m_fileName        = fileName.trimmed().isEmpty() ? QStringLiteral("%user") : fileName.trimmed();
	m_format          = formatAvailable(format) ? format
		: (!formatOptions().isEmpty() ? formatOptions().constFirst().toMap().value(QStringLiteral("value")).toInt() : 0);
	m_mode            = mode >= Mixdown && mode <= TransportOnly ? mode : Mixdown;
	if (!transportSupported() && modeUsesTransport(m_mode)) m_mode = Mixdown;
	emit configurationChanged();
}

bool ModernRecorderController::applyVisualFixtureState(const QString &state, const qint64 elapsedMilliseconds,
												 const QString &outputDirectory, const QString &fileName,
												 const int format, const int mode,
												 const bool transportSupported) {
	static const QStringList allowedStates { QStringLiteral("idle"), QStringLiteral("recording"),
		QStringLiteral("paused"), QStringLiteral("stopping"), QStringLiteral("error") };
	if (m_session || busy() || !allowedStates.contains(state) || mode < Mixdown || mode > TransportOnly) return false;

	m_visualFixtureActive = true;
	m_visualFixtureTransportSupported = transportSupported;
	m_outputDirectory = QDir::toNativeSeparators(outputDirectory.trimmed());
	m_fileName = fileName.trimmed().isEmpty() ? QStringLiteral("%user") : fileName.trimmed();
	m_format = format;
	m_mode = (!transportSupported && modeUsesTransport(mode)) ? Mixdown : mode;
	m_elapsedMilliseconds = std::max< qint64 >(0, elapsedMilliseconds);
	m_operationId.clear();
	m_operationAction.clear();
	m_operationStatus = QStringLiteral("idle");
	m_operationPhase.clear();
	m_operationCancellable = false;
	resetErrorData();
	setState(state);
	emit configurationChanged();
	emit elapsedChanged();
	emit optionsChanged();
	emit operationChanged();
	emit capabilitiesChanged();
	return true;
}

void ModernRecorderController::clearVisualFixtureState() {
	if (!m_visualFixtureActive || m_session || busy()) return;
	m_visualFixtureActive = false;
	m_visualFixtureTransportSupported = false;
	m_elapsedMilliseconds = 0;
	m_operationId.clear();
	m_operationAction.clear();
	m_operationStatus = QStringLiteral("idle");
	m_operationPhase.clear();
	m_operationCancellable = false;
	resetErrorData();
	setState(QStringLiteral("idle"));
	emit elapsedChanged();
	emit optionsChanged();
	emit operationChanged();
	emit capabilitiesChanged();
}

QString ModernRecorderController::state() const { return m_state; }
bool ModernRecorderController::busy() const { return m_operationStatus == QLatin1String("running"); }
bool ModernRecorderController::canEdit() const {
	return !busy() && !m_session && (m_state == QLatin1String("idle") || m_state == QLatin1String("error"));
}
bool ModernRecorderController::canStart() const {
	return m_runtime && !busy() && !m_session
		&& (m_state == QLatin1String("idle") || m_state == QLatin1String("error"));
}
bool ModernRecorderController::canPause() const {
	return m_runtime && !busy() && (m_session || m_visualFixtureActive) && m_state == QLatin1String("recording");
}
bool ModernRecorderController::canResume() const {
	return m_runtime && !busy() && (m_session || m_visualFixtureActive) && m_state == QLatin1String("paused");
}
bool ModernRecorderController::canStop() const {
	return m_runtime && !busy() && (m_session || m_visualFixtureActive)
		&& (m_state == QLatin1String("recording") || m_state == QLatin1String("paused")
			|| m_state == QLatin1String("error"));
}
bool ModernRecorderController::transportSupported() const {
	return m_visualFixtureActive ? m_visualFixtureTransportSupported
		: m_runtime && m_runtime->transportSupported();
}
qint64 ModernRecorderController::elapsedMilliseconds() const { return m_elapsedMilliseconds; }

QString ModernRecorderController::elapsedText() const {
	const qint64 totalSeconds = std::max< qint64 >(0, m_elapsedMilliseconds / 1000);
	const qint64 hours = totalSeconds / 3600;
	const qint64 minutes = (totalSeconds / 60) % 60;
	const qint64 seconds = totalSeconds % 60;
	return QStringLiteral("%1:%2:%3")
		.arg(hours, 2, 10, QLatin1Char('0'))
		.arg(minutes, 2, 10, QLatin1Char('0'))
		.arg(seconds, 2, 10, QLatin1Char('0'));
}

QString ModernRecorderController::outputDirectory() const { return m_outputDirectory; }
QString ModernRecorderController::fileName() const { return m_fileName; }
QString ModernRecorderController::resolvedOutputPath() const { return configuration().resolvedOutputPath; }
int ModernRecorderController::format() const { return m_format; }
int ModernRecorderController::mode() const { return m_mode; }

QVariantList ModernRecorderController::formatOptions() const {
	return m_runtime ? m_runtime->formatOptions() : QVariantList();
}

QVariantList ModernRecorderController::modeOptions() const {
	const bool transport = transportSupported();
	return { option(tr("Mixdown"), Mixdown), option(tr("Multichannel"), Multichannel),
		option(tr("Multichannel + transport"), MultichannelAndTransport, transport),
		option(tr("Transport only"), TransportOnly, transport) };
}

QVariantMap ModernRecorderController::fieldErrors() const { return m_fieldErrors; }
QString ModernRecorderController::errorCode() const { return m_errorCode; }
QString ModernRecorderController::errorMessage() const { return m_errorMessage; }
QString ModernRecorderController::operationId() const { return m_operationId; }
QString ModernRecorderController::operationAction() const { return m_operationAction; }
QString ModernRecorderController::operationStatus() const { return m_operationStatus; }
QString ModernRecorderController::operationPhase() const { return m_operationPhase; }
bool ModernRecorderController::operationCancellable() const { return m_operationCancellable; }

void ModernRecorderController::setOutputDirectory(const QString &outputDirectory) {
	if (!canEdit()) return;
	const QString normalized = QDir::toNativeSeparators(outputDirectory.trimmed());
	if (m_outputDirectory == normalized) return;
	m_outputDirectory = normalized;
	emit configurationChanged();
}

void ModernRecorderController::setFileName(const QString &fileName) {
	if (!canEdit()) return;
	const QString normalized = fileName.trimmed();
	if (m_fileName == normalized) return;
	m_fileName = normalized;
	emit configurationChanged();
}

void ModernRecorderController::setFormat(const int format) {
	if (!canEdit() || !formatAvailable(format) || m_format == format) return;
	m_format = format;
	emit configurationChanged();
}

void ModernRecorderController::setMode(const int mode) {
	if (!canEdit() || mode < Mixdown || mode > TransportOnly || (!transportSupported() && modeUsesTransport(mode))
		|| m_mode == mode) return;
	m_mode = mode;
	emit configurationChanged();
}

bool ModernRecorderController::start() {
	if (!canStart() || m_visualFixtureActive) return false;
	beginOperation(QStringLiteral("start"), QStringLiteral("validating"));
	resetErrorData();
	if (m_elapsedMilliseconds != 0) {
		m_elapsedMilliseconds = 0;
		emit elapsedChanged();
	}
	m_excludedBackendMicroseconds = 0;
	m_pauseStartedAtBackendMicroseconds = 0;
	QVariantMap validationErrors;
	const ModernRecorderConfiguration nextConfiguration = configuration(&validationErrors);
	if (!validationErrors.isEmpty()) {
		setError(QStringLiteral("invalid_configuration"), tr("Check the highlighted recording settings."),
			validationErrors);
		setState(QStringLiteral("error"));
		completeOperation(QStringLiteral("failed"), m_errorCode, m_errorMessage);
		return false;
	}

	ModernRecorderRuntimeResult result = m_runtime->preflight(nextConfiguration);
	if (!result.success) return failOperation(result, true);
	m_operationPhase = QStringLiteral("creating");
	emit operationChanged();
	ModernRecorderSession *session = m_runtime->createSession(nextConfiguration, this, &result);
	if (!session || !result.success) {
		if (session) session->deleteLater();
		if (result.success) result = ModernRecorderRuntimeResult::failure(
			QStringLiteral("create_failed"), tr("The recording backend could not be created."));
		return failOperation(result, true);
	}
	if (!session->parent()) session->setParent(this);
	m_session = session;
	const qulonglong generation = ++m_sessionGeneration;
	connectSession(session, generation);
	m_operationPhase = QStringLiteral("attaching");
	emit operationChanged();
	result = m_runtime->attach(session);
	if (!result.success) {
		retireSession();
		return failOperation(result, true);
	}
	m_sessionAttached = true;
	m_operationPhase = QStringLiteral("starting");
	emit operationChanged();
	m_runtime->persistConfiguration(nextConfiguration);
	session->start();
	return true;
}

bool ModernRecorderController::pause() {
	if (!canPause() || m_visualFixtureActive) return false;
	beginOperation(QStringLiteral("pause"), QStringLiteral("detaching"));
	updateElapsedFromSession();
	m_pauseStartedAtBackendMicroseconds = m_session->elapsedMicroseconds();
	const ModernRecorderRuntimeResult result = m_runtime->detach(m_session);
	if (!result.success) return failOperation(result, false);
	m_sessionAttached = false;
	if (m_announcedRecording) {
		m_runtime->announceRecordingState(m_session, false);
		m_announcedRecording = false;
	}
	m_elapsedTimer.stop();
	setState(QStringLiteral("paused"));
	completeOperation(QStringLiteral("succeeded"));
	return true;
}

bool ModernRecorderController::resume() {
	if (!canResume() || m_visualFixtureActive) return false;
	beginOperation(QStringLiteral("resume"), QStringLiteral("attaching"));
	const ModernRecorderRuntimeResult result = m_runtime->attach(m_session);
	if (!result.success) return failOperation(result, false);
	m_sessionAttached = true;
	const quint64 backendNow = m_session->elapsedMicroseconds();
	if (backendNow >= m_pauseStartedAtBackendMicroseconds)
		m_excludedBackendMicroseconds += backendNow - m_pauseStartedAtBackendMicroseconds;
	m_pauseStartedAtBackendMicroseconds = 0;
	m_runtime->announceRecordingState(m_session, true);
	m_announcedRecording = true;
	setState(QStringLiteral("recording"));
	updateElapsedFromSession();
	m_elapsedTimer.start();
	completeOperation(QStringLiteral("succeeded"));
	return true;
}

bool ModernRecorderController::stop() {
	if (!canStop() || m_visualFixtureActive) return false;
	beginOperation(QStringLiteral("stop"), QStringLiteral("stopping"));
	updateElapsedFromSession();
	if (m_state == QLatin1String("recording")) {
		const ModernRecorderRuntimeResult result = m_runtime->detach(m_session);
		if (!result.success) return failOperation(result, false);
		m_sessionAttached = false;
	}
	if (m_announcedRecording) {
		m_runtime->announceRecordingState(m_session, false);
		m_announcedRecording = false;
	}
	m_elapsedTimer.stop();
	setState(QStringLiteral("stopping"));
	m_session->stop(false);
	return true;
}

void ModernRecorderController::clearError() {
	resetErrorData();
	if (m_state == QLatin1String("error") && !m_session) setState(QStringLiteral("idle"));
}

void ModernRecorderController::refreshCapabilities() {
	if (!transportSupported() && modeUsesTransport(m_mode) && canEdit()) {
		m_mode = Mixdown;
		emit configurationChanged();
	}
	emit optionsChanged();
	emit capabilitiesChanged();
}

void ModernRecorderController::refreshElapsed() { updateElapsedFromSession(); }

ModernRecorderConfiguration ModernRecorderController::configuration(QVariantMap *validationErrors) const {
	ModernRecorderConfiguration result;
	result.outputDirectory = QDir::fromNativeSeparators(m_outputDirectory).trimmed();
	QFileInfo fileInfo(m_fileName.trimmed());
	QString baseName = fileInfo.baseName().trimmed();
	if (baseName.isEmpty()) baseName = QStringLiteral("%user");
	QString suffix = fileInfo.completeSuffix().trimmed();
	if (suffix.isEmpty()) suffix = defaultExtension();
	result.fileName = baseName;
	result.format   = m_format;
	result.mode     = m_mode;
	result.mixDown  = modeUsesMixdown(m_mode);
	result.transportEnabled = modeUsesTransport(m_mode);
	if (!result.outputDirectory.isEmpty()) {
		result.resolvedOutputPath = QDir(result.outputDirectory).absoluteFilePath(baseName + QLatin1Char('.') + suffix);
	}
	if (validationErrors) {
		if (result.outputDirectory.isEmpty())
			validationErrors->insert(QStringLiteral("recording.path"), tr("Choose a target directory."));
		if (!formatAvailable(m_format))
			validationErrors->insert(QStringLiteral("recording.format"), tr("Select a recording format."));
		if (m_mode < Mixdown || m_mode > TransportOnly || (!transportSupported() && modeUsesTransport(m_mode)))
			validationErrors->insert(QStringLiteral("recording.mode"), tr("Select an available recording mode."));
	}
	return result;
}

void ModernRecorderController::setState(const QString &state) {
	if (m_state == state) return;
	m_state = state;
	emit stateChanged();
	emit capabilitiesChanged();
}

void ModernRecorderController::setError(const QString &code, const QString &message,
										const QVariantMap &fieldErrors) {
	m_errorCode    = code.trimmed();
	m_errorMessage = message.trimmed();
	m_fieldErrors  = fieldErrors;
	emit errorChanged();
}

void ModernRecorderController::resetErrorData() {
	if (m_errorCode.isEmpty() && m_errorMessage.isEmpty() && m_fieldErrors.isEmpty()) return;
	m_errorCode.clear();
	m_errorMessage.clear();
	m_fieldErrors.clear();
	emit errorChanged();
}

void ModernRecorderController::beginOperation(const QString &action, const QString &phase, const bool cancellable) {
	m_operationId = QStringLiteral("recorder:%1").arg(m_nextOperationId++);
	m_operationAction = action;
	m_operationStatus = QStringLiteral("running");
	m_operationPhase = phase;
	m_operationCancellable = cancellable;
	emit operationChanged();
	emit capabilitiesChanged();
	emit operationStarted(m_operationId, m_operationAction);
}

void ModernRecorderController::completeOperation(const QString &status, const QString &code, const QString &message) {
	const QString completedId = m_operationId;
	m_operationStatus = status;
	m_operationPhase = QStringLiteral("complete");
	m_operationCancellable = false;
	emit operationChanged();
	emit capabilitiesChanged();
	emit operationFinished(completedId, status, code, message);
}

bool ModernRecorderController::failOperation(const ModernRecorderRuntimeResult &result, const bool fatal) {
	const QString code = result.errorCode.trimmed().isEmpty() ? QStringLiteral("operation_failed")
															 : result.errorCode.trimmed();
	const QString message = operationFailureMessage(result);
	setError(code, message);
	if (fatal) setState(QStringLiteral("error"));
	completeOperation(QStringLiteral("failed"), code, message);
	return false;
}

void ModernRecorderController::connectSession(ModernRecorderSession *session, const qulonglong generation) {
	connect(session, &ModernRecorderSession::started, this, [this, session, generation]() {
		if (generation != m_sessionGeneration || session != m_session) return;
		resetErrorData();
		setState(QStringLiteral("recording"));
		m_runtime->announceRecordingState(session, true);
		m_announcedRecording = true;
		updateElapsedFromSession();
		m_elapsedTimer.start();
		if (busy() && m_operationAction == QLatin1String("start"))
			completeOperation(QStringLiteral("succeeded"));
	});
	connect(session, &ModernRecorderSession::failed, this,
		[this, session, generation](const QString &code, const QString &message) {
			if (generation != m_sessionGeneration || session != m_session) return;
			m_elapsedTimer.stop();
			updateElapsedFromSession();
			if (m_runtime && m_sessionAttached) m_runtime->detach(session);
			m_sessionAttached = false;
			if (m_announcedRecording && m_runtime) m_runtime->announceRecordingState(session, false);
			m_announcedRecording = false;
			setError(code.isEmpty() ? QStringLiteral("backend_error") : code, message);
			setState(QStringLiteral("error"));
			if (!busy()) beginOperation(QStringLiteral("backend"), QStringLiteral("failed"));
			completeOperation(QStringLiteral("failed"), m_errorCode, m_errorMessage);
		});
	connect(session, &ModernRecorderSession::stopped, this, [this, session, generation]() {
		if (generation != m_sessionGeneration || session != m_session) return;
		m_elapsedTimer.stop();
		updateElapsedFromSession();
		if (m_runtime && m_sessionAttached) m_runtime->detach(session);
		m_sessionAttached = false;
		if (m_announcedRecording && m_runtime) m_runtime->announceRecordingState(session, false);
		m_announcedRecording = false;
		const bool requestedStop = m_state == QLatin1String("stopping");
		const bool failedStart = busy() && m_operationAction == QLatin1String("start");
		retireSession();
		if (requestedStop) {
			resetErrorData();
			setState(QStringLiteral("idle"));
			if (busy()) completeOperation(QStringLiteral("succeeded"));
		} else if (m_state != QLatin1String("error")) {
			const QString code = failedStart ? QStringLiteral("start_failed") : QStringLiteral("backend_stopped");
			const QString message = failedStart ? tr("The recording backend did not start.")
											: tr("Recording stopped unexpectedly.");
			setError(code, message);
			setState(QStringLiteral("error"));
			if (!busy()) beginOperation(QStringLiteral("backend"), QStringLiteral("stopped"));
			completeOperation(QStringLiteral("failed"), code, message);
		}
	});
}

void ModernRecorderController::retireSession() {
	if (!m_session) return;
	ModernRecorderSession *retired = m_session;
	m_session = nullptr;
	m_sessionAttached = false;
	++m_sessionGeneration;
	retired->deleteLater();
	emit capabilitiesChanged();
}

void ModernRecorderController::updateElapsedFromSession() {
	if (!m_session) return;
	quint64 backendElapsed = m_state == QLatin1String("paused") && m_pauseStartedAtBackendMicroseconds > 0
		? m_pauseStartedAtBackendMicroseconds : m_session->elapsedMicroseconds();
	backendElapsed = backendElapsed > m_excludedBackendMicroseconds
		? backendElapsed - m_excludedBackendMicroseconds : 0;
	const qint64 nextMilliseconds = static_cast< qint64 >(backendElapsed / 1000);
	if (m_elapsedMilliseconds == nextMilliseconds) return;
	m_elapsedMilliseconds = nextMilliseconds;
	emit elapsedChanged();
}

QString ModernRecorderController::defaultExtension() const {
	const QString extension = m_runtime ? m_runtime->defaultExtension(m_format).trimmed() : QString();
	return extension.isEmpty() ? QStringLiteral("wav") : extension;
}

bool ModernRecorderController::formatAvailable(const int format) const {
	for (const QVariant &entry : formatOptions()) {
		const QVariantMap row = entry.toMap();
		if (row.value(QStringLiteral("value")).toInt() == format
			&& row.value(QStringLiteral("enabled"), true).toBool()) return true;
	}
	return false;
}

bool ModernRecorderController::modeUsesTransport(const int mode) const {
	return mode == MultichannelAndTransport || mode == TransportOnly;
}

bool ModernRecorderController::modeUsesMixdown(const int mode) const {
	return mode == Mixdown || mode == TransportOnly;
}

} // namespace Mumble
