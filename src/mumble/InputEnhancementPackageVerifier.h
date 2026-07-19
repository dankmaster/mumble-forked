// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_INPUTENHANCEMENTPACKAGEVERIFIER_H_
#define MUMBLE_MUMBLE_INPUTENHANCEMENTPACKAGEVERIFIER_H_

#include <QByteArray>
#include <QDir>
#include <QHash>
#include <QString>
#include <QStringList>

#include <atomic>
#include <cstdint>
#include <future>
#include <memory>
#include <mutex>
#include <optional>

namespace Mumble::InputEnhancement {

class Recipe;
enum class CpuClass : std::uint8_t;
struct ResolveRequest;
struct ProfileReadiness;

enum class PackageVerificationError : std::uint8_t {
	None,
	MissingManifest,
	ManifestTooLarge,
	MissingSignature,
	InvalidPublicKey,
	CryptoUnavailable,
	InvalidSignature,
	InvalidJson,
	InvalidSchema,
	CatalogMismatch,
	UnsafeAssetPath,
	MissingAsset,
	AssetSizeMismatch,
	AssetHashMismatch,
	InvalidModelReference
};

struct VerifiedModelAsset final {
	QString id;
	QString backend;
	QString relativePath;
	QString canonicalPath;
	QByteArray sha256;
	qint64 size = 0;
};

struct VerifiedRecipeEntry final {
	QString id;
	std::uint32_t revision = 0;
	QString profile;
	QString engine;
	QStringList modelIds;
	int minimumNoiseReduction         = 0;
	int maximumNoiseReduction         = 0;
	int minimumNaturalCrisp           = 0;
	int maximumNaturalCrisp           = 0;
	unsigned int latencyBudgetSamples = 0;
	QString minimumCpuClass;
	std::uint32_t executionSemanticsVersion = 0;
	std::uint32_t mixCurveVersion           = 0;
	std::uint32_t adaptationPolicyVersion   = 0;
};

struct PackageVerificationReport final {
	PackageVerificationError error = PackageVerificationError::None;
	QString detail;
	bool ready     = false;
	bool verified  = false;
	bool unmanaged = false;
};

/// Verifies the immutable package-local input model and recipe catalogs before
/// AudioInput is constructed. Release-like builds fail closed; only a build-0
/// developer client with no embedded key is allowed to run unmanaged.
class InputEnhancementPackageVerifier final {
public:
	struct Configuration final {
		QDir packageRoot;
		QByteArray rawPublicKey;
		std::uint64_t currentBuild = 0;
		QString supportedCatalogRevision;
	};

	static constexpr qsizetype maximumModelManifestBytes  = 1024 * 1024;
	static constexpr qsizetype maximumRecipeManifestBytes = 512 * 1024;
	static constexpr qsizetype signatureBytes             = 64;
	static constexpr int maximumModels                    = 64;
	static constexpr int maximumRecipes                   = 128;
	static constexpr qint64 maximumModelAssetBytes        = 1024LL * 1024LL * 1024LL;

	explicit InputEnhancementPackageVerifier(Configuration configuration);
	~InputEnhancementPackageVerifier();

	PackageVerificationReport verify();
	PackageVerificationReport report() const;
	bool readyForHealthMarker() const noexcept;
	bool verificationHealthy() const noexcept;
	bool managedBySignedPackage() const noexcept;
	bool hasVerifiedPackage() const noexcept;

	/// Re-hashes the asset immediately before model initialization. Passing an
	/// expected path additionally binds the caller's resolved file to the
	/// manifest-relative path. Build-0 clients may use an unsigned catalog, but
	/// neural models are never authorized without a parsed catalog and exact
	/// asset SHA-256.
	bool modelAuthorized(const QString &modelId, const QString &expectedPath = {}) const;
	bool modelsAuthorized(const QStringList &modelIds) const;
	/// Binds the compiled product recipe to the exact signed recipe catalog,
	/// including revision, profile/engine, model, safe control intervals,
	/// latency budget and CPU class. Unmanaged build-0 clients return true.
	bool recipeAuthorized(const Recipe &recipe) const;
	/// Combines shared CPU/backend readiness with the exact verified recipe and
	/// model catalog. Manual profiles are selectable only when the requested
	/// profile itself can run; deterministic runtime fallback is never presented
	/// as a successful preflight.
	ProfileReadiness readinessForProfile(const ResolveRequest &request) const;
	/// Returns presentation-only readiness from the package snapshot that was
	/// fully hashed by verify(). This deliberately avoids re-reading large model
	/// files while a settings surface is being composed. Callers must still use
	/// readinessForProfile() before accepting or activating a selection.
	ProfileReadiness presentationReadinessForProfile(const ResolveRequest &request) const;
	/// Runs and caches a real Quality pipeline/worker probe off the audio
	/// callback. Manual profile readiness is deliberately independent from
	/// AutoV2's synthetic policy-capability probe.
	CpuClass manualProfileCpuClass() const;
	/// Starts the expensive one-time manual profile capability probe away from
	/// the GUI and audio callback threads. The owned future is joined during
	/// destruction so the verifier always outlives the work.
	void startManualProfileCpuClassProbe();
	/// Returns only an already-computed result. Presentation code must use this
	/// accessor so opening Settings can never run or wait for the real probe.
	std::optional< CpuClass > cachedManualProfileCpuClass() const noexcept;
	QByteArray modelSha256(const QString &modelId) const;
	QString modelSha256Hex(const QString &modelId) const;
	QString modelPath(const QString &modelId) const;
	QString modelRelativePath(const QString &modelId) const;
	QString catalogRevision() const;
	/// SHA-256 binding the verified model and recipe manifests plus their
	/// catalog revision. Capability-probe cache entries use this instead of a
	/// model name so any packaged model/recipe byte drift invalidates the tier.
	QString runtimePayloadFingerprint() const;

	// Immutable after publication; exposed as a type only so the strict parser
	// can construct it before one atomic publication.
	struct Snapshot final {
		QString catalogRevision;
		QByteArray modelManifestSha256;
		QByteArray recipeManifestSha256;
		QHash< QString, VerifiedModelAsset > models;
		QHash< QString, VerifiedRecipeEntry > recipes;
	};

private:
	PackageVerificationReport fail(PackageVerificationError error, QString detail);
	void publishReport(const PackageVerificationReport &report);
	std::shared_ptr< const Snapshot > snapshot() const;
	bool modelSnapshotAuthorized(const QString &modelId) const;

	Configuration m_configuration;
	std::atomic< std::shared_ptr< const Snapshot > > m_snapshot;
	std::atomic< std::shared_ptr< const PackageVerificationReport > > m_report;
	std::atomic_bool m_ready{ false };
	std::atomic_bool m_verified{ false };
	const bool m_developmentBypass;
	mutable std::once_flag m_manualProfileProbeOnce;
	mutable CpuClass m_manualProfileCpuClass;
	mutable std::atomic_bool m_manualProfileProbeReady{ false };
	std::mutex m_manualProfileProbeFutureMutex;
	std::future< void > m_manualProfileProbeFuture;
};

} // namespace Mumble::InputEnhancement

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENTPACKAGEVERIFIER_H_
