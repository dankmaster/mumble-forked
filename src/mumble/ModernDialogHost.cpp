// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernDialogHost.h"

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include "ModernShellBridge.h"
#include "ModernShellPage.h"
#include "UiTheme.h"

#include <QtCore/QEventLoop>
#include <QtCore/QJsonDocument>
#include <QtCore/QCoreApplication>
#include <QtCore/QRect>
#include <QtCore/QStringList>
#include <QtCore/QTimer>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtGui/QCloseEvent>
#include <QtGui/QColor>
#include <QtGui/QGuiApplication>
#include <QtGui/QMouseEvent>
#include <QtGui/QPalette>
#include <QtGui/QScreen>
#include <QtGui/QWindow>
#include <QtWidgets/QVBoxLayout>
#include <QtWebChannel/QWebChannel>
#include <QtWebEngineCore/QWebEngineScript>
#include <QtWebEngineCore/QWebEngineScriptCollection>
#include <QtWebEngineCore/QWebEngineSettings>
#include <QtWebEngineWidgets/QWebEngineView>

namespace {
	void addModernUiTweaksQuery(QUrl &url, const QVariantMap &uiTweaks) {
		if (uiTweaks.isEmpty()) {
			return;
		}

		QUrlQuery query(url);
		const QStringList keys { QStringLiteral("theme"), QStringLiteral("accent"), QStringLiteral("density"),
								 QStringLiteral("userIcons"), QStringLiteral("classicUserIcons"),
								 QStringLiteral("railSide") };
		for (const QString &key : keys) {
			const QVariant value = uiTweaks.value(key);
			if (!value.isValid() || value.isNull()) {
				continue;
			}
			const QString text = value.toString().trimmed();
			if (!text.isEmpty()) {
				query.addQueryItem(key, text);
			}
		}
		url.setQuery(query);
	}

	QUrl modernDialogUrl(const QVariantMap &state) {
		QUrl url(QStringLiteral("qrc:/modern-shell/dialog.html"));
		addModernUiTweaksQuery(url, state.value(QStringLiteral("uiTweaks")).toMap());
		return url;
	}

	QString modernUiTweaksBootstrapSource(const QVariantMap &uiTweaks) {
		QByteArray json = QJsonDocument::fromVariant(uiTweaks).toJson(QJsonDocument::Compact);
		if (json.isEmpty() || json == QByteArrayLiteral("null")) {
			json = QByteArrayLiteral("{}");
		}
		return QStringLiteral("window.__mumbleModernInitialUiTweaks = %1;").arg(QString::fromUtf8(json));
	}

	void installModernUiTweaksBootstrap(ModernShellPage *page, const QVariantMap &uiTweaks) {
		if (!page) {
			return;
		}

		QWebEngineScript script;
		script.setName(QStringLiteral("MumbleModernUiTweaksBootstrap"));
		script.setInjectionPoint(QWebEngineScript::DocumentCreation);
		script.setWorldId(QWebEngineScript::MainWorld);
		script.setRunsOnSubFrames(false);
		script.setSourceCode(modernUiTweaksBootstrapSource(uiTweaks));
		page->scripts().insert(script);
	}

	QScreen *modernDialogScreenForWidget(const QWidget *widget) {
		const QWidget *window = widget ? widget->window() : nullptr;
		if (window && window->screen()) {
			return window->screen();
		}

		if (window) {
			if (QScreen *screen = QGuiApplication::screenAt(window->frameGeometry().center())) {
				return screen;
			}
		}

		return QGuiApplication::primaryScreen();
	}

	QRect modernDialogCenteredRect(const QSize &size, const QRect &anchor, const QRect &bounds) {
		if (!bounds.isValid()) {
			return QRect(QPoint(0, 0), size);
		}

		const QSize boundedSize(qMin(size.width(), bounds.width()), qMin(size.height(), bounds.height()));
		const QRect centered(anchor.center() - QPoint(boundedSize.width() / 2, boundedSize.height() / 2),
							 boundedSize);
		const int maxX = qMax(bounds.left(), bounds.right() - boundedSize.width() + 1);
		const int maxY = qMax(bounds.top(), bounds.bottom() - boundedSize.height() + 1);
		return QRect(QPoint(qBound(bounds.left(), centered.left(), maxX),
							qBound(bounds.top(), centered.top(), maxY)),
					 boundedSize);
	}

	void centerModernDialogWindow(QWidget *dialog) {
		if (!dialog) {
			return;
		}

		QScreen *screen = modernDialogScreenForWidget(dialog->parentWidget() ? dialog->parentWidget() : dialog);
		const QRect bounds = screen ? screen->availableGeometry() : QRect();
		QWidget *anchorWidget = dialog->parentWidget() ? dialog->parentWidget()->window() : nullptr;
		const QRect anchor = anchorWidget && anchorWidget->isVisible() && anchorWidget->frameGeometry().isValid()
								 ? anchorWidget->frameGeometry()
								 : bounds;
		dialog->move(modernDialogCenteredRect(dialog->size(), anchor, bounds).topLeft());
	}

} // namespace

