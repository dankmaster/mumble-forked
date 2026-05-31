// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "CrashReporter.h"

#include "FeedbackReport.h"
#include "OSInfo.h"
#include "Global.h"
#include "UiTheme.h"
#include "Version.h"

#include <QtCore/QDateTime>
#include <QtCore/QDir>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>
#include <QtCore/QUrl>
#include <QtCore/QUrlQuery>
#include <QtCore/QtGlobal>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtGui/QPalette>
#include <QtNetwork/QHostAddress>
#include <QtWidgets/QApplication>
#include <QtWidgets/QDialog>
#include <QtWidgets/QFrame>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

namespace {
	struct CrashDialogPalette {
		QColor window;
		QColor panel;
		QColor panelSoft;
		QColor border;
		QColor text;
		QColor muted;
		QColor accent;
		QColor accentHover;
		QColor onAccent;
	};

	struct CrashIssueDraft {
		QString issueTitle;
		QString issueBody;
		QString clipboardText;
		QUrl fallbackUrl;
	};

	QColor readableTextFor(const QColor &background) {
		return background.lightness() > 145 ? QColor(QStringLiteral("#081210")) : QColor(QStringLiteral("#ffffff"));
	}

	QString crashBuildArchitecture() {
#ifdef MUMBLE_TARGET_ARCH
		return QString::fromUtf8(MUMBLE_TARGET_ARCH);
#else
		return OSInfo::getArchitecture(true);
#endif
	}

	QString humanCrashByteCount(const qint64 bytes) {
		if (bytes < 1024) {
			return CrashReporter::tr("%1 B").arg(bytes);
		}

		const double kib = static_cast< double >(bytes) / 1024.0;
		if (kib < 1024.0) {
			return CrashReporter::tr("%1 KiB").arg(kib, 0, 'f', kib >= 10.0 ? 0 : 1);
		}

		return CrashReporter::tr("%1 MiB").arg(kib / 1024.0, 0, 'f', 1);
	}

	QString yesNo(const bool value) {
		return value ? CrashReporter::tr("yes") : CrashReporter::tr("no");
	}

	QString crashDiagnosticsText(const QString &archiveDirPath) {
		const QDir archiveDir(archiveDirPath);
		const QFileInfo dumpInfo(archiveDir.filePath(QStringLiteral("mumble.dmp")));
		const QFileInfo metadataInfo(archiveDir.filePath(QStringLiteral("metadata.txt")));
		const bool hasLastServer = !Global::get().s.qsLastServer.trimmed().isEmpty();
		const bool hasLastUser   = !Global::get().s.qsUsername.trimmed().isEmpty();

		QString diagnostics;
		QTextStream stream(&diagnostics);
		stream << "Generated: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << "\n";
		stream << "Version: " << Version::getRelease() << "\n";
		stream << "Architecture: " << crashBuildArchitecture() << "\n";
		stream << "OS: " << OSInfo::getOSDisplayableVersion() << "\n";
		stream << "Qt: " << QString::fromLatin1(qVersion()) << "\n";
		stream << "Crash dump archived: " << yesNo(dumpInfo.exists() && dumpInfo.isFile()) << "\n";
		if (dumpInfo.exists() && dumpInfo.isFile()) {
			stream << "Crash dump size: " << humanCrashByteCount(dumpInfo.size()) << "\n";
		}
		stream << "Crash metadata archived: " << yesNo(metadataInfo.exists() && metadataInfo.isFile()) << "\n";
		stream << "Local archive path: omitted by client\n";
		stream << "Console log: omitted by client for crash privacy\n";
		stream << "Last server entry present locally: " << yesNo(hasLastServer) << "\n";
		stream << "Last username entry present locally: " << yesNo(hasLastUser) << "\n";
		stream << "Last server and username values: omitted by client\n";
		return diagnostics;
	}

