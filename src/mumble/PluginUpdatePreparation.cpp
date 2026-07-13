#include "PluginUpdatePreparation.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMap>
#include <QtCore/QLibrary>
#include <QtCore/QRegularExpression>
#include <QtCore/QSet>
#include <QtCore/QTemporaryFile>
#include <QtCore/QCryptographicHash>
#include <QtCore/QUuid>
#include <QtCore/QVector>

#include <algorithm>
#include <utility>

namespace {
struct TransactionArtifact {
	QString path;
	QString fileName;
	QString token;
	bool pending = false;
};

struct DestinationTransactions {
	QString destinationPath;
	QVector< TransactionArtifact > artifacts;
};

bool parseTransactionArtifact(const QFileInfo &entry, QString &destinationName, TransactionArtifact &artifact) {
	// commitPreparedPluginFile always writes a canonical, brace-less UUID at the very end of the name. Keeping
	// this expression anchored prevents unrelated files containing ".pending-" or ".backup-" from being treated
	// as transaction state.
	static const QRegularExpression pattern(QStringLiteral(
		"^(.+)\\.(pending|backup)-([0-9A-Fa-f]{8}-[0-9A-Fa-f]{4}-4[0-9A-Fa-f]{3}-"
		"[89AaBb][0-9A-Fa-f]{3}-[0-9A-Fa-f]{12})$"));
	const QRegularExpressionMatch match = pattern.match(entry.fileName());
	if (!match.hasMatch()) {
		return false;
	}
	const QString token = match.captured(3);
	if (QUuid(token).isNull()) {
		return false;
	}
	destinationName  = match.captured(1);
	artifact.path    = entry.absoluteFilePath();
	artifact.fileName = entry.fileName();
	artifact.token   = token.toLower();
	artifact.pending = match.captured(2) == QLatin1String("pending");
	return true;
}

QString destinationGroupKey(const QString &path) {
#ifdef Q_OS_WIN
	return path.toCaseFolded();
#else
	return path;
#endif
}

} // namespace

PluginUpdateUrlValidationResult validatePluginUpdateUrl(const QUrl &url, const QUrl &previousUrl) {
	if (!url.isValid() || url.isEmpty() || url.isRelative()) {
		return { false, QStringLiteral("invalid-update-url"), QObject::tr("The plugin update URL is invalid") };
	}
	const QString scheme = url.scheme().toLower();
	if (scheme != QLatin1String("http") && scheme != QLatin1String("https")) {
		return { false, QStringLiteral("unsupported-update-scheme"),
				 QObject::tr("Plugin updates may only use HTTPS") };
	}
	if (url.host().isEmpty()) {
		return { false, QStringLiteral("invalid-update-url"), QObject::tr("The plugin update URL is invalid") };
	}
	if (!url.userInfo().isEmpty()) {
		return { false, QStringLiteral("update-url-credentials"),
				 QObject::tr("Plugin update URLs may not contain embedded credentials") };
	}
	if (scheme != QLatin1String("https")) {
		if (!previousUrl.isEmpty()) {
			return { false, QStringLiteral("insecure-redirect"),
					 QObject::tr("The plugin update refused a redirect to an insecure URL") };
		}
		return { false, QStringLiteral("insecure-update-url"),
				 QObject::tr("Plugin updates require HTTPS") };
	}
	if (!previousUrl.isEmpty()
		&& previousUrl.scheme().compare(QLatin1String("https"), Qt::CaseInsensitive) != 0) {
		return { false, QStringLiteral("insecure-redirect"),
				 QObject::tr("The plugin update refused an insecure redirect chain") };
	}
	return { true, {}, {} };
}

bool pluginUpdateDestinationMatchesInstalledPath(const QString &destinationPath, const QString &installedPath) {
	if (destinationPath.trimmed().isEmpty() || installedPath.trimmed().isEmpty()) {
		return false;
	}
	const QString normalizedDestination = QDir::cleanPath(QFileInfo(destinationPath).absoluteFilePath());
	const QString normalizedInstalled   = QDir::cleanPath(QFileInfo(installedPath).absoluteFilePath());
#ifdef Q_OS_WIN
	return normalizedDestination.compare(normalizedInstalled, Qt::CaseInsensitive) == 0;
#else
	return normalizedDestination == normalizedInstalled;
#endif
}

