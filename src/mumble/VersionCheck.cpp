// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "VersionCheck.h"

#include "Global.h"
#include "MainWindow.h"
#include "NetworkConfig.h"
#include "UiTheme.h"
#include "Version.h"

#include <QApplication>
#include <QCoreApplication>
#include <QCryptographicHash>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QMessageBox>
#include <QNetworkReply>
#include <QNetworkRequest>
#include <QPalette>
#include <QProcess>
#include <QProgressDialog>
#include <QPushButton>
#include <QRegularExpression>
#include <QSaveFile>
#include <QStringList>
#include <QTimer>

#include <memory>
#include <utility>

namespace {

constexpr int MaxRedirects = 5;
constexpr int SupportedPackageUpdaterVersion = 2;

QUrl defaultReleaseApiUrl() {
	return QUrl(QStringLiteral("https://api.github.com/repos/dankmaster/mumble/releases/tags/mumble-forked"));
}

QUrl defaultManifestUrl() {
	return QUrl(QStringLiteral(
		"https://github.com/dankmaster/mumble-forked/releases/download/mumble-forked/mumble-forked-update.json"));
}

bool hasConfiguredUpdateOverride() {
	return !qEnvironmentVariable("MUMBLE_FORK_UPDATE_URL").trimmed().isEmpty()
		   || !qEnvironmentVariable("MUMBLE_FORK_UPDATE_MANIFEST_URL").trimmed().isEmpty();
}

QUrl configuredReleaseApiUrl() {
	const QString override = qEnvironmentVariable("MUMBLE_FORK_UPDATE_URL").trimmed();
	if (!override.isEmpty()) {
		const QUrl url(override);
		if (url.isValid()) {
			return url;
		}
	}

	return defaultReleaseApiUrl();
}

QUrl configuredManifestUrl() {
	const QString override = qEnvironmentVariable("MUMBLE_FORK_UPDATE_MANIFEST_URL").trimmed();
	if (override.isEmpty()) {
		return defaultManifestUrl();
	}

	const QUrl url(override);
	return url.isValid() ? url : QUrl();
}

bool forceUpdateNotification() {
	const QString value = qEnvironmentVariable("MUMBLE_FORK_FORCE_UPDATE_NOTIFICATION").trimmed().toLower();
	return value == QLatin1String("1") || value == QLatin1String("true") || value == QLatin1String("yes")
		   || value == QLatin1String("on");
}

int jsonInt(const QJsonObject &object, const QString &key, int fallback = -1) {
	const QJsonValue value = object.value(key);
	if (value.isDouble()) {
		return value.toInt(fallback);
	}
	if (value.isString()) {
		bool ok = false;
		const int parsed = value.toString().trimmed().toInt(&ok);
		if (ok) {
			return parsed;
		}
	}
	return fallback;
}

QUrl jsonUrl(const QJsonObject &object, const QString &key) {
	const QString value = object.value(key).toString().trimmed();
	if (value.isEmpty()) {
		return {};
	}
	const QUrl url(value);
	return url.isValid() ? url : QUrl();
}

QString normalizedSha256String(const QString &value) {
	const QString sha256 = value.trimmed();
	static const QRegularExpression sha256Regex(QStringLiteral("^[0-9A-Fa-f]{64}$"));
	return sha256Regex.match(sha256).hasMatch() ? sha256.toLower() : QString();
}

QString normalizedSha256(const QJsonObject &object) {
	return normalizedSha256String(object.value(QStringLiteral("sha256")).toString());
}

QJsonObject jsonObject(const QJsonObject &object, const QString &key) {
	const QJsonValue value = object.value(key);
	return value.isObject() ? value.toObject() : QJsonObject();
}

bool isTrustedUpdateAssetUrl(const QUrl &url) {
	if (!url.isValid()) {
		return false;
	}
	if (url.scheme() == QLatin1String("https")) {
		return true;
	}

	return url.isLocalFile() && hasConfiguredUpdateOverride();
}

QJsonObject installerObject(const QJsonObject &info) {
	return jsonObject(info, QStringLiteral("installer"));
}

QUrl installerDownloadUrl(const QJsonObject &info) {
	const QUrl nestedUrl = jsonUrl(installerObject(info), QStringLiteral("url"));
	return nestedUrl.isValid() ? nestedUrl : jsonUrl(info, QStringLiteral("installerUrl"));
}

QString installerExpectedSha256(const QJsonObject &info) {
	const QString nestedSha256 = normalizedSha256(installerObject(info));
	return !nestedSha256.isEmpty() ? nestedSha256 : normalizedSha256(info);
}

QJsonObject packageObject(const QJsonObject &info) {
	return jsonObject(info, QStringLiteral("package"));
}

QUrl packageDownloadUrl(const QJsonObject &info) {
	return jsonUrl(packageObject(info), QStringLiteral("url"));
}

QString packageExpectedSha256(const QJsonObject &info) {
	return normalizedSha256(packageObject(info));
}

QString preferredUpdateMode(const QJsonObject &info) {
	const QString mode = info.value(QStringLiteral("preferredUpdate")).toString().trimmed().toLower();
	return mode.isEmpty() ? QStringLiteral("package") : mode;
}

bool canUseInstallerUpdate(const QJsonObject &info) {
#ifdef Q_OS_WIN
	return isTrustedUpdateAssetUrl(installerDownloadUrl(info)) && !installerExpectedSha256(info).isEmpty();
#else
	Q_UNUSED(info);
	return false;
#endif
}

bool canUsePackageUpdate(const QJsonObject &info) {
#ifdef Q_OS_WIN
	const QJsonObject package = packageObject(info);
	if (package.value(QStringLiteral("format")).toString().trimmed() != QLatin1String("mumble-update-v1")) {
		return false;
	}
	if (package.value(QStringLiteral("applyMode")).toString().trimmed() != QLatin1String("replace-staged-payload")) {
		return false;
	}

	const int minUpdaterVersion = jsonInt(package, QStringLiteral("minUpdaterVersion"), -1);
	if (minUpdaterVersion < 0 || minUpdaterVersion > SupportedPackageUpdaterVersion) {
		return false;
	}

	return isTrustedUpdateAssetUrl(packageDownloadUrl(info)) && !packageExpectedSha256(info).isEmpty();
#else
	Q_UNUSED(info);
	return false;
#endif
}

QString selectedUpdateMode(const QJsonObject &info) {
	if (canUsePackageUpdate(info)) {
		return QStringLiteral("package");
	}
	if (canUseInstallerUpdate(info)) {
		return QStringLiteral("installer");
	}
	return QStringLiteral("manual");
}

QUrl selectedDownloadUrl(const QJsonObject &info) {
	return selectedUpdateMode(info) == QLatin1String("package") ? packageDownloadUrl(info) : installerDownloadUrl(info);
}

QString selectedExpectedSha256(const QJsonObject &info) {
	return selectedUpdateMode(info) == QLatin1String("package") ? packageExpectedSha256(info)
																: installerExpectedSha256(info);
}

QString selectedFileSuffix(const QString &updateMode) {
	return updateMode == QLatin1String("package") ? QStringLiteral(".mumble-update") : QStringLiteral(".msi");
}

QString fallbackUpdateFileName(const QJsonObject &info, const QString &updateMode) {
	const QString version = info.value(QStringLiteral("version")).toString().trimmed();
	const int build       = jsonInt(info, QStringLiteral("build"));
	const QString suffix  = selectedFileSuffix(updateMode);
	if (!version.isEmpty()) {
		return QStringLiteral("mumble-forked-%1%2").arg(version, suffix);
	}
	if (build >= 0) {
		return QStringLiteral("mumble-forked-build-%1%2").arg(build).arg(suffix);
	}
	return updateMode == QLatin1String("package") ? QStringLiteral("mumble-forked-update.mumble-update")
												  : QStringLiteral("mumble-forked-update.msi");
}

QString updateAssetFileNameForMode(const QJsonObject &info, const QString &updateMode) {
	const QUrl downloadUrl = updateMode == QLatin1String("package") ? packageDownloadUrl(info) : installerDownloadUrl(info);
	QString fileName         = QFileInfo(downloadUrl.path()).fileName();
	if (fileName.isEmpty() || !fileName.endsWith(selectedFileSuffix(updateMode), Qt::CaseInsensitive)) {
		fileName = fallbackUpdateFileName(info, updateMode);
	}

	fileName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
	return fileName;
}

QString updateAssetFileName(const QJsonObject &info) {
	return updateAssetFileNameForMode(info, selectedUpdateMode(info));
}

QString updateAssetPathForMode(const QJsonObject &info, const QString &updateMode) {
	return QDir(Global::get().qdBasePath.filePath(QStringLiteral("Updates")))
		.filePath(updateAssetFileNameForMode(info, updateMode));
}

QString updateModeFromPath(const QString &updatePath, const QString &requestedMode) {
	const QString mode = requestedMode.trimmed().toLower();
	if (mode == QLatin1String("package") || mode == QLatin1String("installer")) {
		return mode;
	}

	const QFileInfo updateFile(updatePath);
	if (updateFile.fileName().endsWith(QLatin1String(".mumble-update"), Qt::CaseInsensitive)) {
		return QStringLiteral("package");
	}
	return QStringLiteral("installer");
}

QString updateInstallerFileName(const QJsonObject &info) {
	const QUrl installerUrl = installerDownloadUrl(info);
	QString fileName       = QFileInfo(installerUrl.path()).fileName();
	if (fileName.isEmpty() || !fileName.endsWith(QLatin1String(".msi"), Qt::CaseInsensitive)) {
		const QString version = info.value(QStringLiteral("version")).toString().trimmed();
		const int build       = jsonInt(info, QStringLiteral("build"));
		if (!version.isEmpty()) {
			fileName = QStringLiteral("mumble-forked-%1.msi").arg(version);
		} else if (build >= 0) {
			fileName = QStringLiteral("mumble-forked-build-%1.msi").arg(build);
		} else {
			fileName = QStringLiteral("mumble-forked-update.msi");
		}
	}

	fileName.replace(QRegularExpression(QStringLiteral("[^A-Za-z0-9._-]")), QStringLiteral("_"));
	return fileName;
}

QJsonObject parseJsonObject(const QByteArray &data, QString *errorMessage) {
	QJsonParseError error;
	const QJsonDocument document = QJsonDocument::fromJson(data, &error);
	if (error.error != QJsonParseError::NoError || !document.isObject()) {
		if (errorMessage) {
			*errorMessage = VersionCheck::tr("The update response was not valid JSON.");
		}
		return {};
	}

	return document.object();
}

QString releaseBodyValue(const QString &body, const QString &key) {
	const QRegularExpression regex(
		QRegularExpression::anchoredPattern(QStringLiteral(".*(?:^|\\n)\\s*-\\s*%1:\\s*([^\\n\\r]+).*").arg(key)),
		QRegularExpression::DotMatchesEverythingOption);
	const QRegularExpressionMatch match = regex.match(body);
	return match.hasMatch() ? match.captured(1).trimmed() : QString();
}

QJsonObject updateInfoFromRelease(const QJsonObject &release, QUrl *manifestUrl) {
	QJsonObject info;
	info.insert(QStringLiteral("releaseUrl"), release.value(QStringLiteral("html_url")).toString());
	info.insert(QStringLiteral("publishedAt"), release.value(QStringLiteral("published_at")).toString());

	const QString body = release.value(QStringLiteral("body")).toString();
	const QString build = releaseBodyValue(body, QStringLiteral("Build"));
	const QString version = releaseBodyValue(body, QStringLiteral("Version"));
	const QString commit = releaseBodyValue(body, QStringLiteral("Commit"));
	const QString announcement = releaseBodyValue(body, QStringLiteral("Announcement"));
	if (!build.isEmpty()) {
		info.insert(QStringLiteral("build"), build);
	}
	if (!version.isEmpty()) {
		info.insert(QStringLiteral("version"), version);
	}
	if (!commit.isEmpty()) {
		info.insert(QStringLiteral("commit"), commit);
	} else {
		info.insert(QStringLiteral("commit"), release.value(QStringLiteral("target_commitish")).toString());
	}
	if (!announcement.isEmpty()) {
		info.insert(QStringLiteral("announcement"), announcement);
	}

	const QJsonArray assets = release.value(QStringLiteral("assets")).toArray();
	for (const QJsonValue &assetValue : assets) {
		const QJsonObject asset = assetValue.toObject();
		const QString name      = asset.value(QStringLiteral("name")).toString();
		const QString url       = asset.value(QStringLiteral("browser_download_url")).toString();
		if (name == QLatin1String("mumble-forked-update.json") && manifestUrl) {
			*manifestUrl = QUrl(url);
		} else if (name == QLatin1String("mumble-forked.msi")
				   || (name.startsWith(QLatin1String("mumble-forked-"))
					   && name.endsWith(QLatin1String(".msi"), Qt::CaseInsensitive))) {
			info.insert(QStringLiteral("installerUrl"), url);
		}
	}

	return info;
}

QJsonObject normalizeManifestInfo(const QJsonObject &manifest) {
	QJsonObject info = manifest;
	if (!info.contains(QStringLiteral("releaseUrl"))) {
		info.insert(QStringLiteral("releaseUrl"),
					QStringLiteral("https://github.com/dankmaster/mumble/releases/tag/mumble-forked"));
	}

	const QJsonObject installer = installerObject(info);
	if (!info.contains(QStringLiteral("installerUrl"))) {
		const QUrl nestedInstallerUrl = jsonUrl(installer, QStringLiteral("url"));
		if (nestedInstallerUrl.isValid()) {
			info.insert(QStringLiteral("installerUrl"), nestedInstallerUrl.toString());
		}
	}
	if (!info.contains(QStringLiteral("sha256"))) {
		const QString nestedInstallerSha256 = normalizedSha256(installer);
		if (!nestedInstallerSha256.isEmpty()) {
			info.insert(QStringLiteral("sha256"), nestedInstallerSha256);
		}
	}

	if (!info.contains(QStringLiteral("installerUrl"))) {
		info.insert(QStringLiteral("installerUrl"),
					QStringLiteral("https://github.com/dankmaster/mumble/releases/download/mumble-forked/mumble-forked.msi"));
	}
	return info;
}

QString announcementText(const QJsonObject &info) {
	const QString announcement = info.value(QStringLiteral("announcement")).toString().trimmed();
	if (!announcement.isEmpty()) {
		return announcement;
	}

	return VersionCheck::tr("Download the newest update when you are ready to install.");
}

QString releaseNotesText(const QJsonObject &info) {
	QStringList sections;

	const QString releaseNotes = info.value(QStringLiteral("releaseNotes")).toString().trimmed();
	if (!releaseNotes.isEmpty()) {
		sections << releaseNotes;
	}

	const QString notes = info.value(QStringLiteral("notes")).toString().trimmed();
	if (!notes.isEmpty() && notes != releaseNotes) {
		sections << notes;
	}

	const QString changelog = info.value(QStringLiteral("changelog")).toString().trimmed();
	if (!changelog.isEmpty() && changelog != releaseNotes && changelog != notes) {
		sections << changelog;
	}

	return sections.join(QStringLiteral("\n\n"));
}

QString latestLabel(const QJsonObject &info) {
	const QString version = info.value(QStringLiteral("version")).toString().trimmed();
	const int build       = jsonInt(info, QStringLiteral("build"));

	if (!version.isEmpty() && build >= 0) {
		return VersionCheck::tr("%1, build %2").arg(version).arg(build);
	}
	if (!version.isEmpty()) {
		return version;
	}
	if (build >= 0) {
		return VersionCheck::tr("build %1").arg(build);
	}
	return VersionCheck::tr("the latest build");
}

bool isUpdateAvailable(const QJsonObject &info) {
	if (forceUpdateNotification()) {
		return true;
	}

	const int latestBuild  = jsonInt(info, QStringLiteral("build"));
	const int currentBuild = Version::getPatch(Version::get());
	if (latestBuild >= 0) {
		return latestBuild > currentBuild;
	}

	const QString latestVersion = info.value(QStringLiteral("version")).toString().trimmed();
	if (!latestVersion.isEmpty()) {
		const Version::full_t parsedVersion = Version::fromString(latestVersion);
		return parsedVersion != Version::UNKNOWN && parsedVersion > Version::get();
	}

	return false;
}

bool shouldSkipAutomaticUpdateCheck(bool autocheck) {
	if (!autocheck || forceUpdateNotification() || hasConfiguredUpdateOverride()) {
		return false;
	}

	// Build number 0 is the local-development default. Without this guard every
	// local release build reports the current production installer as an update.
	return Version::getPatch(Version::get()) == 0;
}

QJsonArray stringListJsonArray(const QStringList &items) {
	QJsonArray array;
	for (const QString &item : items) {
		array.push_back(item);
	}
	return array;
}

QString fileSha256(const QString &path) {
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}

