// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "FeedbackDialog.h"

#include "FeedbackReport.h"
#include "Global.h"
#include <QtNetwork/QHostAddress>
#include "OSInfo.h"
#include "QtUtils.h"
#include "Version.h"

#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QTextStream>
#include <QtCore/QUuid>
#include <QtCore/QUrlQuery>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtWidgets/QApplication>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

namespace {
QString buildArchitecture() {
#ifdef MUMBLE_TARGET_ARCH
	return QString::fromUtf8(MUMBLE_TARGET_ARCH);
#else
	return OSInfo::getArchitecture(true);
#endif
}

QString kindLabelForUrl(const MumbleProto::FeedbackReportKind kind) {
	switch (kind) {
		case MumbleProto::FeedbackReportBug:
			return QStringLiteral("bug");
		case MumbleProto::FeedbackReportSuggestion:
			return QStringLiteral("enhancement");
		case MumbleProto::FeedbackReportSupport:
			return QStringLiteral("support");
	}

	return QStringLiteral("feedback");
}
} // namespace

FeedbackDialog::FeedbackDialog(const ServerCapability &capability, QWidget *parent)
	: QDialog(parent), m_capability(capability) {
	setWindowTitle(tr("Report feedback"));
	setModal(true);
	resize(760, 640);

	m_typeCombo = new QComboBox(this);
	m_typeCombo->addItem(tr("Bug"), static_cast< int >(MumbleProto::FeedbackReportBug));
	m_typeCombo->addItem(tr("Suggestion"), static_cast< int >(MumbleProto::FeedbackReportSuggestion));
	m_typeCombo->addItem(tr("Support"), static_cast< int >(MumbleProto::FeedbackReportSupport));

	m_titleEdit = new QLineEdit(this);
	m_titleEdit->setMaxLength(160);

	m_descriptionEdit = new QPlainTextEdit(this);
	m_descriptionEdit->setMinimumHeight(110);

	m_reproductionEdit = new QPlainTextEdit(this);
	m_reproductionEdit->setMinimumHeight(80);

	m_includeDiagnosticsBox = new QCheckBox(tr("Include diagnostics"), this);
	m_includeDiagnosticsBox->setChecked(true);

	m_captureButton = new QPushButton(tr("Start repro capture"), this);
	m_statusLabel   = new QLabel(this);
	m_statusLabel->setWordWrap(true);

	m_diagnosticsPreview = new QPlainTextEdit(this);
	m_diagnosticsPreview->setReadOnly(true);
	m_diagnosticsPreview->setMinimumHeight(150);

	QFormLayout *form = new QFormLayout();
	form->addRow(tr("Type"), m_typeCombo);
	form->addRow(tr("Title"), m_titleEdit);
	form->addRow(tr("Description"), m_descriptionEdit);
	form->addRow(tr("Steps to reproduce"), m_reproductionEdit);
	form->addRow(QString(), m_includeDiagnosticsBox);

	QHBoxLayout *captureLayout = new QHBoxLayout();
	captureLayout->addWidget(m_captureButton);
	captureLayout->addWidget(m_statusLabel, 1);

	QDialogButtonBox *buttons = new QDialogButtonBox(this);
	m_submitButton = buttons->addButton(tr("Submit"), QDialogButtonBox::AcceptRole);
	m_copyButton   = buttons->addButton(tr("Copy report"), QDialogButtonBox::ActionRole);
	m_openButton   = buttons->addButton(tr("Open GitHub"), QDialogButtonBox::ActionRole);
	m_closeButton  = buttons->addButton(QDialogButtonBox::Close);

	QVBoxLayout *layout = new QVBoxLayout(this);
	layout->addLayout(form);
	layout->addLayout(captureLayout);
	layout->addWidget(new QLabel(tr("Diagnostics preview"), this));
	layout->addWidget(m_diagnosticsPreview);
	layout->addWidget(buttons);

	connect(m_typeCombo, QOverload< int >::of(&QComboBox::currentIndexChanged), this,
			&FeedbackDialog::updateDiagnosticsDefault);
	connect(m_typeCombo, QOverload< int >::of(&QComboBox::currentIndexChanged), this,
			&FeedbackDialog::updateFormState);
	connect(m_titleEdit, &QLineEdit::textChanged, this, &FeedbackDialog::updateFormState);
	connect(m_descriptionEdit, &QPlainTextEdit::textChanged, this, &FeedbackDialog::updateFormState);
	connect(m_reproductionEdit, &QPlainTextEdit::textChanged, this, &FeedbackDialog::updateFormState);
	connect(m_includeDiagnosticsBox, &QCheckBox::toggled, this, &FeedbackDialog::updateFormState);
	connect(m_includeDiagnosticsBox, &QCheckBox::toggled, this, &FeedbackDialog::updateDiagnosticsPreview);
	connect(m_captureButton, &QPushButton::clicked, this, &FeedbackDialog::toggleCapture);
	connect(m_submitButton, &QPushButton::clicked, this, &FeedbackDialog::submitReport);
	connect(m_copyButton, &QPushButton::clicked, this, &FeedbackDialog::copyReport);
	connect(m_openButton, &QPushButton::clicked, this, &FeedbackDialog::openFallbackIssue);
	connect(m_closeButton, &QPushButton::clicked, this, &FeedbackDialog::reject);

	updateDiagnosticsPreview();
	updateFormState();
}

