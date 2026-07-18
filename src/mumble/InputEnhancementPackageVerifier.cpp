// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementPackageVerifier.h"

#include "InputEnhancement.h"
#include "InputEnhancementPolicy.h"

#include <QCryptographicHash>
#include <QFile>
#include <QFileInfo>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QJsonParseError>
#include <QRegularExpression>
#include <QSet>

#include <algorithm>
#include <cmath>
#include <limits>
#include <optional>
#include <utility>

namespace Mumble::InputEnhancement {
namespace {
	std::optional< QByteArray > readBounded(const QString &path, qsizetype maximumBytes, qsizetype exactBytes = -1) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly)) {
			return std::nullopt;
		}
		const qint64 fileSize = file.size();
		if (fileSize < 0 || fileSize > maximumBytes || (exactBytes >= 0 && fileSize != exactBytes)) {
			return std::nullopt;
		}
		const QByteArray bytes = file.read(maximumBytes + 1);
		if (bytes.size() != fileSize || bytes.size() > maximumBytes
			|| (exactBytes >= 0 && bytes.size() != exactBytes)) {
			return std::nullopt;
		}
		return bytes;
	}

	bool hasExactFields(const QJsonObject &object, const QSet< QString > &required,
						const QSet< QString > &optional = {}) {
		if (object.size() < required.size() || object.size() > required.size() + optional.size()) {
			return false;
		}
		for (const QString &field : required) {
			if (!object.contains(field)) {
				return false;
			}
		}
		for (auto iterator = object.constBegin(); iterator != object.constEnd(); ++iterator) {
			if (!required.contains(iterator.key()) && !optional.contains(iterator.key())) {
				return false;
			}
		}
		return true;
	}

	bool validIdentifier(const QString &value) {
		static const QRegularExpression expression(QStringLiteral("^[A-Za-z0-9][A-Za-z0-9:._-]{0,127}$"));
		return expression.match(value).hasMatch();
	}

	bool validBoundedText(const QJsonValue &value, int maximumCharacters = 128) {
		if (!value.isString()) {
			return false;
		}
		const QString text = value.toString();
		return !text.trimmed().isEmpty() && text.size() <= maximumCharacters && !text.contains(QChar::Null);
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

	bool validSha256Text(const QString &text) {
		static const QRegularExpression expression(QStringLiteral("^[0-9a-f]{64}$"));
		return expression.match(text).hasMatch();
	}

	QByteArray sha256File(const QString &path, qint64 expectedSize, bool &readOk) {
		readOk = false;
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly) || file.size() != expectedSize) {
			return {};
		}
		QCryptographicHash hash(QCryptographicHash::Sha256);
		while (!file.atEnd()) {
			const QByteArray chunk = file.read(1024 * 1024);
			if (chunk.isEmpty() && file.error() != QFileDevice::NoError) {
				return {};
			}
			hash.addData(chunk);
		}
		if (file.size() != expectedSize) {
			return {};
		}
		readOk = true;
		return hash.result();
	}

	bool isPathWithinRoot(const QString &root, const QString &candidate) {
		if (root.isEmpty() || candidate.isEmpty()) {
			return false;
		}
		QString prefix = QDir::fromNativeSeparators(root);
		if (!prefix.endsWith(QLatin1Char('/'))) {
			prefix += QLatin1Char('/');
		}
		const QString normalizedCandidate = QDir::fromNativeSeparators(candidate);
#ifdef Q_OS_WIN
		return normalizedCandidate.startsWith(prefix, Qt::CaseInsensitive);
#else
		return normalizedCandidate.startsWith(prefix, Qt::CaseSensitive);
#endif
	}

	std::optional< QString > resolveSafeAssetPath(const QDir &packageRoot, const QString &relativePath) {
		if (relativePath.isEmpty() || relativePath.size() > 240 || QDir::isAbsolutePath(relativePath)
			|| relativePath.startsWith(QLatin1Char('/')) || relativePath.contains(QLatin1Char('\\'))
			|| relativePath.contains(QRegularExpression(QStringLiteral("[:*?<>|\"]")))) {
			return std::nullopt;
		}
		const QStringList segments = relativePath.split(QLatin1Char('/'), Qt::KeepEmptyParts);
		if (segments.isEmpty()) {
			return std::nullopt;
		}
		for (const QString &segment : segments) {
			if (segment.isEmpty() || segment == QLatin1String(".") || segment == QLatin1String("..")
				|| segment.endsWith(QLatin1Char('.')) || segment.endsWith(QLatin1Char(' '))) {
				return std::nullopt;
			}
		}

		const QString canonicalRoot = QFileInfo(packageRoot.absolutePath()).canonicalFilePath();
		const QFileInfo assetInfo(packageRoot.filePath(relativePath));
		const QString canonicalAsset = assetInfo.canonicalFilePath();
		if (!assetInfo.isFile() || !isPathWithinRoot(canonicalRoot, canonicalAsset)) {
			return std::nullopt;
		}
		return canonicalAsset;
	}

	bool parseJsonObject(const QByteArray &bytes, QJsonObject &object) {
		QJsonParseError error;
		const QJsonDocument document = QJsonDocument::fromJson(bytes, &error);
		if (error.error != QJsonParseError::NoError || !document.isObject()) {
			return false;
		}
		object = document.object();
		return true;
	}

	bool parseStringArray(const QJsonValue &value, QStringList &strings, int maximumItems) {
		if (!value.isArray()) {
			return false;
		}
		const QJsonArray array = value.toArray();
		if (array.size() > maximumItems) {
			return false;
		}
		QSet< QString > unique;
		for (const QJsonValue &item : array) {
			if (!item.isString() || !validIdentifier(item.toString()) || unique.contains(item.toString())) {
				return false;
			}
			unique.insert(item.toString());
			strings.append(item.toString());
		}
		return true;
	}

	bool parseControlRange(const QJsonValue &value, int &minimumResult, int &maximumResult) {
		if (!value.isArray()) {
			return false;
		}
		const QJsonArray range = value.toArray();
		if (range.size() != 2) {
			return false;
		}
		qint64 minimum = 0;
		qint64 maximum = 0;
		if (!exactInteger(range.at(0), 0, 100, minimum) || !exactInteger(range.at(1), 0, 100, maximum)
			|| minimum > maximum) {
			return false;
		}
		minimumResult = static_cast< int >(minimum);
		maximumResult = static_cast< int >(maximum);
		return true;
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
				return QStringLiteral("VoiceFocus");
		}
		return {};
	}

	QString engineName(Engine engine) {
		switch (engine) {
			case Engine::None:
				return QStringLiteral("None");
			case Engine::Speex:
				return QStringLiteral("Speex");
			case Engine::RNNoise:
				return QStringLiteral("RNNoise");
			case Engine::DeepFilterNet:
				return QStringLiteral("DeepFilterNet");
			case Engine::DTLN:
				return QStringLiteral("DTLN");
		}
		return {};
	}

	QString cpuClassName(CpuClass cpuClass) {
		switch (cpuClass) {
			case CpuClass::Low:
				return QStringLiteral("Low");
			case CpuClass::Standard:
				return QStringLiteral("Standard");
			case CpuClass::High:
				return QStringLiteral("High");
		}
		return {};
	}

	PackageVerificationError
		parseAndVerifyPackage(const QDir &root, const QString &supportedCatalogRevision, const QByteArray &modelBytes,
							  const QByteArray &recipeBytes,
							  std::shared_ptr< InputEnhancementPackageVerifier::Snapshot > &snapshot, QString &detail) {
		QJsonObject modelRoot;
		QJsonObject recipeRoot;
		if (!parseJsonObject(modelBytes, modelRoot) || !parseJsonObject(recipeBytes, recipeRoot)) {
			detail = QStringLiteral("Package manifest is not a JSON object");
			return PackageVerificationError::InvalidJson;
		}
		const QSet< QString > modelRootFields  = { QStringLiteral("schemaVersion"), QStringLiteral("catalogRevision"),
												   QStringLiteral("generatedFromAssets"), QStringLiteral("models") };
		const QSet< QString > recipeRootFields = { QStringLiteral("schemaVersion"), QStringLiteral("catalogRevision"),
												   QStringLiteral("modelManifestSha256"), QStringLiteral("recipes") };
		qint64 modelSchema                     = 0;
		qint64 recipeSchema                    = 0;
		if (!hasExactFields(modelRoot, modelRootFields) || !hasExactFields(recipeRoot, recipeRootFields)
			|| !exactInteger(modelRoot.value(QStringLiteral("schemaVersion")), 1, 1, modelSchema)
			|| !exactInteger(recipeRoot.value(QStringLiteral("schemaVersion")), 2, 2, recipeSchema)
			|| !modelRoot.value(QStringLiteral("generatedFromAssets")).isBool()
			|| !modelRoot.value(QStringLiteral("generatedFromAssets")).toBool()
			|| !modelRoot.value(QStringLiteral("catalogRevision")).isString()
			|| !recipeRoot.value(QStringLiteral("catalogRevision")).isString()) {
			detail = QStringLiteral("Package manifest root schema is invalid");
			return PackageVerificationError::InvalidSchema;
		}
		const QString catalogRevision = modelRoot.value(QStringLiteral("catalogRevision")).toString();
		if (catalogRevision != supportedCatalogRevision
			|| recipeRoot.value(QStringLiteral("catalogRevision")).toString() != catalogRevision) {
			detail = QStringLiteral("Package catalog revision does not match %1").arg(supportedCatalogRevision);
			return PackageVerificationError::CatalogMismatch;
		}
		const QByteArray modelManifestHash = QCryptographicHash::hash(modelBytes, QCryptographicHash::Sha256);
		const QString reportedModelHash    = recipeRoot.value(QStringLiteral("modelManifestSha256")).toString();
		if (!recipeRoot.value(QStringLiteral("modelManifestSha256")).isString() || !validSha256Text(reportedModelHash)
			|| reportedModelHash.toLatin1() != modelManifestHash.toHex()) {
			detail = QStringLiteral("Recipe manifest is not bound to the exact model manifest bytes");
			return PackageVerificationError::AssetHashMismatch;
		}

		const QJsonValue modelsValue = modelRoot.value(QStringLiteral("models"));
		if (!modelsValue.isArray() || modelsValue.toArray().isEmpty()
			|| modelsValue.toArray().size() > InputEnhancementPackageVerifier::maximumModels) {
			detail = QStringLiteral("Model list is empty or exceeds its bound");
			return PackageVerificationError::InvalidSchema;
		}

		auto parsedSnapshot                  = std::make_shared< InputEnhancementPackageVerifier::Snapshot >();
		parsedSnapshot->catalogRevision      = catalogRevision;
		parsedSnapshot->modelManifestSha256  = modelManifestHash;
		parsedSnapshot->recipeManifestSha256 = QCryptographicHash::hash(recipeBytes, QCryptographicHash::Sha256);
		QHash< QString, QStringList > compatibleRecipes;
		QSet< QString > normalizedPaths;
		const QSet< QString > modelFields = { QStringLiteral("id"),
											  QStringLiteral("version"),
											  QStringLiteral("backend"),
											  QStringLiteral("path"),
											  QStringLiteral("sha256"),
											  QStringLiteral("size"),
											  QStringLiteral("licenseSpdx"),
											  QStringLiteral("sampleRateHz"),
											  QStringLiteral("algorithmicLatencyMs"),
											  QStringLiteral("recipeCompatibility") };
		for (const QJsonValue &value : modelsValue.toArray()) {
			if (!value.isObject()) {
				detail = QStringLiteral("Model entry is not an object");
				return PackageVerificationError::InvalidSchema;
			}
			const QJsonObject model = value.toObject();
			const QString id        = model.value(QStringLiteral("id")).toString();
			const QString backend   = model.value(QStringLiteral("backend")).toString();
			const QString path      = model.value(QStringLiteral("path")).toString();
			const QString hashText  = model.value(QStringLiteral("sha256")).toString();
			qint64 size             = 0;
			qint64 sampleRate       = 0;
			QStringList compatibility;
			const QJsonValue latency = model.value(QStringLiteral("algorithmicLatencyMs"));
			if (!hasExactFields(model, modelFields) || !validIdentifier(id) || parsedSnapshot->models.contains(id)
				|| !validBoundedText(model.value(QStringLiteral("version")))
				|| !validBoundedText(model.value(QStringLiteral("backend")))
				|| !validBoundedText(model.value(QStringLiteral("licenseSpdx")), 256)
				|| !model.value(QStringLiteral("path")).isString() || !validSha256Text(hashText)
				|| !exactInteger(model.value(QStringLiteral("size")), 1,
								 InputEnhancementPackageVerifier::maximumModelAssetBytes, size)
				|| !exactInteger(model.value(QStringLiteral("sampleRateHz")), 1, 384000, sampleRate)
				|| !latency.isDouble() || !std::isfinite(latency.toDouble()) || latency.toDouble() < 0.0
				|| latency.toDouble() > 1000.0
				|| !parseStringArray(model.value(QStringLiteral("recipeCompatibility")), compatibility,
									 InputEnhancementPackageVerifier::maximumRecipes)) {
				detail = QStringLiteral("Model entry %1 has invalid schema").arg(id);
				return PackageVerificationError::InvalidSchema;
			}
			const auto canonicalPath = resolveSafeAssetPath(root, path);
			if (!canonicalPath) {
				detail = QStringLiteral("Model %1 has an unsafe or missing asset path").arg(id);
				return QFileInfo(root.filePath(path)).exists() ? PackageVerificationError::UnsafeAssetPath
															   : PackageVerificationError::MissingAsset;
			}
			const QString pathKey = canonicalPath->toCaseFolded();
			if (normalizedPaths.contains(pathKey)) {
				detail = QStringLiteral("Multiple models reference the same asset path");
				return PackageVerificationError::InvalidSchema;
			}
			normalizedPaths.insert(pathKey);
			const QFileInfo assetInfo(*canonicalPath);
			if (assetInfo.size() != size) {
				detail = QStringLiteral("Model %1 asset size does not match the manifest").arg(id);
				return PackageVerificationError::AssetSizeMismatch;
			}
			bool readOk                 = false;
			const QByteArray actualHash = sha256File(*canonicalPath, size, readOk);
			if (!readOk || actualHash.toHex() != hashText.toLatin1()) {
				detail = QStringLiteral("Model %1 asset hash does not match the manifest").arg(id);
				return PackageVerificationError::AssetHashMismatch;
			}
			parsedSnapshot->models.insert(id,
										  VerifiedModelAsset{ id, backend, path, *canonicalPath, actualHash, size });
			compatibleRecipes.insert(id, compatibility);
		}

		const QJsonValue recipesValue = recipeRoot.value(QStringLiteral("recipes"));
		if (!recipesValue.isArray() || recipesValue.toArray().isEmpty()
			|| recipesValue.toArray().size() > InputEnhancementPackageVerifier::maximumRecipes) {
			detail = QStringLiteral("Recipe list is empty or exceeds its bound");
			return PackageVerificationError::InvalidSchema;
		}
		const QSet< QString > recipeFields         = { QStringLiteral("id"),
													   QStringLiteral("revision"),
													   QStringLiteral("profile"),
													   QStringLiteral("engine"),
													   QStringLiteral("modelIds"),
													   QStringLiteral("noiseReductionRange"),
													   QStringLiteral("naturalCrispRange"),
													   QStringLiteral("latencyBudgetMs"),
													   QStringLiteral("minimumCpuClass"),
													   QStringLiteral("executionSemanticsVersion"),
													   QStringLiteral("mixCurveVersion"),
													   QStringLiteral("adaptationPolicyVersion") };
		const QSet< QString > optionalRecipeFields = { QStringLiteral("advancedOnly") };
		const QSet< QString > profiles             = { QStringLiteral("Original"), QStringLiteral("Light"),
													   QStringLiteral("Balanced"), QStringLiteral("Quality"),
													   QStringLiteral("Auto"),     QStringLiteral("VoiceFocus") };
		const QSet< QString > engines    = { QStringLiteral("None"), QStringLiteral("Speex"), QStringLiteral("RNNoise"),
											 QStringLiteral("DeepFilterNet"), QStringLiteral("DTLN") };
		const QSet< QString > cpuClasses = { QStringLiteral("Low"), QStringLiteral("Standard"),
											 QStringLiteral("High") };
		QSet< QString > seenRecipeIds;
		QSet< QString > seenProfiles;
		QHash< QString, QStringList > recipeModels;
		for (const QJsonValue &value : recipesValue.toArray()) {
			if (!value.isObject()) {
				detail = QStringLiteral("Recipe entry is not an object");
				return PackageVerificationError::InvalidSchema;
			}
			const QJsonObject recipe = value.toObject();
			const QString id         = recipe.value(QStringLiteral("id")).toString();
			const QString profile    = recipe.value(QStringLiteral("profile")).toString();
			const QString engine     = recipe.value(QStringLiteral("engine")).toString();
			const QString cpuClass   = recipe.value(QStringLiteral("minimumCpuClass")).toString();
			qint64 revision          = 0;
			qint64 executionVersion  = 0;
			qint64 mixVersion        = 0;
			qint64 adaptationVersion = 0;
			QStringList modelIds;
			int minimumNoiseReduction = 0;
			int maximumNoiseReduction = 0;
			int minimumNaturalCrisp   = 0;
			int maximumNaturalCrisp   = 0;
			const QJsonValue latency  = recipe.value(QStringLiteral("latencyBudgetMs"));
			if (!hasExactFields(recipe, recipeFields, optionalRecipeFields) || !validIdentifier(id)
				|| seenRecipeIds.contains(id) || !profiles.contains(profile) || !engines.contains(engine)
				|| !cpuClasses.contains(cpuClass)
				|| !exactInteger(recipe.value(QStringLiteral("revision")), RecipeCatalog::currentRevision,
								 RecipeCatalog::currentRevision, revision)
				|| !exactInteger(recipe.value(QStringLiteral("executionSemanticsVersion")),
								 recipeExecutionSemanticsVersion, recipeExecutionSemanticsVersion, executionVersion)
				|| !exactInteger(recipe.value(QStringLiteral("mixCurveVersion")), qualifiedMixCurveVersion,
								 qualifiedMixCurveVersion, mixVersion)
				|| !exactInteger(recipe.value(QStringLiteral("adaptationPolicyVersion")), adaptationPolicyVersion,
								 adaptationPolicyVersion, adaptationVersion)
				|| !parseStringArray(recipe.value(QStringLiteral("modelIds")), modelIds, 8)
				|| !parseControlRange(recipe.value(QStringLiteral("noiseReductionRange")), minimumNoiseReduction,
									  maximumNoiseReduction)
				|| !parseControlRange(recipe.value(QStringLiteral("naturalCrispRange")), minimumNaturalCrisp,
									  maximumNaturalCrisp)
				|| !latency.isDouble() || !std::isfinite(latency.toDouble()) || latency.toDouble() < 0.0
				|| latency.toDouble() > 1000.0
				|| (recipe.contains(QStringLiteral("advancedOnly"))
					&& !recipe.value(QStringLiteral("advancedOnly")).isBool())) {
				detail = QStringLiteral("Recipe entry %1 has invalid schema").arg(id);
				return PackageVerificationError::InvalidSchema;
			}
			const bool neuralEngine = engine == QLatin1String("RNNoise") || engine == QLatin1String("DeepFilterNet")
									  || engine == QLatin1String("DTLN");
			if (neuralEngine == modelIds.isEmpty()) {
				detail = QStringLiteral("Recipe %1 has an invalid engine/model relationship").arg(id);
				return PackageVerificationError::InvalidModelReference;
			}
			for (const QString &modelId : modelIds) {
				const auto modelIterator = parsedSnapshot->models.constFind(modelId);
				if (modelIterator == parsedSnapshot->models.constEnd() || modelIterator->backend != engine) {
					detail = QStringLiteral("Recipe %1 references unknown or incompatible model %2").arg(id, modelId);
					return PackageVerificationError::InvalidModelReference;
				}
				if (!compatibleRecipes.value(modelId).contains(id)) {
					detail = QStringLiteral("Model %1 does not declare compatibility with recipe %2").arg(modelId, id);
					return PackageVerificationError::InvalidModelReference;
				}
			}
			seenRecipeIds.insert(id);
			seenProfiles.insert(profile);
			recipeModels.insert(id, modelIds);
			const double latencySamplesValue = latency.toDouble() * 48.0;
			if (std::floor(latencySamplesValue) != latencySamplesValue
				|| latencySamplesValue > static_cast< double >(std::numeric_limits< unsigned int >::max())) {
				detail = QStringLiteral("Recipe %1 has a non-integral 48 kHz latency budget").arg(id);
				return PackageVerificationError::InvalidSchema;
			}
			parsedSnapshot->recipes.insert(
				id, VerifiedRecipeEntry{ id, static_cast< std::uint32_t >(revision), profile, engine, modelIds,
										 minimumNoiseReduction, maximumNoiseReduction, minimumNaturalCrisp,
										 maximumNaturalCrisp, static_cast< unsigned int >(latencySamplesValue),
										 cpuClass, static_cast< std::uint32_t >(executionVersion),
										 static_cast< std::uint32_t >(mixVersion),
										 static_cast< std::uint32_t >(adaptationVersion) });
		}
		if (seenProfiles != profiles) {
			detail = QStringLiteral("Package recipes do not cover all five product profiles");
			return PackageVerificationError::InvalidSchema;
		}
		for (auto model = compatibleRecipes.constBegin(); model != compatibleRecipes.constEnd(); ++model) {
			for (const QString &recipeId : model.value()) {
				if (!seenRecipeIds.contains(recipeId) || !recipeModels.value(recipeId).contains(model.key())) {
					detail = QStringLiteral("Model %1 declares an invalid recipe compatibility").arg(model.key());
					return PackageVerificationError::InvalidModelReference;
				}
			}
		}

		snapshot = std::move(parsedSnapshot);
		return PackageVerificationError::None;
	}
} // namespace

