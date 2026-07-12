// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_MODERNTHEME_H_
#define MUMBLE_MUMBLE_MODERNTHEME_H_

#include <QtCore/QDir>
#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QStringList>
#include <QtCore/QVariant>
#include <QtGui/QColor>

#include <optional>

namespace Mumble {
namespace ModernTheme {

struct ThemePalette {
	QColor shellBackground;
	QColor crust;
	QColor mantle;
	QColor base;
	QColor surface0;
	QColor surface1;
	QColor surface2;
	QColor text;
	QColor subtext0;
	QColor overlay0;
	QColor accent;
	QColor accentHover;
	QColor accentSubtle;
	QColor focusAccent;
	QColor red;
	QColor green;
	QColor yellow;
	QColor peach;
};

struct ThemeMetrics {
	int shellRadius = 16;
	int innerRadius = 11;
	int spacing     = 12;
};

struct ThemeDefinition {
	QString id;
	QString name;
	QString sourcePath;
	QString appearance;
	int formatVersion = 1;
	ThemePalette palette;
	ThemeMetrics metrics;
	bool legacyCss = false;
	QVariantMap tokens;
};

QString engineThemeId();
QStringList builtInThemeIds();
bool isBuiltInThemeId(const QString &themeID);
QString normalizedThemeId(const QString &themeID);
QString customAccentId();
QString normalizedAccentId(const QString &accentID);
QString normalizedCustomAccentColor(const QString &color);
int normalizedCustomAccentStrength(int strength);

QDir userThemeDirectory();
bool ensureUserThemeDirectory(QString *errorMessage = nullptr);

QList< ThemeDefinition > customThemes();
std::optional< ThemeDefinition > customTheme(const QString &themeID);
std::optional< ThemeDefinition > loadThemeDefinitionFile(const QString &path, bool allowLegacyCss = false);
QVariantMap customThemeTokens(const QString &themeID);
QVariantMap customAccentTokens(const QString &color, int strength);
QVariantMap themeSwatch(const QVariantMap &tokens);

} // namespace ModernTheme
} // namespace Mumble

#endif // MUMBLE_MUMBLE_MODERNTHEME_H_
