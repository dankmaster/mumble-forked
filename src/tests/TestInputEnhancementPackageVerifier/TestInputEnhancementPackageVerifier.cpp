// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancement.h"
#include "InputEnhancementPackageVerifier.h"

#include <QtTest>

#include <QCryptographicHash>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QTemporaryDir>

#include <openssl/evp.h>

#include <cstdint>
#include <functional>

using namespace Mumble::InputEnhancement;

namespace {
const QByteArray PrivateSeed     = QByteArray::fromHex("9d61b19deffd5a60ba844af492ec2cc4"
														   "4449c5697b326919703bac031cae7f60");
const QByteArray PublicKey       = QByteArray::fromHex("d75a980182b10ab7d54bfed3c964073a"
															 "0ee172f3daa62325af021a68f707511a");
const QString CatalogRevision    = QStringLiteral("input-recipes-v1");
const QByteArray RnnoiseAsset    = QByteArrayLiteral("signed-rnnoise-runtime");
const QByteArray DeepFilterAsset = QByteArrayLiteral("signed-deepfilter-model");

QByteArray sign(const QByteArray &message) {
	EVP_PKEY *key = EVP_PKEY_new_raw_private_key(EVP_PKEY_ED25519, nullptr,
												 reinterpret_cast< const unsigned char * >(PrivateSeed.constData()),
												 static_cast< std::size_t >(PrivateSeed.size()));
	if (!key) {
		return {};
	}
	EVP_MD_CTX *context = EVP_MD_CTX_new();
	if (!context) {
		EVP_PKEY_free(key);
		return {};
	}
	QByteArray signature(InputEnhancementPackageVerifier::signatureBytes, '\0');
	std::size_t signatureSize = static_cast< std::size_t >(signature.size());
	const bool initialized    = EVP_DigestSignInit(context, nullptr, nullptr, nullptr, key) == 1;
	const bool signedMessage =
		initialized
		&& EVP_DigestSign(context, reinterpret_cast< unsigned char * >(signature.data()), &signatureSize,
						  reinterpret_cast< const unsigned char * >(message.constData()),
						  static_cast< std::size_t >(message.size()))
			   == 1;
	EVP_MD_CTX_free(context);
	EVP_PKEY_free(key);
	return signedMessage && signatureSize == static_cast< std::size_t >(InputEnhancementPackageVerifier::signatureBytes)
			   ? signature
			   : QByteArray{};
}

bool writeFile(const QString &path, const QByteArray &bytes) {
	QFile file(path);
	return file.open(QIODevice::WriteOnly | QIODevice::Truncate) && file.write(bytes) == bytes.size();
}

QJsonObject recipe(const QString &id, const QString &profile, const QString &engine, const QJsonArray &modelIds,
				   const QString &cpuClass, double latency) {
	return {
		{ QStringLiteral("id"), id },
		{ QStringLiteral("revision"), 1 },
		{ QStringLiteral("profile"), profile },
		{ QStringLiteral("engine"), engine },
		{ QStringLiteral("modelIds"), modelIds },
		{ QStringLiteral("noiseReductionRange"), QJsonArray{ 0, 100 } },
		{ QStringLiteral("naturalCrispRange"), QJsonArray{ 0, 100 } },
		{ QStringLiteral("latencyBudgetMs"), latency },
		{ QStringLiteral("minimumCpuClass"), cpuClass },
		{ QStringLiteral("executionSemanticsVersion"),
		  static_cast< qint64 >(recipeExecutionSemanticsVersion) },
		{ QStringLiteral("mixCurveVersion"), static_cast< qint64 >(qualifiedMixCurveVersion) },
		{ QStringLiteral("adaptationPolicyVersion"), static_cast< qint64 >(adaptationPolicyVersion) },
	};
}

struct PackageBytes final {
	QByteArray models;
	QByteArray recipes;
};

PackageBytes writeValidPackage(const QTemporaryDir &root,
							   const std::function< void(QJsonObject &, QJsonObject &) > &mutate = {}) {
	QDir directory(root.path());
	directory.mkpath(QStringLiteral("deepfilternet"));
	writeFile(directory.filePath(QStringLiteral("rnnoise.dll")), RnnoiseAsset);
	writeFile(directory.filePath(QStringLiteral("deepfilternet/model.tar.gz")), DeepFilterAsset);

	QJsonObject rnnoiseModel{
		{ QStringLiteral("id"), QStringLiteral("rnnoise:embedded") },
		{ QStringLiteral("version"), QStringLiteral("test-rnnoise-v1") },
		{ QStringLiteral("backend"), QStringLiteral("RNNoise") },
		{ QStringLiteral("path"), QStringLiteral("rnnoise.dll") },
		{ QStringLiteral("sha256"),
		  QString::fromLatin1(QCryptographicHash::hash(RnnoiseAsset, QCryptographicHash::Sha256).toHex()) },
		{ QStringLiteral("size"), RnnoiseAsset.size() },
		{ QStringLiteral("licenseSpdx"), QStringLiteral("BSD-3-Clause") },
		{ QStringLiteral("sampleRateHz"), 48000 },
		{ QStringLiteral("algorithmicLatencyMs"), 30 },
		{ QStringLiteral("recipeCompatibility"), QJsonArray{ QStringLiteral("input.balanced.rnnoise-embedded"),
															 QStringLiteral("input.auto.balanced.rnnoise-embedded") } },
	};
	QJsonObject deepFilterModel{
		{ QStringLiteral("id"), QStringLiteral("deepfilternet:balanced") },
		{ QStringLiteral("version"), QStringLiteral("test-deepfilter-v1") },
		{ QStringLiteral("backend"), QStringLiteral("DeepFilterNet") },
		{ QStringLiteral("path"), QStringLiteral("deepfilternet/model.tar.gz") },
		{ QStringLiteral("sha256"),
		  QString::fromLatin1(QCryptographicHash::hash(DeepFilterAsset, QCryptographicHash::Sha256).toHex()) },
		{ QStringLiteral("size"), DeepFilterAsset.size() },
		{ QStringLiteral("licenseSpdx"), QStringLiteral("MIT OR Apache-2.0") },
		{ QStringLiteral("sampleRateHz"), 48000 },
		{ QStringLiteral("algorithmicLatencyMs"), 40 },
		{ QStringLiteral("recipeCompatibility"), QJsonArray{ QStringLiteral("input.crisp.deepfilternet-balanced") } },
	};
	QJsonObject modelRoot{
		{ QStringLiteral("schemaVersion"), 1 },
		{ QStringLiteral("catalogRevision"), CatalogRevision },
		{ QStringLiteral("generatedFromAssets"), true },
		{ QStringLiteral("models"), QJsonArray{ rnnoiseModel, deepFilterModel } },
	};

	QJsonObject recipeRoot{
		{ QStringLiteral("schemaVersion"), 1 },
		{ QStringLiteral("catalogRevision"), CatalogRevision },
		{ QStringLiteral("modelManifestSha256"), QString(64, QLatin1Char('0')) },
		{ QStringLiteral("recipes"),
		  QJsonArray{
			  recipe(QStringLiteral("input.original"), QStringLiteral("Original"), QStringLiteral("None"), {},
					 QStringLiteral("Low"), 0),
			  recipe(QStringLiteral("input.light.speex"), QStringLiteral("Light"), QStringLiteral("Speex"), {},
					 QStringLiteral("Low"), 10),
			  recipe(QStringLiteral("input.balanced.rnnoise-embedded"), QStringLiteral("Balanced"),
					 QStringLiteral("RNNoise"), QJsonArray{ QStringLiteral("rnnoise:embedded") },
					 QStringLiteral("Standard"), 30),
			  recipe(QStringLiteral("input.crisp.deepfilternet-balanced"), QStringLiteral("Crisp"),
					 QStringLiteral("DeepFilterNet"), QJsonArray{ QStringLiteral("deepfilternet:balanced") },
					 QStringLiteral("High"), 50),
			  recipe(QStringLiteral("input.auto.balanced.rnnoise-embedded"), QStringLiteral("Auto"),
					 QStringLiteral("RNNoise"), QJsonArray{ QStringLiteral("rnnoise:embedded") },
					 QStringLiteral("Standard"), 30),
		  } },
	};
	if (mutate) {
		mutate(modelRoot, recipeRoot);
	}
	const QByteArray modelBytes = QJsonDocument(modelRoot).toJson(QJsonDocument::Indented);
	recipeRoot.insert(QStringLiteral("modelManifestSha256"),
					  QString::fromLatin1(QCryptographicHash::hash(modelBytes, QCryptographicHash::Sha256).toHex()));
	const QByteArray recipeBytes = QJsonDocument(recipeRoot).toJson(QJsonDocument::Indented);
	writeFile(directory.filePath(QStringLiteral("input-models.json")), modelBytes);
	writeFile(directory.filePath(QStringLiteral("input-recipes.json")), recipeBytes);
	writeFile(directory.filePath(QStringLiteral("input-models.json.sig")), sign(modelBytes));
	writeFile(directory.filePath(QStringLiteral("input-recipes.json.sig")), sign(recipeBytes));
	return { modelBytes, recipeBytes };
}

InputEnhancementPackageVerifier::Configuration configuration(const QTemporaryDir &root) {
	return { QDir(root.path()), PublicKey, 100, CatalogRevision };
}

int errorValue(PackageVerificationError error) {
	return static_cast< int >(error);
}
} // namespace

