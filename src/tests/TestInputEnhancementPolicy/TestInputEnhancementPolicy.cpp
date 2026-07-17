// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementChannelPointer.h"
#include "InputEnhancementPolicy.h"

#include <QtTest>

#include <QCryptographicHash>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <memory>

#ifndef MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX
#	define MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX ""
#endif

using namespace Mumble::InputEnhancement;

namespace {
constexpr std::uint64_t CurrentBuild  = 100;
constexpr qint64 MaximumFutureSeconds = 2 * 24 * 60 * 60;

const QByteArray PublicKey(32, 'K');
const QByteArray Signature(64, 'S');
const QDateTime Now = QDateTime::fromString(QStringLiteral("2026-07-14T12:00:00Z"), Qt::ISODate).toUTC();

class FakeVerifier final : public DetachedSignatureVerifier {
public:
	bool available = true;
	QByteArray expectedPublicKey;
	QByteArray expectedManifest;
	QByteArray expectedSignature;
	mutable int calls = 0;

	bool isAvailable() const noexcept override { return available; }
	bool verify(const QByteArray &rawPublicKey, const QByteArray &canonicalManifest,
				const QByteArray &detachedSignature) const noexcept override {
		++calls;
		return rawPublicKey == expectedPublicKey && canonicalManifest == expectedManifest
			   && detachedSignature == expectedSignature;
	}
};

PolicyManifest validPolicy(Profile profile = Profile::Balanced, int expiresInSeconds = 3600) {
	PolicyManifest policy;
	policy.available          = true;
	policy.forceOriginal      = false;
	policy.recommendedProfile = profile;
	policy.recipeSetVersion   = QStringLiteral("input-recipes-v2");
	policy.minBuild           = 90;
	policy.expiresAt          = Now.addSecs(expiresInSeconds);
	return policy;
}

std::shared_ptr< FakeVerifier > verifierFor(const QByteArray &manifest) {
	auto verifier               = std::make_shared< FakeVerifier >();
	verifier->expectedPublicKey = PublicKey;
	verifier->expectedManifest  = manifest;
	verifier->expectedSignature = Signature;
	return verifier;
}

std::unique_ptr< SignedPolicyStore > storeFor(const std::shared_ptr< FakeVerifier > &verifier,
											  QByteArray publicKey = PublicKey) {
	return std::make_unique< SignedPolicyStore >(std::move(publicKey), verifier, CurrentBuild,
												 QStringLiteral("input-recipes-v2"), MaximumFutureSeconds);
}

QJsonObject validChannelPointer(const QString &channel = QStringLiteral("stable")) {
	const QString tag       = QStringLiteral("mumble-forked-build-101-abcdef123456");
	const QString sourceSha = QStringLiteral("abcdef123456") + QString(28, QLatin1Char('a'));
	return QJsonObject{
		{ QStringLiteral("schemaVersion"), 2 },
		{ QStringLiteral("channel"), channel },
		{ QStringLiteral("channelTag"), QStringLiteral("mumble-forked-%1").arg(channel) },
		{ QStringLiteral("immutableTag"), tag },
		{ QStringLiteral("buildId"), tag },
		{ QStringLiteral("buildNumber"), 101 },
		{ QStringLiteral("sourceSha"), sourceSha },
		{ QStringLiteral("artifact"),
		  QJsonObject{ { QStringLiteral("fileName"), QStringLiteral("mumble-forked-1.7.101.mumble-update") },
					   { QStringLiteral("sha256"), QString(64, QLatin1Char('b')) },
					   { QStringLiteral("size"), 123456 },
					   { QStringLiteral("url"),
						 QStringLiteral("https://github.com/dankmaster/mumble-forked/releases/download/%1/"
										"mumble-forked-1.7.101.mumble-update")
							 .arg(tag) } } },
		{ QStringLiteral("installer"),
		  QJsonObject{ { QStringLiteral("fileName"), QStringLiteral("mumble-forked-1.7.101.msi") },
					   { QStringLiteral("sha256"), QString(64, QLatin1Char('2')) },
					   { QStringLiteral("size"), 234567 },
					   { QStringLiteral("executableSha256"), QString(64, QLatin1Char('9')) },
					   { QStringLiteral("url"),
						 QStringLiteral("https://github.com/dankmaster/mumble-forked/releases/download/%1/"
									"mumble-forked-1.7.101.msi")
							 .arg(tag) } } },
		{ QStringLiteral("recoveryInstallers"),
		  QJsonArray{
			  QJsonObject{ { QStringLiteral("immutableTag"),
							QStringLiteral("mumble-forked-build-100-111111111111") },
						 { QStringLiteral("fileName"), QStringLiteral("mumble-forked-1.7.100.msi") },
						 { QStringLiteral("sha256"), QString(64, QLatin1Char('3')) },
						 { QStringLiteral("size"), 220000 },
						 { QStringLiteral("url"),
						   QStringLiteral("https://github.com/dankmaster/mumble-forked/releases/download/"
									  "mumble-forked-build-100-111111111111/mumble-forked-1.7.100.msi") } },
			  QJsonObject{ { QStringLiteral("immutableTag"),
							QStringLiteral("mumble-forked-build-99-222222222222") },
						 { QStringLiteral("fileName"), QStringLiteral("mumble-forked-1.7.99.msi") },
						 { QStringLiteral("sha256"), QString(64, QLatin1Char('4')) },
						 { QStringLiteral("size"), 210000 },
						 { QStringLiteral("url"),
						   QStringLiteral("https://github.com/dankmaster/mumble-forked/releases/download/"
									  "mumble-forked-build-99-222222222222/mumble-forked-1.7.99.msi") } } } },
		{ QStringLiteral("qualification"),
		  QJsonObject{ { QStringLiteral("sha256"), QString(64, QLatin1Char('c')) },
					   { QStringLiteral("url"),
						 QStringLiteral("https://github.com/dankmaster/mumble-forked/releases/download/%1/"
										"qualification.json")
							 .arg(tag) } } },
		{ QStringLiteral("releaseSmoke"),
		  QJsonObject{ { QStringLiteral("sha256"), QString(64, QLatin1Char('1')) },
					   { QStringLiteral("url"),
						 QStringLiteral("https://github.com/dankmaster/mumble-forked/releases/download/%1/"
										"release-smoke.json")
							 .arg(tag) } } },
		{ QStringLiteral("modelManifestSha256"), QString(64, QLatin1Char('d')) },
		{ QStringLiteral("recipeManifestSha256"), QString(64, QLatin1Char('e')) },
		{ QStringLiteral("inputEnhancementPolicy"),
		  QJsonObject{
			  { QStringLiteral("fileName"), QStringLiteral("input-enhancement-policy.json") },
			  { QStringLiteral("sha256"), QString(64, QLatin1Char('f')) },
			  { QStringLiteral("signatureFileName"), QStringLiteral("input-enhancement-policy.json.sig") },
			  { QStringLiteral("signatureSha256"), QString(64, QLatin1Char('0')) },
			  { QStringLiteral("url"), QStringLiteral("https://github.com/dankmaster/mumble-forked/releases/download/"
													  "mumble-forked-%1/input-enhancement-policy.json")
										   .arg(channel) } } },
		{ QStringLiteral("detachedSignature"),
		  QJsonObject{ { QStringLiteral("algorithm"), QStringLiteral("Ed25519") },
					   { QStringLiteral("encoding"), QStringLiteral("raw") },
					   { QStringLiteral("fileName"), QStringLiteral("channel-pointer.json.sig") },
					   { QStringLiteral("publicKeyHex"), QString::fromLatin1(PublicKey.toHex()) } } },
		{ QStringLiteral("knownGoodTags"),
		  QJsonArray{ tag, QStringLiteral("mumble-forked-build-100-111111111111"),
					  QStringLiteral("mumble-forked-build-99-222222222222") } },
		{ QStringLiteral("announcement"), QStringLiteral("Qualified input enhancement preview") },
		// PowerShell DateTime.ToString("o") emits seven fractional digits.
		{ QStringLiteral("promotedAtUtc"), QStringLiteral("2026-07-14T12:00:00.1234567Z") }
	};
}

QByteArray channelPointerBytes(const QJsonObject &pointer) {
	return QJsonDocument(pointer).toJson(QJsonDocument::Compact);
}

QByteArray validChannelPointerBytes(const QString &channel = QStringLiteral("stable")) {
	return channelPointerBytes(validChannelPointer(channel));
}

ChannelPointerDecision verifyPointerBytes(const QByteArray &pointer,
										  const QString &expectedChannel = QStringLiteral("stable")) {
	FakeVerifier verifier;
	verifier.expectedPublicKey = PublicKey;
	verifier.expectedManifest  = pointer;
	verifier.expectedSignature = Signature;
	return verifyAndNormalizeChannelPointer(pointer, Signature, PublicKey, verifier, expectedChannel);
}

int rejectionValue(PolicyRejection rejection) {
	return static_cast< int >(rejection);
}
} // namespace

