// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementPolicy.h"

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>

#include <openssl/evp.h>
#include <openssl/opensslv.h>

#include <cmath>
#include <limits>
#include <utility>

#ifndef MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX
#	define MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX ""
#endif

#if OPENSSL_VERSION_NUMBER >= 0x10101000L && !defined(LIBRESSL_VERSION_NUMBER)
#	define MUMBLE_INPUT_ENHANCEMENT_HAS_ED25519 1
#else
#	define MUMBLE_INPUT_ENHANCEMENT_HAS_ED25519 0
#endif

namespace Mumble::InputEnhancement {
namespace {
	constexpr qsizetype Ed25519PublicKeyBytes               = 32;
	constexpr qsizetype Ed25519SignatureBytes               = 64;
	constexpr double MaximumExactlyRepresentableJsonInteger = 9007199254740991.0; // 2^53 - 1

	const QStringList &policyFieldNames() {
		static const QStringList fields = {
			QStringLiteral("available"), QStringLiteral("expiresAt"),        QStringLiteral("forceOriginal"),
			QStringLiteral("minBuild"),  QStringLiteral("recipeSetVersion"), QStringLiteral("recommendedProfile"),
		};
		return fields;
	}

	QString profileName(Profile profile) {
		switch (profile) {
			case Profile::Original:
				return QStringLiteral("Original");
			case Profile::Light:
				return QStringLiteral("Light");
			case Profile::Balanced:
				return QStringLiteral("Balanced");
			case Profile::Quality:
				return QStringLiteral("Quality");
			case Profile::Auto:
				return QStringLiteral("Auto");
			case Profile::VoiceFocus:
				// Voice Focus deliberately remains an explicit user choice. A
				// signed channel policy may disable enhancement or recommend one
				// of the adaptive/core profiles, but must not opt a user into the
				// aggressive manual-only profile.
				return {};
		}
		return {};
	}

	bool parseProfile(const QString &name, Profile &profile) {
		if (name == QLatin1String("Original")) {
			profile = Profile::Original;
		} else if (name == QLatin1String("Light")) {
			profile = Profile::Light;
		} else if (name == QLatin1String("Balanced")) {
			profile = Profile::Balanced;
		} else if (name == QLatin1String("Quality") || name == QLatin1String("Crisp")) {
			profile = Profile::Quality;
		} else if (name == QLatin1String("Auto")) {
			profile = Profile::Auto;
		} else {
			return false;
		}
		return true;
	}

	bool validRecipeSetVersion(const QString &version) {
		if (version.isEmpty() || version.size() > 64 || !version.front().isLetterOrNumber()) {
			return false;
		}
		for (const QChar character : version) {
			if (!character.isLetterOrNumber() && character != QLatin1Char('.') && character != QLatin1Char('_')
				&& character != QLatin1Char('-')) {
				return false;
			}
			if (character.unicode() > 0x7f) {
				return false;
			}
		}
		return true;
	}

	QByteArray quotedJsonString(const QString &value) {
		QJsonArray array;
		array.append(value);
		QByteArray encoded = QJsonDocument(array).toJson(QJsonDocument::Compact);
		if (encoded.size() < 2 || encoded.front() != '[' || encoded.back() != ']') {
			return {};
		}
		return encoded.mid(1, encoded.size() - 2);
	}

	QString canonicalExpiration(const QDateTime &expiresAt) {
		if (!expiresAt.isValid()) {
			return {};
		}
		return expiresAt.toUTC().toString(QStringLiteral("yyyy-MM-dd'T'HH:mm:ss'Z'"));
	}

	struct ParsedPolicy final {
		PolicyManifest manifest;
		PolicyRejection rejection = PolicyRejection::None;
	};

	ParsedPolicy parseCanonicalPolicy(const QByteArray &bytes) {
		QJsonParseError parseError;
		const QJsonDocument document = QJsonDocument::fromJson(bytes, &parseError);
		if (parseError.error != QJsonParseError::NoError) {
			return { {}, PolicyRejection::InvalidJson };
		}
		if (!document.isObject()) {
			return { {}, PolicyRejection::InvalidRoot };
		}

		const QJsonObject object       = document.object();
		const QStringList &knownFields = policyFieldNames();
		for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
			if (!knownFields.contains(iterator.key())) {
				return { {}, PolicyRejection::UnknownField };
			}
		}
		for (const QString &field : knownFields) {
			if (!object.contains(field)) {
				return { {}, PolicyRejection::MissingField };
			}
		}