class TestInputEnhancementPackageVerifier : public QObject {
	Q_OBJECT

private slots:
	void acceptsExactSignedCatalogAndPublishesModelHashes();
	void bindsRuntimeRecipesToSignedCatalog();
	void rejectsChangedManifestSignatureAndAssetBytes();
	void rejectsUnsafePathsUnknownReferencesAndWrongCatalog_data();
	void rejectsUnsafePathsUnknownReferencesAndWrongCatalog();
	void rejectsMismatchedExecutionSemantics_data();
	void rejectsMismatchedExecutionSemantics();
	void missingSignatureAndReleaseKeyFailClosed();
	void onlyKeylessBuildZeroCanRunUnmanaged();
};

void TestInputEnhancementPackageVerifier::acceptsExactSignedCatalogAndPublishesModelHashes() {
	QTemporaryDir root;
	QVERIFY(root.isValid());
	writeValidPackage(root);
	InputEnhancementPackageVerifier verifier(configuration(root));
	QVERIFY(!verifier.readyForHealthMarker());
	const PackageVerificationReport report = verifier.verify();
	QVERIFY(report.ready);
	QVERIFY(report.verified);
	QVERIFY(!report.unmanaged);
	QVERIFY(verifier.readyForHealthMarker());
	QVERIFY(verifier.verificationHealthy());
	QVERIFY(verifier.hasVerifiedPackage());
	QCOMPARE(verifier.catalogRevision(), CatalogRevision);
	QCOMPARE(verifier.modelSha256Hex(QStringLiteral("rnnoise:embedded")),
			 QString::fromLatin1(QCryptographicHash::hash(RnnoiseAsset, QCryptographicHash::Sha256).toHex()));
	QCOMPARE(verifier.modelRelativePath(QStringLiteral("rnnoise:embedded")), QStringLiteral("rnnoise.dll"));
	QVERIFY(verifier.modelRelativePath(QStringLiteral("missing:model")).isEmpty());
	QVERIFY(verifier.modelAuthorized(QStringLiteral("rnnoise:embedded")));
	QVERIFY(verifier.modelAuthorized(QStringLiteral("rnnoise:embedded"),
									 QDir(root.path()).filePath(QStringLiteral("rnnoise.dll"))));
	QVERIFY(!verifier.modelAuthorized(QStringLiteral("rnnoise:embedded"),
									  QDir(root.path()).filePath(QStringLiteral("deepfilternet/model.tar.gz"))));
	QVERIFY(
		verifier.modelsAuthorized({ QStringLiteral("rnnoise:embedded"), QStringLiteral("deepfilternet:balanced") }));
}