class TestInputEnhancementPolicy : public QObject {
	Q_OBJECT

private slots:
	void emitsAndRequiresCanonicalManifestBytes();
	void acceptsVerifiedPolicyAndExposesRestorableCache();
	void invalidCandidateUsesLastValidCache();
	void invalidWithoutCacheFailsClosedToOriginal();
	void rejectsInvalidSchema_data();
	void rejectsInvalidSchema();
	void rejectsExpiredFutureMinBuildAndRecipeMismatch_data();
	void rejectsExpiredFutureMinBuildAndRecipeMismatch();
	void rejectsUnavailableCryptoAndInvalidPublicKey();
	void expiredCacheIsNeverEffective();
	void verifiesRfc8032VectorWithOpenSsl();
	void exposesOnlyAValidConfiguredReleasePublicKey();
	void emitsMachineReadableConfiguredBuildIdentity();
	void configuredReleaseKeyVerifiesExactBytesOrFailsClosed();
	void acceptsSignedChannelPointerAsInstallablePackage();
	void verifiesChannelPointerSignatureOverExactBytes();
	void rejectsMalformedChannelPointerReferences_data();
	void rejectsMalformedChannelPointerReferences();
	void rejectsUntrustedOrWrongChannelPointers();
};

void TestInputEnhancementPolicy::acceptsSignedChannelPointerAsInstallablePackage() {
	const QByteArray pointer = validChannelPointerBytes();
	FakeVerifier verifier;
	verifier.expectedPublicKey = PublicKey;
	verifier.expectedManifest  = pointer;
	verifier.expectedSignature = Signature;
	const ChannelPointerDecision decision =
		verifyAndNormalizeChannelPointer(pointer, Signature, PublicKey, verifier, QStringLiteral("stable"));
	QVERIFY(decision.accepted);
	QCOMPARE(static_cast< int >(decision.rejection), static_cast< int >(ChannelPointerRejection::None));
	QCOMPARE(decision.updateInfo.value(QStringLiteral("build")).toInt(), 101);
	QCOMPARE(decision.updateInfo.value(QStringLiteral("channel")).toString(), QStringLiteral("stable"));
	const QJsonObject package = decision.updateInfo.value(QStringLiteral("package")).toObject();
	QCOMPARE(package.value(QStringLiteral("format")).toString(), QStringLiteral("mumble-update-v1"));
	QCOMPARE(package.value(QStringLiteral("minUpdaterVersion")).toInt(), 4);
	QCOMPARE(package.value(QStringLiteral("sha256")).toString(), QString(64, QLatin1Char('b')));
	QCOMPARE(package.value(QStringLiteral("size")).toInt(), 123456);
	QCOMPARE(package.value(QStringLiteral("url")).toString(),
			 QStringLiteral("https://github.com/dankmaster/mumble-forked/releases/download/"
							"mumble-forked-build-101-abcdef123456/"
							"mumble-forked-1.7.101.mumble-update"));
	const QJsonObject installer = decision.updateInfo.value(QStringLiteral("installer")).toObject();
	QCOMPARE(installer.value(QStringLiteral("sha256")).toString(), QString(64, QLatin1Char('2')));
	QCOMPARE(installer.value(QStringLiteral("executableSha256")).toString(), QString(64, QLatin1Char('9')));
	QCOMPARE(decision.updateInfo.value(QStringLiteral("recoveryInstallers")).toArray().size(), 2);
	QCOMPARE(decision.updateInfo.value(QStringLiteral("releaseUrl")).toString(),
			 QStringLiteral("https://github.com/dankmaster/mumble-forked/releases/tag/"
							"mumble-forked-build-101-abcdef123456"));
	QCOMPARE(decision.updateInfo.value(QStringLiteral("publishedAt")).toString(),
			 QStringLiteral("2026-07-14T12:00:00.123Z"));
}

