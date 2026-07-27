// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementPolicyController.h"

#include <QtTest>

#include <QFile>
#include <QNetworkRequest>
#include <QSignalSpy>
#include <QTemporaryDir>

#include <openssl/evp.h>

#include <cstdint>

using namespace Mumble::InputEnhancement;

namespace {
constexpr std::uint64_t CurrentBuild = 100;
const QString RecipeSetVersion       = QStringLiteral("input-recipes-v2");
const QDateTime Now          = QDateTime::fromString(QStringLiteral("2026-07-15T12:00:00Z"), Qt::ISODate).toUTC();
const QByteArray PrivateSeed = QByteArray::fromHex("9d61b19deffd5a60ba844af492ec2cc4"
												   "4449c5697b326919703bac031cae7f60");
const QByteArray PublicKey   = QByteArray::fromHex("d75a980182b10ab7d54bfed3c964073a"
													 "0ee172f3daa62325af021a68f707511a");

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

	QByteArray signature(InputEnhancementPolicyController::signatureBytes, '\0');
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
	if (!signedMessage
		|| signatureSize != static_cast< std::size_t >(InputEnhancementPolicyController::signatureBytes)) {
		return {};
	}
	return signature;
}

PolicyManifest policy(Profile profile = Profile::Balanced, int expiresInSeconds = 3600) {
	PolicyManifest manifest;
	manifest.available          = true;
	manifest.forceOriginal      = false;
	manifest.recommendedProfile = profile;
	manifest.recipeSetVersion   = RecipeSetVersion;
	manifest.minBuild           = 90;
	manifest.expiresAt          = Now.addSecs(expiresInSeconds);
	return manifest;
}

InputEnhancementPolicyController::Configuration configuration(const QTemporaryDir &cacheRoot) {
	InputEnhancementPolicyController::Configuration result;
	result.cacheRoot          = QDir(cacheRoot.path());
	result.rawPublicKey       = PublicKey;
	result.currentBuild       = CurrentBuild;
	result.recipeSetVersion   = RecipeSetVersion;
	result.manifestUrl        = InputEnhancementPolicyController::defaultManifestUrl();
	result.remoteFetchEnabled = false;
	return result;
}

QString currentSlot(const QTemporaryDir &cacheRoot) {
	QFile file(QDir(cacheRoot.path()).filePath(QStringLiteral("current-slot")));
	if (!file.open(QIODevice::ReadOnly)) {
		return {};
	}
	return QString::fromLatin1(file.readAll());
}

bool writePackagedBootstrap(const QString &directory, const QByteArray &manifest, const QByteArray &signature) {
	if (!QDir().mkpath(directory)) {
		return false;
	}
	QFile manifestFile(QDir(directory).filePath(
		QString::fromLatin1(InputEnhancementPolicyController::packagedBootstrapManifestFileName)));
	QFile signatureFile(QDir(directory).filePath(
		QString::fromLatin1(InputEnhancementPolicyController::packagedBootstrapSignatureFileName)));
	if (!manifestFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
		|| manifestFile.write(manifest) != manifest.size()) {
		return false;
	}
	manifestFile.close();
	if (!signatureFile.open(QIODevice::WriteOnly | QIODevice::Truncate)
		|| signatureFile.write(signature) != signature.size()) {
		return false;
	}
	signatureFile.close();
	return true;
}

int rejectionValue(PolicyRejection rejection) {
	return static_cast< int >(rejection);
}
} // namespace