void TestInputEnhancementPackageVerifier::bindsRuntimeRecipesToSignedCatalog() {
	QTemporaryDir root;
	QVERIFY(root.isValid());
	writeValidPackage(root);
	InputEnhancementPackageVerifier verifier(configuration(root));
	QVERIFY(verifier.verify().verified);

	BackendAvailability availability{ true, true, true };
	for (const Profile profile :
		 { Profile::Original, Profile::Light, Profile::Balanced, Profile::Crisp, Profile::Auto }) {
		ResolveRequest request;
		request.profile             = profile;
		request.noiseReduction      = 50;
		request.naturalCrisp        = 50;
		request.cpuClass            = profile == Profile::Auto ? CpuClass::Standard : CpuClass::High;
		request.backendAvailability = availability;
		QVERIFY(verifier.recipeAuthorized(RecipeCatalog::resolve(request)));
	}

	QTemporaryDir narrowedRoot;
	QVERIFY(narrowedRoot.isValid());
	writeValidPackage(narrowedRoot, [](QJsonObject &, QJsonObject &recipes) {
		QJsonArray entries   = recipes.value(QStringLiteral("recipes")).toArray();
		QJsonObject balanced = entries.at(2).toObject();
		balanced.insert(QStringLiteral("noiseReductionRange"), QJsonArray{ 0, 10 });
		entries.replace(2, balanced);
		recipes.insert(QStringLiteral("recipes"), entries);
	});
	InputEnhancementPackageVerifier narrowedVerifier(configuration(narrowedRoot));
	QVERIFY(narrowedVerifier.verify().verified);
	ResolveRequest balancedRequest;
	balancedRequest.profile             = Profile::Balanced;
	balancedRequest.noiseReduction      = 50;
	balancedRequest.naturalCrisp        = 50;
	balancedRequest.cpuClass            = CpuClass::Standard;
	balancedRequest.backendAvailability = availability;
	QVERIFY(!narrowedVerifier.recipeAuthorized(RecipeCatalog::resolve(balancedRequest)));
}

