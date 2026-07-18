// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include "ClientActionRegistry.h"
#include "ModernShellMenuSerializer.h"

#include <QtGui/QAction>

class TestModernShellMenuSerializer : public QObject {
	Q_OBJECT

private slots:
	void serializesActionsAndDynamicContextActionWithoutWidgets();
	void omitsUnavailableContextActionsAndRegistryEntries();
	void serializesCompleteSelfActionGroupsAndNormalizesUnavailableEntries();
	void appendsDynamicServerActionsWithoutDroppingFixedEntries();
	void buildsTypedLabelAndSliderItems();
	void mapsStableActionIconsAndCompactsGroupBoundaries();
};

void TestModernShellMenuSerializer::serializesActionsAndDynamicContextActionWithoutWidgets() {
	QObject owner;
	QAction regularAction(QObject::tr("Plain Action"), &owner);
	regularAction.setShortcut(QKeySequence(QStringLiteral("Ctrl+K")));
	QAction checkableAction(QObject::tr("Checkable Action"), &owner);
	checkableAction.setCheckable(true);
	QAction separator(&owner);
	separator.setSeparator(true);
	QAction dynamicAction(QObject::tr("Dynamic Action"), &owner);
	dynamicAction.setData(QStringLiteral("dynamic-token"));

	ModernShellMenuSerializer::ActionRegistry registry;
	const QVariantList items = ModernShellMenuSerializer::serializeActions(
		{ &regularAction, &checkableAction, &separator, &dynamicAction },
		[&regularAction, &checkableAction, &dynamicAction](const QAction *action) {
			ModernShellMenuSerializer::ActionDefinition definition;
			if (action == &regularAction) {
				definition.id = QStringLiteral("plainAction");
				definition.icon = QStringLiteral("settings");
				definition.secondary = QStringLiteral("Current profile");
			} else if (action == &checkableAction) {
				definition.id = QStringLiteral("checkableAction");
			} else if (action == &dynamicAction) {
				definition.id = ModernShellMenuSerializer::contextActionId(QStringLiteral("channel"), action->data());
				definition.contextActionData = action->data().toString();
			}
			return definition;
		},
		&registry);

	QCOMPARE(items.size(), 4);
	QCOMPARE(items.at(0).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("action"));
	QCOMPARE(items.at(0).toMap().value(QStringLiteral("icon")).toString(), QStringLiteral("settings"));
	QCOMPARE(items.at(0).toMap().value(QStringLiteral("secondary")).toString(), QStringLiteral("Current profile"));
	QCOMPARE(items.at(0).toMap().value(QStringLiteral("shortcutPortableText")).toString(),
			 QStringLiteral("Ctrl+K"));
	QVERIFY(!items.at(0).toMap().value(QStringLiteral("checkable")).toBool());
	QVERIFY(items.at(1).toMap().value(QStringLiteral("checkable")).toBool());
	QVERIFY(!items.at(1).toMap().value(QStringLiteral("checked")).toBool());
	QCOMPARE(items.at(2).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("separator"));
	QCOMPARE(items.at(3).toMap().value(QStringLiteral("id")).toString(),
			 QStringLiteral("context:channel:dynamic-token"));
	QCOMPARE(items.at(3).toMap().value(QStringLiteral("icon")).toString(), QStringLiteral("plugin"));

	const auto dynamicRegistryEntry = registry.value(QStringLiteral("context:channel:dynamic-token"));
	QVERIFY(dynamicRegistryEntry.action == &dynamicAction);
	QCOMPARE(dynamicRegistryEntry.contextActionData, QStringLiteral("dynamic-token"));
}

