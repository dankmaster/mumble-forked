// Copyright The Mumble Developers. All rights reserved.

#include "QmlClientModels.h"

#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

class TestQmlClientModels : public QObject {
	Q_OBJECT

private slots:
	void stableRowsUpdateWithoutReset();
	void synchronizeRowsUsesIncrementalSignals();
	void roomAndParticipantStatesStayIncremental();
	void stableIdsRemainIndependentFromSourceMaps();
	void messageRolesExposeStructuredState();
	void chatTimelineAppliesDirectIncrementalMessages();
	void participantPresenceUpdatesOnlyTypedRoles();
	void duplicateStableIdsAreCoalesced();
	void activeScopeAppliesTypedState();
	void sessionPropertiesOnlyNotifyOnChanges();
	void sessionPublishesTypedUpdateBanner();
	void commandsRejectEmptyStableIds();
	void pttStateIsIdempotentAndReleases();
	void dialogStateRoutesTypedRequests();
	void imageViewerStateRemainsStructured();
	void asyncOperationsExposeProgressAndCancellation();
	void asyncOperationsClampProgressAndInterruptByPrefix();
	void mediaSessionValidatesAndPublishesTypedState();
	void selectionStateOnlyNotifiesForRealChanges();
	void invalidRowsAndCommandsAreIgnored();
};

void TestQmlClientModels::stableRowsUpdateWithoutReset() {
	RoomModel model;
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
	QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
	QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);

	model.upsertRow({ { QStringLiteral("id"), QStringLiteral("voice:1") },
					  { QStringLiteral("title"), QStringLiteral("Lobby") } });
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(insertSpy.count(), 1);
	QCOMPARE(resetSpy.count(), 0);

	model.upsertRow({ { QStringLiteral("id"), QStringLiteral("voice:1") },
					  { QStringLiteral("title"), QStringLiteral("Landing") } });
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(changedSpy.count(), 1);
	QCOMPARE(model.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Landing"));

	model.removeRow(QStringLiteral("voice:1"));
	QCOMPARE(model.rowCount(), 0);
	QCOMPARE(removeSpy.count(), 1);
}

void TestQmlClientModels::activeScopeAppliesTypedState() {
	ActiveScopeController scope;
	QSignalSpy labelSpy(&scope, &ActiveScopeController::labelChanged);
	QSignalSpy sendSpy(&scope, &ActiveScopeController::canSendChanged);
	scope.applyState({ { QStringLiteral("scopeToken"), QStringLiteral("channel:42") },
					   { QStringLiteral("label"), QStringLiteral("Lobby") },
					   { QStringLiteral("description"), QStringLiteral("General voice room") },
					   { QStringLiteral("kindLabel"), QStringLiteral("Voice room") },
					   { QStringLiteral("composerPlaceholder"), QStringLiteral("Write in Lobby...") },
					   { QStringLiteral("canSend"), true } });
	QCOMPARE(scope.scopeToken(), QStringLiteral("channel:42"));
	QCOMPARE(scope.label(), QStringLiteral("Lobby"));
	QVERIFY(scope.canSend());
	QCOMPARE(labelSpy.count(), 1);
	QCOMPARE(sendSpy.count(), 1);

	scope.applyState({ { QStringLiteral("scopeToken"), QStringLiteral("channel:42") },
					   { QStringLiteral("label"), QStringLiteral("Lobby") },
					   { QStringLiteral("description"), QStringLiteral("General voice room") },
					   { QStringLiteral("kindLabel"), QStringLiteral("Voice room") },
					   { QStringLiteral("composerPlaceholder"), QStringLiteral("Write in Lobby...") },
					   { QStringLiteral("canSend"), true } });
	QCOMPARE(labelSpy.count(), 1);
	QCOMPARE(sendSpy.count(), 1);
}

