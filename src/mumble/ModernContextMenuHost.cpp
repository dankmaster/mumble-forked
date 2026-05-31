// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernContextMenuHost.h"

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include "ModernShellPage.h"

#include <QtCore/QJsonDocument>
#include <QtCore/QStringList>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtGui/QCloseEvent>
#include <QtGui/QCursor>
#include <QtGui/QGuiApplication>
#include <QtGui/QKeyEvent>
#include <QtGui/QPalette>
#include <QtGui/QPainterPath>
#include <QtGui/QRegion>
#include <QtGui/QScreen>
#include <QtWebChannel/QWebChannel>
#include <QtWebEngineCore/QWebEngineScript>
#include <QtWebEngineCore/QWebEngineScriptCollection>
#include <QtWebEngineCore/QWebEngineSettings>
#include <QtWidgets/QApplication>
#include <QtWebEngineWidgets/QWebEngineView>
#include <QtWidgets/QVBoxLayout>

namespace {
	constexpr int kContextMenuRootWidth = 272;
	constexpr int kContextMenuFlyoutWidth = 236;
	constexpr int kContextMenuFlyoutGap = 6;
	constexpr int kContextMenuHostInset = 12;

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

	QUrl modernContextMenuUrl(const QVariantMap &uiTweaks) {
		QUrl url(QStringLiteral("qrc:/modern-shell/popup.html"));
		addModernUiTweaksQuery(url, uiTweaks);
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

	void makeWidgetTransparent(QWidget *widget) {
		if (!widget) {
			return;
		}

		widget->setAttribute(Qt::WA_NoSystemBackground, true);
		widget->setAttribute(Qt::WA_TranslucentBackground, true);
		widget->setAttribute(Qt::WA_OpaquePaintEvent, false);
		widget->setAutoFillBackground(false);

		QPalette palette = widget->palette();
		palette.setColor(QPalette::Window, Qt::transparent);
		palette.setColor(QPalette::Base, Qt::transparent);
		widget->setPalette(palette);
	}

	int contextMenuItemHeight(const QVariantMap &item) {
		const QString kind = item.value(QStringLiteral("kind"), QStringLiteral("action")).toString();
		if (kind == QLatin1String("separator")) {
			return 13;
		}
		if (kind == QLatin1String("label")) {
			return 24;
		}
		if (kind == QLatin1String("profileHeader")) {
			return 64;
		}
		if (kind == QLatin1String("submenu")) {
			return 32;
		}
		return 32;
	}

	int contextMenuListHeight(const QVariantList &items) {
		int height = 18;
		for (const QVariant &itemVariant : items) {
			height += contextMenuItemHeight(itemVariant.toMap());
		}
		return height;
	}

	bool contextMenuHasSubmenu(const QVariantList &items) {
		for (const QVariant &itemVariant : items) {
			const QVariantMap item = itemVariant.toMap();
			if (item.value(QStringLiteral("kind")).toString() == QLatin1String("submenu")) {
				return true;
			}
		}
		return false;
	}

	int contextMenuMaxSubmenuHeight(const QVariantList &items) {
		int height = 0;
		for (const QVariant &itemVariant : items) {
			const QVariantMap item = itemVariant.toMap();
			if (item.value(QStringLiteral("kind")).toString() != QLatin1String("submenu")) {
				continue;
			}
			const QVariantList childItems = item.value(QStringLiteral("items")).toList();
			height = qMax(height, contextMenuListHeight(childItems));
			height = qMax(height, contextMenuMaxSubmenuHeight(childItems));
		}
		return height;
	}

	int boundedInt(const QVariantMap &map, const QString &key, const int fallback) {
		bool ok = false;
		const double value = map.value(key, fallback).toDouble(&ok);
		return ok ? qRound(value) : fallback;
	}

	QRegion roundedRectRegion(const QRect &rect, const qreal radius) {
		if (rect.isEmpty()) {
			return QRegion();
		}
		QPainterPath path;
		path.addRoundedRect(QRectF(rect), radius, radius);
		return QRegion(path.toFillPolygon().toPolygon());
	}
}

ModernContextMenuHostBridge::ModernContextMenuHostBridge(QObject *parent) : QObject(parent) {
}

void ModernContextMenuHostBridge::activateAction(const int actionIndex) {
	emit actionRequested(actionIndex);
}

void ModernContextMenuHostBridge::closePopup() {
	emit closeRequested();
}

void ModernContextMenuHostBridge::updateMask(const QVariantMap &layout) {
	emit maskRequested(layout);
}

ModernContextMenuHost::ModernContextMenuHost(QWidget *parent) : QWidget(parent) {
	setWindowFlags(Qt::Tool | Qt::FramelessWindowHint | Qt::NoDropShadowWindowHint
				   | Qt::WindowDoesNotAcceptFocus);
	setAttribute(Qt::WA_ShowWithoutActivating, true);
	setWindowTitle(tr("Mumble menu"));
	makeWidgetTransparent(this);
	resize(272, 160);

	m_layout = new QVBoxLayout(this);
	m_layout->setContentsMargins(0, 0, 0, 0);
	m_layout->setSpacing(0);

	m_view = new QWebEngineView(this);
	m_view->setContextMenuPolicy(Qt::NoContextMenu);
	makeWidgetTransparent(m_view);
	m_view->setAttribute(Qt::WA_AlwaysStackOnTop, true);
	m_view->setStyleSheet(QStringLiteral("background: transparent;"));
	m_layout->addWidget(m_view);

	m_page = new ModernShellPage(m_view);
	m_page->setBackgroundColor(Qt::transparent);
	m_view->setPage(m_page);

	m_bridge = new ModernContextMenuHostBridge(this);
	m_channel = new QWebChannel(this);
	m_channel->registerObject(QStringLiteral("contextMenuHost"), m_bridge);
	m_page->setWebChannel(m_channel);

	m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessRemoteUrls, false);
	m_view->settings()->setAttribute(QWebEngineSettings::LocalContentCanAccessFileUrls, false);

