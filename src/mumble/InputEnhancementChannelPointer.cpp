// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementChannelPointer.h"

#include <QDateTime>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>
#include <QStringList>
#include <QUrl>

#include <cmath>
#include <limits>

namespace Mumble::InputEnhancement {
namespace {
	constexpr qsizetype MaximumPointerBytes  = 64 * 1024;
	constexpr qsizetype Ed25519PublicKeySize = 32;
	constexpr qsizetype Ed25519SignatureSize = 64;

	ChannelPointerDecision reject(ChannelPointerRejection rejection, const QString &detail) {
		return { {}, rejection, detail, false };
	}

	bool exactFields(const QJsonObject &object, const QSet< QString > &fields) {
		if (object.size() != fields.size()) {
			return false;
		}
		for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
			if (!fields.contains(iterator.key())) {
				return false;
			}
		}
		return true;
	}

	bool exactInteger(const QJsonValue &value, qint64 minimum, qint64 maximum, qint64 &result) {
		if (!value.isDouble()) {
			return false;
		}
		const double number = value.toDouble();
		if (!std::isfinite(number) || std::floor(number) != number || number < static_cast< double >(minimum)
			|| number > static_cast< double >(maximum)) {
			return false;
		}
		result = static_cast< qint64 >(number);
		return true;
	}

	bool matches(const QString &value, const QString &pattern) {
		const QRegularExpressionMatch match = QRegularExpression(pattern).match(value);
		return match.hasMatch() && match.capturedStart() == 0 && match.capturedLength() == value.size();
	}

	bool sha256(const QJsonValue &value) {
		return value.isString() && matches(value.toString(), QStringLiteral("^[0-9a-f]{64}$"));
	}

	struct GitHubReleaseAssetUrl final {
		QUrl url;
		QString repository;
		QString tag;
		QString fileName;
	};

	bool parseGitHubReleaseAssetUrl(const QJsonValue &value, GitHubReleaseAssetUrl &result) {
		if (!value.isString()) {
			return false;
		}

		// Match the textual URL as well as parsing it with QUrl. This deliberately
		// excludes credentials, ports, queries, fragments, percent-encoded path
		// separators, and lookalike hosts from the signed update boundary.
		static const QRegularExpression releaseAssetPattern(
			QStringLiteral("^https://github[.]com/([A-Za-z0-9_.-]+)/([A-Za-z0-9_.-]+)/releases/download/"
						   "([A-Za-z0-9._-]+)/([A-Za-z0-9._-]+)$"));
		const QRegularExpressionMatch match = releaseAssetPattern.match(value.toString());
		if (!match.hasMatch() || match.capturedStart() != 0 || match.capturedLength() != value.toString().size()
			|| match.captured(1) == QLatin1String(".") || match.captured(1) == QLatin1String("..")
			|| match.captured(2) == QLatin1String(".") || match.captured(2) == QLatin1String("..")) {
			return false;
		}

		const QUrl url(value.toString(), QUrl::StrictMode);
		if (!url.isValid() || url.scheme() != QLatin1String("https") || url.host() != QLatin1String("github.com")
			|| !url.userInfo().isEmpty() || url.port(-1) != -1 || url.hasQuery() || url.hasFragment()) {
			return false;
		}

		result.url        = url;
		result.repository = match.captured(1) + QLatin1Char('/') + match.captured(2);
		result.tag        = match.captured(3);
		result.fileName   = match.captured(4);
		return true;
	}
} // namespace

