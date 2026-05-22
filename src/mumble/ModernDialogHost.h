// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNDIALOGHOST_H_
#define MUMBLE_MUMBLE_MODERNDIALOGHOST_H_

#if defined(MUMBLE_HAS_MODERN_LAYOUT)

#include <QtCore/QVariantMap>
#include <QtWebEngineCore/QWebEnginePage>
#include <QtWidgets/QDialog>

class ModernShellBridge;
class ModernShellPage;
class QVBoxLayout;
class QWebChannel;
class QWebEngineView;

class ModernDialogHost : public QDialog {
private:
	Q_OBJECT
	Q_DISABLE_COPY(ModernDialogHost)

public:
	ModernDialogHost(ModernShellBridge *bridge, QWidget *parent = nullptr);

	bool showDialogState(const QVariantMap &state, QString *errorMessage = nullptr);
	void hideDialog();

signals:
	void nativeCloseRequested(const QString &dialogID);
	void hostFailed(const QString &reason);

protected:
	void closeEvent(QCloseEvent *event) override;

private slots:
	void handleLoadFinished(bool ok);
	void handleRenderProcessTerminated(QWebEnginePage::RenderProcessTerminationStatus status, int exitCode);

private:
	bool start(QString *errorMessage = nullptr);
	void applyDialogGeometry(const QVariantMap &state);

	QVBoxLayout *m_layout = nullptr;
	QWebEngineView *m_view = nullptr;
	ModernShellPage *m_page = nullptr;
	QWebChannel *m_channel = nullptr;
	ModernShellBridge *m_bridge = nullptr;
	bool m_started = false;
	bool m_open = false;
	QString m_currentDialogID;
};

#endif // defined(MUMBLE_HAS_MODERN_LAYOUT)

#endif // MUMBLE_MUMBLE_MODERNDIALOGHOST_H_