		const QJsonValue availableValue  = object.value(QStringLiteral("available"));
		const QJsonValue forceValue      = object.value(QStringLiteral("forceOriginal"));
		const QJsonValue profileValue    = object.value(QStringLiteral("recommendedProfile"));
		const QJsonValue recipeValue     = object.value(QStringLiteral("recipeSetVersion"));
		const QJsonValue minBuildValue   = object.value(QStringLiteral("minBuild"));
		const QJsonValue expirationValue = object.value(QStringLiteral("expiresAt"));
		if (!availableValue.isBool() || !forceValue.isBool() || !profileValue.isString() || !recipeValue.isString()
			|| !minBuildValue.isDouble() || !expirationValue.isString()) {
			return { {}, PolicyRejection::InvalidFieldType };
		}

		PolicyManifest manifest;
		manifest.available     = availableValue.toBool();
		manifest.forceOriginal = forceValue.toBool();
		if (!parseProfile(profileValue.toString(), manifest.recommendedProfile)) {
			return { {}, PolicyRejection::InvalidFieldValue };
		}
		manifest.recipeSetVersion = recipeValue.toString();
		if (!validRecipeSetVersion(manifest.recipeSetVersion)) {
			return { {}, PolicyRejection::InvalidFieldValue };
		}

		const double minBuild = minBuildValue.toDouble(-1.0);
		if (!std::isfinite(minBuild) || minBuild < 0.0 || minBuild > MaximumExactlyRepresentableJsonInteger
			|| std::floor(minBuild) != minBuild) {
			return { {}, PolicyRejection::InvalidFieldValue };
		}
		manifest.minBuild = static_cast< std::uint64_t >(minBuild);

		const QString expirationText     = expirationValue.toString();
		const QDateTime parsedExpiration = QDateTime::fromString(expirationText, Qt::ISODate);
		if (!parsedExpiration.isValid() || canonicalExpiration(parsedExpiration) != expirationText) {
			return { {}, PolicyRejection::InvalidFieldValue };
		}
		manifest.expiresAt = parsedExpiration.toUTC();

		if (canonicalPolicyBytes(manifest) != bytes) {
			return { {}, PolicyRejection::NonCanonicalManifest };
		}
		return { std::move(manifest), PolicyRejection::None };
	}
} // namespace

QString configuredPolicyPublicKeyHex() {
	const QString configured = QString::fromLatin1(MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX).toLower();
	if (configured.size() != Ed25519PublicKeyBytes * 2) {
		return {};
	}
	for (const QChar character : configured) {
		if (!((character >= QLatin1Char('0') && character <= QLatin1Char('9'))
			  || (character >= QLatin1Char('a') && character <= QLatin1Char('f')))) {
			return {};
		}
	}
	return configured;
}

QByteArray configuredPolicyPublicKey() {
	const QString hex = configuredPolicyPublicKeyHex();
	if (hex.isEmpty()) {
		return {};
	}
	const QByteArray decoded = QByteArray::fromHex(hex.toLatin1());
	return decoded.size() == Ed25519PublicKeyBytes ? decoded : QByteArray{};
}

bool hasConfiguredPolicyPublicKey() {
	return configuredPolicyPublicKey().size() == Ed25519PublicKeyBytes;
}

QByteArray configuredPolicyBuildIdentity(const std::uint64_t currentBuild) {
	const QByteArray publicKey = configuredPolicyPublicKey();
	QString verificationMode   = QStringLiteral("invalid");
	if (currentBuild > 0 && publicKey.size() == Ed25519PublicKeyBytes) {
		verificationMode = QStringLiteral("managed-signed");
	} else if (currentBuild == 0 && publicKey.isEmpty()) {
		verificationMode = QStringLiteral("unmanaged-build-zero");
	}

	const QString publicKeySha256 =
		publicKey.size() == Ed25519PublicKeyBytes
			? QString::fromLatin1(QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256).toHex())
			: QString{};
	const QJsonObject identity{
		{ QStringLiteral("buildNumber"), static_cast< qint64 >(currentBuild) },
		{ QStringLiteral("configuredPublicKeySha256"), publicKeySha256 },
		{ QStringLiteral("kind"), QStringLiteral("mumble-input-enhancement-build-identity") },
		{ QStringLiteral("packageVerificationMode"), verificationMode },
		{ QStringLiteral("schemaVersion"), 1 },
	};
	return QJsonDocument(identity).toJson(QJsonDocument::Compact);
}