	QCryptographicHash hash(QCryptographicHash::Sha256);
	while (!file.atEnd()) {
		const QByteArray chunk = file.read(1024 * 1024);
		if (chunk.isEmpty() && file.error() != QFile::NoError) {
			return {};
		}
		hash.addData(chunk);
	}

	return QString::fromLatin1(hash.result().toHex());
}

bool fileMatchesSha256(const QString &path, const QString &expectedSha256) {
	if (expectedSha256.isEmpty()) {
		return false;
	}
	const QFileInfo file(path);
	return file.isFile() && file.isReadable() && fileSha256(path) == expectedSha256;
}

bool canUsePreparedInstallerFallback(const QString &path) {
	const QFileInfo installer(path);
	return installer.isFile() && installer.isReadable()
		   && installer.suffix().compare(QLatin1String("msi"), Qt::CaseInsensitive) == 0;
}

void appendUpdaterThemeColor(QStringList &entries, const QString &key, const QColor &color) {
	if (color.isValid()) {
		entries << QStringLiteral("%1=%2").arg(key, color.name(QColor::HexRgb));
	}
}

QString updaterUiThemeArgument() {
	const QPalette palette = qApp ? qApp->palette() : QPalette();
	const UiThemeWindowChrome chrome = uiThemeWindowChromeForActiveTheme(palette);

	QStringList entries;
	entries << QStringLiteral("dark=%1").arg(chrome.dark ? 1 : 0);
	appendUpdaterThemeColor(entries, QStringLiteral("caption"), chrome.caption);
	appendUpdaterThemeColor(entries, QStringLiteral("captionText"), chrome.text);
	appendUpdaterThemeColor(entries, QStringLiteral("captionBorder"), chrome.border);

	if (const std::optional< UiThemeTokens > tokens = activeUiThemeTokens(); tokens) {
		appendUpdaterThemeColor(entries, QStringLiteral("crust"), tokens->crust);
		appendUpdaterThemeColor(entries, QStringLiteral("mantle"), tokens->mantle);
		appendUpdaterThemeColor(entries, QStringLiteral("base"), tokens->base);
		appendUpdaterThemeColor(entries, QStringLiteral("surface0"), tokens->surface0);
		appendUpdaterThemeColor(entries, QStringLiteral("surface1"), tokens->surface1);
		appendUpdaterThemeColor(entries, QStringLiteral("surface2"), tokens->surface2);
		appendUpdaterThemeColor(entries, QStringLiteral("text"), tokens->text);
		appendUpdaterThemeColor(entries, QStringLiteral("subtext0"), tokens->subtext0);
		appendUpdaterThemeColor(entries, QStringLiteral("overlay0"), tokens->overlay0);
		appendUpdaterThemeColor(entries, QStringLiteral("accent"), tokens->accent);
		appendUpdaterThemeColor(entries, QStringLiteral("accentHover"), tokens->accentHover);
		appendUpdaterThemeColor(entries, QStringLiteral("success"), tokens->success);
		appendUpdaterThemeColor(entries, QStringLiteral("warning"), tokens->warning);
		appendUpdaterThemeColor(entries, QStringLiteral("danger"), tokens->danger);
		appendUpdaterThemeColor(entries, QStringLiteral("onAccent"),
								tokens->accent.lightness() > 145 ? tokens->crust : tokens->text);
	} else {
		appendUpdaterThemeColor(entries, QStringLiteral("crust"), palette.color(QPalette::Window));
		appendUpdaterThemeColor(entries, QStringLiteral("mantle"), palette.color(QPalette::Window));
		appendUpdaterThemeColor(entries, QStringLiteral("base"), palette.color(QPalette::Base));
		appendUpdaterThemeColor(entries, QStringLiteral("surface0"), palette.color(QPalette::Button));
		appendUpdaterThemeColor(entries, QStringLiteral("surface1"), palette.color(QPalette::Mid));
		appendUpdaterThemeColor(entries, QStringLiteral("surface2"), palette.color(QPalette::Light));
		appendUpdaterThemeColor(entries, QStringLiteral("text"), palette.color(QPalette::WindowText));
		appendUpdaterThemeColor(entries, QStringLiteral("subtext0"), palette.color(QPalette::Text));
		appendUpdaterThemeColor(entries, QStringLiteral("overlay0"), palette.color(QPalette::Disabled, QPalette::Text));
		appendUpdaterThemeColor(entries, QStringLiteral("accent"), palette.color(QPalette::Highlight));
		appendUpdaterThemeColor(entries, QStringLiteral("accentHover"), palette.color(QPalette::Highlight));
		appendUpdaterThemeColor(entries, QStringLiteral("onAccent"), palette.color(QPalette::HighlightedText));
	}

	return entries.join(QLatin1Char(';'));
}

