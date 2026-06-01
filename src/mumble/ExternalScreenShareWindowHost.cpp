// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ExternalScreenShareWindowHost.h"

#include "ClientUser.h"
#include "Global.h"
#include "Log.h"
#include "ScreenShare.h"
#include "UiTheme.h"

#ifdef Q_OS_WIN
#	include "win.h"

#	include <mmdeviceapi.h>
#	include <audiopolicy.h>
#endif

#include <QtCore/QEvent>
#include <QtCore/QPoint>
#include <QtCore/QTimer>
#include <QtGui/QCloseEvent>
#include <QtGui/QPalette>
#include <QtGui/QResizeEvent>
#include <QtWidgets/QApplication>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QSlider>
#include <QtWidgets/QStyle>
#include <QtWidgets/QToolButton>
#include <QtWidgets/QVBoxLayout>

#include <algorithm>
#include <limits>
#include <optional>

namespace {
#ifdef Q_OS_WIN
enum class ProcessAudioControlResult { Applied, NoSession, Failed };

struct ExternalWindowSearch {
	DWORD processID = 0;
	HWND window     = nullptr;
};

template< typename T > void releaseComObject(T *&object) {
	if (object) {
		object->Release();
		object = nullptr;
	}
}

class ScopedComApartment {
public:
	ScopedComApartment() {
		const HRESULT hr = CoInitializeEx(nullptr, COINIT_MULTITHREADED);
		m_usable         = SUCCEEDED(hr) || hr == RPC_E_CHANGED_MODE;
		m_uninitialize   = SUCCEEDED(hr);
	}

	~ScopedComApartment() {
		if (m_uninitialize) {
			CoUninitialize();
		}
	}

