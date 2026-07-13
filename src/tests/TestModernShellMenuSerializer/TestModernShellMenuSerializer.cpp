// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include <QtTest>

#include "ModernShellMenuSerializer.h"

#include <QtGui/QAction>

class TestModernShellMenuSerializer : public QObject {
	Q_OBJECT

private slots:
	void serializesActionsAndDynamicContextActionWithoutWidgets();
	void buildsTypedLabelAndSliderItems();
};

void TestModernShellMenuSerializer::serializesActionsAndDynamicContextActionWithoutWidgets() {
	QObject owner;
	QAction regularAction(QObject::tr("Plain Action"), &owner);
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
	QVERIFY(!items.at(0).toMap().value(QStringLiteral("checkable")).toBool());
	QVERIFY(items.at(1).toMap().value(QStringLiteral("checkable")).toBool());
	QVERIFY(!items.at(1).toMap().value(QStringLiteral("checked")).toBool());
	QCOMPARE(items.at(2).toMap().value(QStringLiteral("kind")).toString(), QStringLiteral("separator"));
	QCOMPARE(items.at(3).toMap().value(QStringLiteral("id")).toString(),
			 QStringLiteral("context:channel:dynamic-token"));

	const auto dynamicRegistryEntry = registry.value(QStringLiteral("context:channel:dynamic-token"));
	QVERIFY(dynamicRegistryEntry.action == &dynamicAction);
	QCOMPARE(dynamicRegistryEntry.contextActionData, QStringLiteral("dynamic-token"));
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

QTEST_MAIN(TestModernShellMenuSerializer)
#include "TestModernShellMenuSerializer.moc"