QStringList bundledUpdaterArguments(const QString &updatePath, const QString &updateDirPath, const bool passive,
									const QString &updateMode, const QString &fallbackInstallerPath = QString(),
									const QString &expectedUpdateSha256 = QString()) {
	const QString appPath = QCoreApplication::applicationFilePath();
	const QString appDir  = QFileInfo(appPath).absolutePath();
	const QDir updateDir(updateDirPath);
	const QString updaterLogPath = updateDir.filePath(QStringLiteral("mumble-updater.log"));
	const QString msiLogPath     = updateDir.filePath(QStringLiteral("mumble-update-msi.log"));
	const QString mode           = updateModeFromPath(updatePath, updateMode);

	QStringList arguments {
		QStringLiteral("--parent-pid"),
		QString::number(QCoreApplication::applicationPid()),
		mode == QLatin1String("package") ? QStringLiteral("--package") : QStringLiteral("--installer"),
		QDir::toNativeSeparators(updatePath),
		QStringLiteral("--app"),
		QDir::toNativeSeparators(appPath),
		QStringLiteral("--working-dir"),
		QDir::toNativeSeparators(appDir),
		QStringLiteral("--updater-log"),
		QDir::toNativeSeparators(updaterLogPath),
		QStringLiteral("--msi-log"),
		QDir::toNativeSeparators(msiLogPath),
		passive ? QStringLiteral("--passive") : QStringLiteral("--no-passive"),
	};
	if (mode == QLatin1String("package") && canUsePreparedInstallerFallback(fallbackInstallerPath)) {
		arguments << QStringLiteral("--installer") << QDir::toNativeSeparators(fallbackInstallerPath);
	}
	if (mode == QLatin1String("package") && !expectedUpdateSha256.trimmed().isEmpty()) {
		arguments << QStringLiteral("--package-sha256") << expectedUpdateSha256.trimmed().toLower();
	}
	const QString uiTheme = updaterUiThemeArgument();
	if (!uiTheme.isEmpty()) {
		arguments << QStringLiteral("--ui-theme") << uiTheme;
	}
	return arguments;
}