void TestQmlClientModels::synchronizeRowsUsesIncrementalSignals() {
	RoomModel model;
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
	QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy moveSpy(&model, &QAbstractItemModel::rowsMoved);
	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
	QSignalSpy removeSpy(&model, &QAbstractItemModel::rowsRemoved);

	model.synchronizeRows({ QVariantMap { { QStringLiteral("id"), QStringLiteral("voice:1") },
										 { QStringLiteral("title"), QStringLiteral("Lobby") } },
						 QVariantMap { { QStringLiteral("id"), QStringLiteral("voice:2") },
										 { QStringLiteral("title"), QStringLiteral("Games") } } });
	QCOMPARE(model.rowCount(), 2);
	QCOMPARE(insertSpy.count(), 2);

	model.synchronizeRows({ QVariantMap { { QStringLiteral("id"), QStringLiteral("voice:2") },
										 { QStringLiteral("title"), QStringLiteral("Gaming") } } });
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("voice:2"));
	QCOMPARE(model.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Gaming"));
	QCOMPARE(moveSpy.count(), 1);
	QCOMPARE(changedSpy.count(), 1);
	QCOMPARE(removeSpy.count(), 1);
	QCOMPARE(resetSpy.count(), 0);
}

void TestQmlClientModels::roomAndParticipantStatesStayIncremental() {
	RoomModel rooms;
	QSignalSpy roomResetSpy(&rooms, &QAbstractItemModel::modelReset);
	QSignalSpy roomInsertSpy(&rooms, &QAbstractItemModel::rowsInserted);
	QSignalSpy roomChangedSpy(&rooms, &QAbstractItemModel::dataChanged);
	rooms.replaceRoomStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("channel:1") },
						{ QStringLiteral("label"), QStringLiteral("Lobby") },
						{ QStringLiteral("joined"), true } } },
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("text:2") },
						{ QStringLiteral("label"), QStringLiteral("General") } } });
	QCOMPARE(rooms.rowCount(), 2);
	QCOMPARE(roomInsertSpy.count(), 2);
	QCOMPARE(roomResetSpy.count(), 0);

	rooms.replaceRoomStates(
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("channel:1") },
						{ QStringLiteral("label"), QStringLiteral("Lobby") },
						{ QStringLiteral("joined"), true }, { QStringLiteral("unreadCount"), 3 } } },
		{ QVariantMap { { QStringLiteral("token"), QStringLiteral("text:2") },
						{ QStringLiteral("label"), QStringLiteral("General") } } });
	QCOMPARE(rooms.rowCount(), 2);
	QCOMPARE(roomInsertSpy.count(), 2);
	QCOMPARE(roomChangedSpy.count(), 1);
	QCOMPARE(roomResetSpy.count(), 0);

	ParticipantModel participants;
	QSignalSpy participantResetSpy(&participants, &QAbstractItemModel::modelReset);
	QSignalSpy participantChangedSpy(&participants, &QAbstractItemModel::dataChanged);
	const QVariantList initialParticipants { QVariantMap { { QStringLiteral("session"), 7 },
														{ QStringLiteral("name"), QStringLiteral("Alice") },
														{ QStringLiteral("talkState"), QStringLiteral("passive") } } };
	participants.replaceParticipantStates(initialParticipants);
	participants.replaceParticipantStates(
		{ QVariantMap { { QStringLiteral("session"), 7 }, { QStringLiteral("name"), QStringLiteral("Alice") },
						{ QStringLiteral("talkState"), QStringLiteral("talking") } } });
	QCOMPARE(participants.rowCount(), 1);
	QCOMPARE(participantChangedSpy.count(), 1);
	QCOMPARE(participantResetSpy.count(), 0);
}

void TestQmlClientModels::stableIdsRemainIndependentFromSourceMaps() {
	RoomModel model;
	QVariantMap source { { QStringLiteral("id"), QStringLiteral("voice:1") },
						 { QStringLiteral("title"), QStringLiteral("Lobby") } };
	model.synchronizeRows({ source });
	source.insert(QStringLiteral("id"), QStringLiteral("mutated"));
	source.insert(QStringLiteral("title"), QStringLiteral("Mutated"));

	model.upsertRow({ { QStringLiteral("id"), QStringLiteral("voice:1") },
						  { QStringLiteral("title"), QStringLiteral("Landing") } });
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("voice:1"));
	QCOMPARE(model.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Landing"));
}