class TestInputEnhancementPolicyController : public QObject {
	Q_OBJECT

private slots:
	void keylessUnmanagedModesAreExplicit();
	void releaseLikeKeylessBuildFailsClosed();
	void validPairIsPersistedAndRestored();
	void invalidExpiredAndMismatchedCandidatesKeepTheLastValidPolicy();
	void unavailablePolicyForcesOriginalWithoutChangingRecommendation();
	void cacheRestoreFallsBackToThePreviousAtomicSlot();
	void publishesDynamicForceChangesAsOneAtomicState();
	void validatesHttpsUrlsAndBuildsBoundedRequests();
	void policyRefreshCadenceIsBoundedBelowKillSwitchDeadline();
	void startupReadinessRequiresAnExplicitOfflineDecision();
	void enhancedRuntimeAvailabilityCombinesPolicyAndRecoverySwitch();
	void effectiveProfileEvidenceDistinguishesAutoFromFallback();
	void packagedBootstrapRequiresValidCurrentSignedPair();
};

void TestInputEnhancementPolicyController::enhancedRuntimeAvailabilityCombinesPolicyAndRecoverySwitch() {
	EffectivePolicyState unmanaged;
	unmanaged.managedBySignedPolicy = false;
	unmanaged.available             = true;
	unmanaged.forceOriginal         = false;
	QCOMPARE(enhancedRuntimeBlockReason(unmanaged, false), EnhancedRuntimeBlockReason::None);
	QCOMPARE(enhancedRuntimeBlockReason(unmanaged, true), EnhancedRuntimeBlockReason::RecoveryDisabled);

	EffectivePolicyState unavailable;
	unavailable.managedBySignedPolicy = true;
	unavailable.hasVerifiedPolicy     = true;
	unavailable.available             = false;
	unavailable.forceOriginal         = true;
	QCOMPARE(enhancedRuntimeBlockReason(unavailable, false),
			 EnhancedRuntimeBlockReason::ChannelUnavailable);

	EffectivePolicyState forced = unavailable;
	forced.available             = true;
	QCOMPARE(enhancedRuntimeBlockReason(forced, false), EnhancedRuntimeBlockReason::PolicyForcesOriginal);

	EffectivePolicyState enabled = forced;
	enabled.forceOriginal         = false;
	QCOMPARE(enhancedRuntimeBlockReason(enabled, false), EnhancedRuntimeBlockReason::None);
	QCOMPARE(enhancedRuntimeBlockReason(enabled, true), EnhancedRuntimeBlockReason::RecoveryDisabled);
}

void TestInputEnhancementPolicyController::effectiveProfileEvidenceDistinguishesAutoFromFallback() {
	QCOMPARE(effectiveProfileRelationship(Profile::Balanced, Profile::Balanced),
			 EffectiveProfileRelationship::RequestedProfileActive);
	QCOMPARE(effectiveProfileRelationship(Profile::Quality, Profile::Original),
			 EffectiveProfileRelationship::RuntimeFallback);
	QCOMPARE(effectiveProfileRelationship(Profile::Auto, Profile::Balanced),
			 EffectiveProfileRelationship::AutoSelectedProfile);
	QCOMPARE(effectiveProfileRelationship(Profile::Auto, Profile::Original),
			 EffectiveProfileRelationship::RuntimeFallback);
	QCOMPARE(effectiveProfileRelationship(Profile::Auto, Profile::Auto),
			 EffectiveProfileRelationship::RuntimeFallback);
	QCOMPARE(effectiveProfileRelationship(Profile::Auto, Profile::VoiceFocus),
			 EffectiveProfileRelationship::RuntimeFallback);
}