void TestInputEnhancementPolicy::verifiesChannelPointerSignatureOverExactBytes() {
	const QByteArray signedBytes = validChannelPointerBytes();
	FakeVerifier verifier;
	verifier.expectedPublicKey = PublicKey;
	verifier.expectedManifest  = signedBytes;
	verifier.expectedSignature = Signature;

	QByteArray changedBytes = signedBytes;
	changedBytes.append('\n');
	const ChannelPointerDecision decision =
		verifyAndNormalizeChannelPointer(changedBytes, Signature, PublicKey, verifier, QStringLiteral("stable"));
	QVERIFY(!decision.accepted);
	QCOMPARE(static_cast< int >(decision.rejection), static_cast< int >(ChannelPointerRejection::InvalidSignature));
	QCOMPARE(verifier.calls, 1);
}

void TestInputEnhancementPolicy::rejectsMalformedChannelPointerReferences_data() {
	QTest::addColumn< QByteArray >("pointer");
	QTest::addColumn< int >("expectedRejection");

	auto add = [](const char *name, const QJsonObject &pointer, ChannelPointerRejection rejection) {
		QTest::newRow(name) << channelPointerBytes(pointer) << static_cast< int >(rejection);
	};

	QJsonObject pointer = validChannelPointer();
	pointer.insert(QStringLiteral("promotedAtUtc"), QStringLiteral("2026-07-14T12:00:00.000Z"));
	add("non-powershell-timestamp", pointer, ChannelPointerRejection::InvalidSchema);

	pointer = validChannelPointer();
	pointer.insert(QStringLiteral("buildNumber"), 102);
	add("tag-build-number-mismatch", pointer, ChannelPointerRejection::InvalidSchema);

	pointer = validChannelPointer();
	pointer.insert(QStringLiteral("sourceSha"), QString(40, QLatin1Char('a')));
	add("tag-source-mismatch", pointer, ChannelPointerRejection::InvalidSchema);

	pointer = validChannelPointer();
	QString hashWithNewline(64, QLatin1Char('d'));
	hashWithNewline.append(QLatin1Char('\n'));
	pointer.insert(QStringLiteral("modelManifestSha256"), hashWithNewline);
	add("hash-trailing-newline", pointer, ChannelPointerRejection::InvalidSchema);

	pointer              = validChannelPointer();
	QJsonObject artifact = pointer.value(QStringLiteral("artifact")).toObject();
	artifact.insert(QStringLiteral("url"),
					QStringLiteral("https://evil.example/dankmaster/mumble-forked/releases/download/"
								   "mumble-forked-build-101-abcdef123456/"
								   "mumble-forked-1.7.101.mumble-update"));
	pointer.insert(QStringLiteral("artifact"), artifact);
	add("lookalike-artifact-host", pointer, ChannelPointerRejection::UnsafeArtifact);

	pointer             = validChannelPointer();
	artifact            = pointer.value(QStringLiteral("artifact")).toObject();
	QString artifactUrl = artifact.value(QStringLiteral("url")).toString();
	artifactUrl.append(QStringLiteral("?download=1"));
	artifact.insert(QStringLiteral("url"), artifactUrl);
	pointer.insert(QStringLiteral("artifact"), artifact);
	add("artifact-query", pointer, ChannelPointerRejection::UnsafeArtifact);

	pointer     = validChannelPointer();
	artifact    = pointer.value(QStringLiteral("artifact")).toObject();
	artifactUrl = artifact.value(QStringLiteral("url")).toString();
	artifactUrl.append(QLatin1Char('\n'));
	artifact.insert(QStringLiteral("url"), artifactUrl);
	pointer.insert(QStringLiteral("artifact"), artifact);
	add("artifact-trailing-newline", pointer, ChannelPointerRejection::UnsafeArtifact);

	pointer                   = validChannelPointer();
	QJsonObject qualification = pointer.value(QStringLiteral("qualification")).toObject();
	qualification.insert(QStringLiteral("extra"), true);
	pointer.insert(QStringLiteral("qualification"), qualification);
	add("qualification-unknown-field", pointer, ChannelPointerRejection::InvalidSchema);

	pointer       = validChannelPointer();
	qualification = pointer.value(QStringLiteral("qualification")).toObject();
	qualification.insert(QStringLiteral("url"),
						 QStringLiteral("https://github.com/other/repository/releases/download/"
										"mumble-forked-build-101-abcdef123456/qualification.json"));
	pointer.insert(QStringLiteral("qualification"), qualification);
	add("qualification-repository-mismatch", pointer, ChannelPointerRejection::InvalidSchema);

	pointer                  = validChannelPointer();
	QJsonObject releaseSmoke = pointer.value(QStringLiteral("releaseSmoke")).toObject();
	releaseSmoke.insert(QStringLiteral("url"),
						QStringLiteral("https://github.com/dankmaster/mumble-forked/releases/download/"
									   "mumble-forked-build-101-abcdef123456/other-smoke.json"));
	pointer.insert(QStringLiteral("releaseSmoke"), releaseSmoke);
	add("release-smoke-file-name", pointer, ChannelPointerRejection::InvalidSchema);

	pointer = validChannelPointer();
	pointer.remove(QStringLiteral("releaseSmoke"));
	add("missing-release-smoke", pointer, ChannelPointerRejection::InvalidSchema);

	pointer            = validChannelPointer();
	QJsonObject policy = pointer.value(QStringLiteral("inputEnhancementPolicy")).toObject();
	policy.insert(QStringLiteral("url"), QStringLiteral("https://github.com/dankmaster/mumble-forked/releases/download/"
														"mumble-forked-preview/input-enhancement-policy.json"));
	pointer.insert(QStringLiteral("inputEnhancementPolicy"), policy);
	add("policy-channel-mismatch", pointer, ChannelPointerRejection::InvalidSchema);

	pointer = validChannelPointer();
	policy  = pointer.value(QStringLiteral("inputEnhancementPolicy")).toObject();
	policy.insert(QStringLiteral("signatureSha256"), QStringLiteral("not-a-sha"));
	pointer.insert(QStringLiteral("inputEnhancementPolicy"), policy);
	add("policy-signature-hash", pointer, ChannelPointerRejection::InvalidSchema);
}

