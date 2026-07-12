// Copyright The Mumble Developers. All rights reserved.

#include "QmlClientModels.h"

#include <QtTest/QSignalSpy>
#include <QtTest/QtTest>

class TestQmlClientModels : public QObject {
	Q_OBJECT

private slots:
	void stableRowsUpdateWithoutReset();
	void synchronizeRowsUsesIncrementalSignals();
	void activeScopeAppliesTypedState();
	void sessionPropertiesOnlyNotifyOnChanges();
	void commandsRejectEmptyStableIds();
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
	commands.selectScope(QStringLiteral("   "));
	commands.invokeAction(QString());
	QCOMPARE(scopeSpy.count(), 0);
	QCOMPARE(actionSpy.count(), 0);
	commands.selectScope(QStringLiteral(" channel:42 "));
	commands.invokeAction(QStringLiteral(" qaAudioMute "));
	QCOMPARE(scopeSpy.takeFirst().at(0).toString(), QStringLiteral("channel:42"));
	QCOMPARE(actionSpy.takeFirst().at(0).toString(), QStringLiteral("qaAudioMute"));
}

QTEST_GUILESS_MAIN(TestQmlClientModels)
#include "TestQmlClientModels.moc"
