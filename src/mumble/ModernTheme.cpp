// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernTheme.h"

#include "Global.h"
#include "MumbleApplication.h"

#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QCoreApplication>
#include <QtCore/QJsonDocument>
#include <QtCore/QJsonObject>
#include <QtCore/QMap>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtGui/QColor>

namespace {
	constexpr qint64 kMaxThemeFileBytes = 128 * 1024;
	constexpr int kThemeFormatVersion = 1;

	QString normalizeThemeIdPart(const QString &text) {
		QString normalized;
		bool lastWasDash = false;

		for (const QChar &ch : text.trimmed()) {
			if ((ch >= QLatin1Char('a') && ch <= QLatin1Char('z'))
				|| (ch >= QLatin1Char('A') && ch <= QLatin1Char('Z'))
				|| (ch >= QLatin1Char('0') && ch <= QLatin1Char('9'))) {
				normalized.append(ch.toLower());
				lastWasDash = false;
			} else if (!lastWasDash && !normalized.isEmpty()) {
				normalized.append(QLatin1Char('-'));
				lastWasDash = true;
			}

			if (normalized.size() >= 64) {
				break;
			}
		}

		while (normalized.endsWith(QLatin1Char('-'))) {
			normalized.chop(1);
		}

		return normalized.isEmpty() ? QStringLiteral("theme") : normalized;
	}

	QString customThemeId(const QString &idPart) {
		return QStringLiteral("custom:") + normalizeThemeIdPart(idPart);
	}

	const QMap< QString, QColor > &fixedAccentColors() {
		static const QMap< QString, QColor > accents {
			{ QStringLiteral("teal"), QColor(QStringLiteral("#5ec8b0")) },
			{ QStringLiteral("blue"), QColor(QStringLiteral("#73b7ff")) },
			{ QStringLiteral("violet"), QColor(QStringLiteral("#b59cff")) },
			{ QStringLiteral("amber"), QColor(QStringLiteral("#f2c76f")) },
			{ QStringLiteral("rose"), QColor(QStringLiteral("#ff8aa0")) },
		};
		return accents;
	}

	QString metadataValue(const QString &css, const QString &key) {
		const QRegularExpression expression(
			QStringLiteral("(?:/\\*|//)?\\s*mumble-theme-%1\\s*:\\s*([^\\r\\n*;]+)").arg(key),
			QRegularExpression::CaseInsensitiveOption);
		const QRegularExpressionMatch match = expression.match(css);
		return match.hasMatch() ? match.captured(1).trimmed() : QString();
	}

	bool safeCssVariableValue(const QString &value) {
		const QString trimmed = value.trimmed();
		if (trimmed.isEmpty() || trimmed.size() > 180) {
			return false;
		}

		const QString lower = trimmed.toLower();
		return !trimmed.contains(QLatin1Char('{')) && !trimmed.contains(QLatin1Char('}'))
			   && !trimmed.contains(QLatin1Char('<')) && !trimmed.contains(QLatin1Char('>'))
			   && !lower.contains(QStringLiteral("url(")) && !lower.contains(QStringLiteral("@import"))
			   && !lower.contains(QStringLiteral("javascript:")) && !lower.contains(QStringLiteral("expression("));
	}

	QVariantMap parseCssTokens(const QString &css) {
		QVariantMap tokens;
		const QRegularExpression tokenExpression(
			QStringLiteral("(^|[\\s{;])(--[A-Za-z0-9-]+)\\s*:\\s*([^;{}]+);"),
			QRegularExpression::MultilineOption);
		QRegularExpressionMatchIterator matches = tokenExpression.globalMatch(css);
		while (matches.hasNext()) {
			const QRegularExpressionMatch match = matches.next();
			const QString name                  = match.captured(2).trimmed().toLower();
			const QString value                 = match.captured(3).trimmed();
			if (!name.isEmpty() && safeCssVariableValue(value)) {
				tokens.insert(name, value);
			}
		}

		const QColor accent(tokens.value(QStringLiteral("--accent")).toString());
		if (accent.isValid() && !tokens.contains(QStringLiteral("--accent-rgb"))) {
			tokens.insert(QStringLiteral("--accent-rgb"),
						  QString::fromLatin1("%1, %2, %3").arg(accent.red()).arg(accent.green()).arg(accent.blue()));
		}

		return tokens;
	}

