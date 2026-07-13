#include "QmlWindowStateController.h"

#include <QtTest/QtTest>

class TestQmlWindowStateController : public QObject {
	Q_OBJECT

private slots:
	void clampsOffscreenGeometryToPreferredScreen();
	void preservesGeometryOnSecondaryScreen();
	void roundTripsMaximizedNormalGeometry();
	void rejectsLegacyAndMalformedPayloads();
	void plansNativeWaylandRestoreWithoutAbsolutePosition();
	void fallsBackAfterMonitorHotplug();
	void keepsLogicalSizeAcrossDpiChanges();
	void recognizesCompositorManagedPlatforms();
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

void TestQmlWindowStateController::plansNativeWaylandRestoreWithoutAbsolutePosition() {
	QmlWindowState state;
	state.normalGeometry = QRect(2250, 120, 1400, 900);
	state.screenName = QStringLiteral("Secondary");
	state.devicePixelRatio = 1.5;
	const QList< QmlScreenSnapshot > screens {
		{ QStringLiteral("Primary"), QRect(0, 0, 1920, 1080), 1.0 },
		{ QStringLiteral("Secondary"), QRect(1920, -200, 2560, 1440), 1.5 }
	};

	const QmlWindowRestorePlan plan =
		QmlWindowStateController::createRestorePlan(state, screens, QStringLiteral("Primary"), true);
	QCOMPARE(plan.targetScreen, 1);
	QVERIFY(!plan.restorePosition);
	QCOMPARE(plan.normalGeometry.topLeft(), screens.at(1).availableGeometry.topLeft());
	QCOMPARE(plan.normalGeometry.size(), state.normalGeometry.size());
}

void TestQmlWindowStateController::fallsBackAfterMonitorHotplug() {
	QmlWindowState state;
	state.normalGeometry = QRect(2400, 200, 1700, 1000);
	state.screenName = QStringLiteral("Removed monitor");
	const QList< QmlScreenSnapshot > screens {
		{ QStringLiteral("Primary"), QRect(0, 0, 1280, 720), 1.0 }
	};

	const QmlWindowRestorePlan absolutePlan =
		QmlWindowStateController::createRestorePlan(state, screens, QStringLiteral("Primary"), false);
	QVERIFY(absolutePlan.restorePosition);
	QVERIFY(screens.first().availableGeometry.contains(absolutePlan.normalGeometry));
	QCOMPARE(absolutePlan.normalGeometry.size(), screens.first().availableGeometry.size());

	const QmlWindowRestorePlan waylandPlan =
		QmlWindowStateController::createRestorePlan(state, screens, QStringLiteral("Primary"), true);
	QVERIFY(!waylandPlan.restorePosition);
	QCOMPARE(waylandPlan.normalGeometry, screens.first().availableGeometry);
}

void TestQmlWindowStateController::keepsLogicalSizeAcrossDpiChanges() {
	QmlWindowState state;
	state.normalGeometry = QRect(100, 80, 1200, 760);
	state.screenName = QStringLiteral("Internal");
	state.devicePixelRatio = 1.0;
	const QList< QmlScreenSnapshot > screens {
		{ QStringLiteral("Internal"), QRect(0, 0, 1920, 1080), 2.0 }
	};

	const QmlWindowRestorePlan plan =
		QmlWindowStateController::createRestorePlan(state, screens, QStringLiteral("Internal"), false);
	// QWindow geometry is device-independent. A DPR change must update metadata, not rescale the window.
	QCOMPARE(plan.normalGeometry, state.normalGeometry);
	QCOMPARE(plan.targetDevicePixelRatio, 2.0);
}

void TestQmlWindowStateController::recognizesCompositorManagedPlatforms() {
	QVERIFY(QmlWindowStateController::platformUsesCompositorManagedPositioning(QStringLiteral("wayland")));
	QVERIFY(QmlWindowStateController::platformUsesCompositorManagedPositioning(QStringLiteral("wayland-egl")));
	QVERIFY(QmlWindowStateController::platformUsesCompositorManagedPositioning(QStringLiteral(" WAYLAND ")));
	QVERIFY(!QmlWindowStateController::platformUsesCompositorManagedPositioning(QStringLiteral("xcb")));
	QVERIFY(!QmlWindowStateController::platformUsesCompositorManagedPositioning(QStringLiteral("windows")));
	QVERIFY(!QmlWindowStateController::platformUsesCompositorManagedPositioning(QStringLiteral("cocoa")));
}

QTEST_GUILESS_MAIN(TestQmlWindowStateController)
#include "TestQmlWindowStateController.moc"