InputEnhancementPackageVerifier::InputEnhancementPackageVerifier(Configuration configuration)
	: m_configuration(std::move(configuration)),
	  m_developmentBypass(m_configuration.currentBuild == 0 && m_configuration.rawPublicKey.isEmpty()),
	  m_manualProfileCpuClass(CpuClass::Low) {
	m_report.store(std::make_shared< const PackageVerificationReport >(), std::memory_order_release);
}

PackageVerificationReport InputEnhancementPackageVerifier::verify() {
	if (m_developmentBypass) {
		// Verification may be repeated by tests or a future package refresh. Drop
		// the previously published unsigned catalog before inspecting the current
		// package so a manifest-free build-0 run can never inherit stale neural
		// authorization from an earlier successful verification.
		m_snapshot.store({}, std::memory_order_release);
		const QString modelManifestPath =
			m_configuration.packageRoot.filePath(QStringLiteral("input-models.json"));
		const QString recipeManifestPath =
			m_configuration.packageRoot.filePath(QStringLiteral("input-recipes.json"));
		const bool modelManifestExists  = QFileInfo::exists(modelManifestPath);
		const bool recipeManifestExists = QFileInfo::exists(recipeManifestPath);

		// Build-0 clients do not authenticate manifests with the production key,
		// but packaged product profiles must still be bound to the exact catalog
		// and model bytes that will be initialized. Preserve the manifest-free
		// unmanaged mode for Original/Light development, while refusing to invent
		// a neural model identity when no catalog is present.
		if (modelManifestExists || recipeManifestExists) {
			const auto modelBytes = readBounded(modelManifestPath, maximumModelManifestBytes);
			const auto recipeBytes = readBounded(recipeManifestPath, maximumRecipeManifestBytes);
			if (!modelBytes || !recipeBytes) {
				return fail(PackageVerificationError::MissingManifest,
							QStringLiteral("Unmanaged input model or recipe manifest is missing or oversized"));
			}

			std::shared_ptr< Snapshot > parsedSnapshot;
			QString detail;
			const PackageVerificationError parseError =
				parseAndVerifyPackage(m_configuration.packageRoot, m_configuration.supportedCatalogRevision,
								  *modelBytes, *recipeBytes, parsedSnapshot, detail);
			if (parseError != PackageVerificationError::None) {
				return fail(parseError, std::move(detail));
			}
			m_snapshot.store(std::shared_ptr< const Snapshot >(std::move(parsedSnapshot)), std::memory_order_release);
		}

		PackageVerificationReport unmanaged;
		unmanaged.ready     = true;
		unmanaged.unmanaged = true;
		publishReport(unmanaged);
		return unmanaged;
	}
	if (m_configuration.rawPublicKey.size() != 32) {
		return fail(PackageVerificationError::InvalidPublicKey,
					QStringLiteral("Signed package verification requires a 32-byte Ed25519 public key"));
	}
	OpenSslEd25519Verifier verifier;
	if (!verifier.isAvailable()) {
		return fail(PackageVerificationError::CryptoUnavailable, QStringLiteral("Ed25519 verification is unavailable"));
	}

	const auto modelBytes  = readBounded(m_configuration.packageRoot.filePath(QStringLiteral("input-models.json")),
										 maximumModelManifestBytes);
	const auto recipeBytes = readBounded(m_configuration.packageRoot.filePath(QStringLiteral("input-recipes.json")),
										 maximumRecipeManifestBytes);
	if (!modelBytes || !recipeBytes) {
		return fail(PackageVerificationError::MissingManifest,
					QStringLiteral("Signed input model or recipe manifest is missing or oversized"));
	}
	const auto modelSignature = readBounded(
		m_configuration.packageRoot.filePath(QStringLiteral("input-models.json.sig")), signatureBytes, signatureBytes);
	const auto recipeSignature = readBounded(
		m_configuration.packageRoot.filePath(QStringLiteral("input-recipes.json.sig")), signatureBytes, signatureBytes);
	if (!modelSignature || !recipeSignature) {
		return fail(PackageVerificationError::MissingSignature,
					QStringLiteral("Detached package manifest signature is missing or malformed"));
	}
	if (!verifier.verify(m_configuration.rawPublicKey, *modelBytes, *modelSignature)
		|| !verifier.verify(m_configuration.rawPublicKey, *recipeBytes, *recipeSignature)) {
		return fail(PackageVerificationError::InvalidSignature,
					QStringLiteral("Detached package manifest signature is invalid"));
	}

	std::shared_ptr< Snapshot > parsedSnapshot;
	QString detail;
	const PackageVerificationError parseError =
		parseAndVerifyPackage(m_configuration.packageRoot, m_configuration.supportedCatalogRevision, *modelBytes,
							  *recipeBytes, parsedSnapshot, detail);
	if (parseError != PackageVerificationError::None) {
		return fail(parseError, std::move(detail));
	}

	m_snapshot.store(std::shared_ptr< const Snapshot >(std::move(parsedSnapshot)), std::memory_order_release);
	PackageVerificationReport success;
	success.ready    = true;
	success.verified = true;
	publishReport(success);
	return success;
}