void TestModernShellMenuSerializer::omitsUnavailableContextActionsAndRegistryEntries() {
	QObject owner;
	QAction allowedAction(QObject::tr("Allowed Action"), &owner);
	QAction deniedAction(QObject::tr("Denied Action"), &owner);
	QAction pendingPermissionAction(QObject::tr("Pending Permission Action"), &owner);
	QAction explanatoryAction(QObject::tr("Temporarily Unavailable"), &owner);
	deniedAction.setEnabled(false);
	explanatoryAction.setEnabled(false);

	ModernShellMenuSerializer::ActionRegistry registry;
	const QVariantList items = ModernShellMenuSerializer::serializeActions(
		{ &allowedAction, &deniedAction, &pendingPermissionAction, &explanatoryAction },
		[&allowedAction, &deniedAction, &pendingPermissionAction](const QAction *action) {
			ModernShellMenuSerializer::ActionDefinition definition;
			if (action == &allowedAction) {
				definition.id = QStringLiteral("allowed");
				definition.omitWhenDisabled = true;
			} else if (action == &deniedAction) {
				definition.id = QStringLiteral("denied");
				definition.omitWhenDisabled = true;
			} else if (action == &pendingPermissionAction) {
				definition.id = QStringLiteral("pendingPermission");
				definition.available = false;
			} else {
				definition.id = QStringLiteral("explanatory");
			}
			return definition;
		},
		&registry);

	QCOMPARE(items.size(), 2);
	QCOMPARE(items.at(0).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("allowed"));
	QCOMPARE(items.at(1).toMap().value(QStringLiteral("id")).toString(), QStringLiteral("explanatory"));
	QVERIFY(items.at(1).toMap().value(QStringLiteral("enabled")).toBool() == false);
	QVERIFY(registry.contains(QStringLiteral("allowed")));
	QVERIFY(!registry.contains(QStringLiteral("denied")));
	QVERIFY(!registry.contains(QStringLiteral("pendingPermission")));
	QVERIFY(registry.contains(QStringLiteral("explanatory")));
}