void TestQmlClientModels::messageRolesExposeStructuredState() {
	ChatTimelineModel model;
	model.upsertMessage({ { QStringLiteral("messageKey"), QStringLiteral("message:7") },
						  { QStringLiteral("title"), QStringLiteral("Alice") },
						  { QStringLiteral("subtitle"), QStringLiteral("Hello") },
						  { QStringLiteral("timestamp"), QStringLiteral("12:30") },
						  { QStringLiteral("replyActor"), QStringLiteral("Bob") },
						  { QStringLiteral("replySnippet"), QStringLiteral("Earlier") },
						  { QStringLiteral("reactions"), QVariantList { QVariantMap {
							  { QStringLiteral("emoji"), QStringLiteral("+") }, { QStringLiteral("count"), 2 } } } },
						  { QStringLiteral("preview"), QVariantMap {
							  { QStringLiteral("title"), QStringLiteral("Example") } } },
						  { QStringLiteral("canReply"), true } });

	const QHash< int, QByteArray > roles = model.roleNames();
	const auto roleForName = [&roles](const QByteArray &name) {
		for (auto it = roles.cbegin(); it != roles.cend(); ++it) {
			if (it.value() == name) return it.key();
		}
		return -1;
	};
	const QModelIndex row = model.index(0, 0);
	QCOMPARE(model.data(row, roleForName("timestamp")).toString(), QStringLiteral("12:30"));
	QCOMPARE(model.data(row, roleForName("replyActor")).toString(), QStringLiteral("Bob"));
	QCOMPARE(model.data(row, roleForName("reactions")).toList().size(), 1);
	QCOMPARE(model.data(row, roleForName("preview")).toMap().value(QStringLiteral("title")).toString(),
			 QStringLiteral("Example"));
	QVERIFY(model.data(row, roleForName("canReply")).toBool());
}

void TestQmlClientModels::chatTimelineAppliesDirectIncrementalMessages() {
	ChatTimelineModel model;
	QSignalSpy resetSpy(&model, &QAbstractItemModel::modelReset);
	QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);

	QVERIFY(model.upsertMessage({ { QStringLiteral("messageId"), 41 },
								 { QStringLiteral("actor"), QStringLiteral("Alice") },
								 { QStringLiteral("bodyText"), QStringLiteral("First") } }));
	QCOMPARE(insertSpy.count(), 1);
	QCOMPARE(resetSpy.count(), 0);
	QCOMPARE(model.get(0).value(QStringLiteral("id")).toString(), QStringLiteral("41"));

	QVERIFY(model.upsertMessage({ { QStringLiteral("messageId"), 41 },
								 { QStringLiteral("actor"), QStringLiteral("Alice") },
								 { QStringLiteral("bodyText"), QStringLiteral("Edited") },
								 { QStringLiteral("deliveryState"), QStringLiteral("delivered") } }));
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(changedSpy.count(), 1);
	QCOMPARE(resetSpy.count(), 0);
	QCOMPARE(model.get(0).value(QStringLiteral("subtitle")).toString(), QStringLiteral("Edited"));

	const int appended = model.appendMessages(
		{ QVariantMap { { QStringLiteral("messageId"), 42 }, { QStringLiteral("bodyText"), QStringLiteral("Second") } },
		  QVariantMap { { QStringLiteral("bodyText"), QStringLiteral("Missing stable ID") } } });
	QCOMPARE(appended, 1);
	QCOMPARE(model.rowCount(), 2);
	QCOMPARE(insertSpy.count(), 2);
	QCOMPARE(resetSpy.count(), 0);
}

void TestQmlClientModels::participantPresenceUpdatesOnlyTypedRoles() {
	ParticipantModel model;
	QSignalSpy changedSpy(&model, &QAbstractItemModel::dataChanged);
	model.upsertRow({ { QStringLiteral("id"), QStringLiteral("7") },
					  { QStringLiteral("title"), QStringLiteral("Alice") },
					  { QStringLiteral("avatarUrl"), QStringLiteral("image://avatars/7") },
					  { QStringLiteral("status"), QStringLiteral("passive") },
					  { QStringLiteral("source"), QVariantMap {
							{ QStringLiteral("session"), 7 }, { QStringLiteral("label"), QStringLiteral("Alice") },
							{ QStringLiteral("talkState"), QStringLiteral("passive") } } } });
	changedSpy.clear();

	const QVariantList badges { QStringLiteral("Priority speaker") };
	const QVariantList statuses { QVariantMap { { QStringLiteral("id"), QStringLiteral("talking") } } };
	model.updatePresence(QStringLiteral(" 7 "), QStringLiteral("talking"), QStringLiteral("Talking"),
						 QStringLiteral("success"), true, false, badges, statuses);
	QCOMPARE(changedSpy.count(), 1);
	const QVariantMap row = model.get(0);
	QCOMPARE(row.value(QStringLiteral("title")).toString(), QStringLiteral("Alice"));
	QCOMPARE(row.value(QStringLiteral("avatarUrl")).toString(), QStringLiteral("image://avatars/7"));
	QCOMPARE(row.value(QStringLiteral("status")).toString(), QStringLiteral("talking"));
	const QVariantMap source = row.value(QStringLiteral("source")).toMap();
	QCOMPARE(source.value(QStringLiteral("label")).toString(), QStringLiteral("Alice"));
	QCOMPARE(source.value(QStringLiteral("talkLabel")).toString(), QStringLiteral("Talking"));
	QCOMPARE(source.value(QStringLiteral("talkTone")).toString(), QStringLiteral("success"));
	QVERIFY(source.value(QStringLiteral("talking")).toBool());
	QCOMPARE(source.value(QStringLiteral("badges")).toList(), badges);
	model.updatePresence(QStringLiteral("7"), QStringLiteral("talking"), QStringLiteral("Talking"),
						 QStringLiteral("success"), true, false, badges, statuses);
	QCOMPARE(changedSpy.count(), 1);

	model.updatePresence(QStringLiteral("99"), QStringLiteral("talking"), QString(), QString(), true, false, {}, {});
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(changedSpy.count(), 1);
}

