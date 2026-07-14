#include "ModernTheme.h"
#include "QmlThemeController.h"

#include <QtCore/QFile>
#include <QtCore/QSet>
#include <QtCore/QTemporaryDir>
#include <QtTest>

class TestQmlThemeController : public QObject {
	Q_OBJECT

private slots:
	void appliesBuiltInTokenMappingIdempotently();
	void appliesCustomManifestMetrics();
	void appliesVisualGateAppearance();
	void exposesCompleteDistinctBuiltInThemes();
	void appliesProductDensityAccentAndTypedState();
	void autoAccentTracksThemeAndManualOverridesRemainStable();
	void exposesSemanticPreviewAndEmbedTokens();
	void mapsCustomFocusAppearanceAndLegacyFallback();
	void exposesDarkMediaCanvasForBuiltInAndCustomThemes();
	void fallsBackToNeutralMediaCanvas();
};

namespace {
	UiThemeTokens testTokens() {
		UiThemeTokens tokens;
		tokens.crust = QColor(QStringLiteral("#101112"));
		tokens.mantle = QColor(QStringLiteral("#202122"));
		tokens.base = QColor(QStringLiteral("#303132"));
		tokens.surface0 = QColor(QStringLiteral("#343536"));
		tokens.surface1 = QColor(QStringLiteral("#38393a"));
		tokens.surface2 = QColor(QStringLiteral("#444546"));
		tokens.border = QColor(QStringLiteral("#404142"));
		tokens.text = QColor(QStringLiteral("#f0f1f2"));
		tokens.subtext0 = QColor(QStringLiteral("#d0d1d2"));
		tokens.overlay0 = QColor(QStringLiteral("#909192"));
		tokens.accent = QColor(QStringLiteral("#50c0a0"));
		tokens.accentHover = QColor(QStringLiteral("#70d0b0"));
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
	QCOMPARE(controller.surfaceRaised(), tokens.surface0);
	QCOMPARE(controller.surfaceHover(), tokens.surface1);
	QCOMPARE(controller.surfaceBorder(), tokens.surface2);
	QCOMPARE(controller.mediaCanvas(), QColor(QStringLiteral("#05070a")));
	QCOMPARE(controller.rail(), tokens.mantle);
	QCOMPARE(controller.accent(), tokens.accent);
	QCOMPARE(controller.accentHover(), tokens.accentHover);
	QCOMPARE(controller.accentSubtle(), tokens.accentSubtle);
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
	QVERIFY(controller.applyVisualGateAppearance(QStringLiteral("custom"), QStringLiteral("regular")));
	QCOMPARE(controller.themeId(), QStringLiteral("visual-custom"));
	QCOMPARE(controller.mediaCanvas(), QColor(QStringLiteral("#050b12")));
	QCOMPARE(controller.accent(), QColor(QStringLiteral("#ff8aa0")));
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
	const QVariantMap effectiveTokens = state.value(QStringLiteral("effectiveTokens")).toMap();
	QCOMPARE(effectiveTokens.value(QStringLiteral("spacing")).toInt(), 16);
	QCOMPARE(effectiveTokens.value(QStringLiteral("surfaceRaised")).toString(),
		controller.surfaceRaised().name(QColor::HexRgb));
	QCOMPARE(effectiveTokens.value(QStringLiteral("--accent")).toString(),
		controller.accent().name(QColor::HexRgb));
	QVERIFY(!effectiveTokens.value(QStringLiteral("--accent-rgb")).toString().isEmpty());

	QVERIFY(controller.applyProductAppearance(QStringLiteral("dark"), QStringLiteral("compact"),
		QStringLiteral("custom"), QStringLiteral("#3366cc"), 75));
	QVERIFY(controller.compact());
	QCOMPARE(controller.spacing(), 8);
	QCOMPARE(controller.accent(), QColor(QStringLiteral("#3366cc")));
}

void TestQmlThemeController::autoAccentTracksThemeAndManualOverridesRemainStable() {
	QmlThemeController controller;

	for (const QString &themeID : Mumble::ModernTheme::builtInThemeIds()) {
		const UiThemeTokens themeTokens = uiThemeTokensForThemeId(themeID);
		QVERIFY(controller.applyProductAppearance(
			themeID, QStringLiteral("comfortable"), QStringLiteral("auto")));
		QCOMPARE(controller.themeId(), themeID);
		QCOMPARE(controller.accentId(), QStringLiteral("auto"));
		QCOMPARE(controller.accent(), themeTokens.accent);
		QCOMPARE(controller.accentHover(), themeTokens.accentHover);
		QCOMPARE(controller.accentSubtle(), themeTokens.accentSubtle);
		QCOMPARE(controller.focus(), themeTokens.focusAccent);
	}

	const QString firstTheme = QStringLiteral("nord");
	const QString secondTheme = QStringLiteral("gruvbox");
	const QColor firstThemeAccent = uiThemeTokensForThemeId(firstTheme).accent;
	const QColor secondThemeAccent = uiThemeTokensForThemeId(secondTheme).accent;
	QVERIFY(firstThemeAccent != secondThemeAccent);

	QVERIFY(controller.applyProductAppearance(
		firstTheme, QStringLiteral("comfortable"), QStringLiteral("auto")));
	QCOMPARE(controller.accent(), firstThemeAccent);
	QVERIFY(controller.applyProductAppearance(
		secondTheme, QStringLiteral("comfortable"), QStringLiteral("auto")));
	QCOMPARE(controller.accent(), secondThemeAccent);

	const QColor fixedAccent = Mumble::ModernTheme::accentColorOverride(QStringLiteral("violet"));
	QVERIFY(controller.applyProductAppearance(
		firstTheme, QStringLiteral("comfortable"), QStringLiteral("violet")));
	QCOMPARE(controller.accent(), fixedAccent);
	QVERIFY(controller.applyProductAppearance(
		secondTheme, QStringLiteral("comfortable"), QStringLiteral("violet")));
	QCOMPARE(controller.accent(), fixedAccent);

	const QColor customAccent(QStringLiteral("#3366cc"));
	QVERIFY(controller.applyProductAppearance(firstTheme, QStringLiteral("comfortable"),
		QStringLiteral("custom"), customAccent.name(), 75));
	QCOMPARE(controller.accent(), customAccent);
	QVERIFY(controller.applyProductAppearance(secondTheme, QStringLiteral("comfortable"),
		QStringLiteral("custom"), customAccent.name(), 75));
	QCOMPARE(controller.accent(), customAccent);
}

void TestQmlThemeController::exposesSemanticPreviewAndEmbedTokens() {
	QmlThemeController controller;
	QVERIFY(controller.applyProductAppearance(
		QStringLiteral("nord"), QStringLiteral("comfortable"), QStringLiteral("rose")));

	QVariantMap effectiveTokens =
		controller.state().value(QStringLiteral("effectiveTokens")).toMap();
	const auto colorValue = [](const QColor &color) {
		return color.alpha() < 255 ? color.name(QColor::HexArgb) : color.name(QColor::HexRgb);
	};
	QCOMPARE(effectiveTokens.value(QStringLiteral("previewCardBackground")).toString(),
		colorValue(controller.surfaceRaised()));
	QCOMPARE(effectiveTokens.value(QStringLiteral("previewCardHover")).toString(),
		colorValue(controller.surfaceHover()));
	QCOMPARE(effectiveTokens.value(QStringLiteral("previewCardBorder")).toString(),
		colorValue(controller.surfaceBorder()));
	QCOMPARE(effectiveTokens.value(QStringLiteral("embedCanvas")).toString(),
		colorValue(controller.mediaCanvas()));
	QCOMPARE(effectiveTokens.value(QStringLiteral("embedSurface")).toString(),
		colorValue(controller.panel()));
	QCOMPARE(effectiveTokens.value(QStringLiteral("embedBorder")).toString(),
		colorValue(controller.surfaceBorder()));
	QCOMPARE(effectiveTokens.value(QStringLiteral("embedHover")).toString(),
		colorValue(controller.surfaceHover()));
	QCOMPARE(effectiveTokens.value(QStringLiteral("embedRevealSurface")).toString(),
		colorValue(controller.strip()));
	QCOMPARE(effectiveTokens.value(QStringLiteral("embedSelection")).toString(),
		colorValue(controller.selected()));
	QCOMPARE(effectiveTokens.value(QStringLiteral("embedOverlayBase")).toString(),
		colorValue(controller.strip()));
	QCOMPARE(effectiveTokens.value(QStringLiteral("onAccent")).toString(), QStringLiteral("#10151c"));
	QCOMPARE(effectiveTokens.value(QStringLiteral("--on-accent")).toString(),
		effectiveTokens.value(QStringLiteral("onAccent")).toString());
	QVERIFY(effectiveTokens.value(QStringLiteral("--on-accent")).toString()
		!= colorValue(controller.strip()));

	QVERIFY(controller.applyProductAppearance(QStringLiteral("gruvbox"), QStringLiteral("compact"),
		QStringLiteral("custom"), QStringLiteral("#3366cc"), 75));
	effectiveTokens = controller.state().value(QStringLiteral("effectiveTokens")).toMap();
	QCOMPARE(effectiveTokens.value(QStringLiteral("onAccent")).toString(), QStringLiteral("#ffffff"));
	QCOMPARE(effectiveTokens.value(QStringLiteral("previewCardBackground")).toString(),
		colorValue(controller.surfaceRaised()));
	QCOMPARE(effectiveTokens.value(QStringLiteral("embedSelection")).toString(),
		colorValue(controller.selected()));
}

void TestQmlThemeController::exposesCompleteDistinctBuiltInThemes() {
	QSet< QString > signatures;
	const QStringList themeIDs = Mumble::ModernTheme::builtInThemeIds();
	QCOMPARE(themeIDs.size(), 8);

	for (const QString &themeID : themeIDs) {
		const UiThemeTokens tokens = uiThemeTokensForThemeId(themeID);
		QVERIFY2(tokens.crust.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.mantle.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.base.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.surface0.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.surface1.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.surface2.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.text.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.subtext0.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.overlay0.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.accent.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.accentHover.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.accentSubtle.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.focusAccent.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.red.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.green.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.yellow.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.peach.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.mediaCanvas.isValid(), qPrintable(themeID));
		QVERIFY2(tokens.mediaCanvas.lightnessF() < 0.25, qPrintable(themeID));

		signatures.insert(tokens.base.name(QColor::HexArgb) + QLatin1Char('|')
			+ tokens.mantle.name(QColor::HexArgb) + QLatin1Char('|')
			+ tokens.accent.name(QColor::HexArgb));
	}

	QCOMPARE(signatures.size(), themeIDs.size());
	QCOMPARE(uiThemeTokensForThemeId(QStringLiteral("latte")).preset, UiThemePreset::MumbleLight);
}

void TestQmlThemeController::mapsCustomFocusAppearanceAndLegacyFallback() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());

	const QString typedPath = directory.filePath(QStringLiteral("light-test.mumble-theme.json"));
	QFile typedFile(typedPath);
	QVERIFY(typedFile.open(QFile::WriteOnly));
	typedFile.write(R"json({"formatVersion":1,"id":"light-test","name":"Light Test","appearance":"light","palette":{
"shellBackground":"#fafafa","crust":"#eeeeee","mantle":"#ededed","base":"#ffffff",
"surface0":"#f5f5f5","surface1":"#e5e5e5","surface2":"#d5d5d5","text":"#111111",
"subtext0":"#333333","overlay0":"#666666","accent":"#2468aa","accentHover":"#3579bb",
"accentSubtle":"#292468aa","focusAccent":"#9a32cd","red":"#aa2222","green":"#228844",
"yellow":"#997711","peach":"#bb6633"}})json");
	typedFile.close();
	const auto typedTheme = Mumble::ModernTheme::loadThemeDefinitionFile(typedPath);
	QVERIFY(typedTheme.has_value());
	const UiThemeTokens typedTokens = uiThemeTokensForThemeDefinition(*typedTheme);
	QCOMPARE(typedTokens.preset, UiThemePreset::MumbleLight);
	QCOMPARE(typedTokens.base, QColor(QStringLiteral("#ffffff")));
	QCOMPARE(typedTokens.surface2, QColor(QStringLiteral("#d5d5d5")));
	QCOMPARE(typedTokens.accent, QColor(QStringLiteral("#2468aa")));
	QCOMPARE(typedTokens.focusAccent, QColor(QStringLiteral("#9a32cd")));
	UiThemeTokens autoTokens = typedTokens;
	QVERIFY(!applyUiThemeAccentOverride(autoTokens, QStringLiteral("auto"), QString(), 50));
	QCOMPARE(autoTokens.accent, typedTokens.accent);
	QCOMPARE(autoTokens.focusAccent, typedTokens.focusAccent);
	QCOMPARE(autoTokens.accentSubtle, typedTokens.accentSubtle);

	QmlThemeController controller;
	controller.applyTokens(autoTokens);
	QCOMPARE(controller.accent(), typedTokens.accent);
	QCOMPARE(controller.focus(), typedTokens.focusAccent);
	QCOMPARE(controller.selected(), typedTokens.accentSubtle);
	const QVariantMap customEffectiveTokens =
		controller.state().value(QStringLiteral("effectiveTokens")).toMap();
	QCOMPARE(customEffectiveTokens.value(QStringLiteral("previewCardBackground")).toString(),
		typedTokens.surface0.name(QColor::HexRgb));
	QCOMPARE(customEffectiveTokens.value(QStringLiteral("previewCardBorder")).toString(),
		typedTokens.surface2.name(QColor::HexRgb));
	QCOMPARE(customEffectiveTokens.value(QStringLiteral("embedSurface")).toString(),
		typedTokens.base.name(QColor::HexRgb));
	QCOMPARE(customEffectiveTokens.value(QStringLiteral("embedBorder")).toString(),
		typedTokens.surface2.name(QColor::HexRgb));

	for (const QString &accentID : { QStringLiteral("teal"), QStringLiteral("blue"),
			 QStringLiteral("violet"), QStringLiteral("amber"), QStringLiteral("rose") }) {
		UiThemeTokens fixedTokens = typedTokens;
		QVERIFY(applyUiThemeAccentOverride(fixedTokens, accentID, QString(), 50));
		const QColor expectedAccent = Mumble::ModernTheme::accentColorOverride(accentID);
		QCOMPARE(fixedTokens.accent, expectedAccent);
		QCOMPARE(fixedTokens.focusAccent, expectedAccent);
		controller.applyTokens(fixedTokens);
		QCOMPARE(controller.accent(), expectedAccent);
		QCOMPARE(controller.focus(), expectedAccent);
		QCOMPARE(controller.selected(), fixedTokens.accentSubtle);
	}

	const QString legacyPath = directory.filePath(QStringLiteral("partial.css"));
	QFile legacyFile(legacyPath);
	QVERIFY(legacyFile.open(QFile::WriteOnly));
	legacyFile.write("/* mumble-theme-id: partial */\n:root { --accent: #ff0000; }");
	legacyFile.close();
	const auto legacyTheme = Mumble::ModernTheme::loadThemeDefinitionFile(legacyPath, true);
	QVERIFY(legacyTheme.has_value());
	const UiThemeTokens darkBase = uiThemeTokensForThemeId(QStringLiteral("dark"));
	const UiThemeTokens legacyTokens = uiThemeTokensForThemeDefinition(*legacyTheme);
	QCOMPARE(legacyTokens.preset, UiThemePreset::MumbleDark);
	QCOMPARE(legacyTokens.base, darkBase.base);
	QCOMPARE(legacyTokens.surface0, darkBase.surface0);
	QCOMPARE(legacyTokens.surface2, darkBase.surface2);
	QCOMPARE(legacyTokens.text, darkBase.text);
	QCOMPARE(legacyTokens.accent, QColor(QStringLiteral("#ff0000")));
	QCOMPARE(legacyTokens.focusAccent, QColor(QStringLiteral("#ff0000")));
	UiThemeTokens customTokens = legacyTokens;
	QVERIFY(applyUiThemeAccentOverride(
		customTokens, QStringLiteral("custom"), QStringLiteral("#3366cc"), 75));
	controller.applyTokens(customTokens);
	QCOMPARE(controller.accent(), QColor(QStringLiteral("#3366cc")));
	QCOMPARE(controller.focus(), QColor(QStringLiteral("#3366cc")));
	QCOMPARE(controller.selected(), customTokens.accentSubtle);
}

