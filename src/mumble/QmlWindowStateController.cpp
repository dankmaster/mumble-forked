#include "QmlWindowStateController.h"

#include <QtCore/QEvent>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QWindow>

#include <algorithm>
#include <utility>

namespace {
constexpr auto StateFormat = "mumble-qml-window-state";
constexpr int StateVersion = 1;
constexpr int SaveDebounceMilliseconds = 250;

int screenIndexByName(const QList< QmlScreenSnapshot > &screens, const QString &name) {
	if (name.isEmpty()) return -1;
	for (int index = 0; index < screens.size(); ++index) {
		if (screens.at(index).name == name) return index;
	}
	return -1;
}

int screenIndexForGeometry(const QRect &geometry, const QList< QmlScreenSnapshot > &screens,
						   const int fallbackScreen) {
	int result = -1;
	qint64 largestIntersection = 0;
	for (int index = 0; index < screens.size(); ++index) {
		const QRect intersection = geometry.intersected(screens.at(index).availableGeometry);
		const qint64 area = static_cast< qint64 >(intersection.width()) * intersection.height();
		if (area > largestIntersection) {
			largestIntersection = area;
			result = index;
		}
	}
	if (result >= 0) return result;
	return fallbackScreen >= 0 && fallbackScreen < screens.size() ? fallbackScreen : 0;
}

QScreen *runtimeScreenForPlan(const QmlWindowRestorePlan &plan, const QList< QmlScreenSnapshot > &snapshots) {
	if (plan.targetScreen < 0 || plan.targetScreen >= snapshots.size()) return nullptr;
	const QmlScreenSnapshot &target = snapshots.at(plan.targetScreen);
	for (QScreen *screen : QGuiApplication::screens()) {
		if (screen && screen->name() == target.name && screen->availableGeometry() == target.availableGeometry) {
			return screen;
		}
	}
	for (QScreen *screen : QGuiApplication::screens()) {
		if (screen && screen->name() == target.name) return screen;
	}
	return QGuiApplication::primaryScreen();
}
}

QmlWindowStateController::QmlWindowStateController(QObject *parent) : QObject(parent), m_saveTimer(new QTimer(this)) {
	m_saveTimer->setSingleShot(true);
	m_saveTimer->setInterval(SaveDebounceMilliseconds);
	connect(m_saveTimer, &QTimer::timeout, this, &QmlWindowStateController::flush);
}

QmlWindowStateController::~QmlWindowStateController() {
	flush();
	disconnectRuntimeSignals();
}

QByteArray QmlWindowStateController::encode(const QmlWindowState &state) {
	const QRect geometry = state.normalGeometry.normalized();
	const QJsonObject object { { QStringLiteral("format"), QString::fromLatin1(StateFormat) },
							   { QStringLiteral("version"), StateVersion },
							   { QStringLiteral("x"), geometry.x() },
							   { QStringLiteral("y"), geometry.y() },
							   { QStringLiteral("width"), geometry.width() },
							   { QStringLiteral("height"), geometry.height() },
							   { QStringLiteral("maximized"), state.maximized },
							   { QStringLiteral("screen"), state.screenName },
							   { QStringLiteral("devicePixelRatio"), state.devicePixelRatio } };
	return QJsonDocument(object).toJson(QJsonDocument::Compact);
}

std::optional< QmlWindowState > QmlWindowStateController::decode(const QByteArray &encodedState) {
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(encodedState, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) return std::nullopt;
	const QJsonObject object = document.object();
	if (object.value(QStringLiteral("format")).toString() != QLatin1String(StateFormat)
		|| object.value(QStringLiteral("version")).toInt() != StateVersion) {
		return std::nullopt;
	}
	const QRect geometry(object.value(QStringLiteral("x")).toInt(), object.value(QStringLiteral("y")).toInt(),
					 object.value(QStringLiteral("width")).toInt(), object.value(QStringLiteral("height")).toInt());
	if (!geometry.isValid()) return std::nullopt;
	QmlWindowState state;
	state.normalGeometry = geometry;
	state.maximized = object.value(QStringLiteral("maximized")).toBool();
	state.screenName = object.value(QStringLiteral("screen")).toString();
	state.devicePixelRatio = std::max(0.1, object.value(QStringLiteral("devicePixelRatio")).toDouble(1.0));
	return state;
}