void TestQmlClientModels::duplicateStableIdsAreCoalesced() {
	RoomModel model;
	model.synchronizeRows({ QVariantMap { { QStringLiteral("id"), QStringLiteral("voice:0:0") },
										 { QStringLiteral("title"), QStringLiteral("Root voice") } },
						 QVariantMap { { QStringLiteral("id"), QStringLiteral("voice:0:0") },
										 { QStringLiteral("title"), QStringLiteral("Root replacement") } } });
	QCOMPARE(model.rowCount(), 1);
	QCOMPARE(model.get(0).value(QStringLiteral("title")).toString(), QStringLiteral("Root replacement"));

	model.synchronizeRows({ QVariantMap { { QStringLiteral("id"), QStringLiteral("voice:0:0") } },
						 QVariantMap { { QStringLiteral("id"), QStringLiteral("text:0:0") } } });
	QCOMPARE(model.rowCount(), 2);
}

void TestQmlClientModels::sessionPropertiesOnlyNotifyOnChanges() {
	ClientSessionController session;
	QSignalSpy spy(&session, &ClientSessionController::connectedChanged);
	session.setConnected(false);
	QCOMPARE(spy.count(), 0);
	session.setConnected(true);
	QCOMPARE(spy.count(), 1);
	QVERIFY(session.connected());
}

void TestQmlClientModels::sessionPublishesTypedUpdateBanner() {
	ClientSessionController session;
	QSignalSpy spy(&session, &ClientSessionController::updateBannerChanged);
	const QVariantMap banner { { QStringLiteral("visible"), true },
							 { QStringLiteral("phase"), QStringLiteral("downloading") },
							 { QStringLiteral("title"), QStringLiteral("Downloading update") },
							 { QStringLiteral("progressPercent"), 42 } };
	session.setUpdateBanner(banner);
	QCOMPARE(spy.count(), 1);
	QCOMPARE(session.updateBanner(), banner);
	session.setUpdateBanner(banner);
	QCOMPARE(spy.count(), 1);
	session.setUpdateBanner({});
	QCOMPARE(spy.count(), 2);
	QVERIFY(session.updateBanner().isEmpty());
}

void TestQmlClientModels::commandsRejectEmptyStableIds() {
	UiCommandController commands;
	QSignalSpy scopeSpy(&commands, &UiCommandController::scopeSelectionRequested);
	QSignalSpy actionSpy(&commands, &UiCommandController::actionRequested);
	QSignalSpy participantSpy(&commands, &UiCommandController::participantSelectionRequested);
	QSignalSpy directMessageSpy(&commands, &UiCommandController::directMessageOpenRequested);
	commands.selectScope(QStringLiteral("   "));
	commands.invokeAction(QString());
	commands.selectParticipant(QStringLiteral("  "));
	commands.openDirectMessage(QString());
	QCOMPARE(scopeSpy.count(), 0);
	QCOMPARE(actionSpy.count(), 0);
	QCOMPARE(participantSpy.count(), 0);
	QCOMPARE(directMessageSpy.count(), 0);
	commands.selectScope(QStringLiteral(" channel:42 "));
	commands.invokeAction(QStringLiteral(" qaAudioMute "));
	commands.selectParticipant(QStringLiteral(" 42 "));
	commands.openDirectMessage(QStringLiteral(" 7 "));
	QCOMPARE(scopeSpy.takeFirst().at(0).toString(), QStringLiteral("channel:42"));
	QCOMPARE(actionSpy.takeFirst().at(0).toString(), QStringLiteral("qaAudioMute"));
	QCOMPARE(participantSpy.takeFirst().at(0).toString(), QStringLiteral("42"));
	QCOMPARE(directMessageSpy.takeFirst().at(0).toString(), QStringLiteral("7"));
}