	bool usable() const { return m_usable; }

private:
	bool m_usable       = false;
	bool m_uninitialize = false;
};

BOOL CALLBACK enumExternalProcessWindows(HWND hwnd, LPARAM userData) {
	auto *search = reinterpret_cast< ExternalWindowSearch * >(userData);
	if (!search || !hwnd || !IsWindowVisible(hwnd) || GetAncestor(hwnd, GA_ROOT) != hwnd) {
		return TRUE;
	}

	DWORD windowProcessID = 0;
	GetWindowThreadProcessId(hwnd, &windowProcessID);
	if (windowProcessID != search->processID) {
		return TRUE;
	}

	RECT rect = {};
	if (!GetWindowRect(hwnd, &rect) || rect.right <= rect.left || rect.bottom <= rect.top) {
		return TRUE;
	}

	search->window = hwnd;
	return FALSE;
}

bool externalProcessIsRunning(const qint64 processID) {
	if (processID <= 0 || processID > std::numeric_limits< DWORD >::max()) {
		return false;
	}

	HANDLE process = OpenProcess(SYNCHRONIZE, FALSE, static_cast< DWORD >(processID));
	if (!process) {
		return false;
	}

	const DWORD waitResult = WaitForSingleObject(process, 0);
	CloseHandle(process);
	return waitResult == WAIT_TIMEOUT;
}

HWND findExternalProcessWindow(const qint64 processID) {
	if (processID <= 0 || processID > std::numeric_limits< DWORD >::max()) {
		return nullptr;
	}

	ExternalWindowSearch search;
	search.processID = static_cast< DWORD >(processID);
	EnumWindows(enumExternalProcessWindows, reinterpret_cast< LPARAM >(&search));
	return search.window;
}

bool setControlsForAudioSession(IAudioSessionControl *control, const DWORD processID, const bool muted,
								const float volumeLevel, bool *matchedSession) {
	if (!control || !matchedSession) {
		return false;
	}

	IAudioSessionControl2 *control2 = nullptr;
	HRESULT hr = control->QueryInterface(__uuidof(IAudioSessionControl2), reinterpret_cast< void ** >(&control2));
	if (FAILED(hr) || !control2) {
		return false;
	}

	DWORD sessionProcessID = 0;
	hr = control2->GetProcessId(&sessionProcessID);
	if (FAILED(hr) || sessionProcessID != processID) {
		releaseComObject(control2);
		return true;
	}

	AudioSessionState state = AudioSessionStateExpired;
	if (FAILED(control2->GetState(&state)) || state == AudioSessionStateExpired) {
		releaseComObject(control2);
		return true;
	}

	*matchedSession = true;
	ISimpleAudioVolume *volume = nullptr;
	hr = control2->QueryInterface(__uuidof(ISimpleAudioVolume), reinterpret_cast< void ** >(&volume));
	if (FAILED(hr) || !volume) {
		releaseComObject(control2);
		return false;
	}

	const float clampedVolume = std::clamp(volumeLevel, 0.0f, 1.0f);
	const HRESULT volumeHr   = volume->SetMasterVolume(clampedVolume, nullptr);
	const HRESULT muteHr     = volume->SetMute(muted ? TRUE : FALSE, nullptr);
	releaseComObject(volume);
	releaseComObject(control2);
	return SUCCEEDED(volumeHr) && SUCCEEDED(muteHr);
}

bool setControlsForAudioDevice(IMMDevice *device, const DWORD processID, const bool muted, const float volumeLevel,
							   bool *matchedSession) {
	if (!device || !matchedSession) {
		return false;
	}

	IAudioSessionManager2 *sessionManager = nullptr;
	HRESULT hr = device->Activate(__uuidof(IAudioSessionManager2), CLSCTX_ALL, nullptr,
								  reinterpret_cast< void ** >(&sessionManager));
	if (FAILED(hr) || !sessionManager) {
		return true;
	}

	IAudioSessionEnumerator *sessionEnumerator = nullptr;
	hr                                        = sessionManager->GetSessionEnumerator(&sessionEnumerator);
	if (FAILED(hr) || !sessionEnumerator) {
		releaseComObject(sessionManager);
		return true;
	}

	int sessionCount = 0;
	bool ok          = true;
	if (SUCCEEDED(sessionEnumerator->GetCount(&sessionCount))) {
		for (int i = 0; i < sessionCount; ++i) {
			IAudioSessionControl *sessionControl = nullptr;
			hr                                  = sessionEnumerator->GetSession(i, &sessionControl);
			if (FAILED(hr) || !sessionControl) {
				continue;
			}

			ok = setControlsForAudioSession(sessionControl, processID, muted, volumeLevel, matchedSession) && ok;
			releaseComObject(sessionControl);
		}
	}

	releaseComObject(sessionEnumerator);
	releaseComObject(sessionManager);
	return ok;
}

ProcessAudioControlResult setControlsForProcessAudioSessions(const qint64 processID, const bool muted,
															 const float volumeLevel) {
	if (processID <= 0 || processID > std::numeric_limits< DWORD >::max()) {
		return ProcessAudioControlResult::Failed;
	}

	ScopedComApartment com;
	if (!com.usable()) {
		return ProcessAudioControlResult::Failed;
	}

	IMMDeviceEnumerator *deviceEnumerator = nullptr;
	HRESULT hr = CoCreateInstance(__uuidof(MMDeviceEnumerator), nullptr, CLSCTX_ALL,
								  __uuidof(IMMDeviceEnumerator), reinterpret_cast< void ** >(&deviceEnumerator));
	if (FAILED(hr) || !deviceEnumerator) {
		return ProcessAudioControlResult::Failed;
	}

	IMMDeviceCollection *devices = nullptr;
	hr = deviceEnumerator->EnumAudioEndpoints(eRender, DEVICE_STATE_ACTIVE, &devices);
	if (FAILED(hr) || !devices) {
		releaseComObject(deviceEnumerator);
		return ProcessAudioControlResult::Failed;
	}

	UINT deviceCount = 0;
	bool matched     = false;
	bool ok          = true;
	if (SUCCEEDED(devices->GetCount(&deviceCount))) {
		for (UINT i = 0; i < deviceCount; ++i) {
			IMMDevice *device = nullptr;
			hr                = devices->Item(i, &device);
			if (FAILED(hr) || !device) {
				continue;
			}

			ok = setControlsForAudioDevice(device, static_cast< DWORD >(processID), muted, volumeLevel, &matched)
				 && ok;
			releaseComObject(device);
		}
	}

	releaseComObject(devices);
	releaseComObject(deviceEnumerator);

	if (!matched) {
		return ProcessAudioControlResult::NoSession;
	}
	return ok ? ProcessAudioControlResult::Applied : ProcessAudioControlResult::Failed;
}

void attachExternalWindowToSurface(HWND window, QWidget *surfaceWidget) {
	if (!window || !surfaceWidget) {
		return;
	}

	const HWND ownerWindow = reinterpret_cast< HWND >(surfaceWidget->window()->winId());

	LONG_PTR style = GetWindowLongPtr(window, GWL_STYLE);
	style &= ~(WS_CAPTION | WS_THICKFRAME | WS_SYSMENU | WS_MINIMIZEBOX | WS_MAXIMIZEBOX | WS_CHILD);
	style |= WS_POPUP | WS_VISIBLE | WS_CLIPSIBLINGS | WS_CLIPCHILDREN;
	SetWindowLongPtr(window, GWL_STYLE, style);

	if (ownerWindow) {
		SetWindowLongPtr(window, GWLP_HWNDPARENT, reinterpret_cast< LONG_PTR >(ownerWindow));
	}

	SetWindowPos(window, HWND_TOP, 0, 0, surfaceWidget->width(), surfaceWidget->height(),
				 SWP_NOMOVE | SWP_NOACTIVATE | SWP_FRAMECHANGED | SWP_SHOWWINDOW);
}

void moveExternalWindowToSurface(HWND window, QWidget *surfaceWidget) {
	if (!window || !surfaceWidget) {
		return;
	}

	if (!surfaceWidget->isVisible() || surfaceWidget->window()->isMinimized()) {
		ShowWindow(window, SW_HIDE);
		return;
	}

	const QPoint topLeft = surfaceWidget->mapToGlobal(QPoint(0, 0));
	SetWindowPos(window, HWND_TOP, topLeft.x(), topLeft.y(), std::max(1, surfaceWidget->width()),
				 std::max(1, surfaceWidget->height()), SWP_NOACTIVATE | SWP_SHOWWINDOW);
}
#else
bool externalProcessIsRunning(const qint64 processID) {
	return processID > 0;
}
#endif

QColor fallbackPaletteColor(const QPalette &palette, const QPalette::ColorRole role, const QColor &fallback) {
	const QColor color = palette.color(role);
	return color.isValid() ? color : fallback;
}
} // namespace