bool copyReplacing(const QString &sourcePath, const QString &targetPath) {
	if (!QFileInfo::exists(sourcePath)) {
		return true;
	}
	if (QFile::exists(targetPath) && !QFile::remove(targetPath)) {
		return false;
	}
	return QFile::copy(sourcePath, targetPath);
}

bool copyBundledUpdaterRuntime(const QString &appDirPath, const QString &updateDirPath) {
	const QDir appDir(appDirPath);
	const QDir updateDir(updateDirPath);
	const QStringList dependencies {
		QStringLiteral("zlib1.dll"),
	};

	for (const QString &dependency : dependencies) {
		const QString sourcePath = appDir.filePath(dependency);
		if (!QFileInfo::exists(sourcePath)) {
			continue;
		}
		if (!copyReplacing(sourcePath, updateDir.filePath(dependency))) {
			return false;
		}
	}
	return true;
}

QStringList msiexecUpdateArguments(const QString &installerPath, const bool passive) {
	QStringList arguments { QStringLiteral("/i"), QDir::toNativeSeparators(installerPath) };
	if (passive) {
		arguments << QStringLiteral("/passive") << QStringLiteral("/norestart");
	}
	return arguments;
}

QString prepareBundledUpdaterCopy(const QString &updateDirPath) {
#ifdef Q_OS_WIN
	QDir updateDir(updateDirPath);
	if (!updateDir.exists() && !QDir().mkpath(updateDir.absolutePath())) {
		return {};
	}

	const QString updaterSourcePath =
		QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("mumble-updater.exe"));
	const QFileInfo updaterSource(updaterSourcePath);
	if (!updaterSource.isFile() || !updaterSource.isReadable()) {
		return {};
	}

	const QString updaterTargetPath =
		updateDir.filePath(QStringLiteral("mumble-updater-%1-%2.exe")
							   .arg(QCoreApplication::applicationPid())
							   .arg(QDateTime::currentMSecsSinceEpoch()));
	if (QFile::exists(updaterTargetPath) && !QFile::remove(updaterTargetPath)) {
		return {};
	}
	if (!QFile::copy(updaterSourcePath, updaterTargetPath)) {
		return {};
	}
	if (!copyBundledUpdaterRuntime(QCoreApplication::applicationDirPath(), updateDir.absolutePath())) {
		QFile::remove(updaterTargetPath);
		return {};
	}
	return updaterTargetPath;
#else
	Q_UNUSED(updateDirPath);
	return {};
#endif
}

bool launchBundledUpdater(const QString &updatePath, const bool passive, const QString &updateMode,
						  const QString &fallbackInstallerPath, const QString &expectedUpdateSha256) {
#ifdef Q_OS_WIN
	QDir updateDir(Global::get().qdBasePath.filePath(QStringLiteral("Updates")));
	const QString updaterTargetPath = prepareBundledUpdaterCopy(updateDir.absolutePath());
	if (updaterTargetPath.isEmpty()) {
		return false;
	}

	const QStringList arguments =
		bundledUpdaterArguments(updatePath, updateDir.absolutePath(), passive, updateMode, fallbackInstallerPath,
								expectedUpdateSha256);
	return QProcess::startDetached(updaterTargetPath, arguments, updateDir.absolutePath());
#else
	Q_UNUSED(updatePath);
	Q_UNUSED(passive);
	Q_UNUSED(updateMode);
	Q_UNUSED(fallbackInstallerPath);
	Q_UNUSED(expectedUpdateSha256);
	return false;
#endif
}

class ForkUpdateInstaller : public QObject {
public:
	ForkUpdateInstaller(const QJsonObject &info, QWidget *parent, bool showProgress,
						std::function< void(const QString &) > readyCallback,
						std::function< void(const QString &) > failureCallback,
						std::function< void() > cancelledCallback,
						std::function< void(qint64, qint64) > progressCallback)
		: QObject(parent ? static_cast< QObject * >(parent) : QCoreApplication::instance())
		, m_info(info), m_parent(parent), m_updateMode(selectedUpdateMode(info)), m_showProgress(showProgress)
		, m_readyCallback(std::move(readyCallback)), m_failureCallback(std::move(failureCallback))
		, m_cancelledCallback(std::move(cancelledCallback)), m_progressCallback(std::move(progressCallback)) {
	}

	void start() {
#ifndef Q_OS_WIN
		QDesktopServices::openUrl(m_downloadUrl);
		deleteLater();
		return;
#else
		if (!VersionCheck::canInstallUpdate(m_info)) {
			showFailure(VersionCheck::tr("This update cannot be installed automatically because the update manifest is "
										 "missing a trusted update URL or SHA256 checksum."));
			return;
		}

		QDir updateDir(Global::get().qdBasePath.filePath(QStringLiteral("Updates")));
		if (!updateDir.exists() && !QDir().mkpath(updateDir.absolutePath())) {
			showFailure(VersionCheck::tr("Mumble could not create the update download folder."));
			return;
		}

		m_targetPath = updateDir.filePath(updateAssetFileName(m_info));
		m_file       = std::make_unique< QSaveFile >(m_targetPath);
		if (!m_file->open(QIODevice::WriteOnly)) {
			showFailure(VersionCheck::tr("Mumble could not write the update package to %1.").arg(m_targetPath));
			return;
		}

		if (m_showProgress) {
			m_progress = new QProgressDialog(VersionCheck::tr("Downloading mumble-forked update..."),
											 VersionCheck::tr("Cancel"), 0, 0, m_parent);
			m_progress->setWindowTitle(VersionCheck::tr("Mumble update"));
			m_progress->setWindowModality(Qt::WindowModal);
			m_progress->setMinimumDuration(0);
			connect(m_progress, &QProgressDialog::canceled, this, [this]() {
				m_cancelled = true;
				if (m_reply) {
					m_reply->abort();
				}
			});
			m_progress->show();
		}

		beginDownload(selectedDownloadUrl(m_info), selectedExpectedSha256(m_info), updateAssetPathForMode(m_info, m_updateMode),
					  false);
#endif
	}

private:
	QJsonObject m_info;
	QWidget *m_parent = nullptr;
	QUrl m_downloadUrl;
	QString m_expectedSha256;
	QString m_updateMode;
	QString m_targetPath;
	QString m_primaryUpdatePath;
	QString m_fallbackInstallerPath;
	std::unique_ptr< QSaveFile > m_file;
	QCryptographicHash m_hash { QCryptographicHash::Sha256 };
	QProgressDialog *m_progress = nullptr;
	QNetworkReply *m_reply      = nullptr;
	QProcess *m_prepareProcess = nullptr;
	QString m_pendingFailure;
	bool m_showProgress = true;
	bool m_cancelled    = false;
	bool m_downloadingFallbackInstaller = false;
	int m_redirectCount = 0;
	std::function< void(const QString &) > m_readyCallback;
	std::function< void(const QString &) > m_failureCallback;
	std::function< void() > m_cancelledCallback;
	std::function< void(qint64, qint64) > m_progressCallback;