QRect QmlWindowStateController::clampGeometry(const QRect &geometry, const QList< QRect > &availableScreens,
											const int preferredScreen, const QSize &minimumSize) {
	if (availableScreens.isEmpty()) return geometry;
	int screenIndex = -1;
	qint64 largestIntersection = 0;
	for (int index = 0; index < availableScreens.size(); ++index) {
		const QRect intersection = geometry.intersected(availableScreens.at(index));
		const qint64 area = static_cast< qint64 >(intersection.width()) * intersection.height();
		if (area > largestIntersection) {
			largestIntersection = area;
			screenIndex = index;
		}
	}
	if (screenIndex < 0) {
		screenIndex = preferredScreen >= 0 && preferredScreen < availableScreens.size() ? preferredScreen : 0;
	}
	const QRect screen = availableScreens.at(screenIndex);
	const int minimumWidth = std::min(minimumSize.width(), screen.width());
	const int minimumHeight = std::min(minimumSize.height(), screen.height());
	const int width = std::clamp(geometry.width(), minimumWidth, screen.width());
	const int height = std::clamp(geometry.height(), minimumHeight, screen.height());
	const int x = std::clamp(geometry.x(), screen.left(), screen.right() - width + 1);
	const int y = std::clamp(geometry.y(), screen.top(), screen.bottom() - height + 1);
	return QRect(x, y, width, height);
}

QmlWindowRestorePlan QmlWindowStateController::createRestorePlan(const QmlWindowState &state,
														 const QList< QmlScreenSnapshot > &screens,
														 const QString &currentScreenName,
														 const bool compositorManagedPositioning,
														 const QSize &minimumSize) {
	QmlWindowRestorePlan plan;
	plan.normalGeometry = state.normalGeometry;
	plan.restorePosition = !compositorManagedPositioning;
	if (screens.isEmpty()) return plan;

	int preferredScreen = screenIndexByName(screens, state.screenName);
	const int currentScreen = screenIndexByName(screens, currentScreenName);
	if (preferredScreen < 0) preferredScreen = currentScreen;
	if (preferredScreen < 0) preferredScreen = 0;

	if (compositorManagedPositioning) {
		// Native Wayland compositors own top-level placement. Persist a stable virtual position for
		// cross-platform settings compatibility, but apply only the bounded logical size at runtime.
		plan.targetScreen = preferredScreen;
		const QRect available = screens.at(plan.targetScreen).availableGeometry;
		const int minimumWidth = std::min(std::max(1, minimumSize.width()), available.width());
		const int minimumHeight = std::min(std::max(1, minimumSize.height()), available.height());
		const int width = std::clamp(state.normalGeometry.width(), minimumWidth, available.width());
		const int height = std::clamp(state.normalGeometry.height(), minimumHeight, available.height());
		plan.normalGeometry = QRect(available.topLeft(), QSize(width, height));
	} else {
		QList< QRect > availableScreens;
		availableScreens.reserve(screens.size());
		for (const QmlScreenSnapshot &screen : screens) availableScreens.push_back(screen.availableGeometry);
		plan.normalGeometry = clampGeometry(state.normalGeometry, availableScreens, preferredScreen, minimumSize);
		plan.targetScreen = screenIndexForGeometry(plan.normalGeometry, screens, preferredScreen);
	}
	plan.targetDevicePixelRatio = std::max(0.1, screens.at(plan.targetScreen).devicePixelRatio);
	return plan;
}

bool QmlWindowStateController::platformUsesCompositorManagedPositioning(const QString &platformName) {
	return platformName.trimmed().toLower().startsWith(QStringLiteral("wayland"));
}