void TestQmlClientModels::pttStateIsIdempotentAndReleases() {
	UiCommandController commands;
	QSignalSpy stateSpy(&commands, &UiCommandController::pttStateRequested);
	commands.setPttPressed(true);
	commands.setPttPressed(true);
	QVERIFY(commands.pttPressed());
	QCOMPARE(stateSpy.count(), 1);
	QCOMPARE(stateSpy.takeFirst().at(0).toBool(), true);

	commands.releasePtt();
	QVERIFY(!commands.pttPressed());
	QCOMPARE(stateSpy.count(), 1);
	QCOMPARE(stateSpy.takeFirst().at(0).toBool(), false);
	commands.releasePtt();
	QCOMPARE(stateSpy.count(), 0);
}

void TestQmlClientModels::dialogStateRoutesTypedRequests() {
	DialogStateController dialog;
	QSignalSpy stateSpy(&dialog, &DialogStateController::stateChanged);
	QSignalSpy fieldSpy(&dialog, &DialogStateController::fieldUpdateRequested);
	QSignalSpy actionSpy(&dialog, &DialogStateController::actionRequested);
	QSignalSpy closeSpy(&dialog, &DialogStateController::closeRequested);

	dialog.updateField(QStringLiteral("audio.input"), 1);
	dialog.invokeAction(QStringLiteral("ok"));
	dialog.requestClose();
	QCOMPARE(fieldSpy.count(), 0);
	QCOMPARE(actionSpy.count(), 0);
	QCOMPARE(closeSpy.count(), 0);

	dialog.applyState({ { QStringLiteral("open"), true }, { QStringLiteral("id"), QStringLiteral("settings") },
						{ QStringLiteral("kind"), QStringLiteral("settings") },
						{ QStringLiteral("title"), QStringLiteral("Settings") },
						{ QStringLiteral("sections"),
						  QVariantList { QVariantMap { { QStringLiteral("fields"),
												 QVariantList { QVariantMap {
													 { QStringLiteral("id"), QStringLiteral("audio.input") },
													 { QStringLiteral("value"), 2 } } } } } } },
						{ QStringLiteral("errors"),
						  QVariantMap { { QStringLiteral("audio.input"), QStringLiteral("Choose an input") } } } });
	QCOMPARE(stateSpy.count(), 1);
	QVERIFY(dialog.open());
	QCOMPARE(dialog.dialogId(), QStringLiteral("settings"));
	QCOMPARE(dialog.fieldValue(QStringLiteral("audio.input")).toInt(), 2);
	QCOMPARE(dialog.fieldError(QStringLiteral("audio.input")), QStringLiteral("Choose an input"));
	QVERIFY(!dialog.fieldValue(QStringLiteral("missing")).isValid());

	dialog.updateField(QStringLiteral(" audio.input "), 2);
	QCOMPARE(fieldSpy.count(), 1);
	QCOMPARE(fieldSpy.takeFirst().at(0).toString(), QStringLiteral("settings"));
	dialog.invokeAction(QStringLiteral(" selectPage "), { { QStringLiteral("pageId"), QStringLiteral("plugins") } });
	QCOMPARE(actionSpy.count(), 1);
	QCOMPARE(actionSpy.takeFirst().at(1).toString(), QStringLiteral("selectPage"));
	dialog.requestClose();
	QCOMPARE(closeSpy.count(), 1);
}

