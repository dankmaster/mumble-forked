// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#ifndef MUMBLE_MUMBLE_INPUTENHANCEMENTPOLICYCONTROLLER_H_
#define MUMBLE_MUMBLE_INPUTENHANCEMENTPOLICYCONTROLLER_H_

#include "InputEnhancementPolicy.h"

#include <QByteArray>
#include <QDateTime>
#include <QDir>
#include <QNetworkRequest>
#include <QObject>
#include <QPointer>
#include <QTimer>
#include <QUrl>

#include <atomic>
#include <cstdint>
#include <functional>
#include <memory>

class QNetworkAccessManager;
class QNetworkReply;

namespace Mumble::InputEnhancement {

struct EffectivePolicyState final {
	bool managedBySignedPolicy = true;
	bool hasVerifiedPolicy     = false;
	bool available             = false;
	bool forceOriginal         = true;
	Profile recommendedProfile = Profile::Original;

	bool operator==(const EffectivePolicyState &other) const noexcept;
	bool operator!=(const EffectivePolicyState &other) const noexcept { return !(*this == other); }
};

/// Product-facing reason why a fixed enhanced profile cannot be activated.
/// This deliberately stays separate from recipe/model readiness: channel policy
/// and the local recovery switch can force the runtime to Original even when the
/// packaged processor itself is healthy.
enum class EnhancedRuntimeBlockReason : std::uint8_t {
	None,
	ChannelUnavailable,
	PolicyForcesOriginal,
	RecoveryDisabled
};

EnhancedRuntimeBlockReason enhancedRuntimeBlockReason(const EffectivePolicyState &state,
													  bool recoveryDisabled) noexcept;

/// Describes how the processor that actually produced the E2E output relates
/// to the saved request. Auto legitimately resolves to a concrete profile;
/// every other mismatch is a runtime fallback and must remain visible in
/// qualification evidence.
enum class EffectiveProfileRelationship : std::uint8_t {
	RequestedProfileActive,
	AutoSelectedProfile,
	RuntimeFallback
};

EffectiveProfileRelationship effectiveProfileRelationship(Profile requested, Profile effective) noexcept;

/// Owns client-local signed input-enhancement channel policy state. It has no
/// protobuf, Murmur, voice-packet or receiver dependencies. All policy bytes
/// are verified before use and the effective audio-thread state is one atomic
/// word.
class InputEnhancementPolicyController final : public QObject {
	Q_OBJECT
	Q_DISABLE_COPY(InputEnhancementPolicyController)

public:
	struct Configuration final {
		QDir cacheRoot;
		QByteArray rawPublicKey;
		std::uint64_t currentBuild = 0;
		QString recipeSetVersion;
		QUrl manifestUrl;
		bool remoteFetchEnabled = true;
		bool allowUnsignedCommunityRelease = false;
		/// Optional signed bootstrap pair packaged beside the executable. An
		/// empty directory resolves to QCoreApplication::applicationDirPath().
		/// The files are never treated as URLs and pass the same key/build/
		/// catalog/expiry verification as cache and HTTPS candidates.
		bool packagedBootstrapEnabled = true;
		QString packagedBootstrapDirectory;
	};

	static constexpr qsizetype maximumManifestBytes  = 2048;
	static constexpr qsizetype signatureBytes        = 64;
	static constexpr auto packagedBootstrapManifestFileName = "input-enhancement-policy.json";
	static constexpr auto packagedBootstrapSignatureFileName = "input-enhancement-policy.json.sig";
	static constexpr int maximumRedirects            = 3;
	static constexpr int transferTimeoutMilliseconds = 10'000;
	static constexpr int refreshBaseIntervalMilliseconds    = 15 * 60 * 1000;
	static constexpr int refreshMaximumJitterMilliseconds   = 2 * 60 * 1000;
	static constexpr int refreshMaximumIntervalMilliseconds =
		refreshBaseIntervalMilliseconds + refreshMaximumJitterMilliseconds;

	explicit InputEnhancementPolicyController(Configuration configuration,
											  QNetworkAccessManager *networkManager = nullptr,
											  QObject *parent                       = nullptr);
	~InputEnhancementPolicyController() override;

	/// Restores the last committed cache synchronously, publishes its effective
	/// state, then starts expiry/refresh timers and an asynchronous HTTPS fetch.
	void start();
	void refresh();

