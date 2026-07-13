#include "ModernTheme.h"
#include "QmlThemeController.h"

#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtTest>

class TestQmlThemeController : public QObject {
	Q_OBJECT

private slots:
	void appliesBuiltInTokenMappingIdempotently();
	void appliesCustomManifestMetrics();
	void appliesVisualGateAppearance();
	void appliesProductDensityAccentAndTypedState();
};

namespace {
	UiThemeTokens testTokens() {
		UiThemeTokens tokens;
		tokens.crust = QColor(QStringLiteral("#101112"));
		tokens.mantle = QColor(QStringLiteral("#202122"));
		tokens.base = QColor(QStringLiteral("#303132"));
		tokens.border = QColor(QStringLiteral("#404142"));
		tokens.text = QColor(QStringLiteral("#f0f1f2"));
		tokens.subtext0 = QColor(QStringLiteral("#d0d1d2"));
		tokens.overlay0 = QColor(QStringLiteral("#909192"));
		tokens.accent = QColor(QStringLiteral("#50c0a0"));
		tokens.accentSubtle = QColor(QStringLiteral("#2950c0a0"));
		tokens.danger = QColor(QStringLiteral("#e06060"));
		tokens.success = QColor(QStringLiteral("#60d090"));
		tokens.warning = QColor(QStringLiteral("#d0b060"));
		tokens.focusAccent = QColor(QStringLiteral("#70d0b0"));
		return tokens;
	}
}

void TestQmlThemeController::appliesBuiltInTokenMappingIdempotently() {
	QmlThemeController controller;
	QSignalSpy changed(&controller, &QmlThemeController::themeChanged);
	const UiThemeTokens tokens = testTokens();
	controller.applyTokens(tokens);
	QCOMPARE(changed.count(), 1);
	QCOMPARE(controller.panel(), tokens.base);
	QCOMPARE(controller.rail(), tokens.mantle);
	QCOMPARE(controller.accent(), tokens.accent);
	QCOMPARE(controller.focus(), tokens.focusAccent);
	controller.applyTokens(tokens);
	QCOMPARE(changed.count(), 1);
}

void TestQmlThemeController::appliesCustomManifestMetrics() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("test.mumble-theme.json"));
	QFile file(path);
	QVERIFY(file.open(QFile::WriteOnly));
	file.write(R"json({"formatVersion":1,"id":"test","name":"Test","appearance":"dark","palette":{
"shellBackground":"#242933","crust":"#1f242d","mantle":"#222832","base":"#2e3440",
"surface0":"#343b49","surface1":"#3b4252","surface2":"#44505f","text":"#eceff4",
"subtext0":"#d8dee9","overlay0":"#a7b1c1","accent":"#94e2d5","accentHover":"#b5f3e8",
"accentSubtle":"#2994e2d5","focusAccent":"#94e2d5","red":"#bf616a","green":"#a3be8c",
"yellow":"#ebcb8b","peach":"#d08770"},"metrics":{"shellRadius":23,"innerRadius":9,"spacing":7}})json");
	file.close();
	const auto theme = Mumble::ModernTheme::loadThemeDefinitionFile(path);
	QVERIFY(theme.has_value());
	QmlThemeController controller;
	controller.applyTokens(testTokens(), theme->metrics, theme->palette.shellBackground);
	QCOMPARE(controller.shellBackground(), QColor(QStringLiteral("#242933")));
	QCOMPARE(controller.shellRadius(), 23);
	QCOMPARE(controller.innerRadius(), 9);
	QCOMPARE(controller.spacing(), 7);
}

void TestQmlThemeController::appliesVisualGateAppearance() {
	QmlThemeController controller;
	QSignalSpy densityChanged(&controller, &QmlThemeController::densityChanged);
	QVERIFY(controller.applyVisualGateAppearance(QStringLiteral("light"), QStringLiteral("compact")));
	QVERIFY(controller.compact());
	QCOMPARE(controller.spacing(), 8);
	QCOMPARE(controller.shellBackground(), QColor(QStringLiteral("#f7f9fc")));
	QCOMPARE(densityChanged.count(), 1);
	QVERIFY(controller.applyVisualGateAppearance(QStringLiteral("dark"), QStringLiteral("regular")));
	QVERIFY(!controller.compact());
	QCOMPARE(controller.spacing(), 12);
	QVERIFY(!controller.applyVisualGateAppearance(QStringLiteral("unknown"), QStringLiteral("regular")));
}

void TestQmlThemeController::appliesProductDensityAccentAndTypedState() {
	QmlThemeController controller;
	QSignalSpy densityChanged(&controller, &QmlThemeController::densityChanged);
	QVERIFY(controller.applyProductAppearance(QStringLiteral("nord"), QStringLiteral("spacious"),
		QStringLiteral("rose")));
	QCOMPARE(controller.themeId(), QStringLiteral("nord"));
	QCOMPARE(controller.themeSource(), QStringLiteral("modernShell"));
	QCOMPARE(controller.densityId(), QStringLiteral("spacious"));
	QCOMPARE(controller.accentId(), QStringLiteral("rose"));
	QCOMPARE(controller.spacing(), 16);
	QCOMPARE(controller.accent(), QColor(QStringLiteral("#ff8aa0")));
	QCOMPARE(densityChanged.count(), 1);
	const QVariantMap state = controller.state();
	QCOMPARE(state.value(QStringLiteral("themeId")).toString(), QStringLiteral("nord"));
	QCOMPARE(state.value(QStringLiteral("density")).toString(), QStringLiteral("spacious"));
	QCOMPARE(state.value(QStringLiteral("accent")).toString(), QStringLiteral("rose"));
	QCOMPARE(state.value(QStringLiteral("effectiveTokens")).toMap().value(QStringLiteral("spacing")).toInt(), 16);

	QVERIFY(controller.applyProductAppearance(QStringLiteral("dark"), QStringLiteral("compact"),
		QStringLiteral("custom"), QStringLiteral("#3366cc"), 75));
	QVERIFY(controller.compact());
	QCOMPARE(controller.spacing(), 8);
	QCOMPARE(controller.accent(), QColor(QStringLiteral("#3366cc")));
}

QTEST_APPLESS_MAIN(TestQmlThemeController)
#include "TestQmlThemeController.moc"