ExternalScreenShareWindowHost::ExternalScreenShareWindowHost(const ScreenShareSession &session, QWidget *parent)
	: QWidget(parent, Qt::Window), m_session(session) {
	setAttribute(Qt::WA_DeleteOnClose, true);
	setWindowTitle(windowTitle());
	setMinimumSize(860, 540);

	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->setSpacing(0);

	m_rootFrame = new QFrame(this);
	m_rootFrame->setObjectName(QStringLiteral("qfScreenShareExternalWindow"));
	m_layout->addWidget(m_rootFrame);

	auto *rootLayout = new QVBoxLayout(m_rootFrame);
	rootLayout->setContentsMargins(12, 12, 12, 12);
	rootLayout->setSpacing(10);

	m_headerFrame = new QFrame(m_rootFrame);
	m_headerFrame->setObjectName(QStringLiteral("qfScreenShareExternalHeader"));
	auto *headerLayout = new QHBoxLayout(m_headerFrame);
	headerLayout->setContentsMargins(12, 10, 12, 10);
	headerLayout->setSpacing(10);

	auto *labelColumn = new QWidget(m_headerFrame);
	auto *labelLayout = new QVBoxLayout(labelColumn);
	labelLayout->setContentsMargins(0, 0, 0, 0);
	labelLayout->setSpacing(2);

	m_titleLabel = new QLabel(tr("Live screen share"), labelColumn);
	m_titleLabel->setObjectName(QStringLiteral("qlScreenShareExternalTitle"));
	m_detailLabel = new QLabel(labelColumn);
	m_detailLabel->setObjectName(QStringLiteral("qlScreenShareExternalDetail"));
	labelLayout->addWidget(m_titleLabel);
	labelLayout->addWidget(m_detailLabel);
	headerLayout->addWidget(labelColumn, 1);

	m_pauseButton = new QToolButton(m_headerFrame);
	m_pauseButton->setObjectName(QStringLiteral("qtbScreenShareExternalPause"));
	m_pauseButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	m_pauseButton->setAutoRaise(false);
	m_pauseButton->setIcon(style()->standardIcon(QStyle::SP_MediaPause));
	connect(m_pauseButton, &QToolButton::clicked, this, &ExternalScreenShareWindowHost::handlePauseButton);
	headerLayout->addWidget(m_pauseButton);

	m_audioButton = new QToolButton(m_headerFrame);
	m_audioButton->setObjectName(QStringLiteral("qtbScreenShareExternalAudio"));
	m_audioButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	m_audioButton->setAutoRaise(false);
	m_audioButton->setIcon(style()->standardIcon(QStyle::SP_MediaVolume));
	connect(m_audioButton, &QToolButton::clicked, this, &ExternalScreenShareWindowHost::handleAudioButton);
	headerLayout->addWidget(m_audioButton);

	auto *audioVolumeGroup = new QWidget(m_headerFrame);
	audioVolumeGroup->setObjectName(QStringLiteral("qwScreenShareExternalVolumeGroup"));
	auto *audioVolumeLayout = new QHBoxLayout(audioVolumeGroup);
	audioVolumeLayout->setContentsMargins(0, 0, 0, 0);
	audioVolumeLayout->setSpacing(6);

	m_audioVolumeSlider = new QSlider(Qt::Horizontal, audioVolumeGroup);
	m_audioVolumeSlider->setObjectName(QStringLiteral("qsScreenShareExternalVolume"));
	m_audioVolumeSlider->setAccessibleName(tr("Stream volume"));
	m_audioVolumeSlider->setRange(0, 100);
	m_audioVolumeSlider->setSingleStep(5);
	m_audioVolumeSlider->setPageStep(10);
	m_audioVolumeSlider->setFixedWidth(128);
	m_audioVolumeSlider->setValue(m_audioVolumePercent);
	m_audioVolumeSlider->setToolTip(tr("Adjust this viewer's stream audio volume."));
	connect(m_audioVolumeSlider, &QSlider::valueChanged, this,
			&ExternalScreenShareWindowHost::handleAudioVolumeChanged);
	audioVolumeLayout->addWidget(m_audioVolumeSlider);

	m_audioVolumeLabel = new QLabel(audioVolumeGroup);
	m_audioVolumeLabel->setObjectName(QStringLiteral("qlScreenShareExternalVolume"));
	m_audioVolumeLabel->setMinimumWidth(40);
	m_audioVolumeLabel->setAlignment(Qt::AlignRight | Qt::AlignVCenter);
	audioVolumeLayout->addWidget(m_audioVolumeLabel);
	headerLayout->addWidget(audioVolumeGroup);

	m_stopButton = new QToolButton(m_headerFrame);
	m_stopButton->setObjectName(QStringLiteral("qtbScreenShareExternalStop"));
	m_stopButton->setToolButtonStyle(Qt::ToolButtonTextBesideIcon);
	m_stopButton->setAutoRaise(false);
	m_stopButton->setIcon(style()->standardIcon(QStyle::SP_MediaStop));
	m_stopButton->setText(tr("Stop"));
	m_stopButton->setToolTip(tr("Stop watching this live screen share."));
	connect(m_stopButton, &QToolButton::clicked, this, [this]() { emit stopRequested(m_session.streamID); });
	headerLayout->addWidget(m_stopButton);

	rootLayout->addWidget(m_headerFrame);

	m_videoFrame = new QFrame(m_rootFrame);
	m_videoFrame->setObjectName(QStringLiteral("qfScreenShareExternalVideoFrame"));
	auto *videoLayout = new QVBoxLayout(m_videoFrame);
	videoLayout->setContentsMargins(1, 1, 1, 1);
	videoLayout->setSpacing(0);

	m_videoSurface = new QWidget(m_videoFrame);
	m_videoSurface->setObjectName(QStringLiteral("qwScreenShareExternalVideoSurface"));
	m_videoSurface->setAttribute(Qt::WA_NativeWindow, true);
	m_videoSurface->setAttribute(Qt::WA_DontCreateNativeAncestors, false);
	videoLayout->addWidget(m_videoSurface, 1);

	m_placeholderLabel = new QLabel(m_videoSurface);
	m_placeholderLabel->setObjectName(QStringLiteral("qlScreenShareExternalPlaceholder"));
	m_placeholderLabel->setAlignment(Qt::AlignCenter);
	m_placeholderLabel->setWordWrap(true);
	m_placeholderLabel->setText(tr("Waiting for the GStreamer video surface..."));
	m_placeholderLabel->setGeometry(m_videoSurface->rect());
	m_placeholderLabel->show();
	rootLayout->addWidget(m_videoFrame, 1);

	m_statusLabel = new QLabel(m_rootFrame);
	m_statusLabel->setObjectName(QStringLiteral("qlScreenShareExternalStatus"));
	rootLayout->addWidget(m_statusLabel);

	m_embedPollTimer = new QTimer(this);
	m_embedPollTimer->setInterval(250);
	connect(m_embedPollTimer, &QTimer::timeout, this, &ExternalScreenShareWindowHost::pollForExternalWindow);

	m_audioControlTimer = new QTimer(this);
	m_audioControlTimer->setInterval(500);
	connect(m_audioControlTimer, &QTimer::timeout, this,
			&ExternalScreenShareWindowHost::retryApplyExternalAudioControls);

	updateSession(session);
	updateControls();
	updatePlaceholder();
	applyTheme();
	applyUiThemeNativeTitleBar(this);
}