	void beginDownload(const QUrl &url, const QString &expectedSha256, const QString &targetPath,
					   const bool fallbackInstaller) {
		m_downloadUrl = url;
		m_expectedSha256 = expectedSha256;
		m_targetPath = targetPath;
		m_pendingFailure.clear();
		m_redirectCount = 0;
		m_downloadingFallbackInstaller = fallbackInstaller;
		m_hash.reset();

		m_file = std::make_unique< QSaveFile >(m_targetPath);
		if (!m_file->open(QIODevice::WriteOnly)) {
			showFailure(VersionCheck::tr("Mumble could not write the update package to %1.").arg(m_targetPath));
			return;
		}

		if (m_progress && fallbackInstaller) {
			m_progress->setLabelText(VersionCheck::tr("Downloading verified MSI fallback..."));
			m_progress->setRange(0, 0);
		}

		request(m_downloadUrl);
	}

	void request(const QUrl &url) {
		if (!isTrustedUpdateAssetUrl(url)) {
			showFailure(VersionCheck::tr("The update URL is invalid."));
			return;
		}

		QNetworkRequest request(url);
		Network::prepareRequest(request);
		request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

		m_reply = Global::get().nam->get(request);
		connect(m_reply, &QNetworkReply::readyRead, this, [this]() { writeReplyData(); });
		connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
			if (m_progressCallback) {
				m_progressCallback(received, total);
			}
			if (!m_progress) {
				return;
			}
			if (total > 0) {
				m_progress->setRange(0, 1000);
				m_progress->setValue(static_cast< int >((received * 1000) / total));
			} else {
				m_progress->setRange(0, 0);
			}
		});
		connect(m_reply, &QNetworkReply::finished, this, [this]() { replyFinished(); });
	}

	bool replyHasInstallerBody() const {
		if (!m_reply) {
			return false;
		}

		const QVariant statusValue = m_reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
		if (!statusValue.isValid()) {
			return true;
		}

		const int status = statusValue.toInt();
		return status >= 200 && status < 300;
	}

	void writeReplyData() {
		if (!m_reply || m_pendingFailure.size() > 0 || !replyHasInstallerBody()) {
			if (m_reply) {
				m_reply->readAll();
			}
			return;
		}

		const QByteArray data = m_reply->readAll();
		if (data.isEmpty()) {
			return;
		}

		const qint64 written = m_file ? m_file->write(data) : -1;
		if (written != data.size()) {
			m_pendingFailure = VersionCheck::tr("Mumble could not finish writing the update package.");
			m_reply->abort();
			return;
		}
		m_hash.addData(data);
	}

	void replyFinished() {
		QNetworkReply *reply = m_reply;
		if (!reply) {
			showFailure(VersionCheck::tr("The update download failed."));
			return;
		}

		const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
		if (redirectTarget.isValid() && m_pendingFailure.isEmpty() && !m_cancelled) {
			const QUrl nextUrl = reply->url().resolved(redirectTarget.toUrl());
			reply->deleteLater();
			m_reply = nullptr;
			if (m_redirectCount >= MaxRedirects) {
				showFailure(VersionCheck::tr("The update installer redirected too many times."));
				return;
			}
			++m_redirectCount;
			request(nextUrl);
			return;
		}

		if (m_cancelled) {
			reply->deleteLater();
			m_reply = nullptr;
			cancelDownload();
			return;
		}

		writeReplyData();

		const QNetworkReply::NetworkError error = reply->error();
		const QString errorString               = reply->errorString();
		const QVariant statusValue              = reply->attribute(QNetworkRequest::HttpStatusCodeAttribute);
		reply->deleteLater();
		m_reply = nullptr;

		if (!m_pendingFailure.isEmpty()) {
			showFailure(m_pendingFailure);
			return;
		}
		if (error != QNetworkReply::NoError) {
			showFailure(VersionCheck::tr("Mumble failed to download the update package: %1").arg(errorString));
			return;
		}
		if (statusValue.isValid()) {
			const int status = statusValue.toInt();
			if (status < 200 || status >= 300) {
				showFailure(VersionCheck::tr("Mumble failed to download the update package (HTTP %1).").arg(status));
				return;
			}
		}

		const QString actualSha256 = QString::fromLatin1(m_hash.result().toHex());
		if (actualSha256 != m_expectedSha256) {
			showFailure(VersionCheck::tr("The downloaded update package did not match the published SHA256 checksum."));
			return;
		}

		if (!m_file || !m_file->commit()) {
			showFailure(VersionCheck::tr("Mumble could not save the verified update package."));
			return;
		}
		m_file.reset();

		finishDownloadedAsset();
	}

	void finishDownloadedAsset() {
		if (m_downloadingFallbackInstaller) {
			m_fallbackInstallerPath = m_targetPath;
			preparePackageOrFinish();
			return;
		}

		m_primaryUpdatePath = m_targetPath;
		if (m_updateMode == QLatin1String("package") && canUseInstallerUpdate(m_info)) {
			const QString fallbackPath = updateAssetPathForMode(m_info, QStringLiteral("installer"));
			const QString fallbackSha256 = installerExpectedSha256(m_info);
			if (fileMatchesSha256(fallbackPath, fallbackSha256)) {
				m_fallbackInstallerPath = fallbackPath;
				finishReady();
				return;
			}

			beginDownload(installerDownloadUrl(m_info), fallbackSha256, fallbackPath, true);
			return;
		}

		preparePackageOrFinish();
	}

	void preparePackageOrFinish() {
		if (m_updateMode != QLatin1String("package")) {
			finishReady();
			return;
		}

		QDir updateDir(Global::get().qdBasePath.filePath(QStringLiteral("Updates")));
		const QString updaterPath = prepareBundledUpdaterCopy(updateDir.absolutePath());
		if (updaterPath.isEmpty()) {
			showFailure(VersionCheck::tr("Mumble could not prepare the bundled updater."));
			return;
		}

		if (m_progress) {
			m_progress->setLabelText(VersionCheck::tr("Preparing update package..."));
			m_progress->setRange(0, 0);
		}

		const QString packagePath = m_primaryUpdatePath.isEmpty() ? m_targetPath : m_primaryUpdatePath;
		QStringList arguments = bundledUpdaterArguments(packagePath, updateDir.absolutePath(), true, m_updateMode,
														m_fallbackInstallerPath, packageExpectedSha256(m_info));
		arguments << QStringLiteral("--prepare") << QStringLiteral("--no-ui");

		m_prepareProcess = new QProcess(this);
		m_prepareProcess->setProgram(updaterPath);
		m_prepareProcess->setArguments(arguments);
		m_prepareProcess->setWorkingDirectory(updateDir.absolutePath());
		connect(m_prepareProcess, &QProcess::errorOccurred, this, [this](QProcess::ProcessError) {
			if (m_prepareProcess) {
				m_prepareProcess->deleteLater();
				m_prepareProcess = nullptr;
			}
			showFailure(VersionCheck::tr("Mumble could not start the update prepare step."));
		});
		connect(m_prepareProcess, qOverload< int, QProcess::ExitStatus >(&QProcess::finished), this,
				[this](const int exitCode, const QProcess::ExitStatus exitStatus) {
					QProcess *process = m_prepareProcess;
					m_prepareProcess = nullptr;
					if (process) {
						process->deleteLater();
					}
					if (exitStatus == QProcess::NormalExit && exitCode == 0) {
						finishReady();
						return;
					}
					showFailure(VersionCheck::tr("Mumble could not prepare the update package."));
				});
		m_prepareProcess->start();
	}

	void finishReady() {
		finishProgress();
		if (m_readyCallback) {
			m_readyCallback(m_primaryUpdatePath.isEmpty() ? m_targetPath : m_primaryUpdatePath);
			deleteLater();
		} else {
			promptAndLaunchInstaller();
		}
	}

	void cancelDownload() {
		if (m_file) {
			m_file->cancelWriting();
			m_file.reset();
		}
		finishProgress();
		if (m_cancelledCallback) {
			m_cancelledCallback();
		}
		deleteLater();
	}

	void showFailure(const QString &message) {
		if (m_file) {
			m_file->cancelWriting();
			m_file.reset();
		}
		finishProgress();
		if (m_failureCallback) {
			m_failureCallback(message);
		} else {
			QMessageBox::warning(m_parent, VersionCheck::tr("Mumble update"), message);
		}
		deleteLater();
	}

	void finishProgress() {
		if (m_progress) {
			m_progress->hide();
			m_progress->deleteLater();
			m_progress = nullptr;
		}
	}

	void promptAndLaunchInstaller() {
		QMessageBox messageBox(QMessageBox::Question, VersionCheck::tr("Install Mumble update"),
							   VersionCheck::tr("The update package was downloaded and verified."),
							   QMessageBox::NoButton, m_parent);
		const bool hasMsiFallback = m_updateMode == QLatin1String("package")
									&& canUsePreparedInstallerFallback(m_fallbackInstallerPath);
		messageBox.setInformativeText(
			m_updateMode == QLatin1String("package")
				? (hasMsiFallback
					   ? VersionCheck::tr("Mumble will close, the updater will apply the package, use the MSI only if the package fails, and Mumble will reopen to restore your server and chat. Continue?")
					   : VersionCheck::tr("Mumble will close, the updater will apply the package, and Mumble will reopen to restore your server and chat. Continue?"))
				: VersionCheck::tr("Mumble will close, Windows will run the installer, and Mumble will reopen to restore your server and chat. Continue?"));
		QPushButton *installButton = messageBox.addButton(VersionCheck::tr("Install update"), QMessageBox::AcceptRole);
		messageBox.addButton(VersionCheck::tr("Not now"), QMessageBox::RejectRole);
		messageBox.exec();

		if (messageBox.clickedButton() != installButton) {
			deleteLater();
			return;
		}

		if (Global::get().mw) {
			Global::get().mw->prepareUpdateResumeState();
		}
		if (!VersionCheck::launchPreparedUpdate(m_primaryUpdatePath.isEmpty() ? m_targetPath : m_primaryUpdatePath,
												m_updateMode, true, true, m_fallbackInstallerPath,
												m_updateMode == QLatin1String("package") ? packageExpectedSha256(m_info)
																						 : QString())) {
			if (Global::get().mw) {
				Global::get().mw->clearPendingUpdateResumeState();
			}
			showFailure(VersionCheck::tr("Mumble could not launch the update package."));
			return;
		}

		QTimer::singleShot(0, []() { QCoreApplication::quit(); });
		deleteLater();
	}
};

} // namespace

