// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlVisualFixtureController.h"

#include "QmlClientModels.h"
#include "QmlShellHost.h"
#include "QmlThemeController.h"

#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtQuick/QQuickWindow>

QmlVisualFixtureController::QmlVisualFixtureController(QmlShellHost *host) : m_host(host) {
}

double QmlVisualFixtureController::actualDevicePixelRatio() const {
	return m_host && m_host->window() ? m_host->window()->devicePixelRatio() : 0.0;
}

QVariantMap QmlVisualFixtureController::capabilities() const {
	const bool available = m_host && m_host->window();
	return { { QStringLiteral("capture"), available }, { QStringLiteral("state_injection"), available },
			 { QStringLiteral("window_resize"), available }, { QStringLiteral("theme_override"), available },
			 { QStringLiteral("accessibility_snapshot"), available },
			 { QStringLiteral("supported_states"), QStringList { QStringLiteral("empty"), QStringLiteral("loading"),
															 QStringLiteral("error"), QStringLiteral("connected") } },
			 { QStringLiteral("actual_device_pixel_ratio"), actualDevicePixelRatio() } };
}

QVariantMap QmlVisualFixtureController::apply(const QVariantMap &request, QString *error) {
	if (!m_host || !m_host->window()) {
		if (error) *error = QStringLiteral("The Qt Quick frontend is not active.");
		return {};
	}
	const QString state = request.value(QStringLiteral("state")).toString().trimmed().toLower();
	const QString theme = request.value(QStringLiteral("theme")).toString().trimmed().toLower();
	const QString layout = request.value(QStringLiteral("layout")).toString().trimmed().toLower();
	const QString caseId = request.value(QStringLiteral("case_id")).toString().trimmed();
	const int width = request.value(QStringLiteral("width")).toInt();
	const int height = request.value(QStringLiteral("height")).toInt();
	if (caseId.isEmpty() || !QStringList { QStringLiteral("empty"), QStringLiteral("loading"),
										 QStringLiteral("error"), QStringLiteral("connected") }.contains(state)
		|| width < 760 || height < 520 || width > 4096 || height > 2160
		|| !m_host->themeController()->applyVisualGateAppearance(theme, layout)) {
		if (error) *error = QStringLiteral("The visual fixture request is invalid or unsupported.");
		return {};
	}

	QQuickWindow *window = m_host->window();
	window->setWidth(width);
	window->setHeight(height);
	applyState(state);
	if (!waitForPresentedFrame(error)) return {};
	++m_generation;
	return { { QStringLiteral("case_id"), caseId }, { QStringLiteral("state"), state },
			 { QStringLiteral("theme"), theme }, { QStringLiteral("layout"), layout },
			 { QStringLiteral("width"), window->width() }, { QStringLiteral("height"), window->height() },
			 { QStringLiteral("actual_device_pixel_ratio"), actualDevicePixelRatio() },
			 { QStringLiteral("generation"), m_generation } };
}

