// Copyright The Mumble Developers. All rights reserved.

#include "ScreenShareViewBackend.h"
#include "ScreenShareFrameTransport.h"

#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtGui/QWindow>

#include <algorithm>
#include <utility>

class ScreenShareNativeFrameReader final : public QObject {
	Q_OBJECT
public:
	explicit ScreenShareNativeFrameReader(QObject *parent = nullptr) : QObject(parent), m_timer(new QTimer(this)) {
		m_timer->setInterval(16);
		connect(m_timer, &QTimer::timeout, this, &ScreenShareNativeFrameReader::poll);
	}
public slots:
	void configure(const QString &key, const quint64 generation) {
		m_timer->stop();
		m_transport.detach();
		m_pendingFrame = {};
		m_deliveryPending = false;
		m_inFlightGeneration = 0;
		m_inFlightSequence = 0;
		m_generation = generation;
		const bool active = !key.trimmed().isEmpty() && m_transport.attach(key);
		if (active) m_timer->start();
		emit activeChanged(active);
	}
	void shutdown() {
		m_timer->stop();
		m_transport.detach();
		m_pendingFrame = {};
		m_deliveryPending = false;
		m_generation = 0;
		QThread::currentThread()->quit();
	}
	void poll() {
		Mumble::ScreenShare::NativeFrame frame;
		if (!m_transport.readLatest(&frame) || frame.generation != m_generation) return;
		// Keep only the newest frame while the GUI/render loop is busy. Without
		// this single-flight handoff every 60 Hz frame becomes a queued GUI event,
		// which can retain several full 4K buffers and then present stale frames.
		m_pendingFrame = std::move(frame);
		deliverPendingFrame();
	}
	void acknowledgeFrame(const quint64 generation, const quint64 sequence) {
		if (!m_deliveryPending || generation != m_inFlightGeneration || sequence != m_inFlightSequence) return;
		m_deliveryPending = false;
		m_inFlightGeneration = 0;
		m_inFlightSequence = 0;
		deliverPendingFrame();
	}
signals:
	void activeChanged(bool active);
	void frameReady(const QImage &frame, quint64 generation, quint64 sequence, qint64 timestampUsec);
private:
	void deliverPendingFrame() {
		if (m_deliveryPending || m_pendingFrame.bgra.isEmpty()) return;
		Mumble::ScreenShare::NativeFrame frame = std::move(m_pendingFrame);
		m_pendingFrame = {};
		auto *storage = new QByteArray(std::move(frame.bgra));
		const QImage wrapped(reinterpret_cast< uchar * >(storage->data()), static_cast< int >(frame.width),
						 static_cast< int >(frame.height), static_cast< qsizetype >(frame.stride), QImage::Format_ARGB32,
						 [](void *data) { delete static_cast< QByteArray * >(data); }, storage);
		if (wrapped.isNull()) return;
		m_deliveryPending = true;
		m_inFlightGeneration = frame.generation;
		m_inFlightSequence = frame.sequence;
		emit frameReady(wrapped, frame.generation, frame.sequence, frame.timestampUsec);
	}
	QTimer *m_timer;
	Mumble::ScreenShare::FrameTransport m_transport;
	Mumble::ScreenShare::NativeFrame m_pendingFrame;
	quint64 m_generation = 0;
	quint64 m_inFlightGeneration = 0;
	quint64 m_inFlightSequence = 0;
	bool m_deliveryPending = false;
};

#ifdef Q_OS_WIN
#	include "win.h"
#	include <audiopolicy.h>
#	include <mmdeviceapi.h>
#	include <limits>
#endif