VersionCheck::VersionCheck(bool autocheck, QObject *parent, bool, bool emitResultsOnly)
	: QObject(parent), m_autocheck(autocheck), m_emitResultsOnly(emitResultsOnly) {
	QTimer::singleShot(0, this, &VersionCheck::performRequest);
}

QString VersionCheck::updateModeForInfo(const QJsonObject &info) {
	return selectedUpdateMode(info);
}

QString VersionCheck::expectedUpdateSha256ForInfo(const QJsonObject &info) {
	return selectedExpectedSha256(info);
}

bool VersionCheck::canInstallUpdate(const QJsonObject &info) {
	return canUsePackageUpdate(info) || canUseInstallerUpdate(info);
}

void VersionCheck::installUpdateFromInfo(const QJsonObject &info, QWidget *parent) {
	if (Global::get().mw && Global::get().mw->startModernForkUpdateDownload(info)) {
		Q_UNUSED(parent);
		return;
	}

	if (!canInstallUpdate(info)) {
		const QUrl installerUrl = installerDownloadUrl(info);
		if (installerUrl.isValid()) {
			QDesktopServices::openUrl(installerUrl);
		} else {
			QMessageBox::warning(parent, tr("Mumble update"),
								 tr("This update does not include an installer that Mumble can open."));
		}
		return;
	}

	downloadUpdateFromInfo(info, parent, true);
}

void VersionCheck::downloadUpdateFromInfo(const QJsonObject &info, QWidget *parent, const bool showProgress,
										  std::function< void(const QString &) > readyCallback,
										  std::function< void(const QString &) > failureCallback,
										  std::function< void() > cancelledCallback,
										  std::function< void(qint64, qint64) > progressCallback) {
	const bool wouldUseNativeUi = showProgress || !readyCallback || !failureCallback;
	if (wouldUseNativeUi && Global::get().mw && Global::get().mw->startModernForkUpdateDownload(info)) {
		Q_UNUSED(parent);
		return;
	}

	ForkUpdateInstaller *installer = new ForkUpdateInstaller(info, parent, showProgress, std::move(readyCallback),
															 std::move(failureCallback), std::move(cancelledCallback),
															 std::move(progressCallback));
	installer->start();
}