void ExternalScreenShareWindowHost::closeFromManager() {
	m_closingFromManager = true;
	close();
}

void ExternalScreenShareWindowHost::setAudioMuted(const bool muted) {
	m_audioMuted = muted;
	updateControls();
	applyExternalAudioControls(true);
}

void ExternalScreenShareWindowHost::setPaused(const bool paused) {
	m_paused = paused;
	if (m_paused) {
		clearEmbeddedWindow();
	}
	updateControls();
	updatePlaceholder();
}

void ExternalScreenShareWindowHost::setProcessID(const qint64 processID) {
	if (m_processID == processID) {
		return;
	}

	clearEmbeddedWindow();
	m_processID = processID;
	m_audioControlAttempts = 0;
	updatePlaceholder();
	if (m_processID > 0) {
		m_embedPollTimer->start();
	} else {
		m_embedPollTimer->stop();
	}
	applyExternalAudioControls(true);
}

void ExternalScreenShareWindowHost::updateSession(const ScreenShareSession &session) {
	m_session = session;
	setWindowTitle(windowTitle());
	if (m_detailLabel) {
		const QString codec = Mumble::ScreenShare::codecToConfigToken(m_session.codec).toUpper();
		m_detailLabel->setText(tr("%1 - GStreamer live - %2x%3 @ %4 fps - %5")
								   .arg(ownerLabel(), QString::number(qMax(0U, m_session.width)),
										QString::number(qMax(0U, m_session.height)),
										QString::number(qMax(0U, m_session.fps)),
										codec.isEmpty() ? tr("video") : codec));
	}
	updateControls();
}

