// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNSHELLHOST_H_
#define MUMBLE_MUMBLE_MODERNSHELLHOST_H_

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include <QtCore/QUrl>
#include <QtCore/QSize>
#include <QtCore/QVariantMap>
#include <QtGui/QImage>
#include <QtCore/QPointer>
#include <QtWidgets/QWidget>
#include <QtWebEngineCore/QWebEnginePage>

class ModernShellBridge;
class ModernContextMenuHost;
class ModernShellPage;
class QTimer;
class QVBoxLayout;
class QWebChannel;
class QWebEngineProfile;
class QWebEngineUrlRequestInterceptor;
class QWebEngineView;

class ModernShellHost : public QWidget {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ModernShellHost)

public:
	explicit ModernShellHost(QWidget *parent = nullptr);

	bool start(QString *errorMessage = nullptr);
	ModernShellBridge *bridge() const;
	QWebEngineProfile *webProfile() const;
	void publishHostViewportMetrics(const QSize &viewportSize = QSize(), bool openRail = false);
	void runAutomationScript(const QString &script);
	QVariant runAutomationScriptResult(const QString &script, int timeoutMilliseconds = 3000);
	QVariantMap lastNativeContextMenuRequest() const;
	void clearLastNativeContextMenuRequest();
	void closeNativeContextMenuForAutomation();

signals:
	void bootFailed(const QString &reason);
	void imageDropped(const QImage &image);
	void imageUrlsDropped(const QList< QUrl > &urls);

private slots:
	void handleLoadFinished(bool ok);
	void handleRenderProcessTerminated(QWebEnginePage::RenderProcessTerminationStatus status, int exitCode);
	void handleBridgeBootReady();
	void handleBootTimeout();
	void showNativeContextMenu(const QVariantMap &request);

private:
	bool eventFilter(QObject *watched, QEvent *event) override;

	void notifyNativeContextMenuClosed(const QString &token);
	void invokeNativeContextMenuAction(const QString &token, int actionIndex);
	void closeNativeContextMenu();
	ModernContextMenuHost *ensureNativeContextMenuHost();
	void openProviderSession(const QString &href);

	QVBoxLayout *m_layout = nullptr;
	QWebEngineView *m_view = nullptr;
	QWebEngineProfile *m_profile                          = nullptr;
	ModernShellPage *m_page = nullptr;
	QWebChannel *m_channel = nullptr;
	ModernShellBridge *m_bridge = nullptr;
	QWebEngineUrlRequestInterceptor *m_requestInterceptor = nullptr;
	QPointer< QWebEngineView > m_providerSessionView;
	QTimer *m_bootTimeoutTimer = nullptr;
	QPointer< ModernContextMenuHost > m_nativeContextMenu;
	QVariantMap m_lastNativeContextMenuRequest;
	bool m_started = false;
	bool m_bootReady = false;
};

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)

#endif // MUMBLE_MUMBLE_MODERNSHELLHOST_H_