QJsonObject VersionCheck::describeUpdateHandoff(const QJsonObject &info, const QString &preparedInstallerPath) {
	const QUrl releaseApiUrl  = configuredReleaseApiUrl();
	const QUrl manifestUrl    = configuredManifestUrl();
	const QUrl releaseUrl     = jsonUrl(info, QStringLiteral("releaseUrl"));
	const QUrl installerUrl   = installerDownloadUrl(info);
	const QUrl packageUrl     = packageDownloadUrl(info);
	const QUrl fallbackOpenUrl = installerUrl.isValid() ? installerUrl : releaseUrl;
	const QString installerSha256 = installerExpectedSha256(info);
	const QString packageSha256   = packageExpectedSha256(info);
	const QString installerFileName = updateInstallerFileName(info);
	const QString selectedMode      = selectedUpdateMode(info);
	const QString selectedFileName  = updateAssetFileName(info);
	const bool installerTrusted     = isTrustedUpdateAssetUrl(installerUrl);
	const bool packageTrusted       = isTrustedUpdateAssetUrl(packageUrl);
	const bool installable          = canInstallUpdate(info);
	const bool packageUsesMsiFallback = selectedMode == QLatin1String("package") && canUseInstallerUpdate(info);
	const QString fallbackInstallerPath =
		packageUsesMsiFallback ? updateAssetPathForMode(info, QStringLiteral("installer")) : QString();

	const QDir updateDir(Global::get().qdBasePath.filePath(QStringLiteral("Updates")));
	const QString dryRunUpdatePath = preparedInstallerPath.trimmed().isEmpty()
										 ? updateDir.filePath(selectedFileName)
										 : preparedInstallerPath.trimmed();

	QJsonObject result;
	result.insert(QStringLiteral("releaseApiUrl"), releaseApiUrl.toString());
	result.insert(QStringLiteral("configuredManifestUrl"), manifestUrl.toString());
	result.insert(QStringLiteral("hasConfiguredUpdateOverride"), hasConfiguredUpdateOverride());
	result.insert(QStringLiteral("releaseUrl"), releaseUrl.toString());
	result.insert(QStringLiteral("releaseUrlScheme"), releaseUrl.scheme());
	result.insert(QStringLiteral("releaseUrlHost"), releaseUrl.host());
	result.insert(QStringLiteral("releaseUrlPath"), releaseUrl.path());
	result.insert(QStringLiteral("preferredUpdate"), preferredUpdateMode(info));
	result.insert(QStringLiteral("selectedUpdateMode"), selectedMode);
	result.insert(QStringLiteral("installerUrl"), installerUrl.toString());
	result.insert(QStringLiteral("installerUrlScheme"), installerUrl.scheme());
	result.insert(QStringLiteral("installerUrlHost"), installerUrl.host());
	result.insert(QStringLiteral("installerUrlPath"), installerUrl.path());
	result.insert(QStringLiteral("installerUrlTrusted"), installerTrusted);
	result.insert(QStringLiteral("packageUrl"), packageUrl.toString());
	result.insert(QStringLiteral("packageUrlScheme"), packageUrl.scheme());
	result.insert(QStringLiteral("packageUrlHost"), packageUrl.host());
	result.insert(QStringLiteral("packageUrlPath"), packageUrl.path());
	result.insert(QStringLiteral("packageUrlTrusted"), packageTrusted);
	result.insert(QStringLiteral("packageSha256"), packageSha256);
	result.insert(QStringLiteral("packageSha256Valid"), !packageSha256.isEmpty());
	result.insert(QStringLiteral("packageMinUpdaterVersion"),
				  jsonInt(packageObject(info), QStringLiteral("minUpdaterVersion"), -1));
	result.insert(QStringLiteral("packageApplyMode"),
				  packageObject(info).value(QStringLiteral("applyMode")).toString().trimmed());
	result.insert(QStringLiteral("sha256"), installerSha256);
	result.insert(QStringLiteral("sha256Valid"), !installerSha256.isEmpty());
	result.insert(QStringLiteral("sha256Length"), installerSha256.size());
	result.insert(QStringLiteral("canInstallUpdate"), installable);
	result.insert(QStringLiteral("installerFileName"), installerFileName);
	result.insert(QStringLiteral("updateFileName"), selectedFileName);
	result.insert(QStringLiteral("downloadTargetPath"), QDir::toNativeSeparators(updateDir.filePath(selectedFileName)));
	result.insert(QStringLiteral("downloadRequiresTrustedInstallerUrl"), true);
	result.insert(QStringLiteral("downloadRequiresSha256"), true);
	result.insert(QStringLiteral("downloadWouldVerifySha256"), installable);
	result.insert(QStringLiteral("maxRedirects"), MaxRedirects);
	result.insert(QStringLiteral("fallbackOpenUrl"), fallbackOpenUrl.toString());
	result.insert(QStringLiteral("wouldOpenReleaseUrl"), releaseUrl.isValid());
	result.insert(QStringLiteral("wouldOpenInstallerUrl"), installerUrl.isValid());
	result.insert(QStringLiteral("wouldFallbackToBrowserDownload"), !installable && fallbackOpenUrl.isValid());
	result.insert(QStringLiteral("wouldStartVerifiedDownload"), installable);
	result.insert(QStringLiteral("packageUsesMsiFallback"), packageUsesMsiFallback);
	result.insert(QStringLiteral("fallbackInstallerPath"), QDir::toNativeSeparators(fallbackInstallerPath));
	result.insert(QStringLiteral("fallbackInstallerReady"),
				  packageUsesMsiFallback && canUsePreparedInstallerFallback(fallbackInstallerPath));
	result.insert(QStringLiteral("preparedInstallerPath"), QDir::toNativeSeparators(dryRunUpdatePath));
	result.insert(QStringLiteral("preparedUpdatePath"), QDir::toNativeSeparators(dryRunUpdatePath));
	result.insert(QStringLiteral("preparedInstallerAccepted"), canLaunchPreparedUpdate(dryRunUpdatePath, selectedMode));
	result.insert(QStringLiteral("preparedUpdateAccepted"), canLaunchPreparedUpdate(dryRunUpdatePath, selectedMode));

#ifdef Q_OS_WIN
	result.insert(QStringLiteral("platformCanInstall"), true);
	result.insert(QStringLiteral("launchMode"), QStringLiteral("bundledUpdater"));
	result.insert(QStringLiteral("directLaunchMode"), QStringLiteral("msiexec"));
	result.insert(QStringLiteral("updateDir"), QDir::toNativeSeparators(updateDir.absolutePath()));
	result.insert(QStringLiteral("bundledUpdaterSourcePath"),
				  QDir::toNativeSeparators(QDir(QCoreApplication::applicationDirPath())
											   .filePath(QStringLiteral("mumble-updater.exe"))));
	result.insert(QStringLiteral("bundledUpdaterWorkingDir"), QDir::toNativeSeparators(updateDir.absolutePath()));
	result.insert(QStringLiteral("bundledUpdaterArguments"),
				  stringListJsonArray(bundledUpdaterArguments(dryRunUpdatePath, updateDir.absolutePath(), true,
															  selectedMode, fallbackInstallerPath,
															  selectedMode == QLatin1String("package") ? packageSha256
																										: QString())));
	result.insert(QStringLiteral("directMsiexecProgram"), QStringLiteral("msiexec.exe"));
	result.insert(QStringLiteral("directMsiexecArguments"),
				  selectedMode == QLatin1String("installer")
					  ? stringListJsonArray(msiexecUpdateArguments(dryRunUpdatePath, true))
					  : QJsonArray());
#else
	result.insert(QStringLiteral("platformCanInstall"), false);
	result.insert(QStringLiteral("launchMode"), QStringLiteral("browserDownload"));
	result.insert(QStringLiteral("directLaunchMode"), QStringLiteral("browserDownload"));
	result.insert(QStringLiteral("bundledUpdaterArguments"), QJsonArray());
	result.insert(QStringLiteral("directMsiexecArguments"), QJsonArray());
#endif

	return result;
}

QString VersionCheck::preparedFallbackInstallerPathForInfo(const QJsonObject &info) {
	if (selectedUpdateMode(info) != QLatin1String("package") || !canUseInstallerUpdate(info)) {
		return {};
	}
	return updateAssetPathForMode(info, QStringLiteral("installer"));
}

bool VersionCheck::canLaunchPreparedUpdate(const QString &updatePath, const QString &updateMode) {
#ifdef Q_OS_WIN
	const QFileInfo update(updatePath);
	if (!update.isFile() || !update.isReadable()) {
		return false;
	}

	const QString mode = updateModeFromPath(updatePath, updateMode);
	if (mode == QLatin1String("package")) {
		return update.fileName().endsWith(QLatin1String(".mumble-update"), Qt::CaseInsensitive);
	}
	return update.suffix().compare(QLatin1String("msi"), Qt::CaseInsensitive) == 0;
#else
	Q_UNUSED(updatePath);
	Q_UNUSED(updateMode);
	return false;
#endif
}

