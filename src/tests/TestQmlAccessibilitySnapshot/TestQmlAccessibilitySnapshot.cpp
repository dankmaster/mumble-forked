#include "QmlAccessibilitySnapshot.h"

#include <QtGui/QAccessible>
#include <QtGui/QWindow>
#include <QtTest/QtTest>

#if QT_CONFIG(accessibility)
class TestAccessible final : public QAccessibleInterface {
public:
	explicit TestAccessible(QString name, QRect rect = {}, QAccessible::Role role = QAccessible::StaticText)
		: m_name(std::move(name)), m_rect(rect), m_role(role) {
	}

	bool isValid() const override { return m_valid; }
	QObject *object() const override { return nullptr; }
	QAccessibleInterface *childAt(int, int) const override { return nullptr; }
	QAccessibleInterface *parent() const override { return m_parent; }
	QAccessibleInterface *child(int index) const override {
		return index >= 0 && index < m_children.size() ? m_children.at(index) : nullptr;
	}
	int childCount() const override { return m_children.size(); }
	int indexOfChild(const QAccessibleInterface *child) const override { return m_children.indexOf(child); }
	QString text(QAccessible::Text type) const override {
		return type == QAccessible::Name ? m_name : type == QAccessible::Description ? m_description : QString();
	}
	void setText(QAccessible::Text type, const QString &text) override {
		if (type == QAccessible::Name) m_name = text;
		if (type == QAccessible::Description) m_description = text;
	}
	QRect rect() const override { return m_rect; }
	QAccessible::Role role() const override { return m_role; }
	QAccessible::State state() const override { return m_state; }

	void addChild(TestAccessible *child) {
		m_children.push_back(child);
		child->m_parent = this;
	}
	void addCycle(QAccessibleInterface *child) { m_children.push_back(child); }
	QAccessible::State &mutableState() { return m_state; }

private:
	QString m_name;
	QString m_description;
	QRect m_rect;
	QAccessible::Role m_role;
	QAccessible::State m_state;
	QList< QAccessibleInterface * > m_children;
	QAccessibleInterface *m_parent = nullptr;
	bool m_valid = true;
};
#endif

class TestQmlAccessibilitySnapshot : public QObject {
	Q_OBJECT

private slots:
	void rejectsMissingRoot();
	void serializesDeterministicallyAndBoundsStrings();
	void normalizesScreenCoordinatesToRootWindow();
	void guardsCyclesAndBudgets();
	void queriesWindowAccessibilityWhenAvailable();
};

void TestQmlAccessibilitySnapshot::rejectsMissingRoot() {
	const QVariantMap result = QmlAccessibilitySnapshot::serialize(static_cast< QAccessibleInterface * >(nullptr));
	QVERIFY(!result.value(QStringLiteral("ok")).toBool());
	QVERIFY(!result.value(QStringLiteral("error")).toString().isEmpty());
}

void TestQmlAccessibilitySnapshot::serializesDeterministicallyAndBoundsStrings() {
#if QT_CONFIG(accessibility)
	TestAccessible root(QStringLiteral("root"), QRect(0, 0, 400, 300), QAccessible::Window);
	TestAccessible lower(QStringLiteral("lower"), QRect(10, 50, 100, 20));
	TestAccessible upper(QStringLiteral("name-longer-than-limit"), QRect(10, 10, 100, 20));
	upper.mutableState().focusable = true;
	upper.mutableState().focused = true;
	root.addChild(&lower);
	root.addChild(&upper);

	QmlAccessibilitySnapshotLimits limits;
	limits.maximumStringLength = 8;
	const QVariantMap first = QmlAccessibilitySnapshot::serialize(&root, limits);
	const QVariantMap second = QmlAccessibilitySnapshot::serialize(&root, limits);
	QVERIFY(first.value(QStringLiteral("ok")).toBool());
	QCOMPARE(first, second);
	QVERIFY(first.value(QStringLiteral("truncated")).toBool());
	const QVariantList children = first.value(QStringLiteral("tree")).toMap().value(QStringLiteral("children")).toList();
	QCOMPARE(children.size(), 2);
	QCOMPARE(children.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("name-lon"));
	QCOMPARE(children.at(1).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("lower"));
	const QVariantList states = children.at(0).toMap().value(QStringLiteral("states")).toList();
	QVERIFY(states.contains(QStringLiteral("focusable")));
	QVERIFY(states.contains(QStringLiteral("focused")));
#else
	QSKIP("Qt was built without accessibility support.");
#endif
}

