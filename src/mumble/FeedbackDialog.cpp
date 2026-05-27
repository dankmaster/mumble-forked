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

#include <QtCore/QBuffer>
#include <QtCore/QDateTime>
#include <QtCore/QFile>
#include <QtCore/QFileInfo>
#include <QtCore/QMimeData>
#include <QtCore/QMimeDatabase>
#include <QtCore/QMimeType>
#include <QtCore/QTextStream>
#include <QtCore/QUuid>
#include <QtCore/QUrlQuery>
#include <QtGui/QClipboard>
#include <QtGui/QDesktopServices>
#include <QtGui/QDragEnterEvent>
#include <QtGui/QDragMoveEvent>
#include <QtGui/QDropEvent>
#include <QtGui/QImageReader>
#include <QtGui/QImageWriter>
#include <QtGui/QPainter>
#include <QtGui/QPixmap>
#include <QtWidgets/QApplication>
#include <QtWidgets/QAbstractItemView>
#include <QtWidgets/QCheckBox>
#include <QtWidgets/QComboBox>
#include <QtWidgets/QDialogButtonBox>
#include <QtWidgets/QFormLayout>
#include <QtWidgets/QHBoxLayout>
#include <QtWidgets/QLabel>
#include <QtWidgets/QLineEdit>
#include <QtWidgets/QListWidget>
#include <QtWidgets/QMessageBox>
#include <QtWidgets/QPlainTextEdit>
#include <QtWidgets/QPushButton>
#include <QtWidgets/QVBoxLayout>

#ifdef Q_OS_WIN
#	include <shlobj.h>
#endif

#include <algorithm>
#include <cmath>
#include <cstring>
#include <functional>

namespace {
constexpr qsizetype MAX_EVIDENCE_MARKDOWN_BYTES       = 48 * 1024;
constexpr qsizetype MIN_NON_EVIDENCE_BODY_BYTES       = 12 * 1024;
constexpr qsizetype MAX_EVIDENCE_BINARY_BYTES         = 32 * 1024;
constexpr qsizetype MAX_EVIDENCE_TEXT_BYTES           = 24 * 1024;
constexpr qsizetype MAX_EVIDENCE_SOURCE_IMAGE_BYTES   = 10 * 1024 * 1024;
constexpr int MAX_EVIDENCE_ITEMS                      = 6;

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

QString sanitizedEvidenceName(QString name, const QString &fallback) {
	name = name.trimmed();
	name.replace(QLatin1Char('\r'), QLatin1Char(' '));
	name.replace(QLatin1Char('\n'), QLatin1Char(' '));
	name.replace(QLatin1Char('|'), QLatin1Char('-'));
	if (name.isEmpty()) {
		name = fallback;
	}
	return name.left(120);
}

QString markdownCodeBlockValue(QString value) {
	value.replace(QStringLiteral("```"), QStringLiteral("` ` `"));
	return value.trimmed();
}

QString markdownHeadingValue(const QString &value) {
	QString heading = sanitizedEvidenceName(value, QStringLiteral("pasted-evidence"));
	heading.replace(QLatin1Char('#'), QLatin1Char('-'));
	return heading;
}

QString humanByteCount(const qint64 bytes) {
	if (bytes < 1024) {
		return QObject::tr("%1 B").arg(bytes);
	}

	const double kib = static_cast< double >(bytes) / 1024.0;
	if (kib < 1024.0) {
		return QObject::tr("%1 KiB").arg(kib, 0, 'f', kib >= 10.0 ? 0 : 1);
	}

	return QObject::tr("%1 MiB").arg(kib / 1024.0, 0, 'f', 1);
}

bool isTextMimeType(const QString &mimeType, const QString &fileName = QString()) {
	const QString normalized = mimeType.trimmed().toLower();
	if (normalized.startsWith(QStringLiteral("text/"))) {
		return true;
	}

	if (normalized == QLatin1String("application/json") || normalized == QLatin1String("application/xml")
		|| normalized == QLatin1String("application/javascript") || normalized == QLatin1String("application/x-yaml")
		|| normalized == QLatin1String("application/yaml") || normalized == QLatin1String("application/sql")
		|| normalized.endsWith(QLatin1String("+json")) || normalized.endsWith(QLatin1String("+xml"))) {
		return true;
	}

	const QString suffix = QFileInfo(fileName).suffix().toLower();
	return suffix == QLatin1String("log") || suffix == QLatin1String("md") || suffix == QLatin1String("txt")
		   || suffix == QLatin1String("csv") || suffix == QLatin1String("tsv") || suffix == QLatin1String("json")
		   || suffix == QLatin1String("xml") || suffix == QLatin1String("yml") || suffix == QLatin1String("yaml");
}

bool bytesLookTextual(const QByteArray &bytes) {
	if (bytes.isEmpty()) {
		return true;
	}

	const qsizetype sampleSize = std::min< qsizetype >(bytes.size(), 4096);
	for (qsizetype i = 0; i < sampleSize; ++i) {
		const unsigned char c = static_cast< unsigned char >(bytes.at(i));
		if (c == 0) {
			return false;
		}
	}
	return true;
}

QString decodeEvidenceText(const QByteArray &bytes) {
	QString text = QString::fromUtf8(bytes);
	if (text.contains(QChar::ReplacementCharacter)) {
		text = QString::fromLocal8Bit(bytes);
	}
	return text;
}

QImage mimeDataImage(const QMimeData *mimeData) {
	if (!mimeData || !mimeData->hasImage()) {
		return QImage();
	}

	const QVariant imageData = mimeData->imageData();
	QImage image             = qvariant_cast< QImage >(imageData);
	if (!image.isNull()) {
		return image;
	}

	const QPixmap pixmap = qvariant_cast< QPixmap >(imageData);
	return pixmap.isNull() ? QImage() : pixmap.toImage();
}

bool mimeDataHasLocalFileUrls(const QMimeData *mimeData) {
	if (!mimeData || !mimeData->hasUrls()) {
		return false;
	}

	for (const QUrl &url : mimeData->urls()) {
		if (url.isLocalFile()) {
			return true;
		}
	}
	return false;
}

bool mimeDataHasImageFormatBytes(const QMimeData *mimeData) {
	if (!mimeData) {
		return false;
	}

	for (const QString &format : mimeData->formats()) {
		if (format.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive) && !mimeData->data(format).isEmpty()) {
			return true;
		}
	}
	return false;
}

