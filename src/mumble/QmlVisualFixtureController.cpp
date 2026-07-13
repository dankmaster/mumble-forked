// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "QmlVisualFixtureController.h"

#include "QmlClientModels.h"
#include "QmlShellHost.h"
#include "QmlThemeController.h"

#include <QtCore/QEventLoop>
#include <QtCore/QTimer>
#include <QtQuick/QQuickItem>
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
		|| width < 420 || height < 520 || width > 4096 || height > 2160
		|| !QStringList { QStringLiteral("light"), QStringLiteral("dark") }.contains(theme)
		|| !QStringList { QStringLiteral("regular"), QStringLiteral("compact") }.contains(layout)) {
		if (error) *error = QStringLiteral("The visual fixture request is invalid or unsupported.");
		return {};
	}

	const bool previousFixtureOverride = m_host->visualFixtureOverrideActive();
	m_host->setVisualFixtureOverrideActive(true);
	struct FixtureOverrideRollback {
		QmlShellHost *host;
		bool previous;
		bool committed = false;
		~FixtureOverrideRollback() {
			if (!committed) host->setVisualFixtureOverrideActive(previous);
		}
	} fixtureOverrideRollback { m_host, previousFixtureOverride };
	QQuickWindow *window = m_host->window();
	const int expectedMessageCount = state == QLatin1String("connected") ? 2 : 0;
	{
		struct MutationScope {
			QmlShellHost *host;
			explicit MutationScope(QmlShellHost *value) : host(value) { host->setVisualFixtureMutationActive(true); }
			~MutationScope() { host->setVisualFixtureMutationActive(false); }
		} mutationScope(m_host);
		if (!m_host->themeController()->applyVisualGateAppearance(theme, layout)) {
			if (error) *error = QStringLiteral("The visual fixture appearance is unsupported.");
			return {};
		}
		window->setWidth(width);
		window->setHeight(height);
		applyState(state);
		if (m_host->chatModel()->rowCount() != expectedMessageCount) {
			if (error) {
				*error = QStringLiteral("Visual fixture timeline contains %1 messages immediately after injection; expected %2.")
						 .arg(m_host->chatModel()->rowCount())
						 .arg(expectedMessageCount);
			}
			return {};
		}
	}
	QVariant focusTarget;
	window->requestActivate();
	if (!QMetaObject::invokeMethod(window, "focusVisualFixture", Q_RETURN_ARG(QVariant, focusTarget),
								  Q_ARG(QVariant, QVariant(state)))) {
		if (error) *error = QStringLiteral("The visual fixture could not establish a deterministic focus target.");
		return {};
	}
	QQuickItem *requestedFocusItem = qobject_cast< QQuickItem * >(focusTarget.value< QObject * >());
	const QString requestedFocusName = requestedFocusItem ? requestedFocusItem->objectName().trimmed() : QString();
	if (!requestedFocusItem || requestedFocusName.isEmpty()) {
		if (error) *error = QStringLiteral("The visual fixture returned an invalid focus target.");
		return {};
	}
	if (!waitForPresentedFrame(error)) return {};
	QQuickItem *activeFocusItem = window->activeFocusItem();
	bool requestedTargetOwnsFocus = false;
	for (QQuickItem *item = activeFocusItem; item; item = item->parentItem()) {
		if (item == requestedFocusItem) {
			requestedTargetOwnsFocus = true;
			break;
		}
	}
	if (!requestedTargetOwnsFocus) {
		if (error) {
			*error = QStringLiteral("The visual fixture focus target '%1' did not receive active focus.")
					 .arg(requestedFocusName);
		}
		return {};
	}
	if (m_host->chatModel()->rowCount() != expectedMessageCount) {
		if (error) {
			*error = QStringLiteral("Visual fixture timeline was clobbered before presentation: observed %1 messages, expected %2.")
					 .arg(m_host->chatModel()->rowCount())
					 .arg(expectedMessageCount);
		}
		return {};
	}
	fixtureOverrideRollback.committed = true;
	++m_generation;
	return { { QStringLiteral("case_id"), caseId }, { QStringLiteral("state"), state },
			 { QStringLiteral("theme"), theme }, { QStringLiteral("layout"), layout },
			 { QStringLiteral("width"), window->width() }, { QStringLiteral("height"), window->height() },
			 { QStringLiteral("message_count"), m_host->chatModel()->rowCount() },
			 { QStringLiteral("focus_target"), requestedFocusName },
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
	DialogStateController *dialog = m_host->dialogController();
	rooms->replaceDirectMessageStates({});
	participants->replaceParticipantStates({});
	chat->replaceMessages({});
	operations->clear();
	dialog->applyState({ { QStringLiteral("open"), false } });
	session->setUpdateBanner({});
	session->setMotdHtml({});
	session->setMotdSummary({});
	session->setSelfMuted(false);
	session->setSelfDeafened(false);

	if (state == QLatin1String("connected")) {
		session->setConnected(true);
		session->setServerName(QStringLiteral("Mumble Visual Fixture"));
		session->setConnectionLabel(QStringLiteral("Connected"));
		session->setConnectionState(QStringLiteral("connected"));
		session->setConnectionTone(QStringLiteral("success"));
		session->setConnectionDetail({});
		session->setConnectionRetryRemainingMs(0);
		session->setCanConnect(false);
		session->setCanCancel(false);
		session->setSelfStatusLabel(QStringLiteral("Online"));
		session->setSelfName(QStringLiteral("Demo User"));
		const QVariantList voiceRooms {
			QVariantMap { { QStringLiteral("token"), QStringLiteral("0:1") }, { QStringLiteral("label"), QStringLiteral("Lobby") },
						  { QStringLiteral("selected"), true }, { QStringLiteral("joined"), true }, { QStringLiteral("depth"), 0 } },
			QVariantMap { { QStringLiteral("token"), QStringLiteral("0:2") }, { QStringLiteral("label"), QStringLiteral("Studio") },
						  { QStringLiteral("depth"), 0 } }
		};
		const QVariantList textRoomActions {
			QVariantMap { { QStringLiteral("kind"), QStringLiteral("action") },
						  { QStringLiteral("id"), QStringLiteral("markRead") },
						  { QStringLiteral("label"), QStringLiteral("Mark read") },
						  { QStringLiteral("enabled"), true }, { QStringLiteral("visible"), true },
						  { QStringLiteral("checkable"), false }, { QStringLiteral("checked"), false } }
		};
		const QVariantList textRooms {
			QVariantMap { { QStringLiteral("token"), QStringLiteral("3:1") },
						  { QStringLiteral("label"), QStringLiteral("#general") },
						  { QStringLiteral("description"), QStringLiteral("Text room") },
						  { QStringLiteral("selected"), false }, { QStringLiteral("depth"), 0 },
						  { QStringLiteral("actions"), textRoomActions } }
		};
		rooms->replaceRoomStates(voiceRooms, textRooms);
		participants->replaceParticipantStates({
			QVariantMap { { QStringLiteral("session"), QStringLiteral("101") }, { QStringLiteral("name"), QStringLiteral("Demo User") },
							{ QStringLiteral("statusLabel"), QStringLiteral("Listening") }, { QStringLiteral("talkState"), QStringLiteral("passive") } },
			QVariantMap { { QStringLiteral("session"), QStringLiteral("102") }, { QStringLiteral("name"), QStringLiteral("Alex") },
							{ QStringLiteral("statusLabel"), QStringLiteral("Talking") }, { QStringLiteral("talkState"), QStringLiteral("talking") } }
		});
		// Scope changes can synchronously publish the live conversation. Apply the
		// fixture timeline last so those signal side-effects cannot clear it while
		// the scoped fixture mutation is active.
		scope->applyState({ { QStringLiteral("scopeToken"), QStringLiteral("0:1") }, { QStringLiteral("label"), QStringLiteral("Lobby") },
							{ QStringLiteral("description"), QStringLiteral("Voice room") }, { QStringLiteral("kindLabel"), QStringLiteral("VOICE") },
							{ QStringLiteral("composerPlaceholder"), QStringLiteral("Message Lobby") }, { QStringLiteral("canSend"), true },
							{ QStringLiteral("canAttachImages"), true } });
		const QVariantList fixtureMessages {
			QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("fixture:1") }, { QStringLiteral("actor"), QStringLiteral("Alex") },
							{ QStringLiteral("bodyText"), QStringLiteral("Welcome to the deterministic visual fixture.") },
							{ QStringLiteral("timeLabel"), QStringLiteral("10:24") }, { QStringLiteral("canReply"), true },
							{ QStringLiteral("preview"), QVariantMap() }, { QStringLiteral("attachments"), QVariantList() },
							{ QStringLiteral("reactions"), QVariantList() } },
			QVariantMap { { QStringLiteral("messageKey"), QStringLiteral("fixture:2") }, { QStringLiteral("actor"), QStringLiteral("Demo User") },
							{ QStringLiteral("bodyText"), QStringLiteral("Qt Quick is ready for review.") },
							{ QStringLiteral("timeLabel"), QStringLiteral("10:25") }, { QStringLiteral("own"), true },
							{ QStringLiteral("preview"), QVariantMap() }, { QStringLiteral("attachments"), QVariantList() },
							{ QStringLiteral("reactions"), QVariantList() } }
		};
		chat->replaceMessages(fixtureMessages);
		return;
	}

	session->setConnected(false);
	session->setServerName(QStringLiteral("Mumble"));
	session->setConnectionLabel(state == QLatin1String("loading") ? QStringLiteral("Connecting…") : QStringLiteral("Disconnected"));
	session->setConnectionState(state == QLatin1String("loading") ? QStringLiteral("connecting")
															 : QStringLiteral("disconnected"));
	session->setConnectionTone(state == QLatin1String("error") ? QStringLiteral("danger") : QStringLiteral("muted"));
	session->setConnectionDetail({});
	session->setConnectionRetryRemainingMs(0);
	session->setCanConnect(state != QLatin1String("loading"));
	session->setCanCancel(state == QLatin1String("loading"));
	session->setSelfStatusLabel(QStringLiteral("Offline"));
	session->setSelfName(QStringLiteral("You"));
	rooms->replaceRoomStates({}, {});
	scope->applyState({ { QStringLiteral("label"), state == QLatin1String("empty") ? QStringLiteral("No conversation selected")
																								 : QStringLiteral("Connection") },
						 { QStringLiteral("description"), QStringLiteral("Choose a server to begin") },
						 { QStringLiteral("kindLabel"), QStringLiteral("STATUS") }, { QStringLiteral("canSend"), false } });
	if (state == QLatin1String("loading")) {
		operations->startOperation(QStringLiteral("visual:loading"), QStringLiteral("Connecting"),
								   QStringLiteral("Loading rooms and participants…"), false);
		// A real running operation starts indeterminate, which deliberately animates
		// its ProgressBar. Visual-gate fixtures must instead render the same loading
		// surface at a stable point in time so repeated screenshots are byte-stable.
		// This mutation is scoped to the synthetic fixture and does not change how
		// production operations report or animate unknown progress.
		operations->updateProgress(QStringLiteral("visual:loading"), 42, 100);
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