	CrashIssueDraft buildCrashIssueDraft(const QString &archiveDirPath) {
		Mumble::Feedback::ReportFields fields;
		fields.kind = MumbleProto::FeedbackReportBug;
		fields.title = CrashReporter::tr("Crash archived on startup");
		fields.description =
			CrashReporter::tr("Mumble archived a crash dump on startup after the previous session ended unexpectedly.\n\n"
							  "Please describe what you were doing before the crash.");
		fields.reproductionSteps =
			CrashReporter::tr("1. What were you doing before Mumble closed?\n"
							  "2. Did the crash happen again after restart?\n"
							  "3. If you are comfortable sharing crash data, attach the local `mumble.dmp` from the "
							  "crash archive folder.");
		fields.pastedEvidence =
			CrashReporter::tr("The client saved `mumble.dmp` and `metadata.txt` in a local crash archive. "
							  "These files are not uploaded automatically. Attach `mumble.dmp` manually only if you "
							  "choose to share crash data.");
		fields.diagnosticsIncluded = true;
		fields.diagnostics         = crashDiagnosticsText(archiveDirPath);
		fields.clientRelease       = Version::getRelease();
		fields.clientArch          = crashBuildArchitecture();
		fields.clientOS            = OSInfo::getOSDisplayableVersion();
		fields.clientQt            = QString::fromLatin1(qVersion());
		fields.serverCapabilitySummary =
			CrashReporter::tr("not included in crash reports; saved last-connection values stay local");

		CrashIssueDraft draft;
		draft.issueTitle   = Mumble::Feedback::issueTitle(fields);
		draft.issueBody    = Mumble::Feedback::issueBody(fields, Mumble::Feedback::DEFAULT_MAX_BODY_BYTES,
														 Mumble::Feedback::DEFAULT_MAX_LOG_BYTES);
		draft.clipboardText = CrashReporter::tr("Title: %1\n\n%2").arg(draft.issueTitle, draft.issueBody);

		QUrlQuery query;
		query.addQueryItem(QStringLiteral("title"), draft.issueTitle);
		query.addQueryItem(QStringLiteral("body"), draft.issueBody);
		query.addQueryItem(QStringLiteral("labels"), QStringLiteral("triage,in-app-feedback,bug,crash"));
		draft.fallbackUrl = QUrl(QStringLiteral("https://github.com/dankmaster/mumble-forked/issues/new"));
		draft.fallbackUrl.setQuery(query);
		return draft;
	}

	void writeCrashIssueDraft(const QString &archiveDirPath, const CrashIssueDraft &draft) {
		QFile file(QDir(archiveDirPath).filePath(QStringLiteral("github-issue-draft.md")));
		if (!file.open(QIODevice::WriteOnly | QIODevice::Text)) {
			qWarning("CrashReporter: Unable to write GitHub issue draft");
			return;
		}

		QTextStream stream(&file);
		stream << "# " << draft.issueTitle << "\n\n" << draft.issueBody << "\n";
	}

	CrashDialogPalette crashDialogPalette() {
		const QPalette palette = qApp ? qApp->palette() : QPalette();
		CrashDialogPalette colors;
		colors.window      = palette.color(QPalette::Window);
		colors.panel       = palette.color(QPalette::Base);
		colors.panelSoft   = palette.color(QPalette::AlternateBase);
		colors.border      = palette.color(QPalette::Mid);
		colors.text        = palette.color(QPalette::WindowText);
		colors.muted       = palette.color(QPalette::Disabled, QPalette::WindowText);
		colors.accent      = palette.color(QPalette::Highlight);
		colors.accentHover = colors.accent.lighter(112);
		colors.onAccent    = palette.color(QPalette::HighlightedText);

		if (const std::optional< UiThemeTokens > tokens = activeUiThemeTokens(); tokens) {
			colors.window      = tokens->mantle;
			colors.panel       = tokens->base;
			colors.panelSoft   = tokens->surface0;
			colors.border      = tokens->surface1;
			colors.text        = tokens->text;
			colors.muted       = tokens->textMuted;
			colors.accent      = tokens->accent;
			colors.accentHover = tokens->accentHover.isValid() ? tokens->accentHover : tokens->accent.lighter(112);
			colors.onAccent    = readableTextFor(tokens->accent);
		}

		return colors;
	}

