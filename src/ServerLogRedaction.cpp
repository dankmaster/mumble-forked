// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ServerLogRedaction.h"

#include <QRegularExpression>

namespace {
const QRegularExpression &legacySuperUserPasswordExpression() {
	static const QRegularExpression expression(
		QStringLiteral(R"(^\s*Initialized\s+'SuperUser'\s+password\s+on\s+server\s+(\d+)\s+to\s+'.*'\s*$)"),
		QRegularExpression::CaseInsensitiveOption | QRegularExpression::DotMatchesEverythingOption);
	return expression;
}

const QRegularExpression &authorizationExpression() {
	static const QRegularExpression expression(QStringLiteral(R"(\b((?:proxy-)?authorization\s*:\s*)[^\r\n]*)"),
											   QRegularExpression::CaseInsensitiveOption);
	return expression;
}

const QRegularExpression &cookieHeaderExpression() {
	static const QRegularExpression expression(QStringLiteral(R"(\b((?:set-cookie|cookie)\s*:\s*)[^\r\n]*)"),
											   QRegularExpression::CaseInsensitiveOption);
	return expression;
}

const QRegularExpression &quotedCredentialValueExpression() {
	static const QRegularExpression expression(
		QStringLiteral(
			R"((\"(?:[A-Za-z0-9_.-]*(?:password|passwd|passphrase|token|secret|credential)|api(?:[-_ ]?key)|private(?:[-_ ]?key)|cookie|set-cookie)\"\s*:\s*)(?:\"(?:\\.|[^\"\\])*\"|'(?:\\.|[^'\\])*'|[^,}\]\s]+))"),
		QRegularExpression::CaseInsensitiveOption);
	return expression;
}

const QRegularExpression &credentialValueExpression() {
	static const QRegularExpression expression(
		QStringLiteral(
			R"(\b((?:[A-Za-z0-9_.-]*(?:password|passwd|passphrase|token|secret|credential)|api(?:[-_ ]?key)|private(?:[-_ ]?key)|cookie|set-cookie)\s*[:=]\s*)(?:'[^'\r\n]*'|"[^"\r\n]*"|[^\s&;,\r\n]+))"),
		QRegularExpression::CaseInsensitiveOption);
	return expression;
}

const QRegularExpression &commandLineCredentialExpression() {
	static const QRegularExpression expression(
		QStringLiteral(
			R"(((?:--?|/)(?:[A-Za-z0-9_.-]*(?:password|passwd|passphrase|token|secret|credential)|api(?:[-_]?key)|private(?:[-_]?key)|set[-_]su[-_]pw)\s+)(?:'[^'\r\n]*'|"[^"\r\n]*"|[^\s\r\n]+))"),
		QRegularExpression::CaseInsensitiveOption);
	return expression;
}

const QRegularExpression &uriUserInfoExpression() {
	static const QRegularExpression expression(QStringLiteral(R"(\b([A-Za-z][A-Za-z0-9+.-]*://[^/@\s:]+:)[^/@\s]+(@))"),
											   QRegularExpression::CaseInsensitiveOption);
	return expression;
}
} // namespace

namespace Mumble {
namespace ServerLog {
	QString superUserBootstrapNotice(const unsigned int serverID) {
		return QStringLiteral("Initialized 'SuperUser' credentials on server %1; generated password omitted. "
							  "Set an explicit password with --read-su-pw before first login.")
			.arg(serverID);
	}

	QString redactSensitiveText(const QString &text) {
		const QRegularExpressionMatch legacyMatch = legacySuperUserPasswordExpression().match(text);
		if (legacyMatch.hasMatch()) {
			return superUserBootstrapNotice(legacyMatch.captured(1).toUInt());
		}

		QString redacted = text;
		redacted.replace(authorizationExpression(), QStringLiteral("\\1[redacted]"));
		redacted.replace(cookieHeaderExpression(), QStringLiteral("\\1[redacted]"));
		redacted.replace(quotedCredentialValueExpression(), QStringLiteral("\\1\"[redacted]\""));
		redacted.replace(credentialValueExpression(), QStringLiteral("\\1[redacted]"));
		redacted.replace(commandLineCredentialExpression(), QStringLiteral("\\1[redacted]"));
		redacted.replace(uriUserInfoExpression(), QStringLiteral("\\1[redacted]\\2"));
		return redacted;
	}
} // namespace ServerLog
} // namespace Mumble
