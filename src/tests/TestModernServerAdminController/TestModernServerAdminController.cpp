// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "HostAddress.h"
#include "ModernServerAdminController.h"
#include "QtUtils.h"

#include <QtTest/QtTest>

namespace {
	MumbleProto::UserList userList(const int count) {
		MumbleProto::UserList message;
		for (int index = 0; index < count; ++index) {
			auto *user = message.add_users();
			user->set_user_id(static_cast< quint32 >(1000 + index));
			user->set_name(u8(QStringLiteral("User %1").arg(index, 3, 10, QLatin1Char('0'))));
			user->set_last_seen("2026-07-01T12:00:00Z");
			user->set_last_channel(static_cast< quint32 >(10 + (index % 4)));
		}
		return message;
	}

	MumbleProto::BanList banList(const int count) {
		MumbleProto::BanList message;
		for (int index = 0; index < count; ++index) {
			auto *ban = message.add_bans();
			const QHostAddress address(QStringLiteral("10.20.%1.%2").arg(index / 250).arg((index % 250) + 1));
			const HostAddress host(address);
			ban->set_address(host.toStdString());
			ban->set_mask(128);
			ban->set_name(u8(QStringLiteral("Banned %1").arg(index, 3, 10, QLatin1Char('0'))));
			ban->set_hash(u8(QStringLiteral("hash-%1").arg(index)));
			ban->set_reason(u8(QStringLiteral("Reason %1").arg(index)));
			ban->set_start("2026-07-01T12:00:00Z");
			ban->set_duration(index % 2 == 0 ? 0 : 3600);
		}
		return message;
	}

	QString firstStableId(QAbstractItemModel *model) {
		return model->data(model->index(0, 0), ModernRegisteredUserListModel::StableIdRole).toString();
	}
} // namespace

class TestModernServerAdminController : public QObject {
	Q_OBJECT

private slots:
	void registeredUsersLoadingSearchPaginationAndStableSelection();
	void registeredUsersRenameAndUnregisterRollback();
	void registeredUsersPermissionAndValidation();
	void bansLoadingSearchPaginationAndStableSelection();
	void bansValidateAddEditRemoveAndRollback();
	void bansPermissionAndEmptyState();
	void visualFixtureOverrideRejectsLiveAdminMutations();
};

void TestModernServerAdminController::registeredUsersLoadingSearchPaginationAndStableSelection() {
	ModernRegisteredUsersController controller;
	QSignalSpy refreshSpy(&controller, &ModernRegisteredUsersController::refreshRequested);
	QVERIFY(controller.refresh());
	QCOMPARE(controller.state(), QStringLiteral("loading"));
	QCOMPARE(refreshSpy.size(), 1);
	const qulonglong generation = refreshSpy.constFirst().constFirst().toULongLong();
	QVERIFY(!controller.refresh());

	QHash< quint32, QString > rooms { { 10, QStringLiteral("Lobby") }, { 11, QStringLiteral("Games") } };
	controller.applySnapshot(userList(235), rooms, generation);
	QCOMPARE(controller.state(), QStringLiteral("ready"));
	auto *model = controller.typedModel();
	QCOMPARE(model->totalCount(), 235);
	QCOMPARE(model->pageSize(), 50);
	QCOMPARE(model->pageCount(), 5);
	QCOMPARE(model->rowCount(), 50);

	model->setPageSize(1000);
	QCOMPARE(model->pageSize(), 100);
	QCOMPARE(model->rowCount(), 100);
	const QString selected = model->data(model->index(72, 0), ModernRegisteredUserListModel::StableIdRole).toString();
	model->setSelectedStableId(selected);
	model->setFilter(QStringLiteral("User 072"));
	QCOMPARE(model->selectedStableId(), selected);
	QCOMPARE(model->filteredCount(), 1);
	QCOMPARE(model->rowCount(), 1);
	QCOMPARE(model->data(model->index(0, 0), ModernRegisteredUserListModel::LastChannelLabelRole).toString(),
			 QStringLiteral("Lobby"));

	MumbleProto::UserList reordered = userList(235);
	std::reverse(reordered.mutable_users()->begin(), reordered.mutable_users()->end());
	controller.applySnapshot(reordered, rooms);
	QCOMPARE(model->selectedStableId(), selected);

	controller.refresh();
	const qulonglong staleGeneration = controller.loadGeneration() - 1;
	controller.applyLoadError(QStringLiteral("stale"), staleGeneration);
	QCOMPARE(controller.state(), QStringLiteral("refreshing"));
	controller.applyLoadError(QStringLiteral("offline"), controller.loadGeneration());
	QCOMPARE(controller.state(), QStringLiteral("error"));
	QCOMPARE(controller.errorMessage(), QStringLiteral("offline"));
}