QString boundPluginUpdateDestination(const QString &destinationDirectory, const QString &installedPath) {
	if (destinationDirectory.trimmed().isEmpty() || installedPath.trimmed().isEmpty()) return {};
	const QString directory = QDir::cleanPath(QDir(destinationDirectory).absolutePath());
	const QFileInfo installed(QDir::cleanPath(QFileInfo(installedPath).absoluteFilePath()));
	if (installed.fileName().isEmpty()) return {};
#ifdef Q_OS_WIN
	if (installed.absolutePath().compare(directory, Qt::CaseInsensitive) != 0) return {};
#else
	if (installed.absolutePath() != directory) return {};
#endif
	return installed.absoluteFilePath();
}

QStringList discoverPluginLibraryPaths(const QStringList &searchPaths, const std::atomic< bool > *cancelled) {
	QStringList discovered;
	for (const QString &searchPath : searchPaths) {
		if (cancelled && cancelled->load()) break;
		for (const QFileInfo &entry : QDir(searchPath).entryInfoList(QDir::Files | QDir::Readable)) {
			if (cancelled && cancelled->load()) break;
			if (QLibrary::isLibrary(entry.absoluteFilePath())) discovered.push_back(entry.absoluteFilePath());
		}
	}
	discovered.removeDuplicates();
	return discovered;
}

QByteArray pluginFileSha256(const QString &path, bool *ok, const std::atomic< bool > *cancelled) {
	if (ok) *ok = false;
	QFile file(path);
	if (!file.open(QIODevice::ReadOnly)) return {};
	QCryptographicHash hash(QCryptographicHash::Sha256);
	while (!file.atEnd()) {
		if (cancelled && cancelled->load()) return {};
		const QByteArray chunk = file.read(1024 * 1024);
		if (chunk.isEmpty() && file.error() != QFileDevice::NoError) return {};
		hash.addData(chunk);
	}
	if (ok) *ok = true;
	return hash.result();
}

PluginUpdateFileResult preparePluginUpdateFile(const QByteArray &content, const QString &fileName,
										   const std::atomic< bool > *cancelled) {
	if (cancelled && cancelled->load()) return { {}, QStringLiteral("cancelled"), QObject::tr("Update cancelled"), true };
	if (content.isEmpty())
		return { {}, QStringLiteral("empty-download"), QObject::tr("The downloaded update was empty"), false };
	const QString suffix = QFileInfo(fileName).suffix();
	QTemporaryFile file(QDir::temp().filePath(
		QStringLiteral("mumble-plugin-update-XXXXXX%1").arg(suffix.isEmpty() ? QString() : "." + suffix)));
	file.setAutoRemove(false);
	if (!file.open())
		return { {}, QStringLiteral("temporary-file-error"),
				 QObject::tr("The temporary update file could not be opened"), false };
	if (file.write(content) != content.size()) {
		const QString path = file.fileName();
		file.close();
		QFile::remove(path);
		return { {}, QStringLiteral("temporary-file-error"),
				 QObject::tr("The downloaded update could not be written"), false };
	}
	file.close();
	if (cancelled && cancelled->load()) {
		QFile::remove(file.fileName());
		return { {}, QStringLiteral("cancelled"), QObject::tr("Update cancelled"), true };
	}
	return { file.fileName(), {}, {}, false };
}