QString ExternalScreenShareWindowHost::streamID() const {
	return m_session.streamID;
}

void ExternalScreenShareWindowHost::changeEvent(QEvent *event) {
	QWidget::changeEvent(event);
	if (!event) {
		return;
	}

	switch (event->type()) {
		case QEvent::ApplicationPaletteChange:
		case QEvent::PaletteChange:
		case QEvent::StyleChange:
			if (!m_applyingTheme) {
				applyTheme();
			}
			applyUiThemeNativeTitleBar(this);
			break;
		default:
			break;
	}
}

void ExternalScreenShareWindowHost::closeEvent(QCloseEvent *event) {
	if (!m_closingFromManager) {
		emit closeRequested(m_session.streamID);
	}
	event->accept();
}

void ExternalScreenShareWindowHost::resizeEvent(QResizeEvent *event) {
	QWidget::resizeEvent(event);
	if (m_placeholderLabel && m_videoSurface) {
		m_placeholderLabel->setGeometry(m_videoSurface->rect());
	}
	moveEmbeddedWindow();
}

void ExternalScreenShareWindowHost::handleAudioButton() {
	if (!m_session.captureAudio) {
		return;
	}

	const bool muted = !m_audioMuted;
	setAudioMuted(muted);
	emit audioMuteToggled(m_session.streamID, muted);
}