void TestInputEnhancementPolicy::rejectsMalformedChannelPointerReferences() {
	QFETCH(QByteArray, pointer);
	QFETCH(int, expectedRejection);
	const ChannelPointerDecision decision = verifyPointerBytes(pointer);
	QVERIFY(!decision.accepted);
	QCOMPARE(static_cast< int >(decision.rejection), expectedRejection);
}

void TestInputEnhancementPolicy::rejectsUntrustedOrWrongChannelPointers() {
	const QByteArray pointer = validChannelPointerBytes();
	FakeVerifier verifier;
	verifier.expectedPublicKey = PublicKey;
	verifier.expectedManifest  = pointer;
	verifier.expectedSignature = Signature;
	ChannelPointerDecision decision =
		verifyAndNormalizeChannelPointer(pointer, QByteArray(64, 'X'), PublicKey, verifier, QStringLiteral("stable"));
	QVERIFY(!decision.accepted);
	QCOMPARE(static_cast< int >(decision.rejection), static_cast< int >(ChannelPointerRejection::InvalidSignature));

	decision = verifyAndNormalizeChannelPointer(pointer, Signature, PublicKey, verifier, QStringLiteral("preview"));
	QVERIFY(!decision.accepted);
	QCOMPARE(static_cast< int >(decision.rejection), static_cast< int >(ChannelPointerRejection::WrongChannel));
}