bool verifyWithConfiguredPolicyPublicKey(const QByteArray &exactBytes, const QByteArray &detachedSignature) noexcept {
	try {
		const QByteArray publicKey = configuredPolicyPublicKey();
		OpenSslEd25519Verifier verifier;
		return publicKey.size() == Ed25519PublicKeyBytes && verifier.isAvailable()
			   && verifier.verify(publicKey, exactBytes, detachedSignature);
	} catch (...) {
		return false;
	}
}

QByteArray canonicalPolicyBytes(const PolicyManifest &manifest) {
	const QString recommendedProfile = profileName(manifest.recommendedProfile);
	const QString expiration         = canonicalExpiration(manifest.expiresAt);
	if (recommendedProfile.isEmpty() || expiration.isEmpty() || !validRecipeSetVersion(manifest.recipeSetVersion)
		|| manifest.minBuild > static_cast< std::uint64_t >(MaximumExactlyRepresentableJsonInteger)) {
		return {};
	}

	QByteArray bytes;
	bytes.reserve(192);
	bytes += "{\"available\":";
	bytes += manifest.available ? "true" : "false";
	bytes += ",\"expiresAt\":";
	bytes += quotedJsonString(expiration);
	bytes += ",\"forceOriginal\":";
	bytes += manifest.forceOriginal ? "true" : "false";
	bytes += ",\"minBuild\":";
	bytes += QByteArray::number(manifest.minBuild);
	bytes += ",\"recipeSetVersion\":";
	bytes += quotedJsonString(manifest.recipeSetVersion);
	bytes += ",\"recommendedProfile\":";
	bytes += quotedJsonString(recommendedProfile);
	bytes += '}';
	return bytes;
}

bool OpenSslEd25519Verifier::isAvailable() const noexcept {
#if MUMBLE_INPUT_ENHANCEMENT_HAS_ED25519
	return true;
#else
	return false;
#endif
}

bool OpenSslEd25519Verifier::verify(const QByteArray &rawPublicKey, const QByteArray &canonicalManifest,
									const QByteArray &detachedSignature) const noexcept {
#if MUMBLE_INPUT_ENHANCEMENT_HAS_ED25519
	if (rawPublicKey.size() != Ed25519PublicKeyBytes || detachedSignature.size() != Ed25519SignatureBytes) {
		return false;
	}

	EVP_PKEY *key = EVP_PKEY_new_raw_public_key(EVP_PKEY_ED25519, nullptr,
												reinterpret_cast< const unsigned char * >(rawPublicKey.constData()),
												static_cast< std::size_t >(rawPublicKey.size()));
	if (!key) {
		return false;
	}
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	if (!context) {
		EVP_PKEY_free(key);
		return false;
	}

	const bool initialized = EVP_DigestVerifyInit(context, nullptr, nullptr, nullptr, key) == 1;
	const bool verified =
		initialized
		&& EVP_DigestVerify(context, reinterpret_cast< const unsigned char * >(detachedSignature.constData()),
							static_cast< std::size_t >(detachedSignature.size()),
							reinterpret_cast< const unsigned char * >(canonicalManifest.constData()),
							static_cast< std::size_t >(canonicalManifest.size()))
			   == 1;
	EVP_MD_CTX_free(context);
	EVP_PKEY_free(key);
	return verified;
#else
	Q_UNUSED(rawPublicKey);
	Q_UNUSED(canonicalManifest);
	Q_UNUSED(detachedSignature);
	return false;
#endif
}

SignedPolicyStore::SignedPolicyStore(QByteArray rawPublicKey,
									 std::shared_ptr< const DetachedSignatureVerifier > verifier,
									 std::uint64_t currentBuild, QString supportedRecipeSetVersion,
									 qint64 maximumFutureValiditySeconds)
	: m_rawPublicKey(std::move(rawPublicKey)), m_verifier(std::move(verifier)), m_currentBuild(currentBuild),
	  m_supportedRecipeSetVersion(std::move(supportedRecipeSetVersion)),
	  m_maximumFutureValiditySeconds(maximumFutureValiditySeconds) {
}