void ExternalScreenShareWindowHost::handleAudioVolumeChanged(const int volumePercent) {
	m_audioVolumePercent = std::clamp(volumePercent, 0, 100);
	updateAudioVolumeLabel();

	if (!m_session.captureAudio) {
		return;
	}

	if (m_audioVolumePercent <= 0 && !m_audioMuted) {
		setAudioMuted(true);
		emit audioMuteToggled(m_session.streamID, true);
		return;
	}

	if (m_audioVolumePercent > 0 && m_audioMuted) {
		setAudioMuted(false);
		emit audioMuteToggled(m_session.streamID, false);
		return;
	}

	applyExternalAudioControls(true);
}

void ExternalScreenShareWindowHost::handlePauseButton() {
	emit pauseToggled(m_session.streamID, !m_paused);
}

void ExternalScreenShareWindowHost::retryApplyExternalAudioControls() {
	applyExternalAudioControls(true);
}

void ExternalScreenShareWindowHost::pollForExternalWindow() {
	if (m_processID <= 0) {
		m_embedPollTimer->stop();
		return;
	}

#ifdef Q_OS_WIN
	if (m_externalWindowID != 0) {
		const HWND window = reinterpret_cast< HWND >(m_externalWindowID);
		if (IsWindow(window)) {
			moveEmbeddedWindow();
			return;
		}

		clearEmbeddedWindow();
	}

	const HWND window = findExternalProcessWindow(m_processID);
	if (!window) {
		if (!externalProcessIsRunning(m_processID)) {
			m_embedPollTimer->stop();
			setStatusText(tr("The GStreamer viewer stopped before a video surface was available."));
			updatePlaceholder();
		}
		return;
	}

	m_externalWindowID = reinterpret_cast< quintptr >(window);
	attachExternalWindowToSurface(window, m_videoSurface);
	moveEmbeddedWindow();
	if (m_placeholderLabel) {
		m_placeholderLabel->hide();
	}
	setStatusText(tr("Live via GStreamer. Stream controls are local to this viewer."));
#else
	m_embedPollTimer->stop();
	setStatusText(tr("The GStreamer viewer is running in an external video window."));
#endif
}

void ExternalScreenShareWindowHost::applyExternalAudioControls(const bool allowRetry) {
#ifdef Q_OS_WIN
	if (!m_session.captureAudio || m_processID <= 0) {
		if (m_audioControlTimer) {
			m_audioControlTimer->stop();
		}
		return;
	}

	const float volumeLevel = std::clamp(static_cast< float >(m_audioVolumePercent) / 100.0f, 0.0f, 1.0f);
	const ProcessAudioControlResult result =
		setControlsForProcessAudioSessions(m_processID, m_audioMuted, volumeLevel);
	if (result == ProcessAudioControlResult::Applied) {
		m_audioControlAttempts = 0;
		if (m_audioControlTimer) {
			m_audioControlTimer->stop();
		}
		if (m_audioMuted) {
			setStatusText(tr("Stream audio muted locally. Video remains live via GStreamer."));
		} else if (m_audioVolumePercent != 100) {
			setStatusText(tr("Stream audio set to %1% for this viewer.").arg(m_audioVolumePercent));
		} else {
			setStatusText(tr("Stream audio enabled for this viewer."));
		}
		return;
	}

	if (result == ProcessAudioControlResult::Failed) {
		if (m_audioControlTimer) {
			m_audioControlTimer->stop();
		}
		if (Global::get().l) {
			Global::get().l->log(Log::Warning,
								 tr("Unable to change screen-share audio controls for viewer process %1.")
									 .arg(QString::number(m_processID).toHtmlEscaped()));
		}
		setStatusText(tr("Could not change this viewer's audio state."));
		return;
	}

	const bool needsDeferredControls = m_audioMuted || m_audioVolumePercent != 100;
	if (allowRetry && needsDeferredControls && m_audioControlAttempts < 40) {
		++m_audioControlAttempts;
		if (m_audioControlTimer && !m_audioControlTimer->isActive()) {
			m_audioControlTimer->start();
		}
		setStatusText(tr("Stream audio controls will apply when the GStreamer audio session starts."));
		return;
	}

	if (m_audioControlTimer) {
		m_audioControlTimer->stop();
	}
#else
	Q_UNUSED(allowRetry)
#endif
}