bool VersionCheck::launchPreparedUpdate(const QString &updatePath, const QString &updateMode, const bool passive,
										const bool restartAfterInstall, const QString &fallbackInstallerPath,
										const QString &expectedUpdateSha256) {
#ifdef Q_OS_WIN
	const QString mode = updateModeFromPath(updatePath, updateMode);
	if (!canLaunchPreparedUpdate(updatePath, mode)) {
		return false;
	}

	if (restartAfterInstall) {
		return launchBundledUpdater(updatePath, passive, mode, fallbackInstallerPath, expectedUpdateSha256);
	}

	if (mode == QLatin1String("package")) {
		return false;
	}

	const QStringList arguments = msiexecUpdateArguments(updatePath, passive);
	return QProcess::startDetached(QStringLiteral("msiexec.exe"), arguments);
#else
	Q_UNUSED(updatePath);
	Q_UNUSED(updateMode);
	Q_UNUSED(passive);
	Q_UNUSED(restartAfterInstall);
	Q_UNUSED(fallbackInstallerPath);
	Q_UNUSED(expectedUpdateSha256);
	return false;
#endif
}

void VersionCheck::performRequest() {
	if (shouldSkipAutomaticUpdateCheck(m_autocheck)) {
		deleteLater();
		return;
	}

	if (!qEnvironmentVariable("MUMBLE_FORK_UPDATE_MANIFEST_URL").trimmed().isEmpty()
		|| qEnvironmentVariable("MUMBLE_FORK_UPDATE_URL").trimmed().isEmpty()) {
		request(configuredManifestUrl(), RequestKind::Manifest);
	} else {
		request(configuredReleaseApiUrl(), RequestKind::Release);
	}
}

void VersionCheck::request(const QUrl &url, RequestKind kind) {
	const bool localManifestOverride = kind == RequestKind::Manifest && url.isLocalFile()
									   && !qEnvironmentVariable("MUMBLE_FORK_UPDATE_MANIFEST_URL").trimmed().isEmpty();
	if (!url.isValid() || (!localManifestOverride && url.scheme() != QLatin1String("https"))) {
		finishWithFailure(tr("The forked update URL is invalid."));
		return;
	}

	m_requestKind = kind;
	m_requestURL  = url;

	QNetworkRequest request(url);
	Network::prepareRequest(request);
	request.setRawHeader("Accept", "application/vnd.github+json");
	request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

	m_reply = Global::get().nam->get(request);
	connect(m_reply, &QNetworkReply::finished, this, &VersionCheck::replyFinished);
}

void VersionCheck::replyFinished() {
	QNetworkReply *reply = qobject_cast< QNetworkReply * >(sender());
	if (!reply) {
		finishWithFailure(tr("The forked update request failed."));
		return;
	}

	const QUrl replyUrl = reply->url();
	const QVariant redirectTarget = reply->attribute(QNetworkRequest::RedirectionTargetAttribute);
	if (redirectTarget.isValid()) {
		const QUrl nextUrl = replyUrl.resolved(redirectTarget.toUrl());
		reply->deleteLater();
		m_reply = nullptr;

		if (m_redirectCount >= MaxRedirects) {
			finishWithFailure(tr("The forked update request redirected too many times."));
			return;
		}
		++m_redirectCount;
		request(nextUrl, m_requestKind);
		return;
	}

	const QByteArray data = reply->readAll();
	const QNetworkReply::NetworkError error = reply->error();
	const QString errorString = reply->errorString();
	reply->deleteLater();
	m_reply = nullptr;

	if (error != QNetworkReply::NoError) {
		finishWithFailure(tr("Mumble failed to retrieve forked update information from GitHub: %1").arg(errorString));
		return;
	}

	QString parseError;
	const QJsonObject object = parseJsonObject(data, &parseError);
	if (object.isEmpty()) {
		finishWithFailure(parseError);
		return;
	}

	if (m_requestKind == RequestKind::Release) {
		QUrl manifestUrl;
		const QJsonObject releaseInfo = updateInfoFromRelease(object, &manifestUrl);
		if (manifestUrl.isValid()) {
			m_redirectCount = 0;
			request(manifestUrl, RequestKind::Manifest);
			return;
		}
		finishWithInfo(releaseInfo);
	} else {
		finishWithInfo(normalizeManifestInfo(object));
	}
}

void VersionCheck::finishWithInfo(const QJsonObject &info) {
	const bool updateAvailable = isUpdateAvailable(info);
	if (m_emitResultsOnly) {
		emit updateInfoReceived(info, updateAvailable);
		deleteLater();
		return;
	}

	if (Global::get().mw && Global::get().mw->handleModernVersionCheckResult(info, updateAvailable, m_autocheck)) {
		deleteLater();
		return;
	}

	if (updateAvailable) {
		if (Global::get().mw && Global::get().mw->notifyForkUpdateAvailable(info, m_autocheck)) {
			deleteLater();
			return;
		}

		const QUrl installerUrl = installerDownloadUrl(info);
		const QUrl releaseUrl   = jsonUrl(info, QStringLiteral("releaseUrl"));
		const QUrl openUrl      = installerUrl.isValid() ? installerUrl : releaseUrl;

		QMessageBox messageBox(QMessageBox::Information, tr("Mumble update available"),
							   tr("A new mumble-forked build is available."), QMessageBox::NoButton,
							   Global::get().mw);
		messageBox.setTextFormat(Qt::PlainText);
		messageBox.setInformativeText(
			tr("%1\n\nCurrent: %2, build %3\nLatest: %4")
				.arg(announcementText(info))
				.arg(Version::getRelease())
				.arg(Version::getPatch(Version::get()))
				.arg(latestLabel(info)));

		QString details;
		const QString releaseNotes = releaseNotesText(info);
		if (!releaseNotes.isEmpty()) {
			details += tr("Release notes:\n%1\n\n").arg(releaseNotes);
		}
		const QString commit = info.value(QStringLiteral("commit")).toString().trimmed();
		if (!commit.isEmpty()) {
			details += tr("Commit: %1\n").arg(commit);
		}
		const QString sha256 = selectedExpectedSha256(info);
		if (!sha256.isEmpty()) {
			details += tr("SHA256: %1\n").arg(sha256);
		}
		if (releaseUrl.isValid()) {
			details += tr("Release: %1\n").arg(releaseUrl.toString());
		}
		if (!details.isEmpty()) {
			messageBox.setDetailedText(details.trimmed());
		}

		QPushButton *installButton = nullptr;
		QPushButton *openButton    = nullptr;
		if (canInstallUpdate(info)) {
			installButton = messageBox.addButton(tr("Install update"), QMessageBox::AcceptRole);
			if (openUrl.isValid()) {
				openButton = messageBox.addButton(tr("Open download"), QMessageBox::ActionRole);
			}
		} else if (openUrl.isValid()) {
			openButton = messageBox.addButton(tr("Open download"), QMessageBox::AcceptRole);
		}
		messageBox.addButton(tr("Not now"), QMessageBox::RejectRole);
		messageBox.exec();

		if (installButton && messageBox.clickedButton() == installButton) {
			installUpdateFromInfo(info, Global::get().mw);
		} else if (openButton && messageBox.clickedButton() == openButton) {
			QDesktopServices::openUrl(openUrl);
		}
	} else if (!m_autocheck && Global::get().mw) {
		Global::get().mw->msgBox(
			tr("You're on the latest mumble-forked build (%1, build %2).")
				.arg(Version::getRelease())
				.arg(Version::getPatch(Version::get())));
	}

	deleteLater();
}

void VersionCheck::finishWithFailure(const QString &message) {
	if (m_emitResultsOnly) {
		emit updateCheckFailed(message);
		deleteLater();
		return;
	}

	if (Global::get().mw && Global::get().mw->handleModernVersionCheckFailure(message, m_autocheck)) {
		deleteLater();
		return;
	}

	if (!m_autocheck && Global::get().mw) {
		Global::get().mw->msgBox(message);
	}
	deleteLater();
}

void VersionCheck::fetched(QByteArray data, QUrl url) {
	Q_UNUSED(data);
	Q_UNUSED(url);
}