PolicyDecision SignedPolicyStore::accept(const QByteArray &canonicalManifest, const QByteArray &detachedSignature,
										 const QDateTime &nowUtc) {
	if (!nowUtc.isValid()) {
		return rejectedDecision(PolicyRejection::InvalidClock, nowUtc);
	}
	if (m_rawPublicKey.size() != Ed25519PublicKeyBytes) {
		return rejectedDecision(PolicyRejection::InvalidPublicKey, nowUtc);
	}
	if (!m_verifier || !m_verifier->isAvailable()) {
		return rejectedDecision(PolicyRejection::CryptoUnavailable, nowUtc);
	}

	const ParsedPolicy parsed = parseCanonicalPolicy(canonicalManifest);
	if (parsed.rejection != PolicyRejection::None) {
		return rejectedDecision(parsed.rejection, nowUtc);
	}
	if (detachedSignature.size() != Ed25519SignatureBytes
		|| !m_verifier->verify(m_rawPublicKey, canonicalManifest, detachedSignature)) {
		return rejectedDecision(PolicyRejection::InvalidSignature, nowUtc);
	}

	const PolicyRejection applicability = evaluateApplicability(parsed.manifest, nowUtc);
	if (applicability != PolicyRejection::None) {
		return rejectedDecision(applicability, nowUtc);
	}

	m_cachedPolicy = CachedPolicy{ parsed.manifest, canonicalManifest, detachedSignature };
	return { parsed.manifest, PolicyRejection::None, true, false, true };
}

PolicyDecision SignedPolicyStore::current(const QDateTime &nowUtc) const {
	if (!nowUtc.isValid()) {
		return rejectedDecision(PolicyRejection::InvalidClock, nowUtc);
	}
	if (!m_cachedPolicy) {
		return { safeOriginalPolicy(), PolicyRejection::None, false, false, false };
	}
	const PolicyRejection applicability = evaluateApplicability(m_cachedPolicy->manifest, nowUtc);
	if (applicability != PolicyRejection::None) {
		return { safeOriginalPolicy(), applicability, false, false, false };
	}
	return { m_cachedPolicy->manifest, PolicyRejection::None, false, true, true };
}

bool SignedPolicyStore::hasCachedPolicy() const noexcept {
	return m_cachedPolicy.has_value();
}

QByteArray SignedPolicyStore::cachedManifestBytes() const {
	return m_cachedPolicy ? m_cachedPolicy->canonicalManifest : QByteArray{};
}

QByteArray SignedPolicyStore::cachedSignature() const {
	return m_cachedPolicy ? m_cachedPolicy->signature : QByteArray{};
}

void SignedPolicyStore::clearCache() noexcept {
	m_cachedPolicy.reset();
}

PolicyDecision SignedPolicyStore::rejectedDecision(PolicyRejection rejection, const QDateTime &nowUtc) const {
	if (m_cachedPolicy && evaluateApplicability(m_cachedPolicy->manifest, nowUtc) == PolicyRejection::None) {
		return { m_cachedPolicy->manifest, rejection, false, true, true };
	}
	return { safeOriginalPolicy(), rejection, false, false, false };
}

PolicyRejection SignedPolicyStore::evaluateApplicability(const PolicyManifest &manifest,
														 const QDateTime &nowUtc) const {
	if (!nowUtc.isValid()) {
		return PolicyRejection::InvalidClock;
	}
	const QDateTime normalizedNow = nowUtc.toUTC();
	if (!manifest.expiresAt.isValid() || manifest.expiresAt <= normalizedNow) {
		return PolicyRejection::Expired;
	}
	if (m_maximumFutureValiditySeconds <= 0
		|| normalizedNow.secsTo(manifest.expiresAt) > m_maximumFutureValiditySeconds) {
		return PolicyRejection::ExpirationTooFarInFuture;
	}
	if (manifest.minBuild > m_currentBuild) {
		return PolicyRejection::MinimumBuildNotMet;
	}
	if (manifest.recipeSetVersion != m_supportedRecipeSetVersion) {
		return PolicyRejection::RecipeSetMismatch;
	}
	return PolicyRejection::None;
}

PolicyManifest SignedPolicyStore::safeOriginalPolicy() {
	PolicyManifest policy;
	policy.available          = false;
	policy.forceOriginal      = true;
	policy.recommendedProfile = Profile::Original;
	policy.recipeSetVersion.clear();
	policy.minBuild  = 0;
	policy.expiresAt = {};
	return policy;
}

} // namespace Mumble::InputEnhancement