PluginFileCommitResult commitPreparedPluginFile(const QString &sourcePath, const QString &destinationPath,
											const bool allowOverwrite, const std::atomic< bool > *cancelled,
											const std::function< void() > &afterBackup,
											const QByteArray &expectedSha256) {
	if (cancelled && cancelled->load())
		return { false, true, false, QStringLiteral("cancelled"), QObject::tr("Installation cancelled") };
	const QFileInfo source(sourcePath);
	const QFileInfo destination(destinationPath);
	if (!source.isFile() || destination.absoluteFilePath().isEmpty())
		return { false, false, false, QStringLiteral("invalid-source"), QObject::tr("The prepared plugin file is missing") };
	QDir directory(destination.absolutePath());
	if (!directory.exists() && !QDir().mkpath(directory.absolutePath()))
		return { false, false, false, QStringLiteral("destination-error"), QObject::tr("The plugin directory could not be created") };
	if (destination.exists() && !allowOverwrite)
		return { false, false, false, QStringLiteral("overwrite-required"), QObject::tr("The plugin already exists") };
	const QString token = QUuid::createUuid().toString(QUuid::WithoutBraces);
	const QString pendingPath = destination.absoluteFilePath() + QStringLiteral(".pending-") + token;
	const QString backupPath = destination.absoluteFilePath() + QStringLiteral(".backup-") + token;
	QFile input(source.absoluteFilePath());
	QFile output(pendingPath);
	if (!input.open(QIODevice::ReadOnly) || !output.open(QIODevice::WriteOnly | QIODevice::NewOnly))
		return { false, false, false, QStringLiteral("copy-error"), QObject::tr("The prepared plugin could not be staged") };
	QCryptographicHash hash(QCryptographicHash::Sha256);
	while (!input.atEnd()) {
		if (cancelled && cancelled->load()) {
			output.close(); QFile::remove(pendingPath);
			return { false, true, false, QStringLiteral("cancelled"), QObject::tr("Installation cancelled") };
		}
		const QByteArray chunk = input.read(1024 * 1024);
		if (chunk.isEmpty() && input.error() != QFileDevice::NoError) {
			output.close(); QFile::remove(pendingPath);
			return { false, false, false, QStringLiteral("copy-error"), QObject::tr("The prepared plugin could not be read") };
		}
		hash.addData(chunk);
		if (output.write(chunk) != chunk.size()) {
			output.close(); QFile::remove(pendingPath);
			return { false, false, false, QStringLiteral("copy-error"), QObject::tr("The prepared plugin could not be staged") };
		}
	}
	output.close();
	if (!expectedSha256.isEmpty() && hash.result() != expectedSha256) {
		QFile::remove(pendingPath);
		return { false, false, false, QStringLiteral("hash-mismatch"), QObject::tr("The prepared plugin changed before installation") };
	}
	if (cancelled && cancelled->load()) {
		QFile::remove(pendingPath);
		return { false, true, false, QStringLiteral("cancelled"), QObject::tr("Installation cancelled") };
	}
	bool backupCreated = false;
	if (destination.exists()) {
		if (!QFile::rename(destination.absoluteFilePath(), backupPath)) {
			QFile::remove(pendingPath);
			return { false, false, false, QStringLiteral("replace-error"), QObject::tr("The installed plugin could not be backed up") };
		}
		backupCreated = true;
	}
	if (afterBackup) afterBackup();
	if (cancelled && cancelled->load()) {
		QFile::remove(pendingPath);
		const bool restored = !backupCreated || QFile::rename(backupPath, destination.absoluteFilePath());
		return { false, true, backupCreated && restored,
			restored ? QStringLiteral("cancelled") : QStringLiteral("rollback-error"),
			restored ? QObject::tr("Installation cancelled")
					 : QObject::tr("Installation was cancelled, but the previous plugin could not be restored"),
			destination.absoluteFilePath(), backupCreated ? backupPath : QString() };
	}
	if (!QFile::rename(pendingPath, destination.absoluteFilePath())) {
		QFile::remove(pendingPath);
		const bool restored = !backupCreated || QFile::rename(backupPath, destination.absoluteFilePath());
		return { false, false, backupCreated && restored,
				 restored ? QStringLiteral("replace-error") : QStringLiteral("rollback-error"),
				 restored ? QObject::tr("The new plugin could not be activated; the previous file was restored")
						  : QObject::tr("The new plugin could not be activated and rollback failed"),
				 destination.absoluteFilePath(), backupCreated ? backupPath : QString() };
	}
	if (cancelled && cancelled->load()) {
		const PluginFileCommitResult provisional { true, true, false, QStringLiteral("cancelled"),
													  QObject::tr("Installation cancelled"),
													  destination.absoluteFilePath(),
													  backupCreated ? backupPath : QString() };
		const bool restored = rollbackPluginFileCommit(provisional);
		return { false, true, restored,
			restored ? QStringLiteral("cancelled") : QStringLiteral("rollback-error"),
			restored ? QObject::tr("Installation cancelled")
					 : QObject::tr("Installation was cancelled, but rollback failed"),
			provisional.destinationPath, provisional.backupPath };
	}
	PluginFileCommitResult result { true, false, false, {}, {} };
	result.destinationPath = destination.absoluteFilePath();
	result.backupPath = backupCreated ? backupPath : QString();
	return result;
}