QString ExternalScreenShareWindowHost::ownerLabel() const {
	const ClientUser *owner = ClientUser::get(m_session.ownerSession);
	if (owner && !owner->qsName.trimmed().isEmpty()) {
		return owner->qsName;
	}
	if (m_session.ownerSession != 0) {
		return tr("session %1").arg(m_session.ownerSession);
	}
	return tr("screen share");
}

QString ExternalScreenShareWindowHost::windowTitle() const {
	return tr("Mumble Screen Share - %1").arg(ownerLabel());
}

void ExternalScreenShareWindowHost::applyTheme() {
	if (m_applyingTheme) {
		return;
	}

	m_applyingTheme = true;
	const QPalette pal = palette();
	const std::optional< UiThemeTokens > tokens = activeUiThemeTokens();
	const QColor crust =
		tokens ? tokens->crust : fallbackPaletteColor(pal, QPalette::Window, QColor(QStringLiteral("#191f26")));
	const QColor mantle =
		tokens ? tokens->mantle : fallbackPaletteColor(pal, QPalette::Base, QColor(QStringLiteral("#252c34")));
	const QColor surface =
		tokens ? tokens->surface0 : fallbackPaletteColor(pal, QPalette::Button, QColor(QStringLiteral("#313a44")));
	const QColor surface2 =
		tokens ? tokens->surface1 : fallbackPaletteColor(pal, QPalette::Mid, QColor(QStringLiteral("#39424d")));
	const QColor text =
		tokens ? tokens->text : fallbackPaletteColor(pal, QPalette::WindowText, QColor(QStringLiteral("#e0e7ef")));
	const QColor muted =
		tokens ? tokens->subtext0 : fallbackPaletteColor(pal, QPalette::Text, QColor(QStringLiteral("#7d8996")));
	const QColor accent =
		tokens ? tokens->accent : fallbackPaletteColor(pal, QPalette::Highlight, QColor(QStringLiteral("#6aa6cf")));
	const QColor danger = tokens ? tokens->danger : QColor(QStringLiteral("#c46a74"));

	setStyleSheet(QString::fromLatin1(
					  "QFrame#qfScreenShareExternalWindow { background: %1; border: 1px solid %2; }"
					  "QFrame#qfScreenShareExternalHeader { background: %3; border: 1px solid %2; border-radius: 8px; }"
					  "QFrame#qfScreenShareExternalVideoFrame { background: #05070a; border: 1px solid %2; border-radius: 8px; }"
					  "QWidget#qwScreenShareExternalVideoSurface { background: #05070a; }"
					  "QWidget#qwScreenShareExternalVolumeGroup { background: transparent; }"
					  "QLabel#qlScreenShareExternalTitle { color: %4; font-size: 14px; font-weight: 700; }"
					  "QLabel#qlScreenShareExternalDetail, QLabel#qlScreenShareExternalStatus,"
					  "QLabel#qlScreenShareExternalPlaceholder { color: %5; }"
					  "QLabel#qlScreenShareExternalVolume { color: %4; font-weight: 600; }"
					  "QToolButton { background: %6; color: %4; border: 1px solid %2; border-radius: 7px;"
					  "padding: 6px 10px; font-weight: 600; }"
					  "QToolButton:hover { border-color: %7; }"
					  "QToolButton:checked { background: %8; border-color: %7; }"
					  "QToolButton:disabled { color: %5; }"
					  "QToolButton#qtbScreenShareExternalStop { color: %9; }"
					  "QSlider#qsScreenShareExternalVolume::groove:horizontal { background: %2; height: 4px; border-radius: 2px; }"
					  "QSlider#qsScreenShareExternalVolume::sub-page:horizontal { background: %7; border-radius: 2px; }"
					  "QSlider#qsScreenShareExternalVolume::add-page:horizontal { background: %2; border-radius: 2px; }"
					  "QSlider#qsScreenShareExternalVolume::handle:horizontal { background: %4; border: 1px solid %7;"
					  "width: 12px; margin: -5px 0; border-radius: 6px; }"
					  "QSlider#qsScreenShareExternalVolume:disabled::handle:horizontal { background: %5; border-color: %2; }")
					  .arg(uiThemeQssColor(crust), uiThemeQssColor(surface2), uiThemeQssColor(mantle),
						   uiThemeQssColor(text), uiThemeQssColor(muted), uiThemeQssColor(surface),
						   uiThemeQssColor(accent), uiThemeQssColor(uiThemeColorWithAlpha(accent, 0.24)),
						   uiThemeQssColor(danger)));
	m_applyingTheme = false;
}

