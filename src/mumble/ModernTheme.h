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

#include <optional>

namespace Mumble {
namespace ModernTheme {

struct ThemeDefinition {
	QString id;
	QString name;
	QString sourcePath;
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
QVariantMap customThemeTokens(const QString &themeID);
QVariantMap customAccentTokens(const QString &color, int strength);
QVariantMap themeSwatch(const QVariantMap &tokens);

} // namespace ModernTheme
} // namespace Mumble

#endif // MUMBLE_MUMBLE_MODERNTHEME_H_
