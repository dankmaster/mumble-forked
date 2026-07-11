// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "PluginInstallService.h"

#include "Global.h"
#include "PluginManager.h"
#include "PluginManifest.h"
#include "QtUtils.h"

#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QLibrary>

#include <Poco/Exception.h>
#include <Poco/FileStream.h>
#include <Poco/StreamCopier.h>
#include <Poco/Zip/ZipArchive.h>
#include <Poco/Zip/ZipStream.h>

#include <fstream>

namespace {
	QString versionLabel(const mumble_version_t version) {
		return version == MUMBLE_VERSION_UNKNOWN ? QObject::tr("Unknown") : static_cast< QString >(version);
	}
}

PluginInstallService::PluginInstallService(const QString &filePath) : m_archive(filePath) {
	inspect();
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

void PluginInstallService::inspect() {
	if (!canBePluginFile(m_archive)) {
		throw PluginInstallException(QObject::tr("The file \"%1\" is not a valid plugin file!")
									 .arg(m_archive.fileName()));
	}

	if (QLibrary::isLibrary(m_archive.fileName())) {
		m_source     = m_archive;
		m_copySource = true;
	} else {
		if (!m_tempDirectory.isValid()) {
			throw PluginInstallException(QObject::tr("Unable to create a temporary plugin inspection directory."));
		}
		try {
			Poco::FileInputStream zipInput(m_archive.filePath().toStdString());
			Poco::Zip::ZipArchive archive(zipInput);
			const auto manifestIt = archive.findHeader("manifest.xml");
			if (manifestIt == archive.headerEnd()) {
				throw PluginInstallException(QObject::tr("Unable to locate the plugin manifest (manifest.xml)"));
			}

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

			const QString extractedPath =
				QDir(m_tempDirectory.path()).absoluteFilePath(QFileInfo(QString::fromStdString(pluginPath)).fileName());
			zipInput.clear();
			Poco::Zip::ZipInputStream pluginStream(zipInput, pluginIt->second);
			std::ofstream output(Mumble::QtUtils::qstring_to_path(extractedPath), std::ios::out | std::ios::binary);
			Poco::StreamCopier::copyStream(pluginStream, output);
			output.close();
			m_source = QFileInfo(extractedPath);
		} catch (const PluginInstallException &) {
			throw;
		} catch (const Poco::Exception &exception) {
			throw PluginInstallException(
				QObject::tr("Failed to process zip archive: %1").arg(QString::fromStdString(exception.message())));
		}
	}

	try {
		m_plugin.reset(Plugin::createNew< Plugin >(m_source.absoluteFilePath()));
	} catch (const PluginError &) {
		throw PluginInstallException(QObject::tr("Unable to load plugin \"%1\" - check the plugin interface!")
									 .arg(m_source.fileName()));
	}

	QDir installDir(installDirectory());
	if (!installDir.exists() && !QDir().mkpath(installDir.absolutePath())) {
		throw PluginInstallException(QObject::tr("Unable to create plugin directory \"%1\"")
									 .arg(installDir.absolutePath()));
	}
	m_destination = QFileInfo(installDir.absoluteFilePath(m_source.fileName()));
	m_inspection.name            = m_plugin->getName();
	m_inspection.version         = versionLabel(m_plugin->getVersion());
	m_inspection.apiVersion      = versionLabel(m_plugin->getAPIVersion());
	m_inspection.author          = m_plugin->getAuthor();
	m_inspection.description     = m_plugin->getDescription();
	m_inspection.destinationPath = m_destination.absoluteFilePath();
	m_inspection.overwriteRequired = m_destination.exists() && m_source.absoluteFilePath() != m_destination.absoluteFilePath();

	if (m_inspection.overwriteRequired && Global::get().pluginManager) {
		for (const const_plugin_ptr_t &existing : Global::get().pluginManager->getPlugins()) {
			if (existing && QFileInfo(existing->getFilePath()).absoluteFilePath() == m_destination.absoluteFilePath()) {
				m_inspection.existingName    = existing->getName();
				m_inspection.existingVersion = versionLabel(existing->getVersion());
				break;
			}
		}
	}
}

bool PluginInstallService::install(const bool allowOverwrite) {
	if (!m_plugin) {
		throw PluginInstallException(QObject::tr("Plugin inspection is no longer valid."));
	}
	if (m_source.absoluteFilePath() == m_destination.absoluteFilePath()) {
		return false;
	}
	if (m_destination.exists() && !allowOverwrite) {
		return false;
	}
	if (m_destination.exists()) {
		if (Global::get().pluginManager) {
			for (const const_plugin_ptr_t &existing : Global::get().pluginManager->getPlugins()) {
				if (existing && QFileInfo(existing->getFilePath()).absoluteFilePath() == m_destination.absoluteFilePath()) {
					Global::get().pluginManager->clearPlugin(existing->getID());
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
