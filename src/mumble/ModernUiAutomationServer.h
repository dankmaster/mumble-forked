// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNUIAUTOMATIONSERVER_H_
#define MUMBLE_MUMBLE_MODERNUIAUTOMATIONSERVER_H_

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>

class MainWindow;
class QEvent;
class QTcpServer;
class QTcpSocket;
class QWidget;

class ModernUiAutomationServer : public QObject {
public:
	explicit ModernUiAutomationServer(MainWindow *mainWindow, QObject *parent = nullptr);

	bool start(QString *errorMessage = nullptr);
	bool isListening() const;
	quint16 port() const;

protected:
	bool eventFilter(QObject *watched, QEvent *event) override;

private:
	void handleNewConnection();
	void handleReadyRead(QTcpSocket *socket);
	QVariantMap handleRequest(const QVariantMap &request);
	QVariantMap buildStateResponse() const;
	void writeResponse(QTcpSocket *socket, const QVariantMap &response) const;
	bool authorizeRequest(const QVariantMap &request, QVariantMap &response) const;
	bool automationOffscreenModeEnabled() const;
	void installAutomationOffscreenFilter();
	void prepareTopLevelWidgetForAutomation(QWidget *widget) const;

	MainWindow *m_mainWindow = nullptr;
	QTcpServer *m_server     = nullptr;
	QString m_token;
	bool m_offscreenFilterInstalled = false;
};

#endif // MUMBLE_MUMBLE_MODERNUIAUTOMATIONSERVER_H_