	connect(m_view, &QWebEngineView::loadFinished, this, &ModernContextMenuHost::handleLoadFinished);
	connect(m_page, &QWebEnginePage::renderProcessTerminated, this,
			&ModernContextMenuHost::handleRenderProcessTerminated);
	connect(m_bridge, &ModernContextMenuHostBridge::actionRequested, this,
			&ModernContextMenuHost::handlePopupAction);
	connect(m_bridge, &ModernContextMenuHostBridge::closeRequested, this, &ModernContextMenuHost::handlePopupClose);
	connect(m_bridge, &ModernContextMenuHostBridge::maskRequested, this, &ModernContextMenuHost::handlePopupMask);
	connect(m_page, &ModernShellPage::externalNavigationRequested, this, [](const QUrl &url) {
		Q_UNUSED(url);
	});
}

bool ModernContextMenuHost::prewarm() {
	return start();
}

bool ModernContextMenuHost::showMenu(const QString &token, const QVariantList &items, const QPoint &globalAnchor,
									 const QString &openSubmenuLabel, const QVariantMap &uiTweaks) {
	const QString nextToken = token.trimmed();
	if (nextToken.isEmpty() || items.isEmpty()) {
		return false;
	}
	m_uiTweaks = uiTweaks;
	if (!start()) {
		return false;
	}

	m_token = nextToken;
	m_openSubmenuLabel = openSubmenuLabel.trimmed();
	m_items = items;
	const bool hasSubmenu = contextMenuHasSubmenu(items);
	m_popupLayout.clear();
	m_popupLayout.insert(QStringLiteral("rootLeft"), 0);
	m_popupLayout.insert(QStringLiteral("rootWidth"), kContextMenuRootWidth);
	m_popupLayout.insert(QStringLiteral("submenuSide"), QStringLiteral("right"));

	const QSize size = preferredMenuSize(items, globalAnchor);
	resize(size);
	move(clampedPopupPosition(globalAnchor, size, hasSubmenu));
	updatePopupMask();

	if (!m_loaded) {
		m_showWhenLoaded = true;
		return true;
	}

	publishMenuState();
	showPopup();
	return true;
}

void ModernContextMenuHost::closeEvent(QCloseEvent *event) {
	removeDismissFilter();
	const QString token = m_token;
	m_token.clear();
	m_openSubmenuLabel.clear();
	m_items.clear();
	m_popupLayout.clear();
	m_uiTweaks.clear();
	m_showWhenLoaded = false;
	clearMask();
	emit popupClosed(token);
	QWidget::closeEvent(event);
}