	std::optional< Mumble::ModernTheme::ThemeDefinition > loadLegacyCssThemeFile(const QFileInfo &fileInfo) {
		if (!fileInfo.exists() || !fileInfo.isFile() || fileInfo.size() > kMaxThemeFileBytes) {
			return std::nullopt;
		}

		QFile file(fileInfo.absoluteFilePath());
		if (!file.open(QFile::ReadOnly)) {
			return std::nullopt;
		}

		const QString css    = QString::fromUtf8(file.readAll());
		const QVariantMap tokens = parseCssTokens(css);
		if (tokens.isEmpty()) {
			return std::nullopt;
		}

		Mumble::ModernTheme::ThemeDefinition theme;
		const QString configuredID = metadataValue(css, QStringLiteral("id"));
		const QString name         = metadataValue(css, QStringLiteral("name"));
		theme.id                  = customThemeId(configuredID.isEmpty() ? fileInfo.completeBaseName() : configuredID);
		theme.name                = name.isEmpty() ? fileInfo.completeBaseName() : name;
		const QString appearance  = metadataValue(css, QStringLiteral("appearance")).trimmed().toLower();
		theme.appearance          = appearance == QLatin1String("light") ? QStringLiteral("light")
																		 : QStringLiteral("dark");
		theme.sourcePath          = fileInfo.absoluteFilePath();
		theme.legacyCss           = true;
		theme.tokens              = tokens;
		return theme;
	}

	QColor requiredColor(const QJsonObject &object, const QString &key, bool &valid) {
		if (!object.value(key).isString()) {
			valid = false;
			return {};
		}
		const QColor color(object.value(key).toString());
		if (!color.isValid()) {
			valid = false;
		}
		return color;
	}

	QString cssColor(const QColor &color) {
		return color.alpha() == 255 ? color.name(QColor::HexRgb) : color.name(QColor::HexArgb);
	}

