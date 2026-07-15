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
#include <memory>

namespace Mumble::InputEnhancement {

class Recipe;

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
	std::uint32_t mixCurveVersion            = 0;
	std::uint32_t adaptationPolicyVersion    = 0;
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

	PackageVerificationReport verify();
	PackageVerificationReport report() const;
	bool readyForHealthMarker() const noexcept;
	bool verificationHealthy() const noexcept;
	bool managedBySignedPackage() const noexcept;
	bool hasVerifiedPackage() const noexcept;

	/// Re-hashes the asset immediately before model initialization. Passing an
	/// expected path additionally binds the caller's resolved file to the signed
	/// relative path. Unmanaged build-0 developer clients return true.
	bool modelAuthorized(const QString &modelId, const QString &expectedPath = {}) const;
	bool modelsAuthorized(const QStringList &modelIds) const;
	/// Binds the compiled product recipe to the exact signed recipe catalog,
	/// including revision, profile/engine, model, safe control intervals,
	/// latency budget and CPU class. Unmanaged build-0 clients return true.
	bool recipeAuthorized(const Recipe &recipe) const;
	QByteArray modelSha256(const QString &modelId) const;
	QString modelSha256Hex(const QString &modelId) const;
	QString modelPath(const QString &modelId) const;
	QString modelRelativePath(const QString &modelId) const;
	QString catalogRevision() const;

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

	Configuration m_configuration;
	std::atomic< std::shared_ptr< const Snapshot > > m_snapshot;
	std::atomic< std::shared_ptr< const PackageVerificationReport > > m_report;
	std::atomic_bool m_ready{ false };
	std::atomic_bool m_verified{ false };
	const bool m_developmentBypass;
};

} // namespace Mumble::InputEnhancement

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENTPACKAGEVERIFIER_H_
