// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNCONTEXTMENUHOST_H_
#define MUMBLE_MUMBLE_MODERNCONTEXTMENUHOST_H_

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include <QtCore/QPoint>
#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantList>
#include <QtCore/QVariantMap>
#include <QtWebEngineCore/QWebEnginePage>
#include <QtWidgets/QWidget>

class ModernShellPage;
class QCloseEvent;
class QVBoxLayout;
class QWebChannel;
class QWebEngineView;

class ModernContextMenuHostBridge : public QObject {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ModernContextMenuHostBridge)

public:
	explicit ModernContextMenuHostBridge(QObject *parent = nullptr);

	Q_INVOKABLE void activateAction(int actionIndex);
	Q_INVOKABLE void closePopup();
	Q_INVOKABLE void updateMask(const QVariantMap &layout);

signals:
	void actionRequested(int actionIndex);
	void closeRequested();
	void maskRequested(const QVariantMap &layout);
};

class ModernContextMenuHost : public QWidget {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ModernContextMenuHost)

public:
	explicit ModernContextMenuHost(QWidget *parent = nullptr);

	bool prewarm();
	bool showMenu(const QString &token, const QVariantList &items, const QPoint &globalAnchor,
				  const QString &openSubmenuLabel = QString(), const QVariantMap &uiTweaks = QVariantMap());
	bool showMenuAtGlobalPosition(const QString &token, const QVariantList &items, const QPoint &globalAnchor,
								  const QString &openSubmenuLabel = QString(),
								  const QVariantMap &uiTweaks = QVariantMap());

signals:
	void actionRequested(const QString &token, int actionIndex);
	void popupClosed(const QString &token);
	void hostFailed(const QString &reason);

protected:
	void closeEvent(QCloseEvent *event) override;
	bool eventFilter(QObject *watched, QEvent *event) override;

private slots:
	void handleLoadFinished(bool ok);
	void handleRenderProcessTerminated(QWebEnginePage::RenderProcessTerminationStatus status, int exitCode);
	void handlePopupAction(int actionIndex);
	void handlePopupClose();
	void handlePopupMask(const QVariantMap &layout);

private:
	bool start();
	void showPopup();
	bool eventTargetsPopup(QObject *watched, const QPoint &globalPosition = QPoint()) const;
	void installDismissFilter();
	void removeDismissFilter();
	void publishMenuState();
	void updatePopupMask(const QVariantMap &layout = QVariantMap());
	QSize preferredMenuSize(const QVariantList &items, const QPoint &globalAnchor) const;
	QPoint clampedPopupPosition(const QPoint &globalAnchor, const QSize &size, bool hasSubmenu);

	QVBoxLayout *m_layout = nullptr;
	QWebEngineView *m_view = nullptr;
	ModernShellPage *m_page = nullptr;
	QWebChannel *m_channel = nullptr;
	ModernContextMenuHostBridge *m_bridge = nullptr;
	QString m_token;
	QString m_openSubmenuLabel;
	QVariantList m_items;
	QVariantMap m_popupLayout;
	QVariantMap m_uiTweaks;
	bool m_started = false;
	bool m_loaded = false;
	bool m_showWhenLoaded = false;
	bool m_dismissFilterInstalled = false;
};

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)

#endif // MUMBLE_MUMBLE_MODERNCONTEXTMENUHOST_H_