	std::optional< Mumble::ModernTheme::ThemeDefinition > loadThemeManifest(const QFileInfo &fileInfo) {
		if (!fileInfo.exists() || !fileInfo.isFile() || fileInfo.size() > kMaxThemeFileBytes) {
			return std::nullopt;
		}
		QFile file(fileInfo.absoluteFilePath());
		if (!file.open(QFile::ReadOnly)) {
			return std::nullopt;
		}
		QJsonParseError error;
		const QJsonDocument document = QJsonDocument::fromJson(file.readAll(), &error);
		if (error.error != QJsonParseError::NoError || !document.isObject()) {
			return std::nullopt;
		}
		const QJsonObject root = document.object();
		if (root.value(QStringLiteral("formatVersion")).toInt(-1) != kThemeFormatVersion
			|| !root.value(QStringLiteral("id")).isString() || !root.value(QStringLiteral("name")).isString()
			|| !root.value(QStringLiteral("palette")).isObject()) {
			return std::nullopt;
		}

		Mumble::ModernTheme::ThemeDefinition theme;
		theme.formatVersion = kThemeFormatVersion;
		theme.id = customThemeId(root.value(QStringLiteral("id")).toString());
		theme.name = root.value(QStringLiteral("name")).toString().trimmed();
		theme.appearance = root.value(QStringLiteral("appearance")).toString(QStringLiteral("dark")).toLower();
		theme.sourcePath = fileInfo.absoluteFilePath();
		if (theme.name.isEmpty() || (theme.appearance != QStringLiteral("dark") && theme.appearance != QStringLiteral("light"))) {
			return std::nullopt;
		}

		const QJsonObject palette = root.value(QStringLiteral("palette")).toObject();
		bool valid = true;
		theme.palette.shellBackground = requiredColor(palette, QStringLiteral("shellBackground"), valid);
		theme.palette.crust = requiredColor(palette, QStringLiteral("crust"), valid);
		theme.palette.mantle = requiredColor(palette, QStringLiteral("mantle"), valid);
		theme.palette.base = requiredColor(palette, QStringLiteral("base"), valid);
		theme.palette.surface0 = requiredColor(palette, QStringLiteral("surface0"), valid);
		theme.palette.surface1 = requiredColor(palette, QStringLiteral("surface1"), valid);
		theme.palette.surface2 = requiredColor(palette, QStringLiteral("surface2"), valid);
		theme.palette.text = requiredColor(palette, QStringLiteral("text"), valid);
		theme.palette.subtext0 = requiredColor(palette, QStringLiteral("subtext0"), valid);
		theme.palette.overlay0 = requiredColor(palette, QStringLiteral("overlay0"), valid);
		theme.palette.accent = requiredColor(palette, QStringLiteral("accent"), valid);
		theme.palette.accentHover = requiredColor(palette, QStringLiteral("accentHover"), valid);
		theme.palette.accentSubtle = requiredColor(palette, QStringLiteral("accentSubtle"), valid);
		theme.palette.focusAccent = requiredColor(palette, QStringLiteral("focusAccent"), valid);
		theme.palette.red = requiredColor(palette, QStringLiteral("red"), valid);
		theme.palette.green = requiredColor(palette, QStringLiteral("green"), valid);
		theme.palette.yellow = requiredColor(palette, QStringLiteral("yellow"), valid);
		theme.palette.peach = requiredColor(palette, QStringLiteral("peach"), valid);
		if (!valid) {
			return std::nullopt;
		}

		const QJsonObject metrics = root.value(QStringLiteral("metrics")).toObject();
		theme.metrics.shellRadius = qBound(0, metrics.value(QStringLiteral("shellRadius")).toInt(16), 64);
		theme.metrics.innerRadius = qBound(0, metrics.value(QStringLiteral("innerRadius")).toInt(11), 64);
		theme.metrics.spacing = qBound(0, metrics.value(QStringLiteral("spacing")).toInt(12), 48);

		// Compatibility adapter for existing native settings/preview consumers.
		theme.tokens.insert(QStringLiteral("--shell-bg"), cssColor(theme.palette.shellBackground));
		theme.tokens.insert(QStringLiteral("--shell-strip"), cssColor(theme.palette.crust));
		theme.tokens.insert(QStringLiteral("--shell-rail"), cssColor(theme.palette.mantle));
		theme.tokens.insert(QStringLiteral("--shell-panel"), cssColor(theme.palette.base));
		theme.tokens.insert(QStringLiteral("--shell-panel-soft"), cssColor(theme.palette.surface0));
		theme.tokens.insert(QStringLiteral("--shell-highlight"), cssColor(theme.palette.surface1));
		theme.tokens.insert(QStringLiteral("--surface-border"), cssColor(theme.palette.surface2));
		theme.tokens.insert(QStringLiteral("--text-strong"), cssColor(theme.palette.text));
		theme.tokens.insert(QStringLiteral("--text-main"), cssColor(theme.palette.subtext0));
		theme.tokens.insert(QStringLiteral("--text-muted"), cssColor(theme.palette.overlay0));
		theme.tokens.insert(QStringLiteral("--accent"), cssColor(theme.palette.accent));
		theme.tokens.insert(QStringLiteral("--accent-strong"), cssColor(theme.palette.accentHover));
		theme.tokens.insert(QStringLiteral("--accent-soft"), cssColor(theme.palette.accentSubtle));
		theme.tokens.insert(QStringLiteral("--focus-accent"), cssColor(theme.palette.focusAccent));
		theme.tokens.insert(QStringLiteral("--danger"), cssColor(theme.palette.red));
		theme.tokens.insert(QStringLiteral("--success"), cssColor(theme.palette.green));
		theme.tokens.insert(QStringLiteral("--warning"), cssColor(theme.palette.yellow));
		theme.tokens.insert(QStringLiteral("--latency-orange"), cssColor(theme.palette.peach));
		return theme;
	}

	QString rgbaString(const QColor &color, const qreal alpha) {
		return QString::fromLatin1("rgba(%1, %2, %3, %4)")
			.arg(color.red())
			.arg(color.green())
			.arg(color.blue())
			.arg(QString::number(qBound(0.0, alpha, 1.0), 'f', 3));
	}

	QString rgbString(const QColor &color) {
		return QString::fromLatin1("%1, %2, %3").arg(color.red()).arg(color.green()).arg(color.blue());
	}

