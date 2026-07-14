// Copyright The Mumble Developers. All rights reserved.

#include "ScreenShareViewBackend.h"
#include "ScreenShareFrameTransport.h"

#include <QtCore/QThread>
#include <QtCore/QTimer>
#include <QtGui/QWindow>

#include <algorithm>

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
		m_generation = generation;
		const bool active = !key.trimmed().isEmpty() && m_transport.attach(key);
		if (active) m_timer->start();
		emit activeChanged(active);
	}
	void shutdown() {
		m_timer->stop();
		m_transport.detach();
		m_generation = 0;
		QThread::currentThread()->quit();
	}
	void poll() {
		Mumble::ScreenShare::NativeFrame frame;
		if (!m_transport.readLatest(&frame) || frame.generation != m_generation) return;
		const QImage wrapped(reinterpret_cast< const uchar * >(frame.bgra.constData()), static_cast< int >(frame.width),
						 static_cast< int >(frame.height), static_cast< qsizetype >(frame.stride), QImage::Format_ARGB32);
		if (!wrapped.isNull()) emit frameReady(wrapped.copy(), frame.sequence, frame.timestampUsec);
	}
signals:
	void activeChanged(bool active);
	void frameReady(const QImage &frame, quint64 sequence, qint64 timestampUsec);
private:
	QTimer *m_timer;
	Mumble::ScreenShare::FrameTransport m_transport;
	quint64 m_generation = 0;
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

bool setProcessAudioControls(const qint64 processId, const bool muted, const float volumeLevel) {
	if (processId <= 0 || processId > std::numeric_limits< DWORD >::max()) return false;
	const HRESULT initialized = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
	const bool uninitialize = SUCCEEDED(initialized);
	IMMDeviceEnumerator *deviceEnumerator = nullptr;
	IMMDeviceCollection *devices = nullptr;
	bool matched = false;
	if (SUCCEEDED(CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
								 __uuidof(IMMDeviceEnumerator), reinterpret_cast< void ** >(&deviceEnumerator)))
		&& deviceEnumerator
		&& SUCCEEDED(deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices)) && devices) {
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
						const float volume = std::clamp(volumeLevel, 0.0f, 1.0f);
						matched = SUCCEEDED(audio->SetMasterVolume(volume, nullptr))
							  && SUCCEEDED(audio->SetMute(muted ? TRUE : FALSE, nullptr));
					}
					releaseCom(audio); releaseCom(control2); releaseCom(control);
				}
			}
			releaseCom(sessions); releaseCom(manager); releaseCom(device);
		}
	}
	releaseCom(devices); releaseCom(deviceEnumerator);
	if (uninitialize) CoUninitialize();
	return matched;
}
#endif
}

ScreenShareViewBackend::ScreenShareViewBackend(const ScreenShareSession &session, QObject *parent)
	: QObject(parent), m_session(session), m_status(tr("Waiting for the live viewer to start.")) {
	m_windowPollTimer = new QTimer(this);
	m_windowPollTimer->setInterval(200);
	connect(m_windowPollTimer, &QTimer::timeout, this, &ScreenShareViewBackend::pollForVideoWindow);
	m_audioRetryTimer = new QTimer(this);
	m_audioRetryTimer->setInterval(500);
	connect(m_audioRetryTimer, &QTimer::timeout, this, &ScreenShareViewBackend::retryAudioControls);
	// The frame reader owns a dedicated event loop. It tears itself down
	// asynchronously so closing a viewer never waits for a 60 Hz worker on the
	// GUI thread.
	m_frameThread = new QThread;
	m_frameReader = new ScreenShareNativeFrameReader();
	m_frameReader->moveToThread(m_frameThread);
	connect(m_frameThread, &QThread::finished, m_frameReader, &QObject::deleteLater);
	connect(m_frameThread, &QThread::finished, m_frameThread, &QObject::deleteLater);
	connect(m_frameReader, &ScreenShareNativeFrameReader::activeChanged, this, [this](const bool active) {
		if (m_nativeFrameActive == active) return;
		m_nativeFrameActive = active;
		emit nativeFrameActiveChanged();
		setStatus(active ? tr("Connecting to the native Qt Quick video surface...")
						 : tr("The native frame transport is unavailable; using the external viewer fallback."));
	});
	connect(m_frameReader, &ScreenShareNativeFrameReader::frameReady, this,
			[this](const QImage &frame, quint64, qint64) {
				m_currentFrame = frame;
				emit frameChanged();
				setStatus(tr("Live via native Qt Quick frame transport."));
			});
	m_frameThread->start();
}

