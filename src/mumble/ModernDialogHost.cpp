// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernDialogHost.h"

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include "ModernShellBridge.h"
#include "ModernShellPage.h"

#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtGui/QCloseEvent>
#include <QtGui/QColor>
#include <QtGui/QPalette>
#include <QtWidgets/QVBoxLayout>
#include <QtWebChannel/QWebChannel>
#include <QtWebEngineCore/QWebEngineScript>
#include <QtWebEngineCore/QWebEngineSettings>
#include <QtWebEngineWidgets/QWebEngineView>

#ifdef Q_OS_WIN
#	include <windows.h>
#endif

namespace {
	struct ModernDialogChromeColors {
		QColor caption;
		QColor text;
		QColor border;
	};

	QUrl modernDialogUrl() {
		return QUrl(QStringLiteral("qrc:/modern-shell/dialog.html"));
	}

	bool modernDialogThemeIsLight(const QString &theme) {
		const QString normalized = theme.trimmed().toLower();
		return normalized == QLatin1String("light") || normalized == QLatin1String("latte");
	}

	ModernDialogChromeColors modernDialogChromeColorsForTheme(const QString &theme, const QPalette &palette) {
		const QString normalized = theme.trimmed().toLower();
		if (normalized == QLatin1String("light") || normalized == QLatin1String("latte")) {
			return { QColor(QStringLiteral("#f7f9fc")), QColor(QStringLiteral("#1f2937")),
					 QColor(QStringLiteral("#cfd7e0")) };
		}
		if (normalized == QLatin1String("mocha")) {
			return { QColor(QStringLiteral("#1e1e2e")), QColor(QStringLiteral("#cdd6f4")),
					 QColor(QStringLiteral("#45475a")) };
		}
		if (normalized == QLatin1String("macchiato")) {
			return { QColor(QStringLiteral("#24273a")), QColor(QStringLiteral("#cad3f5")),
					 QColor(QStringLiteral("#494d64")) };
		}
		if (normalized == QLatin1String("frappe")) {
			return { QColor(QStringLiteral("#303446")), QColor(QStringLiteral("#c6d0f5")),
					 QColor(QStringLiteral("#51576d")) };
		}
		if (normalized == QLatin1String("nord")) {
			return { QColor(QStringLiteral("#2e3440")), QColor(QStringLiteral("#eceff4")),
					 QColor(QStringLiteral("#434c5e")) };
		}
		if (normalized == QLatin1String("gruvbox")) {
			return { QColor(QStringLiteral("#282828")), QColor(QStringLiteral("#fbf1c7")),
					 QColor(QStringLiteral("#504945")) };
		}

		return { palette.color(QPalette::Window), palette.color(QPalette::WindowText),
				 palette.color(QPalette::Mid) };
	}

#ifdef Q_OS_WIN
	using DwmSetWindowAttributeFn = HRESULT(WINAPI *)(HWND, DWORD, LPCVOID, DWORD);

	constexpr DWORD DwmUseImmersiveDarkModeLegacyAttribute = 19;
	constexpr DWORD DwmUseImmersiveDarkModeAttribute       = 20;
	constexpr DWORD DwmBorderColorAttribute                = 34;
	constexpr DWORD DwmCaptionColorAttribute               = 35;
	constexpr DWORD DwmTextColorAttribute                  = 36;

	COLORREF colorRefFromQColor(const QColor &color) {
		return RGB(color.red(), color.green(), color.blue());
	}
#endif
} // namespace

ModernDialogHost::ModernDialogHost(ModernShellBridge *bridge, QWidget *parent)
	: QDialog(parent), m_bridge(bridge) {
	setAttribute(Qt::WA_DeleteOnClose, false);
	setWindowModality(Qt::NonModal);
	setWindowTitle(tr("Mumble"));
	setMinimumSize(560, 360);
	resize(760, 560);
	applyAutomationOffscreenFlags();

	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->setSpacing(0);

	m_view = new QWebEngineView(this);
	m_view->setContextMenuPolicy(Qt::NoContextMenu);
	m_layout->addWidget(m_view);

	m_page = new ModernShellPage(m_view);
	m_view->setPage(m_page);

	m_channel = new QWebChannel(this);
	if (m_bridge) {
		m_channel->registerObject(QStringLiteral("modernBridge"), m_bridge);
	}
	m_page->setWebChannel(m_channel);

	m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, true);
	m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);
	m_view->settings()->setAttribute(QWebEngineSettings::PlaybackRequiresUserGesture, false);

	m_stateRepublishTimer = new QTimer(this);
	m_stateRepublishTimer->setSingleShot(true);
	m_stateRepublishTimer->setInterval(75);

	connect(m_view, &QWebEngineView::loadFinished, this, &ModernDialogHost::handleLoadFinished);
	connect(m_page, &QWebEnginePage::renderProcessTerminated, this,
			&ModernDialogHost::handleRenderProcessTerminated);
	connect(m_stateRepublishTimer, &QTimer::timeout, this, &ModernDialogHost::republishDialogState);
	connect(m_page, &ModernShellPage::externalNavigationRequested, this, [](const QUrl &url) {
		Q_UNUSED(url);
	});
}

