#include "QmlWindowStateController.h"

#include <QtTest/QtTest>

class TestQmlWindowStateController : public QObject {
	Q_OBJECT

private slots:
	void clampsOffscreenGeometryToPreferredScreen();
	void preservesGeometryOnSecondaryScreen();
	void roundTripsMaximizedNormalGeometry();
	void rejectsLegacyAndMalformedPayloads();
};

void TestQmlWindowStateController::clampsOffscreenGeometryToPreferredScreen() {
	const QList< QRect > screens { QRect(0, 0, 1920, 1080), QRect(1920, 0, 2560, 1440) };
	const QRect clamped = QmlWindowStateController::clampGeometry(QRect(-5000, -3000, 1280, 820), screens, 1);
	QVERIFY(screens.at(1).contains(clamped));
	QCOMPARE(clamped.size(), QSize(1280, 820));
}

void TestQmlWindowStateController::preservesGeometryOnSecondaryScreen() {
	const QList< QRect > screens { QRect(0, 0, 1920, 1080), QRect(1920, -200, 2560, 1440) };
	const QRect desired(2200, 80, 1400, 900);
	QCOMPARE(QmlWindowStateController::clampGeometry(desired, screens), desired);
}

void TestQmlWindowStateController::roundTripsMaximizedNormalGeometry() {
	QmlWindowState state;
	state.normalGeometry = QRect(2100, 40, 1360, 860);
	state.maximized = true;
	state.screenName = QStringLiteral("Secondary");
	state.devicePixelRatio = 1.5;
	const auto decoded = QmlWindowStateController::decode(QmlWindowStateController::encode(state));
	QVERIFY(decoded.has_value());
	QCOMPARE(decoded->normalGeometry, state.normalGeometry);
	QVERIFY(decoded->maximized);
	QCOMPARE(decoded->screenName, state.screenName);
	QCOMPARE(decoded->devicePixelRatio, state.devicePixelRatio);
}

void TestQmlWindowStateController::rejectsLegacyAndMalformedPayloads() {
	QVERIFY(!QmlWindowStateController::decode(QByteArray::fromHex("01d9d0cb00030000")).has_value());
	QVERIFY(!QmlWindowStateController::decode(QByteArrayLiteral("not-json")).has_value());
	QVERIFY(!QmlWindowStateController::decode(QByteArrayLiteral(
		R"({"format":"mumble-qml-window-state","version":99,"x":0,"y":0,"width":800,"height":600})"))
			 .has_value());
}

QTEST_GUILESS_MAIN(TestQmlWindowStateController)
#include "TestQmlWindowStateController.moc"