void TestInputEnhancementPolicyController::packagedBootstrapRequiresValidCurrentSignedPair() {
	const auto makeConfiguration = [](const QTemporaryDir &root) {
		auto config = configuration(root);
		config.cacheRoot = QDir(QDir(root.path()).filePath(QStringLiteral("cache")));
		config.packagedBootstrapDirectory = QDir(root.path()).filePath(QStringLiteral("package"));
		config.remoteFetchEnabled = false;
		return config;
	};

	{
		QTemporaryDir root;
		QVERIFY(root.isValid());
		PolicyManifest manifest = policy(Profile::Balanced);
		manifest.expiresAt = QDateTime::currentDateTimeUtc().addSecs(3600);
		const QByteArray bytes = canonicalPolicyBytes(manifest);
		QVERIFY(writePackagedBootstrap(QDir(root.path()).filePath(QStringLiteral("package")), bytes, sign(bytes)));

		InputEnhancementPolicyController first(makeConfiguration(root));
		first.start();
		QVERIFY(first.effectiveState().hasVerifiedPolicy);
		QVERIFY(first.available());
		QVERIFY(!first.forceOriginal());

		// Once persisted, the verified cache wins over later packaged drift.
		QVERIFY(writePackagedBootstrap(QDir(root.path()).filePath(QStringLiteral("package")), bytes,
									 QByteArray(InputEnhancementPolicyController::signatureBytes, 'x')));
		InputEnhancementPolicyController restored(makeConfiguration(root));
		restored.start();
		QVERIFY(restored.effectiveState().hasVerifiedPolicy);
		QVERIFY(restored.available());
	}

	{
		QTemporaryDir root;
		QVERIFY(root.isValid());
		PolicyManifest manifest = policy(Profile::Quality);
		manifest.expiresAt = QDateTime::currentDateTimeUtc().addSecs(3600);
		QByteArray tampered       = canonicalPolicyBytes(manifest);
		const QByteArray signature = sign(tampered);
		tampered[0] = tampered[0] == '{' ? '[' : '{';
		QVERIFY(writePackagedBootstrap(QDir(root.path()).filePath(QStringLiteral("package")), tampered, signature));

		InputEnhancementPolicyController controller(makeConfiguration(root));
		controller.start();
		QVERIFY(!controller.effectiveState().hasVerifiedPolicy);
		QVERIFY(!controller.available());
		QVERIFY(controller.forceOriginal());
	}

	{
		QTemporaryDir root;
		QVERIFY(root.isValid());
		PolicyManifest manifest = policy(Profile::Light);
		manifest.expiresAt = QDateTime::currentDateTimeUtc().addSecs(-60);
		const QByteArray bytes = canonicalPolicyBytes(manifest);
		QVERIFY(writePackagedBootstrap(QDir(root.path()).filePath(QStringLiteral("package")), bytes, sign(bytes)));

		InputEnhancementPolicyController controller(makeConfiguration(root));
		controller.start();
		QVERIFY(!controller.effectiveState().hasVerifiedPolicy);
		QVERIFY(!controller.available());
		QVERIFY(controller.forceOriginal());
	}
}

void TestInputEnhancementPolicyController::keylessUnmanagedModesAreExplicit() {
	QTemporaryDir cacheRoot;
	QVERIFY(cacheRoot.isValid());
	auto config = configuration(cacheRoot);
	config.rawPublicKey.clear();
	config.currentBuild = 0;

	InputEnhancementPolicyController controller(std::move(config));
	QVERIFY(!controller.managedBySignedPolicy());
	QVERIFY(controller.available());
	QVERIFY(!controller.forceOriginal());
	QVERIFY(!controller.policyForcedOriginalCanQualifyAudioHealth(false));
	QVERIFY(!controller.policyForcedOriginalCanQualifyAudioHealth(true));
	QVERIFY(!controller.readyForHealthMarker());
	controller.start();
	QVERIFY(controller.readyForHealthMarker());
	QVERIFY(controller.policyDecisionHealthy());

	auto communityConfig = configuration(cacheRoot);
	communityConfig.rawPublicKey.clear();
	communityConfig.currentBuild                   = 83;
	communityConfig.allowUnsignedCommunityRelease = true;

	InputEnhancementPolicyController communityController(std::move(communityConfig));
	QVERIFY(!communityController.managedBySignedPolicy());
	QVERIFY(communityController.available());
	QVERIFY(!communityController.forceOriginal());
	communityController.start();
	QVERIFY(communityController.readyForHealthMarker());
	QVERIFY(communityController.policyDecisionHealthy());
}