void TestModernShellMenuSerializer::serializesCompleteSelfActionGroupsAndNormalizesUnavailableEntries() {
	QObject owner;
	QAction mute(QObject::tr("Mute Self"), &owner);
	QAction deafen(QObject::tr("Deafen Self"), &owner);
	QAction prioritySpeaker(QObject::tr("Priority Speaker"), &owner);
	QAction recording(QObject::tr("Record..."), &owner);
	QAction comment(QObject::tr("Change Comment"), &owner);
	QAction avatarChange(QObject::tr("Change Avatar"), &owner);
	QAction avatarRemove(QObject::tr("Remove Avatar"), &owner);
	QAction registerAction(QObject::tr("Register"), &owner);
	QAction audioStats(QObject::tr("Audio Statistics"), &owner);
	mute.setCheckable(true);
	mute.setChecked(true);

	ClientActionList selfActions;
	selfActions.addAction(&mute);
	selfActions.addAction(&deafen);
	selfActions.addAction(&prioritySpeaker);
	selfActions.addSeparator();
	selfActions.addAction(&recording);
	selfActions.addSeparator();
	selfActions.addAction(&comment);
	selfActions.addAction(&avatarChange);
	selfActions.addAction(&avatarRemove);
	selfActions.addSeparator();
	selfActions.addAction(&registerAction);
	selfActions.addAction(&audioStats);

	const auto resolveSelfAction = [&](const QAction *action) {
		ModernShellMenuSerializer::ActionDefinition definition;
		definition.omitWhenDisabled = true;
		if (action == &mute) definition.id = QStringLiteral("self.toggleMute");
		else if (action == &deafen) definition.id = QStringLiteral("self.toggleDeaf");
		else if (action == &prioritySpeaker) definition.id = QStringLiteral("self.prioritySpeaker");
		else if (action == &recording) definition.id = QStringLiteral("self.recording");
		else if (action == &comment) definition.id = QStringLiteral("self.comment");
		else if (action == &avatarChange) definition.id = QStringLiteral("self.avatarChange");
		else if (action == &avatarRemove) definition.id = QStringLiteral("self.avatarRemove");
		else if (action == &registerAction) definition.id = QStringLiteral("self.register");
		else if (action == &audioStats) definition.id = QStringLiteral("self.audioStats");
		return definition;
	};
	const auto actionIds = [](const QVariantList &items) {
		QStringList ids;
		for (const QVariant &item : items) {
			const QVariantMap map = item.toMap();
			if (map.value(QStringLiteral("kind")).toString() == QLatin1String("action")) {
				ids.push_back(map.value(QStringLiteral("id")).toString());
			}
		}
		return ids;
	};

	QVariantList items = ModernShellMenuSerializer::serializeActions(selfActions.actions(), resolveSelfAction);
	QCOMPARE(actionIds(items),
		QStringList({ QStringLiteral("self.toggleMute"), QStringLiteral("self.toggleDeaf"),
			QStringLiteral("self.prioritySpeaker"), QStringLiteral("self.recording"),
			QStringLiteral("self.comment"), QStringLiteral("self.avatarChange"),
			QStringLiteral("self.avatarRemove"), QStringLiteral("self.register"),
			QStringLiteral("self.audioStats") }));
	QVERIFY(items.first().toMap().value(QStringLiteral("checked")).toBool());
	int separatorCount = 0;
	for (const QVariant &item : items) {
		if (item.toMap().value(QStringLiteral("kind")).toString() == QLatin1String("separator")) {
			++separatorCount;
		}
	}
	QCOMPARE(separatorCount, 3);

	prioritySpeaker.setEnabled(false);
	avatarRemove.setEnabled(false);
	registerAction.setEnabled(false);
	items = ModernShellMenuSerializer::serializeActions(selfActions.actions(), resolveSelfAction);
	const QStringList availableIds = actionIds(items);
	QVERIFY(availableIds.contains(QStringLiteral("self.recording")));
	QVERIFY(availableIds.contains(QStringLiteral("self.audioStats")));
	QVERIFY(!availableIds.contains(QStringLiteral("self.prioritySpeaker")));
	QVERIFY(!availableIds.contains(QStringLiteral("self.avatarRemove")));
	QVERIFY(!availableIds.contains(QStringLiteral("self.register")));
	QVERIFY(items.first().toMap().value(QStringLiteral("kind")).toString() != QLatin1String("separator"));
	QVERIFY(items.last().toMap().value(QStringLiteral("kind")).toString() != QLatin1String("separator"));
	for (qsizetype index = 1; index < items.size(); ++index) {
		const bool currentSeparator =
			items.at(index).toMap().value(QStringLiteral("kind")).toString() == QLatin1String("separator");
		const bool previousSeparator =
			items.at(index - 1).toMap().value(QStringLiteral("kind")).toString() == QLatin1String("separator");
		QVERIFY(!currentSeparator || !previousSeparator);
	}
}

void TestModernShellMenuSerializer::appendsDynamicServerActionsWithoutDroppingFixedEntries() {
	QAction dynamicAction(QObject::tr("Server extension"));
	dynamicAction.setData(QStringLiteral("extension-token"));
	ModernShellMenuSerializer::ActionRegistry registry;
	const QVariantList dynamicItems = ModernShellMenuSerializer::serializeActions(
		{ &dynamicAction }, [](const QAction *action) {
			ModernShellMenuSerializer::ActionDefinition definition;
			definition.id = ModernShellMenuSerializer::contextActionId(QStringLiteral("server"), action->data());
			definition.contextActionData = action->data().toString();
			definition.omitWhenDisabled = true;
			return definition;
		},
		&registry);

	QVariantList serverItems {
		ModernShellMenuSerializer::actionItem(QStringLiteral("server.favorite"), QStringLiteral("Add to Favorites"),
			true, false),
		ModernShellMenuSerializer::separatorItem(),
		ModernShellMenuSerializer::actionItem(QStringLiteral("server.userList"), QStringLiteral("Registered users..."),
			true, false),
		ModernShellMenuSerializer::actionItem(QStringLiteral("server.acl"), QStringLiteral("Access control (ACL)..."),
			true, false),
		ModernShellMenuSerializer::actionItem(QStringLiteral("server.settings"), QStringLiteral("Server settings..."),
			true, false),
		ModernShellMenuSerializer::separatorItem()
	};
	serverItems.append(dynamicItems);
	serverItems = ModernShellMenuSerializer::normalize(serverItems);

	QStringList ids;
	for (const QVariant &item : serverItems) {
		const QString id = item.toMap().value(QStringLiteral("id")).toString();
		if (!id.isEmpty()) ids.push_back(id);
	}
	QCOMPARE(ids,
		QStringList({ QStringLiteral("server.favorite"), QStringLiteral("server.userList"),
			QStringLiteral("server.acl"), QStringLiteral("server.settings"),
			QStringLiteral("context:server:extension-token") }));
	QVERIFY(registry.value(QStringLiteral("context:server:extension-token")).action == &dynamicAction);
}

