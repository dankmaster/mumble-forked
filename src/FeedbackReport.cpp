// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "FeedbackReport.h"

#include <QtCore/QRegularExpression>
#include <QtCore/QStringList>
#include <QtCore/QTextStream>

namespace Mumble {
namespace Feedback {
	namespace {
		QString markdownValue(const QString &value) {
			const QString trimmed = value.trimmed();
			return trimmed.isEmpty() ? QStringLiteral("_Not provided._") : trimmed;
		}

		QString codeBlockValue(const QString &value) {
			QString sanitized = value;
			sanitized.replace(QStringLiteral("```"), QStringLiteral("` ` `"));
			return sanitized.trimmed();
		}
	} // namespace

	QString kindLabel(const MumbleProto::FeedbackReportKind kind) {
		switch (kind) {
			case MumbleProto::FeedbackReportBug:
				return QStringLiteral("Bug");
			case MumbleProto::FeedbackReportSuggestion:
				return QStringLiteral("Suggestion");
			case MumbleProto::FeedbackReportSupport:
				return QStringLiteral("Question");
		}

		return QStringLiteral("Feedback");
	}

	QString kindTitlePrefix(const MumbleProto::FeedbackReportKind kind) {
		return QStringLiteral("[%1]").arg(kindLabel(kind));
	}

	QString issueTitle(const ReportFields &fields) {
		const QString title = fields.title.trimmed();
		if (title.startsWith(kindTitlePrefix(fields.kind), Qt::CaseInsensitive)) {
			return title;
		}

		return QStringLiteral("%1 %2").arg(kindTitlePrefix(fields.kind), title);
	}

	QString truncateUtf8Bytes(const QString &value, const unsigned int maxBytes, const QString &truncationNote) {
		if (maxBytes == 0) {
			return QString();
		}

		const QByteArray encoded = value.toUtf8();
		if (encoded.size() <= static_cast< int >(maxBytes)) {
			return value;
		}

		QByteArray truncated = encoded.left(static_cast< int >(maxBytes));
		QString decoded      = QString::fromUtf8(truncated);
		while (!decoded.isEmpty() && decoded.toUtf8().size() > static_cast< int >(maxBytes)) {
			decoded.chop(1);
		}

		const QString note = truncationNote.trimmed();
		if (note.isEmpty()) {
			return decoded;
		}

		const QString separator      = QStringLiteral("\n\n");
		const QByteArray encodedNote = note.toUtf8();
		const int noteBytes          = static_cast< int >(encodedNote.size() + separator.toUtf8().size());
		if (noteBytes >= static_cast< int >(maxBytes)) {
			return QString::fromUtf8(encodedNote.left(static_cast< int >(maxBytes)));
		}

		const int allowedDecodedBytes = static_cast< int >(maxBytes) - noteBytes;
		while (!decoded.isEmpty() && decoded.toUtf8().size() > allowedDecodedBytes) {
			decoded.chop(1);
		}

		return decoded + separator + note;
	}

	QString redactedDiagnostics(const QString &diagnostics, const unsigned int maxBytes) {
		static const QRegularExpression sensitiveLine(
			QLatin1String("(password|passphrase|token|secret|authorization|certificate|private key|"
						  "cert(ificate)?[_ -]?hash|access token|api[_ -]?key|cookie|set-cookie)"),
			QRegularExpression::CaseInsensitiveOption);
		static const QRegularExpression bearerToken(
			QLatin1String("\\bBearer\\s+[A-Za-z0-9._~+/=-]+"), QRegularExpression::CaseInsensitiveOption);
		static const QRegularExpression ipv4Address(
			QLatin1String("\\b(?:\\d{1,3}\\.){3}\\d{1,3}\\b"));
		static const QRegularExpression ipv6Address(
			QLatin1String("\\b(?:[0-9A-Fa-f]{1,4}:){2,7}[0-9A-Fa-f]{0,4}\\b"));

		QStringList lines;
		for (QString line : diagnostics.split(QLatin1Char('\n'))) {
			line.replace(bearerToken, QStringLiteral("Bearer [redacted]"));
			line.replace(ipv4Address, QStringLiteral("[redacted-ip]"));
			line.replace(ipv6Address, QStringLiteral("[redacted-ip]"));
			if (sensitiveLine.match(line).hasMatch()) {
				lines << QStringLiteral("[redacted diagnostic line]");
			} else {
				lines << line;
			}
		}

		return truncateUtf8Bytes(lines.join(QLatin1Char('\n')), maxBytes,
								 QStringLiteral("[diagnostics truncated]"));
	}

	QString issueBody(const ReportFields &fields, const unsigned int maxBodyBytes,
					  const unsigned int maxDiagnosticsBytes) {
		QString body;
		QTextStream stream(&body);

		stream << "### Type\n" << kindLabel(fields.kind) << "\n\n";
		stream << "### Description\n" << markdownValue(fields.description) << "\n\n";

		if (fields.kind == MumbleProto::FeedbackReportBug || !fields.reproductionSteps.trimmed().isEmpty()) {
			stream << "### Steps to reproduce\n" << markdownValue(fields.reproductionSteps) << "\n\n";
		}

		if (!fields.pastedEvidence.trimmed().isEmpty()) {
			stream << "### Pasted evidence\n" << fields.pastedEvidence.trimmed() << "\n\n";
		}

		stream << "### Client environment\n";
		stream << "- Version: " << markdownValue(fields.clientRelease) << "\n";
		stream << "- Architecture: " << markdownValue(fields.clientArch) << "\n";
		stream << "- OS: " << markdownValue(fields.clientOS) << "\n";
		stream << "- Qt: " << markdownValue(fields.clientQt) << "\n";
		stream << "- Connected server feedback: " << markdownValue(fields.serverCapabilitySummary) << "\n\n";

		stream << "### Diagnostics\n";
		if (fields.diagnosticsIncluded && !fields.diagnostics.trimmed().isEmpty()) {
			stream << "```text\n" << codeBlockValue(redactedDiagnostics(fields.diagnostics, maxDiagnosticsBytes)) << "\n```\n";
		} else {
			stream << "_Not included._\n";
		}

		return truncateUtf8Bytes(body, maxBodyBytes, QStringLiteral("[issue body truncated]"));
	}

	QStringList splitLabels(const QString &labels) {
		QStringList result;
		for (const QString &label : labels.split(QRegularExpression(QLatin1String("[,\\n]+")), Qt::SkipEmptyParts)) {
			const QString trimmed = label.trimmed();
			if (!trimmed.isEmpty() && !result.contains(trimmed)) {
				result << trimmed;
			}
		}
		return result;
	}
} // namespace Feedback
} // namespace Mumble
