// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ScreenShareHelperServer.h"

#include "ScreenShareIPC.h"
#include "ScreenShareWindowFollowPipeline.h"

#include <QtCore/QCommandLineOption>
#include <QtCore/QCommandLineParser>
#include <QtCore/QCoreApplication>
#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QJsonDocument>
#include <QtCore/QMutex>
#include <QtCore/QMutexLocker>
#include <QtCore/QThread>
#include <QtCore/QTextStream>

namespace {
	QString g_diagnosticsLogFilePath;
	QMutex g_diagnosticsLogMutex;
	QtMessageHandler g_previousMessageHandler = nullptr;
	constexpr qint64 DIAGNOSTICS_LOG_MAX_SIZE_BYTES = 5 * 1024 * 1024;
	constexpr int DIAGNOSTICS_LOG_MAX_FILES = 3;

	QString messageTypeToken(QtMsgType type) {
		switch (type) {
			case QtDebugMsg:
				return QStringLiteral("DEBUG");
			case QtInfoMsg:
				return QStringLiteral("INFO");
			case QtWarningMsg:
				return QStringLiteral("WARN");
			case QtCriticalMsg:
				return QStringLiteral("ERROR");
			case QtFatalMsg:
				return QStringLiteral("FATAL");
		}

		return QStringLiteral("LOG");
	}

	void diagnosticsMessageHandler(QtMsgType type, const QMessageLogContext &context, const QString &msg) {
		Q_UNUSED(context);

		if (!g_diagnosticsLogFilePath.isEmpty()) {
			QMutexLocker locker(&g_diagnosticsLogMutex);
			const QFileInfo logInfo(g_diagnosticsLogFilePath);
			if (!logInfo.absoluteDir().exists()) {
				logInfo.absoluteDir().mkpath(QStringLiteral("."));
			}

			auto rotatedPathForIndex = [](const QString &path, const int index) {
				return QStringLiteral("%1.%2").arg(path).arg(index);
			};

			if (logInfo.exists() && logInfo.size() >= DIAGNOSTICS_LOG_MAX_SIZE_BYTES) {
				QFile::remove(rotatedPathForIndex(g_diagnosticsLogFilePath, DIAGNOSTICS_LOG_MAX_FILES));
				for (int index = DIAGNOSTICS_LOG_MAX_FILES - 1; index >= 1; --index) {
					const QString sourcePath = rotatedPathForIndex(g_diagnosticsLogFilePath, index);
					const QString destinationPath = rotatedPathForIndex(g_diagnosticsLogFilePath, index + 1);
					if (QFileInfo::exists(sourcePath)) {
						QFile::remove(destinationPath);
						QFile::rename(sourcePath, destinationPath);
					}
				}

				const QString firstRotationPath = rotatedPathForIndex(g_diagnosticsLogFilePath, 1);
				QFile::remove(firstRotationPath);
				QFile::rename(g_diagnosticsLogFilePath, firstRotationPath);
			}

			QFile file(g_diagnosticsLogFilePath);
			if (file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text)) {
				QTextStream stream(&file);
				stream << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << ' '
					   << "[pid=" << QCoreApplication::applicationPid() << "] "
					   << "[tid=0x" << QString::number(reinterpret_cast< quintptr >(QThread::currentThreadId()), 16) << "] "
					   << '[' << messageTypeToken(type) << "] " << msg << '\n';
				stream.flush();
			}
		}

		if (g_previousMessageHandler) {
			g_previousMessageHandler(type, context, msg);
		}
	}

	void installDiagnosticsLogging(const QString &logFilePath) {
		g_diagnosticsLogFilePath = logFilePath.trimmed();
		if (g_diagnosticsLogFilePath.isEmpty()) {
			return;
		}

		const QFileInfo logInfo(g_diagnosticsLogFilePath);
		if (!logInfo.absoluteDir().exists()) {
			logInfo.absoluteDir().mkpath(QStringLiteral("."));
		}

		g_previousMessageHandler = qInstallMessageHandler(diagnosticsMessageHandler);
		qInfo().noquote() << QStringLiteral("ScreenShareHelper: diagnostics log file %1").arg(g_diagnosticsLogFilePath);
	}
} // namespace