void TestModernShellMenuSerializer::buildsTypedLabelAndSliderItems() {
	const QVariantMap label = ModernShellMenuSerializer::labelItem(QStringLiteral("Local volume"),
																   QStringLiteral("Per-user control"));
	QCOMPARE(label.value(QStringLiteral("kind")).toString(), QStringLiteral("label"));
	QCOMPARE(label.value(QStringLiteral("hint")).toString(), QStringLiteral("Per-user control"));

	const QVariantMap slider = ModernShellMenuSerializer::sliderItem(
		QStringLiteral("listenerVolume"), QStringLiteral("Listener volume"), 4, -30, 30, 1,
		QStringLiteral(" dB"), true, true);
	QCOMPARE(slider.value(QStringLiteral("kind")).toString(), QStringLiteral("slider"));
	QCOMPARE(slider.value(QStringLiteral("min")).toInt(), -30);
	QCOMPARE(slider.value(QStringLiteral("max")).toInt(), 30);
	QVERIFY(slider.value(QStringLiteral("finalOnRelease")).toBool());
}

void TestModernShellMenuSerializer::mapsStableActionIconsAndCompactsGroupBoundaries() {
	QCOMPARE(ModernShellMenuSerializer::actionIconId(QStringLiteral("server.disconnect")),
			 QStringLiteral("disconnect"));
	QCOMPARE(ModernShellMenuSerializer::actionIconId(QStringLiteral("qaConfigDialog")),
			 QStringLiteral("settings"));
	QCOMPARE(ModernShellMenuSerializer::actionIconId(QStringLiteral("qaAudioReset")),
			 QStringLiteral("refresh"));
	QCOMPARE(ModernShellMenuSerializer::actionIconId(QStringLiteral("screenShareStart")),
			 QStringLiteral("screen-share"));
	QCOMPARE(ModernShellMenuSerializer::actionIconId(QStringLiteral("friendRemove")),
			 QStringLiteral("user-remove"));
	QCOMPARE(ModernShellMenuSerializer::actionIconId(QStringLiteral("context:user:plugin-action")),
			 QStringLiteral("plugin"));
	QCOMPARE(ModernShellMenuSerializer::actionIconId(QStringLiteral("future.action")),
			 QStringLiteral("action"));

	const QVariantMap disconnect = ModernShellMenuSerializer::actionItem(
		QStringLiteral("server.disconnect"), QStringLiteral("Disconnect"), true, false,
		QStringLiteral("danger"));
	QCOMPARE(disconnect.value(QStringLiteral("icon")).toString(), QStringLiteral("disconnect"));
	QCOMPARE(disconnect.value(QStringLiteral("secondary")).toString(), QString());

	QVariantMap section;
	section.insert(QStringLiteral("kind"), QStringLiteral("section"));
	section.insert(QStringLiteral("label"), QStringLiteral("Administration"));
	const QVariantList normalized = ModernShellMenuSerializer::normalize(
		{ disconnect, ModernShellMenuSerializer::separatorItem(), section,
		  ModernShellMenuSerializer::separatorItem(), disconnect });
	QCOMPARE(normalized.size(), 3);
	QCOMPARE(normalized.at(0).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("action"));
	QCOMPARE(normalized.at(1).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("section"));
	QCOMPARE(normalized.at(2).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("action"));
}

QTEST_MAIN(TestModernShellMenuSerializer)
#include "TestModernShellMenuSerializer.moc"