	class CrashArchivedDialog final : public QDialog {
	public:
		explicit CrashArchivedDialog(const QString &archiveDirPath, const CrashIssueDraft &issueDraft) : QDialog(nullptr) {
			const CrashDialogPalette colors = crashDialogPalette();
			const QString nativePath        = QDir::toNativeSeparators(archiveDirPath);

			setObjectName(QStringLiteral("qdwCrashArchived"));
			setWindowTitle(CrashReporter::tr("Crash archived"));
			setModal(true);
			setMinimumWidth(600);
			setAttribute(Qt::WA_StyledBackground, true);
			setAttribute(Qt::WA_NoSystemBackground, false);
			setWindowFlag(Qt::WindowStaysOnTopHint, true);
			setAutoFillBackground(true);
			QPalette dialogPalette = palette();
			dialogPalette.setColor(QPalette::Window, colors.window);
			setPalette(dialogPalette);

			QVBoxLayout *layout = new QVBoxLayout(this);
			layout->setContentsMargins(18, 14, 18, 16);
			layout->setSpacing(16);

			QHBoxLayout *headerLayout = new QHBoxLayout();
			headerLayout->setSpacing(14);

			QLabel *icon = new QLabel(QStringLiteral("i"), this);
			icon->setObjectName(QStringLiteral("qlCrashArchivedIcon"));
			icon->setAlignment(Qt::AlignCenter);
			icon->setFixedSize(38, 38);

			QVBoxLayout *copyLayout = new QVBoxLayout();
			copyLayout->setContentsMargins(0, 0, 0, 0);
			copyLayout->setSpacing(5);

			QLabel *title = new QLabel(CrashReporter::tr("Crash data archived"), this);
			title->setObjectName(QStringLiteral("qlCrashArchivedTitle"));

			QLabel *subtitle = new QLabel(
				CrashReporter::tr("The dump was saved locally for troubleshooting. Nothing was uploaded."),
				this);
			subtitle->setObjectName(QStringLiteral("qlCrashArchivedSubtitle"));
			subtitle->setWordWrap(true);

			copyLayout->addWidget(title);
			copyLayout->addWidget(subtitle);
			headerLayout->addWidget(icon, 0, Qt::AlignTop);
			headerLayout->addLayout(copyLayout, 1);
			layout->addLayout(headerLayout);

			QFrame *pathCard = new QFrame(this);
			pathCard->setObjectName(QStringLiteral("qfCrashArchivedPathCard"));
			QVBoxLayout *pathLayout = new QVBoxLayout(pathCard);
			pathLayout->setContentsMargins(14, 12, 14, 12);
			pathLayout->setSpacing(7);

			QLabel *pathEyebrow = new QLabel(CrashReporter::tr("Local archive"), pathCard);
			pathEyebrow->setObjectName(QStringLiteral("qlCrashArchivedEyebrow"));

			QLabel *pathLabel = new QLabel(nativePath, pathCard);
			pathLabel->setObjectName(QStringLiteral("qlCrashArchivedPath"));
			pathLabel->setTextInteractionFlags(Qt::TextSelectableByMouse | Qt::TextSelectableByKeyboard);
			pathLabel->setWordWrap(true);

			pathLayout->addWidget(pathEyebrow);
			pathLayout->addWidget(pathLabel);
			layout->addWidget(pathCard);

			QLabel *privacyNote = new QLabel(
				CrashReporter::tr("GitHub drafts omit server address, username, room names, logs, and local paths. "
								  "Attach the dump manually only if you choose."),
				this);
			privacyNote->setObjectName(QStringLiteral("qlCrashArchivedPrivacy"));
			privacyNote->setWordWrap(true);
			layout->addWidget(privacyNote);

			QHBoxLayout *footerLayout = new QHBoxLayout();
			footerLayout->setContentsMargins(0, 2, 0, 0);
			footerLayout->setSpacing(8);
			footerLayout->addStretch(1);

			QPushButton *copyButton = new QPushButton(CrashReporter::tr("Copy path"), this);
			copyButton->setObjectName(QStringLiteral("qpbCrashArchivedSecondary"));
			copyButton->setAutoDefault(false);
			QPushButton *copyReportButton = new QPushButton(CrashReporter::tr("Copy report"), this);
			copyReportButton->setObjectName(QStringLiteral("qpbCrashArchivedSecondary"));
			copyReportButton->setAutoDefault(false);
			QPushButton *openButton = new QPushButton(CrashReporter::tr("Open folder"), this);
			openButton->setObjectName(QStringLiteral("qpbCrashArchivedSecondary"));
			openButton->setAutoDefault(false);
			QPushButton *reportButton = new QPushButton(CrashReporter::tr("Open GitHub"), this);
			reportButton->setObjectName(QStringLiteral("qpbCrashArchivedPrimary"));
			reportButton->setAutoDefault(false);
			QPushButton *okButton = new QPushButton(CrashReporter::tr("OK"), this);
			okButton->setObjectName(QStringLiteral("qpbCrashArchivedSecondary"));
			okButton->setAutoDefault(false);
			okButton->setDefault(false);

			footerLayout->addWidget(copyButton);
			footerLayout->addWidget(copyReportButton);
			footerLayout->addWidget(openButton);
			footerLayout->addWidget(reportButton);
			footerLayout->addWidget(okButton);
			layout->addLayout(footerLayout);

			QObject::connect(copyButton, &QPushButton::clicked, this, [nativePath]() {
				if (QApplication::clipboard()) {
					QApplication::clipboard()->setText(nativePath);
				}
			});
			QObject::connect(copyReportButton, &QPushButton::clicked, this, [issueDraft, privacyNote]() {
				if (QApplication::clipboard()) {
					QApplication::clipboard()->setText(issueDraft.clipboardText);
				}
				privacyNote->setText(CrashReporter::tr(
					"Sanitized report markdown copied. Server address, username, room names, logs, and local paths "
					"are still omitted."));
			});
			QObject::connect(openButton, &QPushButton::clicked, this, [archiveDirPath]() {
				QDesktopServices::openUrl(QUrl::fromLocalFile(archiveDirPath));
			});
			QObject::connect(reportButton, &QPushButton::clicked, this, [issueDraft, privacyNote]() {
				if (QApplication::clipboard()) {
					QApplication::clipboard()->setText(issueDraft.clipboardText);
				}
				QDesktopServices::openUrl(issueDraft.fallbackUrl);
				privacyNote->setText(CrashReporter::tr(
					"GitHub issue draft opened and sanitized markdown copied. Attach the local dump only if you "
					"choose to share crash data."));
			});
			QObject::connect(okButton, &QPushButton::clicked, this, &QDialog::accept);

			setStyleSheet(
				QString::fromLatin1(
					"QDialog#qdwCrashArchived {"
					" background-color:%1;"
					" color:%5;"
					"}"
					"QLabel#qlCrashArchivedIcon {"
					" border-radius:19px;"
					" background:%7;"
					" color:%9;"
					" font-size:20px;"
					" font-weight:700;"
					"}"
					"QLabel#qlCrashArchivedTitle {"
					" color:%5;"
					" font-size:18px;"
					" font-weight:700;"
					"}"
					"QLabel#qlCrashArchivedSubtitle {"
					" color:%6;"
					" font-size:13px;"
					"}"
					"QLabel#qlCrashArchivedPrivacy {"
					" color:%6;"
					" font-size:12px;"
					"}"
					"QFrame#qfCrashArchivedPathCard {"
					" border:1px solid %4;"
					" border-radius:10px;"
					" background:%2;"
					"}"
					"QLabel#qlCrashArchivedEyebrow {"
					" color:%6;"
					" font-size:11px;"
					" font-weight:700;"
					"}"
					"QLabel#qlCrashArchivedPath {"
					" color:%5;"
					" font-size:13px;"
					"}"
					"QPushButton {"
					" min-width:82px;"
					" min-height:30px;"
					" padding:0 12px;"
					" border-radius:7px;"
					" font-weight:600;"
					"}"
					"QPushButton#qpbCrashArchivedSecondary {"
					" border:1px solid %4;"
					" background:%3;"
					" color:%5;"
					"}"
					"QPushButton#qpbCrashArchivedSecondary:hover {"
					" background:%2;"
					"}"
					"QPushButton#qpbCrashArchivedPrimary {"
					" border:1px solid %8;"
					" background:%7;"
					" color:%9;"
					"}"
					"QPushButton#qpbCrashArchivedPrimary:hover {"
					" background:%8;"
					"}")
					.arg(uiThemeQssColor(colors.window), uiThemeQssColor(colors.panel),
						 uiThemeQssColor(colors.panelSoft), uiThemeQssColor(colors.border),
						 uiThemeQssColor(colors.text), uiThemeQssColor(colors.muted),
						 uiThemeQssColor(colors.accent), uiThemeQssColor(colors.accentHover),
						 uiThemeQssColor(colors.onAccent)));

			applyUiThemeNativeTitleBar(this);
		}
	};

