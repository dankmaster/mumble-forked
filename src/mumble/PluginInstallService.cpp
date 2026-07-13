// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "PluginInstallService.h"

#include "Global.h"
#include "PluginManager.h"
#include "PluginManifest.h"
#include "PluginUpdatePreparation.h"
#include "QtUtils.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QCryptographicHash>
#include <QtCore/QLibrary>

#include <Poco/Exception.h>
#include <Poco/FileStream.h>
#include <Poco/Zip/ZipArchive.h>
#include <Poco/Zip/ZipStream.h>

#include <fstream>
#include <array>
#include <exception>

namespace {
	constexpr Poco::UInt64 MaxManifestBytes = 1024 * 1024;
	constexpr qint64 MaxArchiveBytes = 160LL * 1024 * 1024;
	constexpr Poco::UInt64 MaxPluginCompressedBytes = 128ULL * 1024 * 1024;
	constexpr Poco::UInt64 MaxPluginExtractedBytes = 256ULL * 1024 * 1024;
	QString versionLabel(const mumble_version_t version) {
		return version == MUMBLE_VERSION_UNKNOWN ? QObject::tr("Unknown") : static_cast< QString >(version);
	}
}

PluginInstallService::PluginInstallService(const QString &filePath)
	: PluginInstallService(prepare(filePath, installDirectory())) {}

PluginInstallService::PluginInstallService(PreparedPackage prepared)
	: m_archive(prepared.sourcePath), m_source(prepared.sourcePath), m_destination(prepared.destinationPath),
	  m_tempDirectory(std::move(prepared.temporaryDirectory)), m_copySource(true) {
	inspectPrepared();
}

PluginInstallException::PluginInstallException(const QString &message) : m_message(message) {
}

QString PluginInstallException::getMessage() const {
	return m_message;
}

PluginInstallService::~PluginInstallService() = default;

const PluginInstallService::Inspection &PluginInstallService::inspection() const {
	return m_inspection;
}

bool PluginInstallService::canBePluginFile(const QFileInfo &fileInfo) noexcept {
	return fileInfo.isFile()
		   && (fileInfo.suffix().compare(QStringLiteral("mumble_plugin"), Qt::CaseInsensitive) == 0
			   || QLibrary::isLibrary(fileInfo.fileName()));
}

QString PluginInstallService::installDirectory() {
	return Global::get().qdBasePath.absoluteFilePath(QStringLiteral("Plugins"));
}