bool mimeDataHasVirtualFiles(const QMimeData *mimeData) {
#ifdef Q_OS_WIN
	return mimeData
		   && mimeData->hasFormat(
			   QLatin1String("application/x-qt-windows-mime;value=\"FileGroupDescriptorW\""));
#else
	Q_UNUSED(mimeData);
	return false;
#endif
}

bool mimeDataHasEvidence(const QMimeData *mimeData) {
	return !mimeDataImage(mimeData).isNull() || mimeDataHasLocalFileUrls(mimeData)
		   || mimeDataHasImageFormatBytes(mimeData) || mimeDataHasVirtualFiles(mimeData);
}

QImage flattenedImageForEvidence(const QImage &image) {
	if (!image.hasAlphaChannel()) {
		return image.convertToFormat(QImage::Format_RGB888);
	}

	QImage flattened(image.size(), QImage::Format_RGB888);
	flattened.fill(Qt::white);
	QPainter painter(&flattened);
	painter.drawImage(QPoint(0, 0), image);
	return flattened;
}

QByteArray encodeImageBytes(const QImage &image, const char *format, const int quality) {
	QByteArray bytes;
	QBuffer buffer(&bytes);
	if (!buffer.open(QIODevice::WriteOnly)) {
		return QByteArray();
	}

	QImageWriter writer(&buffer, format);
	if (quality >= 0) {
		writer.setQuality(quality);
	}
	if (!writer.write(image)) {
		return QByteArray();
	}
	return bytes;
}

QByteArray encodeEvidenceImage(const QImage &source, const qsizetype maxBytes, QString *mimeType, QString *extension,
							   QSize *encodedSize) {
	if (source.isNull() || maxBytes <= 0) {
		return QByteArray();
	}

	const QList< int > maxDimensions = { 1600, 1280, 960, 720, 540, 420 };
	const QList< int > qualities     = { 82, 74, 66, 58 };
	for (const int maxDimension : maxDimensions) {
		QImage image = source;
		const int longestSide = std::max(image.width(), image.height());
		if (longestSide > maxDimension) {
			image = image.scaled(maxDimension, maxDimension, Qt::KeepAspectRatio, Qt::SmoothTransformation);
		}

		image = flattenedImageForEvidence(image);
		for (const int quality : qualities) {
			const QByteArray bytes = encodeImageBytes(image, "jpg", quality);
			if (!bytes.isEmpty() && bytes.size() <= maxBytes) {
				if (mimeType) {
					*mimeType = QStringLiteral("image/jpeg");
				}
				if (extension) {
					*extension = QStringLiteral("jpg");
				}
				if (encodedSize) {
					*encodedSize = image.size();
				}
				return bytes;
			}
		}
	}

	return QByteArray();
}

