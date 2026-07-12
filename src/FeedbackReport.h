// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_FEEDBACKREPORT_H_
#define MUMBLE_FEEDBACKREPORT_H_

#include "Mumble.pb.h"

#include <QtCore/QList>
#include <QtCore/QString>
#include <QtCore/QUrl>

namespace Mumble {
namespace Feedback {
	constexpr unsigned int DEFAULT_MAX_LOG_BYTES  = 200000;
	constexpr unsigned int DEFAULT_MAX_BODY_BYTES = 60000;

	struct ReportFields {
		MumbleProto::FeedbackReportKind kind = MumbleProto::FeedbackReportBug;
		QString title;
		QString description;
		QString reproductionSteps;
		QString diagnostics;
		bool diagnosticsIncluded = false;
		QString clientRelease;
		QString clientArch;
		QString clientOS;
		QString clientQt;
		QString serverCapabilitySummary;
		QString pastedEvidence;
	};

	struct ServerCapability {
		bool connected = false;
		bool supported = false;
		bool enabled = false;
		unsigned int maxLogBytes = DEFAULT_MAX_LOG_BYTES;
		unsigned int maxBodyBytes = DEFAULT_MAX_BODY_BYTES;
		QString summary;
	};

	struct PreparedReport {
		MumbleProto::FeedbackReport message;
		QString issueTitle;
		QString issueBody;
		QUrl fallbackUrl;
	};

	QString kindLabel(MumbleProto::FeedbackReportKind kind);
	QString kindTitlePrefix(MumbleProto::FeedbackReportKind kind);
	QString issueTitle(const ReportFields &fields);
	QString redactedDiagnostics(const QString &diagnostics, unsigned int maxBytes = DEFAULT_MAX_LOG_BYTES);
	QString truncateUtf8Bytes(const QString &value, unsigned int maxBytes, const QString &truncationNote);
	QString issueBody(const ReportFields &fields, unsigned int maxBodyBytes = DEFAULT_MAX_BODY_BYTES,
					  unsigned int maxDiagnosticsBytes = DEFAULT_MAX_LOG_BYTES);
	QStringList splitLabels(const QString &labels);
	QUrl fallbackIssueUrl(const QString &title, const QString &body, MumbleProto::FeedbackReportKind kind);
} // namespace Feedback
} // namespace Mumble

#endif