void TestInputEnhancementPolicyController::releaseLikeKeylessBuildFailsClosed() {
	QTemporaryDir cacheRoot;
	QVERIFY(cacheRoot.isValid());
	auto config = configuration(cacheRoot);
	config.rawPublicKey.clear();
	config.currentBuild = 1;

	InputEnhancementPolicyController controller(std::move(config));
	QVERIFY(controller.managedBySignedPolicy());
	QVERIFY(!controller.available());
	QVERIFY(controller.forceOriginal());
	QVERIFY(controller.policyForcedOriginalCanQualifyAudioHealth(false));
	QVERIFY(!controller.policyForcedOriginalCanQualifyAudioHealth(true));
	controller.start();
	QVERIFY(controller.readyForHealthMarker());
	QVERIFY(controller.policyDecisionHealthy());
}

void TestInputEnhancementPolicyController::validPairIsPersistedAndRestored() {
	QTemporaryDir cacheRoot;
	QVERIFY(cacheRoot.isValid());
	const QByteArray manifest  = canonicalPolicyBytes(policy(Profile::Crisp));
	const QByteArray signature = sign(manifest);
	QCOMPARE(signature.size(), InputEnhancementPolicyController::signatureBytes);

	InputEnhancementPolicyController first(configuration(cacheRoot));
	const PolicyDecision accepted = first.acceptDownloadedCandidate(manifest, signature, Now);
	QVERIFY(accepted.candidateAccepted);
	QVERIFY(first.effectiveState().hasVerifiedPolicy);
	QCOMPARE(first.recommendedProfile(), Profile::Crisp);
	QVERIFY(!first.forceOriginal());
	QVERIFY(currentSlot(cacheRoot) == QLatin1String("a") || currentSlot(cacheRoot) == QLatin1String("b"));

	InputEnhancementPolicyController restored(configuration(cacheRoot));
	QVERIFY(restored.restoreCache(Now.addSecs(5)));
	QVERIFY(restored.effectiveState().hasVerifiedPolicy);
	QCOMPARE(restored.recommendedProfile(), Profile::Crisp);
	QVERIFY(!restored.forceOriginal());
}

void TestInputEnhancementPolicyController::invalidExpiredAndMismatchedCandidatesKeepTheLastValidPolicy() {
	QTemporaryDir cacheRoot;
	QVERIFY(cacheRoot.isValid());
	InputEnhancementPolicyController controller(configuration(cacheRoot));

	const QByteArray balanced = canonicalPolicyBytes(policy(Profile::Balanced));
	QVERIFY(controller.acceptDownloadedCandidate(balanced, sign(balanced), Now).candidateAccepted);

	const QByteArray crisp  = canonicalPolicyBytes(policy(Profile::Crisp));
	PolicyDecision rejected = controller.acceptDownloadedCandidate(crisp, sign(balanced), Now.addSecs(1));
	QCOMPARE(rejectionValue(rejected.rejection), rejectionValue(PolicyRejection::InvalidSignature));
	QVERIFY(rejected.usingCachedPolicy);
	QCOMPARE(controller.recommendedProfile(), Profile::Balanced);

	QByteArray changedSignature = sign(crisp);
	changedSignature[0] ^= 1;
	rejected = controller.acceptDownloadedCandidate(crisp, changedSignature, Now.addSecs(2));
	QCOMPARE(rejectionValue(rejected.rejection), rejectionValue(PolicyRejection::InvalidSignature));
	QCOMPARE(controller.recommendedProfile(), Profile::Balanced);

	const QByteArray expired = canonicalPolicyBytes(policy(Profile::Auto, -1));
	rejected                 = controller.acceptDownloadedCandidate(expired, sign(expired), Now.addSecs(3));
	QCOMPARE(rejectionValue(rejected.rejection), rejectionValue(PolicyRejection::Expired));
	QCOMPARE(controller.recommendedProfile(), Profile::Balanced);

	rejected = controller.acceptDownloadedCandidate(
		QByteArray(InputEnhancementPolicyController::maximumManifestBytes + 1, 'X'), sign(crisp), Now.addSecs(4));
	QVERIFY(!rejected.candidateAccepted);
	QCOMPARE(controller.recommendedProfile(), Profile::Balanced);
	QVERIFY(!controller.forceOriginal());
}