QString textEvidenceMarkdown(const QString &label, const QString &source, const QString &mimeType,
							 const QByteArray &bytes, const qint64 originalSize, const bool truncated) {
	QString markdown;
	QTextStream stream(&markdown);
	stream << "#### " << markdownHeadingValue(label) << "\n";
	stream << "- Source: " << source << "\n";
	if (!mimeType.trimmed().isEmpty()) {
		stream << "- MIME: " << mimeType.trimmed() << "\n";
	}
	stream << "- Size: " << humanByteCount(originalSize) << "\n\n";
	stream << "```text\n" << markdownCodeBlockValue(decodeEvidenceText(bytes)) << "\n";
	if (truncated) {
		stream << "\n[pasted file truncated]\n";
	}
	stream << "```\n";
	return markdown;
}

QString binaryEvidenceMarkdown(const QString &label, const QString &source, const QString &mimeType,
							   const QByteArray &bytes, const QStringList &extraLines = QStringList()) {
	QString markdown;
	QTextStream stream(&markdown);
	stream << "#### " << markdownHeadingValue(label) << "\n";
	stream << "- Source: " << source << "\n";
	if (!mimeType.trimmed().isEmpty()) {
		stream << "- MIME: " << mimeType.trimmed() << "\n";
	}
	stream << "- Size: " << humanByteCount(bytes.size()) << "\n";
	for (const QString &line : extraLines) {
		if (!line.trimmed().isEmpty()) {
			stream << "- " << line.trimmed() << "\n";
		}
	}
	stream << "\n```text\n" << QString::fromLatin1(bytes.toBase64()) << "\n```\n";
	return markdown;
}

QString imageEvidenceMarkdown(const QString &label, const QString &source, const QString &mimeType,
							  const QByteArray &bytes, const QSize &dimensions) {
	QString markdown;
	QTextStream stream(&markdown);
	stream << "#### " << markdownHeadingValue(label) << "\n";
	stream << "- Source: " << source << "\n";
	stream << "- MIME: " << mimeType << "\n";
	stream << "- Size: " << humanByteCount(bytes.size()) << "\n";
	if (dimensions.isValid()) {
		stream << "- Dimensions: " << dimensions.width() << "x" << dimensions.height() << "\n";
	}
	stream << "\n";
	stream << "![" << sanitizedEvidenceName(label, QStringLiteral("pasted screenshot"))
		   << "](data:" << mimeType << ";base64," << QString::fromLatin1(bytes.toBase64()) << ")\n";
	return markdown;
}

class FeedbackEvidenceTextEdit : public QPlainTextEdit {
public:
	using EvidencePasteHandler = std::function< bool(const QMimeData *) >;

	explicit FeedbackEvidenceTextEdit(QWidget *parent = nullptr) : QPlainTextEdit(parent) {
		setAcceptDrops(true);
	}

	void setEvidencePasteHandler(EvidencePasteHandler handler) { m_evidencePasteHandler = std::move(handler); }

protected:
	bool canInsertFromMimeData(const QMimeData *source) const Q_DECL_OVERRIDE {
		return QPlainTextEdit::canInsertFromMimeData(source) || mimeDataHasEvidence(source);
	}

	void insertFromMimeData(const QMimeData *source) Q_DECL_OVERRIDE {
		if (mimeDataHasEvidence(source) && m_evidencePasteHandler && m_evidencePasteHandler(source)) {
			return;
		}

		QPlainTextEdit::insertFromMimeData(source);
	}

	void dragEnterEvent(QDragEnterEvent *event) Q_DECL_OVERRIDE {
		if (event && mimeDataHasEvidence(event->mimeData())) {
			event->acceptProposedAction();
			return;
		}
		QPlainTextEdit::dragEnterEvent(event);
	}