void QmlWindowStateController::attach(QWindow *window, const QByteArray &encodedState, const QSize &minimumSize) {
	if (m_window == window) return;
	disconnectRuntimeSignals();
	m_window = window;
	if (!m_window) return;
	m_minimumSize = minimumSize.expandedTo(QSize(1, 1));
	m_compositorManagedPositioning =
		platformUsesCompositorManagedPositioning(QGuiApplication::platformName());

	const std::optional< QmlWindowState > restored = decode(encodedState);
	const QList< QmlScreenSnapshot > snapshots = screenSnapshots();
	QmlWindowState initialState;
	initialState.normalGeometry = restored ? restored->normalGeometry : m_window->geometry();
	initialState.maximized = restored ? restored->maximized : false;
	initialState.screenName = restored ? restored->screenName
									   : (m_window->screen() ? m_window->screen()->name() : QString());
	initialState.devicePixelRatio = restored ? restored->devicePixelRatio : m_window->devicePixelRatio();
	const QmlWindowRestorePlan plan = createRestorePlan(
		initialState, snapshots, m_window->screen() ? m_window->screen()->name() : QString(),
		m_compositorManagedPositioning, m_minimumSize);
	m_normalGeometry = plan.normalGeometry;
	if (QScreen *targetScreen = runtimeScreenForPlan(plan, snapshots);
		targetScreen && (m_compositorManagedPositioning || !m_window->screen())) {
		m_window->setScreen(targetScreen);
	}
	if (plan.restorePosition) {
		m_window->setGeometry(m_normalGeometry);
	} else {
		m_window->resize(m_normalGeometry.size());
	}
	if (restored) {
		m_window->setWindowState(restored->maximized ? Qt::WindowMaximized : Qt::WindowNoState);
	}
	m_window->installEventFilter(this);
	m_runtimeConnections.push_back(connect(m_window, &QWindow::screenChanged, this, [this](QScreen *screen) {
		observeScreen(screen);
		scheduleScreenReconcile();
	}));
	if (qGuiApp) {
		m_runtimeConnections.push_back(
			connect(qGuiApp, &QGuiApplication::screenAdded, this, [this](QScreen *) { scheduleScreenReconcile(); }));
		m_runtimeConnections.push_back(connect(qGuiApp, &QGuiApplication::screenRemoved, this,
											 [this](QScreen *screen) {
												 if (m_observedScreen == screen) observeScreen(nullptr);
												 scheduleScreenReconcile();
											 }));
		m_runtimeConnections.push_back(connect(qGuiApp, &QGuiApplication::primaryScreenChanged, this,
											 [this](QScreen *) { scheduleScreenReconcile(); }));
	}
	observeScreen(m_window->screen());
}

void QmlWindowStateController::flush() {
	if (!m_window) return;
	if (m_saveTimer->isActive()) m_saveTimer->stop();
	emit encodedStateChanged(encode(currentState()));
}

bool QmlWindowStateController::eventFilter(QObject *watched, QEvent *event) {
	if (watched == m_window && event) {
		switch (event->type()) {
			case QEvent::Move:
			case QEvent::Resize:
				if (!(m_window->windowState() & Qt::WindowMaximized) && !m_reconcilingScreenState) {
					if (m_compositorManagedPositioning) {
						const QPoint stablePosition = m_normalGeometry.isValid() ? m_normalGeometry.topLeft() : QPoint();
						m_normalGeometry = QRect(stablePosition, m_window->size());
					} else {
						m_normalGeometry = m_window->geometry();
					}
				}
				scheduleSave();
				break;
			case QEvent::WindowStateChange:
			case QEvent::Show:
			case QEvent::Hide: scheduleSave(); break;
			default: break;
		}
	}
	return QObject::eventFilter(watched, event);
}

void QmlWindowStateController::scheduleSave() {
	if (m_saveTimer) m_saveTimer->start();
}

void QmlWindowStateController::scheduleScreenReconcile() {
	if (m_screenReconcilePending) return;
	m_screenReconcilePending = true;
	QTimer::singleShot(0, this, [this]() {
		m_screenReconcilePending = false;
		reconcileScreenState();
	});
}

