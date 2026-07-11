// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNDIALOGHOST_H_
#define MUMBLE_MUMBLE_MODERNDIALOGHOST_H_

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include <QtCore/QPoint>
#include <QtCore/QPointer>
#include <QtCore/QRect>
#include <QtCore/QVariantMap>
#include <QtWebEngineCore/QWebEnginePage>
#include <QtWidgets/QDialog>

class ModernShellBridge;
class ModernShellPage;
class QMoveEvent;
class QResizeEvent;
class QTimer;
class QVBoxLayout;
class QWebChannel;
class QWebEngineView;

class ModernDialogHost : public QDialog {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ModernDialogHost)

public:
	ModernDialogHost(ModernShellBridge *bridge, QWidget *parent = nullptr);
	~ModernDialogHost() override;

	bool showDialogState(const QVariantMap &state, QString *errorMessage = nullptr);
	void hideDialog();
	QVariant runAutomationScriptResult(const QString &script, int timeoutMilliseconds = 3000);
	Q_INVOKABLE void acknowledgeDialogState(const QString &dialogID);

signals:
	void nativeCloseRequested(const QString &dialogID);
	void hostFailed(const QString &reason);

protected:
	void closeEvent(QCloseEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;
	void moveEvent(QMoveEvent *event) override;
	void resizeEvent(QResizeEvent *event) override;

private slots:
	void handleLoadFinished(bool ok);
	void handleRenderProcessTerminated(QWebEnginePage::RenderProcessTerminationStatus status, int exitCode);
	void republishDialogState();

private:
	bool start(QString *errorMessage = nullptr);
	void applyDialogGeometry(const QVariantMap &state);
	void applyWindowChrome(const QVariantMap &state);
	void applyWindowRoleForDialog(const QVariantMap &state);
	bool automationOffscreenModeEnabled() const;
	void applyAutomationOffscreenFlags();
	void showForAutomationCapture();
	void queueDialogStateRepublish();
	bool isImageViewerDialog() const;
	void rememberImageViewerGeometry();
	Qt::Edges resizeEdgesAtGlobalPoint(const QPoint &globalPosition) const;
	void updateResizeCursor(const QPoint &globalPosition);
	void clearResizeCursor();
	void beginManualResize(const QPoint &globalPosition, Qt::Edges edges);
	void trackManualResize(const QPoint &globalPosition);
	void finishManualResize(bool commitGeometry);
	bool shouldStartWindowDrag(const QPoint &viewPosition) const;

	QVBoxLayout *m_layout = nullptr;
	QWebEngineView *m_view = nullptr;
	ModernShellPage *m_page = nullptr;
	QWebChannel *m_channel = nullptr;
	QTimer *m_stateRepublishTimer = nullptr;
	ModernShellBridge *m_bridge = nullptr;
	QPointer< QWidget > m_owner;
	QVariantMap m_lastDialogState;
	bool m_started = false;
	bool m_open = false;
	bool m_dialogBridgeReady = false;
	bool m_manualDragActive = false;
	bool m_manualResizeActive = false;
	bool m_resizeCursorActive = false;
	bool m_independentWindowRole = false;
	int m_stateRepublishRemaining = 0;
	QPoint m_manualDragOffset;
	QPoint m_manualResizeStartGlobalPosition;
	QRect m_manualResizeStartGeometry;
	Qt::Edges m_manualResizeEdges;
	QString m_currentDialogID;
};

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)

#endif // MUMBLE_MUMBLE_MODERNDIALOGHOST_H_