void TestInputEnhancementPolicy::exposesOnlyAValidConfiguredReleasePublicKey() {
	const QByteArray configuredHex(MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX);
	if (configuredHex.isEmpty()) {
		QVERIFY(!hasConfiguredPolicyPublicKey());
		QVERIFY(configuredPolicyPublicKey().isEmpty());
		QVERIFY(configuredPolicyPublicKeyHex().isEmpty());
		return;
	}

	QCOMPARE(configuredHex.size(), 64);
	QVERIFY(hasConfiguredPolicyPublicKey());
	QCOMPARE(configuredPolicyPublicKey().size(), 32);
	QCOMPARE(configuredPolicyPublicKey(), QByteArray::fromHex(configuredHex));
	QCOMPARE(configuredPolicyPublicKeyHex(), QString::fromLatin1(configuredHex).toLower());
}

void TestInputEnhancementPolicy::emitsMachineReadableConfiguredBuildIdentity() {
	const QJsonDocument positiveDocument = QJsonDocument::fromJson(configuredPolicyBuildIdentity(42));
	QVERIFY(positiveDocument.isObject());
	const QJsonObject positive = positiveDocument.object();
	QCOMPARE(positive.value(QStringLiteral("schemaVersion")).toInt(), 1);
	QCOMPARE(positive.value(QStringLiteral("kind")).toString(),
			 QStringLiteral("mumble-input-enhancement-build-identity"));
	QCOMPARE(positive.value(QStringLiteral("buildNumber")).toInt(), 42);

	const QByteArray configuredHex(MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX);
	if (configuredHex.isEmpty()) {
		QCOMPARE(positive.value(QStringLiteral("packageVerificationMode")).toString(), QStringLiteral("invalid"));
		QVERIFY(positive.value(QStringLiteral("configuredPublicKeySha256")).toString().isEmpty());

		const QJsonObject development = QJsonDocument::fromJson(configuredPolicyBuildIdentity(0)).object();
		QCOMPARE(development.value(QStringLiteral("buildNumber")).toInt(), 0);
		QCOMPARE(development.value(QStringLiteral("packageVerificationMode")).toString(),
				 QStringLiteral("unmanaged-build-zero"));
		QVERIFY(development.value(QStringLiteral("configuredPublicKeySha256")).toString().isEmpty());
		return;
	}

	const QByteArray publicKey = QByteArray::fromHex(configuredHex);
	QCOMPARE(positive.value(QStringLiteral("packageVerificationMode")).toString(),
			 QStringLiteral("managed-signed"));
	QCOMPARE(positive.value(QStringLiteral("configuredPublicKeySha256")).toString(),
			 QString::fromLatin1(QCryptographicHash::hash(publicKey, QCryptographicHash::Sha256).toHex()));

	const QJsonObject zeroBuild = QJsonDocument::fromJson(configuredPolicyBuildIdentity(0)).object();
	QCOMPARE(zeroBuild.value(QStringLiteral("packageVerificationMode")).toString(), QStringLiteral("invalid"));
}