namespace {
#ifdef Q_OS_WIN
struct WindowSearch {
	DWORD processId = 0;
	HWND window = nullptr;
};

BOOL CALLBACK findProcessWindow(HWND window, LPARAM data) {
	auto *search = reinterpret_cast< WindowSearch * >(data);
	DWORD processId = 0;
	GetWindowThreadProcessId(window, &processId);
	RECT rect {};
	if (processId == search->processId && IsWindowVisible(window) && GetAncestor(window, GA_ROOT) == window
		&& GetWindowRect(window, &rect) && rect.right > rect.left && rect.bottom > rect.top) {
		search->window = window;
		return FALSE;
	}
	return TRUE;
}

HWND externalVideoWindow(const qint64 processId) {
	if (processId <= 0 || processId > std::numeric_limits< DWORD >::max()) return nullptr;
	WindowSearch search { static_cast< DWORD >(processId), nullptr };
	EnumWindows(findProcessWindow, reinterpret_cast< LPARAM >(&search));
	return search.window;
}

template< typename T > void releaseCom(T *&object) {
	if (object) { object->Release(); object = nullptr; }
}

struct AudioControlResult {
	bool applied = false;
	QString errorCode;
};

AudioControlResult setProcessAudioControls(const qint64 processId, const bool muted, const float volumeLevel) {
	if (processId <= 0 || processId > std::numeric_limits< DWORD >::max()) {
		return { false, QStringLiteral("invalid-process") };
	}
	const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	if (FAILED(initialized)) {
		return { false, QStringLiteral("com-initialization-failed") };
	}
	const bool uninitialize = SUCCEEDED(initialized);
	IMMDeviceEnumerator *deviceEnumerator = nullptr;
	IMMDeviceCollection *devices = nullptr;
	bool enumeratorReady = false;
	bool endpointsReady = false;
	bool sessionFound = false;
	bool applied = false;
	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
								 __uuidof(IMMDeviceEnumerator), reinterpret_cast< void ** >(&deviceEnumerator)))
		&& deviceEnumerator) {
		enumeratorReady = true;
	}
	if (enumeratorReady
		&& SUCCEEDED(deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)) && devices) {
		endpointsReady = true;
		UINT deviceCount = 0;
		devices->GetCount(&deviceCount);
		for (UINT deviceIndex = 0; deviceIndex < deviceCount; ++deviceIndex) {
			IMMDevice *device = nullptr;
			IAudioSessionManager2 *manager = nullptr;
			IAudioSessionEnumerator *sessions = nullptr;
			if (FAILED(devices->Item(deviceIndex, &device)) || !device) continue;
			if (SUCCEEDED(device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
								  reinterpret_cast< void ** >(&manager))) && manager
				&& SUCCEEDED(manager->GetSessionEnumerator(&sessions)) && sessions) {
				int sessionCount = 0;
				sessions->GetCount(&sessionCount);
				for (int sessionIndex = 0; sessionIndex < sessionCount; ++sessionIndex) {
					IAudioSessionControl *control = nullptr;
					IAudioSessionControl2 *control2 = nullptr;
					ISimpleAudioVolume *audio = nullptr;
					DWORD ownerProcessId = 0;
					if (SUCCEEDED(sessions->GetSession(sessionIndex, &control)) && control
						&& SUCCEEDED(control->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast< void ** >(&control2)))
						&& control2 && SUCCEEDED(control2->GetProcessId(&ownerProcessId))
						&& ownerProcessId == static_cast< DWORD >(processId)
						&& SUCCEEDED(control2->QueryInterface(__uuidof(ISimpleAudioVolume), reinterpret_cast< void ** >(&audio)))
						&& audio) {
						sessionFound = true;
						const float volume = std::clamp(volumeLevel, 0.0f, 1.0f);
						const bool sessionApplied = SUCCEEDED(audio->SetMasterVolume(volume, nullptr))
							  && SUCCEEDED(audio->SetMute(muted ? TRUE : FALSE, nullptr));
						applied = applied || sessionApplied;
					}
					releaseCom(audio); releaseCom(control2); releaseCom(control);
				}
			}
			releaseCom(sessions); releaseCom(manager); releaseCom(device);
		}
	}
	releaseCom(devices); releaseCom(deviceEnumerator);
	if (uninitialize) CoUninitialize();
	if (applied) return { true, {} };
	if (!enumeratorReady) return { false, QStringLiteral("device-enumerator-unavailable") };
	if (!endpointsReady) return { false, QStringLiteral("render-endpoints-unavailable") };
	return { false, sessionFound ? QStringLiteral("apply-failed") : QStringLiteral("session-not-found") };
}
#endif
}