void TestQmlAccessibilitySnapshot::normalizesScreenCoordinatesToRootWindow() {
#if QT_CONFIG(accessibility)
	TestAccessible firstRoot(QStringLiteral("root"), QRect(100, 200, 400, 300), QAccessible::Window);
	TestAccessible firstLower(QStringLiteral("lower"), QRect(110, 250, 100, 20));
	TestAccessible firstUpper(QStringLiteral("upper"), QRect(110, 210, 100, 20));
	firstRoot.addChild(&firstLower);
	firstRoot.addChild(&firstUpper);

	TestAccessible movedRoot(QStringLiteral("root"), QRect(1700, -400, 400, 300), QAccessible::Window);
	TestAccessible movedLower(QStringLiteral("lower"), QRect(1710, -350, 100, 20));
	TestAccessible movedUpper(QStringLiteral("upper"), QRect(1710, -390, 100, 20));
	movedRoot.addChild(&movedLower);
	movedRoot.addChild(&movedUpper);

	const QVariantMap first = QmlAccessibilitySnapshot::serialize(&firstRoot);
	const QVariantMap moved = QmlAccessibilitySnapshot::serialize(&movedRoot);
	QCOMPARE(first, moved);
	const QVariantMap rootRect = first.value(QStringLiteral("tree")).toMap().value(QStringLiteral("rect")).toMap();
	QCOMPARE(rootRect.value(QStringLiteral("x")).toInt(), 0);
	QCOMPARE(rootRect.value(QStringLiteral("y")).toInt(), 0);
	const QVariantList children = first.value(QStringLiteral("tree")).toMap().value(QStringLiteral("children")).toList();
	QCOMPARE(children.at(0).toMap().value(QStringLiteral("name")).toString(), QStringLiteral("upper"));
#else
	QSKIP("Qt was built without accessibility support.");
#endif
}

void TestQmlAccessibilitySnapshot::guardsCyclesAndBudgets() {
#if QT_CONFIG(accessibility)
	TestAccessible root(QStringLiteral("root"));
	TestAccessible child(QStringLiteral("child"));
	TestAccessible extra(QStringLiteral("extra"));
	root.addChild(&child);
	root.addChild(&extra);
	child.addCycle(&root);
	QmlAccessibilitySnapshotLimits limits;
	limits.maximumNodes = 4;
	const QVariantMap result = QmlAccessibilitySnapshot::serialize(&root, limits);
	QVERIFY(result.value(QStringLiteral("ok")).toBool());
	QVERIFY(result.value(QStringLiteral("truncated")).toBool());
	const QVariantList rootChildren = result.value(QStringLiteral("tree")).toMap().value(QStringLiteral("children")).toList();
	QCOMPARE(rootChildren.size(), 2);
	const QVariantList childChildren = rootChildren.at(0).toMap().value(QStringLiteral("children")).toList();
	QCOMPARE(childChildren.size(), 1);
	QVERIFY(childChildren.at(0).toMap().value(QStringLiteral("cycle")).toBool());

	limits.maximumNodes = 2;
	const QVariantMap nodeLimited = QmlAccessibilitySnapshot::serialize(&root, limits);
	QVERIFY(nodeLimited.value(QStringLiteral("truncated")).toBool());
	QCOMPARE(nodeLimited.value(QStringLiteral("nodeCount")).toInt(), 2);

	limits.maximumNodes = 4;
	limits.maximumDepth = 0;
	const QVariantMap depthLimited = QmlAccessibilitySnapshot::serialize(&root, limits);
	QVERIFY(depthLimited.value(QStringLiteral("truncated")).toBool());
	QCOMPARE(depthLimited.value(QStringLiteral("nodeCount")).toInt(), 1);
#else
	QSKIP("Qt was built without accessibility support.");
#endif
}

void TestQmlAccessibilitySnapshot::queriesWindowAccessibilityWhenAvailable() {
	QWindow window;
	window.setTitle(QStringLiteral("Accessibility snapshot test"));
	const QVariantMap result = QmlAccessibilitySnapshot::serialize(&window);
#if QT_CONFIG(accessibility)
	if (!result.value(QStringLiteral("ok")).toBool()) {
		QSKIP(qPrintable(result.value(QStringLiteral("error")).toString()));
	}
	QVERIFY(!result.value(QStringLiteral("tree")).toMap().value(QStringLiteral("role")).toString().isEmpty());
#else
	QVERIFY(!result.value(QStringLiteral("ok")).toBool());
#endif
}

QTEST_MAIN(TestQmlAccessibilitySnapshot)
#include "TestQmlAccessibilitySnapshot.moc"