void TestInputEnhancementPolicy::configuredReleaseKeyVerifiesExactBytesOrFailsClosed() {
	const QByteArray signature =
		QByteArray::fromHex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
							"5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
	const QByteArray rfcPublicKeyHex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
	if (QByteArray(MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX).toLower() != rfcPublicKeyHex) {
		QVERIFY(!verifyWithConfiguredPolicyPublicKey(QByteArray{}, signature));
		return;
	}
	QVERIFY(verifyWithConfiguredPolicyPublicKey(QByteArray{}, signature));
	QVERIFY(!verifyWithConfiguredPolicyPublicKey(QByteArray("changed"), signature));
	QByteArray changedSignature = signature;
	changedSignature[0] ^= 1;
	QVERIFY(!verifyWithConfiguredPolicyPublicKey(QByteArray{}, changedSignature));
}

void TestInputEnhancementPolicy::emitsAndRequiresCanonicalManifestBytes() {
	const PolicyManifest policy = validPolicy();
	const QByteArray canonical  = canonicalPolicyBytes(policy);
	QCOMPARE(canonical, QByteArray("{\"available\":true,\"expiresAt\":\"2026-07-14T13:00:00Z\",\"forceOriginal\":false,"
								   "\"minBuild\":90,\"recipeSetVersion\":\"input-recipes-v2\","
								   "\"recommendedProfile\":\"Balanced\"}"));

	auto verifier                 = verifierFor(canonical);
	auto store                    = storeFor(verifier);
	const QByteArray nonCanonical = QByteArray(" \n") + canonical;
	const PolicyDecision rejected = store->accept(nonCanonical, Signature, Now);
	QCOMPARE(rejectionValue(rejected.rejection), rejectionValue(PolicyRejection::NonCanonicalManifest));
	QVERIFY(!rejected.hasVerifiedPolicy);
	QCOMPARE(verifier->calls, 0);
}

void TestInputEnhancementPolicy::acceptsVerifiedPolicyAndExposesRestorableCache() {
	const PolicyManifest policy = validPolicy(Profile::Crisp);
	const QByteArray canonical  = canonicalPolicyBytes(policy);
	auto verifier               = verifierFor(canonical);
	auto store                  = storeFor(verifier);

	const PolicyDecision accepted = store->accept(canonical, Signature, Now);
	QVERIFY(accepted.candidateAccepted);
	QVERIFY(accepted.hasVerifiedPolicy);
	QVERIFY(!accepted.usingCachedPolicy);
	QCOMPARE(accepted.effectivePolicy.recommendedProfile, Profile::Crisp);
	QCOMPARE(accepted.effectivePolicy.recipeSetVersion, QStringLiteral("input-recipes-v2"));
	QVERIFY(store->hasCachedPolicy());
	QCOMPARE(store->cachedManifestBytes(), canonical);
	QCOMPARE(store->cachedSignature(), Signature);

	const PolicyDecision current = store->current(Now.addSecs(10));
	QVERIFY(!current.candidateAccepted);
	QVERIFY(current.usingCachedPolicy);
	QVERIFY(current.hasVerifiedPolicy);
	QCOMPARE(current.effectivePolicy.recommendedProfile, Profile::Crisp);

	auto restoredVerifier = verifierFor(canonical);
	auto restoredStore    = storeFor(restoredVerifier);
	const PolicyDecision restored =
		restoredStore->accept(store->cachedManifestBytes(), store->cachedSignature(), Now.addSecs(20));
	QVERIFY(restored.candidateAccepted);
	QCOMPARE(restored.effectivePolicy.recommendedProfile, Profile::Crisp);
}

void TestInputEnhancementPolicy::invalidCandidateUsesLastValidCache() {
	const QByteArray canonical = canonicalPolicyBytes(validPolicy(Profile::Light));
	auto verifier              = verifierFor(canonical);
	auto store                 = storeFor(verifier);
	QVERIFY(store->accept(canonical, Signature, Now).candidateAccepted);

	PolicyManifest replacement        = validPolicy(Profile::Auto);
	const QByteArray replacementBytes = canonicalPolicyBytes(replacement);
	verifier->expectedManifest        = replacementBytes;
	const PolicyDecision decision     = store->accept(replacementBytes, QByteArray(64, 'X'), Now.addSecs(30));
	QCOMPARE(rejectionValue(decision.rejection), rejectionValue(PolicyRejection::InvalidSignature));
	QVERIFY(!decision.candidateAccepted);
	QVERIFY(decision.usingCachedPolicy);
	QVERIFY(decision.hasVerifiedPolicy);
	QCOMPARE(decision.effectivePolicy.recommendedProfile, Profile::Light);
	QCOMPARE(store->cachedManifestBytes(), canonical);
}

