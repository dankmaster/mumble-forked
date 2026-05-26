// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "FeedbackReport.h"

#include <QtTest>

class TestFeedbackReport : public QObject {
	Q_OBJECT

private slots:
	void buildsBugMarkdown();
	void buildsSuggestionWithoutLogsByDefault();
	void buildsSupportMarkdown();
	void includesPastedEvidence();
	void redactsDiagnosticsAndCapsBytes();
	void splitsLabels();
};

namespace {
Mumble::Feedback::ReportFields baseFields(MumbleProto::FeedbackReportKind kind) {
	Mumble::Feedback::ReportFields fields;
	fields.kind                    = kind;
	fields.title                   = QStringLiteral("Short title");
	fields.description             = QStringLiteral("The thing that happened.");
	fields.clientRelease           = QStringLiteral("1.6.0");
	fields.clientArch              = QStringLiteral("x64");
	fields.clientOS                = QStringLiteral("Windows");
	fields.clientQt                = QStringLiteral("6.7.3");
	fields.serverCapabilitySummary = QStringLiteral("connected=yes; feature=yes; server-submit=yes");
	return fields;
}
} // namespace

void TestFeedbackReport::buildsBugMarkdown() {
	Mumble::Feedback::ReportFields fields = baseFields(MumbleProto::FeedbackReportBug);
	fields.reproductionSteps             = QStringLiteral("1. Click it\n2. It breaks");
	fields.diagnosticsIncluded           = true;
	fields.diagnostics                   = QStringLiteral("recent log line");

	const QString body = Mumble::Feedback::issueBody(fields, 60000, 200000);
	QVERIFY(body.contains(QStringLiteral("### Type\nBug")));
	QVERIFY(body.contains(QStringLiteral("### Description\nThe thing that happened.")));
	QVERIFY(body.contains(QStringLiteral("### Steps to reproduce\n1. Click it")));
	QVERIFY(body.contains(QStringLiteral("- Architecture: x64")));
	QVERIFY(body.contains(QStringLiteral("recent log line")));
	QCOMPARE(Mumble::Feedback::issueTitle(fields), QStringLiteral("[Bug] Short title"));
}

void TestFeedbackReport::buildsSuggestionWithoutLogsByDefault() {
	Mumble::Feedback::ReportFields fields = baseFields(MumbleProto::FeedbackReportSuggestion);
	fields.diagnostics                   = QStringLiteral("should not be included");

	const QString body = Mumble::Feedback::issueBody(fields, 60000, 200000);
	QVERIFY(body.contains(QStringLiteral("### Type\nSuggestion")));
	QVERIFY(!body.contains(QStringLiteral("### Steps to reproduce")));
	QVERIFY(body.contains(QStringLiteral("### Diagnostics\n_Not included._")));
	QVERIFY(!body.contains(QStringLiteral("should not be included")));
	QCOMPARE(Mumble::Feedback::issueTitle(fields), QStringLiteral("[Suggestion] Short title"));
}

void TestFeedbackReport::buildsSupportMarkdown() {
	Mumble::Feedback::ReportFields fields = baseFields(MumbleProto::FeedbackReportSupport);

	const QString body = Mumble::Feedback::issueBody(fields, 60000, 200000);
	QVERIFY(body.contains(QStringLiteral("### Type\nSupport")));
	QVERIFY(body.contains(QStringLiteral("### Client environment")));
	QVERIFY(body.contains(QStringLiteral("- Connected server feedback: connected=yes")));
	QCOMPARE(Mumble::Feedback::issueTitle(fields), QStringLiteral("[Support] Short title"));
}

void TestFeedbackReport::includesPastedEvidence() {
	Mumble::Feedback::ReportFields fields = baseFields(MumbleProto::FeedbackReportBug);
	fields.pastedEvidence                 = QStringLiteral("#### pasted.txt\n```text\nhello\n```");

	const QString body = Mumble::Feedback::issueBody(fields, 60000, 200000);
	QVERIFY(body.contains(QStringLiteral("### Pasted evidence\n#### pasted.txt")));
	QVERIFY(body.contains(QStringLiteral("hello")));
}

void TestFeedbackReport::redactsDiagnosticsAndCapsBytes() {
	const QString redacted = Mumble::Feedback::redactedDiagnostics(
		QStringLiteral("ok 192.168.1.20\nAuthorization: Bearer abc.def\npassword=hunter2\n")
			+ QString(200, QLatin1Char('x')), 80);

	QVERIFY(redacted.toUtf8().size() <= 80);
	QVERIFY(redacted.contains(QStringLiteral("[diagnostics truncated]")));
	QVERIFY(!redacted.contains(QStringLiteral("hunter2")));
	QVERIFY(!redacted.contains(QStringLiteral("abc.def")));
	QVERIFY(!redacted.contains(QStringLiteral("192.168.1.20")));
}

void TestFeedbackReport::splitsLabels() {
	QCOMPARE(Mumble::Feedback::splitLabels(QStringLiteral("triage, in-app-feedback\nbug,triage")),
			 QStringList({ QStringLiteral("triage"), QStringLiteral("in-app-feedback"), QStringLiteral("bug") }));
}

QTEST_MAIN(TestFeedbackReport)
#include "TestFeedbackReport.moc"