PluginInstallService::PreparedPackage PluginInstallService::prepare(const QString &filePath,
												 const QString &destinationDirectory,
												 const std::atomic< bool > *cancelled,
												 const QString &boundInstalledPath) {
	const QFileInfo archiveInfo(filePath);
	if (!canBePluginFile(archiveInfo)) {
		throw PluginInstallException(QObject::tr("The file \"%1\" is not a valid plugin file!")
									 .arg(archiveInfo.fileName()));
	}
	if (archiveInfo.size() > MaxArchiveBytes)
		throw PluginInstallException(QObject::tr("The plugin package exceeds the archive size limit."));
	if (cancelled && cancelled->load()) throw PluginInstallException(QObject::tr("Plugin installation cancelled."));
	PreparedPackage prepared;
	if (QLibrary::isLibrary(archiveInfo.fileName())) {
		prepared.sourcePath = archiveInfo.absoluteFilePath();
	} else {
		prepared.temporaryDirectory = std::make_shared< QTemporaryDir >();
		if (!prepared.temporaryDirectory->isValid()) {
			throw PluginInstallException(QObject::tr("Unable to create a temporary plugin inspection directory."));
		}
		try {
			Poco::FileInputStream zipInput(archiveInfo.filePath().toStdString());
			Poco::Zip::ZipArchive archive(zipInput);
			const auto manifestIt = archive.findHeader("manifest.xml");
			if (manifestIt == archive.headerEnd()) {
				throw PluginInstallException(QObject::tr("Unable to locate the plugin manifest (manifest.xml)"));
			}
			if (manifestIt->second.getUncompressedSize() > MaxManifestBytes
				|| manifestIt->second.getCompressedSize() > MaxManifestBytes)
				throw PluginInstallException(QObject::tr("The plugin manifest is too large."));

			zipInput.clear();
			Poco::Zip::ZipInputStream manifestStream(zipInput, manifestIt->second);
			PluginManifest manifest;
			try {
				manifest.parse(manifestStream);
			} catch (const PluginManifestException &exception) {
				throw PluginInstallException(
					QObject::tr("Error while processing manifest: %1").arg(QString::fromUtf8(exception.what())));
			}
			if (!manifest.specifiesPluginPath(MUMBLE_TARGET_OS, MUMBLE_TARGET_ARCH)) {
				throw PluginInstallException(
					QObject::tr("Unable to find plugin for the current OS (\"%1\") and architecture (\"%2\")")
						.arg(QString::fromUtf8(MUMBLE_TARGET_OS), QString::fromUtf8(MUMBLE_TARGET_ARCH)));
			}

			const std::string pluginPath = manifest.getPluginPath(MUMBLE_TARGET_OS, MUMBLE_TARGET_ARCH);
			const auto pluginIt          = archive.findHeader(pluginPath);
			if (pluginIt == archive.headerEnd()) {
				throw PluginInstallException(QObject::tr("Unable to locate plugin library specified in manifest (\"%1\")")
											 .arg(QString::fromStdString(pluginPath)));
			}
			if (pluginIt->second.getCompressedSize() > MaxPluginCompressedBytes
				|| pluginIt->second.getUncompressedSize() > MaxPluginExtractedBytes)
				throw PluginInstallException(QObject::tr("The plugin package exceeds the extraction size limit."));

			const QString extractedPath = QDir(prepared.temporaryDirectory->path())
										  .absoluteFilePath(QFileInfo(QString::fromStdString(pluginPath)).fileName());
			zipInput.clear();
			Poco::Zip::ZipInputStream pluginStream(zipInput, pluginIt->second);
			std::ofstream output(Mumble::QtUtils::qstring_to_path(extractedPath), std::ios::out | std::ios::binary);
			std::array< char, 1024 * 1024 > buffer;
			Poco::UInt64 extractedBytes = 0;
			while (pluginStream.good()) {
				if (cancelled && cancelled->load())
					throw PluginInstallException(QObject::tr("Plugin installation cancelled."));
				pluginStream.read(buffer.data(), static_cast< std::streamsize >(buffer.size()));
				const std::streamsize count = pluginStream.gcount();
				if (count <= 0) break;
				extractedBytes += static_cast< Poco::UInt64 >(count);
				if (extractedBytes > MaxPluginExtractedBytes)
					throw PluginInstallException(QObject::tr("The plugin package exceeds the extraction size limit."));
				output.write(buffer.data(), count);
				if (!output.good()) throw PluginInstallException(QObject::tr("Unable to write the extracted plugin."));
			}
			output.close();
			prepared.sourcePath = extractedPath;
		} catch (const PluginInstallException &) {
			throw;
		} catch (const Poco::Exception &exception) {
			throw PluginInstallException(
				QObject::tr("Failed to process zip archive: %1").arg(QString::fromStdString(exception.message())));
		}
	}
	if (cancelled && cancelled->load()) throw PluginInstallException(QObject::tr("Plugin installation cancelled."));
	QFile source(prepared.sourcePath);
	if (!source.open(QIODevice::ReadOnly))
		throw PluginInstallException(QObject::tr("Unable to read the prepared plugin file."));
	QCryptographicHash hash(QCryptographicHash::Sha256);
	while (!source.atEnd()) {
		if (cancelled && cancelled->load()) throw PluginInstallException(QObject::tr("Plugin installation cancelled."));
		hash.addData(source.read(1024 * 1024));
	}
	prepared.sha256 = hash.result();
	if (!QDir(destinationDirectory).exists() && !QDir().mkpath(destinationDirectory))
		throw PluginInstallException(QObject::tr("Unable to create plugin directory \"%1\"").arg(destinationDirectory));
	if (boundInstalledPath.trimmed().isEmpty()) {
		prepared.destinationPath = QDir(destinationDirectory).absoluteFilePath(QFileInfo(prepared.sourcePath).fileName());
	} else {
		prepared.destinationPath = boundPluginUpdateDestination(destinationDirectory, boundInstalledPath);
		if (prepared.destinationPath.isEmpty()) {
			throw PluginInstallException(QObject::tr("The checked plugin update destination is invalid."));
		}
	}
	return prepared;
}