bool ModernDialogHost::showDialogState(const QVariantMap &state, QString *errorMessage) {
	if (!state.value(QStringLiteral("open")).toBool()) {
		hideDialog();
		return true;
	}

	if (!m_bridge) {
		if (errorMessage) {
			*errorMessage = tr("The modern dialog bridge is unavailable.");
		}
		return false;
	}

	if (!start(errorMessage)) {
		return false;
	}

	const QString nextDialogID = state.value(QStringLiteral("id")).toString();
	const bool shouldPresent   = !m_open || !isVisible() || m_currentDialogID != nextDialogID;
	m_lastDialogState          = state;
	m_open                    = true;
	m_currentDialogID         = nextDialogID;
	const QString title = state.value(QStringLiteral("title")).toString().trimmed();
	setWindowTitle(title.isEmpty() ? tr("Mumble") : title);
	applyDialogGeometry(state);
	applyWindowChrome(state);

	if (shouldPresent) {
		if (automationOffscreenModeEnabled()) {
			showForAutomationCapture();
		} else {
			show();
			raise();
			activateWindow();
		}
	}
	queueDialogStateRepublish();
	return true;
}

void ModernDialogHost::hideDialog() {
	m_open = false;
	m_stateRepublishRemaining = 0;
	if (m_stateRepublishTimer) {
		m_stateRepublishTimer->stop();
	}
	m_currentDialogID.clear();
	m_lastDialogState.clear();
	hide();
}

QVariant ModernDialogHost::runAutomationScriptResult(const QString &script, const int timeoutMilliseconds) {
	if (!m_page || script.trimmed().isEmpty()) {
		return QVariant();
	}

	QVariant result;
	bool finished = false;
	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	m_page->runJavaScript(script, QWebEngineScript::MainWorld, [&result, &finished, &loop](const QVariant &value) {
		result   = value;
		finished = true;
		loop.quit();
	});
	timeout.start(qBound(50, timeoutMilliseconds, 10000));
	loop.exec();
	return finished ? result : QVariant();
}

void ModernDialogHost::closeEvent(QCloseEvent *event) {
	if (m_open && !m_currentDialogID.isEmpty()) {
		event->ignore();
		const QString dialogID = m_currentDialogID;
		hide();
		emit nativeCloseRequested(dialogID);
		return;
	}

	QDialog::closeEvent(event);
}

bool ModernDialogHost::start(QString *errorMessage) {
	if (m_started) {
		return true;
	}

	if (!m_view) {
		if (errorMessage) {
			*errorMessage = tr("The modern dialog view could not be initialized.");
		}
		return false;
	}

	const QUrl url = modernDialogUrl();
	if (!url.isValid() || url.isEmpty()) {
		if (errorMessage) {
			*errorMessage = tr("The modern dialog URL is invalid.");
		}
		return false;
	}

	m_view->load(url);
	m_started = true;
	return true;
}

void ModernDialogHost::applyDialogGeometry(const QVariantMap &state) {
	const QString kind = state.value(QStringLiteral("kind")).toString();
	QSize desiredSize(760, 560);
	QSize minimumSize(560, 360);

	if (kind == QLatin1String("connect")) {
		desiredSize = QSize(880, 620);
	} else if (kind == QLatin1String("settings")) {
		desiredSize = QSize(900, 620);
	} else if (kind == QLatin1String("failedConnection")) {
		desiredSize = QSize(600, 420);
	} else if (kind == QLatin1String("confirm")) {
		minimumSize = QSize(420, 220);
	}

	const int requestedWidth  = state.value(QStringLiteral("width")).toInt();
	const int requestedHeight = state.value(QStringLiteral("height")).toInt();
	if (requestedWidth > 0 && requestedHeight > 0) {
		desiredSize = QSize(requestedWidth, requestedHeight);
	}

	setMinimumSize(minimumSize);
	if (!isVisible()) {
		resize(desiredSize);
	}
	if (automationOffscreenModeEnabled()) {
		move(-32000, -32000);
	}
}