	/// Testable candidate boundary shared with network completion. Oversized or
	/// malformed inputs never reach the signature store or cache.
	PolicyDecision acceptDownloadedCandidate(const QByteArray &canonicalManifest, const QByteArray &rawSignature,
											 const QDateTime &nowUtc = QDateTime::currentDateTimeUtc());
	bool restoreCache(const QDateTime &nowUtc = QDateTime::currentDateTimeUtc());
	void reevaluate(const QDateTime &nowUtc = QDateTime::currentDateTimeUtc());

	EffectivePolicyState effectiveState() const noexcept;
	bool forceOriginal() const noexcept;
	bool available() const noexcept;
	Profile recommendedProfile() const noexcept;
	bool managedBySignedPolicy() const noexcept;
	/// True when the effective signed-policy state, rather than the explicit
	/// recovery switch, owns a safe Original-only audio session. Policy startup
	/// readiness remains a separate update-health gate.
	bool policyForcedOriginalCanQualifyAudioHealth(bool recoveryDisabled) const noexcept;
	/// True after startup has produced an explicit policy decision: verified
	/// cache, completed initial download attempt, or fail-closed offline state.
	bool readyForHealthMarker() const noexcept;
	/// Health markers may only be written once the initial decision is complete
	/// and the effective state is either verified or safely forced to Original.
	bool policyDecisionHealthy() const noexcept;

	static QUrl defaultManifestUrl(const QString &channel = QStringLiteral("stable"));
	static QUrl manifestUrlFromEnvironment();
	static QUrl signatureUrlForManifest(const QUrl &manifestUrl);
	static bool isAllowedHttpsUrl(const QUrl &url) noexcept;
	static QNetworkRequest networkRequest(const QUrl &url, qsizetype maximumBytes);
	/// Maps an entropy sample to the bounded 15-17 minute policy refresh
	/// interval. The immediate startup fetch is separate from this cadence.
	static int refreshIntervalMilliseconds(std::uint32_t jitterSample) noexcept;

signals:
	void effectivePolicyChanged();
	void forceOriginalChanged(bool forceOriginal);
	void readinessChanged(bool ready);
	void refreshFinished(bool candidateAccepted);

private:
	static constexpr std::uint32_t availableBit     = 1U << 0U;
	static constexpr std::uint32_t forceOriginalBit = 1U << 1U;
	static constexpr std::uint32_t verifiedBit      = 1U << 2U;
	static constexpr std::uint32_t managedBit       = 1U << 3U;
	static constexpr std::uint32_t profileShift     = 8U;
	static constexpr std::uint32_t profileMask      = 0xffU << profileShift;

	static std::uint32_t pack(const EffectivePolicyState &state) noexcept;
	static EffectivePolicyState unpack(std::uint32_t packed) noexcept;
	void publish(const PolicyDecision &decision);
	void publishUnmanagedDevelopmentState();
	bool persistAcceptedPair(const QByteArray &manifest, const QByteArray &signature);
	bool restorePackagedBootstrap(const QDateTime &nowUtc = QDateTime::currentDateTimeUtc());
	bool loadSlot(const QString &slot, QByteArray &manifest, QByteArray &signature) const;
	QString currentSlot() const;
	bool commitCurrentSlot(const QString &slot) const;
	static bool writeAtomically(const QString &path, const QByteArray &bytes);
	void requestAsset(const QUrl &url, qsizetype maximumBytes,
					  const std::function< void(bool, QByteArray) > &completion);
	void finishRefresh(bool accepted);
	void scheduleNextRefresh();
	void markInitialDecisionReady();

	Configuration m_configuration;
	QNetworkAccessManager *m_networkManager = nullptr;
	std::unique_ptr< SignedPolicyStore > m_store;
	std::atomic< std::uint32_t > m_effectiveState;
	std::atomic< bool > m_readyForHealthMarker{ false };
	QPointer< QNetworkReply > m_reply;
	QByteArray m_pendingManifest;
	QTimer m_expiryTimer;
	QTimer m_refreshTimer;
	bool m_started                = false;
	bool m_fetchInProgress        = false;
	bool m_unmanagedBypass        = false;
	bool m_initialDecisionPending = false;
};

} // namespace Mumble::InputEnhancement

#endif // MUMBLE_MUMBLE_INPUTENHANCEMENTPOLICYCONTROLLER_H_