PackageVerificationReport InputEnhancementPackageVerifier::report() const {
	const auto current = m_report.load(std::memory_order_acquire);
	return current ? *current : PackageVerificationReport{};
}

bool InputEnhancementPackageVerifier::readyForHealthMarker() const noexcept {
	return m_ready.load(std::memory_order_acquire);
}

bool InputEnhancementPackageVerifier::verificationHealthy() const noexcept {
	if (!readyForHealthMarker()) {
		return false;
	}
	if (!m_developmentBypass) {
		return m_verified.load(std::memory_order_acquire);
	}
	const auto current = m_report.load(std::memory_order_acquire);
	return current && current->unmanaged && current->error == PackageVerificationError::None;
}

bool InputEnhancementPackageVerifier::managedBySignedPackage() const noexcept {
	return !m_developmentBypass;
}

bool InputEnhancementPackageVerifier::hasVerifiedPackage() const noexcept {
	return m_verified.load(std::memory_order_acquire);
}

bool InputEnhancementPackageVerifier::modelAuthorized(const QString &modelId, const QString &expectedPath) const {
	const auto current = snapshot();
	// A neural product profile always needs a concrete catalog entry, including
	// in an unmanaged build-0 client. This prevents a development run from
	// reporting an empty active-model hash as if it were qualified evidence.
	if (!current || (!m_developmentBypass && !m_verified.load(std::memory_order_acquire))
		|| !verificationHealthy()) {
		return false;
	}
	const auto iterator = current->models.constFind(modelId);
	if (iterator == current->models.constEnd()) {
		return false;
	}
	if (!expectedPath.isEmpty()) {
		const QString expectedCanonical = QFileInfo(expectedPath).canonicalFilePath();
#ifdef Q_OS_WIN
		if (expectedCanonical.compare(iterator->canonicalPath, Qt::CaseInsensitive) != 0) {
#else
		if (expectedCanonical != iterator->canonicalPath) {
#endif
			return false;
		}
	}
	const QFileInfo assetInfo(iterator->canonicalPath);
	if (!assetInfo.isFile() || assetInfo.size() != iterator->size) {
		return false;
	}
	bool readOk = false;
	return sha256File(iterator->canonicalPath, iterator->size, readOk) == iterator->sha256 && readOk;
}

bool InputEnhancementPackageVerifier::modelsAuthorized(const QStringList &modelIds) const {
	if (modelIds.isEmpty()) {
		return false;
	}
	return std::all_of(modelIds.cbegin(), modelIds.cend(), [this](const QString &id) { return modelAuthorized(id); });
}

bool InputEnhancementPackageVerifier::recipeAuthorized(const Recipe &recipe) const {
	const auto current = snapshot();
	if (!current) {
		// Keep manifest-free build-0 development usable for the non-neural
		// Original/Light path. Neural recipes are rejected separately by
		// modelAuthorized(), so they can never initialize without an attested
		// model asset and SHA-256.
		return m_developmentBypass && verificationHealthy() && !recipe.usesNeuralProcessor();
	}
	if ((!m_developmentBypass && !m_verified.load(std::memory_order_acquire)) || !verificationHealthy()) {
		return false;
	}
	const auto iterator = current->recipes.constFind(recipe.id());
	if (iterator == current->recipes.constEnd()) {
		return false;
	}
	QStringList expectedModelIds;
	if (!recipe.modelId().isEmpty()) {
		expectedModelIds.append(recipe.modelId());
	}
	return iterator->revision == recipe.revision() && iterator->profile == profileName(recipe.requestedProfile())
		   && iterator->engine == engineName(recipe.engine()) && iterator->modelIds == expectedModelIds
		   && recipe.noiseReduction() >= iterator->minimumNoiseReduction
		   && recipe.noiseReduction() <= iterator->maximumNoiseReduction
		   && recipe.naturalCrisp() >= iterator->minimumNaturalCrisp
		   && recipe.naturalCrisp() <= iterator->maximumNaturalCrisp
		   && recipe.latencyBudgetSamples() == iterator->latencyBudgetSamples
		   && iterator->minimumCpuClass == cpuClassName(recipe.minimumCpuClass())
		   && iterator->executionSemanticsVersion == recipeExecutionSemanticsVersion
		   && iterator->mixCurveVersion == qualifiedMixCurveVersion
		   && iterator->adaptationPolicyVersion == adaptationPolicyVersion;
}

ProfileReadiness InputEnhancementPackageVerifier::readinessForProfile(const ResolveRequest &request) const {
	ProfileReadiness readiness = profileReadiness(request);
	if (!readiness.selectable || request.profile == Profile::Original) {
		return readiness;
	}
	if (!verificationHealthy()) {
		return { false, false, ProfileReadinessReason::PackageUnavailable };
	}

	const Recipe recipe = RecipeCatalog::resolve(request);
	if ((request.profile != Profile::Auto && recipe.effectiveProfile() != request.profile)
		|| !recipeAuthorized(recipe)) {
		return { false, false, ProfileReadinessReason::RecipeUnauthorized };
	}
	if (recipe.usesNeuralProcessor() && !modelAuthorized(recipe.modelId())) {
		return { false, false, ProfileReadinessReason::ModelUnavailable };
	}
	return readiness;
}

CpuClass InputEnhancementPackageVerifier::manualProfileCpuClass() const {
	if (!verificationHealthy()) {
		return CpuClass::Low;
	}

	std::call_once(m_manualProfileProbeOnce, [this]() {
		const BackendAvailability availability = BackendAvailability::compiled();
		ManualProfileCapabilityMetrics metrics;
		metrics.rnnoiseAvailable       = availability.rnnoise;
		metrics.deepFilterNetAvailable = availability.deepFilterNet;
		m_manualProfileCpuClass        = manualProfileCpuClassForMetrics(metrics);
		if (!availability.deepFilterNet) {
			return;
		}

		try {
			ResolveRequest request;
			request.profile             = Profile::Quality;
			request.cpuClass            = CpuClass::High;
			request.backendAvailability = availability;
			const Recipe recipe         = RecipeCatalog::resolve(request);
			if (recipe.effectiveProfile() != Profile::Quality || !recipeAuthorized(recipe)
				|| !modelAuthorized(recipe.modelId())) {
				return;
			}

			const QString authorizedSha256 = modelSha256Hex(recipe.modelId());
			const QString authorizedPath   = modelPath(recipe.modelId());
			// Use a relaxed probe deadline to collect the actual distributions, then
			// classify against the stricter 8 ms p99 / 10 ms catastrophe contract.
			Pipeline pipeline(Pipeline::ProcessorFactory{}, Pipeline::NanosecondClock{}, 20'000'000);
			if (!pipeline.configure(recipe, authorizedSha256, authorizedPath)) {
				return;
			}

			bool healthy = true;
			std::array< float, frameSamples > frame = {};
			for (unsigned int frameIndex = 0; frameIndex < 32; ++frameIndex) {
				if (!pipeline.prepareOfflineFrame()) {
					healthy = false;
					break;
				}
				for (unsigned int sample = 0; sample < frameSamples; ++sample) {
					const int phase = static_cast< int >((sample + frameIndex * 37U) % 97U) - 48;
					frame[sample]   = static_cast< float >(phase) / 192.0f;
				}
				if (!pipeline.processFrame(frame)) {
					healthy = false;
					break;
				}
			}
			healthy = pipeline.finishOfflineProcessing() && healthy;
			const Diagnostics diagnostics = pipeline.diagnostics();
			metrics.qualityPipelineReady =
				healthy && !diagnostics.fallbackActive() && diagnostics.deadlineMisses() == 0;
			metrics.callbackP99Nanoseconds     = diagnostics.processingP99Nanoseconds();
			metrics.callbackMaximumNanoseconds = diagnostics.maximumProcessingNanoseconds();
			metrics.workerFrames               = diagnostics.workerProcessingFrames();
			metrics.workerP99Nanoseconds       = diagnostics.workerProcessingP99Nanoseconds();
			metrics.workerMaximumNanoseconds   = diagnostics.workerMaximumProcessingNanoseconds();
			m_manualProfileCpuClass            = manualProfileCpuClassForMetrics(metrics);
		} catch (...) {
			// The capability probe is advisory and runs before a manual profile is
			// selected. Any allocation/runtime exception keeps the conservative
			// Standard classification established above.
		}
	});
	return m_manualProfileCpuClass;
}

QByteArray InputEnhancementPackageVerifier::modelSha256(const QString &modelId) const {
	const auto current = snapshot();
	return current ? current->models.value(modelId).sha256 : QByteArray{};
}

QString InputEnhancementPackageVerifier::modelSha256Hex(const QString &modelId) const {
	return QString::fromLatin1(modelSha256(modelId).toHex());
}

QString InputEnhancementPackageVerifier::modelPath(const QString &modelId) const {
	const auto current = snapshot();
	return current ? current->models.value(modelId).canonicalPath : QString{};
}

QString InputEnhancementPackageVerifier::modelRelativePath(const QString &modelId) const {
	const auto current = snapshot();
	return current ? current->models.value(modelId).relativePath : QString{};
}

QString InputEnhancementPackageVerifier::catalogRevision() const {
	const auto current = snapshot();
	return current ? current->catalogRevision : QString{};
}

QString InputEnhancementPackageVerifier::runtimePayloadFingerprint() const {
	const auto current = snapshot();
	if (!current || current->modelManifestSha256.size() != QCryptographicHash::hashLength(QCryptographicHash::Sha256)
		|| current->recipeManifestSha256.size() != QCryptographicHash::hashLength(QCryptographicHash::Sha256)) {
		return {};
	}
	QByteArray canonical("mumble-input-enhancement-runtime-payload-v1", 43);
	canonical.append('\0');
	canonical.append(current->catalogRevision.toUtf8());
	canonical.append('\0');
	canonical.append(current->modelManifestSha256);
	canonical.append(current->recipeManifestSha256);
	return QString::fromLatin1(QCryptographicHash::hash(canonical, QCryptographicHash::Sha256).toHex());
}

PackageVerificationReport InputEnhancementPackageVerifier::fail(PackageVerificationError error, QString detail) {
	m_snapshot.store({}, std::memory_order_release);
	PackageVerificationReport failure;
	failure.error  = error;
	failure.detail = std::move(detail);
	failure.ready  = true;
	publishReport(failure);
	return failure;
}

void InputEnhancementPackageVerifier::publishReport(const PackageVerificationReport &reportValue) {
	m_report.store(std::make_shared< const PackageVerificationReport >(reportValue), std::memory_order_release);
	m_verified.store(reportValue.verified, std::memory_order_release);
	m_ready.store(reportValue.ready, std::memory_order_release);
}

std::shared_ptr< const InputEnhancementPackageVerifier::Snapshot > InputEnhancementPackageVerifier::snapshot() const {
	return m_snapshot.load(std::memory_order_acquire);
}

} // namespace Mumble::InputEnhancement