void ModernDialogHost::applyWindowChrome(const QVariantMap &state) {
	const QString theme = state.value(QStringLiteral("uiTweaks")).toMap().value(QStringLiteral("theme")).toString();
	const ModernDialogChromeColors colors = modernDialogChromeColorsForTheme(theme, palette());

	QPalette themedPalette = palette();
	themedPalette.setColor(QPalette::Window, colors.caption);
	themedPalette.setColor(QPalette::WindowText, colors.text);
	themedPalette.setColor(QPalette::Base, colors.caption);
	setPalette(themedPalette);
	if (m_view) {
		m_view->setStyleSheet(QString::fromLatin1("background: %1;").arg(colors.caption.name()));
	}

#ifdef Q_OS_WIN
	const HWND hwnd = reinterpret_cast< HWND >(winId());
	if (!hwnd) {
		return;
	}

	static const HMODULE dwmapiModule = GetModuleHandleW(L"dwmapi.dll");
	if (!dwmapiModule) {
		return;
	}

	static const DwmSetWindowAttributeFn setWindowAttribute =
		reinterpret_cast< DwmSetWindowAttributeFn >(GetProcAddress(dwmapiModule, "DwmSetWindowAttribute"));
	if (!setWindowAttribute) {
		return;
	}

	const BOOL immersiveDarkMode = modernDialogThemeIsLight(theme) ? FALSE : TRUE;
	HRESULT result =
		setWindowAttribute(hwnd, DwmUseImmersiveDarkModeAttribute, &immersiveDarkMode, sizeof(immersiveDarkMode));
	if (FAILED(result)) {
		setWindowAttribute(hwnd, DwmUseImmersiveDarkModeLegacyAttribute, &immersiveDarkMode, sizeof(immersiveDarkMode));
	}

	const COLORREF captionColorRef = colorRefFromQColor(colors.caption);
	const COLORREF textColorRef    = colorRefFromQColor(colors.text);
	const COLORREF borderColorRef  = colorRefFromQColor(colors.border);
	setWindowAttribute(hwnd, DwmCaptionColorAttribute, &captionColorRef, sizeof(captionColorRef));
	setWindowAttribute(hwnd, DwmTextColorAttribute, &textColorRef, sizeof(textColorRef));
	setWindowAttribute(hwnd, DwmBorderColorAttribute, &borderColorRef, sizeof(borderColorRef));
#endif
}

bool ModernDialogHost::automationOffscreenModeEnabled() const {
	return qEnvironmentVariableIsSet("MUMBLE_MODERN_AUTOMATION_OFFSCREEN");
}

void ModernDialogHost::applyAutomationOffscreenFlags() {
	if (!automationOffscreenModeEnabled()) {
		return;
	}

	setAttribute(Qt::WA_ShowWithoutActivating, true);
	setWindowFlag(Qt::WindowDoesNotAcceptFocus, true);
	move(-32000, -32000);
}

void ModernDialogHost::showForAutomationCapture() {
	applyAutomationOffscreenFlags();
	move(-32000, -32000);
	show();
}

void ModernDialogHost::handleLoadFinished(const bool ok) {
	if (ok) {
		queueDialogStateRepublish();
		return;
	}

	m_started = false;
	emit hostFailed(tr("The modern dialog window failed to load its local web assets."));
}

void ModernDialogHost::handleRenderProcessTerminated(const QWebEnginePage::RenderProcessTerminationStatus status,
													 const int exitCode) {
	Q_UNUSED(status);
	m_started = false;
	emit hostFailed(tr("The modern dialog renderer stopped unexpectedly with exit code %1.").arg(exitCode));
}

void ModernDialogHost::queueDialogStateRepublish() {
	if (!m_open || !m_bridge || !m_stateRepublishTimer || m_lastDialogState.isEmpty()) {
		return;
	}

	m_stateRepublishRemaining = 4;
	if (!m_stateRepublishTimer->isActive()) {
		m_stateRepublishTimer->start();
	}
}

void ModernDialogHost::republishDialogState() {
	if (!m_open || !m_bridge || m_lastDialogState.isEmpty()
		|| m_lastDialogState.value(QStringLiteral("id")).toString() != m_currentDialogID) {
		m_stateRepublishRemaining = 0;
		return;
	}

	m_bridge->publishModernDialogState(m_lastDialogState);
	--m_stateRepublishRemaining;
	if (m_stateRepublishRemaining > 0 && m_stateRepublishTimer) {
		m_stateRepublishTimer->start(125);
	}
}

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)