void QmlWindowStateController::reconcileScreenState() {
	if (!m_window || m_reconcilingScreenState) return;
	const QList< QmlScreenSnapshot > snapshots = screenSnapshots();
	if (snapshots.isEmpty()) return;

	QmlWindowState state = currentState();
	state.normalGeometry = m_normalGeometry.isValid() ? m_normalGeometry : m_window->geometry();
	const QmlWindowRestorePlan plan = createRestorePlan(
		state, snapshots, m_window->screen() ? m_window->screen()->name() : QString(),
		m_compositorManagedPositioning, m_minimumSize);
	m_reconcilingScreenState = true;
	m_normalGeometry = plan.normalGeometry;
	if (QScreen *targetScreen = runtimeScreenForPlan(plan, snapshots); targetScreen && !m_window->screen()) {
		m_window->setScreen(targetScreen);
	}
	if (!(m_window->windowState() & Qt::WindowMaximized)) {
		if (plan.restorePosition) {
			if (m_window->geometry() != plan.normalGeometry) m_window->setGeometry(plan.normalGeometry);
		} else if (m_window->size() != plan.normalGeometry.size()) {
			m_window->resize(plan.normalGeometry.size());
		}
	}
	m_reconcilingScreenState = false;
	observeScreen(m_window->screen());
	scheduleSave();
}

void QmlWindowStateController::observeScreen(QScreen *screen) {
	for (const QMetaObject::Connection &connection : std::as_const(m_screenConnections)) {
		disconnect(connection);
	}
	m_screenConnections.clear();
	m_observedScreen = screen;
	if (!screen) return;
	const auto reconcile = [this]() { scheduleScreenReconcile(); };
	const auto saveDpi = [this]() { scheduleSave(); };
	m_screenConnections.push_back(connect(screen, &QScreen::geometryChanged, this,
										  [reconcile](const QRect &) { reconcile(); }));
	m_screenConnections.push_back(connect(screen, &QScreen::availableGeometryChanged, this,
										  [reconcile](const QRect &) { reconcile(); }));
	m_screenConnections.push_back(connect(screen, &QScreen::virtualGeometryChanged, this,
										  [reconcile](const QRect &) { reconcile(); }));
	m_screenConnections.push_back(connect(screen, &QScreen::logicalDotsPerInchChanged, this,
										  [saveDpi](qreal) { saveDpi(); }));
	m_screenConnections.push_back(connect(screen, &QScreen::physicalDotsPerInchChanged, this,
										  [saveDpi](qreal) { saveDpi(); }));
}

void QmlWindowStateController::disconnectRuntimeSignals() {
	if (m_window) m_window->removeEventFilter(this);
	for (const QMetaObject::Connection &connection : std::as_const(m_runtimeConnections)) {
		disconnect(connection);
	}
	for (const QMetaObject::Connection &connection : std::as_const(m_screenConnections)) {
		disconnect(connection);
	}
	m_runtimeConnections.clear();
	m_screenConnections.clear();
	m_observedScreen = nullptr;
	m_screenReconcilePending = false;
}

QList< QmlScreenSnapshot > QmlWindowStateController::screenSnapshots() const {
	QList< QmlScreenSnapshot > snapshots;
	const QList< QScreen * > screens = QGuiApplication::screens();
	snapshots.reserve(screens.size());
	for (QScreen *screen : screens) {
		if (!screen || !screen->availableGeometry().isValid()) continue;
		snapshots.push_back({ screen->name(), screen->availableGeometry(), screen->devicePixelRatio() });
	}
	return snapshots;
}

QmlWindowState QmlWindowStateController::currentState() const {
	QmlWindowState state;
	state.normalGeometry = m_normalGeometry.isValid() ? m_normalGeometry : m_window->geometry();
	state.maximized = m_window->windowState() & Qt::WindowMaximized;
	if (QScreen *screen = m_window->screen()) {
		state.screenName = screen->name();
		state.devicePixelRatio = screen->devicePixelRatio();
	}
	return state;
}