ScreenShareViewBackend::~ScreenShareViewBackend() {
	clearVideoWindow();
	if (m_frameReader && m_frameThread && m_frameThread->isRunning()) {
		disconnect(m_frameReader, nullptr, this, nullptr);
		QMetaObject::invokeMethod(m_frameReader, "shutdown", Qt::QueuedConnection);
	}
	m_frameReader = nullptr;
	m_frameThread = nullptr;
}
QString ScreenShareViewBackend::streamId() const { return m_session.streamID; }
QString ScreenShareViewBackend::title() const { return tr("Live screen share"); }
QString ScreenShareViewBackend::detail() const {
	return tr("Session %1").arg(m_session.ownerSession);
}
QString ScreenShareViewBackend::status() const { return m_status; }
bool ScreenShareViewBackend::paused() const { return m_paused; }
bool ScreenShareViewBackend::audioMuted() const { return m_audioMuted; }
bool ScreenShareViewBackend::audioAvailable() const { return m_session.captureAudio; }
int ScreenShareViewBackend::audioVolume() const { return m_audioVolume; }
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
	m_currentFrame = {};
	emit frameChanged();
	QMetaObject::invokeMethod(m_frameReader, "configure", Qt::QueuedConnection, Q_ARG(QString, sharedMemoryKey),
						  Q_ARG(quint64, generation));
}

void ScreenShareViewBackend::updateSession(const ScreenShareSession &session) {
	m_session = session;
	emit sessionChanged();
}

void ScreenShareViewBackend::setProcessId(const qint64 processId) {
	if (m_processId == processId) return;
	m_processId = processId;
	clearVideoWindow();
	emit processIdChanged();
	if (processId > 0 && !m_paused) {
		setStatus(tr("Connecting to the GStreamer video surface..."));
		m_windowPollTimer->start();
		m_audioRetryAttempts = 0;
		if (!applyAudioControls()) m_audioRetryTimer->start();
	} else {
		m_windowPollTimer->stop();
		m_audioRetryTimer->stop();
		setStatus(m_paused ? tr("Paused locally. Resume returns to the live edge.")
						   : tr("Waiting for the live viewer to start."));
	}
}

void ScreenShareViewBackend::setPaused(const bool paused) {
	if (m_paused == paused) return;
	m_paused = paused;
	emit pausedChanged();
	emit pauseToggled(streamId(), paused);
}

void ScreenShareViewBackend::setAudioMuted(const bool muted) {
	if (m_audioMuted == muted) return;
	m_audioMuted = muted;
	emit audioMutedChanged();
	emit audioMuteToggled(streamId(), muted);
	if (!applyAudioControls() && m_processId > 0) m_audioRetryTimer->start();
}

void ScreenShareViewBackend::setAudioVolume(const int percent) {
	const int value = qBound(0, percent, 100);
	if (m_audioVolume == value) return;
	m_audioVolume = value;
	emit audioVolumeChanged();
	if (!applyAudioControls() && m_processId > 0) m_audioRetryTimer->start();
}

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
	if (applyAudioControls() || ++m_audioRetryAttempts >= 40) m_audioRetryTimer->stop();
}


bool ScreenShareViewBackend::applyAudioControls() {
#ifdef Q_OS_WIN
	if (!audioAvailable() || m_processId <= 0) return true;
	return setProcessAudioControls(m_processId, m_audioMuted, static_cast< float >(m_audioVolume) / 100.0f);
#else
	return true;
#endif
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