class ScreenShareAudioControlWorker final : public QObject {
	Q_OBJECT
public slots:
	void apply(const quint64 generation, const qint64 processId, const bool muted, const float volumeLevel) {
#ifdef Q_OS_WIN
		const AudioControlResult result = setProcessAudioControls(processId, muted, volumeLevel);
		emit completed(generation, processId, result.applied, result.errorCode);
#else
		Q_UNUSED(muted);
		Q_UNUSED(volumeLevel);
		emit completed(generation, processId, true, {});
#endif
	}
	void shutdown() { QThread::currentThread()->quit(); }

signals:
	void completed(quint64 generation, qint64 processId, bool applied, const QString &errorCode);
};

ScreenShareViewBackend::ScreenShareViewBackend(const ScreenShareSession &session, QObject *parent)
	: QObject(parent), m_session(session), m_status(tr("Waiting for the live viewer to start.")) {
	m_windowPollTimer = new QTimer(this);
	m_windowPollTimer->setInterval(200);
	connect(m_windowPollTimer, &QTimer::timeout, this, &ScreenShareViewBackend::pollForVideoWindow);
	m_audioRetryTimer = new QTimer(this);
	m_audioRetryTimer->setInterval(500);
	m_audioRetryTimer->setSingleShot(true);
	connect(m_audioRetryTimer, &QTimer::timeout, this, &ScreenShareViewBackend::retryAudioControls);
	m_audioDispatchTimer = new QTimer(this);
	m_audioDispatchTimer->setSingleShot(true);
	connect(m_audioDispatchTimer, &QTimer::timeout, this, &ScreenShareViewBackend::dispatchAudioControls);
#ifdef Q_OS_WIN
	// Windows Core Audio session discovery can enumerate every active endpoint
	// and session. Keep it entirely off the GUI thread, and only ever queue the
	// latest desired mute/volume state to this one worker.
	m_audioThread = new QThread;
	m_audioWorker = new ScreenShareAudioControlWorker();
	m_audioWorker->moveToThread(m_audioThread);
	connect(m_audioWorker, &QObject::destroyed, this, [this]() { m_audioWorker = nullptr; });
	connect(m_audioThread, &QObject::destroyed, this, [this]() { m_audioThread = nullptr; });
	connect(m_audioThread, &QThread::finished, m_audioWorker, &QObject::deleteLater);
	connect(m_audioThread, &QThread::finished, m_audioThread, &QObject::deleteLater);
	connect(m_audioWorker, &ScreenShareAudioControlWorker::completed, this,
			&ScreenShareViewBackend::handleAudioControlsApplied);
	m_audioThread->start();
#endif
	// The frame reader owns a dedicated event loop. It tears itself down
	// asynchronously so closing a viewer never waits for a 60 Hz worker on the
	// GUI thread.
	m_frameThread = new QThread;
	m_frameReader = new ScreenShareNativeFrameReader();
	m_frameReader->moveToThread(m_frameThread);
	connect(m_frameThread, &QThread::finished, m_frameReader, &QObject::deleteLater);
	connect(m_frameThread, &QThread::finished, m_frameThread, &QObject::deleteLater);
	connect(m_frameReader, &ScreenShareNativeFrameReader::activeChanged, this, [this](const bool active) {
		if (active && !m_acceptNativeFrames) return;
		if (m_nativeFrameActive == active) return;
		m_nativeFrameActive = active;
		emit nativeFrameActiveChanged();
		setStatus(active ? tr("Connecting to the native Qt Quick video surface...")
						 : tr("The native frame transport is unavailable; using the external viewer fallback."));
	});
	connect(m_frameReader, &ScreenShareNativeFrameReader::frameReady, this,
			[this](const QImage &frame, const quint64 generation, const quint64 sequence, qint64) {
				// A frame can already be queued for delivery when the GUI thread
				// pauses or replaces the transport. Pause intentionally retains the
				// last frame and transport identity, but must not advance presentation.
				if (m_acceptNativeFrames && !m_paused && generation == m_nativeFrameGeneration) {
					m_currentFrame = frame;
					emit frameChanged();
					setStatus(tr("Live via native Qt Quick frame transport."));
				}
				QMetaObject::invokeMethod(m_frameReader, "acknowledgeFrame", Qt::QueuedConnection,
								  Q_ARG(quint64, generation), Q_ARG(quint64, sequence));
			});
	m_frameThread->start();
}