bool ModernContextMenuHost::eventFilter(QObject *watched, QEvent *event) {
	if (!m_dismissFilterInstalled || !isVisible() || !event) {
		return QWidget::eventFilter(watched, event);
	}

	switch (event->type()) {
		case QEvent::ApplicationDeactivate:
			close();
			return QWidget::eventFilter(watched, event);
		case QEvent::KeyPress: {
			const QKeyEvent *keyEvent = static_cast< QKeyEvent * >(event);
			if (keyEvent->key() == Qt::Key_Escape) {
				close();
				event->accept();
				return true;
			}
			break;
		}
		case QEvent::ContextMenu:
		case QEvent::MouseButtonDblClick:
		case QEvent::MouseButtonPress:
		case QEvent::Wheel:
			if (!eventTargetsPopup(watched, QCursor::pos())) {
				close();
				event->accept();
				return true;
			}
			break;
		default:
			break;
	}

	return QWidget::eventFilter(watched, event);
}

bool ModernContextMenuHost::start() {
	if (m_started) {
		return true;
	}
	if (!m_view) {
		emit hostFailed(tr("The modern context-menu view could not be initialized."));
		return false;
	}

	const QUrl url = modernContextMenuUrl(m_uiTweaks);
	installModernUiTweaksBootstrap(m_page, m_uiTweaks);
	if (!url.isValid() || url.isEmpty()) {
		emit hostFailed(tr("The modern context-menu URL is invalid."));
		return false;
	}

	m_view->load(url);
	m_started = true;
	return true;
}

void ModernContextMenuHost::handleLoadFinished(const bool ok) {
	if (!ok) {
		m_started = false;
		m_loaded  = false;
		emit hostFailed(tr("The modern context-menu renderer failed to load."));
		close();
		return;
	}

	m_loaded = true;
	if (m_showWhenLoaded && !m_token.isEmpty() && !m_items.isEmpty()) {
		m_showWhenLoaded = false;
		publishMenuState();
		showPopup();
	}
}

void ModernContextMenuHost::handleRenderProcessTerminated(
	QWebEnginePage::RenderProcessTerminationStatus status, const int exitCode) {
	Q_UNUSED(status);
	m_started = false;
	m_loaded  = false;
	emit hostFailed(tr("The modern context-menu renderer stopped unexpectedly (%1).").arg(exitCode));
	close();
}

void ModernContextMenuHost::handlePopupAction(const int actionIndex) {
	if (actionIndex < 0 || m_token.isEmpty()) {
		return;
	}

	const QString token = m_token;
	emit actionRequested(token, actionIndex);
	close();
}

void ModernContextMenuHost::handlePopupClose() {
	close();
}

void ModernContextMenuHost::showPopup() {
	show();
	raise();
	installDismissFilter();
}

bool ModernContextMenuHost::eventTargetsPopup(QObject *watched, const QPoint &globalPosition) const {
	QWidget *widget = qobject_cast< QWidget * >(watched);
	while (widget) {
		if (widget == this) {
			return true;
		}
		widget = widget->parentWidget();
	}

	if (!globalPosition.isNull()) {
		const QPoint localPosition = mapFromGlobal(globalPosition);
		if (rect().contains(localPosition)) {
			const QRegion currentMask = mask();
			return currentMask.isEmpty() || currentMask.contains(localPosition);
		}
	}

	return false;
}

void ModernContextMenuHost::installDismissFilter() {
	if (m_dismissFilterInstalled) {
		return;
	}

	if (qApp) {
		qApp->installEventFilter(this);
		m_dismissFilterInstalled = true;
	}
}

void ModernContextMenuHost::removeDismissFilter() {
	if (!m_dismissFilterInstalled) {
		return;
	}

	if (qApp) {
		qApp->removeEventFilter(this);
	}
	m_dismissFilterInstalled = false;
}

void ModernContextMenuHost::handlePopupMask(const QVariantMap &layout) {
	updatePopupMask(layout);
}

void ModernContextMenuHost::publishMenuState() {
	if (!m_page || m_token.isEmpty()) {
		return;
	}

	QVariantMap payload;
	payload.insert(QStringLiteral("token"), m_token);
	payload.insert(QStringLiteral("items"), m_items);
	payload.insert(QStringLiteral("layout"), m_popupLayout);
	if (!m_uiTweaks.isEmpty()) {
		payload.insert(QStringLiteral("uiTweaks"), m_uiTweaks);
	}
	if (!m_openSubmenuLabel.isEmpty()) {
		payload.insert(QStringLiteral("openSubmenuLabel"), m_openSubmenuLabel);
	}

	const QString script =
		QStringLiteral("if(window.__mumbleModernPopupSetItems){"
					   "window.__mumbleModernPopupSetItems(%1);"
					   "}")
			.arg(QString::fromUtf8(QJsonDocument::fromVariant(payload).toJson(QJsonDocument::Compact)));
	m_page->runJavaScript(script, QWebEngineScript::MainWorld);
}