	void showCrashArchivedDialog(const QString &archiveDirPath) {
		const CrashIssueDraft issueDraft = buildCrashIssueDraft(archiveDirPath);
		writeCrashIssueDraft(archiveDirPath, issueDraft);
		CrashArchivedDialog dialog(archiveDirPath, issueDraft);
		dialog.exec();
	}
}

CrashReporter::CrashReporter(QWidget *p) : QDialog(p) {
}

CrashReporter::~CrashReporter() {
}

void CrashReporter::run() {
	QByteArray qbaDumpContents;
	QFile qfCrashDump(Global::get().qdBasePath.filePath(QLatin1String("mumble.dmp")));
	if (!qfCrashDump.exists())
		return;

	if (!qfCrashDump.open(QIODevice::ReadOnly)) {
		qWarning("CrashReporter: Failed to open crash dump file: %s", qUtf8Printable(qfCrashDump.errorString()));
		return;
	}

#if defined(Q_OS_WIN)
	/* On Windows, the .dmp file is a real minidump. */

	if (qfCrashDump.peek(4) != "MDMP")
		return;
	qbaDumpContents = qfCrashDump.readAll();

#elif defined(Q_OS_MAC)
	/*
	 * On OSX, the .dmp file is simply a dummy file that we
	 * use to find the *real* crash dump, made by the OSX
	 * built in crash reporter.
	 */
	QFileInfo qfiDump(qfCrashDump);
	QDateTime qdtModification = qfiDump.lastModified();

	/* Find the real crash report. */
	QDir qdCrashReports(QDir::home().absolutePath() + QLatin1String("/Library/Logs/DiagnosticReports/"));
	if (!qdCrashReports.exists()) {
		qdCrashReports.setPath(QDir::home().absolutePath() + QLatin1String("/Library/Logs/CrashReporter/"));
	}

	QStringList qslFilters;
	qslFilters << QString::fromLatin1("Mumble_*.crash");
	qdCrashReports.setNameFilters(qslFilters);
	qdCrashReports.setSorting(QDir::Time);
	QFileInfoList qfilEntries = qdCrashReports.entryInfoList();

	/*
	 * Figure out if our delta is sufficiently close to the Apple crash dump, or
	 * if something weird happened.
	 */
	for (const QFileInfo &fi : qfilEntries) {
		qint64 delta = qAbs< qint64 >(qdtModification.secsTo(fi.lastModified()));
		if (delta < 8) {
			QFile f(fi.absoluteFilePath());
			if (!f.open(QIODevice::ReadOnly)) {
				qWarning("CrashReporter: Failed to open crash report file: %s", qUtf8Printable(f.errorString()));
				continue;
			}
			qbaDumpContents = f.readAll();
			break;
		}
	}
#endif

	QString details;
#ifdef Q_OS_WIN
	details = QLatin1String("Windows minidump archived locally. No crash data was uploaded.");
#endif

	if (qbaDumpContents.isEmpty()) {
		qWarning("CrashReporter: Empty crash dump file, not reporting.");
		return;
	}

	const QString timestamp = QDateTime::currentDateTime().toString(QLatin1String("yyyyMMdd-HHmmss"));
	QDir crashRoot(Global::get().qdBasePath.filePath(QLatin1String("crash-reports")));
	if (!crashRoot.mkpath(QLatin1String("."))) {
		qWarning("CrashReporter: Unable to create crash report directory");
		return;
	}

	const QString archiveDirPath = crashRoot.filePath(timestamp);
	if (!crashRoot.mkpath(timestamp)) {
		qWarning("CrashReporter: Unable to create crash report archive");
		return;
	}

	QFile archivedDump(QDir(archiveDirPath).filePath(QLatin1String("mumble.dmp")));
	if (!archivedDump.open(QIODevice::WriteOnly)) {
		qWarning("CrashReporter: Unable to write archived dump");
		return;
	}
	archivedDump.write(qbaDumpContents);
	archivedDump.close();

	QFile metadata(QDir(archiveDirPath).filePath(QLatin1String("metadata.txt")));
	if (metadata.open(QIODevice::WriteOnly | QIODevice::Text)) {
		QStringList lines;
		lines << QString::fromLatin1("timestamp=%1").arg(QDateTime::currentDateTime().toString(Qt::ISODate));
		lines << QString::fromLatin1("version=%1").arg(Version::getRelease());
		lines << QString::fromLatin1("os=%1 %2").arg(OSInfo::getOS(), OSInfo::getOSVersion());
		lines << QString::fromLatin1("app=%1").arg(QFileInfo(qApp->applicationFilePath()).fileName());
		if (!details.isEmpty()) {
			lines << QString();
			lines << details;
		}
		metadata.write(lines.join(QLatin1Char('\n')).toUtf8());
		metadata.close();
	}

	if (!qfCrashDump.remove())
		qWarning("CrashReporeter: Unable to remove crash file.");

	showCrashArchivedDialog(archiveDirPath);
}
