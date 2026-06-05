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
#include <QtCore/QMap>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QStringList>
#include <QtGui/QColor>

namespace {
	constexpr qint64 kMaxThemeCssBytes = 128 * 1024;

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

	std::optional< Mumble::ModernTheme::ThemeDefinition > loadThemeFile(const QFileInfo &fileInfo) {
		if (!fileInfo.exists() || !fileInfo.isFile() || fileInfo.size() > kMaxThemeCssBytes) {
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
		theme.sourcePath          = fileInfo.absoluteFilePath();
		theme.tokens              = tokens;
		return theme;
	}

	QList< QDir > customThemeDirectories() {
		QList< QDir > directories;

		if (MumbleApplication *application = qobject_cast< MumbleApplication * >(QCoreApplication::instance())) {
			const QDir appThemeDirectory(application->applicationVersionRootPath() + QLatin1String("/ModernThemes"));
			if (appThemeDirectory.exists()) {
				directories.push_back(appThemeDirectory);
			}
		}

		if (Global::g_global_struct) {
			const QDir userDirectory = Mumble::ModernTheme::userThemeDirectory();
			if (userDirectory.exists()) {
				directories.push_back(userDirectory);
			}
		}

		return directories;
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

			const QStringList filters { QStringLiteral("*.css") };
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
	static const QSet< QString > builtIns { QStringLiteral("auto"), QStringLiteral("teal"),  QStringLiteral("blue"),
											QStringLiteral("violet"), QStringLiteral("amber"),
											QStringLiteral("rose"), QStringLiteral("custom") };
	return builtIns.contains(normalized) ? normalized : QStringLiteral("auto");
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
	const QStringList filters { QStringLiteral("*.css") };
	for (const QDir &directory : customThemeDirectories()) {
		for (const QFileInfo &fileInfo : directory.entryInfoList(filters, QDir::Files, QDir::Name)) {
			const std::optional< ThemeDefinition > theme = loadThemeFile(fileInfo);
			if (theme) {
				themesByID.insert(theme->id, *theme);
			}
		}
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