ScreenShareViewBackend::~ScreenShareViewBackend() {
	clearVideoWindow();
	if (m_audioWorker && m_audioThread && m_audioThread->isRunning()) {
		disconnect(m_audioWorker, nullptr, this, nullptr);
		QMetaObject::invokeMethod(m_audioWorker, "shutdown", Qt::QueuedConnection);
	}
	m_audioWorker = nullptr;
	m_audioThread = nullptr;
	if (m_frameReader && m_frameThread && m_frameThread->isRunning()) {
		disconnect(m_frameReader, nullptr, this, nullptr);
		QMetaObject::invokeMethod(m_frameReader, "shutdown", Qt::QueuedConnection);
	}
	m_frameReader = nullptr;
	m_frameThread = nullptr;
}
QString ScreenShareViewBackend::streamId() const { return m_session.streamID; }
QString ScreenShareViewBackend::ownerLabel() const {
	return m_ownerLabel.isEmpty() ? tr("Session %1").arg(m_session.ownerSession) : m_ownerLabel;
}
QString ScreenShareViewBackend::roomLabel() const {
	if (!m_roomLabel.isEmpty()) return m_roomLabel;
	return m_session.scope == MumbleProto::ScreenShareScopeChannel
		? tr("Voice room %1").arg(m_session.scopeID) : QString();
}
QString ScreenShareViewBackend::title() const { return tr("%1's screen share").arg(ownerLabel()); }
QString ScreenShareViewBackend::detail() const {
	return roomLabel().isEmpty() ? ownerLabel() : tr("%1 · %2").arg(ownerLabel(), roomLabel());
}
QString ScreenShareViewBackend::status() const { return m_status; }
bool ScreenShareViewBackend::paused() const { return m_paused; }
bool ScreenShareViewBackend::audioMuted() const { return m_audioMuted; }
bool ScreenShareViewBackend::audioAvailable() const { return m_session.captureAudio; }
int ScreenShareViewBackend::audioVolume() const { return m_audioVolume; }
QString ScreenShareViewBackend::audioControlStatus() const { return m_audioControlStatus; }
QString ScreenShareViewBackend::audioControlError() const { return m_audioControlError; }
qint64 ScreenShareViewBackend::processId() const { return m_processId; }
QWindow *ScreenShareViewBackend::videoWindow() const { return m_videoWindow; }
QString ScreenShareViewBackend::renderTransport() const {
	return nativeFrameActive() ? QStringLiteral("native-shared-memory-bgra") : QStringLiteral("external-process-window");
}
bool ScreenShareViewBackend::nativeFrameTransportAvailable() const { return true; }
QString ScreenShareViewBackend::nativeFrameTransportBlocker() const {
	return {};
}
bool ScreenShareViewBackend::nativeFrameActive() const { return m_nativeFrameActive; }
QImage ScreenShareViewBackend::currentFrame() const { return m_currentFrame; }
bool ScreenShareViewBackend::hasCurrentFrame() const { return !m_currentFrame.isNull(); }
QString ScreenShareViewBackend::operationStatus() const { return m_operationStatus; }
QString ScreenShareViewBackend::operationError() const { return m_operationError; }
bool ScreenShareViewBackend::operationCancellable() const { return m_operationCancellable; }