int main(int argc, char **argv) {
	QCoreApplication app(argc, argv);
	app.setApplicationName(QStringLiteral("mumble-screen-helper"));

	QCommandLineParser parser;
	parser.setApplicationDescription(QStringLiteral("Mumble screen-share helper"));
	QCommandLineOption helpOption(QStringList{ QStringLiteral("?"), QStringLiteral("h"), QStringLiteral("help") },
								  QStringLiteral("Displays help on commandline options."));
	QCommandLineOption helpAllOption(QStringList{ QStringLiteral("help-all") },
									 QStringLiteral("Displays help, including generic Qt options."));
	QCommandLineOption diagnosticsLogFileOption(QStringList{ QStringLiteral("diagnostics-log-file") },
												QStringLiteral("Write helper diagnostics to the given file."),
												QStringLiteral("path"));
	QCommandLineOption socketNameOption(QStringList{ QStringLiteral("socket-name") },
										QStringLiteral("Listen on the given local socket base name."),
										QStringLiteral("name"));
	QCommandLineOption printCapabilitiesOption(QStringList{ QStringLiteral("print-capabilities-json") },
											   QStringLiteral("Print the helper capability payload as JSON and exit."));
	QCommandLineOption selfTestOption(QStringList{ QStringLiteral("self-test") },
									  QStringLiteral("Run a local publish/view helper self-test and exit."));
	// Internal sub-mode: run a capture pipeline in-process and keep its monitor crop following the
	// given window. Not part of the public interface; spawned by the helper for browser captures.
	QCommandLineOption windowFollowOption(QStringList{ QStringLiteral("internal-gst-window-follow") },
										  QStringLiteral("Internal: run a window-following capture pipeline and exit."));
	QCommandLineOption windowFollowSourceNameOption(QStringList{ QStringLiteral("source-name") },
													QStringLiteral("Internal: name of the capture element to retarget."),
													QStringLiteral("name"));
	QCommandLineOption windowFollowHandleOption(QStringList{ QStringLiteral("window-handle") },
												QStringLiteral("Internal: native handle of the window to follow."),
												QStringLiteral("handle"));
	QCommandLineOption windowFollowGstBinDirOption(QStringList{ QStringLiteral("gst-bin-dir") },
												   QStringLiteral("Internal: bundled GStreamer bin directory."),
												   QStringLiteral("dir"));
	parser.addOption(helpOption);
	parser.addOption(helpAllOption);
	parser.addOption(diagnosticsLogFileOption);
	parser.addOption(socketNameOption);
	parser.addOption(printCapabilitiesOption);
	parser.addOption(selfTestOption);
	parser.addOption(windowFollowOption);
	parser.addOption(windowFollowSourceNameOption);
	parser.addOption(windowFollowHandleOption);
	parser.addOption(windowFollowGstBinDirOption);
	if (!parser.parse(app.arguments())) {
		QTextStream(stderr) << parser.errorText() << Qt::endl << parser.helpText() << Qt::endl;
		return 1;
	}
	if (parser.isSet(helpOption) || parser.isSet(helpAllOption)) {
		QTextStream(stdout) << parser.helpText() << Qt::endl;
		return 0;
	}

	if (parser.isSet(diagnosticsLogFileOption)) {
		installDiagnosticsLogging(parser.value(diagnosticsLogFileOption));
	}

	if (parser.isSet(windowFollowOption)) {
		ScreenShareWindowFollow::Options options;
		options.pipelineTokens = parser.positionalArguments();
		options.sourceName     = parser.value(windowFollowSourceNameOption);
		options.gstBinDir      = parser.value(windowFollowGstBinDirOption);
		bool handleOk          = false;
		options.windowHandle   = parser.value(windowFollowHandleOption).toULongLong(&handleOk);
		if (!handleOk) {
			options.windowHandle = 0;
		}
		return ScreenShareWindowFollow::run(options);
	}

	ScreenShareHelperServer helperServer;
	if (parser.isSet(printCapabilitiesOption)) {
		QTextStream(stdout)
			<< QJsonDocument(Mumble::ScreenShare::IPC::makeSuccessReply(helperServer.capabilityPayload()))
				   .toJson(QJsonDocument::Compact)
			<< Qt::endl;
		return 0;
	}
	if (parser.isSet(selfTestOption)) {
		const QJsonObject reply = helperServer.runSelfTest();
		QTextStream(stdout) << QJsonDocument(reply).toJson(QJsonDocument::Compact) << Qt::endl;
		return Mumble::ScreenShare::IPC::replySucceeded(reply) ? 0 : 1;
	}

	QString errorMessage;
	if (!helperServer.start(parser.value(socketNameOption), &errorMessage)) {
		qCritical().noquote() << QStringLiteral("ScreenShareHelper: failed to start: %1").arg(errorMessage);
		return 1;
	}

	return app.exec();
}