void ExternalScreenShareWindowHost::clearEmbeddedWindow() {
#ifdef Q_OS_WIN
	if (m_externalWindowID != 0) {
		const HWND window = reinterpret_cast< HWND >(m_externalWindowID);
		if (IsWindow(window)) {
			ShowWindow(window, SW_HIDE);
		}
	}
#endif
	m_externalWindowID = 0;
	if (m_placeholderLabel) {
		m_placeholderLabel->show();
	}
}

void ExternalScreenShareWindowHost::moveEmbeddedWindow() {
#ifdef Q_OS_WIN
	if (m_externalWindowID == 0 || !m_videoSurface) {
		return;
	}

	const HWND window = reinterpret_cast< HWND >(m_externalWindowID);
	if (!IsWindow(window)) {
		clearEmbeddedWindow();
		return;
	}

	moveExternalWindowToSurface(window, m_videoSurface);
#endif
}

void ExternalScreenShareWindowHost::setStatusText(const QString &status) {
	if (m_statusLabel) {
		m_statusLabel->setText(status);
	}
}

void ExternalScreenShareWindowHost::updateAudioVolumeLabel() {
	if (m_audioVolumeLabel) {
		m_audioVolumeLabel->setText(tr("%1%").arg(m_audioVolumePercent));
	}
}

void ExternalScreenShareWindowHost::updateControls() {
	if (m_pauseButton) {
		m_pauseButton->setText(m_paused ? tr("Resume live") : tr("Pause view"));
		m_pauseButton->setIcon(style()->standardIcon(m_paused ? QStyle::SP_MediaPlay : QStyle::SP_MediaPause));
		m_pauseButton->setToolTip(
			m_paused ? tr("Resume this live viewer.")
					 : tr("Pause only this local viewer. The live stream continues for everyone else."));
		m_pauseButton->setChecked(m_paused);
	}
	if (m_audioButton) {
		m_audioButton->setEnabled(m_session.captureAudio);
		m_audioButton->setText(m_audioMuted ? tr("Unmute audio") : tr("Mute audio"));
		m_audioButton->setToolTip(m_session.captureAudio ? tr("Toggle this viewer's stream audio.")
														  : tr("This stream does not include audio."));
		m_audioButton->setChecked(m_audioMuted);
	}
	if (m_audioVolumeSlider) {
		m_audioVolumeSlider->setEnabled(m_session.captureAudio);
		m_audioVolumeSlider->setToolTip(m_session.captureAudio ? tr("Adjust this viewer's stream audio volume.")
															   : tr("This stream does not include audio."));
	}
	if (m_audioVolumeLabel) {
		m_audioVolumeLabel->setEnabled(m_session.captureAudio);
	}
	updateAudioVolumeLabel();

	if (m_paused) {
		setStatusText(tr("Paused locally. Resume returns to the live edge; no playback buffer is kept."));
	} else if (m_processID > 0 && externalProcessIsRunning(m_processID)) {
		setStatusText(tr("Connecting to the GStreamer video surface..."));
	} else {
		setStatusText(tr("Waiting for the live viewer to start."));
	}
}

void ExternalScreenShareWindowHost::updatePlaceholder() {
	if (!m_placeholderLabel) {
		return;
	}
	if (m_paused) {
		m_placeholderLabel->setText(tr("Paused locally\n\nThe stream is live. Resume jumps back to the live edge."));
		m_placeholderLabel->show();
		return;
	}
	if (m_processID <= 0) {
		m_placeholderLabel->setText(tr("Waiting for the GStreamer viewer to start..."));
		m_placeholderLabel->show();
		return;
	}
	m_placeholderLabel->setText(tr("Connecting to the GStreamer video surface..."));
	m_placeholderLabel->show();
}