void TestModernServerAdminController::registeredUsersRenameAndUnregisterRollback() {
	ModernRegisteredUsersController controller;
	controller.setCanManage(true);
	controller.applySnapshot(userList(3));
	auto *model = controller.typedModel();
	const QString stableId = firstStableId(model);
	model->setSelectedStableId(stableId);

	qulonglong emittedOperation = 0;
	MumbleProto::UserList emittedUpdate;
	connect(&controller, &ModernRegisteredUsersController::updateRequested, this,
		[&](const qulonglong operationId, const MumbleProto::UserList &update) {
			emittedOperation = operationId;
			emittedUpdate    = update;
		});
	QVERIFY(controller.beginRename(stableId, QStringLiteral("Renamed")));
	QCOMPARE(controller.pendingConfirmation().value(QStringLiteral("kind")).toString(),
			 QStringLiteral("renameUser"));
	QVERIFY(controller.confirmPending());
	QVERIFY(controller.busy());
	QCOMPARE(emittedUpdate.users_size(), 1);
	QCOMPARE(u8(emittedUpdate.users(0).name()), QStringLiteral("Renamed"));
	QCOMPARE(model->item(stableId).value(QStringLiteral("name")).toString(), QStringLiteral("Renamed"));
	QVERIFY(model->item(stableId).value(QStringLiteral("pending")).toBool());

	controller.completeOperation(emittedOperation, false, QStringLiteral("Permission denied"));
	QVERIFY(!controller.busy());
	QCOMPARE(model->item(stableId).value(QStringLiteral("name")).toString(), QStringLiteral("User 000"));
	QCOMPARE(controller.operationError(), QStringLiteral("Permission denied"));
	QCOMPARE(model->selectedStableId(), stableId);
	controller.applySnapshot(userList(3));
	QCOMPARE(controller.operationError(), QStringLiteral("Permission denied"));

	QVERIFY(controller.beginUnregister(stableId));
	QCOMPARE(controller.pendingConfirmation().value(QStringLiteral("tone")).toString(), QStringLiteral("danger"));
	controller.cancelPending();
	QVERIFY(controller.pendingConfirmation().isEmpty());
	QCOMPARE(model->totalCount(), 3);
	QVERIFY(controller.beginUnregister(stableId));
	QVERIFY(controller.confirmPending());
	QCOMPARE(model->totalCount(), 2);
	QVERIFY(!emittedUpdate.users(0).has_name());
	MumbleProto::UserList authoritativeUsers = userList(3);
	authoritativeUsers.mutable_users()->DeleteSubrange(0, 1);
	controller.applySnapshot(authoritativeUsers);
	QVERIFY(!controller.busy());
	QCOMPARE(model->totalCount(), 2);
}

void TestModernServerAdminController::registeredUsersPermissionAndValidation() {
	ModernRegisteredUsersController controller;
	controller.applySnapshot(userList(2));
	auto *model = controller.typedModel();
	const QString stableId = firstStableId(model);
	QVERIFY(!controller.beginRename(stableId, QStringLiteral("Other")));
	QVERIFY(controller.operationError().contains(QStringLiteral("permission"), Qt::CaseInsensitive));
	controller.setCanManage(true);
	QVERIFY(!controller.beginRename(stableId, QString()));
	QVERIFY(!controller.beginRename(stableId, QStringLiteral("User 001")));
	QVERIFY(!controller.beginRename(QStringLiteral("user:999999"), QStringLiteral("Other")));
}

