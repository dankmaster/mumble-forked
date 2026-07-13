// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#ifndef MUMBLE_MUMBLE_PLUGININSTALLSERVICE_H_
#define MUMBLE_MUMBLE_PLUGININSTALLSERVICE_H_

#include "Plugin.h"

#include <QtCore/QFileInfo>
#include <QtCore/QException>
#include <QtCore/QString>
#include <QtCore/QTemporaryDir>

#include <memory>
#include <atomic>

class PluginInstallException : public QException {
public:
	explicit PluginInstallException(const QString &message);
	QString getMessage() const;

private:
	QString m_message;
};

class PluginInstallService {
public:
	struct Inspection {
		QString name;
		QString version;
		QString apiVersion;
		QString author;
		QString description;
		QString destinationPath;
		QString existingName;
		QString existingVersion;
		QByteArray existingSha256;
		bool overwriteRequired = false;
	};
	struct PreparedPackage {
		QString sourcePath;
		QString destinationPath;
		QByteArray sha256;
		QByteArray expectedDestinationSha256;
		std::shared_ptr< QTemporaryDir > temporaryDirectory;
	};

	explicit PluginInstallService(const QString &filePath);
	explicit PluginInstallService(PreparedPackage prepared);
	~PluginInstallService();

	const Inspection &inspection() const;
	bool install(bool allowOverwrite);

	static bool canBePluginFile(const QFileInfo &fileInfo) noexcept;
	static QString installDirectory();
	static PreparedPackage prepare(const QString &filePath, const QString &destinationDirectory,
								   const std::atomic< bool > *cancelled = nullptr,
								   const QString &boundInstalledPath = {});

private:
	QFileInfo m_archive;
	QFileInfo m_source;
	QFileInfo m_destination;
	std::shared_ptr< QTemporaryDir > m_tempDirectory;
	std::unique_ptr< Plugin > m_plugin;
	Inspection m_inspection;
	bool m_copySource = false;

	void inspectPrepared();
};

#endif // MUMBLE_MUMBLE_PLUGININSTALLSERVICE_H_