	void dragMoveEvent(QDragMoveEvent *event) Q_DECL_OVERRIDE {
		if (event && mimeDataHasEvidence(event->mimeData())) {
			event->acceptProposedAction();
			return;
		}
		QPlainTextEdit::dragMoveEvent(event);
	}

	void dropEvent(QDropEvent *event) Q_DECL_OVERRIDE {
		if (event && mimeDataHasEvidence(event->mimeData()) && m_evidencePasteHandler
			&& m_evidencePasteHandler(event->mimeData())) {
			event->acceptProposedAction();
			return;
		}
		QPlainTextEdit::dropEvent(event);
	}

private:
	EvidencePasteHandler m_evidencePasteHandler;
};
} // namespace

FeedbackDialog::FeedbackDialog(const ServerCapability &capability, QWidget *parent)
	: QDialog(parent), m_capability(capability) {
	setWindowTitle(tr("Report feedback"));
	setModal(true);
	resize(780, 720);

	m_typeCombo = new QComboBox(this);
	m_typeCombo->addItem(tr("Bug"), static_cast< int >(MumbleProto::FeedbackReportBug));
	m_typeCombo->addItem(tr("Suggestion"), static_cast< int >(MumbleProto::FeedbackReportSuggestion));
	m_typeCombo->addItem(tr("Support"), static_cast< int >(MumbleProto::FeedbackReportSupport));

	m_titleEdit = new QLineEdit(this);
	m_titleEdit->setMaxLength(160);

	auto *descriptionEdit = new FeedbackEvidenceTextEdit(this);
	descriptionEdit->setEvidencePasteHandler([this](const QMimeData *mimeData) {
		return addEvidenceFromMimeData(mimeData);
	});
	m_descriptionEdit = descriptionEdit;
	m_descriptionEdit->setMinimumHeight(110);

	auto *reproductionEdit = new FeedbackEvidenceTextEdit(this);
	reproductionEdit->setEvidencePasteHandler([this](const QMimeData *mimeData) {
		return addEvidenceFromMimeData(mimeData);
	});
	m_reproductionEdit = reproductionEdit;
	m_reproductionEdit->setMinimumHeight(80);

	m_evidenceList = new QListWidget(this);
	m_evidenceList->setMinimumHeight(54);
	m_evidenceList->setMaximumHeight(90);
	m_evidenceList->setSelectionMode(QAbstractItemView::NoSelection);
	m_evidenceList->setFocusPolicy(Qt::NoFocus);

	m_pasteEvidenceButton = new QPushButton(tr("Paste file/screenshot"), this);
	m_clearEvidenceButton = new QPushButton(tr("Clear"), this);

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
	QWidget *evidenceWidget = new QWidget(this);
	QVBoxLayout *evidenceLayout = new QVBoxLayout(evidenceWidget);
	evidenceLayout->setContentsMargins(0, 0, 0, 0);
	evidenceLayout->addWidget(m_evidenceList);
	QHBoxLayout *evidenceButtons = new QHBoxLayout();
	evidenceButtons->setContentsMargins(0, 0, 0, 0);
	evidenceButtons->addWidget(m_pasteEvidenceButton);
	evidenceButtons->addWidget(m_clearEvidenceButton);
	evidenceButtons->addStretch(1);
	evidenceLayout->addLayout(evidenceButtons);
	form->addRow(tr("Pasted evidence"), evidenceWidget);
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
	connect(m_pasteEvidenceButton, &QPushButton::clicked, this, &FeedbackDialog::pasteEvidenceFromClipboard);
	connect(m_clearEvidenceButton, &QPushButton::clicked, this, &FeedbackDialog::clearPastedEvidence);
	connect(m_submitButton, &QPushButton::clicked, this, &FeedbackDialog::submitReport);
	connect(m_copyButton, &QPushButton::clicked, this, &FeedbackDialog::copyReport);
	connect(m_openButton, &QPushButton::clicked, this, &FeedbackDialog::openFallbackIssue);
	connect(m_closeButton, &QPushButton::clicked, this, &FeedbackDialog::reject);

	updateDiagnosticsPreview();
	updateEvidenceList();
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
	fields.pastedEvidence          = pastedEvidenceMarkdown();

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
	if (!fields.pastedEvidence.isEmpty()) {
		report.message.set_pasted_evidence(u8(fields.pastedEvidence));
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

qsizetype FeedbackDialog::maxPastedEvidenceMarkdownBytes() const {
	const qsizetype bodyBytes = static_cast< qsizetype >(qMax(1u, m_capability.maxBodyBytes));
	if (bodyBytes <= MIN_NON_EVIDENCE_BODY_BYTES) {
		return bodyBytes / 2;
	}
	return std::min< qsizetype >(MAX_EVIDENCE_MARKDOWN_BYTES, bodyBytes - MIN_NON_EVIDENCE_BODY_BYTES);
}

qsizetype FeedbackDialog::currentPastedEvidenceMarkdownBytes() const {
	qsizetype bytes = 0;
	for (const PastedEvidence &evidence : m_pastedEvidence) {
		bytes += evidence.markdown.toUtf8().size() + 2;
	}
	return bytes;
}

QString FeedbackDialog::pastedEvidenceMarkdown() const {
	QString markdown;
	QTextStream stream(&markdown);
	for (const PastedEvidence &evidence : m_pastedEvidence) {
		stream << evidence.markdown.trimmed() << "\n\n";
	}
	return Mumble::Feedback::truncateUtf8Bytes(markdown.trimmed(), maxPastedEvidenceMarkdownBytes(),
											   QStringLiteral("[pasted evidence truncated]"));
}

bool FeedbackDialog::addEvidenceMarkdown(const QString &label, const QString &markdown, const qint64 byteSize) {
	if (m_pastedEvidence.size() >= MAX_EVIDENCE_ITEMS) {
		m_statusLabel->setText(tr("Skipped pasted evidence. The report already has %1 evidence items.")
								   .arg(MAX_EVIDENCE_ITEMS));
		return false;
	}

	const qsizetype markdownBytes = markdown.toUtf8().size() + 2;
	const qsizetype maxBytes      = maxPastedEvidenceMarkdownBytes();
	if (currentPastedEvidenceMarkdownBytes() + markdownBytes > maxBytes) {
		m_statusLabel->setText(tr("Skipped pasted evidence. The report evidence budget is %1.")
								   .arg(humanByteCount(maxBytes)));
		return false;
	}

	PastedEvidence evidence;
	evidence.label    = sanitizedEvidenceName(label, tr("Pasted evidence"));
	evidence.markdown = markdown;
	evidence.byteSize = byteSize;
	m_pastedEvidence.push_back(evidence);
	return true;
}

void FeedbackDialog::updateEvidenceList() {
	m_evidenceList->clear();
	if (m_pastedEvidence.isEmpty()) {
		QListWidgetItem *item = new QListWidgetItem(tr("No pasted files or screenshots."), m_evidenceList);
		item->setFlags(Qt::NoItemFlags);
		m_clearEvidenceButton->setEnabled(false);
		return;
	}

	for (const PastedEvidence &evidence : m_pastedEvidence) {
		QListWidgetItem *item =
			new QListWidgetItem(tr("%1 (%2)").arg(evidence.label, humanByteCount(evidence.byteSize)), m_evidenceList);
		item->setFlags(Qt::ItemIsEnabled);
	}
	m_clearEvidenceButton->setEnabled(true);
}

bool FeedbackDialog::addEvidenceFromMimeData(const QMimeData *mimeData) {
	if (!mimeData || !mimeDataHasEvidence(mimeData)) {
		return false;
	}

	const int initialCount = m_pastedEvidence.size();
	QStringList skipped;

	const auto remainingRawBinaryBudget = [this]() -> qsizetype {
		const qsizetype remaining = maxPastedEvidenceMarkdownBytes() - currentPastedEvidenceMarkdownBytes() - 1024;
		if (remaining <= 0) {
			return 0;
		}
		return std::min< qsizetype >(MAX_EVIDENCE_BINARY_BYTES, (remaining * 3) / 4);
	};

	const auto addImageEvidence = [this, &skipped, &remainingRawBinaryBudget](const QImage &image,
																			 const QString &label,
																			 const QString &source) {
		const qsizetype rawLimit = remainingRawBinaryBudget();
		QString mimeType;
		QString extension;
		QSize encodedSize;
		const QByteArray bytes = encodeEvidenceImage(image, rawLimit, &mimeType, &extension, &encodedSize);
		if (bytes.isEmpty()) {
			skipped << tr("%1 was too large to fit in this report.").arg(label);
			return false;
		}

		const QString evidenceLabel = sanitizedEvidenceName(
			label.endsWith(QLatin1String(".jpg"), Qt::CaseInsensitive) ? label
																	   : QStringLiteral("%1.%2").arg(label, extension),
			tr("pasted-screenshot.jpg"));
		return addEvidenceMarkdown(evidenceLabel,
								   imageEvidenceMarkdown(evidenceLabel, source, mimeType, bytes, encodedSize),
								   bytes.size());
	};

	const auto addBytesEvidence = [this, &skipped, &remainingRawBinaryBudget, &addImageEvidence](
									  const QString &label, const QString &source, const QString &mimeType,
									  const QByteArray &bytes, const qint64 originalSize) {
		const QString safeLabel = sanitizedEvidenceName(label, tr("pasted-file"));
		const bool isImage      = mimeType.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive);
		if (isImage) {
			const QImage image = QImage::fromData(bytes);
			if (!image.isNull()) {
				return addImageEvidence(image, safeLabel, source);
			}
		}

		if (isTextMimeType(mimeType, safeLabel) || bytesLookTextual(bytes)) {
			const qsizetype maxTextBytes =
				std::max< qsizetype >(0, std::min< qsizetype >(MAX_EVIDENCE_TEXT_BYTES,
															   maxPastedEvidenceMarkdownBytes()
																   - currentPastedEvidenceMarkdownBytes()
																   - 1024));
			if (maxTextBytes <= 0) {
				skipped << tr("%1 was too large to fit in this report.").arg(safeLabel);
				return false;
			}

			const QByteArray textBytes = bytes.left(maxTextBytes);
			const bool truncated       = originalSize > textBytes.size();
			return addEvidenceMarkdown(safeLabel,
									   textEvidenceMarkdown(safeLabel, source, mimeType, textBytes, originalSize,
															truncated),
									   originalSize);
		}

		const qsizetype rawLimit = remainingRawBinaryBudget();
		if (bytes.size() > rawLimit) {
			skipped << tr("%1 was too large to fit in this report.").arg(safeLabel);
			return false;
		}

		return addEvidenceMarkdown(safeLabel, binaryEvidenceMarkdown(safeLabel, source, mimeType, bytes), bytes.size());
	};

	const QImage clipboardImage = mimeDataImage(mimeData);
	if (!clipboardImage.isNull()) {
		addImageEvidence(clipboardImage,
						 QStringLiteral("clipboard-screenshot-%1")
							 .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss"))),
						 tr("Clipboard image"));
	}

	if (mimeData->hasUrls()) {
		QMimeDatabase mimeDatabase;
		for (const QUrl &url : mimeData->urls()) {
			if (!url.isLocalFile()) {
				continue;
			}

			const QFileInfo info(url.toLocalFile());
			const QString label = info.fileName();
			if (!info.exists() || !info.isFile()) {
				skipped << tr("%1 is not a file.").arg(label.isEmpty() ? url.toString() : label);
				continue;
			}

			const QMimeType mime = mimeDatabase.mimeTypeForFile(info);
			if (mime.name().startsWith(QStringLiteral("image/"), Qt::CaseInsensitive)
				|| !QImageReader::imageFormat(info.absoluteFilePath()).isEmpty()) {
				if (info.size() > MAX_EVIDENCE_SOURCE_IMAGE_BYTES) {
					skipped << tr("%1 was too large to read as an image.").arg(label);
					continue;
				}

				QImageReader reader(info.absoluteFilePath());
				reader.setAutoTransform(true);
				const QImage image = reader.read();
				if (!image.isNull()) {
					addImageEvidence(image, label, tr("Local file"));
					continue;
				}
			}

			QFile file(info.absoluteFilePath());
			if (!file.open(QIODevice::ReadOnly)) {
				skipped << tr("Could not read %1.").arg(label);
				continue;
			}

			const qint64 readBytes =
				std::min< qint64 >(info.size(), std::max< qsizetype >(MAX_EVIDENCE_TEXT_BYTES,
																	   MAX_EVIDENCE_BINARY_BYTES));
			const QByteArray bytes = file.read(readBytes);
			addBytesEvidence(label, tr("Local file"), mime.name(), bytes, info.size());
		}
	}

#ifdef Q_OS_WIN
	const QString descriptorFormat =
		QLatin1String("application/x-qt-windows-mime;value=\"FileGroupDescriptorW\"");
	if (mimeData->hasFormat(descriptorFormat)) {
		QMimeDatabase mimeDatabase;
		const QByteArray descriptorBytes = mimeData->data(descriptorFormat);
		if (descriptorBytes.size() >= static_cast< int >(sizeof(UINT))) {
			UINT itemCount = 0;
			std::memcpy(&itemCount, descriptorBytes.constData(), sizeof(UINT));
			if (itemCount <= 16) {
				const qsizetype descriptorSize =
					sizeof(UINT) + static_cast< qsizetype >(itemCount) * sizeof(FILEDESCRIPTORW);
				if (descriptorBytes.size() < descriptorSize) {
					skipped << tr("Clipboard file list could not be read.");
				} else {
					const auto *descriptors =
						reinterpret_cast< const FILEDESCRIPTORW * >(descriptorBytes.constData() + sizeof(UINT));
					for (UINT i = 0; i < itemCount; ++i) {
						const QString label = sanitizedEvidenceName(QString::fromWCharArray(descriptors[i].cFileName),
																	tr("clipboard-file"));
						const QString indexedContentFormat =
							QStringLiteral("application/x-qt-windows-mime;value=\"FileContents\";index=%1").arg(i);
						QByteArray bytes = mimeData->data(indexedContentFormat);
						if (bytes.isEmpty() && itemCount == 1) {
							bytes =
								mimeData->data(QLatin1String("application/x-qt-windows-mime;value=\"FileContents\""));
						}
						if (bytes.isEmpty()) {
							skipped << tr("%1 did not expose file contents on the clipboard.").arg(label);
							continue;
						}

						const QMimeType mime = mimeDatabase.mimeTypeForFileNameAndData(label, bytes);
						addBytesEvidence(label, tr("Windows clipboard file"), mime.name(), bytes, bytes.size());
					}
				}
			}
		}
	}
#endif

	if (m_pastedEvidence.size() == initialCount && mimeData->hasImage() == false) {
		for (const QString &format : mimeData->formats()) {
			if (!format.startsWith(QStringLiteral("image/"), Qt::CaseInsensitive)) {
				continue;
			}
			const QByteArray bytes = mimeData->data(format);
			const QImage image     = QImage::fromData(bytes);
			if (!image.isNull()) {
				const QString extension =
					format.section(QLatin1Char('/'), 1, 1).replace(QLatin1Char('+'), QLatin1Char('-'));
				addImageEvidence(image,
								 QStringLiteral("clipboard-image-%1.%2")
									 .arg(QDateTime::currentDateTimeUtc().toString(QStringLiteral("yyyyMMdd-HHmmss")),
										  extension.isEmpty() ? QStringLiteral("img") : extension),
								 tr("Clipboard image data"));
				break;
			}
		}
	}

	updateEvidenceList();
	const int addedCount = m_pastedEvidence.size() - initialCount;
	if (addedCount > 0) {
		m_statusLabel->setText(tr("Added %n pasted evidence item(s).", nullptr, addedCount));
	} else if (!skipped.isEmpty()) {
		m_statusLabel->setText(skipped.join(QLatin1Char(' ')));
	} else {
		m_statusLabel->setText(tr("Clipboard did not contain a supported small file or screenshot."));
	}
	return true;
}

void FeedbackDialog::setCaptureActive(const bool active) {
	m_captureActive = active;
	m_captureButton->setText(active ? tr("Stop capture") : tr("Start repro capture"));
}

void FeedbackDialog::updateFormState() {
	const bool requiredFilled =
		!m_titleEdit->text().trimmed().isEmpty() && !m_descriptionEdit->toPlainText().trimmed().isEmpty();
	m_submitButton->setEnabled(requiredFilled);
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

void FeedbackDialog::pasteEvidenceFromClipboard() {
	const QClipboard *clipboard = QApplication::clipboard();
	const QMimeData *mimeData   = clipboard ? clipboard->mimeData() : nullptr;
	if (!addEvidenceFromMimeData(mimeData)) {
		QMessageBox::information(
			this, tr("No pasteable evidence"),
			tr("Copy a screenshot or a small local file, then paste it into the feedback report."));
	}
}

void FeedbackDialog::clearPastedEvidence() {
	m_pastedEvidence.clear();
	updateEvidenceList();
	m_statusLabel->setText(tr("Pasted evidence cleared."));
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