void TestInputEnhancementPolicyController::unavailablePolicyForcesOriginalWithoutChangingRecommendation() {
	QTemporaryDir cacheRoot;
	QVERIFY(cacheRoot.isValid());
	InputEnhancementPolicyController controller(configuration(cacheRoot));
	PolicyManifest manifest       = policy(Profile::Auto);
	manifest.available            = false;
	manifest.forceOriginal        = false;
	const QByteArray manifestData = canonicalPolicyBytes(manifest);

	QVERIFY(controller.acceptDownloadedCandidate(manifestData, sign(manifestData), Now).candidateAccepted);
	QVERIFY(controller.effectiveState().hasVerifiedPolicy);
	QVERIFY(!controller.available());
	QVERIFY(controller.forceOriginal());
	QVERIFY(controller.policyForcedOriginalCanQualifyAudioHealth(false));
	QVERIFY(!controller.policyForcedOriginalCanQualifyAudioHealth(true));
	QCOMPARE(controller.recommendedProfile(), Profile::Auto);
}

void TestInputEnhancementPolicyController::cacheRestoreFallsBackToThePreviousAtomicSlot() {
	QTemporaryDir cacheRoot;
	QVERIFY(cacheRoot.isValid());
	InputEnhancementPolicyController controller(configuration(cacheRoot));
	const QByteArray balanced = canonicalPolicyBytes(policy(Profile::Balanced));
	const QByteArray crisp    = canonicalPolicyBytes(policy(Profile::Crisp));
	QVERIFY(controller.acceptDownloadedCandidate(balanced, sign(balanced), Now).candidateAccepted);
	const QString olderSlot = currentSlot(cacheRoot);
	QVERIFY(controller.acceptDownloadedCandidate(crisp, sign(crisp), Now.addSecs(1)).candidateAccepted);
	const QString newestSlot = currentSlot(cacheRoot);
	QVERIFY(!olderSlot.isEmpty());
	QVERIFY(!newestSlot.isEmpty());
	QVERIFY(olderSlot != newestSlot);

	QFile corruptSignature(
		QDir(cacheRoot.path()).filePath(newestSlot + QStringLiteral("/input-enhancement-policy.json.sig")));
	QVERIFY(corruptSignature.open(QIODevice::WriteOnly | QIODevice::Truncate));
	QCOMPARE(corruptSignature.write(QByteArray(InputEnhancementPolicyController::signatureBytes, 'X')),
			 qint64(InputEnhancementPolicyController::signatureBytes));
	corruptSignature.close();

	InputEnhancementPolicyController restored(configuration(cacheRoot));
	QVERIFY(restored.restoreCache(Now.addSecs(2)));
	QCOMPARE(restored.recommendedProfile(), Profile::Balanced);
	QCOMPARE(currentSlot(cacheRoot), olderSlot);
}