void PluginInstallService::inspectPrepared() {
	try {
		m_plugin.reset(Plugin::createNew< Plugin >(m_source.absoluteFilePath()));
	} catch (const PluginError &) {
		throw PluginInstallException(QObject::tr("Unable to load plugin \"%1\" - check the plugin interface!")
									 .arg(m_source.fileName()));
	}

	try {
		m_inspection.name        = m_plugin->getName();
		m_inspection.version     = versionLabel(m_plugin->getVersion());
		m_inspection.apiVersion  = versionLabel(m_plugin->getAPIVersion());
		m_inspection.author      = m_plugin->getAuthor();
		m_inspection.description = m_plugin->getDescription();
	} catch (const std::exception &exception) {
		throw PluginInstallException(QObject::tr("Unable to inspect plugin metadata: %1")
			.arg(QString::fromUtf8(exception.what())));
	} catch (...) {
		throw PluginInstallException(QObject::tr("Unable to inspect plugin metadata."));
	}
	m_inspection.destinationPath = m_destination.absoluteFilePath();
	m_inspection.overwriteRequired = m_destination.exists()
		&& !pluginUpdateDestinationMatchesInstalledPath(m_source.absoluteFilePath(), m_destination.absoluteFilePath());

	if (m_inspection.overwriteRequired && Global::get().pluginManager) {
		for (const PluginDescriptor &existing : Global::get().pluginManager->pluginDescriptors()) {
			if (pluginUpdateDestinationMatchesInstalledPath(existing.path, m_destination.absoluteFilePath())) {
				// Preserve the installed descriptor's exact spelling. This keeps Windows' case-sensitive settings key
				// stable even though filesystem path comparison is case-insensitive.
				m_destination = QFileInfo(existing.path);
				m_inspection.destinationPath = m_destination.absoluteFilePath();
				m_inspection.existingName    = existing.name;
				m_inspection.existingVersion = existing.version;
				break;
			}
		}
	}
	if (m_inspection.overwriteRequired) {
		bool hashOK = false;
		m_inspection.existingSha256 = pluginFileSha256(m_destination.absoluteFilePath(), &hashOK);
		if (!hashOK) {
			throw PluginInstallException(QObject::tr("Unable to fingerprint the installed plugin before replacement."));
		}
	}
}

bool PluginInstallService::install(const bool allowOverwrite) {
	if (!m_plugin) {
		throw PluginInstallException(QObject::tr("Plugin inspection is no longer valid."));
	}
	if (pluginUpdateDestinationMatchesInstalledPath(m_source.absoluteFilePath(), m_destination.absoluteFilePath())) {
		return false;
	}
	if (m_destination.exists() && !allowOverwrite) {
		return false;
	}
	if (m_destination.exists()) {
		if (Global::get().pluginManager) {
			for (const PluginDescriptor &existing : Global::get().pluginManager->pluginDescriptors()) {
				if (pluginUpdateDestinationMatchesInstalledPath(existing.path, m_destination.absoluteFilePath())) {
					Global::get().pluginManager->clearPlugin(existing.id);
					break;
				}
			}
		}
		m_plugin.reset();
		if (!QFile::remove(m_destination.absoluteFilePath())) {
			throw PluginInstallException(QObject::tr("Unable to delete old plugin at \"%1\"")
										 .arg(m_destination.absoluteFilePath()));
		}
	}

	m_plugin.reset();
	const bool installed = m_copySource ? QFile::copy(m_source.absoluteFilePath(), m_destination.absoluteFilePath())
									  : QFile::rename(m_source.absoluteFilePath(), m_destination.absoluteFilePath());
	if (!installed) {
		throw PluginInstallException(QObject::tr("Unable to install plugin at \"%1\"")
									 .arg(m_destination.absoluteFilePath()));
	}
	return true;
}