	void seedUserThemeExamples(const QDir &userDirectory) {
		if (!userDirectory.exists()) {
			return;
		}

		if (MumbleApplication *application = qobject_cast< MumbleApplication * >(QCoreApplication::instance())) {
			const QDir appThemeDirectory(application->applicationVersionRootPath() + QLatin1String("/ModernThemes"));
			if (!appThemeDirectory.exists()) {
				return;
			}

			const QStringList filters { QStringLiteral("*.mumble-theme.json") };
			for (const QFileInfo &fileInfo : appThemeDirectory.entryInfoList(filters, QDir::Files, QDir::Name)) {
				const QString destinationPath = userDirectory.absoluteFilePath(fileInfo.fileName());
				if (!QFileInfo::exists(destinationPath)) {
					QFile::copy(fileInfo.absoluteFilePath(), destinationPath);
				}
			}
		}
	}
}

namespace Mumble {
namespace ModernTheme {

QString engineThemeId() {
	return QStringLiteral("engine");
}

QStringList builtInThemeIds() {
	return { QStringLiteral("dark"),      QStringLiteral("light"), QStringLiteral("mocha"),
			 QStringLiteral("macchiato"), QStringLiteral("frappe"), QStringLiteral("latte"),
			 QStringLiteral("nord"),      QStringLiteral("gruvbox") };
}

bool isBuiltInThemeId(const QString &themeID) {
	static const QSet< QString > builtIns { QStringLiteral("dark"),  QStringLiteral("light"),
											QStringLiteral("mocha"), QStringLiteral("macchiato"),
											QStringLiteral("frappe"), QStringLiteral("latte"),
											QStringLiteral("nord"),  QStringLiteral("gruvbox") };
	return builtIns.contains(themeID.trimmed().toLower());
}

QString customAccentId() {
	return QStringLiteral("custom");
}

QString normalizedAccentId(const QString &accentID) {
	const QString normalized = accentID.trimmed().toLower();
	return normalized == QLatin1String("auto") || normalized == customAccentId()
			   || fixedAccentColors().contains(normalized)
			   ? normalized
			   : QStringLiteral("auto");
}

QColor accentColorOverride(const QString &accentID, const QString &customColor) {
	const QString normalized = normalizedAccentId(accentID);
	if (normalized == customAccentId()) {
		return QColor(normalizedCustomAccentColor(customColor));
	}
	return fixedAccentColors().value(normalized);
}

QString normalizedCustomAccentColor(const QString &color) {
	const QColor parsed(color.trimmed());
	return parsed.isValid() ? parsed.name(QColor::HexRgb) : QStringLiteral("#5ec8b0");
}

int normalizedCustomAccentStrength(const int strength) {
	return qBound(0, strength, 100);
}

QString normalizedThemeId(const QString &themeID) {
	const QString normalized = themeID.trimmed().toLower();
	if (isBuiltInThemeId(normalized)) {
		return normalized;
	}
	if (customTheme(normalized)) {
		return normalized;
	}
	return QStringLiteral("dark");
}

QDir userThemeDirectory() {
	if (!Global::g_global_struct) {
		return QDir();
	}
	return QDir(Global::get().qdBasePath.absoluteFilePath(QLatin1String("ModernThemes")));
}

bool ensureUserThemeDirectory(QString *errorMessage) {
	if (!Global::g_global_struct) {
		if (errorMessage) {
			*errorMessage = QStringLiteral("Mumble profile storage is not initialized yet.");
		}
		return false;
	}

	QDir directory = userThemeDirectory();
	if (directory.exists()) {
		seedUserThemeExamples(directory);
		return true;
	}

	if (directory.mkpath(QStringLiteral("."))) {
		seedUserThemeExamples(directory);
		return true;
	}

	if (errorMessage) {
		*errorMessage = QStringLiteral("Unable to create %1.").arg(directory.absolutePath());
	}
	return false;
}

QList< ThemeDefinition > customThemes() {
	QMap< QString, ThemeDefinition > themesByID;
	const auto loadManifests = [&themesByID](const QDir &directory) {
		for (const QFileInfo &fileInfo : directory.entryInfoList(
				 { QStringLiteral("*.mumble-theme.json") }, QDir::Files, QDir::Name)) {
			if (const std::optional< ThemeDefinition > theme = loadThemeManifest(fileInfo); theme) {
				themesByID.insert(theme->id, *theme);
			}
		}
	};

	// Bundled manifests provide defaults. Legacy CSS is accepted only from the user's profile,
	// and a typed user manifest with the same ID takes final precedence.
	if (MumbleApplication *application = qobject_cast< MumbleApplication * >(QCoreApplication::instance())) {
		loadManifests(QDir(application->applicationVersionRootPath() + QLatin1String("/ModernThemes")));
	}
	if (Global::g_global_struct) {
		const QDir userDirectory = userThemeDirectory();
		for (const QFileInfo &fileInfo : userDirectory.entryInfoList(
				 { QStringLiteral("*.css") }, QDir::Files, QDir::Name)) {
			if (const std::optional< ThemeDefinition > theme = loadLegacyCssThemeFile(fileInfo); theme) {
				themesByID.insert(theme->id, *theme);
			}
		}
		loadManifests(userDirectory);
	}

	return themesByID.values();
}

std::optional< ThemeDefinition > customTheme(const QString &themeID) {
	const QString normalized = themeID.trimmed().toLower();
	for (const ThemeDefinition &theme : customThemes()) {
		if (theme.id == normalized) {
			return theme;
		}
	}
	return std::nullopt;
}

std::optional< ThemeDefinition > loadThemeDefinitionFile(const QString &path, const bool allowLegacyCss) {
	const QFileInfo fileInfo(path);
	if (fileInfo.fileName().endsWith(QStringLiteral(".mumble-theme.json"), Qt::CaseInsensitive)) {
		return loadThemeManifest(fileInfo);
	}
	if (allowLegacyCss && fileInfo.suffix().compare(QStringLiteral("css"), Qt::CaseInsensitive) == 0) {
		return loadLegacyCssThemeFile(fileInfo);
	}
	return std::nullopt;
}

QVariantMap customThemeTokens(const QString &themeID) {
	const std::optional< ThemeDefinition > theme = customTheme(themeID);
	return theme ? theme->tokens : QVariantMap();
}

QVariantMap customAccentTokens(const QString &color, const int strength) {
	const QColor accent(normalizedCustomAccentColor(color));
	const int normalizedStrength = normalizedCustomAccentStrength(strength);
	const qreal amount           = static_cast< qreal >(normalizedStrength) / 100.0;
	const qreal softAlpha        = 0.06 + (amount * 0.22);
	const qreal borderAlpha      = 0.22 + (amount * 0.46);
	const qreal glowAlpha        = 0.035 + (amount * 0.135);

	QVariantMap tokens;
	tokens.insert(QStringLiteral("--theme-supported-accents"),
				  QStringLiteral("auto teal blue violet amber rose custom"));
	tokens.insert(QStringLiteral("--theme-accent-custom"), accent.name(QColor::HexRgb));
	tokens.insert(QStringLiteral("--theme-accent-custom-rgb"), rgbString(accent));
	tokens.insert(QStringLiteral("--theme-accent-custom-soft"), rgbaString(accent, softAlpha));
	tokens.insert(QStringLiteral("--theme-accent-custom-border"), rgbaString(accent, borderAlpha));
	tokens.insert(QStringLiteral("--theme-accent-custom-glow"), rgbaString(accent, glowAlpha));
	tokens.insert(QStringLiteral("--body-bg-glow"), rgbaString(accent, glowAlpha));
	return tokens;
}

QVariantMap themeSwatch(const QVariantMap &tokens) {
	QVariantMap swatch;
	const QString background = tokens.value(QStringLiteral("--shell-bg"),
											tokens.value(QStringLiteral("--shell-panel"),
														 tokens.value(QStringLiteral("--shell-rail"))))
								   .toString();
	const QString accent = tokens.value(QStringLiteral("--accent")).toString();
	if (!background.isEmpty()) {
		swatch.insert(QStringLiteral("bg"), background);
	}
	if (!accent.isEmpty()) {
		swatch.insert(QStringLiteral("accent"), accent);
	}
	return swatch;
}

} // namespace ModernTheme
} // namespace Mumble