bool finalizePluginFileCommit(const PluginFileCommitResult &commit) {
	return commit.success && (commit.backupPath.isEmpty() || QFile::remove(commit.backupPath));
}

bool rollbackPluginFileCommit(const PluginFileCommitResult &commit) {
	if (!commit.success || commit.destinationPath.isEmpty()) return false;
	if (!QFile::remove(commit.destinationPath)) return false;
	return commit.backupPath.isEmpty() || QFile::rename(commit.backupPath, commit.destinationPath);
}

PluginTransactionRecoveryResult recoverPluginFileTransactions(
	const QString &directoryPath, const std::atomic< bool > *cancelled) {
	PluginTransactionRecoveryResult result;
	const auto stopRequested = [&]() {
		if (!cancelled || !cancelled->load()) return false;
		result.cancelled = true;
		return true;
	};
	if (stopRequested()) return result;
	QDir directory(directoryPath);
	if (!directory.exists()) return result;
	QMap< QString, DestinationTransactions > transactions;
	const QFileInfoList entries = directory.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot,
											 QDir::Name | QDir::IgnoreCase);
	for (const QFileInfo &entry : entries) {
		if (stopRequested()) return result;
		QString destinationName;
		TransactionArtifact artifact;
		if (!parseTransactionArtifact(entry, destinationName, artifact)) {
			continue;
		}
		const QString destinationPath = QDir::cleanPath(directory.absoluteFilePath(destinationName));
		DestinationTransactions &group = transactions[destinationGroupKey(destinationPath)];
		if (group.destinationPath.isEmpty()) {
			group.destinationPath = destinationPath;
		}
		group.artifacts.push_back(std::move(artifact));
	}

	for (DestinationTransactions &group : transactions) {
		// Once a transaction group starts, finish that one group atomically so a
		// cancellation cannot strand a renamed backup beside half-cleaned staging
		// files. The next group remains cancellable.
		if (stopRequested()) return result;
		std::sort(group.artifacts.begin(), group.artifacts.end(), [](const TransactionArtifact &left,
																	 const TransactionArtifact &right) {
			return left.fileName.compare(right.fileName, Qt::CaseInsensitive) < 0;
		});
		QSet< QString > tokens;
		QVector< TransactionArtifact > pending;
		QVector< TransactionArtifact > backups;
		for (const TransactionArtifact &artifact : std::as_const(group.artifacts)) {
			tokens.insert(artifact.token);
			(artifact.pending ? pending : backups).push_back(artifact);
		}

		if (backups.isEmpty()) {
			// Pending files have never become active and can be removed without touching an installed plugin.
			for (const TransactionArtifact &artifact : std::as_const(pending)) {
				if (QFile::remove(artifact.path)) {
					++result.pendingRemoved;
				} else {
					result.errors.push_back(
						QObject::tr("Unable to remove abandoned staging file %1").arg(artifact.fileName));
				}
			}
			continue;
		}

		const bool exactTransaction = tokens.size() == 1 && backups.size() == 1;
		const bool destinationExists = QFileInfo::exists(group.destinationPath);
		if (!exactTransaction || destinationExists) {
			// If a destination and backup coexist, either one can be the known-good copy. Likewise, multiple UUIDs
			// cannot be ordered reliably after a crash. Leave every artifact untouched: partially moving a group could
			// make one arbitrary backup appear unambiguous during the next recovery pass.
			++result.ambiguousGroups;
			result.ambiguousArtifactsIgnored += group.artifacts.size();
			result.errors.push_back(
				QObject::tr("Ambiguous plugin transaction for %1 was left untouched to protect the installed file")
					.arg(QFileInfo(group.destinationPath).fileName()));
			continue;
		}

		const TransactionArtifact &backup = backups.constFirst();
		if (!QFile::rename(backup.path, group.destinationPath)) {
			result.errors.push_back(QObject::tr("Unable to restore abandoned backup %1").arg(backup.fileName));
			continue;
		}
		++result.backupsRestored;
		for (const TransactionArtifact &artifact : std::as_const(pending)) {
			if (QFile::remove(artifact.path)) {
				++result.pendingRemoved;
			} else {
				result.errors.push_back(
					QObject::tr("Unable to remove abandoned staging file %1").arg(artifact.fileName));
			}
		}
	}
	return result;
}