void ScreenShareViewBackend::setOperationState(const QString &status, const QString &error, const bool cancellable) {
	if (m_operationStatus == status && m_operationError == error && m_operationCancellable == cancellable) return;
	m_operationStatus = status;
	m_operationError = error;
	m_operationCancellable = cancellable;
	emit operationStateChanged();
}

void ScreenShareViewBackend::setNativeFrameTransport(const QString &sharedMemoryKey, const quint64 generation) {
	m_acceptNativeFrames = !sharedMemoryKey.trimmed().isEmpty();
	m_nativeFrameGeneration = m_acceptNativeFrames ? generation : 0;
	m_currentFrame = {};
	emit frameChanged();
	QMetaObject::invokeMethod(m_frameReader, "configure", Qt::QueuedConnection, Q_ARG(QString, sharedMemoryKey),
						  Q_ARG(quint64, generation));
}

#if defined(MUMBLE_HAS_MODERN_UI_MOCKUPS) || defined(MUMBLE_HAS_MODERN_UI_AUTOMATION)
void ScreenShareViewBackend::setVisualFixtureFrame(const QImage &frame) {
	if (frame.isNull()) return;
	m_currentFrame = frame.convertToFormat(QImage::Format_ARGB32);
	if (!m_nativeFrameActive) {
		m_nativeFrameActive = true;
		emit nativeFrameActiveChanged();
	}
	emit frameChanged();
	setStatus(tr("Live via native Qt Quick frame transport."));
}
#endif

void ScreenShareViewBackend::updateSession(const ScreenShareSession &session) {
	const bool audioCapabilityChanged = m_session.captureAudio != session.captureAudio;
	m_session = session;
	emit sessionChanged();
	if (audioCapabilityChanged) {
		m_audioRetryAttempts = 0;
		m_audioRetryTimer->stop();
		scheduleAudioControls(0);
	}
}

void ScreenShareViewBackend::setIdentity(const QString &ownerLabel, const QString &roomLabel) {
	const QString normalizedOwner = ownerLabel.trimmed();
	const QString normalizedRoom  = roomLabel.trimmed();
	if (m_ownerLabel == normalizedOwner && m_roomLabel == normalizedRoom) return;
	m_ownerLabel = normalizedOwner;
	m_roomLabel  = normalizedRoom;
	emit sessionChanged();
}

void ScreenShareViewBackend::setProcessId(const qint64 processId) {
	if (m_processId == processId) return;
	m_processId = processId;
	clearVideoWindow();
	emit processIdChanged();
	m_audioRetryTimer->stop();
	m_audioDispatchTimer->stop();
	m_audioRetryAttempts = 0;
	m_audioApplyPending = false;
	if (processId > 0) {
		setStatus(m_paused ? tr("Paused locally. The receiver remains connected on the current frame.")
						   : tr("Connecting to the GStreamer video surface..."));
		m_windowPollTimer->start();
		scheduleAudioControls(0);
	} else {
		m_windowPollTimer->stop();
		setAudioControlState(QStringLiteral("idle"));
		setStatus(m_paused ? tr("Paused locally. Resume returns to the live edge.")
						   : tr("Waiting for the live viewer to start."));
	}
}

