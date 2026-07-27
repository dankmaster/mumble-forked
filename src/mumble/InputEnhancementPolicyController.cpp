// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "InputEnhancementPolicyController.h"

#include <QCoreApplication>
#include <QFile>
#include <QFileInfo>
#include <QNetworkAccessManager>
#include <QNetworkReply>
#include <QRandomGenerator>
#include <QSaveFile>

#include <algorithm>
#include <array>
#include <functional>
#include <optional>
#include <utility>

namespace Mumble::InputEnhancement {
namespace {
	constexpr auto slotA = "a";
	constexpr auto slotB = "b";

	std::optional< QByteArray > readBoundedFile(const QString &path, qsizetype maximumBytes,
												qsizetype exactBytes = -1) {
		QFile file(path);
		if (!file.open(QIODevice::ReadOnly | QIODevice::Unbuffered)) {
			return std::nullopt;
		}
		const qint64 size = file.size();
		if (size < 0 || size > maximumBytes || (exactBytes >= 0 && size != exactBytes)) {
			return std::nullopt;
		}
		const QByteArray bytes = file.read(maximumBytes + 1);
		if (bytes.size() != size || bytes.size() > maximumBytes || (exactBytes >= 0 && bytes.size() != exactBytes)) {
			return std::nullopt;
		}
		return bytes;
	}
} // namespace

bool EffectivePolicyState::operator==(const EffectivePolicyState &other) const noexcept {
	return managedBySignedPolicy == other.managedBySignedPolicy && hasVerifiedPolicy == other.hasVerifiedPolicy
		   && available == other.available && forceOriginal == other.forceOriginal
		   && recommendedProfile == other.recommendedProfile;
}

EnhancedRuntimeBlockReason enhancedRuntimeBlockReason(const EffectivePolicyState &state,
													  const bool recoveryDisabled) noexcept {
	if (recoveryDisabled) {
		return EnhancedRuntimeBlockReason::RecoveryDisabled;
	}
	if (!state.managedBySignedPolicy) {
		return EnhancedRuntimeBlockReason::None;
	}
	if (!state.available) {
		return EnhancedRuntimeBlockReason::ChannelUnavailable;
	}
	if (state.forceOriginal) {
		return EnhancedRuntimeBlockReason::PolicyForcesOriginal;
	}
	return EnhancedRuntimeBlockReason::None;
}

EffectiveProfileRelationship effectiveProfileRelationship(const Profile requested, const Profile effective) noexcept {
	if (requested == Profile::Auto) {
		switch (effective) {
			case Profile::Light:
			case Profile::Balanced:
			case Profile::Quality:
				return EffectiveProfileRelationship::AutoSelectedProfile;
			case Profile::Original:
			case Profile::VoiceFocus:
			case Profile::Auto:
				return EffectiveProfileRelationship::RuntimeFallback;
		}
	}
	if (requested == effective) {
		return EffectiveProfileRelationship::RequestedProfileActive;
	}
	return EffectiveProfileRelationship::RuntimeFallback;
}

InputEnhancementPolicyController::InputEnhancementPolicyController(Configuration configuration,
																   QNetworkAccessManager *networkManager,
																   QObject *parent)
	: QObject(parent), m_configuration(std::move(configuration)), m_networkManager(networkManager),
	  m_store(std::make_unique< SignedPolicyStore >(m_configuration.rawPublicKey,
													std::make_shared< OpenSslEd25519Verifier >(),
													m_configuration.currentBuild, m_configuration.recipeSetVersion)),
	  m_effectiveState(pack({ true, false, false, true, Profile::Original })) {
	// A build-number-0 developer client and the compile-time-selected unsigned
	// community lane remain usable without a release key. This cannot bypass a
	// build that actually carries an embedded signing key.
	m_unmanagedBypass = m_configuration.rawPublicKey.isEmpty()
						&& (m_configuration.currentBuild == 0
							|| m_configuration.allowUnsignedCommunityRelease);
	if (m_unmanagedBypass) {
		publishUnmanagedDevelopmentState();
	}

	m_expiryTimer.setInterval(60'000);
	m_expiryTimer.setSingleShot(false);
	connect(&m_expiryTimer, &QTimer::timeout, this, [this]() { reevaluate(); });
	// Fetch immediately at startup, then use bounded single-shot scheduling so
	// clients do not synchronize on one fixed channel-policy polling instant.
	// The 17-minute maximum leaves margin for the bounded HTTPS request and an
	// input restart while retaining the 20-minute force-Original contract.
	m_refreshTimer.setSingleShot(true);
	connect(&m_refreshTimer, &QTimer::timeout, this, &InputEnhancementPolicyController::refresh);
}

InputEnhancementPolicyController::~InputEnhancementPolicyController() {
	if (m_reply) {
		m_reply->abort();
	}
}

void InputEnhancementPolicyController::start() {
	if (m_started) {
		return;
	}
	m_started = true;
	if (m_unmanagedBypass) {
		markInitialDecisionReady();
		return;
	}

	const bool restoredVerifiedCache = restoreCache();
	const bool restoredPackagedBootstrap =
		!restoredVerifiedCache && restorePackagedBootstrap(QDateTime::currentDateTimeUtc());
	const bool restoredVerifiedPolicy = restoredVerifiedCache || restoredPackagedBootstrap;
	m_expiryTimer.start();
	const bool remoteUrlUsable = isAllowedHttpsUrl(m_configuration.manifestUrl)
								 && isAllowedHttpsUrl(signatureUrlForManifest(m_configuration.manifestUrl));
	if (m_configuration.remoteFetchEnabled && m_networkManager && remoteUrlUsable) {
		m_initialDecisionPending = !restoredVerifiedPolicy;
		if (restoredVerifiedPolicy) {
			markInitialDecisionReady();
		}
		refresh();
	} else {
		// No remote transport was configured. The verified cache result or the
		// already-published fail-closed Original state is the explicit offline
		// decision used by the update health gate.
		markInitialDecisionReady();
	}
}

bool InputEnhancementPolicyController::restorePackagedBootstrap(const QDateTime &nowUtc) {
	if (m_unmanagedBypass || !m_configuration.packagedBootstrapEnabled) {
		return false;
	}
	const QString directory = m_configuration.packagedBootstrapDirectory.trimmed().isEmpty()
		? QCoreApplication::applicationDirPath()
		: m_configuration.packagedBootstrapDirectory;
	const QDir bootstrapRoot(directory);
	const auto manifest = readBoundedFile(
		bootstrapRoot.filePath(QString::fromLatin1(packagedBootstrapManifestFileName)), maximumManifestBytes);
	const auto signature = readBoundedFile(
		bootstrapRoot.filePath(QString::fromLatin1(packagedBootstrapSignatureFileName)), signatureBytes,
		signatureBytes);
	if (!manifest || !signature) {
		return false;
	}

	const PolicyDecision decision = m_store->accept(*manifest, *signature, nowUtc);
	if (!decision.candidateAccepted || !persistAcceptedPair(*manifest, *signature)) {
		// A packaged pair is only authoritative after it has crossed the same
		// durable cache boundary as a verified HTTPS candidate. Invalid,
		// expired, or unpersistable bytes leave the managed runtime fail-closed.
		m_store->clearCache();
		publish(m_store->current(nowUtc));
		return false;
	}
	publish(decision);
	return true;
}

void InputEnhancementPolicyController::refresh() {
	if (m_unmanagedBypass || m_fetchInProgress || !m_configuration.remoteFetchEnabled || !m_networkManager
		|| !isAllowedHttpsUrl(m_configuration.manifestUrl)) {
		return;
	}
	const QUrl signatureUrl = signatureUrlForManifest(m_configuration.manifestUrl);
	if (!isAllowedHttpsUrl(signatureUrl)) {
		return;
	}

	m_fetchInProgress = true;
	m_pendingManifest.clear();
	requestAsset(m_configuration.manifestUrl, maximumManifestBytes,
				 [this, signatureUrl](bool manifestOk, QByteArray manifest) {
					 if (!manifestOk) {
						 finishRefresh(false);
						 return;
					 }
					 m_pendingManifest = std::move(manifest);
					 requestAsset(signatureUrl, signatureBytes, [this](bool signatureOk, QByteArray signature) {
						 if (!signatureOk || signature.size() != signatureBytes) {
							 finishRefresh(false);
							 return;
						 }
						 const PolicyDecision decision =
							 acceptDownloadedCandidate(m_pendingManifest, signature, QDateTime::currentDateTimeUtc());
						 finishRefresh(decision.candidateAccepted);
					 });
				 });
}

PolicyDecision InputEnhancementPolicyController::acceptDownloadedCandidate(const QByteArray &canonicalManifest,
																		   const QByteArray &rawSignature,
																		   const QDateTime &nowUtc) {
	if (m_unmanagedBypass) {
		return { {}, PolicyRejection::InvalidPublicKey, false, false, false };
	}
	if (canonicalManifest.isEmpty() || canonicalManifest.size() > maximumManifestBytes
		|| rawSignature.size() != signatureBytes) {
		const PolicyDecision current = m_store->current(nowUtc);
		publish(current);
		return current;
	}

	const PolicyDecision decision = m_store->accept(canonicalManifest, rawSignature, nowUtc);
	if (decision.candidateAccepted) {
		persistAcceptedPair(canonicalManifest, rawSignature);
	}
	publish(decision);
	return decision;
}

bool InputEnhancementPolicyController::restoreCache(const QDateTime &nowUtc) {
	if (m_unmanagedBypass) {
		return false;
	}
	const QString preferred             = currentSlot();
	std::array< QString, 2 > cacheSlots = { preferred, preferred == QLatin1String(slotA) ? QStringLiteral("b")
																						 : QStringLiteral("a") };
	if (preferred != QLatin1String(slotA) && preferred != QLatin1String(slotB)) {
		cacheSlots = { QStringLiteral("a"), QStringLiteral("b") };
	}

	for (const QString &slot : cacheSlots) {
		QByteArray manifest;
		QByteArray signature;
		if (!loadSlot(slot, manifest, signature)) {
			continue;
		}
		const PolicyDecision decision = m_store->accept(manifest, signature, nowUtc);
		if (decision.candidateAccepted) {
			if (slot != preferred) {
				commitCurrentSlot(slot);
			}
			publish(decision);
			return true;
		}
	}
	publish(m_store->current(nowUtc));
	return false;
}

void InputEnhancementPolicyController::reevaluate(const QDateTime &nowUtc) {
	if (!m_unmanagedBypass) {
		publish(m_store->current(nowUtc));
	}
}

EffectivePolicyState InputEnhancementPolicyController::effectiveState() const noexcept {
	return unpack(m_effectiveState.load(std::memory_order_acquire));
}

bool InputEnhancementPolicyController::forceOriginal() const noexcept {
	return effectiveState().forceOriginal;
}

bool InputEnhancementPolicyController::available() const noexcept {
	return effectiveState().available;
}

Profile InputEnhancementPolicyController::recommendedProfile() const noexcept {
	return effectiveState().recommendedProfile;
}

bool InputEnhancementPolicyController::managedBySignedPolicy() const noexcept {
	return effectiveState().managedBySignedPolicy;
}

bool InputEnhancementPolicyController::policyForcedOriginalCanQualifyAudioHealth(
	const bool recoveryDisabled) const noexcept {
	const EffectivePolicyState state = effectiveState();
	return !recoveryDisabled && state.managedBySignedPolicy && state.forceOriginal;
}

bool InputEnhancementPolicyController::readyForHealthMarker() const noexcept {
	return m_readyForHealthMarker.load(std::memory_order_acquire);
}

bool InputEnhancementPolicyController::policyDecisionHealthy() const noexcept {
	if (!readyForHealthMarker()) {
		return false;
	}
	const EffectivePolicyState state = effectiveState();
	return !state.managedBySignedPolicy || state.hasVerifiedPolicy || state.forceOriginal;
}

QUrl InputEnhancementPolicyController::defaultManifestUrl(const QString &channel) {
	const QString normalized = channel.compare(QStringLiteral("preview"), Qt::CaseInsensitive) == 0
								   ? QStringLiteral("preview")
								   : QStringLiteral("stable");
	return QUrl(QStringLiteral("https://github.com/dankmaster/mumble-forked/releases/download/"
							   "mumble-forked-%1/input-enhancement-policy.json")
					.arg(normalized));
}

QUrl InputEnhancementPolicyController::manifestUrlFromEnvironment() {
	const QString overrideUrl = qEnvironmentVariable("MUMBLE_INPUT_ENHANCEMENT_POLICY_URL").trimmed();
	if (!overrideUrl.isEmpty()) {
		const QUrl parsed = QUrl::fromUserInput(overrideUrl);
		return isAllowedHttpsUrl(parsed) ? parsed : QUrl{};
	}
	QString channel = qEnvironmentVariable("MUMBLE_INPUT_ENHANCEMENT_POLICY_CHANNEL").trimmed();
	if (channel.isEmpty()) {
		// Update packages and their signed emergency policy are one channel
		// decision. A preview client must never silently fetch stable policy.
		channel = qEnvironmentVariable("MUMBLE_FORK_UPDATE_CHANNEL").trimmed();
	}
	if (channel.isEmpty() || channel.compare(QStringLiteral("stable"), Qt::CaseInsensitive) == 0) {
		return defaultManifestUrl(QStringLiteral("stable"));
	}
	if (channel.compare(QStringLiteral("preview"), Qt::CaseInsensitive) == 0) {
		return defaultManifestUrl(QStringLiteral("preview"));
	}
	return {};
}

QUrl InputEnhancementPolicyController::signatureUrlForManifest(const QUrl &manifestUrl) {
	if (!isAllowedHttpsUrl(manifestUrl)) {
		return {};
	}
	QUrl signatureUrl = manifestUrl;
	if (signatureUrl.path().isEmpty() || signatureUrl.path().endsWith(QLatin1Char('/'))) {
		return {};
	}
	signatureUrl.setPath(signatureUrl.path() + QStringLiteral(".sig"));
	return signatureUrl;
}

bool InputEnhancementPolicyController::isAllowedHttpsUrl(const QUrl &url) noexcept {
	return url.isValid() && url.scheme().compare(QStringLiteral("https"), Qt::CaseInsensitive) == 0
		   && !url.host().isEmpty() && url.userInfo().isEmpty() && !url.hasFragment();
}

QNetworkRequest InputEnhancementPolicyController::networkRequest(const QUrl &url, qsizetype maximumBytes) {
	QNetworkRequest request(url);
	request.setAttribute(QNetworkRequest::RedirectPolicyAttribute, QNetworkRequest::NoLessSafeRedirectPolicy);
	request.setMaximumRedirectsAllowed(maximumRedirects);
	request.setTransferTimeout(transferTimeoutMilliseconds);
	request.setAttribute(QNetworkRequest::MaximumDownloadBufferSizeAttribute, maximumBytes);
	request.setRawHeader("Accept", "application/json, application/octet-stream;q=0.9");
	return request;
}

int InputEnhancementPolicyController::refreshIntervalMilliseconds(const std::uint32_t jitterSample) noexcept {
	return refreshBaseIntervalMilliseconds
		   + static_cast< int >(jitterSample % static_cast< std::uint32_t >(refreshMaximumJitterMilliseconds + 1));
}

std::uint32_t InputEnhancementPolicyController::pack(const EffectivePolicyState &state) noexcept {
	std::uint32_t packed = (static_cast< std::uint32_t >(state.recommendedProfile) << profileShift) & profileMask;
	packed |= state.available ? availableBit : 0U;
	packed |= state.forceOriginal ? forceOriginalBit : 0U;
	packed |= state.hasVerifiedPolicy ? verifiedBit : 0U;
	packed |= state.managedBySignedPolicy ? managedBit : 0U;
	return packed;
}

EffectivePolicyState InputEnhancementPolicyController::unpack(std::uint32_t packed) noexcept {
	EffectivePolicyState state;
	state.available             = (packed & availableBit) != 0;
	state.forceOriginal         = (packed & forceOriginalBit) != 0;
	state.hasVerifiedPolicy     = (packed & verifiedBit) != 0;
	state.managedBySignedPolicy = (packed & managedBit) != 0;
	const std::uint32_t profile = (packed & profileMask) >> profileShift;
	state.recommendedProfile =
		profile <= static_cast< std::uint32_t >(Profile::Auto) ? static_cast< Profile >(profile) : Profile::Original;
	return state;
}

void InputEnhancementPolicyController::publish(const PolicyDecision &decision) {
	EffectivePolicyState next;
	next.managedBySignedPolicy = true;
	next.hasVerifiedPolicy     = decision.hasVerifiedPolicy;
	if (decision.hasVerifiedPolicy) {
		next.available          = decision.effectivePolicy.available;
		next.forceOriginal      = decision.effectivePolicy.forceOriginal || !decision.effectivePolicy.available;
		next.recommendedProfile = decision.effectivePolicy.recommendedProfile;
	}
	const std::uint32_t nextPacked     = pack(next);
	const std::uint32_t previousPacked = m_effectiveState.exchange(nextPacked, std::memory_order_acq_rel);
	if (previousPacked != nextPacked) {
		const EffectivePolicyState previous = unpack(previousPacked);
		emit effectivePolicyChanged();
		if (previous.forceOriginal != next.forceOriginal) {
			emit forceOriginalChanged(next.forceOriginal);
		}
	}
}

void InputEnhancementPolicyController::publishUnmanagedDevelopmentState() {
	EffectivePolicyState state;
	state.managedBySignedPolicy = false;
	state.available             = true;
	state.forceOriginal         = false;
	state.hasVerifiedPolicy     = false;
	state.recommendedProfile    = Profile::Original;
	m_effectiveState.store(pack(state), std::memory_order_release);
}

bool InputEnhancementPolicyController::persistAcceptedPair(const QByteArray &manifest, const QByteArray &signature) {
	if (!m_configuration.cacheRoot.mkpath(QStringLiteral("."))) {
		return false;
	}
	const QString previous = currentSlot();
	const QString target   = previous == QLatin1String(slotA) ? QStringLiteral("b") : QStringLiteral("a");
	const QString slotPath = m_configuration.cacheRoot.filePath(target);
	if (!QDir().mkpath(slotPath)) {
		return false;
	}
	const QString manifestPath  = QDir(slotPath).filePath(QStringLiteral("input-enhancement-policy.json"));
	const QString signaturePath = QDir(slotPath).filePath(QStringLiteral("input-enhancement-policy.json.sig"));
	if (!writeAtomically(manifestPath, manifest) || !writeAtomically(signaturePath, signature)) {
		return false;
	}
	QByteArray persistedManifest;
	QByteArray persistedSignature;
	if (!loadSlot(target, persistedManifest, persistedSignature) || persistedManifest != manifest
		|| persistedSignature != signature) {
		return false;
	}
	return commitCurrentSlot(target);
}

bool InputEnhancementPolicyController::loadSlot(const QString &slot, QByteArray &manifest,
												QByteArray &signature) const {
	if (slot != QLatin1String(slotA) && slot != QLatin1String(slotB)) {
		return false;
	}
	const QDir slotDir(m_configuration.cacheRoot.filePath(slot));
	const auto manifestBytes =
		readBoundedFile(slotDir.filePath(QStringLiteral("input-enhancement-policy.json")), maximumManifestBytes);
	const auto signatureData = readBoundedFile(slotDir.filePath(QStringLiteral("input-enhancement-policy.json.sig")),
											   signatureBytes, signatureBytes);
	if (!manifestBytes || !signatureData) {
		return false;
	}
	manifest  = *manifestBytes;
	signature = *signatureData;
	return true;
}

QString InputEnhancementPolicyController::currentSlot() const {
	const auto bytes = readBoundedFile(m_configuration.cacheRoot.filePath(QStringLiteral("current-slot")), 1, 1);
	if (!bytes || (*bytes != QByteArray(slotA) && *bytes != QByteArray(slotB))) {
		return {};
	}
	return QString::fromLatin1(*bytes);
}

bool InputEnhancementPolicyController::commitCurrentSlot(const QString &slot) const {
	if (slot != QLatin1String(slotA) && slot != QLatin1String(slotB)) {
		return false;
	}
	return writeAtomically(m_configuration.cacheRoot.filePath(QStringLiteral("current-slot")), slot.toLatin1());
}

bool InputEnhancementPolicyController::writeAtomically(const QString &path, const QByteArray &bytes) {
	QSaveFile file(path);
	file.setDirectWriteFallback(false);
	if (!file.open(QIODevice::WriteOnly | QIODevice::Unbuffered) || file.write(bytes) != bytes.size()
		|| !file.commit()) {
		file.cancelWriting();
		return false;
	}
	QFile::setPermissions(path, QFileDevice::ReadOwner | QFileDevice::WriteOwner);
	return true;
}

void InputEnhancementPolicyController::requestAsset(const QUrl &url, qsizetype maximumBytes,
													const std::function< void(bool, QByteArray) > &completion) {
	if (!m_networkManager || !isAllowedHttpsUrl(url)) {
		completion(false, {});
		return;
	}
	QNetworkReply *reply = m_networkManager->get(networkRequest(url, maximumBytes));
	m_reply              = reply;
	connect(reply, &QNetworkReply::downloadProgress, this, [reply, maximumBytes](qint64 received, qint64 total) {
		if (received > maximumBytes || total > maximumBytes) {
			reply->abort();
		}
	});
	connect(reply, &QNetworkReply::finished, this, [this, reply, maximumBytes, completion]() {
		const bool networkOk         = reply->error() == QNetworkReply::NoError && isAllowedHttpsUrl(reply->url());
		const QVariant contentLength = reply->header(QNetworkRequest::ContentLengthHeader);
		const qint64 reportedLength  = contentLength.isValid() ? contentLength.toLongLong() : -1;
		QByteArray bytes             = reply->read(maximumBytes + 1);
		const bool sizeOk            = bytes.size() <= maximumBytes && reply->bytesAvailable() == 0
							&& (reportedLength < 0 || reportedLength <= maximumBytes);
		m_reply = nullptr;
		reply->deleteLater();
		completion(networkOk && sizeOk, networkOk && sizeOk ? std::move(bytes) : QByteArray{});
	});
}

void InputEnhancementPolicyController::finishRefresh(bool accepted) {
	m_fetchInProgress = false;
	m_pendingManifest.clear();
	if (!accepted) {
		reevaluate();
	}
	if (m_initialDecisionPending) {
		m_initialDecisionPending = false;
		markInitialDecisionReady();
	}
	scheduleNextRefresh();
	emit refreshFinished(accepted);
}

void InputEnhancementPolicyController::scheduleNextRefresh() {
	if (!m_started || m_unmanagedBypass || !m_configuration.remoteFetchEnabled || !m_networkManager
		|| !isAllowedHttpsUrl(m_configuration.manifestUrl)
		|| !isAllowedHttpsUrl(signatureUrlForManifest(m_configuration.manifestUrl))) {
		m_refreshTimer.stop();
		return;
	}
	m_refreshTimer.start(refreshIntervalMilliseconds(QRandomGenerator::global()->generate()));
}

void InputEnhancementPolicyController::markInitialDecisionReady() {
	if (!m_readyForHealthMarker.exchange(true, std::memory_order_acq_rel)) {
		emit readinessChanged(true);
	}
}

} // namespace Mumble::InputEnhancement