ModernDialogHost::ModernDialogHost(ModernShellBridge *bridge, QWidget *parent)
	: QDialog(parent), m_bridge(bridge) {
	setAttribute(Qt::WA_DeleteOnClose, false);
	setAttribute(Qt::WA_TranslucentBackground, true);
	setAttribute(Qt::WA_NoSystemBackground, true);
	setWindowModality(Qt::NonModal);
	setWindowFlag(Qt::FramelessWindowHint, true);
	setWindowTitle(tr("Mumble"));
	setAutoFillBackground(false);
	setMinimumSize(560, 360);
	resize(760, 560);
	applyAutomationOffscreenFlags();

	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->setSpacing(0);

	m_view = new QWebEngineView(this);
	m_view->setAttribute(Qt::WA_TranslucentBackground, true);
	m_view->setAttribute(Qt::WA_NoSystemBackground, true);
	m_view->setAutoFillBackground(false);
	m_view->setContextMenuPolicy(Qt::NoContextMenu);
	m_layout->addWidget(m_view);
	if (QCoreApplication *application = QCoreApplication::instance()) {
		application->installEventFilter(this);
	}

	m_page = new ModernShellPage(m_view);
	m_view->setPage(m_page);
	m_page->setBackgroundColor(Qt::transparent);

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

ModernDialogHost::~ModernDialogHost() {
	if (QCoreApplication *application = QCoreApplication::instance()) {
		application->removeEventFilter(this);
	}
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

	m_lastDialogState = state;
	if (!start(errorMessage)) {
		return false;
	}

	const QString nextDialogID = state.value(QStringLiteral("id")).toString();
	const bool shouldPresent   = !m_open || !isVisible() || m_currentDialogID != nextDialogID;
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

bool ModernDialogHost::eventFilter(QObject *watched, QEvent *event) {
	if (!m_view || !isVisible() || automationOffscreenModeEnabled()) {
		return QDialog::eventFilter(watched, event);
	}

	QWidget *watchedWidget = qobject_cast< QWidget * >(watched);
	if (!watchedWidget || (watchedWidget != m_view && !m_view->isAncestorOf(watchedWidget))) {
		return QDialog::eventFilter(watched, event);
	}

	if (event->type() == QEvent::MouseButtonRelease) {
		m_manualDragActive = false;
		return QDialog::eventFilter(watched, event);
	}

	if (event->type() == QEvent::MouseMove && m_manualDragActive) {
		QMouseEvent *mouseEvent = static_cast< QMouseEvent * >(event);
		if (mouseEvent->buttons() & Qt::LeftButton) {
			move(mouseEvent->globalPosition().toPoint() - m_manualDragOffset);
			event->accept();
			return true;
		}
		m_manualDragActive = false;
		return QDialog::eventFilter(watched, event);
	}

	if (event->type() != QEvent::MouseButtonPress) {
		return QDialog::eventFilter(watched, event);
	}

	QMouseEvent *mouseEvent = static_cast< QMouseEvent * >(event);
	if (mouseEvent->button() != Qt::LeftButton) {
		return QDialog::eventFilter(watched, event);
	}

	const QPoint viewPosition = m_view->mapFromGlobal(mouseEvent->globalPosition().toPoint());
	if (!shouldStartWindowDrag(viewPosition)) {
		return QDialog::eventFilter(watched, event);
	}

	if (QWindow *window = windowHandle()) {
		if (window->startSystemMove()) {
			event->accept();
			return true;
		}
	}

	m_manualDragActive = true;
	m_manualDragOffset = mouseEvent->globalPosition().toPoint() - frameGeometry().topLeft();
	event->accept();
	return true;
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

	const QUrl url = modernDialogUrl(m_lastDialogState);
	installModernUiTweaksBootstrap(m_page, m_lastDialogState.value(QStringLiteral("uiTweaks")).toMap());
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
	const QSize targetSize = desiredSize.expandedTo(minimumSize);
	if (size() != targetSize) {
		resize(targetSize);
	}
	if (automationOffscreenModeEnabled()) {
		move(-32000, -32000);
	} else {
		centerModernDialogWindow(this);
	}
}

void ModernDialogHost::applyWindowChrome(const QVariantMap &state) {
	Q_UNUSED(state);
	const UiThemeWindowChrome colors = uiThemeWindowChromeForActiveTheme(palette());

	QPalette themedPalette = palette();
	themedPalette.setColor(QPalette::Window, Qt::transparent);
	themedPalette.setColor(QPalette::WindowText, colors.text);
	themedPalette.setColor(QPalette::Base, Qt::transparent);
	setPalette(themedPalette);
	if (m_view) {
		m_view->setStyleSheet(QStringLiteral("background: transparent;"));
		m_view->setAttribute(Qt::WA_TranslucentBackground, true);
		m_view->setAttribute(Qt::WA_NoSystemBackground, true);
		m_view->setAutoFillBackground(false);
	}
	if (m_page) {
		m_page->setBackgroundColor(Qt::transparent);
	}
	applyUiThemeNativeTitleBar(this, colors);
}

bool ModernDialogHost::automationOffscreenModeEnabled() const {
#if defined(MUMBLE_HAS_MODERN_UI_AUTOMATION)
	return qEnvironmentVariableIsSet("MUMBLE_MODERN_AUTOMATION_OFFSCREEN");
#else
	return false;
#endif
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

bool ModernDialogHost::shouldStartWindowDrag(const QPoint &viewPosition) const {
	if (!m_view || viewPosition.x() < 0 || viewPosition.y() < 0 || viewPosition.x() >= m_view->width()
		|| viewPosition.y() >= m_view->height()) {
		return false;
	}

	const QString kind = m_lastDialogState.value(QStringLiteral("kind")).toString();
	const int dragHeight = kind == QLatin1String("stonks") ? 78 : 62;
	const int trailingInteractiveWidth = kind == QLatin1String("settings") ? 176 : 54;
	return viewPosition.y() <= dragHeight && viewPosition.x() < (m_view->width() - trailingInteractiveWidth);
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