void ScreenShareViewBackend::setPaused(const bool paused) {
	if (m_paused == paused) return;
	m_paused = paused;
	if (paused) {
		setStatus(tr("Paused locally. The receiver remains connected on the current frame."));
	} else if (m_nativeFrameActive && hasCurrentFrame()) {
		setStatus(tr("Live via native Qt Quick frame transport."));
	} else if (m_videoWindow) {
		setStatus(tr("Live via GStreamer."));
	} else if (m_processId > 0) {
		setStatus(tr("Resuming the live video surface..."));
		m_windowPollTimer->start();
	}
	// Pause is local presentation state. The receiver, process identity,
	// shared-memory transport, and last frame stay alive so resume never needs a
	// helper teardown/reconnect. Audio is temporarily silenced without changing
	// the viewer's persisted mute preference.
	m_audioRetryAttempts = 0;
	m_audioRetryTimer->stop();
	scheduleAudioControls(0);
	emit pausedChanged();
	emit pauseToggled(streamId(), paused);
}

void ScreenShareViewBackend::setAudioMuted(const bool muted) {
	if (m_audioMuted == muted) return;
	m_audioMuted = muted;
	emit audioMutedChanged();
	emit audioMuteToggled(streamId(), muted);
	m_audioRetryAttempts = 0;
	m_audioRetryTimer->stop();
	scheduleAudioControls();
}

void ScreenShareViewBackend::setAudioVolume(const int percent) {
	const int value = qBound(0, percent, 100);
	if (m_audioVolume == value) return;
	m_audioVolume = value;
	emit audioVolumeChanged();
	emit audioVolumeAdjusted(streamId(), value);
	m_audioRetryAttempts = 0;
	m_audioRetryTimer->stop();
	scheduleAudioControls();
}

void ScreenShareViewBackend::requestRetry() { emit retryRequested(streamId()); }
void ScreenShareViewBackend::requestStop() { emit stopRequested(streamId()); }
void ScreenShareViewBackend::requestClose() { emit closeRequested(streamId()); }

void ScreenShareViewBackend::pollForVideoWindow() {
#ifdef Q_OS_WIN
	const HWND handle = externalVideoWindow(m_processId);
	if (!handle) return;
	m_windowPollTimer->stop();
	m_videoWindow = QWindow::fromWinId(reinterpret_cast< WId >(handle));
	if (!m_videoWindow) {
		setStatus(tr("The GStreamer window could not be attached to Qt Quick."));
		return;
	}
	emit videoWindowChanged();
	setStatus(tr("Live via GStreamer."));
#else
	m_windowPollTimer->stop();
	setStatus(tr("The GStreamer viewer is running in its platform video window."));
#endif
}

void ScreenShareViewBackend::retryAudioControls() {
	scheduleAudioControls(0);
}

void ScreenShareViewBackend::scheduleAudioControls(const int delayMsec) {
	if (!audioAvailable() || m_processId <= 0) {
		m_audioApplyPending = false;
		m_audioDispatchTimer->stop();
		m_audioRetryTimer->stop();
		setAudioControlState(QStringLiteral("idle"));
		return;
	}
#ifndef Q_OS_WIN
	Q_UNUSED(delayMsec);
	setAudioControlState(QStringLiteral("ready"));
	return;
#else
	m_audioApplyPending = true;
	setAudioControlState(QStringLiteral("pending"));
	if (m_audioApplyInFlight) return;
	m_audioDispatchTimer->start(qMax(0, delayMsec));
#endif
}

