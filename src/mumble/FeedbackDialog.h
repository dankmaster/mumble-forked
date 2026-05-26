// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_FEEDBACKDIALOG_H_
#define MUMBLE_MUMBLE_FEEDBACKDIALOG_H_

#include "Mumble.pb.h"

#include <QtCore/QUrl>
#include <QtWidgets/QDialog>

class QCheckBox;
class QComboBox;
class QLabel;
class QLineEdit;
class QPlainTextEdit;
class QPushButton;

class FeedbackDialog : public QDialog {
private:
	Q_OBJECT
	Q_DISABLE_COPY(FeedbackDialog)

public:
	enum DialogResult {
		SubmitReport = QDialog::Accepted + 1,
	};

	struct ServerCapability {
		bool connected = false;
		bool supported = false;
		bool enabled   = false;
		unsigned int maxLogBytes  = 200000;
		unsigned int maxBodyBytes = 60000;
		QString summary;
	};

	struct PreparedReport {
		MumbleProto::FeedbackReport message;
		QString issueTitle;
		QString issueBody;
		QUrl fallbackUrl;
	};

	explicit FeedbackDialog(const ServerCapability &capability, QWidget *parent = nullptr);

	PreparedReport preparedReport() const;
	static QUrl fallbackIssueUrl(const QString &title, const QString &body,
								 MumbleProto::FeedbackReportKind kind);

private slots:
	void updateFormState();
	void updateDiagnosticsPreview();
	void updateDiagnosticsDefault();
	void toggleCapture();
	void copyReport();
	void openFallbackIssue();
	void submitReport();

private:
	MumbleProto::FeedbackReportKind selectedKind() const;
	QString diagnosticsText() const;
	QString consoleLogSnippet() const;
	void setCaptureActive(bool active);

	ServerCapability m_capability;
	qint64 m_captureStartOffset = -1;
	bool m_captureActive        = false;

	QComboBox *m_typeCombo              = nullptr;
	QLineEdit *m_titleEdit              = nullptr;
	QPlainTextEdit *m_descriptionEdit   = nullptr;
	QPlainTextEdit *m_reproductionEdit  = nullptr;
	QCheckBox *m_includeDiagnosticsBox  = nullptr;
	QPlainTextEdit *m_diagnosticsPreview = nullptr;
	QLabel *m_statusLabel               = nullptr;
	QPushButton *m_captureButton        = nullptr;
	QPushButton *m_submitButton         = nullptr;
	QPushButton *m_copyButton           = nullptr;
	QPushButton *m_openButton           = nullptr;
	QPushButton *m_closeButton          = nullptr;
};

#endif
