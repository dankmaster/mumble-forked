#ifndef MUMBLE_MUMBLE_QMLWINDOWSTATECONTROLLER_H_
#define MUMBLE_MUMBLE_QMLWINDOWSTATECONTROLLER_H_

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QMetaObject>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtCore/QString>

#include <optional>

class QEvent;
class QScreen;
class QTimer;
class QWindow;

struct QmlWindowState {
	QRect normalGeometry;
	bool maximized = false;
	QString screenName;
	qreal devicePixelRatio = 1.0;
};

struct QmlScreenSnapshot {
	QString name;
	QRect availableGeometry;
	qreal devicePixelRatio = 1.0;
};

struct QmlWindowRestorePlan {
	QRect normalGeometry;
	int targetScreen = -1;
	qreal targetDevicePixelRatio = 1.0;
	bool restorePosition = true;
};

class QmlWindowStateController final : public QObject {
	Q_OBJECT

public:
	explicit QmlWindowStateController(QObject *parent = nullptr);
	~QmlWindowStateController() override;

	void attach(QWindow *window, const QByteArray &encodedState);
	void flush();

	static QByteArray encode(const QmlWindowState &state);
	static std::optional< QmlWindowState > decode(const QByteArray &encodedState);
	static QRect clampGeometry(const QRect &geometry, const QList< QRect > &availableScreens,
							   int preferredScreen = -1, const QSize &minimumSize = QSize(760, 520));
	static QmlWindowRestorePlan createRestorePlan(const QmlWindowState &state,
												  const QList< QmlScreenSnapshot > &screens,
												  const QString &currentScreenName,
												  bool compositorManagedPositioning,
												  const QSize &minimumSize = QSize(760, 520));
	static bool platformUsesCompositorManagedPositioning(const QString &platformName);

signals:
	void encodedStateChanged(const QByteArray &encodedState);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void scheduleSave();
	void scheduleScreenReconcile();
	void reconcileScreenState();
	void observeScreen(QScreen *screen);
	void disconnectRuntimeSignals();
	QList< QmlScreenSnapshot > screenSnapshots() const;
	QmlWindowState currentState() const;

	QPointer< QWindow > m_window;
	QPointer< QScreen > m_observedScreen;
	QTimer *m_saveTimer = nullptr;
	QRect m_normalGeometry;
	QList< QMetaObject::Connection > m_runtimeConnections;
	QList< QMetaObject::Connection > m_screenConnections;
	bool m_compositorManagedPositioning = false;
	bool m_screenReconcilePending = false;
	bool m_reconcilingScreenState = false;
};

#endif
