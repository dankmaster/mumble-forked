// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_INPUTENHANCEMENTPOLICY_H_
#define MUMBLE_MUMBLE_INPUTENHANCEMENTPOLICY_H_

#include "InputEnhancement.h"

#include <QByteArray>
#include <QDateTime>
#include <QString>

#include <cstdint>
#include <memory>
#include <optional>

namespace Mumble::InputEnhancement {

/// Returns the release public key compiled into this client. Developer builds
/// intentionally return an empty array unless explicitly configured; policy
/// verification then remains fail-closed.
QByteArray configuredPolicyPublicKey();
QString configuredPolicyPublicKeyHex();
bool hasConfiguredPolicyPublicKey();

/// Emits the exact machine-readable identity used by the pre-Azure rehearsal
/// to prove that the launched candidate has a positive build number and the
/// expected Ed25519 public key compiled into its package verifier. The result
/// contains no private material and is deliberately independent of
/// Authenticode, which may be applied after the program was built.
QByteArray configuredPolicyBuildIdentity(std::uint64_t currentBuild);

/// Verifies exact bytes with the release key compiled into this client. An
/// unconfigured developer build, malformed signature or unavailable crypto
/// backend returns false.
bool verifyWithConfiguredPolicyPublicKey(const QByteArray &exactBytes, const QByteArray &detachedSignature) noexcept;

/// The six fields in the signed channel policy. The signature is deliberately
/// not part of this object: it is transported and stored as detached bytes.
struct PolicyManifest final {
	bool available             = false;
	bool forceOriginal         = true;
	Profile recommendedProfile = Profile::Original;
	QString recipeSetVersion;
	std::uint64_t minBuild = 0;
	QDateTime expiresAt;
};

enum class PolicyRejection : std::uint8_t {
	None,
	InvalidClock,
	InvalidPublicKey,
	CryptoUnavailable,
	InvalidJson,
	InvalidRoot,
	UnknownField,
	MissingField,
	InvalidFieldType,
	InvalidFieldValue,
	NonCanonicalManifest,
	InvalidSignature,
	Expired,
	ExpirationTooFarInFuture,
	MinimumBuildNotMet,
	RecipeSetMismatch
};

/// Produces the exact UTF-8 bytes that are signed. Keys are emitted in lexical
/// order, JSON is compact, timestamps are UTC with whole-second precision and
/// no trailing newline is present. Returns an empty byte array for a manifest
/// that cannot be represented by the strict schema.
QByteArray canonicalPolicyBytes(const PolicyManifest &manifest);

/// Abstract detached-signature verification makes the policy parser testable
/// without a private key and keeps cryptography outside policy semantics.
class DetachedSignatureVerifier {
public:
	virtual ~DetachedSignatureVerifier() = default;

	virtual bool isAvailable() const noexcept                               = 0;
	virtual bool verify(const QByteArray &rawPublicKey, const QByteArray &canonicalManifest,
						const QByteArray &detachedSignature) const noexcept = 0;
};

/// OpenSSL's one-shot Ed25519 verifier. It expects a 32-byte raw public key and
/// a 64-byte raw signature. On OpenSSL versions without the required API,
/// isAvailable() and verify() safely return false.
class OpenSslEd25519Verifier final : public DetachedSignatureVerifier {
public:
	bool isAvailable() const noexcept override;
	bool verify(const QByteArray &rawPublicKey, const QByteArray &canonicalManifest,
				const QByteArray &detachedSignature) const noexcept override;
};

struct PolicyDecision final {
	PolicyManifest effectivePolicy;
	PolicyRejection rejection = PolicyRejection::None;
	bool candidateAccepted    = false;
	bool usingCachedPolicy    = false;
	bool hasVerifiedPolicy    = false;
};

/// Client-local policy state with no network, voice-protocol or server feature
/// dependencies. Invalid candidates never replace the last accepted policy.
/// If neither the candidate nor the cache is currently usable, the effective
/// result is a fail-closed policy that forces Original.
class SignedPolicyStore final {
public:
	static constexpr qint64 defaultMaximumFutureValiditySeconds = 31 * 24 * 60 * 60;

	SignedPolicyStore(QByteArray rawPublicKey, std::shared_ptr< const DetachedSignatureVerifier > verifier,
					  std::uint64_t currentBuild, QString supportedRecipeSetVersion,
					  qint64 maximumFutureValiditySeconds = defaultMaximumFutureValiditySeconds);

	SignedPolicyStore(const SignedPolicyStore &)            = delete;
	SignedPolicyStore(SignedPolicyStore &&)                 = default;
	SignedPolicyStore &operator=(const SignedPolicyStore &) = delete;
	SignedPolicyStore &operator=(SignedPolicyStore &&)      = default;

	/// Verifies and evaluates a downloaded or persisted candidate. The caller
	/// supplies raw canonical JSON and raw detached-signature bytes. Calling this
	/// method for a persisted entry is the supported cache-restore path.
	PolicyDecision accept(const QByteArray &canonicalManifest, const QByteArray &detachedSignature,
						  const QDateTime &nowUtc);

	/// Re-evaluates the in-memory cache at the supplied time. Expired cache data
	/// is never reactivated as an effective policy.
	PolicyDecision current(const QDateTime &nowUtc) const;

	bool hasCachedPolicy() const noexcept;
	QByteArray cachedManifestBytes() const;
	QByteArray cachedSignature() const;
	void clearCache() noexcept;

private:
	struct CachedPolicy final {
		PolicyManifest manifest;
		QByteArray canonicalManifest;
		QByteArray signature;
	};

	PolicyDecision rejectedDecision(PolicyRejection rejection, const QDateTime &nowUtc) const;
	PolicyRejection evaluateApplicability(const PolicyManifest &manifest, const QDateTime &nowUtc) const;
	static PolicyManifest safeOriginalPolicy();

	QByteArray m_rawPublicKey;
	std::shared_ptr< const DetachedSignatureVerifier > m_verifier;
	std::uint64_t m_currentBuild;
	QString m_supportedRecipeSetVersion;
	qint64 m_maximumFutureValiditySeconds;
	std::optional< CachedPolicy > m_cachedPolicy;
};

} // namespace Mumble::InputEnhancement

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENTPOLICY_H_
