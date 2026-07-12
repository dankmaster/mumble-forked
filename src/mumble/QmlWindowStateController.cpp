#include "QmlWindowStateController.h"

#include <QtCore/QEvent>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QTimer>
#include <QtGui/QGuiApplication>
#include <QtGui/QScreen>
#include <QtGui/QWindow>

#include <algorithm>

namespace {
constexpr auto StateFormat = "mumble-qml-window-state";
constexpr int StateVersion = 1;
constexpr int SaveDebounceMilliseconds = 250;
}

QmlWindowStateController::QmlWindowStateController(QObject *parent) : QObject(parent), m_saveTimer(new QTimer(this)) {
	m_saveTimer->setSingleShot(true);
	m_saveTimer->setInterval(SaveDebounceMilliseconds);
	connect(m_saveTimer, &QTimer::timeout, this, &QmlWindowStateController::flush);
}

QmlWindowStateController::~QmlWindowStateController() {
	flush();
	if (m_window) m_window->removeEventFilter(this);
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

void QmlWindowStateController::attach(QWindow *window, const QByteArray &encodedState) {
	if (m_window == window) return;
	if (m_window) m_window->removeEventFilter(this);
	m_window = window;
	if (!m_window) return;

	QList< QRect > availableScreens;
	int preferredScreen = -1;
	const std::optional< QmlWindowState > restored = decode(encodedState);
	const QList< QScreen * > screens = QGuiApplication::screens();
	for (int index = 0; index < screens.size(); ++index) {
		availableScreens.push_back(screens.at(index)->availableGeometry());
		if (restored && screens.at(index)->name() == restored->screenName) preferredScreen = index;
	}
	if (restored) {
		m_normalGeometry = clampGeometry(restored->normalGeometry, availableScreens, preferredScreen);
		m_window->setGeometry(m_normalGeometry);
		m_window->setWindowState(restored->maximized ? Qt::WindowMaximized : Qt::WindowNoState);
	} else {
		m_normalGeometry = clampGeometry(m_window->geometry(), availableScreens);
		m_window->setGeometry(m_normalGeometry);
	}
	m_window->installEventFilter(this);
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
				if (!(m_window->windowState() & Qt::WindowMaximized)) m_normalGeometry = m_window->geometry();
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