void TestInputEnhancementPolicyController::publishesDynamicForceChangesAsOneAtomicState() {
	QTemporaryDir cacheRoot;
	QVERIFY(cacheRoot.isValid());
	InputEnhancementPolicyController controller(configuration(cacheRoot));
	QSignalSpy forceSpy(&controller, &InputEnhancementPolicyController::forceOriginalChanged);
	QSignalSpy stateSpy(&controller, &InputEnhancementPolicyController::effectivePolicyChanged);

	const QByteArray balanced = canonicalPolicyBytes(policy(Profile::Balanced));
	QVERIFY(controller.acceptDownloadedCandidate(balanced, sign(balanced), Now).candidateAccepted);
	QVERIFY(!controller.policyForcedOriginalCanQualifyAudioHealth(false));
	QCOMPARE(forceSpy.count(), 1);
	QCOMPARE(forceSpy.takeFirst().at(0).toBool(), false);

	PolicyManifest forced        = policy(Profile::Crisp);
	forced.forceOriginal         = true;
	const QByteArray forcedBytes = canonicalPolicyBytes(forced);
	QVERIFY(controller.acceptDownloadedCandidate(forcedBytes, sign(forcedBytes), Now.addSecs(1)).candidateAccepted);
	const EffectivePolicyState snapshot = controller.effectiveState();
	QVERIFY(snapshot.managedBySignedPolicy);
	QVERIFY(snapshot.hasVerifiedPolicy);
	QVERIFY(snapshot.available);
	QVERIFY(snapshot.forceOriginal);
	QVERIFY(controller.policyForcedOriginalCanQualifyAudioHealth(false));
	QVERIFY(!controller.policyForcedOriginalCanQualifyAudioHealth(true));
	QCOMPARE(snapshot.recommendedProfile, Profile::Crisp);
	QCOMPARE(forceSpy.count(), 1);
	QCOMPARE(forceSpy.takeFirst().at(0).toBool(), true);
	QCOMPARE(stateSpy.count(), 2);
}

void TestInputEnhancementPolicyController::validatesHttpsUrlsAndBuildsBoundedRequests() {
	const QUrl stable  = InputEnhancementPolicyController::defaultManifestUrl();
	const QUrl preview = InputEnhancementPolicyController::defaultManifestUrl(QStringLiteral("preview"));
	QVERIFY(InputEnhancementPolicyController::isAllowedHttpsUrl(stable));
	QVERIFY(InputEnhancementPolicyController::isAllowedHttpsUrl(preview));
	QVERIFY(stable.path().contains(QStringLiteral("mumble-forked-stable")));
	QVERIFY(preview.path().contains(QStringLiteral("mumble-forked-preview")));
	QCOMPARE(InputEnhancementPolicyController::signatureUrlForManifest(stable).path(),
			 stable.path() + QStringLiteral(".sig"));

	QVERIFY(
		!InputEnhancementPolicyController::isAllowedHttpsUrl(QUrl(QStringLiteral("http://example.com/policy.json"))));
	QVERIFY(!InputEnhancementPolicyController::isAllowedHttpsUrl(
		QUrl(QStringLiteral("https://user@example.com/policy.json"))));
	QVERIFY(!InputEnhancementPolicyController::isAllowedHttpsUrl(
		QUrl(QStringLiteral("https://example.com/policy.json#fragment"))));
	QVERIFY(InputEnhancementPolicyController::signatureUrlForManifest(QUrl(QStringLiteral("https://example.com/")))
				.isEmpty());

	const QNetworkRequest request = InputEnhancementPolicyController::networkRequest(
		stable, InputEnhancementPolicyController::maximumManifestBytes);
	QCOMPARE(request.attribute(QNetworkRequest::RedirectPolicyAttribute).toInt(),
			 static_cast< int >(QNetworkRequest::NoLessSafeRedirectPolicy));
	QCOMPARE(request.maximumRedirectsAllowed(), InputEnhancementPolicyController::maximumRedirects);
	QCOMPARE(request.transferTimeout(), InputEnhancementPolicyController::transferTimeoutMilliseconds);
	QCOMPARE(request.attribute(QNetworkRequest::MaximumDownloadBufferSizeAttribute).toLongLong(),
			 qint64(InputEnhancementPolicyController::maximumManifestBytes));

	const bool hadUrlOverride         = qEnvironmentVariableIsSet("MUMBLE_INPUT_ENHANCEMENT_POLICY_URL");
	const QByteArray oldUrlOverride   = qgetenv("MUMBLE_INPUT_ENHANCEMENT_POLICY_URL");
	const bool hadChannel             = qEnvironmentVariableIsSet("MUMBLE_INPUT_ENHANCEMENT_POLICY_CHANNEL");
	const QByteArray oldChannel       = qgetenv("MUMBLE_INPUT_ENHANCEMENT_POLICY_CHANNEL");
	const bool hadUpdateChannel       = qEnvironmentVariableIsSet("MUMBLE_FORK_UPDATE_CHANNEL");
	const QByteArray oldUpdateChannel = qgetenv("MUMBLE_FORK_UPDATE_CHANNEL");
	qunsetenv("MUMBLE_INPUT_ENHANCEMENT_POLICY_URL");
	qunsetenv("MUMBLE_INPUT_ENHANCEMENT_POLICY_CHANNEL");
	qputenv("MUMBLE_FORK_UPDATE_CHANNEL", "preview");
	QCOMPARE(InputEnhancementPolicyController::manifestUrlFromEnvironment(), preview);
	qputenv("MUMBLE_INPUT_ENHANCEMENT_POLICY_CHANNEL", "preview");
	QCOMPARE(InputEnhancementPolicyController::manifestUrlFromEnvironment(), preview);
	qputenv("MUMBLE_INPUT_ENHANCEMENT_POLICY_URL", "http://example.com/policy.json");
	QVERIFY(InputEnhancementPolicyController::manifestUrlFromEnvironment().isEmpty());
	qputenv("MUMBLE_INPUT_ENHANCEMENT_POLICY_URL", "https://example.com/policy.json");
	QCOMPARE(InputEnhancementPolicyController::manifestUrlFromEnvironment(),
			 QUrl(QStringLiteral("https://example.com/policy.json")));
	if (hadUrlOverride) {
		qputenv("MUMBLE_INPUT_ENHANCEMENT_POLICY_URL", oldUrlOverride);
	} else {
		qunsetenv("MUMBLE_INPUT_ENHANCEMENT_POLICY_URL");
	}
	if (hadChannel) {
		qputenv("MUMBLE_INPUT_ENHANCEMENT_POLICY_CHANNEL", oldChannel);
	} else {
		qunsetenv("MUMBLE_INPUT_ENHANCEMENT_POLICY_CHANNEL");
	}
	if (hadUpdateChannel) {
		qputenv("MUMBLE_FORK_UPDATE_CHANNEL", oldUpdateChannel);
	} else {
		qunsetenv("MUMBLE_FORK_UPDATE_CHANNEL");
	}
}