void ScreenShareViewBackend::dispatchAudioControls() {
#ifdef Q_OS_WIN
	if (m_audioApplyInFlight) return;
	if (!audioAvailable() || m_processId <= 0) {
		m_audioApplyPending = false;
		setAudioControlState(QStringLiteral("idle"));
		return;
	}
	if (!m_audioWorker || !m_audioThread) {
		m_audioApplyPending = false;
		setAudioControlState(QStringLiteral("error"),
						  tr("Windows audio controls are unavailable for this viewer."));
		return;
	}

	m_audioApplyPending = false;
	m_audioApplyInFlight = true;
	const quint64 generation = ++m_audioControlGeneration;
	const qint64 processId = m_processId;
	const bool effectiveMuted = m_audioMuted || m_paused;
	setAudioControlState(QStringLiteral("applying"));
#ifdef MUMBLE_HAS_MODERN_UI_MOCKUPS
	emit audioControlsDispatchedForTest(processId, effectiveMuted, m_audioVolume);
#endif
	const bool queued = QMetaObject::invokeMethod(
		m_audioWorker, "apply", Qt::QueuedConnection, Q_ARG(quint64, generation), Q_ARG(qint64, processId),
		Q_ARG(bool, effectiveMuted), Q_ARG(float, static_cast< float >(m_audioVolume) / 100.0f));
	if (!queued) {
		m_audioApplyInFlight = false;
		setAudioControlState(QStringLiteral("error"),
						  tr("Windows audio controls could not queue the requested change."));
	}
#endif
}

void ScreenShareViewBackend::handleAudioControlsApplied(const quint64 generation, const qint64 processId,
												 const bool applied, const QString &errorCode) {
#ifdef Q_OS_WIN
	if (!m_audioApplyInFlight || generation != m_audioControlGeneration) return;
	m_audioApplyInFlight = false;

	// A slider move, mute toggle, pause transition, or replacement helper may
	// have superseded the values that were just applied. Never queue every
	// intermediate value: dispatch only the latest desired state.
	if (m_audioApplyPending || processId != m_processId) {
		if (audioAvailable() && m_processId > 0) {
			scheduleAudioControls(0);
		} else {
			setAudioControlState(QStringLiteral("idle"));
		}
		return;
	}
	if (!audioAvailable() || m_processId <= 0) {
		setAudioControlState(QStringLiteral("idle"));
		return;
	}

	if (applied) {
		m_audioRetryAttempts = 0;
		m_audioRetryTimer->stop();
		setAudioControlState(QStringLiteral("ready"));
		return;
	}

	++m_audioRetryAttempts;
	const QString error = audioControlErrorForCode(errorCode);
	if (m_audioRetryAttempts < 40 && m_processId > 0) {
		setAudioControlState(QStringLiteral("retrying"), error);
		m_audioRetryTimer->start();
		return;
	}
	setAudioControlState(QStringLiteral("error"), error);
#else
	Q_UNUSED(generation);
	Q_UNUSED(processId);
	Q_UNUSED(applied);
	Q_UNUSED(errorCode);
#endif
}

void ScreenShareViewBackend::setAudioControlState(const QString &status, const QString &error) {
	if (m_audioControlStatus == status && m_audioControlError == error) return;
	m_audioControlStatus = status;
	m_audioControlError = error;
	emit audioControlStateChanged();
}

QString ScreenShareViewBackend::audioControlErrorForCode(const QString &errorCode) const {
	if (errorCode == QStringLiteral("session-not-found")) {
		return tr("Waiting for the viewer's Windows audio session.");
	}
	if (errorCode == QStringLiteral("apply-failed")) {
		return tr("Windows could not apply mute or volume to the viewer audio session.");
	}
	if (errorCode == QStringLiteral("com-initialization-failed")) {
		return tr("Windows audio controls could not initialize.");
	}
	if (errorCode == QStringLiteral("device-enumerator-unavailable")
		|| errorCode == QStringLiteral("render-endpoints-unavailable")) {
		return tr("Windows audio output devices are currently unavailable.");
	}
	return tr("Windows audio controls are unavailable for this viewer.");
}

void ScreenShareViewBackend::clearVideoWindow() {
	if (!m_videoWindow) return;
	delete m_videoWindow.data();
	m_videoWindow = nullptr;
	emit videoWindowChanged();
}

void ScreenShareViewBackend::setStatus(const QString &status) {
	if (m_status == status) return;
	m_status = status;
	emit statusChanged();
}

#include "ScreenShareViewBackend.moc"
