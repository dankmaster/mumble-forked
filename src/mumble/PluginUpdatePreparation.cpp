#include "PluginUpdatePreparation.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTemporaryFile>
#include <QtCore/QCryptographicHash>
#include <QtCore/QUuid>

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
		return { false, true, backupCreated && restored, QStringLiteral("cancelled"),
				 QObject::tr("Installation cancelled") };
	}
	if (!QFile::rename(pendingPath, destination.absoluteFilePath())) {
		QFile::remove(pendingPath);
		const bool restored = !backupCreated || QFile::rename(backupPath, destination.absoluteFilePath());
		return { false, false, backupCreated && restored, QStringLiteral("replace-error"),
				 restored ? QObject::tr("The new plugin could not be activated; the previous file was restored")
						  : QObject::tr("The new plugin could not be activated and rollback failed") };
	}
	if (cancelled && cancelled->load()) {
		const PluginFileCommitResult provisional { true, true, false, QStringLiteral("cancelled"),
													  QObject::tr("Installation cancelled"),
													  destination.absoluteFilePath(),
													  backupCreated ? backupPath : QString() };
		const bool restored = rollbackPluginFileCommit(provisional);
		return { false, true, restored, QStringLiteral("cancelled"), QObject::tr("Installation cancelled") };
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

PluginTransactionRecoveryResult recoverPluginFileTransactions(const QString &directoryPath) {
	PluginTransactionRecoveryResult result;
	QDir directory(directoryPath);
	if (!directory.exists()) return result;
	const QFileInfoList entries = directory.entryInfoList(QDir::Files | QDir::Hidden | QDir::NoDotAndDotDot);
	for (const QFileInfo &entry : entries) {
		const QString name = entry.fileName();
		const int pendingMarker = name.lastIndexOf(QStringLiteral(".pending-"));
		const int backupMarker = name.lastIndexOf(QStringLiteral(".backup-"));
		if (pendingMarker > 0) {
			if (QFile::remove(entry.absoluteFilePath())) ++result.pendingRemoved;
			else result.errors.push_back(QObject::tr("Unable to remove abandoned staging file %1").arg(name));
			continue;
		}
		if (backupMarker <= 0) continue;
		const QString destinationPath = directory.absoluteFilePath(name.left(backupMarker));
		if (QFileInfo::exists(destinationPath) && !QFile::remove(destinationPath)) {
			result.errors.push_back(QObject::tr("Unable to replace unverified plugin while restoring %1").arg(name));
			continue;
		}
		if (QFile::rename(entry.absoluteFilePath(), destinationPath)) {
			++result.backupsRestored;
		} else {
			result.errors.push_back(QObject::tr("Unable to restore abandoned backup %1").arg(name));
		}
	}
	return result;
}