void TestModernServerAdminController::bansLoadingSearchPaginationAndStableSelection() {
	ModernBanListController controller;
	QSignalSpy refreshSpy(&controller, &ModernBanListController::refreshRequested);
	QVERIFY(controller.refresh());
	const qulonglong generation = refreshSpy.constFirst().constFirst().toULongLong();
	controller.applySnapshot(banList(123), generation);
	QCOMPARE(controller.state(), QStringLiteral("ready"));
	auto *model = controller.typedModel();
	QCOMPARE(model->totalCount(), 123);
	QCOMPARE(model->rowCount(), 50);
	QCOMPARE(model->pageCount(), 3);
	const QString selected = model->data(model->index(12, 0), ModernBanListModel::StableIdRole).toString();
	model->setSelectedStableId(selected);
	const QString selectedName = model->item(selected).value(QStringLiteral("userName")).toString();
	model->setFilter(selectedName);
	QCOMPARE(model->selectedStableId(), selected);
	QCOMPARE(model->filteredCount(), 1);

	MumbleProto::BanList reordered = banList(123);
	std::reverse(reordered.mutable_bans()->begin(), reordered.mutable_bans()->end());
	controller.applySnapshot(reordered);
	QCOMPARE(model->selectedStableId(), selected);
	QCOMPARE(model->item(selected).value(QStringLiteral("mask")).toInt(), 32);
}

void TestModernServerAdminController::bansValidateAddEditRemoveAndRollback() {
	ModernBanListController controller;
	controller.setCanManage(true);
	controller.applySnapshot(banList(2));
	auto *model = controller.typedModel();

	QVariantMap invalid { { QStringLiteral("address"), QStringLiteral("not-an-ip") },
		{ QStringLiteral("permanent"), true } };
	QVariantMap validation = controller.validateDraft(invalid);
	QVERIFY(!validation.value(QStringLiteral("valid")).toBool());
	QCOMPARE(validation.value(QStringLiteral("field")).toString(), QStringLiteral("address"));

	QVariantMap draft { { QStringLiteral("address"), QStringLiteral("192.168.40.20") },
		{ QStringLiteral("mask"), 24 }, { QStringLiteral("userName"), QStringLiteral("Trouble") },
		{ QStringLiteral("hash"), QStringLiteral("new-hash") },
		{ QStringLiteral("reason"), QStringLiteral("Abuse") },
		{ QStringLiteral("startUtc"), QStringLiteral("2026-07-17T08:00:00Z") },
		{ QStringLiteral("durationSeconds"), 7200 } };
	validation = controller.validateDraft(draft);
	QVERIFY(validation.value(QStringLiteral("valid")).toBool());

	qulonglong emittedOperation = 0;
	MumbleProto::BanList emittedUpdate;
	connect(&controller, &ModernBanListController::updateRequested, this,
		[&](const qulonglong operationId, const MumbleProto::BanList &update) {
			emittedOperation = operationId;
			emittedUpdate    = update;
		});
	QVERIFY(controller.beginAdd(draft));
	QVERIFY(controller.confirmPending());
	QCOMPARE(model->totalCount(), 3);
	QVERIFY(controller.busy());
	QCOMPARE(emittedUpdate.bans_size(), 3);
	QVERIFY(!emittedUpdate.query());
	bool found = false;
	for (const auto &ban : emittedUpdate.bans()) {
		if (u8(ban.hash()) == QLatin1String("new-hash")) {
			found = true;
			QCOMPARE(ban.mask(), 120U);
			QCOMPARE(ban.duration(), 7200U);
		}
	}
	QVERIFY(found);
	controller.completeOperation(emittedOperation, false, QStringLiteral("Disconnected"));
	QCOMPARE(model->totalCount(), 2);
	QCOMPARE(controller.operationError(), QStringLiteral("Disconnected"));
	controller.applySnapshot(banList(2));
	QCOMPARE(controller.operationError(), QStringLiteral("Disconnected"));

	const QString selected = model->data(model->index(0, 0), ModernBanListModel::StableIdRole).toString();
	QVariantMap edit = model->item(selected);
	edit.insert(QStringLiteral("reason"), QStringLiteral("Updated reason"));
	QVERIFY(controller.beginEdit(selected, edit));
	QVERIFY(controller.confirmPending());
	QCOMPARE(model->item(selected).value(QStringLiteral("reason")).toString(), QStringLiteral("Updated reason"));
	controller.applySnapshot(emittedUpdate);
	QVERIFY(!controller.busy());
	QVERIFY(!model->item(selected).value(QStringLiteral("pending")).toBool());

	QVERIFY(controller.beginRemove(selected));
	QCOMPARE(controller.pendingConfirmation().value(QStringLiteral("kind")).toString(), QStringLiteral("removeBan"));
	QVERIFY(controller.confirmPending());
	QCOMPARE(model->totalCount(), 1);
	controller.applySnapshot(emittedUpdate);
	QVERIFY(!controller.busy());
	QCOMPARE(model->totalCount(), 1);
}

