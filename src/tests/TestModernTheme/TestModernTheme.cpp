#include "ModernTheme.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QTemporaryDir>
#include <QtTest>

class TestModernTheme : public QObject {
	Q_OBJECT

private slots:
	void loadsTypedManifestWithStableID();
	void rejectsInvalidManifest();
	void rejectsInvalidColorsAndOversizedFiles();
	void clampsMetricsAndPreservesAlpha();
	void legacyCssRequiresExplicitCompatibilityMode();
	void resolvesSharedAccentOverrides();

private:
	void writeFile(const QString &name, const QByteArray &contents);
	QByteArray manifest(const QString &accent = QStringLiteral("#94e2d5")) const;
};

void TestModernTheme::writeFile(const QString &name, const QByteArray &contents) {
	QFile file(name);
	QVERIFY(file.open(QFile::WriteOnly | QFile::Truncate));
	QCOMPARE(file.write(contents), contents.size());
}

QByteArray TestModernTheme::manifest(const QString &accent) const {
	return QString::fromLatin1(R"json({
 "formatVersion":1,"id":"catppuccin-nord","name":"Catppuccin Nord","appearance":"dark",
 "palette":{"shellBackground":"#242933","crust":"#1f242d","mantle":"#222832","base":"#2e3440",
 "surface0":"#343b49","surface1":"#3b4252","surface2":"#44505f","text":"#eceff4",
 "subtext0":"#d8dee9","overlay0":"#a7b1c1","accent":"%1","accentHover":"#b5f3e8",
 "accentSubtle":"#2994e2d5","focusAccent":"#94e2d5","red":"#bf616a","green":"#a3be8c",
 "yellow":"#ebcb8b","peach":"#d08770"},"metrics":{"shellRadius":16,"innerRadius":11,"spacing":12}
})json").arg(accent).toUtf8();
}

void TestModernTheme::loadsTypedManifestWithStableID() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString path = directory.filePath(QStringLiteral("catppuccin-nord.mumble-theme.json"));
	QByteArray document = manifest();
	document.replace("\"focusAccent\":\"#94e2d5\"", "\"focusAccent\":\"#cba6f7\"");
	writeFile(path, document);
	const auto theme = Mumble::ModernTheme::loadThemeDefinitionFile(path);
	QVERIFY(theme.has_value());
	QCOMPARE(theme->id, QStringLiteral("custom:catppuccin-nord"));
	QCOMPARE(theme->formatVersion, 1);
	QVERIFY(!theme->legacyCss);
	QCOMPARE(theme->tokens.value(QStringLiteral("--accent")).toString(), QStringLiteral("#94e2d5"));
	QCOMPARE(theme->tokens.value(QStringLiteral("--focus-accent")).toString(), QStringLiteral("#cba6f7"));
}

void TestModernTheme::rejectsInvalidManifest() {
	QTemporaryDir directory;
	const QString path = directory.filePath(QStringLiteral("invalid.mumble-theme.json"));
	writeFile(path, QByteArrayLiteral("{\"formatVersion\":1,\"id\":\"invalid\"}"));
	QVERIFY(!Mumble::ModernTheme::loadThemeDefinitionFile(path).has_value());
}

void TestModernTheme::rejectsInvalidColorsAndOversizedFiles() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	const QString invalidColorPath = directory.filePath(QStringLiteral("invalid-color.mumble-theme.json"));
	writeFile(invalidColorPath, manifest(QStringLiteral("not-a-color")));
	QVERIFY(!Mumble::ModernTheme::loadThemeDefinitionFile(invalidColorPath).has_value());

	const QString oversizedPath = directory.filePath(QStringLiteral("oversized.mumble-theme.json"));
	writeFile(oversizedPath, QByteArray(128 * 1024 + 1, ' '));
	QVERIFY(!Mumble::ModernTheme::loadThemeDefinitionFile(oversizedPath).has_value());
}

void TestModernTheme::clampsMetricsAndPreservesAlpha() {
	QTemporaryDir directory;
	QVERIFY(directory.isValid());
	QByteArray document = manifest();
	document.replace("\"shellRadius\":16", "\"shellRadius\":999");
	document.replace("\"innerRadius\":11", "\"innerRadius\":-10");
	document.replace("\"spacing\":12", "\"spacing\":99");
	const QString path = directory.filePath(QStringLiteral("bounded.mumble-theme.json"));
	writeFile(path, document);
	const auto theme = Mumble::ModernTheme::loadThemeDefinitionFile(path);
	QVERIFY(theme.has_value());
	QCOMPARE(theme->metrics.shellRadius, 64);
	QCOMPARE(theme->metrics.innerRadius, 0);
	QCOMPARE(theme->metrics.spacing, 48);
	QCOMPARE(theme->tokens.value(QStringLiteral("--accent-soft")).toString(), QStringLiteral("#2994e2d5"));
}

void TestModernTheme::legacyCssRequiresExplicitCompatibilityMode() {
	QTemporaryDir directory;
	const QString path = directory.filePath(QStringLiteral("catppuccin-nord.css"));
	writeFile(path, QByteArrayLiteral(
		"/* mumble-theme-id: catppuccin-nord */\n/* mumble-theme-name: Legacy */\n:root { --accent: #ff0000; }"));
	QVERIFY(!Mumble::ModernTheme::loadThemeDefinitionFile(path).has_value());
	const auto theme = Mumble::ModernTheme::loadThemeDefinitionFile(path, true);
	QVERIFY(theme.has_value());
	QVERIFY(theme->legacyCss);
	QCOMPARE(theme->tokens.value(QStringLiteral("--accent")).toString(), QStringLiteral("#ff0000"));
}

void TestModernTheme::resolvesSharedAccentOverrides() {
	using namespace Mumble::ModernTheme;
	QCOMPARE(automaticAccentId(), QStringLiteral("auto"));
	QCOMPARE(normalizedAccentId(QStringLiteral(" AUTO ")), automaticAccentId());
	QCOMPARE(normalizedAccentId(QStringLiteral("not-an-accent")), automaticAccentId());
	QVERIFY(!accentColorOverride(automaticAccentId()).isValid());
	QVERIFY(!accentColorOverride(automaticAccentId(), QStringLiteral("#123456")).isValid());
	QCOMPARE(accentColorOverride(QStringLiteral("teal")), QColor(QStringLiteral("#5ec8b0")));
	QCOMPARE(accentColorOverride(QStringLiteral("blue")), QColor(QStringLiteral("#73b7ff")));
	QCOMPARE(accentColorOverride(QStringLiteral("violet")), QColor(QStringLiteral("#b59cff")));
	QCOMPARE(accentColorOverride(QStringLiteral("amber")), QColor(QStringLiteral("#f2c76f")));
	QCOMPARE(accentColorOverride(QStringLiteral("rose")), QColor(QStringLiteral("#ff8aa0")));
	QCOMPARE(accentColorOverride(QStringLiteral("custom"), QStringLiteral("#123456")),
		QColor(QStringLiteral("#123456")));
	QVERIFY(!accentColorOverride(QStringLiteral("not-an-accent")).isValid());
}

QTEST_APPLESS_MAIN(TestModernTheme)
#include "TestModernTheme.moc"