FeedbackDialog::PreparedReport FeedbackDialog::preparedReport() const {
	Mumble::Feedback::ReportFields fields;
	fields.kind                    = selectedKind();
	fields.title                   = m_titleEdit->text().trimmed();
	fields.description             = Mumble::Feedback::truncateUtf8Bytes(
		m_descriptionEdit->toPlainText().trimmed(), m_capability.maxBodyBytes, QStringLiteral("[description truncated]"));
	fields.reproductionSteps       = Mumble::Feedback::truncateUtf8Bytes(
		m_reproductionEdit->toPlainText().trimmed(), m_capability.maxBodyBytes, QStringLiteral("[steps truncated]"));
	fields.diagnosticsIncluded     = m_includeDiagnosticsBox->isChecked();
	fields.diagnostics             = fields.diagnosticsIncluded ? diagnosticsText() : QString();
	fields.clientRelease           = Version::getRelease();
	fields.clientArch              = buildArchitecture();
	fields.clientOS                = OSInfo::getOSDisplayableVersion();
	fields.clientQt                = QString::fromLatin1(qVersion());
	fields.serverCapabilitySummary = m_capability.summary;

	PreparedReport report;
	report.issueTitle = Mumble::Feedback::issueTitle(fields);
	report.issueBody =
		Mumble::Feedback::issueBody(fields, m_capability.maxBodyBytes, m_capability.maxLogBytes);
	report.fallbackUrl = fallbackIssueUrl(report.issueTitle, report.issueBody, fields.kind);

	report.message.set_kind(fields.kind);
	report.message.set_title(u8(fields.title));
	report.message.set_description(u8(fields.description));
	if (!fields.reproductionSteps.isEmpty()) {
		report.message.set_reproduction_steps(u8(fields.reproductionSteps));
	}
	report.message.set_diagnostics_included(fields.diagnosticsIncluded);
	if (fields.diagnosticsIncluded && !fields.diagnostics.isEmpty()) {
		report.message.set_diagnostics(u8(Mumble::Feedback::redactedDiagnostics(fields.diagnostics, m_capability.maxLogBytes)));
	}
	report.message.set_client_report_id(u8(QUuid::createUuid().toString(QUuid::WithoutBraces)));
	report.message.set_created_at(static_cast< uint64_t >(QDateTime::currentSecsSinceEpoch()));
	report.message.set_client_release(u8(fields.clientRelease));
	report.message.set_client_arch(u8(fields.clientArch));
	report.message.set_client_os(u8(fields.clientOS));
	report.message.set_client_qt(u8(fields.clientQt));

	return report;
}

QUrl FeedbackDialog::fallbackIssueUrl(const QString &title, const QString &body,
									  const MumbleProto::FeedbackReportKind kind) {
	QUrl url(QStringLiteral("https://github.com/dankmaster/mumble-forked/issues/new"));
	QUrlQuery query;
	query.addQueryItem(QStringLiteral("title"), title);
	query.addQueryItem(QStringLiteral("body"), body);
	query.addQueryItem(QStringLiteral("labels"),
					   QStringLiteral("triage,in-app-feedback,%1").arg(kindLabelForUrl(kind)));
	url.setQuery(query);
	return url;
}

MumbleProto::FeedbackReportKind FeedbackDialog::selectedKind() const {
	return static_cast< MumbleProto::FeedbackReportKind >(m_typeCombo->currentData().toInt());
}