void ModernContextMenuHost::updatePopupMask(const QVariantMap &layout) {
	QRegion mask;
	const QVariantList rects = layout.value(QStringLiteral("rects")).toList();
	if (!rects.isEmpty()) {
		for (const QVariant &rectVariant : rects) {
			const QVariantMap rectMap = rectVariant.toMap();
			const int left	 = boundedInt(rectMap, QStringLiteral("left"), 0);
			const int top	 = boundedInt(rectMap, QStringLiteral("top"), 0);
			const int width	 = boundedInt(rectMap, QStringLiteral("width"), 0);
			const int height = boundedInt(rectMap, QStringLiteral("height"), 0);
			if (width <= 0 || height <= 0) {
				continue;
			}
			mask = mask.united(roundedRectRegion(QRect(left, top, width, height), 12.0));
		}
	}

	if (mask.isEmpty()) {
		const int rootLeft = m_popupLayout.value(QStringLiteral("rootLeft"), 0).toInt() + kContextMenuHostInset;
		const int rootTop  = kContextMenuHostInset;
		const int rootHeight =
			qBound(48, contextMenuListHeight(m_items), qMax(48, height() - (kContextMenuHostInset * 2)));
		mask = roundedRectRegion(QRect(rootLeft, rootTop, kContextMenuRootWidth, rootHeight), 12.0);
	}

	setMask(mask);
}

QSize ModernContextMenuHost::preferredMenuSize(const QVariantList &items, const QPoint &globalAnchor) const {
	const int rootHeight = contextMenuListHeight(items);
	const int flyoutHeight = contextMenuMaxSubmenuHeight(items);
	const int height = qMax(rootHeight, flyoutHeight);

	const QScreen *screen = QGuiApplication::screenAt(globalAnchor);
	if (!screen) {
		screen = QGuiApplication::primaryScreen();
	}
	const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);
	const int maxHeight = qMax(128 + (kContextMenuHostInset * 2),
							   qMin(620 + (kContextMenuHostInset * 2), available.height() - 16));
	const int preferredWidth = contextMenuHasSubmenu(items)
								   ? kContextMenuRootWidth + kContextMenuFlyoutGap + kContextMenuFlyoutWidth
								   : kContextMenuRootWidth;
	const int maxWidth = qMax(kContextMenuRootWidth + (kContextMenuHostInset * 2), available.width() - 16);
	return QSize(qMin(preferredWidth + (kContextMenuHostInset * 2), maxWidth),
				 qBound(48 + (kContextMenuHostInset * 2), height + (kContextMenuHostInset * 2), maxHeight));
}

QPoint ModernContextMenuHost::clampedPopupPosition(const QPoint &globalAnchor, const QSize &size, const bool hasSubmenu) {
	const QScreen *screen = QGuiApplication::screenAt(globalAnchor);
	if (!screen) {
		screen = QGuiApplication::primaryScreen();
	}
	const QRect available = screen ? screen->availableGeometry() : QRect(0, 0, 1280, 720);

	int x = globalAnchor.x() - kContextMenuHostInset;
	int y = globalAnchor.y() - kContextMenuHostInset;
	if (hasSubmenu) {
		const int rootOffsetForLeftFlyout = kContextMenuFlyoutWidth + kContextMenuFlyoutGap;
		const bool rightFits = x + size.width() <= available.right() + 1;
		const bool leftFits = globalAnchor.x() - rootOffsetForLeftFlyout >= available.left();
		if (!rightFits && leftFits) {
			x = globalAnchor.x() - rootOffsetForLeftFlyout - kContextMenuHostInset;
			m_popupLayout.insert(QStringLiteral("rootLeft"), rootOffsetForLeftFlyout);
			m_popupLayout.insert(QStringLiteral("submenuSide"), QStringLiteral("left"));
		}
	}
	if (x + size.width() > available.right() + 1) {
		x = available.right() + 1 - size.width();
	}
	if (y + size.height() > available.bottom() + 1) {
		y = available.bottom() + 1 - size.height();
	}
	x = qMax(available.left(), x);
	y = qMax(available.top(), y);
	return QPoint(x, y);
}

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)