void QmlVisualFixtureController::applyState(const QString &state) {
	ClientSessionController *session = m_host->sessionController();
	ActiveScopeController *scope = m_host->activeScopeController();
	RoomModel *rooms = m_host->roomModel();
	ParticipantModel *participants = m_host->participantModel();
	ChatTimelineModel *chat = m_host->chatModel();
	AsyncOperationModel *operations = m_host->operationModel();
	rooms->replaceDirectMessageStates({});
	participants->replaceParticipantStates({});
	chat->replaceMessages({});
	operations->clear();
	session->setUpdateBanner({});
	session->setMotdHtml({});
	session->setMotdSummary({});
	session->setSelfMuted(false);
	session->setSelfDeafened(false);

	if (state == QLatin1String("connected")) {
		session->setConnected(true);
		session->setServerName(QStringLiteral("Mumble Visual Fixture"));
		session->setConnectionLabel(QStringLiteral("Connected"));
		session->setSelfName(QStringLiteral("Demo User"));
		const QVariantList voiceRooms {
			QVariantMap { { QStringLiteral("token"), QStringLiteral("1:1") }, { QStringLiteral("label"), QStringLiteral("Lobby") },
						  { QStringLiteral("selected"), true }, { QStringLiteral("joined"), true }, { QStringLiteral("depth"), 0 } },
			QVariantMap { { QStringLiteral("token"), QStringLiteral("1:2") }, { QStringLiteral("label"), QStringLiteral("Studio") },
						  { QStringLiteral("depth"), 0 } }
		};
		rooms->replaceRoomStates(voiceRooms, {});
		participants->replaceParticipantStates({
			QVariantMap { { QStringLiteral("session"), QStringLiteral("101") }, { QStringLiteral("name"), QStringLiteral("Demo User") },
							{ QStringLiteral("statusLabel"), QStringLiteral("Listening") }, { QStringLiteral("talkState"), QStringLiteral("passive") } },
			QVariantMap { { QStringLiteral("session"), QStringLiteral("102") }, { QStringLiteral("name"), QStringLiteral("Alex") },
							{ QStringLiteral("statusLabel"), QStringLiteral("Talking") }, { QStringLiteral("talkState"), QStringLiteral("talking") } }
		});
		chat->replaceMessages({
			QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("fixture:1") }, { QStringLiteral("actor"), QStringLiteral("Alex") },
							{ QStringLiteral("bodyText"), QStringLiteral("Welcome to the deterministic visual fixture.") },
							{ QStringLiteral("timeLabel"), QStringLiteral("10:24") }, { QStringLiteral("canReply"), true } },
			QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("fixture:2") }, { QStringLiteral("actor"), QStringLiteral("Demo User") },
							{ QStringLiteral("bodyText"), QStringLiteral("Qt Quick is ready for review.") },
							{ QStringLiteral("timeLabel"), QStringLiteral("10:25") }, { QStringLiteral("own"), true } }
		});
		scope->applyState({ { QStringLiteral("scopeToken"), QStringLiteral("1:1") }, { QStringLiteral("label"), QStringLiteral("Lobby") },
							{ QStringLiteral("description"), QStringLiteral("Voice room") }, { QStringLiteral("kindLabel"), QStringLiteral("VOICE") },
							{ QStringLiteral("composerPlaceholder"), QStringLiteral("Message Lobby") }, { QStringLiteral("canSend"), true },
							{ QStringLiteral("canAttachImages"), true } });
		return;
	}

	session->setConnected(false);
	session->setServerName(QStringLiteral("Mumble"));
	session->setConnectionLabel(state == QLatin1String("loading") ? QStringLiteral("Connecting…") : QStringLiteral("Disconnected"));
	session->setSelfName(QStringLiteral("You"));
	rooms->replaceRoomStates({}, {});
	scope->applyState({ { QStringLiteral("label"), state == QLatin1String("empty") ? QStringLiteral("No conversation selected")
																								 : QStringLiteral("Connection") },
						 { QStringLiteral("description"), QStringLiteral("Choose a server to begin") },
						 { QStringLiteral("kindLabel"), QStringLiteral("STATUS") }, { QStringLiteral("canSend"), false } });
	if (state == QLatin1String("loading")) {
		operations->startOperation(QStringLiteral("visual:loading"), QStringLiteral("Connecting"),
								   QStringLiteral("Loading rooms and participants…"), false);
	} else if (state == QLatin1String("error")) {
		operations->startOperation(QStringLiteral("visual:error"), QStringLiteral("Connection failed"),
								   QStringLiteral("The test server could not be reached."), false);
		operations->finishOperation(QStringLiteral("visual:error"), false, QStringLiteral("fixture-error"),
									QStringLiteral("Retry the connection."));
	}
}

bool QmlVisualFixtureController::waitForPresentedFrame(QString *error) {
	QQuickWindow *window = m_host ? m_host->window() : nullptr;
	if (!window || !window->isExposed()) {
		if (error) *error = QStringLiteral("The Qt Quick window is not exposed for visual capture.");
		return false;
	}
	QEventLoop loop;
	QTimer timeout;
	timeout.setSingleShot(true);
	bool presented = false;
	const QMetaObject::Connection frameConnection = QObject::connect(window, &QQuickWindow::frameSwapped, &loop, [&]() {
		presented = true;
		loop.quit();
	}, Qt::QueuedConnection);
	QObject::connect(&timeout, &QTimer::timeout, &loop, &QEventLoop::quit);
	timeout.start(5000);
	window->requestUpdate();
	loop.exec();
	QObject::disconnect(frameConnection);
	if (!presented && error) *error = QStringLiteral("Timed out waiting for a presented Qt Quick frame.");
	return presented;
}
