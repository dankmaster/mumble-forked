// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_QMLVISUALFIXTURECONTROLLER_H_
#define MUMBLE_MUMBLE_QMLVISUALFIXTURECONTROLLER_H_

#include <QtCore/QString>
#include <QtCore/QPointer>
#include <QtCore/QVariantMap>

#include <memory>

class QmlShellHost;
class QObject;
class QQuickItem;
class QQuickWindow;

class QmlVisualFixtureController {
public:
	explicit QmlVisualFixtureController(QmlShellHost *host);
	~QmlVisualFixtureController();
	void setHost(QmlShellHost *host) { m_host = host; }

	QVariantMap capabilities() const;
	QVariantMap apply(const QVariantMap &request, QString *error = nullptr);
	bool ensureFocus(const QString &windowId, QString *error = nullptr);
	qulonglong generation() const { return m_generation; }
	double actualDevicePixelRatio() const;

private:
	QQuickWindow *waitForCaptureWindow(const QString &windowId, QString *error);
	bool waitForPresentedFrame(QString *error, QQuickWindow *window = nullptr);
	bool applySurface(const QString &surfaceVariant, QString *captureWindow, QString *error);
	void resetSurfaceFixtures(bool preserveDetachedMediaWindow = false, bool preserveProductMenus = false);
	void applyState(const QString &state, const QString &motdVariant, const QString &richPreviewVariant,
					const QString &richPreviewSize, const QString &caseVariant,
					const QString &surfaceVariant);

	QmlShellHost *m_host = nullptr;
	std::unique_ptr< QObject > m_visualScreenShareBackend;
	QPointer< QObject > m_visualScreenShareView;
	QPointer< QQuickWindow > m_visualAttachmentViewer;
	QPointer< QQuickWindow > m_visualImageViewer;
	QPointer< QQuickWindow > m_focusWindow;
	QPointer< QQuickItem > m_focusItem;
	QString m_focusWindowId;
	QString m_focusItemName;
	QString m_focusState;
	QString m_focusSurfaceVariant;
	qulonglong m_generation = 0;
};

#endif // MUMBLE_MUMBLE_QMLVISUALFIXTURECONTROLLER_H_
