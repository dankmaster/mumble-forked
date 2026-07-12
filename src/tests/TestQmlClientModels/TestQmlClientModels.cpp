// Copyright The Mumble Developers. All rights reserved.

#include "QmlClientModels.h"

#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

class TestQmlClientModels : public QObject {
	Q_OBJECT

private slots:
	void stableRowsUpdateWithoutReset();
	void synchronizeRowsUsesIncrementalSignals();
	void stableIdsRemainIndependentFromSourceMaps();
	void messageRolesExposeStructuredState();
	void duplicateStableIdsAreCoalesced();
	void activeScopeAppliesTypedState();
	void sessionPropertiesOnlyNotifyOnChanges();
	void commandsRejectEmptyStableIds();
	void pttStateIsIdempotentAndReleases();
	void dialogStateRoutesTypedRequests();
	void asyncOperationsExposeProgressAndCancellation();
	void mediaSessionValidatesAndPublishesTypedState();
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
	model.upsertRow({ { QStringLiteral("id"), QStringLiteral("message:7") },
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
}

void TestQmlClientModels::mediaSessionValidatesAndPublishesTypedState() {
	MediaSessionBackend media;
	QSignalSpy playSpy(&media, &MediaSessionBackend::playRequested);
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
	media.close();
	QVERIFY(!media.active());
	QCOMPARE(media.state(), QStringLiteral("idle"));
}

QTEST_GUILESS_MAIN(TestQmlClientModels)
#include "TestQmlClientModels.moc"