ChannelPointerDecision verifyAndNormalizeChannelPointer(const QByteArray &exactPointerBytes,
														const QByteArray &detachedSignature,
														const QByteArray &rawPublicKey,
														const DetachedSignatureVerifier &verifier,
														const QString &expectedChannel) noexcept {
	try {
		if (exactPointerBytes.isEmpty() || exactPointerBytes.size() > MaximumPointerBytes) {
			return reject(ChannelPointerRejection::TooLarge, QStringLiteral("Channel pointer size is invalid"));
		}
		if (rawPublicKey.size() != Ed25519PublicKeySize) {
			return reject(ChannelPointerRejection::InvalidPublicKey,
						  QStringLiteral("Channel pointer public key is invalid"));
		}
		if (!verifier.isAvailable()) {
			return reject(ChannelPointerRejection::CryptoUnavailable,
						  QStringLiteral("Ed25519 verification is unavailable"));
		}
		if (detachedSignature.size() != Ed25519SignatureSize
			|| !verifier.verify(rawPublicKey, exactPointerBytes, detachedSignature)) {
			return reject(ChannelPointerRejection::InvalidSignature,
						  QStringLiteral("Channel pointer signature is invalid"));
		}

		QJsonParseError parseError;
		const QJsonDocument document = QJsonDocument::fromJson(exactPointerBytes, &parseError);
		if (parseError.error != QJsonParseError::NoError || !document.isObject()) {
			return reject(ChannelPointerRejection::InvalidJson, QStringLiteral("Channel pointer is not a JSON object"));
		}
		const QJsonObject root = document.object();
		qint64 schemaVersion   = 0;
		if (!exactInteger(root.value(QStringLiteral("schemaVersion")), 1, 2, schemaVersion)) {
			return reject(ChannelPointerRejection::InvalidSchema,
						  QStringLiteral("Channel pointer schema version is invalid"));
		}
		QSet< QString > rootFields = { QStringLiteral("schemaVersion"),
											 QStringLiteral("channel"),
											 QStringLiteral("channelTag"),
											 QStringLiteral("immutableTag"),
											 QStringLiteral("buildId"),
											 QStringLiteral("buildNumber"),
											 QStringLiteral("sourceSha"),
											 QStringLiteral("artifact"),
											 QStringLiteral("qualification"),
											 QStringLiteral("releaseSmoke"),
											 QStringLiteral("modelManifestSha256"),
											 QStringLiteral("recipeManifestSha256"),
											 QStringLiteral("inputEnhancementPolicy"),
											 QStringLiteral("detachedSignature"),
											 QStringLiteral("knownGoodTags"),
											 QStringLiteral("announcement"),
											 QStringLiteral("promotedAtUtc") };
		if (schemaVersion == 2) {
			rootFields.insert(QStringLiteral("installer"));
			rootFields.insert(QStringLiteral("recoveryInstallers"));
		}
		qint64 buildNumber               = 0;
		if (!exactFields(root, rootFields)
			|| !exactInteger(root.value(QStringLiteral("buildNumber")), 1, std::numeric_limits< int >::max(),
							 buildNumber)) {
			return reject(ChannelPointerRejection::InvalidSchema,
						  QStringLiteral("Channel pointer root schema is invalid"));
		}

		const QString channel      = root.value(QStringLiteral("channel")).toString();
		const QString channelTag   = root.value(QStringLiteral("channelTag")).toString();
		const QString immutableTag = root.value(QStringLiteral("immutableTag")).toString();
		const QString buildId      = root.value(QStringLiteral("buildId")).toString();
		const QString sourceSha    = root.value(QStringLiteral("sourceSha")).toString();
		if ((expectedChannel != QLatin1String("stable") && expectedChannel != QLatin1String("preview"))
			|| channel != expectedChannel || channelTag != QStringLiteral("mumble-forked-%1").arg(channel)) {
			return reject(ChannelPointerRejection::WrongChannel,
						  QStringLiteral("Channel pointer belongs to a different channel"));
		}
		const QString expectedBuildTag =
			QStringLiteral("mumble-forked-build-%1-%2").arg(buildNumber).arg(sourceSha.left(12));
		if (immutableTag != buildId || immutableTag != expectedBuildTag
			|| !matches(immutableTag, QStringLiteral("^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$"))
			|| !matches(sourceSha, QStringLiteral("^[0-9a-f]{40}$"))
			|| !sha256(root.value(QStringLiteral("modelManifestSha256")))
			|| !sha256(root.value(QStringLiteral("recipeManifestSha256")))) {
			return reject(ChannelPointerRejection::InvalidSchema,
						  QStringLiteral("Channel pointer build identity is invalid"));
		}

		const QJsonObject artifact           = root.value(QStringLiteral("artifact")).toObject();
		const QSet< QString > artifactFields = { QStringLiteral("fileName"), QStringLiteral("sha256"),
												 QStringLiteral("size"), QStringLiteral("url") };
		qint64 artifactSize                  = 0;
		GitHubReleaseAssetUrl artifactUrl;
		const QString artifactName = artifact.value(QStringLiteral("fileName")).toString();
		if (!exactFields(artifact, artifactFields) || !sha256(artifact.value(QStringLiteral("sha256")))
			|| !exactInteger(artifact.value(QStringLiteral("size")), 1, 4LL * 1024LL * 1024LL * 1024LL, artifactSize)
			|| !parseGitHubReleaseAssetUrl(artifact.value(QStringLiteral("url")), artifactUrl)
			|| !matches(artifactName, QStringLiteral("^[A-Za-z0-9._-]+\\.mumble-update$"))
			|| artifactUrl.fileName != artifactName || artifactUrl.tag != immutableTag) {
			return reject(ChannelPointerRejection::UnsafeArtifact,
						  QStringLiteral("Channel pointer artifact is invalid"));
		}

		QJsonObject normalizedInstaller;
		QJsonArray normalizedRecoveryInstallers;
		QStringList recoveryTags;
		if (schemaVersion == 2) {
			const QJsonObject installer = root.value(QStringLiteral("installer")).toObject();
			const QSet< QString > installerFields = { QStringLiteral("fileName"), QStringLiteral("sha256"),
												QStringLiteral("size"), QStringLiteral("url"),
												QStringLiteral("executableSha256") };
			qint64 installerSize = 0;
			GitHubReleaseAssetUrl installerUrl;
			const QString installerName = installer.value(QStringLiteral("fileName")).toString();
			if (!exactFields(installer, installerFields) || !sha256(installer.value(QStringLiteral("sha256")))
				|| !sha256(installer.value(QStringLiteral("executableSha256")))
				|| !exactInteger(installer.value(QStringLiteral("size")), 1,
								 4LL * 1024LL * 1024LL * 1024LL, installerSize)
				|| !parseGitHubReleaseAssetUrl(installer.value(QStringLiteral("url")), installerUrl)
				|| !matches(installerName, QStringLiteral("^mumble-forked-[A-Za-z0-9._-]+\\.msi$"))
				|| installerUrl.repository != artifactUrl.repository || installerUrl.tag != immutableTag
				|| installerUrl.fileName != installerName) {
				return reject(ChannelPointerRejection::UnsafeArtifact,
							  QStringLiteral("Channel pointer candidate installer is invalid"));
			}
			normalizedInstaller = installer;

			const QJsonArray recovery = root.value(QStringLiteral("recoveryInstallers")).toArray();
			if (recovery.size() != 2) {
				return reject(ChannelPointerRejection::InvalidSchema,
							  QStringLiteral("Channel pointer must contain two recovery installers"));
			}
			QSet< QString > uniqueRecoveryTags;
			for (const QJsonValue &value : recovery) {
				const QJsonObject entry = value.toObject();
				const QSet< QString > fields = { QStringLiteral("immutableTag"), QStringLiteral("fileName"),
												QStringLiteral("sha256"), QStringLiteral("size"),
												QStringLiteral("url") };
				qint64 size = 0;
				GitHubReleaseAssetUrl url;
				const QString tag      = entry.value(QStringLiteral("immutableTag")).toString();
				const QString fileName = entry.value(QStringLiteral("fileName")).toString();
				if (!exactFields(entry, fields)
					|| !matches(tag, QStringLiteral("^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$"))
					|| tag == immutableTag || uniqueRecoveryTags.contains(tag)
					|| !matches(fileName, QStringLiteral("^mumble-forked-[A-Za-z0-9._-]+\\.msi$"))
					|| !sha256(entry.value(QStringLiteral("sha256")))
					|| !exactInteger(entry.value(QStringLiteral("size")), 1,
									 4LL * 1024LL * 1024LL * 1024LL, size)
					|| !parseGitHubReleaseAssetUrl(entry.value(QStringLiteral("url")), url)
					|| url.repository != artifactUrl.repository || url.tag != tag || url.fileName != fileName) {
					return reject(ChannelPointerRejection::UnsafeArtifact,
								  QStringLiteral("Channel pointer recovery installer is invalid"));
				}
				uniqueRecoveryTags.insert(tag);
				recoveryTags.push_back(tag);
				normalizedRecoveryInstallers.push_back(entry);
			}
		}

		const QJsonObject qualification           = root.value(QStringLiteral("qualification")).toObject();
		const QSet< QString > qualificationFields = { QStringLiteral("sha256"), QStringLiteral("url") };
		GitHubReleaseAssetUrl qualificationUrl;
		if (!exactFields(qualification, qualificationFields) || !sha256(qualification.value(QStringLiteral("sha256")))
			|| !parseGitHubReleaseAssetUrl(qualification.value(QStringLiteral("url")), qualificationUrl)
			|| qualificationUrl.repository != artifactUrl.repository || qualificationUrl.tag != immutableTag
			|| qualificationUrl.fileName != QLatin1String("qualification.json")) {
			return reject(ChannelPointerRejection::InvalidSchema,
						  QStringLiteral("Channel pointer qualification reference is invalid"));
		}

		const QJsonObject releaseSmoke           = root.value(QStringLiteral("releaseSmoke")).toObject();
		const QSet< QString > releaseSmokeFields = { QStringLiteral("sha256"), QStringLiteral("url") };
		GitHubReleaseAssetUrl releaseSmokeUrl;
		if (!exactFields(releaseSmoke, releaseSmokeFields) || !sha256(releaseSmoke.value(QStringLiteral("sha256")))
			|| !parseGitHubReleaseAssetUrl(releaseSmoke.value(QStringLiteral("url")), releaseSmokeUrl)
			|| releaseSmokeUrl.repository != artifactUrl.repository || releaseSmokeUrl.tag != immutableTag
			|| releaseSmokeUrl.fileName != QLatin1String("release-smoke.json")) {
			return reject(ChannelPointerRejection::InvalidSchema,
						  QStringLiteral("Channel pointer release-smoke reference is invalid"));
		}

		const QJsonObject policy           = root.value(QStringLiteral("inputEnhancementPolicy")).toObject();
		const QSet< QString > policyFields = { QStringLiteral("fileName"), QStringLiteral("sha256"),
											   QStringLiteral("signatureFileName"), QStringLiteral("signatureSha256"),
											   QStringLiteral("url") };
		GitHubReleaseAssetUrl policyUrl;
		if (!exactFields(policy, policyFields) || !sha256(policy.value(QStringLiteral("sha256")))
			|| !sha256(policy.value(QStringLiteral("signatureSha256")))
			|| policy.value(QStringLiteral("fileName")).toString() != QLatin1String("input-enhancement-policy.json")
			|| policy.value(QStringLiteral("signatureFileName")).toString()
				   != QLatin1String("input-enhancement-policy.json.sig")
			|| !parseGitHubReleaseAssetUrl(policy.value(QStringLiteral("url")), policyUrl)
			|| policyUrl.repository != artifactUrl.repository || policyUrl.tag != channelTag
			|| policyUrl.fileName != QLatin1String("input-enhancement-policy.json")) {
			return reject(ChannelPointerRejection::InvalidSchema,
						  QStringLiteral("Channel pointer policy reference is invalid"));
		}

		const QJsonObject signature           = root.value(QStringLiteral("detachedSignature")).toObject();
		const QSet< QString > signatureFields = { QStringLiteral("algorithm"), QStringLiteral("encoding"),
												  QStringLiteral("fileName"), QStringLiteral("publicKeyHex") };
		if (!exactFields(signature, signatureFields)
			|| signature.value(QStringLiteral("algorithm")).toString() != QLatin1String("Ed25519")
			|| signature.value(QStringLiteral("encoding")).toString() != QLatin1String("raw")
			|| signature.value(QStringLiteral("fileName")).toString() != QLatin1String("channel-pointer.json.sig")
			|| signature.value(QStringLiteral("publicKeyHex")).toString()
				   != QString::fromLatin1(rawPublicKey.toHex())) {
			return reject(ChannelPointerRejection::InvalidSchema,
						  QStringLiteral("Channel pointer signature metadata is invalid"));
		}

		const QJsonArray knownGood = root.value(QStringLiteral("knownGoodTags")).toArray();
		const bool validKnownGoodCount = schemaVersion == 2 ? knownGood.size() == 3
														 : (!knownGood.isEmpty() && knownGood.size() <= 2);
		if (!validKnownGoodCount || knownGood.first().toString() != immutableTag) {
			return reject(ChannelPointerRejection::InvalidSchema,
						  QStringLiteral("Channel pointer recovery set is invalid"));
		}
		QSet< QString > uniqueTags;
		for (const QJsonValue &tag : knownGood) {
			if (!tag.isString()
				|| !matches(tag.toString(), QStringLiteral("^mumble-forked-build-[1-9][0-9]*-[0-9a-f]{12}$"))
				|| uniqueTags.contains(tag.toString())) {
				return reject(ChannelPointerRejection::InvalidSchema,
							  QStringLiteral("Channel pointer recovery tag is invalid"));
			}
			uniqueTags.insert(tag.toString());
		}
		if (schemaVersion == 2
			&& (knownGood.at(1).toString() != recoveryTags.at(0)
				|| knownGood.at(2).toString() != recoveryTags.at(1))) {
			return reject(ChannelPointerRejection::InvalidSchema,
						  QStringLiteral("Channel pointer recovery order is inconsistent"));
		}

		const QString rawAnnouncement = root.value(QStringLiteral("announcement")).toString();
		const QString announcement    = rawAnnouncement.trimmed();
		const QString promotedAtText  = root.value(QStringLiteral("promotedAtUtc")).toString();
		const QDateTime promotedAt    = QDateTime::fromString(promotedAtText, Qt::ISODateWithMs);
		if (announcement.isEmpty() || announcement != rawAnnouncement || announcement.size() > 2048
			|| !matches(promotedAtText,
						QStringLiteral("^[0-9]{4}-[0-9]{2}-[0-9]{2}T[0-9]{2}:[0-9]{2}:[0-9]{2}[.][0-9]{7}Z$"))
			|| !promotedAt.isValid() || promotedAt.offsetFromUtc() != 0) {
			return reject(ChannelPointerRejection::InvalidSchema,
						  QStringLiteral("Channel pointer presentation fields are invalid"));
		}

		QJsonObject package{ { QStringLiteral("format"), QStringLiteral("mumble-update-v1") },
							 { QStringLiteral("applyMode"), QStringLiteral("replace-staged-payload") },
							 { QStringLiteral("minUpdaterVersion"), schemaVersion == 2 ? 4 : 3 },
							 { QStringLiteral("fileName"), artifactName },
							 { QStringLiteral("url"), artifactUrl.url.toString() },
							 { QStringLiteral("sha256"), artifact.value(QStringLiteral("sha256")) },
							 { QStringLiteral("size"), static_cast< double >(artifactSize) } };
		QJsonObject info{
			{ QStringLiteral("build"), static_cast< int >(buildNumber) },
			{ QStringLiteral("commit"), sourceSha },
			{ QStringLiteral("announcement"), announcement },
			{ QStringLiteral("publishedAt"), promotedAt.toUTC().toString(Qt::ISODateWithMs) },
			{ QStringLiteral("preferredUpdate"), QStringLiteral("package") },
			{ QStringLiteral("releaseUrl"),
			  QStringLiteral("https://github.com/%1/releases/tag/%2").arg(artifactUrl.repository, immutableTag) },
			{ QStringLiteral("channel"), channel },
			{ QStringLiteral("immutableTag"), immutableTag },
			{ QStringLiteral("knownGoodTags"), knownGood },
			{ QStringLiteral("package"), package }
		};
		if (schemaVersion == 2) {
			info.insert(QStringLiteral("installer"), normalizedInstaller);
			info.insert(QStringLiteral("installerUrl"),
						normalizedInstaller.value(QStringLiteral("url")));
			info.insert(QStringLiteral("sha256"),
						normalizedInstaller.value(QStringLiteral("sha256")));
			info.insert(QStringLiteral("recoveryInstallers"), normalizedRecoveryInstallers);
		}
		return { info, ChannelPointerRejection::None, {}, true };
	} catch (...) {
		return reject(ChannelPointerRejection::InvalidSchema, QStringLiteral("Channel pointer verification failed"));
	}
}

} // namespace Mumble::InputEnhancement