void TestQmlThemeController::exposesDarkMediaCanvasForBuiltInAndCustomThemes() {
	const UiThemeTokens darkTokens = uiThemeTokensForThemeId(QStringLiteral("dark"));
	const UiThemeTokens lightTokens = uiThemeTokensForThemeId(QStringLiteral("light"));
	QVERIFY(darkTokens.mediaCanvas.isValid());
	QVERIFY(lightTokens.mediaCanvas.isValid());
	QVERIFY(darkTokens.mediaCanvas.lightnessF() < 0.25);
	QVERIFY(lightTokens.mediaCanvas.lightnessF() < 0.25);
	QVERIFY(darkTokens.mediaCanvas != lightTokens.mediaCanvas);

	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("media-light.mumble-theme.json"));
	QFile file(path);
	QVERIFY(file.open(QFile::WriteOnly));
	file.write(R"json({"formatVersion":1,"id":"media-light","name":"Media Light","appearance":"light","palette":{
"shellBackground":"#fafafa","crust":"#eeeeee","mantle":"#ededed","base":"#ffffff",
"surface0":"#f5f5f5","surface1":"#e5e5e5","surface2":"#d5d5d5","text":"#334455",
"subtext0":"#445566","overlay0":"#667788","accent":"#2468aa","accentHover":"#3579bb",
"accentSubtle":"#292468aa","focusAccent":"#9a32cd","red":"#aa2222","green":"#228844",
"yellow":"#997711","peach":"#bb6633"}})json");
	file.close();
	const auto customTheme = Mumble::ModernTheme::loadThemeDefinitionFile(path);
	QVERIFY(customTheme.has_value());
	const UiThemeTokens customTokens = uiThemeTokensForThemeDefinition(*customTheme);
	QVERIFY(customTokens.mediaCanvas.isValid());
	QVERIFY(customTokens.mediaCanvas.lightnessF() < 0.25);
	QVERIFY(customTokens.mediaCanvas != lightTokens.mediaCanvas);

	QmlThemeController controller;
	controller.applyTokens(customTokens);
	QCOMPARE(controller.mediaCanvas(), customTokens.mediaCanvas);
	const QVariantMap effectiveTokens = controller.state().value(QStringLiteral("effectiveTokens")).toMap();
	QCOMPARE(effectiveTokens.value(QStringLiteral("mediaCanvas")).toString(),
		customTokens.mediaCanvas.name(QColor::HexRgb));
}

void TestQmlThemeController::fallsBackToNeutralMediaCanvas() {
	UiThemeTokens tokens = testTokens();
	QVERIFY(!tokens.mediaCanvas.isValid());
	QmlThemeController controller;
	controller.applyTokens(tokens);
	QCOMPARE(controller.mediaCanvas(), QColor(QStringLiteral("#05070a")));
	QCOMPARE(controller.property("mediaCanvas").value< QColor >(), controller.mediaCanvas());
}

QTEST_APPLESS_MAIN(TestQmlThemeController)
#include "TestQmlThemeController.moc"