void TestInputEnhancementPackageVerifier::rejectsChangedManifestSignatureAndAssetBytes() {
	QTemporaryDir changedManifestRoot;
	QVERIFY(changedManifestRoot.isValid());
	writeValidPackage(changedManifestRoot);
	QFile modelManifest(QDir(changedManifestRoot.path()).filePath(QStringLiteral("input-models.json")));
	QVERIFY(modelManifest.open(QIODevice::Append));
	QCOMPARE(modelManifest.write(" "), qint64(1));
	modelManifest.close();
	InputEnhancementPackageVerifier changedManifestVerifier(configuration(changedManifestRoot));
	QCOMPARE(errorValue(changedManifestVerifier.verify().error),
			 errorValue(PackageVerificationError::InvalidSignature));
	QVERIFY(changedManifestVerifier.readyForHealthMarker());
	QVERIFY(!changedManifestVerifier.verificationHealthy());

	QTemporaryDir changedAssetRoot;
	QVERIFY(changedAssetRoot.isValid());
	writeValidPackage(changedAssetRoot);
	QVERIFY(writeFile(QDir(changedAssetRoot.path()).filePath(QStringLiteral("rnnoise.dll")),
					  QByteArray(RnnoiseAsset.size(), 'X')));
	InputEnhancementPackageVerifier changedAssetVerifier(configuration(changedAssetRoot));
	QCOMPARE(errorValue(changedAssetVerifier.verify().error), errorValue(PackageVerificationError::AssetHashMismatch));
	QVERIFY(!changedAssetVerifier.modelAuthorized(QStringLiteral("rnnoise:embedded")));

	QTemporaryDir changedAfterVerificationRoot;
	QVERIFY(changedAfterVerificationRoot.isValid());
	writeValidPackage(changedAfterVerificationRoot);
	InputEnhancementPackageVerifier rehashingVerifier(configuration(changedAfterVerificationRoot));
	QVERIFY(rehashingVerifier.verify().verified);
	QVERIFY(writeFile(QDir(changedAfterVerificationRoot.path()).filePath(QStringLiteral("rnnoise.dll")),
					  QByteArray(RnnoiseAsset.size(), 'Y')));
	QVERIFY(!rehashingVerifier.modelAuthorized(QStringLiteral("rnnoise:embedded")));
}

void TestInputEnhancementPackageVerifier::rejectsUnsafePathsUnknownReferencesAndWrongCatalog_data() {
	QTest::addColumn< int >("mutation");
	QTest::addColumn< int >("expectedError");
	QTest::newRow("unsafe-path") << 0 << errorValue(PackageVerificationError::UnsafeAssetPath);
	QTest::newRow("unknown-model-reference") << 1 << errorValue(PackageVerificationError::InvalidModelReference);
	QTest::newRow("wrong-catalog") << 2 << errorValue(PackageVerificationError::CatalogMismatch);
}

void TestInputEnhancementPackageVerifier::rejectsUnsafePathsUnknownReferencesAndWrongCatalog() {
	QFETCH(int, mutation);
	QFETCH(int, expectedError);
	QTemporaryDir root;
	QVERIFY(root.isValid());
	writeValidPackage(root, [mutation](QJsonObject &models, QJsonObject &recipes) {
		if (mutation == 0) {
			QJsonArray entries = models.value(QStringLiteral("models")).toArray();
			QJsonObject first  = entries.at(0).toObject();
			first.insert(QStringLiteral("path"), QStringLiteral("deepfilternet/../rnnoise.dll"));
			entries.replace(0, first);
			models.insert(QStringLiteral("models"), entries);
		} else if (mutation == 1) {
			QJsonArray entries   = recipes.value(QStringLiteral("recipes")).toArray();
			QJsonObject balanced = entries.at(2).toObject();
			balanced.insert(QStringLiteral("modelIds"), QJsonArray{ QStringLiteral("unknown:model") });
			entries.replace(2, balanced);
			recipes.insert(QStringLiteral("recipes"), entries);
		} else {
			models.insert(QStringLiteral("catalogRevision"), QStringLiteral("input-recipes-v2"));
			recipes.insert(QStringLiteral("catalogRevision"), QStringLiteral("input-recipes-v2"));
		}
	});
	InputEnhancementPackageVerifier verifier(configuration(root));
	QCOMPARE(errorValue(verifier.verify().error), expectedError);
	QVERIFY(!verifier.verificationHealthy());
}