void TestInputEnhancementPolicy::invalidWithoutCacheFailsClosedToOriginal() {
	const QByteArray canonical = canonicalPolicyBytes(validPolicy());
	auto verifier              = verifierFor(canonical);
	auto store                 = storeFor(verifier);

	const PolicyDecision decision = store->accept(canonical, QByteArray(64, 'X'), Now);
	QCOMPARE(rejectionValue(decision.rejection), rejectionValue(PolicyRejection::InvalidSignature));
	QVERIFY(!decision.hasVerifiedPolicy);
	QVERIFY(!decision.effectivePolicy.available);
	QVERIFY(decision.effectivePolicy.forceOriginal);
	QCOMPARE(decision.effectivePolicy.recommendedProfile, Profile::Original);
	QVERIFY(decision.effectivePolicy.recipeSetVersion.isEmpty());
}

void TestInputEnhancementPolicy::rejectsInvalidSchema_data() {
	QTest::addColumn< QByteArray >("manifest");
	QTest::addColumn< int >("expectedRejection");

	QTest::newRow("invalid-json") << QByteArray("{") << rejectionValue(PolicyRejection::InvalidJson);
	QTest::newRow("array-root") << QByteArray("[]") << rejectionValue(PolicyRejection::InvalidRoot);
	QTest::newRow("unknown-field") << QByteArray(
		"{\"available\":true,\"expiresAt\":\"2026-07-14T13:00:00Z\",\"forceOriginal\":false,"
		"\"minBuild\":90,\"recipeSetVersion\":\"input-recipes-v2\","
		"\"recommendedProfile\":\"Balanced\",\"serverFeature\":true}")
								   << rejectionValue(PolicyRejection::UnknownField);
	QTest::newRow("missing-field") << QByteArray("{\"available\":true}")
								   << rejectionValue(PolicyRejection::MissingField);
	QTest::newRow("wrong-type") << QByteArray(
		"{\"available\":\"yes\",\"expiresAt\":\"2026-07-14T13:00:00Z\","
		"\"forceOriginal\":false,\"minBuild\":90,\"recipeSetVersion\":\"input-recipes-v2\","
		"\"recommendedProfile\":\"Balanced\"}")
								<< rejectionValue(PolicyRejection::InvalidFieldType);
	QTest::newRow("unknown-profile") << QByteArray(
		"{\"available\":true,\"expiresAt\":\"2026-07-14T13:00:00Z\",\"forceOriginal\":false,"
		"\"minBuild\":90,\"recipeSetVersion\":\"input-recipes-v2\","
		"\"recommendedProfile\":\"Ultra\"}")
									 << rejectionValue(PolicyRejection::InvalidFieldValue);
	QTest::newRow("manual-only-voice-focus") << QByteArray(
		"{\"available\":true,\"expiresAt\":\"2026-07-14T13:00:00Z\",\"forceOriginal\":false,"
		"\"minBuild\":90,\"recipeSetVersion\":\"input-recipes-v2\","
		"\"recommendedProfile\":\"VoiceFocus\"}")
										 << rejectionValue(PolicyRejection::InvalidFieldValue);
	QTest::newRow("fractional-build") << QByteArray(
		"{\"available\":true,\"expiresAt\":\"2026-07-14T13:00:00Z\",\"forceOriginal\":false,"
		"\"minBuild\":90.5,\"recipeSetVersion\":\"input-recipes-v2\","
		"\"recommendedProfile\":\"Balanced\"}")
									  << rejectionValue(PolicyRejection::InvalidFieldValue);
	QTest::newRow("offset-timestamp") << QByteArray(
		"{\"available\":true,\"expiresAt\":\"2026-07-14T15:00:00+02:00\","
		"\"forceOriginal\":false,\"minBuild\":90,\"recipeSetVersion\":\"input-recipes-v2\","
		"\"recommendedProfile\":\"Balanced\"}")
									  << rejectionValue(PolicyRejection::InvalidFieldValue);
}

void TestInputEnhancementPolicy::rejectsInvalidSchema() {
	QFETCH(QByteArray, manifest);
	QFETCH(int, expectedRejection);
	auto verifier                 = verifierFor(manifest);
	auto store                    = storeFor(verifier);
	const PolicyDecision decision = store->accept(manifest, Signature, Now);
	QCOMPARE(rejectionValue(decision.rejection), expectedRejection);
	QVERIFY(!decision.hasVerifiedPolicy);
	QCOMPARE(verifier->calls, 0);
}

void TestInputEnhancementPolicy::rejectsExpiredFutureMinBuildAndRecipeMismatch_data() {
	QTest::addColumn< int >("expirationOffset");
	QTest::addColumn< qulonglong >("minBuild");
	QTest::addColumn< QString >("recipeSetVersion");
	QTest::addColumn< int >("expectedRejection");

	QTest::newRow("expired") << -1 << qulonglong(90) << QStringLiteral("input-recipes-v2")
							 << rejectionValue(PolicyRejection::Expired);
	QTest::newRow("too-far-future") << int(MaximumFutureSeconds + 1) << qulonglong(90)
									<< QStringLiteral("input-recipes-v2")
									<< rejectionValue(PolicyRejection::ExpirationTooFarInFuture);
	QTest::newRow("minimum-build") << 3600 << qulonglong(CurrentBuild + 1) << QStringLiteral("input-recipes-v2")
								   << rejectionValue(PolicyRejection::MinimumBuildNotMet);
	QTest::newRow("recipe-set") << 3600 << qulonglong(90) << QStringLiteral("input-recipes-v3")
								<< rejectionValue(PolicyRejection::RecipeSetMismatch);
}

