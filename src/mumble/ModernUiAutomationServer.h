// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNUIAUTOMATIONSERVER_H_
#define MUMBLE_MUMBLE_MODERNUIAUTOMATIONSERVER_H_

#include <QtCore/QObject>
#include <QtCore/QString>
#include <QtCore/QVariantMap>
#include <QtCore/QVariantList>

#include <memory>

class MainWindow;
class QmlVisualFixtureController;
class QmlShellHost;
class QEvent;
class QTcpServer;
class QTcpSocket;
class QWidget;

class ModernUiAutomationServer : public QObject {
	Q_OBJECT

public:
	explicit ModernUiAutomationServer(MainWindow *mainWindow, QObject *parent = nullptr);
	~ModernUiAutomationServer() override;

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
	QmlVisualFixtureController *visualFixtureController();
	bool automationOffscreenModeEnabled() const;
	void installAutomationOffscreenFilter();
	void prepareTopLevelWidgetForAutomation(QWidget *widget) const;
	QVariantMap finalizeChatPerformanceWorkload(QmlShellHost *host);
	QVariantMap finalizeTalkPerformanceWorkload(QmlShellHost *host);

	MainWindow *m_mainWindow = nullptr;
	QTcpServer *m_server     = nullptr;
	QString m_token;
	std::unique_ptr< QmlVisualFixtureController > m_visualFixtureController;
	bool m_offscreenFilterInstalled = false;
	struct ChatPerformanceWorkloadState {
		bool active = false;
		bool previousFixtureOverride = false;
		QVariantList liveMessages;
		int presentedFramesBeforeSeed = 0;
	} m_chatPerformanceWorkload;
	struct TalkPerformanceWorkloadState {
		bool active = false;
		bool previousFixtureOverride = false;
		QVariantList liveParticipants;
		QString sessionId;
		bool talking = false;
		int transitionCount = 0;
		int presentedFramesBefore = 0;
	} m_talkPerformanceWorkload;
};

#endif // MUMBLE_MUMBLE_MODERNUIAUTOMATIONSERVER_H_