void TestInputEnhancementPackageVerifier::rejectsMismatchedExecutionSemantics_data() {
	QTest::addColumn< QString >("field");
	QTest::newRow("execution-semantics") << QStringLiteral("executionSemanticsVersion");
	QTest::newRow("mix-curve") << QStringLiteral("mixCurveVersion");
	QTest::newRow("adaptation-policy") << QStringLiteral("adaptationPolicyVersion");
}

void TestInputEnhancementPackageVerifier::rejectsMismatchedExecutionSemantics() {
	QFETCH(QString, field);
	QTemporaryDir root;
	QVERIFY(root.isValid());
	writeValidPackage(root, [&field](QJsonObject &, QJsonObject &recipes) {
		QJsonArray entries = recipes.value(QStringLiteral("recipes")).toArray();
		QJsonObject first  = entries.at(0).toObject();
		first.insert(field, first.value(field).toInt() + 1);
		entries.replace(0, first);
		recipes.insert(QStringLiteral("recipes"), entries);
	});
	InputEnhancementPackageVerifier verifier(configuration(root));
	QCOMPARE(errorValue(verifier.verify().error), errorValue(PackageVerificationError::InvalidSchema));
	QVERIFY(!verifier.verificationHealthy());
}

void TestInputEnhancementPackageVerifier::missingSignatureAndReleaseKeyFailClosed() {
	QTemporaryDir missingSignatureRoot;
	QVERIFY(missingSignatureRoot.isValid());
	writeValidPackage(missingSignatureRoot);
	QVERIFY(QFile::remove(QDir(missingSignatureRoot.path()).filePath(QStringLiteral("input-recipes.json.sig"))));
	InputEnhancementPackageVerifier missingSignatureVerifier(configuration(missingSignatureRoot));
	QCOMPARE(errorValue(missingSignatureVerifier.verify().error),
			 errorValue(PackageVerificationError::MissingSignature));
	QVERIFY(!missingSignatureVerifier.verificationHealthy());

	QTemporaryDir keylessRoot;
	QVERIFY(keylessRoot.isValid());
	auto keylessConfiguration = configuration(keylessRoot);
	keylessConfiguration.rawPublicKey.clear();
	keylessConfiguration.currentBuild = 1;
	InputEnhancementPackageVerifier keylessVerifier(std::move(keylessConfiguration));
	QCOMPARE(errorValue(keylessVerifier.verify().error), errorValue(PackageVerificationError::InvalidPublicKey));
	QVERIFY(keylessVerifier.readyForHealthMarker());
	QVERIFY(!keylessVerifier.verificationHealthy());
	QVERIFY(!keylessVerifier.modelAuthorized(QStringLiteral("rnnoise:embedded")));
}

void TestInputEnhancementPackageVerifier::onlyKeylessBuildZeroCanRunUnmanaged() {
	QTemporaryDir root;
	QVERIFY(root.isValid());
	auto developmentConfiguration = configuration(root);
	developmentConfiguration.rawPublicKey.clear();
	developmentConfiguration.currentBuild = 0;
	InputEnhancementPackageVerifier verifier(std::move(developmentConfiguration));
	const PackageVerificationReport report = verifier.verify();
	QVERIFY(report.ready);
	QVERIFY(report.unmanaged);
	QVERIFY(!report.verified);
	QVERIFY(!verifier.managedBySignedPackage());
	QVERIFY(verifier.verificationHealthy());
	QVERIFY(verifier.modelAuthorized(QStringLiteral("local:e2e-model"), QStringLiteral("missing.bin")));
}

QTEST_GUILESS_MAIN(TestInputEnhancementPackageVerifier)
#include "TestInputEnhancementPackageVerifier.moc"
