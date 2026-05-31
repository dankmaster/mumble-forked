// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "VersionCheck.h"

#include "Global.h"
#include "MainWindow.h"
#include "NetworkConfig.h"
#include "Version.h"

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

QUrl defaultReleaseApiUrl() {
	return QUrl(QStringLiteral("https://api.github.com/repos/dankmaster/mumble/releases/tags/mumble-forked"));
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
		return {};
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
	const QUrl url(object.value(key).toString().trimmed());
	return url.isValid() ? url : QUrl();
}

QString normalizedSha256(const QJsonObject &object) {
	const QString sha256 = object.value(QStringLiteral("sha256")).toString().trimmed();
	static const QRegularExpression sha256Regex(QStringLiteral("^[0-9A-Fa-f]{64}$"));
	return sha256Regex.match(sha256).hasMatch() ? sha256.toLower() : QString();
}

bool isTrustedInstallerUrl(const QUrl &url) {
	if (!url.isValid()) {
		return false;
	}
	if (url.scheme() == QLatin1String("https")) {
		return true;
	}

	return url.isLocalFile() && hasConfiguredUpdateOverride();
}

QString updateInstallerFileName(const QJsonObject &info) {
	const QUrl installerUrl = jsonUrl(info, QStringLiteral("installerUrl"));
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
		} else if (name == QLatin1String("mumble-forked.msi")) {
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

	return VersionCheck::tr("Download the newest installer when you are ready to update.");
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

QStringList bundledUpdaterArguments(const QString &installerPath, const QString &updateDirPath, const bool passive) {
	const QString appPath = QCoreApplication::applicationFilePath();
	const QString appDir  = QFileInfo(appPath).absolutePath();
	const QDir updateDir(updateDirPath);
	const QString updaterLogPath = updateDir.filePath(QStringLiteral("mumble-updater.log"));
	const QString msiLogPath     = updateDir.filePath(QStringLiteral("mumble-update-msi.log"));

	return QStringList {
		QStringLiteral("--parent-pid"),
		QString::number(QCoreApplication::applicationPid()),
		QStringLiteral("--installer"),
		QDir::toNativeSeparators(installerPath),
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
}

QStringList msiexecUpdateArguments(const QString &installerPath, const bool passive) {
	QStringList arguments { QStringLiteral("/i"), QDir::toNativeSeparators(installerPath) };
	if (passive) {
		arguments << QStringLiteral("/passive") << QStringLiteral("/norestart");
	}
	return arguments;
}

bool launchBundledUpdater(const QString &installerPath, const bool passive) {
#ifdef Q_OS_WIN
	QDir updateDir(Global::get().qdBasePath.filePath(QStringLiteral("Updates")));
	if (!updateDir.exists() && !QDir().mkpath(updateDir.absolutePath())) {
		return false;
	}

	const QString updaterSourcePath =
		QDir(QCoreApplication::applicationDirPath()).filePath(QStringLiteral("mumble-updater.exe"));
	const QFileInfo updaterSource(updaterSourcePath);
	if (!updaterSource.isFile() || !updaterSource.isReadable()) {
		return false;
	}

	const QString updaterTargetPath =
		updateDir.filePath(QStringLiteral("mumble-updater-%1-%2.exe")
							   .arg(QCoreApplication::applicationPid())
							   .arg(QDateTime::currentMSecsSinceEpoch()));
	if (QFile::exists(updaterTargetPath) && !QFile::remove(updaterTargetPath)) {
		return false;
	}
	if (!QFile::copy(updaterSourcePath, updaterTargetPath)) {
		return false;
	}

	const QStringList arguments = bundledUpdaterArguments(installerPath, updateDir.absolutePath(), passive);
	return QProcess::startDetached(updaterTargetPath, arguments, updateDir.absolutePath());
#else
	Q_UNUSED(installerPath);
	Q_UNUSED(passive);
	return false;
#endif
}

class ForkUpdateInstaller : public QObject {
public:
	ForkUpdateInstaller(const QJsonObject &info, QWidget *parent, bool showProgress,
						std::function< void(const QString &) > readyCallback,
						std::function< void(const QString &) > failureCallback,
						std::function< void() > cancelledCallback)
		: QObject(parent ? static_cast< QObject * >(parent) : QCoreApplication::instance())
		, m_info(info), m_parent(parent), m_downloadUrl(jsonUrl(info, QStringLiteral("installerUrl")))
		, m_expectedSha256(normalizedSha256(info)), m_showProgress(showProgress)
		, m_readyCallback(std::move(readyCallback)), m_failureCallback(std::move(failureCallback))
		, m_cancelledCallback(std::move(cancelledCallback)) {
	}

	void start() {
#ifndef Q_OS_WIN
		QDesktopServices::openUrl(m_downloadUrl);
		deleteLater();
		return;
#else
		if (!VersionCheck::canInstallUpdate(m_info)) {
			showFailure(VersionCheck::tr("This update cannot be installed automatically because the update manifest is "
										 "missing a trusted installer URL or SHA256 checksum."));
			return;
		}

		QDir updateDir(Global::get().qdBasePath.filePath(QStringLiteral("Updates")));
		if (!updateDir.exists() && !QDir().mkpath(updateDir.absolutePath())) {
			showFailure(VersionCheck::tr("Mumble could not create the update download folder."));
			return;
		}

		m_targetPath = updateDir.filePath(updateInstallerFileName(m_info));
		m_file       = std::make_unique< QSaveFile >(m_targetPath);
		if (!m_file->open(QIODevice::WriteOnly)) {
			showFailure(VersionCheck::tr("Mumble could not write the update installer to %1.").arg(m_targetPath));
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

		request(m_downloadUrl);
#endif
	}

private:
	QJsonObject m_info;
	QWidget *m_parent = nullptr;
	QUrl m_downloadUrl;
	QString m_expectedSha256;
	QString m_targetPath;
	std::unique_ptr< QSaveFile > m_file;
	QCryptographicHash m_hash { QCryptographicHash::Sha256 };
	QProgressDialog *m_progress = nullptr;
	QNetworkReply *m_reply      = nullptr;
	QString m_pendingFailure;
	bool m_showProgress = true;
	bool m_cancelled    = false;
	int m_redirectCount = 0;
	std::function< void(const QString &) > m_readyCallback;
	std::function< void(const QString &) > m_failureCallback;
	std::function< void() > m_cancelledCallback;

	void request(const QUrl &url) {
		if (!isTrustedInstallerUrl(url)) {
			showFailure(VersionCheck::tr("The update installer URL is invalid."));
			return;
		}

		QNetworkRequest request(url);
		Network::prepareRequest(request);
		request.setAttribute(QNetworkRequest::CacheLoadControlAttribute, QNetworkRequest::AlwaysNetwork);

		m_reply = Global::get().nam->get(request);
		connect(m_reply, &QNetworkReply::readyRead, this, [this]() { writeReplyData(); });
		connect(m_reply, &QNetworkReply::downloadProgress, this, [this](qint64 received, qint64 total) {
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
			m_pendingFailure = VersionCheck::tr("Mumble could not finish writing the update installer.");
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
			showFailure(VersionCheck::tr("Mumble failed to download the update installer: %1").arg(errorString));
			return;
		}
		if (statusValue.isValid()) {
			const int status = statusValue.toInt();
			if (status < 200 || status >= 300) {
				showFailure(VersionCheck::tr("Mumble failed to download the update installer (HTTP %1).").arg(status));
				return;
			}
		}

		const QString actualSha256 = QString::fromLatin1(m_hash.result().toHex());
		if (actualSha256 != m_expectedSha256) {
			showFailure(VersionCheck::tr("The downloaded update installer did not match the published SHA256 checksum."));
			return;
		}

		if (!m_file || !m_file->commit()) {
			showFailure(VersionCheck::tr("Mumble could not save the verified update installer."));
			return;
		}
		m_file.reset();

		finishProgress();
		if (m_readyCallback) {
			m_readyCallback(m_targetPath);
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
							   VersionCheck::tr("The update installer was downloaded and verified."),
							   QMessageBox::NoButton, m_parent);
		messageBox.setInformativeText(
			VersionCheck::tr("Mumble will close, Windows will run the installer, and Mumble will reopen to restore your server and chat. Continue?"));
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
		if (!VersionCheck::launchPreparedUpdate(m_targetPath)) {
			if (Global::get().mw) {
				Global::get().mw->clearPendingUpdateResumeState();
			}
			showFailure(VersionCheck::tr("Mumble could not launch the update installer."));
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

bool VersionCheck::canInstallUpdate(const QJsonObject &info) {
#ifdef Q_OS_WIN
	return isTrustedInstallerUrl(jsonUrl(info, QStringLiteral("installerUrl"))) && !normalizedSha256(info).isEmpty();
#else
	Q_UNUSED(info);
	return false;
#endif
}

void VersionCheck::installUpdateFromInfo(const QJsonObject &info, QWidget *parent) {
#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	if (Global::get().mw && Global::get().mw->startModernForkUpdateDownload(info)) {
		Q_UNUSED(parent);
		return;
	}
#endif

	if (!canInstallUpdate(info)) {
		const QUrl installerUrl = jsonUrl(info, QStringLiteral("installerUrl"));
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
										  std::function< void() > cancelledCallback) {
#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	const bool wouldUseNativeUi = showProgress || !readyCallback || !failureCallback;
	if (wouldUseNativeUi && Global::get().mw && Global::get().mw->startModernForkUpdateDownload(info)) {
		Q_UNUSED(parent);
		return;
	}
#endif

	ForkUpdateInstaller *installer = new ForkUpdateInstaller(info, parent, showProgress, std::move(readyCallback),
															 std::move(failureCallback), std::move(cancelledCallback));
	installer->start();
}

QJsonObject VersionCheck::describeUpdateHandoff(const QJsonObject &info, const QString &preparedInstallerPath) {
	const QUrl releaseApiUrl  = configuredReleaseApiUrl();
	const QUrl manifestUrl    = configuredManifestUrl();
	const QUrl releaseUrl     = jsonUrl(info, QStringLiteral("releaseUrl"));
	const QUrl installerUrl   = jsonUrl(info, QStringLiteral("installerUrl"));
	const QUrl fallbackOpenUrl = installerUrl.isValid() ? installerUrl : releaseUrl;
	const QString sha256      = normalizedSha256(info);
	const QString installerFileName = updateInstallerFileName(info);
	const bool installerTrusted     = isTrustedInstallerUrl(installerUrl);
	const bool installable          = canInstallUpdate(info);

	const QDir updateDir(Global::get().qdBasePath.filePath(QStringLiteral("Updates")));
	const QString dryRunInstallerPath =
		preparedInstallerPath.trimmed().isEmpty()
			? updateDir.filePath(installerFileName)
			: preparedInstallerPath.trimmed();

	QJsonObject result;
	result.insert(QStringLiteral("releaseApiUrl"), releaseApiUrl.toString());
	result.insert(QStringLiteral("configuredManifestUrl"), manifestUrl.toString());
	result.insert(QStringLiteral("hasConfiguredUpdateOverride"), hasConfiguredUpdateOverride());
	result.insert(QStringLiteral("releaseUrl"), releaseUrl.toString());
	result.insert(QStringLiteral("releaseUrlScheme"), releaseUrl.scheme());
	result.insert(QStringLiteral("releaseUrlHost"), releaseUrl.host());
	result.insert(QStringLiteral("releaseUrlPath"), releaseUrl.path());
	result.insert(QStringLiteral("installerUrl"), installerUrl.toString());
	result.insert(QStringLiteral("installerUrlScheme"), installerUrl.scheme());
	result.insert(QStringLiteral("installerUrlHost"), installerUrl.host());
	result.insert(QStringLiteral("installerUrlPath"), installerUrl.path());
	result.insert(QStringLiteral("installerUrlTrusted"), installerTrusted);
	result.insert(QStringLiteral("sha256"), sha256);
	result.insert(QStringLiteral("sha256Valid"), !sha256.isEmpty());
	result.insert(QStringLiteral("sha256Length"), sha256.size());
	result.insert(QStringLiteral("canInstallUpdate"), installable);
	result.insert(QStringLiteral("installerFileName"), installerFileName);
	result.insert(QStringLiteral("downloadTargetPath"), QDir::toNativeSeparators(updateDir.filePath(installerFileName)));
	result.insert(QStringLiteral("downloadRequiresTrustedInstallerUrl"), true);
	result.insert(QStringLiteral("downloadRequiresSha256"), true);
	result.insert(QStringLiteral("downloadWouldVerifySha256"), installable);
	result.insert(QStringLiteral("maxRedirects"), MaxRedirects);
	result.insert(QStringLiteral("fallbackOpenUrl"), fallbackOpenUrl.toString());
	result.insert(QStringLiteral("wouldOpenReleaseUrl"), releaseUrl.isValid());
	result.insert(QStringLiteral("wouldOpenInstallerUrl"), installerUrl.isValid());
	result.insert(QStringLiteral("wouldFallbackToBrowserDownload"), !installable && fallbackOpenUrl.isValid());
	result.insert(QStringLiteral("wouldStartVerifiedDownload"), installable);
	result.insert(QStringLiteral("preparedInstallerPath"), QDir::toNativeSeparators(dryRunInstallerPath));
	result.insert(QStringLiteral("preparedInstallerAccepted"), canLaunchPreparedUpdate(dryRunInstallerPath));

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
				  stringListJsonArray(bundledUpdaterArguments(dryRunInstallerPath, updateDir.absolutePath(), true)));
	result.insert(QStringLiteral("directMsiexecProgram"), QStringLiteral("msiexec.exe"));
	result.insert(QStringLiteral("directMsiexecArguments"), stringListJsonArray(msiexecUpdateArguments(dryRunInstallerPath, true)));
#else
	result.insert(QStringLiteral("platformCanInstall"), false);
	result.insert(QStringLiteral("launchMode"), QStringLiteral("browserDownload"));
	result.insert(QStringLiteral("directLaunchMode"), QStringLiteral("browserDownload"));
	result.insert(QStringLiteral("bundledUpdaterArguments"), QJsonArray());
	result.insert(QStringLiteral("directMsiexecArguments"), QJsonArray());
#endif

	return result;
}

bool VersionCheck::canLaunchPreparedUpdate(const QString &installerPath) {
#ifdef Q_OS_WIN
	const QFileInfo installer(installerPath);
	return installer.isFile() && installer.isReadable()
		   && installer.suffix().compare(QLatin1String("msi"), Qt::CaseInsensitive) == 0;
#else
	Q_UNUSED(installerPath);
	return false;
#endif
}

bool VersionCheck::launchPreparedUpdate(const QString &installerPath, const bool passive, const bool restartAfterInstall) {
#ifdef Q_OS_WIN
	if (!canLaunchPreparedUpdate(installerPath)) {
		return false;
	}

	if (restartAfterInstall) {
		return launchBundledUpdater(installerPath, passive);
	}

	const QStringList arguments = msiexecUpdateArguments(installerPath, passive);
	return QProcess::startDetached(QStringLiteral("msiexec.exe"), arguments);
#else
	Q_UNUSED(installerPath);
	Q_UNUSED(passive);
	Q_UNUSED(restartAfterInstall);
	return false;
#endif
}

void VersionCheck::performRequest() {
	if (shouldSkipAutomaticUpdateCheck(m_autocheck)) {
		deleteLater();
		return;
	}

	if (!qEnvironmentVariable("MUMBLE_FORK_UPDATE_MANIFEST_URL").trimmed().isEmpty()) {
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

#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	if (Global::get().mw && Global::get().mw->handleModernVersionCheckResult(info, updateAvailable, m_autocheck)) {
		deleteLater();
		return;
	}
#endif

	if (updateAvailable) {
#if defined(MUMBLE_HAS_MODERN_LAYOUT)
		if (Global::get().mw && Global::get().mw->notifyForkUpdateAvailable(info, m_autocheck)) {
			deleteLater();
			return;
		}
#endif

		const QUrl installerUrl = jsonUrl(info, QStringLiteral("installerUrl"));
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
		const QString sha256 = info.value(QStringLiteral("sha256")).toString().trimmed();
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

#if defined(MUMBLE_HAS_MODERN_LAYOUT)
	if (Global::get().mw && Global::get().mw->handleModernVersionCheckFailure(message, m_autocheck)) {
		deleteLater();
		return;
	}
#endif

	if (!m_autocheck && Global::get().mw) {
		Global::get().mw->msgBox(message);
	}
	deleteLater();
}

void VersionCheck::fetched(QByteArray data, QUrl url) {
	Q_UNUSED(data);
	Q_UNUSED(url);
}