void TestInputEnhancementPolicy::rejectsExpiredFutureMinBuildAndRecipeMismatch() {
	QFETCH(int, expirationOffset);
	QFETCH(qulonglong, minBuild);
	QFETCH(QString, recipeSetVersion);
	PolicyManifest policy         = validPolicy(Profile::Balanced, expirationOffset);
	policy.minBuild               = minBuild;
	policy.recipeSetVersion       = recipeSetVersion;
	const QByteArray canonical    = canonicalPolicyBytes(policy);
	auto verifier                 = verifierFor(canonical);
	auto store                    = storeFor(verifier);
	const PolicyDecision decision = store->accept(canonical, Signature, Now);
	QFETCH(int, expectedRejection);
	QCOMPARE(rejectionValue(decision.rejection), expectedRejection);
	QVERIFY(!decision.hasVerifiedPolicy);
	QCOMPARE(verifier->calls, 1);
}

void TestInputEnhancementPolicy::rejectsUnavailableCryptoAndInvalidPublicKey() {
	const QByteArray canonical = canonicalPolicyBytes(validPolicy());
	auto unavailable           = verifierFor(canonical);
	unavailable->available     = false;
	auto noCryptoStore         = storeFor(unavailable);
	QCOMPARE(rejectionValue(noCryptoStore->accept(canonical, Signature, Now).rejection),
			 rejectionValue(PolicyRejection::CryptoUnavailable));
	QCOMPARE(unavailable->calls, 0);

	auto verifier        = verifierFor(canonical);
	auto invalidKeyStore = storeFor(verifier, QByteArray(31, 'K'));
	QCOMPARE(rejectionValue(invalidKeyStore->accept(canonical, Signature, Now).rejection),
			 rejectionValue(PolicyRejection::InvalidPublicKey));
	QCOMPARE(verifier->calls, 0);
}

void TestInputEnhancementPolicy::expiredCacheIsNeverEffective() {
	const QByteArray canonical = canonicalPolicyBytes(validPolicy(Profile::Auto, 60));
	auto verifier              = verifierFor(canonical);
	auto store                 = storeFor(verifier);
	QVERIFY(store->accept(canonical, Signature, Now).candidateAccepted);

	const PolicyDecision expired = store->current(Now.addSecs(61));
	QCOMPARE(rejectionValue(expired.rejection), rejectionValue(PolicyRejection::Expired));
	QVERIFY(!expired.hasVerifiedPolicy);
	QVERIFY(!expired.usingCachedPolicy);
	QVERIFY(expired.effectivePolicy.forceOriginal);
	QCOMPARE(expired.effectivePolicy.recommendedProfile, Profile::Original);

	PolicyManifest invalidReplacement = validPolicy(Profile::Crisp, -1);
	const QByteArray invalidBytes     = canonicalPolicyBytes(invalidReplacement);
	verifier->expectedManifest        = invalidBytes;
	const PolicyDecision rejected     = store->accept(invalidBytes, Signature, Now.addSecs(61));
	QVERIFY(!rejected.hasVerifiedPolicy);
	QVERIFY(rejected.effectivePolicy.forceOriginal);
}

void TestInputEnhancementPolicy::verifiesRfc8032VectorWithOpenSsl() {
	OpenSslEd25519Verifier verifier;
	if (!verifier.isAvailable()) {
		QSKIP("This OpenSSL build has no supported Ed25519 API");
	}

	const QByteArray publicKey =
		QByteArray::fromHex("d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a");
	const QByteArray signature =
		QByteArray::fromHex("e5564300c360ac729086e2cc806e828a84877f1eb8e5d974d873e06522490155"
							"5fb8821590a33bacc61e39701cf9b46bd25bf5f0595bbe24655141438e7a100b");
	QVERIFY(verifier.verify(publicKey, QByteArray{}, signature));

	QByteArray changedSignature = signature;
	changedSignature[0] ^= 1;
	QVERIFY(!verifier.verify(publicKey, QByteArray{}, changedSignature));
	QVERIFY(!verifier.verify(QByteArray(31, '\0'), QByteArray{}, signature));
}

QTEST_GUILESS_MAIN(TestInputEnhancementPolicy)
#include "TestInputEnhancementPolicy.moc"