QString FeedbackDialog::diagnosticsText() const {
	QString diagnostics;
	QTextStream stream(&diagnostics);
	stream << "Generated: " << QDateTime::currentDateTimeUtc().toString(Qt::ISODateWithMs) << "Z\n";
	stream << "Version: " << Version::getRelease() << "\n";
	stream << "Architecture: " << buildArchitecture() << "\n";
	stream << "OS: " << OSInfo::getOSDisplayableVersion() << "\n";
	stream << "Qt: " << QString::fromLatin1(qVersion()) << "\n";
	stream << "Server feedback: " << m_capability.summary << "\n";

	const QString logs = consoleLogSnippet().trimmed();
	if (!logs.isEmpty()) {
		stream << "\nConsole.txt:\n" << logs << "\n";
	}

	return diagnostics;
}

QString FeedbackDialog::consoleLogSnippet() const {
	const QFileInfo info(Global::get().qdBasePath.filePath(QStringLiteral("Console.txt")));
	if (!info.exists() || !info.isFile()) {
		return QString();
	}

	QFile file(info.absoluteFilePath());
	if (!file.open(QIODevice::ReadOnly | QIODevice::Text)) {
		return QString();
	}

	const qint64 maxBytes = qMax< qint64 >(1, static_cast< qint64 >(m_capability.maxLogBytes));
	const qint64 size     = file.size();
	const qint64 offset   = (m_captureStartOffset >= 0 && m_captureStartOffset <= size)
								? m_captureStartOffset
								: qMax< qint64 >(0, size - maxBytes);
	if (!file.seek(offset)) {
		return QString();
	}

	QByteArray bytes = file.read(maxBytes + 1024);
	return Mumble::Feedback::redactedDiagnostics(QString::fromUtf8(bytes), m_capability.maxLogBytes);
}

void FeedbackDialog::setCaptureActive(const bool active) {
	m_captureActive = active;
	m_captureButton->setText(active ? tr("Stop capture") : tr("Start repro capture"));
}

void FeedbackDialog::updateFormState() {
	const bool requiredFilled =
		!m_titleEdit->text().trimmed().isEmpty() && !m_descriptionEdit->toPlainText().trimmed().isEmpty();
	m_submitButton->setEnabled(requiredFilled && m_capability.connected && m_capability.supported
							   && m_capability.enabled);
	m_copyButton->setEnabled(requiredFilled);
	m_openButton->setEnabled(requiredFilled);
	m_reproductionEdit->setEnabled(selectedKind() == MumbleProto::FeedbackReportBug
								   || !m_reproductionEdit->toPlainText().trimmed().isEmpty());

	if (!m_capability.connected) {
		m_statusLabel->setText(tr("Not connected. Copy or open the prefilled GitHub issue instead."));
	} else if (!m_capability.supported) {
		m_statusLabel->setText(tr("This server does not advertise in-app feedback. Fallback is available."));
	} else if (!m_capability.enabled) {
		m_statusLabel->setText(tr("The connected server has feedback submission disabled. Fallback is available."));
	} else if (m_captureActive) {
		m_statusLabel->setText(tr("Capturing new Console.txt lines for this repro."));
	} else if (m_captureStartOffset >= 0) {
		m_statusLabel->setText(tr("Capture stopped. Diagnostics use logs since capture start."));
	} else {
		m_statusLabel->setText(tr("Diagnostics use a capped recent Console.txt tail unless repro capture is started."));
	}
}

void FeedbackDialog::updateDiagnosticsPreview() {
	m_diagnosticsPreview->setPlainText(
		Mumble::Feedback::redactedDiagnostics(diagnosticsText(), m_capability.maxLogBytes));
}

void FeedbackDialog::updateDiagnosticsDefault() {
	m_includeDiagnosticsBox->setChecked(selectedKind() != MumbleProto::FeedbackReportSuggestion);
	updateDiagnosticsPreview();
}

void FeedbackDialog::toggleCapture() {
	if (m_captureActive) {
		setCaptureActive(false);
		updateDiagnosticsPreview();
		updateFormState();
		return;
	}

	const QFileInfo info(Global::get().qdBasePath.filePath(QStringLiteral("Console.txt")));
	m_captureStartOffset = info.exists() ? info.size() : 0;
	setCaptureActive(true);
	updateDiagnosticsPreview();
	updateFormState();
}

void FeedbackDialog::copyReport() {
	const PreparedReport report = preparedReport();
	QApplication::clipboard()->setText(tr("Title: %1\n\n%2").arg(report.issueTitle, report.issueBody));
	QMessageBox::information(this, tr("Report copied"), tr("The feedback report markdown was copied."));
}

void FeedbackDialog::openFallbackIssue() {
	QDesktopServices::openUrl(preparedReport().fallbackUrl);
}

void FeedbackDialog::submitReport() {
	if (!m_submitButton->isEnabled()) {
		return;
	}
	done(SubmitReport);
}