void TestInputEnhancementPolicyController::policyRefreshCadenceIsBoundedBelowKillSwitchDeadline() {
	using Controller = InputEnhancementPolicyController;
	QCOMPARE(Controller::refreshIntervalMilliseconds(0), Controller::refreshBaseIntervalMilliseconds);
	QCOMPARE(Controller::refreshIntervalMilliseconds(Controller::refreshMaximumJitterMilliseconds),
			 Controller::refreshMaximumIntervalMilliseconds);
	QCOMPARE(Controller::refreshIntervalMilliseconds(Controller::refreshMaximumJitterMilliseconds + 1),
			 Controller::refreshBaseIntervalMilliseconds);
	QVERIFY(Controller::refreshBaseIntervalMilliseconds >= 15 * 60 * 1000);
	QVERIFY(Controller::refreshMaximumIntervalMilliseconds < 20 * 60 * 1000);
}

void TestInputEnhancementPolicyController::startupReadinessRequiresAnExplicitOfflineDecision() {
	QTemporaryDir cacheRoot;
	QVERIFY(cacheRoot.isValid());
	InputEnhancementPolicyController controller(configuration(cacheRoot));
	QSignalSpy readinessSpy(&controller, &InputEnhancementPolicyController::readinessChanged);
	QVERIFY(!controller.readyForHealthMarker());
	QVERIFY(!controller.policyDecisionHealthy());
	controller.start();
	QVERIFY(controller.readyForHealthMarker());
	QVERIFY(controller.policyDecisionHealthy());
	QCOMPARE(readinessSpy.count(), 1);
	QCOMPARE(readinessSpy.takeFirst().at(0).toBool(), true);
}

QTEST_GUILESS_MAIN(TestInputEnhancementPolicyController)
#include "TestInputEnhancementPolicyController.moc"