void TestModernServerAdminController::bansPermissionAndEmptyState() {
	ModernBanListController controller;
	controller.applySnapshot(MumbleProto::BanList());
	QCOMPARE(controller.state(), QStringLiteral("ready"));
	QCOMPARE(controller.typedModel()->totalCount(), 0);
	QVariantMap hashOnly { { QStringLiteral("hash"), QStringLiteral("certificate") },
		{ QStringLiteral("permanent"), true } };
	QVERIFY(controller.validateDraft(hashOnly).value(QStringLiteral("valid")).toBool());
	QVERIFY(!controller.beginAdd(hashOnly));
	controller.setCanManage(true);
	QVERIFY(controller.beginAdd(hashOnly));
	controller.cancelPending();
	QVERIFY(controller.pendingConfirmation().isEmpty());
	controller.reset();
	QCOMPARE(controller.state(), QStringLiteral("idle"));
}

void TestModernServerAdminController::visualFixtureOverrideRejectsLiveAdminMutations() {
	ModernRegisteredUsersController users;
	users.setCanManage(true);
	users.applySnapshot(userList(2));
	users.setProperty("_mumbleVisualFixtureOverride", true);
	users.setCanManage(false);
	users.applySnapshot(MumbleProto::UserList());
	users.applyLoadError(QStringLiteral("late live error"));
	users.reset();
	QVERIFY(users.canManage());
	QCOMPARE(users.state(), QStringLiteral("ready"));
	QCOMPARE(users.typedModel()->totalCount(), 2);

	ModernBanListController bans;
	bans.setCanManage(true);
	bans.applySnapshot(banList(2));
	bans.setProperty("_mumbleVisualFixtureOverride", true);
	bans.setCanManage(false);
	bans.applySnapshot(MumbleProto::BanList());
	bans.applyLoadError(QStringLiteral("late live error"));
	bans.reset();
	QVERIFY(bans.canManage());
	QCOMPARE(bans.state(), QStringLiteral("ready"));
	QCOMPARE(bans.typedModel()->totalCount(), 2);

	users.setProperty("_mumbleVisualFixtureWrite", true);
	users.setCanManage(false);
	users.reset();
	QVERIFY(!users.canManage());
	QCOMPARE(users.state(), QStringLiteral("idle"));
	QCOMPARE(users.typedModel()->totalCount(), 0);

	bans.setProperty("_mumbleVisualFixtureWrite", true);
	bans.setCanManage(false);
	bans.reset();
	QVERIFY(!bans.canManage());
	QCOMPARE(bans.state(), QStringLiteral("idle"));
	QCOMPARE(bans.typedModel()->totalCount(), 0);
}

QTEST_MAIN(TestModernServerAdminController)
#include "TestModernServerAdminController.moc"
