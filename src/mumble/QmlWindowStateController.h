#ifndef MUMBLE_MUMBLE_QMLWINDOWSTATECONTROLLER_H_
#define MUMBLE_MUMBLE_QMLWINDOWSTATECONTROLLER_H_

#include <QtCore/QByteArray>
#include <QtCore/QList>
#include <QtCore/QObject>
#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtCore/QSize>
#include <QtCore/QString>

#include <optional>

class QEvent;
class QTimer;
class QWindow;

struct QmlWindowState {
	QRect normalGeometry;
	bool maximized = false;
	QString screenName;
	qreal devicePixelRatio = 1.0;
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

signals:
	void encodedStateChanged(const QByteArray &encodedState);

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void scheduleSave();
	QmlWindowState currentState() const;

	QPointer< QWindow > m_window;
	QTimer *m_saveTimer = nullptr;
	QRect m_normalGeometry;
};

#endif
