// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license.

#include "ServerLogRedaction.h"

#include <QtTest>

class TestServerLogRedaction : public QObject {
	Q_OBJECT

private slots:
	void redactsLegacySuperUserBootstrapPassword();
	void redactsCommonCredentialShapes();
	void preservesOrdinaryLogText();
};

void TestServerLogRedaction::redactsLegacySuperUserBootstrapPassword() {
	const QString secret   = QStringLiteral("do-not-persist-this-secret");
	const QString input    = QStringLiteral("Initialized 'SuperUser' password on server 42 to '%1'").arg(secret);
	const QString redacted = Mumble::ServerLog::redactSensitiveText(input);

	QCOMPARE(redacted, Mumble::ServerLog::superUserBootstrapNotice(42));
	QVERIFY(!redacted.contains(secret));
	QVERIFY(!redacted.contains(QStringLiteral(" to '")));
	QVERIFY(redacted.contains(QStringLiteral("--read-su-pw")));
}

void TestServerLogRedaction::redactsCommonCredentialShapes() {
	const QString input    = QStringLiteral("password=hunter2 token: \"token-value\" api_key='api-value' "
											   "serverpassword=server-value refresh_token=refresh-value "
											   "Authorization: Digest digest-value\nCookie: session=cookie-value\n"
											   R"(json={"client_secret":"json-value","credential":"credential-value"} )"
											   "--token cli-value --set-su-pw positional-value 0 "
											   "database=postgres://operator:uri-value@example.invalid/mumble "
											   "url=https://example.invalid/?client_secret=query-value&room=7");
	const QString redacted = Mumble::ServerLog::redactSensitiveText(input);

	for (const QString &secret :
		 { QStringLiteral("hunter2"), QStringLiteral("token-value"), QStringLiteral("api-value"),
		   QStringLiteral("server-value"), QStringLiteral("refresh-value"), QStringLiteral("digest-value"),
		   QStringLiteral("cookie-value"), QStringLiteral("json-value"), QStringLiteral("credential-value"),
		   QStringLiteral("cli-value"), QStringLiteral("positional-value"), QStringLiteral("uri-value"),
		   QStringLiteral("query-value") }) {
		QVERIFY2(!redacted.contains(secret), qPrintable(QStringLiteral("Secret remained in: %1").arg(redacted)));
	}
	QVERIFY(redacted.count(QStringLiteral("[redacted]")) >= 13);
	QVERIFY(redacted.contains(QStringLiteral("&room=7")));
	QVERIFY(redacted.contains(QStringLiteral("postgres://operator:[redacted]@example.invalid/mumble")));
}

void TestServerLogRedaction::preservesOrdinaryLogText() {
	const QString input =
		QStringLiteral("User mentioned that the password was changed; no credential value was included.");
	QCOMPARE(Mumble::ServerLog::redactSensitiveText(input), input);
}

QTEST_GUILESS_MAIN(TestServerLogRedaction)
#include "TestServerLogRedaction.moc"