void TestQmlClientModels::imageViewerStateRemainsStructured() {
	DialogStateController dialog;
	QSignalSpy closeSpy(&dialog, &DialogStateController::closeRequested);
	const QVariantMap image { { QStringLiteral("src"), QStringLiteral("data:image/png;base64,AA==") },
							  { QStringLiteral("width"), 1920 }, { QStringLiteral("height"), 1080 } };
	dialog.applyState({ { QStringLiteral("open"), true }, { QStringLiteral("id"), QStringLiteral("imageViewer") },
						{ QStringLiteral("kind"), QStringLiteral("imageViewer") },
						{ QStringLiteral("title"), QStringLiteral("Screenshot") },
						{ QStringLiteral("imageViewer"), image } });

	QCOMPARE(dialog.kind(), QStringLiteral("imageViewer"));
	QCOMPARE(dialog.state().value(QStringLiteral("imageViewer")).toMap(), image);
	dialog.requestClose();
	QCOMPARE(closeSpy.count(), 1);
	QCOMPARE(closeSpy.takeFirst().at(0).toString(), QStringLiteral("imageViewer"));
}

void TestQmlClientModels::asyncOperationsExposeProgressAndCancellation() {
	AsyncOperationModel operations;
	QSignalSpy cancelSpy(&operations, &AsyncOperationModel::cancellationRequested);
	operations.startOperation(QStringLiteral("plugin-update:7"), QStringLiteral("Example"),
							  QStringLiteral("Downloading"), true);
	QCOMPARE(operations.rowCount(), 1);
	QCOMPARE(operations.get(0).value(QStringLiteral("status")).toString(), QStringLiteral("running"));

	operations.updateProgress(QStringLiteral("plugin-update:7"), 25, 100);
	QCOMPARE(operations.get(0).value(QStringLiteral("payload")).toMap().value(QStringLiteral("progress")).toInt(),
			 25);
	operations.cancel(QStringLiteral(" plugin-update:7 "));
	QCOMPARE(cancelSpy.count(), 1);
	QCOMPARE(cancelSpy.takeFirst().at(0).toString(), QStringLiteral("plugin-update:7"));

	operations.finishOperation(QStringLiteral("plugin-update:7"), false, QStringLiteral("network-error"),
							   QStringLiteral("Offline"));
	const QVariantMap finished = operations.get(0);
	QCOMPARE(finished.value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
	QCOMPARE(finished.value(QStringLiteral("subtitle")).toString(), QStringLiteral("Offline"));
	QVERIFY(!finished.value(QStringLiteral("payload")).toMap().value(QStringLiteral("cancellable")).toBool());

	operations.dismiss(QStringLiteral(" plugin-update:7 "));
	QCOMPARE(operations.rowCount(), 0);

	operations.startOperation(QStringLiteral("plugin-update:8"), QStringLiteral("Another plugin"),
							  QStringLiteral("Downloading"), true);
	operations.dismiss(QStringLiteral("plugin-update:8"));
	QCOMPARE(operations.rowCount(), 1);
}

void TestQmlClientModels::asyncOperationsClampProgressAndInterruptByPrefix() {
	AsyncOperationModel operations;
	operations.startOperation(QStringLiteral("plugin-update:one"), QStringLiteral("One"), QString(), true);
	operations.startOperation(QStringLiteral("plugin-update:two"), QStringLiteral("Two"), QString(), true);
	operations.startOperation(QStringLiteral("download:one"), QStringLiteral("Download"), QString(), true);

	operations.updateProgress(QStringLiteral("plugin-update:one"), 125, 100);
	QCOMPARE(operations.get(0).value(QStringLiteral("payload")).toMap().value(QStringLiteral("progress")).toInt(),
			 100);
	operations.updateProgress(QStringLiteral("plugin-update:two"), 5, 0);
	const QVariantMap indeterminate = operations.get(1).value(QStringLiteral("payload")).toMap();
	QCOMPARE(indeterminate.value(QStringLiteral("progress")).toInt(), -1);
	QVERIFY(indeterminate.value(QStringLiteral("indeterminate")).toBool());

	operations.interruptOperations(QStringLiteral("plugin-update:"));
	QCOMPARE(operations.get(0).value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
	QCOMPARE(operations.get(1).value(QStringLiteral("status")).toString(), QStringLiteral("failed"));
	QCOMPARE(operations.get(2).value(QStringLiteral("status")).toString(), QStringLiteral("running"));
}

void TestQmlClientModels::selectionStateOnlyNotifiesForRealChanges() {
	QmlSelectionState selection;
	QSignalSpy scopeSpy(&selection, &QmlSelectionState::scopeTokenChanged);
	QSignalSpy userSpy(&selection, &QmlSelectionState::selectedUserSessionChanged);
	QSignalSpy channelSpy(&selection, &QmlSelectionState::selectedVoiceChannelIdChanged);

	selection.setScopeToken(QStringLiteral("channel:42"));
	selection.setSelectedUserSession(7);
	selection.setSelectedVoiceChannelId(42);
	selection.setScopeToken(QStringLiteral("channel:42"));
	selection.setSelectedUserSession(7);
	selection.setSelectedVoiceChannelId(42);

	QCOMPARE(scopeSpy.count(), 1);
	QCOMPARE(userSpy.count(), 1);
	QCOMPARE(channelSpy.count(), 1);
	QCOMPARE(selection.scopeToken(), QStringLiteral("channel:42"));
	QCOMPARE(selection.selectedUserSession().toInt(), 7);
	QCOMPARE(selection.selectedVoiceChannelId().toInt(), 42);
}

void TestQmlClientModels::invalidRowsAndCommandsAreIgnored() {
	RoomModel model;
	QSignalSpy insertSpy(&model, &QAbstractItemModel::rowsInserted);
	QSignalSpy countSpy(&model, &StableListModel::countChanged);
	model.upsertRow({ { QStringLiteral("title"), QStringLiteral("Missing ID") } });
	model.synchronizeRows({ QVariantMap { { QStringLiteral("id"), QString() } }, QVariantMap {} });
	QCOMPARE(model.rowCount(), 0);
	QCOMPARE(insertSpy.count(), 0);
	QCOMPARE(countSpy.count(), 0);

	UiCommandController commands;
	QSignalSpy joinSpy(&commands, &UiCommandController::voiceJoinRequested);
	QSignalSpy messageSpy(&commands, &UiCommandController::messageSendRequested);
	commands.joinVoiceChannel(QStringLiteral("  "));
	commands.sendMessage(QStringLiteral("  "));
	QCOMPARE(joinSpy.count(), 0);
	QCOMPARE(messageSpy.count(), 0);
	commands.joinVoiceChannel(QStringLiteral(" channel:42 "));
	commands.sendMessage(QStringLiteral(" hello "));
	QCOMPARE(joinSpy.takeFirst().at(0).toString(), QStringLiteral("channel:42"));
	QCOMPARE(messageSpy.takeFirst().at(0).toString(), QStringLiteral(" hello "));
}

void TestQmlClientModels::mediaSessionValidatesAndPublishesTypedState() {
	MediaSessionBackend media;
	QSignalSpy playSpy(&media, &MediaSessionBackend::playRequested);
	QSignalSpy pauseSpy(&media, &MediaSessionBackend::pauseRequested);
	QSignalSpy seekSpy(&media, &MediaSessionBackend::seekRequested);
	QVERIFY(!media.open(QUrl(QStringLiteral("http://example.com/video")), QStringLiteral("direct"),
						QStringLiteral("room:1")));
	QCOMPARE(media.state(), QStringLiteral("error"));
	QVERIFY(media.open(QUrl(QStringLiteral("https://example.com/video")), QStringLiteral("direct"),
					   QStringLiteral("room:1")));
	QVERIFY(media.active());
	media.play();
	QCOMPARE(playSpy.count(), 1);
	media.reportPlaybackState(12.5, 90.0, false);
	QCOMPARE(media.state(), QStringLiteral("playing"));
	QCOMPARE(media.position(), 12.5);
	media.applyRemoteState(QUrl(QStringLiteral("https://example.com/next")), QStringLiteral("direct"),
						   QStringLiteral("room:2"), 24.0, true, 42);
	QCOMPARE(media.sessionId(), QStringLiteral("room:2"));
	QCOMPARE(media.position(), 24.0);
	QCOMPARE(media.state(), QStringLiteral("paused"));
	QCOMPARE(seekSpy.count(), 1);
	QCOMPARE(pauseSpy.count(), 1);
	media.applyRemoteState(QUrl(QStringLiteral("https://example.com/stale")), QStringLiteral("direct"),
						   QStringLiteral("stale"), 1.0, false, 41);
	QCOMPARE(media.sessionId(), QStringLiteral("room:2"));
	media.close();
	QVERIFY(!media.active());
	QCOMPARE(media.state(), QStringLiteral("idle"));
}

QTEST_GUILESS_MAIN(TestQmlClientModels)
#include "TestQmlClientModels.moc"
