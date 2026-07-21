// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.

#include "ModernSettingsController.h"

#include "AudioInput.h"
#include "AudioOutput.h"
#include "ClientUser.h"
#include "EchoCancelOption.h"
#include "GlobalShortcut.h"
#include "Database.h"
#include "Global.h"
#include "Log.h"
#include "InputEnhancement.h"
#include "InputEnhancementAutoV2.h"
#include "InputEnhancementCalibrationWorker.h"
#include "InputEnhancementPackageVerifier.h"
#include "InputEnhancementPolicyController.h"
#include "ModernShellMenuSerializer.h"
#include "ModernTheme.h"
#include "PersistentChatMediaCache.h"
#include "Plugin.h"
#include "PluginManager.h"
#include "SpeechCleanup.h"
#include "UiTheme.h"
#include "Version.h"
#if defined(Q_OS_WIN) && defined(USE_WASAPI)
#	include "WASAPI.h"
#endif

#include <QtCore/QDebug>
#include <QtCore/QCryptographicHash>
#include <QtCore/QDateTime>
#include <QtCore/QHash>
#include <QtCore/QMap>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtCore/QReadLocker>
#include <QtCore/QRandomGenerator>
#include <QtCore/QSet>
#include <QtCore/QSysInfo>
#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>

#include <algorithm>
#include <cmath>

namespace {
	constexpr int kMaxAmplificationSliderValue = 19500;
	constexpr int kAmplificationSliderBase     = 20000;

	Mumble::InputEnhancement::CpuClass
		inputEnhancementCpuClass(const Mumble::InputEnhancement::InputEnhancementPackageVerifier *verifier,
								  const Mumble::InputEnhancement::Profile profile) {
		using namespace Mumble::InputEnhancement;
		if (!verifier) {
			return CpuClass::Low;
		}
		if (profile == Profile::Original || profile == Profile::Light) {
			return CpuClass::Low;
		}
		if (profile == Profile::Balanced) {
			return BackendAvailability::compiled().rnnoise ? CpuClass::Standard : CpuClass::Low;
		}
		if (profile != Profile::Auto) {
			// The real Quality/Voice Focus probe initializes and exercises a neural
			// pipeline. It is pre-warmed in the background; Settings must never
			// perform or wait for that work on the GUI thread.
			return verifier->cachedManualProfileCpuClass().value_or(CpuClass::Low);
		}
		const AutoV2::CapabilityProbeKey key = AutoV2::currentCapabilityProbeKey(verifier->runtimePayloadFingerprint());
		const AutoV2::CapabilityProbeResult result = AutoV2::cachedCapabilityProbe(key);
		return result.valid ? result.cpuTier : CpuClass::Low;
	}

	Mumble::InputEnhancement::DeviceIdentity draftInputEnhancementDeviceIdentity(const Settings &settings);

	struct InputEnhancementSettingsReadiness final {
		bool selectable = false;
		bool processingSelectable = false;
		bool productionQualified = false;
		Mumble::InputEnhancement::ProfileReadinessReason reason =
			Mumble::InputEnhancement::ProfileReadinessReason::Ready;
		Mumble::InputEnhancement::EnhancedRuntimeBlockReason runtimeBlockReason =
			Mumble::InputEnhancement::EnhancedRuntimeBlockReason::None;
	};

	QString inputEnhancementReadinessReasonText(Mumble::InputEnhancement::ProfileReadinessReason reason) {
		using Reason = Mumble::InputEnhancement::ProfileReadinessReason;
		switch (reason) {
			case Reason::Ready:
				return QString();
			case Reason::ExperimentalAuto:
				return QObject::tr(
					"Experimental: profile switching is not part of the core release qualification.");
			case Reason::AutoRuntimeUnavailable:
				return QObject::tr("Safe dual-pipeline transitions are not enabled in this build.");
			case Reason::InsufficientCpu:
				return QObject::tr("This profile requires a higher qualified CPU class.");
			case Reason::BackendUnavailable:
				return QObject::tr("The required audio engine is not included in this build.");
			case Reason::PackageUnavailable:
				return QObject::tr("The input-enhancement package is unavailable or has not passed verification.");
			case Reason::RecipeUnauthorized:
				return QObject::tr("The exact recipe is not authorized by the current package.");
			case Reason::ModelUnavailable:
				return QObject::tr("The required model is missing or failed verification.");
		}
		return QObject::tr("The selected input-enhancement profile is unavailable.");
	}

	QString inputEnhancementReadinessReasonText(const InputEnhancementSettingsReadiness &readiness) {
		if (!readiness.processingSelectable) {
			return inputEnhancementReadinessReasonText(readiness.reason);
		}
		using Reason = Mumble::InputEnhancement::EnhancedRuntimeBlockReason;
		switch (readiness.runtimeBlockReason) {
			case Reason::None:
				return inputEnhancementReadinessReasonText(readiness.reason);
			case Reason::ChannelUnavailable:
				return QObject::tr("The verified channel policy does not make input enhancement available.");
			case Reason::PolicyForcesOriginal:
				return QObject::tr("The verified channel policy currently requires Original.");
			case Reason::RecoveryDisabled:
				return QObject::tr("Input enhancement is disabled by the local recovery switch.");
		}
		return QObject::tr("The selected input-enhancement profile is unavailable.");
	}

	InputEnhancementSettingsReadiness inputEnhancementReadinessForSettings(
		const Settings &settings, Mumble::InputEnhancement::Profile profile, int noiseReduction, int naturalCrisp,
		const bool presentationOnly = false) {
		using namespace Mumble::InputEnhancement;
		const InputEnhancementPackageVerifier *verifier =
			Global::g_global_struct ? Global::get().inputEnhancementPackageVerifier : nullptr;
		EnhancedRuntimeBlockReason runtimeBlockReason = EnhancedRuntimeBlockReason::None;
		if (profile != Profile::Original && Global::g_global_struct) {
			const InputEnhancementPolicyController *policyController =
				Global::get().inputEnhancementPolicyController;
			EffectivePolicyState policyState;
			if (policyController) {
				policyState = policyController->effectiveState();
			} else {
				// A controller-less build is an unmanaged developer configuration.
				// The explicit recovery switch still owns an Original-only session.
				policyState.managedBySignedPolicy = false;
				policyState.available             = true;
				policyState.forceOriginal         = false;
			}
			const bool recoveryDisabled = Global::get().bInputEnhancementRecoveryDisabled
				|| (!policyController && Global::get().bDisableInputEnhancement);
			runtimeBlockReason = enhancedRuntimeBlockReason(policyState, recoveryDisabled);
		}
		const DeviceIdentity identity = draftInputEnhancementDeviceIdentity(settings);
		ResolveRequest request;
		request.profile             = profile;
		request.noiseReduction      = noiseReduction;
		request.naturalCrisp        = naturalCrisp;
		request.cpuClass            = inputEnhancementCpuClass(verifier, profile);
		request.backendAvailability = BackendAvailability::compiled();
		request.captureDevice = CaptureDeviceContext::liveDevice(identity.backendId, identity.stable);
		if (!verifier && profile != Profile::Original) {
			return { false, false, false, ProfileReadinessReason::PackageUnavailable, runtimeBlockReason };
		}
		const ProfileReadiness processingReadiness = verifier
			? (presentationOnly ? verifier->presentationReadinessForProfile(request)
							: verifier->readinessForProfile(request))
			: profileReadiness(request);
		return { processingReadiness.selectable && runtimeBlockReason == EnhancedRuntimeBlockReason::None,
				 processingReadiness.selectable, processingReadiness.productionQualified, processingReadiness.reason,
				 runtimeBlockReason };
	}

	QVariantMap optionItem(const QVariant &value, const QString &label, const bool enabled = true,
						   const QString &hint = QString()) {
		QVariantMap option;
		option.insert(QStringLiteral("value"), value);
		option.insert(QStringLiteral("label"), label);
		option.insert(QStringLiteral("enabled"), enabled);
		if (!hint.isEmpty()) {
			option.insert(QStringLiteral("hint"), hint);
		}
		return option;
	}

	QVariantMap optionItem(const int value, const QString &label) {
		return optionItem(QVariant(value), label);
	}

	QVariantMap optionItem(const int value, const QString &label, const QString &hint) {
		return optionItem(QVariant(value), label, true, hint);
	}

	QString normalizedStonksTickerPlacement(const QVariant &value) {
		const QString placement = value.toString().trimmed();
		if (placement == QLatin1String("windowTop") || placement == QLatin1String("top")
			|| placement == QLatin1String("aboveComposer")) {
			return placement;
		}
		return QStringLiteral("bottom");
	}

	QString normalizedStonksTickerDirection(const QVariant &value) {
		const QString direction = value.toString().trimmed();
		if (direction == QLatin1String("right") || direction == QLatin1String("up")
			|| direction == QLatin1String("down")) {
			return direction;
		}
		return QStringLiteral("left");
	}

	QString normalizedStonksTickerSpeed(const QVariant &value) {
		const QString speed = value.toString().trimmed();
		if (speed == QLatin1String("verySlow") || speed == QLatin1String("slow")
			|| speed == QLatin1String("fast")) {
			return speed;
		}
		return QStringLiteral("normal");
	}

	QVariantMap normalizedStonksContext(const QVariantMap &context) {
		QVariantMap normalized;
		normalized.insert(QStringLiteral("connected"), context.value(QStringLiteral("connected")).toBool());
		normalized.insert(QStringLiteral("supported"), context.value(QStringLiteral("supported")).toBool());
		normalized.insert(QStringLiteral("canAdmin"), context.value(QStringLiteral("canAdmin")).toBool());
		normalized.insert(QStringLiteral("enabled"), context.value(QStringLiteral("enabled"), true).toBool());
		normalized.insert(QStringLiteral("socialAnnouncementsEnabled"),
						  context.value(QStringLiteral("socialAnnouncementsEnabled"), true).toBool());
		normalized.insert(QStringLiteral("textChannelId"), context.value(QStringLiteral("textChannelId")).toUInt());
		normalized.insert(QStringLiteral("textChannels"), context.value(QStringLiteral("textChannels")).toList());
		return normalized;
	}

	QVariantMap normalizedMotdContext(const QVariantMap &context) {
		QVariantMap normalized;
		const bool connected = context.value(QStringLiteral("connected")).toBool();
		const bool available = connected && context.value(QStringLiteral("available"), true).toBool();
		const int maximumLength = qBound(1024, context.value(QStringLiteral("maximumLength"), 100000).toInt(),
									 1000000);
		const QString html = context.value(QStringLiteral("html")).toString().left(maximumLength + 1);
		normalized.insert(QStringLiteral("connected"), connected);
		normalized.insert(QStringLiteral("available"), available);
		normalized.insert(QStringLiteral("canEdit"),
						  available && context.value(QStringLiteral("canEdit")).toBool());
		normalized.insert(QStringLiteral("serverName"), context.value(QStringLiteral("serverName")).toString());
		normalized.insert(QStringLiteral("html"), html);
		normalized.insert(QStringLiteral("originalHtml"),
						  context.value(QStringLiteral("originalHtml"), html).toString().left(maximumLength + 1));
		normalized.insert(QStringLiteral("previewSourceHtml"),
						  context.value(QStringLiteral("previewSourceHtml")).toString().left(maximumLength + 1));
		normalized.insert(QStringLiteral("previewBlocks"),
						  context.value(QStringLiteral("previewBlocks")).toList());
		normalized.insert(QStringLiteral("previewSummary"),
						  context.value(QStringLiteral("previewSummary")).toString().left(4096));
		normalized.insert(QStringLiteral("maximumLength"), maximumLength);
		return normalized;
	}

	QVariantMap fieldItem(const QString &id, const QString &label, const QString &type, const QVariant &value) {
		QVariantMap field;
		field.insert(QStringLiteral("id"), id);
		field.insert(QStringLiteral("label"), label);
		field.insert(QStringLiteral("type"), type);
		field.insert(QStringLiteral("value"), value);
		return field;
	}

	QVariantMap enabledField(QVariantMap field, const bool enabled) {
		field.insert(QStringLiteral("enabled"), enabled);
		return field;
	}

	QVariantMap hiddenField(QVariantMap field, const bool hidden = true) {
		if (hidden) {
			field.insert(QStringLiteral("type"), QStringLiteral("hidden"));
		}
		return field;
	}

	QVariantMap hintedField(QVariantMap field, const QString &hint) {
		if (!hint.isEmpty()) {
			field.insert(QStringLiteral("hint"), hint);
		}
		return field;
	}

	QVariantMap tooltippedField(QVariantMap field, const QString &tooltip) {
		if (!tooltip.isEmpty()) {
			field.insert(QStringLiteral("tooltip"), tooltip);
		}
		return field;
	}

	QVariantMap advancedField(QVariantMap field) {
		field.insert(QStringLiteral("advanced"), true);
		return field;
	}

	QVariantMap presentationField(QVariantMap field, const QString &presentation) {
		field.insert(QStringLiteral("presentation"), presentation);
		return field;
	}

	QVariantMap numberField(const QString &id, const QString &label, const int value, const int min, const int max,
							const int step = 1, const QString &suffix = QString()) {
		QVariantMap field = fieldItem(id, label, QStringLiteral("number"), value);
		field.insert(QStringLiteral("min"), min);
		field.insert(QStringLiteral("max"), max);
		field.insert(QStringLiteral("step"), step);
		if (!suffix.isEmpty()) {
			field.insert(QStringLiteral("suffix"), suffix);
		}
		return field;
	}

	QVariantMap rangeField(const QString &id, const QString &label, const int value, const int min, const int max,
						   const int step, const QString &suffix) {
		QVariantMap field = numberField(id, label, value, min, max, step, suffix);
		field.insert(QStringLiteral("type"), QStringLiteral("range"));
		return field;
	}

	QVariantMap colorField(const QString &id, const QString &label, const QString &value) {
		return fieldItem(id, label, QStringLiteral("color"), value);
	}

	QVariantMap boolField(const QString &id, const QString &label, const bool value) {
		return fieldItem(id, label, QStringLiteral("checkbox"), value);
	}

	QVariantMap readonlyField(const QString &label, const QVariant &value) {
		return fieldItem(QString(), label, QStringLiteral("readonly"), value);
	}

	QVariantMap actionField(const QString &id, const QString &label, const QString &buttonLabel,
							const QString &actionID, const QString &tone = QString()) {
		QVariantMap field = fieldItem(id, label, QStringLiteral("button"), buttonLabel);
		field.insert(QStringLiteral("buttonLabel"), buttonLabel);
		field.insert(QStringLiteral("actionId"), actionID);
		if (!tone.isEmpty()) {
			field.insert(QStringLiteral("tone"), tone);
		}
		return field;
	}

	QVariantMap selectField(const QString &id, const QString &label, const QVariant &value, const QVariantList &options,
							const QString &valueType = QStringLiteral("number")) {
		QVariantMap field = fieldItem(id, label, QStringLiteral("select"), value);
		field.insert(QStringLiteral("options"), options);
		field.insert(QStringLiteral("valueType"), valueType);
		return field;
	}

	QVariantMap noteField(const QString &text) {
		QVariantMap field;
		field.insert(QStringLiteral("type"), QStringLiteral("note"));
		field.insert(QStringLiteral("text"), text);
		return field;
	}

	QString fieldTooltip(const QString &id) {
		const QHash< QString, QString > tooltips {
			{ QStringLiteral("look.quitBehavior"),
			  QObject::tr("Choose whether closing the main window asks, minimizes to tray, or quits the client.") },
			{ QStringLiteral("look.alwaysOnTop"),
			  QObject::tr("Keep the Mumble window above other windows in the selected layout modes.") },
			{ QStringLiteral("look.modernTheme"),
			  QObject::tr("Choose the color theme used by the native Qt Quick client.") },
			{ QStringLiteral("look.modernThemesDirectory"),
			  QObject::tr("Open the folder where custom Modern theme manifests are stored.") },
			{ QStringLiteral("look.modernThemesReload"),
			  QObject::tr("Reload custom theme manifests after adding or editing files in the theme folder.") },
			{ QStringLiteral("look.modernDensity"),
			  QObject::tr("Adjust spacing density for Qt Quick lists, controls, and panels.") },
			{ QStringLiteral("look.modernClassicUserIcons"),
			  QObject::tr("Use the original Mumble user icons in room lists instead of avatar bubbles.") },
			{ QStringLiteral("look.modernRailSide"),
			  QObject::tr("Choose which side of the Modern shell hosts the room rail.") },
			{ QStringLiteral("look.modernAccent"),
			  QObject::tr("Choose the Modern shell accent color, or let the active theme decide.") },
			{ QStringLiteral("look.modernCustomAccent"),
			  QObject::tr("Pick the custom Modern shell accent color used when Accent is set to Custom.") },
			{ QStringLiteral("look.modernCustomAccentStrength"),
			  QObject::tr("Adjust how strongly the custom accent tints selected surfaces, glow, and borders.") },
			{ QStringLiteral("look.hideInTray"),
			  QObject::tr("Hide the taskbar window when Mumble is minimized to the system tray.") },
			{ QStringLiteral("look.stateInTray"),
			  QObject::tr("Reflect your talking, muted, and deafened state in the tray icon.") },
			{ QStringLiteral("look.showVolumeAdjustments"),
			  QObject::tr("Show when users have local volume adjustments applied.") },
			{ QStringLiteral("look.showNicknamesOnly"),
			  QObject::tr("Prefer nicknames over full usernames where both are available.") },
			{ QStringLiteral("look.filterHidesEmptyChannels"),
			  QObject::tr("Hide empty rooms when a room-list filter is active.") },
			{ QStringLiteral("look.presenceIdleTimeout"),
			  QObject::tr("Set how long you can be inactive before Mumble marks you idle.") },
			{ QStringLiteral("network.autoReconnect"),
			  QObject::tr("Reconnect automatically if the current server connection drops.") },
			{ QStringLiteral("network.autoConnect"),
			  QObject::tr("Client-side startup setting: connect to your last server automatically when Mumble starts.") },
			{ QStringLiteral("network.reconnectToLastChannel"),
			  QObject::tr("Ask compatible servers to place you in your last known voice channel when you connect.") },
			{ QStringLiteral("network.startWithPC"),
			  QObject::tr("Start Mumble hidden in the system tray when you sign in to Windows.") },
			{ QStringLiteral("network.tcpMode"),
			  QObject::tr("Use TCP instead of UDP for voice traffic when networks block or degrade UDP.") },
			{ QStringLiteral("network.qos"),
			  QObject::tr("Ask the operating system and network to prioritize Mumble voice packets.") },
			{ QStringLiteral("network.suppressIdentity"),
			  QObject::tr("Avoid sending your certificate identity to servers unless it is required.") },
			{ QStringLiteral("network.linkPreviews"),
			  QObject::tr("Fetch and show previews for links posted in chat.") },
			{ QStringLiteral("network.clearPreviewCache"),
			  QObject::tr("Remove locally cached chat preview images and media for this client profile.") },
			{ QStringLiteral("network.hideOS"),
			  QObject::tr("Do not advertise your operating system details to connected servers.") },
			{ QStringLiteral("network.proxyType"),
			  QObject::tr("Choose whether Mumble connects directly or through an HTTP/SOCKS proxy.") },
			{ QStringLiteral("network.proxyHost"), QObject::tr("Hostname or IP address of the proxy server.") },
			{ QStringLiteral("network.proxyPort"), QObject::tr("Network port used by the configured proxy server.") },
			{ QStringLiteral("network.proxyUsername"), QObject::tr("Username sent to the proxy when authentication is needed.") },
			{ QStringLiteral("network.proxyPassword"), QObject::tr("Password sent to the proxy when authentication is needed.") },
			{ QStringLiteral("network.updateCheck"),
			  QObject::tr("Check whether a newer Mumble client build is available.") },
			{ QStringLiteral("network.pluginCheck"), QObject::tr("Check installed plugins for available updates.") },
			{ QStringLiteral("network.pluginAutoUpdate"),
			  QObject::tr("Install plugin updates automatically when they are available.") },
			{ QStringLiteral("network.advertisedRelease"),
			  QObject::tr("Override the release string Mumble reports to servers for compatibility testing.") },
			{ QStringLiteral("network.advertisedOS"),
			  QObject::tr("Override the operating-system name Mumble reports to servers for compatibility testing.") },
			{ QStringLiteral("network.advertisedOSVersion"),
			  QObject::tr("Override the operating-system version Mumble reports to servers for compatibility testing.") },
			{ QStringLiteral("messages.clock24Hour"),
			  QObject::tr("Display message timestamps using a 24-hour clock.") },
			{ QStringLiteral("messages.whisperFriends"),
			  QObject::tr("Treat messages from friends as whispers for notification and speech rules.") },
			{ QStringLiteral("messages.limitThreshold"),
			  QObject::tr("Limit high-volume message events when the connected server reaches this user count.") },
			{ QStringLiteral("messages.ttsEnabled"),
			  QObject::tr("Read enabled message events aloud using text to speech.") },
			{ QStringLiteral("messages.ttsVolume"), QObject::tr("Set text-to-speech playback volume.") },
			{ QStringLiteral("messages.ttsThreshold"),
			  QObject::tr("Do not speak messages longer than this number of characters.") },
			{ QStringLiteral("messages.ttsReadOwn"),
			  QObject::tr("Read your own sent messages back through text to speech.") },
			{ QStringLiteral("messages.ttsNoScope"),
			  QObject::tr("Omit the room or conversation name from spoken messages.") },
			{ QStringLiteral("messages.ttsNoAuthor"),
			  QObject::tr("Omit the sender name from spoken messages.") },
			{ QStringLiteral("messages.ttsLanguage"),
			  QObject::tr("Choose the BCP 47 language tag used by the speech engine.") },
			{ QStringLiteral("messages.notificationVolume"),
			  QObject::tr("Set the playback volume for per-event notification sounds.") },
			{ QStringLiteral("messages.cueVolume"),
			  QObject::tr("Set the playback volume for transmit and mute cue sounds.") },
			{ QStringLiteral("messages.events"),
			  QObject::tr("Choose logging, notification, highlight, speech, limiting, and sound behavior for each event.") },
			{ QStringLiteral("screenShare.autoOpenCurrentRoom"),
			  QObject::tr("Open screen shares automatically when they are posted in your current voice room.") },
			{ QStringLiteral("screenShare.diagnostics"),
			  QObject::tr("Write extra screen-sharing diagnostics to the profile log folder.") },
			{ QStringLiteral("audio.inputMeter"),
			  QObject::tr("Shows the live signal used by voice activation and the current stop/start thresholds.") },
			{ QStringLiteral("audio.inputSystem"), QObject::tr("Select the audio backend used for microphone capture.") },
			{ QStringLiteral("audio.inputDevice"), QObject::tr("Select which microphone or capture source Mumble uses.") },
			{ QStringLiteral("audio.wasapiInputRouting"),
			  QObject::tr("Choose whether Windows default fallback is allowed when the selected microphone disappears.") },
			{ QStringLiteral("audio.exclusiveInput"),
			  QObject::tr("Let Mumble take exclusive control of the input device when the backend supports it.") },
			{ QStringLiteral("audio.transmitMode"),
			  QObject::tr("Choose whether to transmit continuously, by voice activity, or only while push-to-talk is held.") },
			{ QStringLiteral("audio.vadSource"),
			  QObject::tr("Choose how voice activation decides whether the microphone should open.") },
			{ QStringLiteral("audio.inputGateMode"),
			  QObject::tr("Add an optional post-cleanup gate that rejects low-level or low-confidence voice activation.") },
			{ QStringLiteral("audio.vadMin"),
			  QObject::tr("Signal level below which voice activation closes the microphone.") },
			{ QStringLiteral("audio.vadMax"),
			  QObject::tr("Signal level above which voice activation opens the microphone.") },
			{ QStringLiteral("audio.voiceHold"),
			  QObject::tr("How long voice activation stays open after the signal drops below the stop threshold.") },
			{ QStringLiteral("audio.doublePush"),
			  QObject::tr("How quickly a second push-to-talk press toggles lock mode.") },
			{ QStringLiteral("audio.pttHold"),
			  QObject::tr("How long transmission continues after releasing push-to-talk.") },
			{ QStringLiteral("audio.showPttWindow"),
			  QObject::tr("Show a small on-screen push-to-talk button window.") },
			{ QStringLiteral("audio.framesPerPacket"),
			  QObject::tr("Amount of captured audio bundled into each outgoing voice packet.") },
			{ QStringLiteral("audio.quality"), QObject::tr("Target voice bitrate in kilobits per second.") },
			{ QStringLiteral("audio.experimentalHighBitrate"),
			  QObject::tr("Allow voice bitrate values above the normal compatibility range.") },
			{ QStringLiteral("audio.allowLowDelay"),
			  QObject::tr("Allow Opus to use lower-latency encoding when the server and settings permit it.") },
			{ QStringLiteral("audio.maxAmplification"),
			  QObject::tr("Maximum automatic microphone gain Mumble may apply to quiet input.") },
			{ QStringLiteral("audio.echoMode"),
			  QObject::tr("Reduce echo from speakers or shared output paths when supported by the audio backend.") },
			{ QStringLiteral("audio.inputEnhancementProfile"),
			  QObject::tr("Choose Original, Light, Balanced, Quality, or Voice Focus input enhancement.") },
			{ QStringLiteral("audio.inputEnhancementReduction"),
			  QObject::tr("Adjust noise reduction only within the selected recipe's qualified range.") },
			{ QStringLiteral("audio.inputEnhancementCharacter"),
			  QObject::tr("Balance natural voice character against a clearer, more processed result.") },
			{ QStringLiteral("audio.inputEnhancementExperimentalAuto"),
			  QObject::tr(
				  "Advanced experimental profile switching among Light, Balanced, and Quality; Voice Focus is excluded.") },
			{ QStringLiteral("audio.noiseCancelMode"),
			  QObject::tr("Choose whether local microphone noise suppression is disabled, classic, neural, or combined.") },
			{ QStringLiteral("audio.noiseCancelBackend"),
			  QObject::tr("Select the neural noise-suppression engine used for local microphone cleanup.") },
			{ QStringLiteral("audio.noiseCancelModel"),
			  QObject::tr("Select the model file or preset used by the local neural cleanup backend.") },
			{ QStringLiteral("audio.noiseCancelCustomModelPath"),
			  QObject::tr("Path to a custom local neural cleanup model file.") },
			{ QStringLiteral("audio.speexNoiseStrength"),
			  QObject::tr("Adjust the strength of classic Speex noise suppression.") },
			{ QStringLiteral("audio.cuePtt"),
			  QObject::tr("Play a cue sound when push-to-talk transmission starts or stops.") },
			{ QStringLiteral("audio.cueVad"),
			  QObject::tr("Play a cue sound when voice activation starts or stops transmitting.") },
			{ QStringLiteral("audio.cueOnPath"), QObject::tr("Sound file played when transmission starts.") },
			{ QStringLiteral("audio.cueOffPath"), QObject::tr("Sound file played when transmission stops.") },
			{ QStringLiteral("audio.muteCue"), QObject::tr("Play a cue sound when your microphone mute state changes.") },
			{ QStringLiteral("audio.muteCuePath"), QObject::tr("Sound file played for microphone mute cues.") },
			{ QStringLiteral("audio.idleMinutes"),
			  QObject::tr("Minutes of inactivity before Mumble performs the selected idle action.") },
			{ QStringLiteral("audio.idleAction"),
			  QObject::tr("Action Mumble performs when you have been idle for the configured time.") },
			{ QStringLiteral("audio.undoIdleAction"),
			  QObject::tr("Undo an automatic idle mute or deafen when activity resumes.") },
			{ QStringLiteral("audio.outputSystem"), QObject::tr("Select the audio backend used for playback.") },
			{ QStringLiteral("audio.outputDevice"), QObject::tr("Select which speakers or headset Mumble uses.") },
			{ QStringLiteral("audio.wasapiOutputRouting"),
			  QObject::tr("Choose whether Windows default fallback is allowed when the selected output disappears.") },
			{ QStringLiteral("audio.wasapiLatencyProfile"),
			  QObject::tr(
				  "Controls only the local Windows shared-mode audio engine period: how often WASAPI asks Mumble "
				  "to provide or consume audio. It does not reduce server ping, codec delay, or Mumble's network "
				  "jitter buffer, and it has no effect in exclusive mode. Shorter periods mean more frequent CPU "
				  "wake-ups and tighter deadlines, which can increase power use and cause crackles or dropouts on "
				  "busy systems or sensitive USB, Bluetooth, and dock drivers. Stable is recommended unless you "
				  "have measured a real local-monitoring benefit.") },
			{ QStringLiteral("audio.exclusiveOutput"),
			  QObject::tr("Let Mumble take exclusive control of the output device when the backend supports it.") },
			{ QStringLiteral("audio.outputVolume"), QObject::tr("Adjust playback volume for incoming speech.") },
			{ QStringLiteral("audio.outputDelay"),
			  QObject::tr("Delay audio playback to compensate for device or routing latency.") },
			{ QStringLiteral("audio.jitterBuffer"),
			  QObject::tr("Buffer more or less incoming audio to smooth network jitter.") },
			{ QStringLiteral("audio.loopMode"), QObject::tr("Play your own transmitted audio locally or through the server.") },
			{ QStringLiteral("audio.loopPacketDelay"),
			  QObject::tr("Simulate packet delay for loopback testing.") },
			{ QStringLiteral("audio.loopPacketLoss"),
			  QObject::tr("Simulate packet loss for loopback testing.") },
			{ QStringLiteral("audio.attenuateOthers"),
			  QObject::tr("Lower other applications while other users are talking.") },
			{ QStringLiteral("audio.attenuateOnTalk"),
			  QObject::tr("Lower other applications while you are talking.") },
			{ QStringLiteral("audio.externalApplicationsVolume"),
			  QObject::tr("Volume kept for other applications while attenuation is active.") },
			{ QStringLiteral("audio.attenuatePrioritySpeaker"),
			  QObject::tr("Lower regular users when a priority speaker talks.") },
			{ QStringLiteral("audio.alwaysAttenuateListeners"),
			  QObject::tr("Apply listener attenuation even outside normal priority-speaker situations.") },
			{ QStringLiteral("audio.listenerAttenuation"),
			  QObject::tr("Volume kept for listeners when listener attenuation is active.") },
			{ QStringLiteral("audio.attenuateSameOutputOnly"),
			  QObject::tr("Only attenuate applications using the same output device as Mumble.") },
			{ QStringLiteral("audio.attenuateLoopbacks"),
			  QObject::tr("Also attenuate loopback audio when same-device attenuation is enabled.") },
			{ QStringLiteral("audio.positional"), QObject::tr("Enable positional audio from supported games and plugins.") },
			{ QStringLiteral("audio.positionalHeadphone"),
			  QObject::tr("Optimize positional audio rendering for headphones.") },
			{ QStringLiteral("audio.transmitPosition"),
			  QObject::tr("Send your positional audio coordinates to the server when available.") },
			{ QStringLiteral("audio.minDistance"),
			  QObject::tr("Distance at which positional audio starts to become quieter.") },
			{ QStringLiteral("audio.maxDistance"),
			  QObject::tr("Distance at which positional audio reaches its minimum volume.") },
			{ QStringLiteral("audio.minPositionalVolume"),
			  QObject::tr("Lowest volume positional audio may fade to at maximum distance.") },
			{ QStringLiteral("audio.bloom"),
			  QObject::tr("Widen the perceived spread of positional audio sources.") },
			{ QStringLiteral("audio.remoteCleanupEnabled"),
			  QObject::tr("Clean up incoming speech from other users before playback.") },
			{ QStringLiteral("audio.remoteCleanupBackend"),
			  QObject::tr("Select the neural cleanup engine used for incoming speech.") },
			{ QStringLiteral("audio.remoteCleanupModel"),
			  QObject::tr("Select the model file or preset used for incoming speech cleanup.") },
			{ QStringLiteral("audio.remoteCleanupCustomModelPath"),
			  QObject::tr("Path to a custom neural cleanup model for incoming speech.") },
			{ QStringLiteral("audio.remoteCleanupPreset"),
			  QObject::tr("Balance cleanup strength against speech naturalness for incoming audio.") },
			{ QStringLiteral("keys.globalShortcuts"),
			  QObject::tr("Enable or disable the global shortcut engine without changing configured bindings.") },
			{ QStringLiteral("keys.shortcuts"),
			  QObject::tr("Add, remove, capture, and rebind global shortcuts in the Modern settings window.") },
			{ QStringLiteral("keys.enableUiAccess"),
			  QObject::tr("Allow the Windows shortcut engine to work while elevated applications are focused.") },
			{ QStringLiteral("keys.enableGKey"), QObject::tr("Enable Logitech G-key shortcut input.") },
			{ QStringLiteral("keys.enableXboxInput"), QObject::tr("Enable XInput controller shortcut input.") },
			{ QStringLiteral("about.openMumble"),
			  QObject::tr("Open the full Mumble About dialog with project, license, and credits.") },
			{ QStringLiteral("about.openQt"),
			  QObject::tr("Open Qt runtime and licensing information for this client.") }
		};
		return tooltips.value(id);
	}

	QVariantMap sectionItem(const QString &title, const QVariantList &fields) {
		QVariantList resolvedFields;
		resolvedFields.reserve(fields.size());
		for (const QVariant &fieldValue : fields) {
			QVariantMap field = fieldValue.toMap();
			const QString id  = field.value(QStringLiteral("id")).toString();
			if (!id.isEmpty() && field.value(QStringLiteral("tooltip")).toString().isEmpty()) {
				field = tooltippedField(field, fieldTooltip(id));
			}
			resolvedFields.push_back(field);
		}

		QVariantMap section;
		section.insert(QStringLiteral("title"), title);
		section.insert(QStringLiteral("fields"), resolvedFields);
		return section;
	}

	QVariantMap advancedSection(QVariantMap section) {
		section.insert(QStringLiteral("advanced"), true);
		return section;
	}

	int percentFromFloat(const float value) {
		return qBound(0, static_cast< int >(std::lround(value * 100.0f)), 200);
	}

	float floatFromPercent(const QVariant &value) {
		return static_cast< float >(qBound(0, value.toInt(), 200)) / 100.0f;
	}

	int bitrateKbitFromBits(const int value) {
		return qBound(8, static_cast< int >(std::lround(static_cast< double >(value) / 1000.0)), 512);
	}

	int bitrateBitsFromKbit(const QVariant &value) {
		return qBound(8, value.toInt(), 512) * 1000;
	}

	QVariantList transmitModeOptions() {
		return QVariantList {
			optionItem(Settings::Continuous, QObject::tr("Continuous"),
					   QObject::tr("Transmit microphone audio continuously while connected.")),
			optionItem(Settings::VAD, QObject::tr("Voice Activity"),
					   QObject::tr("Open the microphone automatically when speech is detected.")),
			optionItem(Settings::PushToTalk, QObject::tr("Push To Talk"),
					   QObject::tr("Transmit only while your push-to-talk shortcut is held."))
		};
	}

	QVariantList vadSourceOptions() {
		return QVariantList {
			optionItem(static_cast< int >(Settings::SignalToNoise), QObject::tr("Speech probability"), true,
					   QObject::tr("Recommended for most microphones; separates speech from steady background noise.")),
			optionItem(static_cast< int >(Settings::Hybrid), QObject::tr("Speech + volume"), true,
					   QObject::tr("Requires speech probability and volume level to agree before opening the microphone.")),
			optionItem(static_cast< int >(Settings::Amplitude), QObject::tr("Volume level"), true,
					   QObject::tr("Fallback for manual tuning; reacts to microphone loudness after cleanup.")) };
	}

	QString vadSourceLabel(const Settings::VADSource source) {
		switch (source) {
			case Settings::SignalToNoise:
				return QObject::tr("Speech probability");
			case Settings::Hybrid:
				return QObject::tr("Speech + volume");
			case Settings::Amplitude:
				return QObject::tr("Volume level");
		}

		return QObject::tr("Volume level");
	}

	QVariantList inputGateModeOptions() {
		return QVariantList {
			optionItem(static_cast< int >(Settings::InputGateOff), QObject::tr("Off"), true,
					   QObject::tr("Preserve the classic voice-activity behavior.")),
			optionItem(static_cast< int >(Settings::InputGateBalanced), QObject::tr("Balanced"), true,
					   QObject::tr("Require voice activity, speech probability, and a small level floor before opening.")),
			optionItem(static_cast< int >(Settings::InputGateStrict), QObject::tr("Strict"), true,
					   QObject::tr("Reject more non-voice audio, with a higher chance of clipping very soft speech."))
		};
	}

	QVariantList proxyOptions() {
		return QVariantList { optionItem(Settings::NoProxy, QObject::tr("No proxy"),
										 QObject::tr("Connect directly to servers.")),
							  optionItem(Settings::HttpProxy, QObject::tr("HTTP proxy"),
										 QObject::tr("Route server connections through an HTTP proxy.")),
							  optionItem(Settings::Socks5Proxy, QObject::tr("SOCKS5 proxy"),
										 QObject::tr("Route server connections through a SOCKS5 proxy.")) };
	}

	QVariantList alwaysOnTopOptions() {
		return QVariantList { optionItem(Settings::OnTopNever, QObject::tr("Never"),
										 QObject::tr("Use the normal operating-system window stacking order.")),
							  optionItem(Settings::OnTopAlways, QObject::tr("Always"),
										 QObject::tr("Keep Mumble above other windows.")) };
	}

	QVariantList quitBehaviorOptions() {
		return QVariantList { optionItem(static_cast< int >(QuitBehavior::ALWAYS_ASK), QObject::tr("Always ask"), true,
										 QObject::tr("Ask what to do every time the main window is closed.")),
							  optionItem(static_cast< int >(QuitBehavior::ASK_WHEN_CONNECTED),
										 QObject::tr("Ask when connected"), true,
										 QObject::tr("Ask only when closing Mumble would disconnect from a server.")),
							  optionItem(static_cast< int >(QuitBehavior::ALWAYS_MINIMIZE),
										 QObject::tr("Always minimize"), true,
										 QObject::tr("Send Mumble to the system tray instead of quitting.")),
							  optionItem(static_cast< int >(QuitBehavior::MINIMIZE_WHEN_CONNECTED),
										 QObject::tr("Minimize when connected"), true,
										 QObject::tr("Minimize while connected, and quit normally when offline.")),
							  optionItem(static_cast< int >(QuitBehavior::ALWAYS_QUIT), QObject::tr("Always quit"), true,
										 QObject::tr("Close the client immediately when the main window is closed.")) };
	}

	QVariantList presenceTimeoutOptions() {
		return QVariantList {
			optionItem(1, QObject::tr("1 minute"), QObject::tr("Mark you idle after 1 minute of inactivity.")),
			optionItem(5, QObject::tr("5 minutes"), QObject::tr("Mark you idle after 5 minutes of inactivity.")),
			optionItem(10, QObject::tr("10 minutes"), QObject::tr("Mark you idle after 10 minutes of inactivity.")),
			optionItem(15, QObject::tr("15 minutes"), QObject::tr("Mark you idle after 15 minutes of inactivity.")),
			optionItem(30, QObject::tr("30 minutes"), QObject::tr("Mark you idle after 30 minutes of inactivity.")),
			optionItem(60, QObject::tr("60 minutes"), QObject::tr("Mark you idle after 60 minutes of inactivity."))
		};
	}

	QVariantList idleActionOptions() {
		return QVariantList { optionItem(Settings::Nothing, QObject::tr("Do nothing"),
										 QObject::tr("Leave your audio state unchanged when you become idle.")),
							  optionItem(Settings::Deafen, QObject::tr("Deafen"),
										 QObject::tr("Deafen yourself after the idle timer expires.")),
							  optionItem(Settings::Mute, QObject::tr("Mute"),
										 QObject::tr("Mute your microphone after the idle timer expires.")) };
	}

	QVariantList loopModeOptions() {
		return QVariantList { optionItem(Settings::None, QObject::tr("None"),
										 QObject::tr("Do not replay your own transmitted voice.")),
							  optionItem(Settings::Local, QObject::tr("Local"),
										 QObject::tr("Replay processed microphone audio locally without a server round trip.")),
							  optionItem(Settings::Server, QObject::tr("Server"),
										 QObject::tr("Replay your voice after it travels through the connected server.")) };
	}

	QVariantList noiseCancelModeOptions() {
		return QVariantList {
			optionItem(Settings::NoiseCancelOff, QObject::tr("Off"),
					   QObject::tr("Leave microphone noise cleanup off.")),
			optionItem(Settings::NoiseCancelSpeex, QObject::tr("Light cleanup"),
					   QObject::tr("Use lightweight classic suppression for steady hiss, hum, and fan noise.")),
			optionItem(Settings::NoiseCancelRNN, QObject::tr("Neural cleanup"),
					   QObject::tr("Use the selected neural model for stronger speech cleanup.")),
			optionItem(Settings::NoiseCancelBoth, QObject::tr("Maximum cleanup"),
					   QObject::tr("Run classic suppression before neural cleanup for difficult background noise."))
		};
	}

	QVariantList inputEnhancementProfileOptions(
		const Settings &settings, Mumble::InputEnhancement::Profile selectedProfile) {
		using namespace Mumble::InputEnhancement;
		auto profileOption = [&](Profile profile, const QString &label, const QString &description) {
			const std::optional< ExplicitProfileControlPreset > preset = qualifiedExplicitSelectionPreset(profile);
			const InputEnhancementSettingsReadiness readiness =
				inputEnhancementReadinessForSettings(
					settings, profile, preset ? preset->noiseReduction : 0,
					preset ? preset->naturalCrisp : 0, true);
			QString reason = inputEnhancementReadinessReasonText(readiness);
			if (readiness.selectable && !readiness.productionQualified) {
				reason = QObject::tr("Preview/session-only on this input backend or device identity.");
			}
			if (!reason.isEmpty() && !readiness.selectable) {
				reason.prepend(QObject::tr("Unavailable: "));
			}
			return optionItem(static_cast< int >(profile), label, readiness.selectable,
							  reason.isEmpty() ? description : description + QLatin1Char(' ') + reason);
		};

		QVariantList options {
			profileOption(Profile::Original, QObject::tr("Original"),
						  QObject::tr("Keep the established Mumble input path without added enhancement latency.")),
			profileOption(Profile::Light, QObject::tr("Light"),
						  QObject::tr("Low-cost classic suppression for steady hiss, hum, and fan noise.")),
			profileOption(Profile::Balanced, QObject::tr("Balanced"),
						  QObject::tr("RNNoise-based cleanup for everyday voice chat.")),
			profileOption(Profile::Quality, QObject::tr("Quality"),
						  QObject::tr("Full-band neural enhancement tuned for natural clarity.")),
			profileOption(Profile::VoiceFocus, QObject::tr("Voice Focus"),
						  QObject::tr("Aggressive full-band cleanup that prioritizes speech over background sound.")),
		};
		// Auto is deliberately absent from the normal profile picker. Preserve a
		// readable disabled value when opening settings that already contain an
		// experimental Auto selection; it can be disabled or replaced from the
		// Advanced controls below.
		if (selectedProfile == Profile::Auto) {
			options.append(optionItem(static_cast< int >(Profile::Auto), QObject::tr("Auto (Advanced)"), false,
									  QObject::tr("Experimental Auto is managed in Advanced settings.")));
		}
		return options;
	}

	QString inputEnhancementProfileLabel(Mumble::InputEnhancement::Profile profile) {
		using Profile = Mumble::InputEnhancement::Profile;
		switch (profile) {
			case Profile::Original:
				return QObject::tr("Original");
			case Profile::Light:
				return QObject::tr("Light");
			case Profile::Balanced:
				return QObject::tr("Balanced");
			case Profile::Quality:
				return QObject::tr("Quality");
			case Profile::VoiceFocus:
				return QObject::tr("Voice Focus");
			case Profile::Auto:
				return QObject::tr("Auto");
		}
		return QObject::tr("Unknown");
	}

	Mumble::InputEnhancement::LegacyOverride legacyInputOverride(const Settings &settings) {
		Mumble::InputEnhancement::LegacyOverride legacy;
		legacy.noiseCancelMode = static_cast< int >(settings.noiseCancelMode);
		legacy.backend         = static_cast< int >(settings.noiseCancelBackend);
		legacy.modelId         = settings.noiseCancelModelId;
		legacy.customModelPath = settings.noiseCancelCustomModelPath;
		legacy.speexNoiseCancelStrength = settings.iSpeexNoiseCancelStrength;
		return legacy;
	}

	Mumble::InputEnhancement::DeviceIdentity draftInputEnhancementDeviceIdentity(const Settings &settings);

	void captureLegacyInputOverride(Settings &settings) {
		using namespace Mumble::InputEnhancement;
		const LegacyOverride legacy = legacyInputOverride(settings);
		const Profile mappedProfile  = profileForLegacy(static_cast< int >(settings.noiseCancelMode),
														static_cast< int >(settings.noiseCancelBackend));
		const DeviceIdentity identity = draftInputEnhancementDeviceIdentity(settings);
		if (!identity.backendId.isEmpty() && !identity.physicalId.isEmpty()) {
			if (DeviceProfileState *profile = ensureDeviceProfile(settings.inputEnhancement, identity)) {
				profile->legacyOverride        = legacy;
				profile->preference.profile     = mappedProfile;
				profile->preference.autoAdapt   = false;
				profile->calibrated             = false;
				profile->pendingValidation      = false;
				profile->pendingRecipeBinding.reset();
				profile->pendingAutoRecipeSetFingerprint.reset();
				profile->rollbackUndoPreference.reset();
				profile->rollbackUndoRecipeBinding.reset();
				profile->rollbackUndoAutoRecipeSetFingerprint.reset();
				return;
			}
		}

		settings.inputEnhancement.legacyOverride             = legacy;
		settings.inputEnhancement.defaultPreference.profile   = mappedProfile;
		settings.inputEnhancement.defaultPreference.autoAdapt = false;
	}

	AudioInputPtr currentAudioInput() {
		return Global::g_global_struct ? Global::g_global_struct->ai : AudioInputPtr {};
	}

	Mumble::InputEnhancement::DeviceIdentity draftInputEnhancementDeviceIdentity(const Settings &settings) {
		QString backend = settings.qsAudioInput;
		if (backend.isEmpty()) {
			backend = AudioInputRegistrar::current;
		}
		return AudioInputRegistrar::resolveDeviceIdentity(backend, settings);
	}

	const Mumble::InputEnhancement::DefaultPreference &currentInputEnhancementPreference(
		const Settings &settings) {
		return Mumble::InputEnhancement::preferenceForDevice(
			settings.inputEnhancement, draftInputEnhancementDeviceIdentity(settings));
	}

	bool currentInputEnhancementUsesLegacyOverride(const Settings &settings) {
		using namespace Mumble::InputEnhancement;
		const DeviceIdentity identity            = draftInputEnhancementDeviceIdentity(settings);
		const DeviceProfileState *deviceProfile   = findDeviceProfile(settings.inputEnhancement, identity);
		if (deviceProfile) {
			const bool unsafePendingState =
				deviceProfile->pendingValidation
				&& (!deviceProfile->lastKnownGood
					|| !executionBindingMatchesPreference(deviceProfile->preference,
														 deviceProfile->pendingRecipeBinding,
														 deviceProfile->pendingAutoRecipeSetFingerprint)
					|| !executionBindingMatchesPreference(*deviceProfile->lastKnownGood,
														 deviceProfile->lastKnownGoodRecipeBinding,
														 deviceProfile->lastKnownGoodAutoRecipeSetFingerprint));
			return !unsafePendingState && deviceProfile->legacyOverride
				   && isValidLegacyOverride(*deviceProfile->legacyOverride);
		}

		return settings.inputEnhancement.legacyOverride
			   && isValidLegacyOverride(*settings.inputEnhancement.legacyOverride);
	}

	Mumble::InputEnhancement::DefaultPreference *editableCurrentInputEnhancementPreference(Settings &settings) {
		const Mumble::InputEnhancement::DeviceIdentity identity = draftInputEnhancementDeviceIdentity(settings);
		if (identity.backendId.isEmpty() || identity.physicalId.isEmpty()) {
			return &settings.inputEnhancement.defaultPreference;
		}
		Mumble::InputEnhancement::DeviceProfileState *profile =
			Mumble::InputEnhancement::ensureDeviceProfile(settings.inputEnhancement, identity);
		return profile ? &profile->preference : nullptr;
	}

	void disarmDraftInputEnhancementProbationForManualEdit(Settings &settings) {
		using namespace Mumble::InputEnhancement;
		const DeviceIdentity identity = draftInputEnhancementDeviceIdentity(settings);
		DeviceProfileState *state      = ensureDeviceProfile(settings.inputEnhancement, identity);
		if (!state || !state->pendingValidation) {
			return;
		}
		// This is draft-only state. The persisted candidate remains armed until
		// Apply durably replaces it with the newly bound candidate.
		state->pendingValidation = false;
		state->pendingRecipeBinding.reset();
		state->pendingAutoRecipeSetFingerprint.reset();
		state->rollbackUndoPreference.reset();
		state->rollbackUndoRecipeBinding.reset();
		state->rollbackUndoAutoRecipeSetFingerprint.reset();
	}

	void projectInputEnhancementPreference(Settings &settings) {
		using Profile = Mumble::InputEnhancement::Profile;
		const Mumble::InputEnhancement::DefaultPreference &preference =
			currentInputEnhancementPreference(settings);

		const Mumble::InputEnhancement::DeviceIdentity identity = draftInputEnhancementDeviceIdentity(settings);
		if (Mumble::InputEnhancement::DeviceProfileState *profile =
				Mumble::InputEnhancement::ensureDeviceProfile(settings.inputEnhancement, identity)) {
			profile->legacyOverride.reset();
		} else if (identity.backendId.isEmpty() || identity.physicalId.isEmpty()) {
			settings.inputEnhancement.legacyOverride.reset();
		}
		settings.iSpeexNoiseCancelStrength = -qBound(0, preference.reduction, 100);
		switch (preference.profile) {
			case Profile::Original:
				settings.noiseCancelMode = Settings::NoiseCancelOff;
				break;
			case Profile::Light:
				settings.noiseCancelMode = Settings::NoiseCancelSpeex;
				break;
			case Profile::Balanced:
				settings.noiseCancelMode            = Settings::NoiseCancelRNN;
				settings.noiseCancelBackend         = Settings::RNNoiseBackend;
				settings.noiseCancelModelId         = QStringLiteral("rnnoise:embedded");
				settings.noiseCancelCustomModelPath.clear();
				break;
			case Profile::Quality:
			case Profile::VoiceFocus:
				settings.noiseCancelMode            = Settings::NoiseCancelRNN;
				settings.noiseCancelBackend         = Settings::DeepFilterNetBackend;
				settings.noiseCancelModelId         = QStringLiteral("deepfilternet:low-latency");
				settings.noiseCancelCustomModelPath.clear();
				break;
			case Profile::Auto:
				settings.noiseCancelMode            = Settings::NoiseCancelRNN;
				settings.noiseCancelBackend         = Settings::RNNoiseBackend;
				settings.noiseCancelModelId         = QStringLiteral("rnnoise:embedded");
				settings.noiseCancelCustomModelPath.clear();
				break;
		}
	}

	bool inputEnhancementSelectionChanged(const Settings &draft, const Settings &original) {
		const Mumble::InputEnhancement::DeviceIdentity draftIdentity =
			draftInputEnhancementDeviceIdentity(draft);
		const Mumble::InputEnhancement::DeviceIdentity originalIdentity =
			draftInputEnhancementDeviceIdentity(original);
		if (draftIdentity.backendId != originalIdentity.backendId
			|| draftIdentity.physicalId != originalIdentity.physicalId) {
			return true;
		}
		return currentInputEnhancementPreference(draft) != currentInputEnhancementPreference(original);
	}

	bool reconcileResolvedInputEnhancementProbation(Settings &draft, const Settings &original) {
		using namespace Mumble::InputEnhancement;
		if (!Global::g_global_struct) {
			return false;
		}
		const DeviceIdentity identity = draftInputEnhancementDeviceIdentity(draft);
		const DeviceProfileState *originalState = findDeviceProfile(original.inputEnhancement, identity);
		const DeviceProfileState *runtimeState = findDeviceProfile(Global::get().s.inputEnhancement, identity);
		const DeviceProfileState *draftState = findDeviceProfile(draft.inputEnhancement, identity);
		if (!originalState || !draftState || !originalState->pendingValidation || !runtimeState
			|| runtimeState->pendingValidation || draftState->preference != originalState->preference) {
			return false;
		}
		DeviceProfileState *mutableDraftState = ensureDeviceProfile(draft.inputEnhancement, identity);
		if (!mutableDraftState) {
			return false;
		}

		// A dialog can remain open while the real audio probation finishes or
		// rolls back. Merge that authoritative result before persisting unrelated
		// settings so stale draft state cannot resurrect the candidate.
		DeviceProfileState resolved = *runtimeState;
		resolved.identity           = identity;
		*mutableDraftState          = std::move(resolved);
		projectInputEnhancementPreference(draft);
		return true;
	}

	std::optional< Mumble::InputEnhancement::RecipeBinding > verifiedRecipeBindingForPreference(
		const Mumble::InputEnhancement::DefaultPreference &preference,
		const Mumble::InputEnhancement::DeviceIdentity &identity) {
		using namespace Mumble::InputEnhancement;
		if (preference.profile == Profile::Original || preference.profile == Profile::Auto
			|| !Global::g_global_struct) {
			return std::nullopt;
		}
		const InputEnhancementPackageVerifier *verifier = Global::get().inputEnhancementPackageVerifier;
		if (!verifier || !verifier->verificationHealthy()) {
			return std::nullopt;
		}

		ResolveRequest request;
		request.profile             = preference.profile;
		request.noiseReduction      = preference.reduction;
		request.naturalCrisp        = preference.character;
		request.cpuClass            = inputEnhancementCpuClass(verifier, preference.profile);
		request.backendAvailability = BackendAvailability::compiled();
		request.captureDevice       = CaptureDeviceContext::liveDevice(identity.backendId, identity.stable);
		if (!verifier->readinessForProfile(request).selectable) {
			return std::nullopt;
		}
		const Recipe recipe = RecipeCatalog::resolve(request);
		if (recipe.effectiveProfile() != preference.profile || !verifier->recipeAuthorized(recipe)) {
			return std::nullopt;
		}

		QString modelSha256;
		QString modelRelativePath;
		if (recipe.usesNeuralProcessor()) {
			if (!verifier->modelAuthorized(recipe.modelId())) {
				return std::nullopt;
			}
			modelSha256       = verifier->modelSha256Hex(recipe.modelId());
			modelRelativePath = verifier->modelRelativePath(recipe.modelId());
		}
		const QString catalogRevision = verifier->managedBySignedPackage()
			? verifier->catalogRevision()
			: QStringLiteral("unmanaged-build-zero");
		RecipeBinding binding =
			recipeBindingForRecipe(recipe, catalogRevision, modelSha256, modelRelativePath);
		if (!recipeBindingMatches(binding, recipe, catalogRevision, modelSha256, modelRelativePath)
			|| !recipeBindingMatchesPreference(binding, preference)) {
			return std::nullopt;
		}
		return binding;
	}

	struct ExactManualLastKnownGood final {
		Mumble::InputEnhancement::DefaultPreference preference;
		std::optional< Mumble::InputEnhancement::RecipeBinding > binding;
	};

	ExactManualLastKnownGood lastKnownGoodForManualProfileChange(
		const Settings &original, const Mumble::InputEnhancement::DeviceIdentity &identity) {
		using namespace Mumble::InputEnhancement;
		const auto safeOriginal = []() {
			ExactManualLastKnownGood result;
			result.preference.profile   = Profile::Original;
			result.preference.autoAdapt = false;
			return result;
		};
		const auto exactStoredFixedProfile = [&safeOriginal, &identity](
			const DefaultPreference &preference, const std::optional< RecipeBinding > &storedBinding,
			const std::optional< QString > &storedAutoFingerprint) -> std::optional< ExactManualLastKnownGood > {
			if (preference.profile == Profile::Original) {
				return safeOriginal();
			}
			if (preference.profile == Profile::Auto || storedAutoFingerprint || !storedBinding) {
				return std::nullopt;
			}
			const std::optional< RecipeBinding > currentBinding =
				verifiedRecipeBindingForPreference(preference, identity);
			if (!currentBinding || *currentBinding != *storedBinding) {
				return std::nullopt;
			}
			return ExactManualLastKnownGood { preference, *currentBinding };
		};

		const DeviceProfileState *originalState = findDeviceProfile(original.inputEnhancement, identity);
		const DeviceProfileState *runtimeState = Global::g_global_struct
			? findDeviceProfile(Global::get().s.inputEnhancement, identity)
			: nullptr;
		const DeviceProfileState *stateForLegacyCheck = runtimeState ? runtimeState : originalState;
		const bool legacyActive = stateForLegacyCheck
			? (stateForLegacyCheck->legacyOverride && isValidLegacyOverride(*stateForLegacyCheck->legacyOverride))
			: (original.inputEnhancement.legacyOverride
			   && isValidLegacyOverride(*original.inputEnhancement.legacyOverride));
		if (legacyActive) {
			return safeOriginal();
		}

		if (runtimeState) {
			if (runtimeState->pendingValidation && runtimeState->lastKnownGood) {
				if (const auto exact = exactStoredFixedProfile(
						*runtimeState->lastKnownGood, runtimeState->lastKnownGoodRecipeBinding,
						runtimeState->lastKnownGoodAutoRecipeSetFingerprint)) {
					return *exact;
				}
				return safeOriginal();
			}
			if (!runtimeState->pendingValidation && runtimeState->preference.profile == Profile::Original) {
				return safeOriginal();
			}
			if (!runtimeState->pendingValidation && runtimeState->lastKnownGood
				&& *runtimeState->lastKnownGood == runtimeState->preference) {
				const auto exact = exactStoredFixedProfile(
					*runtimeState->lastKnownGood, runtimeState->lastKnownGoodRecipeBinding,
					runtimeState->lastKnownGoodAutoRecipeSetFingerprint);
				const AudioInputPtr input = currentAudioInput();
				if (exact && input) {
					const std::optional< RecipeBinding > active =
						input->healthyActiveInputEnhancementBinding(identity, exact->preference);
					if (active && exact->binding && *active == *exact->binding) {
						return *exact;
					}
				}
			}
			return safeOriginal();
		}

		// A pending candidate can carry a previously proven rollback target even
		// when no AudioInput currently exists (for example while capture restarts).
		// Non-pending persisted profiles are not promoted to LKG without matching
		// healthy runtime evidence.
		if (originalState && originalState->pendingValidation && originalState->lastKnownGood) {
			if (const auto exact = exactStoredFixedProfile(
					*originalState->lastKnownGood, originalState->lastKnownGoodRecipeBinding,
					originalState->lastKnownGoodAutoRecipeSetFingerprint)) {
				return *exact;
			}
		}
		return safeOriginal();
	}

	bool prepareManualInputEnhancementProbation(Settings &draft, const Settings &original, qint64 nowEpochMs) {
		using namespace Mumble::InputEnhancement;
		const DeviceIdentity identity = draftInputEnhancementDeviceIdentity(draft);
		if (identity.backendId.isEmpty() || identity.physicalId.isEmpty()) {
			return currentInputEnhancementPreference(draft).profile == Profile::Original;
		}
		DeviceProfileState *draftState = ensureDeviceProfile(draft.inputEnhancement, identity);
		if (!draftState) {
			return false;
		}
		DefaultPreference candidate = draftState->preference;
		candidate.autoAdapt         = candidate.profile == Profile::Auto;

		const DeviceProfileState *originalState = findDeviceProfile(original.inputEnhancement, identity);
		const DefaultPreference &previous = preferenceForDevice(original.inputEnhancement, identity);
		const bool originalLegacyActive = originalState
			? (originalState->legacyOverride && isValidLegacyOverride(*originalState->legacyOverride))
			: (original.inputEnhancement.legacyOverride
			   && isValidLegacyOverride(*original.inputEnhancement.legacyOverride));
		if (candidate == previous && !originalLegacyActive && originalState) {
			DeviceProfileState unchanged = *originalState;
			unchanged.identity           = identity;
			*draftState                  = std::move(unchanged);
			projectInputEnhancementPreference(draft);
			return true;
		}

		if (candidate.profile == Profile::Original) {
			candidate.autoAdapt                         = false;
			draftState->preference                     = candidate;
			draftState->lastKnownGood                  = candidate;
			draftState->lastKnownGoodRecipeBinding.reset();
			draftState->lastKnownGoodAutoRecipeSetFingerprint.reset();
			draftState->pendingRecipeBinding.reset();
			draftState->pendingAutoRecipeSetFingerprint.reset();
			draftState->calibrated       = false;
			draftState->pendingValidation = false;
			draftState->lastUsedEpochMs = std::max(draftState->lastUsedEpochMs, nowEpochMs);
			draftState->lastRollbackReason.clear();
			draftState->legacyOverride.reset();
			draftState->rollbackUndoPreference.reset();
			draftState->rollbackUndoRecipeBinding.reset();
			draftState->rollbackUndoAutoRecipeSetFingerprint.reset();
			return true;
		}
		if (candidate.profile == Profile::Auto) {
			// Auto has its own complete recipe-set binding and is not a direct
			// fixed-profile selection in the community core release.
			return false;
		}

		const std::optional< RecipeBinding > candidateBinding =
			verifiedRecipeBindingForPreference(candidate, identity);
		if (!candidateBinding) {
			return false;
		}
		const ExactManualLastKnownGood lastKnownGood =
			lastKnownGoodForManualProfileChange(original, identity);
		return armManualProfileProbation(draft.inputEnhancement, identity, candidate, *candidateBinding,
									 lastKnownGood.preference, lastKnownGood.binding, nowEpochMs);
	}

	QVariantList remoteSpeechCleanupPresetOptions() {
		return QVariantList {
			optionItem(Settings::Light, QObject::tr("Light"),
					   QObject::tr("Subtle cleanup that keeps incoming speech sounding most natural.")),
			optionItem(Settings::Normal, QObject::tr("Normal"),
					   QObject::tr("Balanced cleanup for everyday voice chat.")),
			optionItem(Settings::Aggressive, QObject::tr("Aggressive"),
					   QObject::tr("Stronger cleanup for noisy incoming audio, with a higher chance of artifacts."))
		};
	}

	QString speechCleanupBackendHint(const Settings::SpeechCleanupBackend backend) {
		const QString unavailableReason = Mumble::SpeechCleanup::unavailableReason(backend);
		if (!unavailableReason.isEmpty()) {
			return unavailableReason;
		}

		switch (backend) {
			case Settings::RNNoiseBackend:
				return QObject::tr("Lightweight neural noise suppression that is a good default for live voice.");
			case Settings::DTLNBackend:
				return QObject::tr("Deep-learning denoising with bundled model variants for different training sets.");
			case Settings::DeepFilterNetBackend:
				return QObject::tr("DeepFilterNet speech enhancement for stronger cleanup when the backend is available.");
		}

		return QString();
	}

	QVariantList speechCleanupBackendOptions() {
		QVariantList options;
		for (Settings::SpeechCleanupBackend backend : Mumble::SpeechCleanup::supportedBackends) {
			options.push_back(optionItem(static_cast< int >(backend),
										 QString::fromLatin1(Mumble::SpeechCleanup::backendDisplayName(backend)),
										 Mumble::SpeechCleanup::isBackendAvailable(backend),
										 speechCleanupBackendHint(backend)));
		}
		return options;
	}

	QString speechCleanupModelHint(const Settings::SpeechCleanupBackend backend, const QString &modelID) {
		const QString normalizedModelID = Mumble::SpeechCleanup::normalizedModelId(backend, modelID);

		switch (backend) {
			case Settings::RNNoiseBackend:
				if (normalizedModelID == QLatin1String("rnnoise:little")) {
					return QObject::tr("Smaller local RNNoise model with lower resource use.");
				}
				if (normalizedModelID == QLatin1String("rnnoise:custom")) {
					return QObject::tr("Load a custom RNNoise model file from the path below.");
				}
				return QObject::tr("Built-in RNNoise model bundled with Mumble.");
			case Settings::DTLNBackend:
				if (normalizedModelID == QLatin1String("dtln:norm500h")) {
					return QObject::tr("DTLN model normalized from a larger training set; useful for heavier cleanup.");
				}
				if (normalizedModelID == QLatin1String("dtln:norm40h")) {
					return QObject::tr("DTLN model normalized from a smaller training set; useful for lighter cleanup.");
				}
				return QObject::tr("Baseline DTLN model bundled with Mumble.");
			case Settings::DeepFilterNetBackend:
				if (normalizedModelID == QLatin1String("deepfilternet:gentle")) {
					return QObject::tr("Lower DeepFilterNet attenuation for fewer voice artifacts.");
				}
				if (normalizedModelID == QLatin1String("deepfilternet:balanced")) {
					return QObject::tr("Moderate DeepFilterNet attenuation for everyday cleanup.");
				}
				if (normalizedModelID == QLatin1String("deepfilternet:low-latency")) {
					return QObject::tr("DeepFilterNet3 low-latency model with less look-ahead than the default model.");
				}
				if (normalizedModelID == QLatin1String("deepfilternet:maximum")) {
					return QObject::tr("Full DeepFilterNet attenuation without the extra post-filter.");
				}
				if (normalizedModelID == QLatin1String("deepfilternet:maximum-postfilter")) {
					return QObject::tr("Full DeepFilterNet attenuation with extra suppression in very noisy sections.");
				}
				return QObject::tr("Default DeepFilterNet cleanup profile.");
		}

		return QString();
	}

	QVariantList speechCleanupModelOptions(const Settings::SpeechCleanupBackend backend) {
		QVariantList options;
		for (const QString &modelID : Mumble::SpeechCleanup::supportedModelIds(backend)) {
			options.push_back(optionItem(modelID, Mumble::SpeechCleanup::modelDisplayName(backend, modelID), true,
										 speechCleanupModelHint(backend, modelID)));
		}
		return options;
	}

	QString normalizedSpeechCleanupModelID(const Settings::SpeechCleanupBackend backend, const QString &modelID) {
		return Mumble::SpeechCleanup::normalizedModelId(backend, modelID);
	}

	QVariantList echoOptionsFor(const Settings &settings) {
		QVariantList options;
		const AudioInputRegistrar *registrar = nullptr;
		const QString inputSystem =
			settings.qsAudioInput.isEmpty() ? AudioInputRegistrar::current : settings.qsAudioInput;
		const QString outputSystem =
			settings.qsAudioOutput.isEmpty() ? AudioOutputRegistrar::current : settings.qsAudioOutput;
		if (AudioInputRegistrar::qmNew) {
			registrar = AudioInputRegistrar::qmNew->value(inputSystem);
		}

		for (const EchoCancelOption &option : EchoCancelOption::getOptions()) {
			bool available = option.id == EchoCancelOptionID::DISABLED;
			if (registrar && option.id != EchoCancelOptionID::DISABLED) {
				available = registrar->canEcho(option.id, outputSystem);
			}
			options.push_back(optionItem(static_cast< int >(option.id), option.description, available,
										 available ? QString() : QObject::tr("Not available for the selected audio backend/device combination.")));
		}
		return options;
	}

	QList< QString > inputSystemNames() {
		return AudioInputRegistrar::qmNew ? AudioInputRegistrar::qmNew->keys() : QList< QString >();
	}

	QList< QString > outputSystemNames() {
		return AudioOutputRegistrar::qmNew ? AudioOutputRegistrar::qmNew->keys() : QList< QString >();
	}

	int systemIndex(const QList< QString > &systems, const QString &selected, const QString &fallback) {
		int index = systems.indexOf(selected);
		if (index >= 0) {
			return index;
		}
		index = systems.indexOf(fallback);
		return index >= 0 ? index : 0;
	}

	QVariantList systemOptions(const QList< QString > &systems) {
		QVariantList options;
		for (int i = 0; i < systems.size(); ++i) {
			options.push_back(optionItem(i, systems.at(i)));
		}
		return options;
	}

	QString systemNameAt(const QList< QString > &systems, const QVariant &value, const QString &fallback) {
		const int index = value.toInt();
		if (index >= 0 && index < systems.size()) {
			return systems.at(index);
		}
		return fallback;
	}

	QVariant inputDeviceChoiceFor(const Settings &settings, const QString &system) {
		if (system == QLatin1String("WASAPI")) {
			return settings.qsWASAPIInput;
		}
		if (system == QLatin1String("ALSA")) {
			return settings.qsALSAInput;
		}
		if (system == QLatin1String("PulseAudio")) {
			return settings.qsPulseAudioInput;
		}
		if (system == QLatin1String("PipeWire")) {
			return settings.pipeWireInput;
		}
		if (system == QLatin1String("PortAudio")) {
			return settings.iPortAudioInput;
		}
		if (system == QLatin1String("CoreAudio")) {
			return settings.qsCoreAudioInput;
		}
		if (system == QLatin1String("OSS")) {
			return settings.qsOSSInput;
		}
		return AudioInputRegistrar::qmNew && AudioInputRegistrar::qmNew->contains(system)
				   ? AudioInputRegistrar::qmNew->value(system)->getDeviceChoice()
				   : QVariant();
	}

	QVariant outputDeviceChoiceFor(const Settings &settings, const QString &system) {
		if (system == QLatin1String("WASAPI")) {
			return settings.qsWASAPIOutput;
		}
		if (system == QLatin1String("ALSA")) {
			return settings.qsALSAOutput;
		}
		if (system == QLatin1String("PulseAudio")) {
			return settings.qsPulseAudioOutput;
		}
		if (system == QLatin1String("PipeWire")) {
			return settings.pipeWireOutput;
		}
		if (system == QLatin1String("PortAudio")) {
			return settings.iPortAudioOutput;
		}
		if (system == QLatin1String("CoreAudio")) {
			return settings.qsCoreAudioOutput;
		}
		if (system == QLatin1String("JACK")) {
			return settings.qsJackAudioOutput;
		}
		if (system == QLatin1String("OSS")) {
			return settings.qsOSSOutput;
		}
		return AudioOutputRegistrar::qmNew && AudioOutputRegistrar::qmNew->contains(system)
				   ? AudioOutputRegistrar::qmNew->value(system)->getDeviceChoice()
				   : QVariant();
	}

	bool deviceChoiceMatches(const QVariant &lhs, const QVariant &rhs) {
		return lhs == rhs || lhs.toString() == rhs.toString();
	}

	QList< audioDevice > inputDeviceChoices(const QString &system) {
		if (!AudioInputRegistrar::qmNew || !AudioInputRegistrar::qmNew->contains(system)) {
			return {};
		}
		return AudioInputRegistrar::qmNew->value(system)->getDeviceChoices();
	}

	QList< audioDevice > outputDeviceChoices(const QString &system) {
		if (!AudioOutputRegistrar::qmNew || !AudioOutputRegistrar::qmNew->contains(system)) {
			return {};
		}
		return AudioOutputRegistrar::qmNew->value(system)->getDeviceChoices();
	}

	QList< audioDevice > deviceChoicesWithUnavailableSelection(QList< audioDevice > choices,
																 const QVariant &selected) {
		if (selected.toString().isEmpty()) {
			return choices;
		}
		for (const audioDevice &choice : choices) {
			if (deviceChoiceMatches(choice.second, selected)) {
				return choices;
			}
		}
		choices.prepend(audioDevice(QObject::tr("Previously selected device — unavailable"), selected));
		return choices;
	}

	QVariantList deviceOptions(const QList< audioDevice > &choices) {
		QVariantList options;
		for (int i = 0; i < choices.size(); ++i) {
			options.push_back(optionItem(i, choices.at(i).first));
		}
		return options;
	}

	int deviceIndex(const QList< audioDevice > &choices, const QVariant &selected) {
		for (int i = 0; i < choices.size(); ++i) {
			if (deviceChoiceMatches(choices.at(i).second, selected)) {
				return i;
			}
		}
		return choices.isEmpty() ? -1 : 0;
	}

#if defined(Q_OS_WIN) && defined(USE_WASAPI)
	ERole wasapiRole(const Settings &settings) {
		const QString role = settings.qsWASAPIRole.trimmed().toLower();
		if (role == QLatin1String("console")) return eConsole;
		if (role == QLatin1String("multimedia")) return eMultimedia;
		return eCommunications;
	}

	QVariantList wasapiRoutingOptions() {
		return QVariantList {
			optionItem(0, QObject::tr("Follow Windows communications device"), true,
					   QObject::tr("Automatically follows the Windows default for Mumble's selected audio role.")),
			optionItem(1, QObject::tr("Prefer selected device"), true,
					   QObject::tr("Uses the selected device when available and a temporary Windows fallback otherwise.")),
			optionItem(2, QObject::tr("Selected device only"), true,
					   QObject::tr("Pauses this stream instead of switching to another microphone or speaker."))
		};
	}

	int wasapiRoutingIndex(const QString &name, const bool configuredEndpointPresent) {
		switch (Mumble::WASAPI::routingPolicyFromName(name, configuredEndpointPresent)) {
			case Mumble::WASAPI::RoutingPolicy::FollowDefault: return 0;
			case Mumble::WASAPI::RoutingPolicy::StrictSelected: return 2;
			case Mumble::WASAPI::RoutingPolicy::PreferSelected:
			default: return 1;
		}
	}

	QVariantList wasapiLatencyOptions() {
		return QVariantList {
			optionItem(0, QObject::tr("Stable (recommended)"), true,
					   QObject::tr(
						   "Uses Mumble's traditional WASAPI shared-mode initialization and lets Windows and the driver "
						   "keep conservative buffering. This gives audio callbacks more scheduling headroom and is the "
						   "best default for reliability, battery life, Bluetooth devices, USB headsets, and docks.")),
			optionItem(1, QObject::tr("Balanced"), true,
					   QObject::tr(
						   "Uses IAudioClient3 and requests the driver's advertised default shared-mode engine period. "
						   "This may reduce local device buffering while retaining more margin than the minimum period. "
						   "It causes more frequent callbacks than Stable on some drivers and automatically falls back to "
						   "Stable when IAudioClient3 or the requested format is unsupported.")),
			optionItem(2, QObject::tr("Minimum period (advanced)"), true,
					   QObject::tr(
						   "Requests the driver's minimum supported IAudioClient3 shared-mode period. This minimizes only "
						   "the local device-buffer contribution to latency; it does not improve network delay. The tighter "
						   "callback deadlines increase CPU wake-ups and the risk of underruns, crackles, or dropouts. Use "
						   "it only after measuring a benefit; unsupported requests fall back to Stable."))
		};
	}

	int wasapiLatencyIndex(const QString &name) {
		switch (Mumble::WASAPI::latencyProfileFromName(name)) {
			case Mumble::WASAPI::LatencyProfile::Balanced: return 1;
			case Mumble::WASAPI::LatencyProfile::Low: return 2;
			case Mumble::WASAPI::LatencyProfile::Stable:
			default: return 0;
		}
	}

	QVariantMap wasapiRuntimeStatusField(const EDataFlow flow) {
		const Mumble::WASAPI::RuntimeState state = WASAPISystem::runtimeState(flow);
		QString text;
		if (state.usingFallback) {
			text = QObject::tr("Preferred device unavailable. Currently using %1 temporarily.")
					   .arg(state.activeDisplayName.isEmpty() ? QObject::tr("the Windows communications device")
															  : state.activeDisplayName);
		} else if (!state.preferredAvailable && state.policy == Mumble::WASAPI::RoutingPolicy::StrictSelected) {
			text = QObject::tr("Selected device unavailable. This audio stream is paused by strict routing.");
		} else if (state.reboundByFingerprint) {
			text = QObject::tr("Reconnected to %1 after Windows assigned it a new endpoint identifier.")
					   .arg(state.activeDisplayName);
		} else if (!state.activeDisplayName.isEmpty()) {
			text = QObject::tr("Currently using %1.").arg(state.activeDisplayName);
		} else {
			text = QObject::tr("The WASAPI stream has not selected an active device yet.");
		}
		if (!state.lastError.isEmpty() && !text.contains(state.lastError)) {
			text += QLatin1Char(' ');
			text += state.lastError;
		}
		return noteField(text);
	}
#endif

	int percentFromInvertedFloat(const float value) {
		return qBound(0, static_cast< int >(std::lround((1.0f - value) * 100.0f)), 100);
	}

	float invertedFloatFromPercent(const QVariant &value) {
		return 1.0f - (static_cast< float >(qBound(0, value.toInt(), 100)) / 100.0f);
	}

	int vadThresholdFromFloat(const float value) {
		return qBound(0, static_cast< int >(std::lround(value * 100.0f)), 100);
	}

	float vadThresholdFromPercent(const QVariant &value) {
		return static_cast< float >(qBound(0, value.toInt(), 100)) / 100.0f;
	}

	int amplificationFromMinLoudness(const int minLoudness);
	bool hasAvailableSpeechCleanupBackend();

	Settings::NoiseCancel recommendedNoiseCancelMode(const Settings &settings) {
		if (settings.noiseCancelMode != Settings::NoiseCancelOff) {
			return settings.noiseCancelMode;
		}
		return hasAvailableSpeechCleanupBackend() ? Settings::NoiseCancelBoth : Settings::NoiseCancelSpeex;
	}

	QString calibrationWorkerStateName(Mumble::InputEnhancement::CalibrationEvaluationWorker::State state) {
		using State = Mumble::InputEnhancement::CalibrationEvaluationWorker::State;
		switch (state) {
			case State::Running:
				return QStringLiteral("running");
			case State::Cancelling:
				return QStringLiteral("cancelling");
			case State::Succeeded:
				return QStringLiteral("succeeded");
			case State::Failed:
				return QStringLiteral("failed");
			case State::Cancelled:
				return QStringLiteral("cancelled");
			case State::Idle:
				return QStringLiteral("idle");
		}
		return QStringLiteral("idle");
	}

	QString autoCalibrationUnavailableText() {
		return QObject::tr("Calibration is unavailable while Auto or automatic adaptation is enabled. Apply Original, "
						   "Light, Balanced, Quality, or Voice Focus with automatic adaptation off first.");
	}

	QVariantMap voiceMeterField(
		const Settings &settings,
		const Mumble::InputEnhancement::CalibrationEvaluationWorker::Snapshot &worker,
		const QString &uiError) {
		QVariantMap field = fieldItem(QStringLiteral("audio.inputMeter"), QObject::tr("Current voice input"),
									  QStringLiteral("voiceMeter"), 0);
		using Profile = Mumble::InputEnhancement::Profile;
		const AudioInputPtr input = currentAudioInput();
		// Calibrate the input that is actually running. Audio backends may fall back
		// to their default device when a previously saved device disappears; the
		// device selector mirrors that fallback by selecting its first choice.
		const Mumble::InputEnhancement::DefaultPreference &preference =
			input ? Mumble::InputEnhancement::preferenceForDevice(settings.inputEnhancement,
														  input->inputDeviceIdentity())
				  : currentInputEnhancementPreference(settings);
		bool calibrationAvailable = false;
		QString calibrationUnavailableReason;
		if (preference.profile == Profile::Auto
				   || Mumble::InputEnhancement::runtimeAutoAdaptationEnabled(preference)) {
			calibrationUnavailableReason = autoCalibrationUnavailableText();
		} else if (!input) {
			calibrationUnavailableReason = QObject::tr("Start the selected input device before calibrating enhancement.");
		} else if (!input->inputEnhancementHealthyForUpdate()) {
			calibrationUnavailableReason =
				QObject::tr("Restart or reapply a healthy input-enhancement profile before calibrating.");
		} else if (preference.profile != Profile::Original
				   && !input->healthyActiveInputEnhancementBinding(input->inputDeviceIdentity(), preference)) {
			calibrationUnavailableReason =
				QObject::tr("Apply the selected input-enhancement profile before calibrating it.");
		} else {
			calibrationAvailable = true;
		}
		field.insert(QStringLiteral("vadSource"), static_cast< int >(settings.vsVAD));
		field.insert(QStringLiteral("transmitMode"), static_cast< int >(settings.atTransmit));
		field.insert(QStringLiteral("active"), settings.atTransmit == Settings::VAD);
		field.insert(QStringLiteral("calibrationState"), QStringLiteral("idle"));
		field.insert(QStringLiteral("calibrationActionId"), QStringLiteral("finishAudioSetupWizard"));
		field.insert(QStringLiteral("calibrationLabel"), QObject::tr("Set up voice activation"));
		field.insert(QStringLiteral("calibrationTooltip"),
					 QObject::tr("Measure room sound and normal speech, then compare suitable detection methods."));
		field.insert(QStringLiteral("calibrationStatusText"),
					 QObject::tr("Run the guided check for this microphone. Manual tuning is available in Advanced."));
		field.insert(QStringLiteral("replayStartActionId"), QStringLiteral("startVoiceReplay"));
		field.insert(QStringLiteral("replayStopActionId"), QStringLiteral("stopVoiceReplay"));
		field.insert(QStringLiteral("replayLabel"), QObject::tr("Replay"));
		field.insert(QStringLiteral("replayTooltip"),
					 QObject::tr("Listen to your microphone through server loopback when connected, or local loopback when offline."));
		field.insert(QStringLiteral("sourceLabel"), vadSourceLabel(settings.vsVAD));
		field.insert(QStringLiteral("silenceThreshold"), vadThresholdFromFloat(settings.fVADmin));
		field.insert(QStringLiteral("speechThreshold"), vadThresholdFromFloat(settings.fVADmax));
		field.insert(QStringLiteral("voiceHold"), settings.iVoiceHold);
		field.insert(QStringLiteral("loopbackMode"), static_cast< int >(settings.lmLoopMode));
		field.insert(QStringLiteral("inputGateMode"), static_cast< int >(settings.inputGateMode));
		field.insert(QStringLiteral("recommendedVadSource"), static_cast< int >(Settings::SignalToNoise));
		field.insert(QStringLiteral("recommendedInputGateMode"), static_cast< int >(Settings::InputGateOff));
		field.insert(QStringLiteral("neuralCleanupAvailable"), hasAvailableSpeechCleanupBackend());
		field.insert(QStringLiteral("inputEnhancementCalibrationAvailable"), calibrationAvailable);
		field.insert(QStringLiteral("inputEnhancementCalibrationUnavailableReason"),
					 calibrationUnavailableReason);
		field.insert(QStringLiteral("inputEnhancementCalibrationWorkerState"),
					 calibrationWorkerStateName(worker.state));
		field.insert(QStringLiteral("inputEnhancementCalibrationProgress"), worker.progressPercent);
		field.insert(QStringLiteral("inputEnhancementCalibrationErrorText"),
					 uiError.isEmpty() ? worker.error : uiError);
		if (input) {
			field.insert(QStringLiteral("inputEnhancementCalibrationStartActionId"),
						 QStringLiteral("startInputEnhancementCalibration"));
			field.insert(QStringLiteral("inputEnhancementProbationRunning"),
						 input->inputEnhancementProbationRunning());
			field.insert(QStringLiteral("inputEnhancementProbationUndoAvailable"),
						 input->inputEnhancementProbationUndoAvailable());
			field.insert(QStringLiteral("inputEnhancementProbationUndoActionId"),
						 QStringLiteral("undoInputEnhancementRollback"));
			if (Mumble::InputEnhancement::CalibrationRuntimeBridge *runtime =
					input->inputEnhancementCalibrationRuntime()) {
				const Mumble::InputEnhancement::CalibrationSession::State state = runtime->state();
				field.insert(QStringLiteral("inputEnhancementCalibrationState"), static_cast< int >(state));
				field.insert(QStringLiteral("inputEnhancementCalibrationTransmissionBlocked"),
							 runtime->transmissionBlocked());
				field.insert(QStringLiteral("inputEnhancementCalibrationAdvanceActionId"),
							 QStringLiteral("advanceInputEnhancementCalibration"));
				field.insert(QStringLiteral("inputEnhancementCalibrationCancelActionId"),
							 QStringLiteral("cancelInputEnhancementCalibration"));
				field.insert(QStringLiteral("inputEnhancementCalibrationSkipNoiseActionId"),
							 QStringLiteral("skipInputEnhancementCalibrationNoise"));
				field.insert(QStringLiteral("inputEnhancementCalibrationEvaluateActionId"),
							 QStringLiteral("evaluateInputEnhancementCalibration"));
				field.insert(QStringLiteral("inputEnhancementCalibrationRefreshActionId"),
							 QStringLiteral("refreshInputEnhancementCalibration"));
				field.insert(QStringLiteral("inputEnhancementCalibrationSelectActionId"),
							 QStringLiteral("selectInputEnhancementCalibration"));
				field.insert(QStringLiteral("inputEnhancementCalibrationApplyActionId"),
							 QStringLiteral("applyInputEnhancementCalibration"));
				if (state == Mumble::InputEnhancement::CalibrationSession::State::LevelCheck
					|| state == Mumble::InputEnhancement::CalibrationSession::State::LevelCheckReady) {
					const Mumble::InputEnhancement::CalibrationSession::LevelMetrics metrics =
						runtime->levelMetrics();
					field.insert(QStringLiteral("inputEnhancementCalibrationLevelStatus"),
							 static_cast< int >(metrics.status));
					field.insert(QStringLiteral("inputEnhancementCalibrationLevelPeakPercent"),
							 qBound(0, static_cast< int >(std::lround(metrics.peak * 100.0f)), 100));
					field.insert(QStringLiteral("inputEnhancementCalibrationLevelRmsPercent"),
							 qBound(0, static_cast< int >(std::lround(metrics.rms * 100.0f)), 100));
				}
				if (state == Mumble::InputEnhancement::CalibrationSession::State::BlindComparison
					|| state == Mumble::InputEnhancement::CalibrationSession::State::DraftReady) {
					const Mumble::InputEnhancement::CalibrationSession::BlindComparison comparison =
						runtime->blindComparison();
					QVariantList playbackOptions;
					for (std::size_t index = 0; index < comparison.count; ++index) {
						QVariantMap option;
						option.insert(QStringLiteral("label"),
									  QString(QChar(QLatin1Char('A').unicode() + static_cast< ushort >(index))));
						option.insert(QStringLiteral("playbackToken"),
									  QString::number(comparison.playbackTokens[index]));
						playbackOptions.push_back(option);
					}
					field.insert(QStringLiteral("inputEnhancementCalibrationPlaybackOptions"), playbackOptions);
					if (comparison.count >= 2) {
						field.insert(QStringLiteral("inputEnhancementCalibrationLeftPlaybackToken"),
									 QString::number(comparison.playbackTokens[0]));
						field.insert(QStringLiteral("inputEnhancementCalibrationRightPlaybackToken"),
									 QString::number(comparison.playbackTokens[1]));
					}
					if (state == Mumble::InputEnhancement::CalibrationSession::State::DraftReady) {
						if (const Mumble::InputEnhancement::DefaultPreference *draftPreference =
								runtime->draftPreference()) {
							field.insert(QStringLiteral("inputEnhancementCalibrationSelectedProfile"),
										 inputEnhancementProfileLabel(draftPreference->profile));
						}
					}
				}
			} else {
				field.insert(QStringLiteral("inputEnhancementCalibrationState"),
							 static_cast< int >(Mumble::InputEnhancement::CalibrationSession::State::Idle));
				field.insert(QStringLiteral("inputEnhancementCalibrationTransmissionBlocked"), false);
			}
		}
		return field;
	}

	QVariantMap inputEnhancementCalibrationField(
		const Settings &settings,
		const Mumble::InputEnhancement::CalibrationEvaluationWorker::Snapshot &worker,
		const QString &uiError) {
		QVariantMap field = voiceMeterField(settings, worker, uiError);
		field.insert(QStringLiteral("id"), QStringLiteral("audio.inputEnhancementCalibration"));
		field.insert(QStringLiteral("label"), QObject::tr("Hear what others hear"));
		field.insert(QStringLiteral("type"), QStringLiteral("inputEnhancementCalibration"));
		field.insert(QStringLiteral("tooltip"),
					 QObject::tr("Record one guided sample, hear every safe processing profile after Opus, and choose your sound."));
		return field;
	}

	int amplificationFromMinLoudness(const int minLoudness) {
		return qBound(0, kAmplificationSliderBase - minLoudness, kMaxAmplificationSliderValue);
	}

	int minLoudnessFromAmplification(const QVariant &value) {
		return kAmplificationSliderBase - qBound(0, value.toInt(), kMaxAmplificationSliderValue);
	}

	int outputDelaySliderFromSettings(const int value) {
		return qBound(0, value, 100);
	}

	bool hasAvailableSpeechCleanupBackend() {
		for (Settings::SpeechCleanupBackend backend : Mumble::SpeechCleanup::supportedBackends) {
			if (Mumble::SpeechCleanup::isBackendAvailable(backend)) {
				return true;
			}
		}
		return false;
	}

	Settings::SpeechCleanupBackend normalizedBackendFromValue(const QVariant &value) {
		const auto backend = static_cast< Settings::SpeechCleanupBackend >(value.toInt());
		return Mumble::SpeechCleanup::isBackendAvailable(backend) ? backend : Mumble::SpeechCleanup::fallbackBackend();
	}

	const Mumble::ModernTheme::ThemeDefinition *modernShellCustomTheme(
		const QList< Mumble::ModernTheme::ThemeDefinition > &catalog, const QString &themeID) {
		const QString normalized = themeID.trimmed().toLower();
		for (const Mumble::ModernTheme::ThemeDefinition &theme : catalog) {
			if (theme.id == normalized) {
				return &theme;
			}
		}
		return nullptr;
	}

	QString normalizedModernShellTheme(
		const QVariant &value, const QList< Mumble::ModernTheme::ThemeDefinition > &catalog) {
		const QString normalized = value.toString().trimmed().toLower();
		if (Mumble::ModernTheme::isBuiltInThemeId(normalized)
			|| modernShellCustomTheme(catalog, normalized)) {
			return normalized;
		}
		return QStringLiteral("dark");
	}

	QString normalizedModernShellDensity(const QVariant &value) {
		const QString normalized = value.toString().trimmed().toLower();
		static const QSet< QString > allowed { QStringLiteral("compact"), QStringLiteral("comfortable"),
											   QStringLiteral("spacious") };
		return allowed.contains(normalized) ? normalized : QStringLiteral("comfortable");
	}

	QString normalizedModernShellRailSide(const QVariant &value) {
		const QString normalized = value.toString().trimmed().toLower();
		return normalized == QLatin1String("left") || normalized == QLatin1String("right")
				   ? normalized
				   : QStringLiteral("right");
	}

	QString normalizedModernShellAccent(const QVariant &value) {
		return Mumble::ModernTheme::normalizedAccentId(value.toString());
	}

	QString normalizedModernShellCustomAccent(const QVariant &value) {
		return Mumble::ModernTheme::normalizedCustomAccentColor(value.toString());
	}

	int normalizedModernShellCustomAccentStrength(const int value) {
		return Mumble::ModernTheme::normalizedCustomAccentStrength(value);
	}

	bool isModernAppearancePreviewField(const QString &fieldID) {
		static const QSet< QString > previewFields {
			QStringLiteral("look.modernTheme"),
			QStringLiteral("look.modernDensity"),
			QStringLiteral("look.modernAccent"),
			QStringLiteral("look.modernCustomAccent"),
			QStringLiteral("look.modernCustomAccentStrength")
		};
		return previewFields.contains(fieldID);
	}

	ModernSettingsController::AppearancePreview modernAppearancePreview(
		const Settings &settings, const QList< Mumble::ModernTheme::ThemeDefinition > &catalog) {
		ModernSettingsController::AppearancePreview preview;
		preview.theme = normalizedModernShellTheme(settings.qsModernShellTheme, catalog);
		preview.density = normalizedModernShellDensity(settings.qsModernShellDensity);
		preview.accent = normalizedModernShellAccent(settings.qsModernShellAccent);
		preview.customAccent = normalizedModernShellCustomAccent(settings.qsModernShellCustomAccent);
		preview.customAccentStrength =
			normalizedModernShellCustomAccentStrength(settings.iModernShellCustomAccentStrength);
		return preview;
	}

	QString previewColor(const QColor &color) {
		return color.alpha() < 255 ? color.name(QColor::HexArgb) : color.name(QColor::HexRgb);
	}

	QVariantMap modernShellThemePreview(const UiThemeTokens &tokens, const QColor &shellBackground = {}) {
		return QVariantMap {
			{ QStringLiteral("shell"), previewColor(shellBackground.isValid() ? shellBackground : tokens.base) },
			{ QStringLiteral("rail"), previewColor(tokens.mantle) },
			{ QStringLiteral("strip"), previewColor(tokens.crust) },
			{ QStringLiteral("panel"), previewColor(tokens.base) },
			{ QStringLiteral("surface"), previewColor(tokens.surface0) },
			{ QStringLiteral("surfaceHover"), previewColor(tokens.surface1) },
			{ QStringLiteral("border"), previewColor(tokens.surface2) },
			{ QStringLiteral("text"), previewColor(tokens.text) },
			{ QStringLiteral("textMuted"), previewColor(tokens.overlay0) },
			{ QStringLiteral("accent"), previewColor(tokens.accent) },
			{ QStringLiteral("accentHover"), previewColor(tokens.accentHover) },
			{ QStringLiteral("accentSubtle"), previewColor(tokens.accentSubtle) },
			{ QStringLiteral("danger"), previewColor(tokens.red) },
			{ QStringLiteral("success"), previewColor(tokens.green) },
			{ QStringLiteral("warning"), previewColor(tokens.yellow) }
		};
	}

	QVariantMap modernShellThemePreview(const Mumble::ModernTheme::ThemeDefinition &theme) {
		const Mumble::ModernTheme::ThemePalette &palette = theme.palette;
		if (!palette.base.isValid() || !palette.text.isValid() || !palette.accent.isValid()) {
			// Legacy CSS themes only carry parsed custom-property tokens. Build the
			// most faithful preview available from those colors and leave missing
			// roles unset so QML can use its safe product-token fallback.
			QVariantMap preview;
			const auto insertToken = [&preview, &theme](const QString &role, const QStringList &tokenNames) {
				for (const QString &tokenName : tokenNames) {
					const QColor color(theme.tokens.value(tokenName).toString());
					if (color.isValid()) {
						preview.insert(role, previewColor(color));
						return;
					}
				}
			};
			insertToken(QStringLiteral("shell"),
						{ QStringLiteral("--shell-bg"), QStringLiteral("--shell-panel"),
						  QStringLiteral("--shell-rail") });
			insertToken(QStringLiteral("rail"), { QStringLiteral("--shell-rail") });
			insertToken(QStringLiteral("strip"), { QStringLiteral("--shell-strip") });
			insertToken(QStringLiteral("panel"), { QStringLiteral("--shell-panel") });
			insertToken(QStringLiteral("surface"), { QStringLiteral("--shell-panel-soft") });
			insertToken(QStringLiteral("surfaceHover"), { QStringLiteral("--shell-highlight") });
			insertToken(QStringLiteral("border"), { QStringLiteral("--surface-border") });
			insertToken(QStringLiteral("text"),
						{ QStringLiteral("--text-strong"), QStringLiteral("--text-main") });
			insertToken(QStringLiteral("textMuted"), { QStringLiteral("--text-muted") });
			insertToken(QStringLiteral("accent"), { QStringLiteral("--accent") });
			insertToken(QStringLiteral("accentHover"), { QStringLiteral("--accent-strong") });
			insertToken(QStringLiteral("accentSubtle"), { QStringLiteral("--accent-soft") });
			insertToken(QStringLiteral("danger"), { QStringLiteral("--danger") });
			insertToken(QStringLiteral("success"), { QStringLiteral("--success") });
			insertToken(QStringLiteral("warning"), { QStringLiteral("--warning") });
			return preview;
		}
		return QVariantMap {
			{ QStringLiteral("shell"), previewColor(palette.shellBackground) },
			{ QStringLiteral("rail"), previewColor(palette.mantle) },
			{ QStringLiteral("strip"), previewColor(palette.crust) },
			{ QStringLiteral("panel"), previewColor(palette.base) },
			{ QStringLiteral("surface"), previewColor(palette.surface0) },
			{ QStringLiteral("surfaceHover"), previewColor(palette.surface1) },
			{ QStringLiteral("border"), previewColor(palette.surface2) },
			{ QStringLiteral("text"), previewColor(palette.text) },
			{ QStringLiteral("textMuted"), previewColor(palette.overlay0) },
			{ QStringLiteral("accent"), previewColor(palette.accent) },
			{ QStringLiteral("accentHover"), previewColor(palette.accentHover) },
			{ QStringLiteral("accentSubtle"), previewColor(palette.accentSubtle) },
			{ QStringLiteral("danger"), previewColor(palette.red) },
			{ QStringLiteral("success"), previewColor(palette.green) },
			{ QStringLiteral("warning"), previewColor(palette.yellow) }
		};
	}

	QString modernShellBuiltInThemeLabel(const QString &themeID) {
		if (themeID == QLatin1String("dark")) return QObject::tr("Dark");
		if (themeID == QLatin1String("light")) return QObject::tr("Light");
		if (themeID == QLatin1String("mocha")) return QObject::tr("Mocha");
		if (themeID == QLatin1String("macchiato")) return QObject::tr("Macchiato");
		if (themeID == QLatin1String("frappe")) return QObject::tr("Frappe");
		if (themeID == QLatin1String("latte")) return QObject::tr("Latte");
		if (themeID == QLatin1String("nord")) return QObject::tr("Nord");
		if (themeID == QLatin1String("gruvbox")) return QObject::tr("Gruvbox");
		return themeID;
	}

	QVariantList modernShellThemeOptions(
		const QList< Mumble::ModernTheme::ThemeDefinition > &customThemes) {
		QVariantList options;
		for (const QString &themeID : Mumble::ModernTheme::builtInThemeIds()) {
			QVariantMap option = optionItem(themeID, modernShellBuiltInThemeLabel(themeID));
			option.insert(QStringLiteral("source"), QStringLiteral("builtIn"));
			option.insert(QStringLiteral("preview"), modernShellThemePreview(uiThemeTokensForThemeId(themeID)));
			options.push_back(option);
		}

		for (const Mumble::ModernTheme::ThemeDefinition &theme : customThemes) {
			QVariantMap option = optionItem(theme.id, theme.name, true, theme.sourcePath);
			option.insert(QStringLiteral("source"), QStringLiteral("custom"));
			option.insert(QStringLiteral("appearance"), theme.appearance);
			const QVariantMap preview = modernShellThemePreview(theme);
			if (!preview.isEmpty()) {
				option.insert(QStringLiteral("preview"), preview);
			}
			const QVariantMap swatch = Mumble::ModernTheme::themeSwatch(theme.tokens);
			if (!swatch.isEmpty()) {
				option.insert(QStringLiteral("swatch"), swatch);
			}
			options.push_back(option);
		}

		return options;
	}

	QVariantList modernShellDensityOptions() {
		return QVariantList { optionItem(QStringLiteral("compact"), QObject::tr("Compact")),
							  optionItem(QStringLiteral("comfortable"), QObject::tr("Comfortable")),
							  optionItem(QStringLiteral("spacious"), QObject::tr("Spacious")) };
	}

	QVariantList modernShellRailSideOptions() {
		return QVariantList { optionItem(QStringLiteral("left"), QObject::tr("Left")),
							  optionItem(QStringLiteral("right"), QObject::tr("Right")) };
	}

	QVariantMap modernShellCustomAccentSwatch(const Settings &settings) {
		QVariantMap swatch;
		swatch.insert(QStringLiteral("bg"), QStringLiteral("#2b3340"));
		swatch.insert(QStringLiteral("accent"),
					  normalizedModernShellCustomAccent(settings.qsModernShellCustomAccent));
		return swatch;
	}

	QVariantMap modernShellAccentOption(const QString &id, const QString &label, const QColor &color,
									  const QString &hint = {}) {
		QVariantMap option = optionItem(id, label, true, hint);
		QVariantMap swatch;
		swatch.insert(QStringLiteral("accent"), previewColor(color));
		option.insert(QStringLiteral("swatch"), swatch);
		return option;
	}

	QVariantList modernShellAccentOptions(
		const Settings &settings, const QList< Mumble::ModernTheme::ThemeDefinition > &customThemes) {
		const QString themeID = normalizedModernShellTheme(settings.qsModernShellTheme, customThemes);
		QColor automaticAccent = uiThemeTokensForThemeId(themeID).accent;
		if (const Mumble::ModernTheme::ThemeDefinition *customTheme =
				modernShellCustomTheme(customThemes, themeID)) {
			QColor customThemeAccent = customTheme->palette.accent;
			if (!customThemeAccent.isValid()) {
				customThemeAccent = QColor(
					Mumble::ModernTheme::themeSwatch(customTheme->tokens).value(QStringLiteral("accent")).toString());
			}
			if (customThemeAccent.isValid()) {
				automaticAccent = customThemeAccent;
			}
		}
		QVariantMap automaticOption = modernShellAccentOption(
			QStringLiteral("auto"), QObject::tr("Auto"), automaticAccent,
			QObject::tr("Uses the accent supplied by the selected theme."));
		automaticOption.insert(QStringLiteral("automatic"), true);

		QVariantMap customOption = modernShellAccentOption(
			Mumble::ModernTheme::customAccentId(), QObject::tr("Custom"),
			QColor(normalizedModernShellCustomAccent(settings.qsModernShellCustomAccent)));
		customOption.insert(QStringLiteral("swatch"), modernShellCustomAccentSwatch(settings));
		return QVariantList { automaticOption,
							  modernShellAccentOption(QStringLiteral("teal"), QObject::tr("Teal"),
								  Mumble::ModernTheme::accentColorOverride(QStringLiteral("teal"))),
							  modernShellAccentOption(QStringLiteral("blue"), QObject::tr("Blue"),
								  Mumble::ModernTheme::accentColorOverride(QStringLiteral("blue"))),
							  modernShellAccentOption(QStringLiteral("violet"), QObject::tr("Violet"),
								  Mumble::ModernTheme::accentColorOverride(QStringLiteral("violet"))),
							  modernShellAccentOption(QStringLiteral("amber"), QObject::tr("Amber"),
								  Mumble::ModernTheme::accentColorOverride(QStringLiteral("amber"))),
							  modernShellAccentOption(QStringLiteral("rose"), QObject::tr("Rose"),
								  Mumble::ModernTheme::accentColorOverride(QStringLiteral("rose"))),
							  customOption };
	}

	QVariantMap modernShellAccentDto(const Settings &settings) {
		const QString normalized = normalizedModernShellAccent(settings.qsModernShellAccent);
		QVariantMap dto;
		dto.insert(QStringLiteral("id"), normalized);
		if (normalized == Mumble::ModernTheme::customAccentId()) {
			dto.insert(QStringLiteral("color"), normalizedModernShellCustomAccent(settings.qsModernShellCustomAccent));
			dto.insert(QStringLiteral("strength"),
					   normalizedModernShellCustomAccentStrength(settings.iModernShellCustomAccentStrength));
		}
		return dto;
	}

	QVariantMap mergedThemeTokens(const QVariantMap &themeTokens, const QVariantMap &accentTokens) {
		QVariantMap merged = themeTokens;
		for (auto it = accentTokens.cbegin(); it != accentTokens.cend(); ++it) {
			merged.insert(it.key(), it.value());
		}
		return merged;
	}

	QVariantMap modernShellUiTweaksDto(
		const Settings &settings, const QList< Mumble::ModernTheme::ThemeDefinition > &customThemes) {
		const QString accent = normalizedModernShellAccent(settings.qsModernShellAccent);
		const QString theme  = normalizedModernShellTheme(settings.qsModernShellTheme, customThemes);
		const Mumble::ModernTheme::ThemeDefinition *customTheme = modernShellCustomTheme(customThemes, theme);
		const QVariantMap customThemeTokens = customTheme ? customTheme->tokens : QVariantMap();
		const QVariantMap customAccentTokens =
			accent == Mumble::ModernTheme::customAccentId()
				? Mumble::ModernTheme::customAccentTokens(
					  settings.qsModernShellCustomAccent, settings.iModernShellCustomAccentStrength)
				: QVariantMap();
		const QVariantMap themeTokens = mergedThemeTokens(customThemeTokens, customAccentTokens);
		QVariantMap dto;
		if (!customThemeTokens.isEmpty()) {
			dto.insert(QStringLiteral("theme"), Mumble::ModernTheme::engineThemeId());
			dto.insert(QStringLiteral("themeSource"), QStringLiteral("customTheme"));
			dto.insert(QStringLiteral("themeId"), theme);
		} else {
			dto.insert(QStringLiteral("theme"), theme);
			dto.insert(QStringLiteral("themeSource"), QStringLiteral("modernShell"));
		}
		if (!themeTokens.isEmpty()) {
			dto.insert(QStringLiteral("themeTokens"), themeTokens);
		}
		dto.insert(QStringLiteral("density"), normalizedModernShellDensity(settings.qsModernShellDensity));
		dto.insert(QStringLiteral("userIcons"),
				   settings.bModernShellClassicUserIcons ? QStringLiteral("classic") : QStringLiteral("avatars"));
		dto.insert(QStringLiteral("classicUserIcons"), settings.bModernShellClassicUserIcons);
		dto.insert(QStringLiteral("railSide"), normalizedModernShellRailSide(settings.qsModernShellRailSide));
		dto.insert(QStringLiteral("accent"), accent);
		dto.insert(QStringLiteral("accentDetails"), modernShellAccentDto(settings));
		dto.insert(QStringLiteral("stonksProfileShortcutVisible"),
				   settings.bModernShellStonksProfileShortcutVisible);
		dto.insert(QStringLiteral("tickerBannerEnabled"), settings.bModernShellTickerBannerEnabled);
		dto.insert(QStringLiteral("tickerPlacement"), settings.qsModernShellTickerPlacement);
		dto.insert(QStringLiteral("tickerDirection"), settings.qsModernShellTickerDirection);
		dto.insert(QStringLiteral("tickerSpeed"), settings.qsModernShellTickerSpeed);
		return dto;
	}

	QString buildArchitectureLabel() {
#ifdef MUMBLE_TARGET_ARCH
		return QString::fromUtf8(MUMBLE_TARGET_ARCH);
#else
		return QObject::tr("Unknown");
#endif
	}

	QString shortcutActionLabel(const Shortcut &shortcut) {
		if (GlobalShortcutEngine::engine) {
			if (GlobalShortcut *registered = GlobalShortcutEngine::engine->qmShortcuts.value(shortcut.iIndex, nullptr)) {
				return registered->name;
			}
		}

		if (shortcut.iIndex < 0) {
			return QObject::tr("New shortcut");
		}
		return QObject::tr("Unknown shortcut %1").arg(shortcut.iIndex);
	}

	QString shortcutButtonsLabel(const Shortcut &shortcut) {
		if (shortcut.qlButtons.isEmpty()) {
			return QObject::tr("Not assigned");
		}

		if (!GlobalShortcutEngine::engine) {
			return QObject::tr("%n input(s)", nullptr, static_cast< int >(shortcut.qlButtons.size()));
		}

		QStringList labels;
		for (const QVariant &button : shortcut.qlButtons) {
			const GlobalShortcutEngine::ButtonInfo info = GlobalShortcutEngine::engine->buttonInfo(button);
			const QString device                       = info.device.trimmed();
			const QString name                         = info.name.trimmed();
			if (!device.isEmpty() && device != name) {
				labels << QObject::tr("%1: %2").arg(device, name.isEmpty() ? QObject::tr("Unknown") : name);
			} else {
				labels << (name.isEmpty() ? QObject::tr("Unknown") : name);
			}
		}
		return labels.join(QStringLiteral(" + "));
	}

	const GlobalShortcut *shortcutDefinition(const int index) {
		if (!GlobalShortcutEngine::engine) {
			return nullptr;
		}
		return GlobalShortcutEngine::engine->qmShortcuts.value(index, nullptr);
	}

	QVariant shortcutEffectiveData(const Shortcut &shortcut) {
		const GlobalShortcut *definition = shortcutDefinition(shortcut.iIndex);
		if (shortcut.qvData.isValid() && definition && shortcut.qvData.metaType() == definition->qvDefault.metaType()) {
			return shortcut.qvData;
		}
		return definition ? definition->qvDefault : shortcut.qvData;
	}

	QString shortcutTargetMode(const ShortcutTarget &target) {
		if (target.bCurrentSelection) {
			return QStringLiteral("selection");
		}
		if (target.bUsers) {
			return QStringLiteral("users");
		}
		if (target.iChannel == SHORTCUT_TARGET_ROOT) {
			return QStringLiteral("root");
		}
		if (target.iChannel == SHORTCUT_TARGET_PARENT) {
			return QStringLiteral("parent");
		}
		if (target.iChannel == SHORTCUT_TARGET_CURRENT) {
			return QStringLiteral("current");
		}
		if (target.iChannel >= 0) {
			return QStringLiteral("channel:%1").arg(target.iChannel);
		}
		return QStringLiteral("current");
	}

	ShortcutTarget shortcutTargetFromMode(const QString &mode) {
		ShortcutTarget target;
		target.bUsers            = false;
		target.bCurrentSelection = false;

		if (mode == QLatin1String("selection")) {
			target.bCurrentSelection = true;
			return target;
		}
		if (mode == QLatin1String("users")) {
			target.bUsers = true;
			return target;
		}
		if (mode == QLatin1String("root")) {
			target.iChannel = SHORTCUT_TARGET_ROOT;
			return target;
		}
		if (mode == QLatin1String("parent")) {
			target.iChannel = SHORTCUT_TARGET_PARENT;
			return target;
		}
		if (mode.startsWith(QLatin1String("channel:"))) {
			bool ok = false;
			const int channelID = mode.mid(QStringLiteral("channel:").size()).toInt(&ok);
			if (ok && channelID >= 0) {
				target.iChannel = channelID;
				return target;
			}
		}

		target.iChannel = SHORTCUT_TARGET_CURRENT;
		return target;
	}

	QString shortcutTargetKind(const ShortcutTarget &target) {
		if (target.bCurrentSelection) {
			return QStringLiteral("selection");
		}
		if (target.bUsers) {
			return QStringLiteral("users");
		}
		return QStringLiteral("channel");
	}

	QVariantList shortcutTargetModeOptions() {
		return QVariantList { optionItem(QStringLiteral("selection"), QObject::tr("Current selection")),
							  optionItem(QStringLiteral("users"), QObject::tr("List of users")),
							  optionItem(QStringLiteral("channel"), QObject::tr("Channel")) };
	}

	QString shortcutTargetChannelLabel(const int channelID) {
		switch (channelID) {
			case SHORTCUT_TARGET_ROOT:
				return QObject::tr("Root");
			case SHORTCUT_TARGET_PARENT:
				return QObject::tr("Parent");
			case SHORTCUT_TARGET_CURRENT:
				return QObject::tr("Current");
			default:
				if (channelID <= SHORTCUT_TARGET_PARENT_SUBCHANNEL && channelID > SHORTCUT_TARGET_PARENT_SUBCHANNEL - 8) {
					return QObject::tr("Parent / Subchannel #%1").arg(SHORTCUT_TARGET_PARENT_SUBCHANNEL + 1 - channelID);
				}
				if (channelID <= SHORTCUT_TARGET_SUBCHANNEL && channelID > SHORTCUT_TARGET_SUBCHANNEL - 8) {
					return QObject::tr("Current / Subchannel #%1").arg(SHORTCUT_TARGET_CURRENT - channelID);
				}
				break;
		}

		if (channelID >= 0) {
			if (const Channel *channel = Channel::get(static_cast< unsigned int >(channelID))) {
				const QString path = channel->getPath().trimmed();
				return path.isEmpty() ? channel->qsName : path;
			}
		}

		return QObject::tr("Current");
	}

	QVariantList shortcutWhisperChannelOptions() {
		QVariantList options {
			optionItem(SHORTCUT_TARGET_CURRENT, QObject::tr("Current")),
			optionItem(SHORTCUT_TARGET_ROOT, QObject::tr("Root")),
			optionItem(SHORTCUT_TARGET_PARENT, QObject::tr("Parent"))
		};

		for (int i = 0; i < 8; ++i) {
			options.push_back(optionItem(SHORTCUT_TARGET_SUBCHANNEL - i, QObject::tr("Current / Subchannel #%1").arg(i + 1)));
		}
		for (int i = 0; i < 8; ++i) {
			options.push_back(optionItem(SHORTCUT_TARGET_PARENT_SUBCHANNEL - i,
										 QObject::tr("Parent / Subchannel #%1").arg(i + 1)));
		}

		QReadLocker lock(&Channel::c_qrwlChannels);
		QList< Channel * > channels = Channel::c_qhChannels.values();
		std::sort(channels.begin(), channels.end(), [](const Channel *lhs, const Channel *rhs) {
			const QString left  = lhs ? lhs->getPath().toCaseFolded() : QString();
			const QString right = rhs ? rhs->getPath().toCaseFolded() : QString();
			return left < right;
		});
		for (const Channel *channel : channels) {
			if (!channel) {
				continue;
			}
			const QString path = channel->getPath().trimmed();
			options.push_back(optionItem(static_cast< int >(channel->iId), path.isEmpty() ? channel->qsName : path));
		}
		return options;
	}

	QMap< QString, QString > shortcutTargetKnownUsers() {
		QMap< QString, QString > namesByHash;
		const Global *global = Global::g_global_struct;
		if (!global) {
			return namesByHash;
		}

		if (global->db) {
			const QMap< QString, QString > friends = global->db->getFriends();
			for (auto it = friends.constBegin(); it != friends.constEnd(); ++it) {
				const QString name = it.key().trimmed();
				const QString hash = it.value().trimmed();
				if (!hash.isEmpty()) {
					namesByHash.insert(hash, name.isEmpty() ? QString::fromLatin1("#%1").arg(hash) : name);
				}
			}
		}

		if (global->uiSession) {
			QReadLocker lock(&ClientUser::c_qrwlUsers);
			for (const ClientUser *user : ClientUser::c_qmUsers) {
				if (!user || user->uiSession == global->uiSession || user->qsHash.isEmpty()) {
					continue;
				}
				const QString displayName =
					user->qsFriendName.trimmed().isEmpty() ? user->qsName.trimmed() : user->qsFriendName.trimmed();
				namesByHash.insert(user->qsHash, displayName.isEmpty() ? QString::fromLatin1("#%1").arg(user->qsHash)
																	  : displayName);
			}
		}

		return namesByHash;
	}

	QVariantList shortcutTargetUserOptions() {
		QVariantList options;
		const QMap< QString, QString > namesByHash = shortcutTargetKnownUsers();
		QList< QPair< QString, QString > > users;
		for (auto it = namesByHash.constBegin(); it != namesByHash.constEnd(); ++it) {
			users.push_back(qMakePair(it.value(), it.key()));
		}
		std::sort(users.begin(), users.end(), [](const QPair< QString, QString > &lhs,
												 const QPair< QString, QString > &rhs) {
			const int nameCompare = QString::localeAwareCompare(lhs.first, rhs.first);
			if (nameCompare == 0) {
				return lhs.second < rhs.second;
			}
			return nameCompare < 0;
		});
		for (const QPair< QString, QString > &user : users) {
			options.push_back(optionItem(user.second, user.first));
		}
		return options;
	}

	QVariantList shortcutTargetSelectedUsers(const ShortcutTarget &target) {
		QVariantList users;
		const QMap< QString, QString > namesByHash = shortcutTargetKnownUsers();
		for (const QString &hash : target.qlUsers) {
			if (hash.trimmed().isEmpty()) {
				continue;
			}
			users.push_back(optionItem(hash, namesByHash.value(hash, QString::fromLatin1("#%1").arg(hash))));
		}
		return users;
	}

	QVariantMap shortcutTargetDetail(const ShortcutTarget &target) {
		QVariantMap detail;
		detail.insert(QStringLiteral("mode"), shortcutTargetKind(target));
		detail.insert(QStringLiteral("channelId"), target.iChannel);
		detail.insert(QStringLiteral("channelLabel"), shortcutTargetChannelLabel(target.iChannel));
		detail.insert(QStringLiteral("group"), target.qsGroup);
		detail.insert(QStringLiteral("links"), target.bLinks);
		detail.insert(QStringLiteral("children"), target.bChildren);
		detail.insert(QStringLiteral("forceCenter"), target.bForceCenter);
		detail.insert(QStringLiteral("users"), shortcutTargetSelectedUsers(target));
		detail.insert(QStringLiteral("summary"), ShortcutTargetWidget::targetString(target));
		return detail;
	}

	void applyShortcutTargetMode(ShortcutTarget &target, const QString &mode) {
		if (mode == QLatin1String("selection")) {
			target.bCurrentSelection = true;
			target.bUsers            = false;
			return;
		}
		if (mode == QLatin1String("users")) {
			target.bCurrentSelection = false;
			target.bUsers            = true;
			return;
		}
		if (mode == QLatin1String("channel")) {
			target.bCurrentSelection = false;
			target.bUsers            = false;
			if (target.iChannel < SHORTCUT_TARGET_PARENT_SUBCHANNEL - 7) {
				target.iChannel = SHORTCUT_TARGET_CURRENT;
			}
		}
	}

	QString shortcutDataType(const QVariant &data) {
		if (!data.isValid()) {
			return QStringLiteral("none");
		}
		if (data.userType() == QVariant::fromValue(ShortcutTarget()).userType()) {
			return QStringLiteral("target");
		}
		if (data.userType() == QVariant::fromValue(ChannelTarget()).userType()) {
			return QStringLiteral("channel");
		}
		switch (data.typeId()) {
			case QMetaType::Int:
				return QStringLiteral("toggle");
			case QMetaType::QString:
				return QStringLiteral("text");
			default:
				return QStringLiteral("readonly");
		}
	}

	QString shortcutDataLabel(const QVariant &data) {
		if (!data.isValid()) {
			return QObject::tr("No data");
		}
		if (data.userType() == QVariant::fromValue(ShortcutTarget()).userType()) {
			return ShortcutTargetWidget::targetString(data.value< ShortcutTarget >());
		}
		if (data.userType() == QVariant::fromValue(ChannelTarget()).userType()) {
			const ChannelTarget target = data.value< ChannelTarget >();
			const Channel *channel     = Channel::get(target.channelID);
			return channel ? channel->qsName : QObject::tr("< Unknown Channel >");
		}
		switch (data.typeId()) {
			case QMetaType::Int: {
				const int value = data.toInt();
				if (value > 0) {
					return QObject::tr("On");
				}
				if (value < 0) {
					return QObject::tr("Off");
				}
				return QObject::tr("Toggle");
			}
			case QMetaType::QString:
				return data.toString().isEmpty() ? QObject::tr("Empty") : data.toString();
			default:
				return data.toString();
		}
	}

	QVariantList shortcutActionOptions() {
		QVariantList options;
		options.push_back(optionItem(-1, QObject::tr("Unassigned")));

		if (!GlobalShortcutEngine::engine) {
			return options;
		}

		QList< GlobalShortcut * > definitions = GlobalShortcutEngine::engine->qmShortcuts.values();
		std::sort(definitions.begin(), definitions.end(), [](const GlobalShortcut *lhs, const GlobalShortcut *rhs) {
			const QString left  = lhs ? lhs->name.toCaseFolded() : QString();
			const QString right = rhs ? rhs->name.toCaseFolded() : QString();
			if (left == right) {
				return (lhs ? lhs->idx : 0) < (rhs ? rhs->idx : 0);
			}
			return left < right;
		});

		for (const GlobalShortcut *definition : definitions) {
			if (!definition) {
				continue;
			}
			const QString hint =
				!definition->qsToolTip.trimmed().isEmpty() ? definition->qsToolTip.trimmed()
														   : definition->qsWhatsThis.trimmed();
			options.push_back(optionItem(definition->idx, definition->name, true, hint));
		}
		return options;
	}

	QVariantList shortcutTargetOptions() {
		QVariantList options {
			optionItem(QStringLiteral("current"), QObject::tr("Current channel")),
			optionItem(QStringLiteral("selection"), QObject::tr("Current selection")),
			optionItem(QStringLiteral("users"), QObject::tr("Selected users")),
			optionItem(QStringLiteral("root"), QObject::tr("Root")),
			optionItem(QStringLiteral("parent"), QObject::tr("Parent channel"))
		};

		QReadLocker lock(&Channel::c_qrwlChannels);
		QList< Channel * > channels = Channel::c_qhChannels.values();
		std::sort(channels.begin(), channels.end(), [](const Channel *lhs, const Channel *rhs) {
			const QString left  = lhs ? lhs->getPath().toCaseFolded() : QString();
			const QString right = rhs ? rhs->getPath().toCaseFolded() : QString();
			return left < right;
		});
		for (const Channel *channel : channels) {
			if (!channel) {
				continue;
			}
			options.push_back(
				optionItem(QStringLiteral("channel:%1").arg(channel->iId), channel->getPath().trimmed().isEmpty()
																			? channel->qsName
																			: channel->getPath()));
		}
		return options;
	}

	QVariantList shortcutChannelOptions() {
		QVariantList options;

		QReadLocker lock(&Channel::c_qrwlChannels);
		QList< Channel * > channels = Channel::c_qhChannels.values();
		std::sort(channels.begin(), channels.end(), [](const Channel *lhs, const Channel *rhs) {
			const QString left  = lhs ? lhs->getPath().toCaseFolded() : QString();
			const QString right = rhs ? rhs->getPath().toCaseFolded() : QString();
			return left < right;
		});
		for (const Channel *channel : channels) {
			if (!channel) {
				continue;
			}
			options.push_back(optionItem(static_cast< int >(channel->iId), channel->getPath().trimmed().isEmpty()
																		   ? channel->qsName
																		   : channel->getPath()));
		}
		if (options.isEmpty()) {
			options.push_back(optionItem(static_cast< int >(Mumble::ROOT_CHANNEL_ID), QObject::tr("Root")));
		}
		return options;
	}

	QVariantList shortcutToggleOptions() {
		return QVariantList { optionItem(-1, QObject::tr("Off")), optionItem(0, QObject::tr("Toggle")),
							  optionItem(1, QObject::tr("On")) };
	}

	QVariantMap shortcutEditorField(const Settings &settings, const int captureIndex) {
		QVariantMap field = fieldItem(QStringLiteral("keys.shortcuts"), QObject::tr("Shortcuts"),
									  QStringLiteral("shortcutEditor"), QVariant());
		field.insert(QStringLiteral("rows"), QVariantList());
		field.insert(QStringLiteral("actionOptions"), shortcutActionOptions());
		field.insert(QStringLiteral("toggleOptions"), shortcutToggleOptions());
		field.insert(QStringLiteral("targetOptions"), shortcutTargetOptions());
		field.insert(QStringLiteral("channelOptions"), shortcutChannelOptions());
		field.insert(QStringLiteral("targetModeOptions"), shortcutTargetModeOptions());
		field.insert(QStringLiteral("targetChannelOptions"), shortcutWhisperChannelOptions());
		field.insert(QStringLiteral("targetUserOptions"), shortcutTargetUserOptions());
		field.insert(QStringLiteral("canSuppress"),
					 GlobalShortcutEngine::engine && GlobalShortcutEngine::engine->canSuppress());
		field.insert(QStringLiteral("canCapture"), GlobalShortcutEngine::engine != nullptr);
		field.insert(QStringLiteral("enabled"), settings.bShortcutEnable);

		QVariantList rows;
		for (int i = 0; i < settings.qlShortcuts.size(); ++i) {
			const Shortcut &shortcut = settings.qlShortcuts.at(i);
			const QVariant data      = shortcutEffectiveData(shortcut);

			QVariantMap row;
			row.insert(QStringLiteral("index"), i);
			row.insert(QStringLiteral("actionIndex"), shortcut.iIndex);
			row.insert(QStringLiteral("actionLabel"), shortcutActionLabel(shortcut));
			row.insert(QStringLiteral("dataType"), shortcutDataType(data));
			row.insert(QStringLiteral("dataLabel"), shortcutDataLabel(data));
			row.insert(QStringLiteral("dataEditable"), data.isValid());
			row.insert(QStringLiteral("inputLabel"), shortcutButtonsLabel(shortcut));
			row.insert(QStringLiteral("assigned"), !shortcut.qlButtons.isEmpty());
			row.insert(QStringLiteral("suppress"), shortcut.bSuppress);
			row.insert(QStringLiteral("capturing"), i == captureIndex);

			const QString dataType = row.value(QStringLiteral("dataType")).toString();
			if (dataType == QLatin1String("toggle")) {
				row.insert(QStringLiteral("dataValue"), data.toInt());
			} else if (dataType == QLatin1String("text")) {
				row.insert(QStringLiteral("dataValue"), data.toString());
			} else if (dataType == QLatin1String("channel")) {
				row.insert(QStringLiteral("dataValue"), static_cast< int >(data.value< ChannelTarget >().channelID));
			} else if (dataType == QLatin1String("target")) {
				const ShortcutTarget target = data.value< ShortcutTarget >();
				row.insert(QStringLiteral("dataValue"), shortcutTargetMode(target));
				row.insert(QStringLiteral("target"), shortcutTargetDetail(target));
			}

			rows.push_back(row);
		}

		field.insert(QStringLiteral("rows"), rows);
		return field;
	}

	int shortcutPayloadIndex(const QVariantMap &payload, const QList< Shortcut > &shortcuts) {
		bool ok        = false;
		const int row  = payload.value(QStringLiteral("index")).toInt(&ok);
		const int size = shortcuts.size();
		if (!ok || row < 0 || row >= size) {
			return -1;
		}
		return row;
	}

	void normalizeShortcutData(Shortcut &shortcut) {
		const GlobalShortcut *definition = shortcutDefinition(shortcut.iIndex);
		if (!definition) {
			shortcut.qvData = QVariant();
			return;
		}
		if (!shortcut.qvData.isValid() || shortcut.qvData.metaType() != definition->qvDefault.metaType()) {
			shortcut.qvData = definition->qvDefault;
		}
	}

	QVariantList shortcutSummaryFields(const Settings &settings) {
		QList< Shortcut > shortcuts = settings.qlShortcuts;
		std::stable_sort(shortcuts.begin(), shortcuts.end());

		int assignedCount = 0;
		for (const Shortcut &shortcut : shortcuts) {
			if (!shortcut.qlButtons.isEmpty()) {
				++assignedCount;
			}
		}

		QVariantList fields;
		fields.push_back(readonlyField(QObject::tr("Configured"),
									   QObject::tr("%1 assigned / %2 total").arg(assignedCount).arg(shortcuts.size())));

		int shown = 0;
		for (const Shortcut &shortcut : shortcuts) {
			if (shortcut.qlButtons.isEmpty()) {
				continue;
			}
			fields.push_back(readonlyField(shortcutActionLabel(shortcut), shortcutButtonsLabel(shortcut)));
			if (++shown >= 8) {
				break;
			}
		}

		if (shortcuts.isEmpty()) {
			fields.push_back(noteField(QObject::tr("No shortcuts are configured yet.")));
		} else if (assignedCount > shown) {
			fields.push_back(noteField(QObject::tr("%n more assigned shortcut(s) are available in the editor.", nullptr,
												  assignedCount - shown)));
		}
		return fields;
	}

	QString pluginSettingsKey(const QString &path) {
		return QString::fromLatin1(QCryptographicHash::hash(path.toUtf8(), QCryptographicHash::Sha1).toHex());
	}

	PluginSetting pluginDraftSetting(const Settings &settings, const PluginDescriptor &plugin) {
		const QString path = plugin.path;
		const QString key  = pluginSettingsKey(path);
		if (settings.qhPluginSettings.contains(key)) {
			PluginSetting setting = settings.qhPluginSettings.value(key);
			setting.path = path;
			return setting;
		}

		PluginSetting setting;
		setting.path                    = path;
		setting.enabled                 = plugin.loaded;
		setting.positionalDataEnabled   = plugin.positionalDataEnabled;
		setting.allowKeyboardMonitoring = plugin.keyboardMonitoringAllowed;
		return setting;
	}

	QVariantMap pluginEditorField(const Settings &settings) {
		QVariantMap field = fieldItem(QStringLiteral("plugins.entries"), QObject::tr("Installed plugins"),
									  QStringLiteral("pluginEditor"), QVariant());
		QVariantList rows;
		if (Global::get().pluginManager) {
			const QVector< PluginDescriptor > plugins = Global::get().pluginManager->pluginDescriptors(true);
			for (const PluginDescriptor &plugin : plugins) {
				const PluginSetting setting = pluginDraftSetting(settings, plugin);
				QVariantMap row;
				row.insert(QStringLiteral("id"), static_cast< qulonglong >(plugin.id));
				row.insert(QStringLiteral("name"), plugin.name);
				row.insert(QStringLiteral("description"), plugin.description);
				row.insert(QStringLiteral("version"), plugin.version);
				row.insert(QStringLiteral("author"), plugin.author);
				row.insert(QStringLiteral("path"), plugin.path);
				row.insert(QStringLiteral("loaded"), plugin.loaded);
				row.insert(QStringLiteral("enabled"), setting.enabled);
				row.insert(QStringLiteral("positionalAvailable"),
						   (plugin.features & MUMBLE_FEATURE_POSITIONAL) != 0);
				row.insert(QStringLiteral("positionalEnabled"), setting.positionalDataEnabled);
				row.insert(QStringLiteral("keyboardMonitoringAllowed"), setting.allowKeyboardMonitoring);
				row.insert(QStringLiteral("canConfigure"), plugin.canConfigure);
				row.insert(QStringLiteral("canShowAbout"), plugin.canShowAbout);
				row.insert(QStringLiteral("builtIn"), plugin.builtIn);
				rows.push_back(row);
			}
		}
		field.insert(QStringLiteral("rows"), rows);
		field.insert(QStringLiteral("enabled"), true);
		return field;
	}

	bool updatePluginDraft(Settings &settings, const QVariantMap &payload) {
		if (!Global::get().pluginManager) {
			return false;
		}
		const plugin_id_t pluginID =
			static_cast< plugin_id_t >(payload.value(QStringLiteral("pluginId")).toULongLong());
		const std::optional< PluginDescriptor > plugin = Global::get().pluginManager->pluginDescriptor(pluginID);
		if (!plugin) {
			return false;
		}

		PluginSetting setting = pluginDraftSetting(settings, *plugin);
		const QString property = payload.value(QStringLiteral("property")).toString();
		const bool value       = payload.value(QStringLiteral("value")).toBool();
		if (property == QLatin1String("enabled")) {
			setting.enabled = value;
		} else if (property == QLatin1String("positional")) {
			if ((plugin->features & MUMBLE_FEATURE_POSITIONAL) == 0) {
				return false;
			}
			setting.positionalDataEnabled = value;
		} else if (property == QLatin1String("keyboard")) {
			setting.allowKeyboardMonitoring = value;
		} else {
			return false;
		}

		settings.qhPluginSettings.insert(pluginSettingsKey(plugin->path), setting);
		return true;
	}

	QVariantMap messageEventEditorField(const Settings &settings) {
		QVariantMap field = fieldItem(QStringLiteral("messages.events"), QObject::tr("Event behavior"),
									  QStringLiteral("messageEventEditor"), QVariant());
		QVariantList rows;
		for (int index = Log::firstMsgType; index <= Log::lastMsgType; ++index) {
			const Log::MsgType type = Log::msgOrder[index];
			const quint32 flags     = settings.qmMessages.value(type);
			QVariantMap row;
			row.insert(QStringLiteral("type"), static_cast< int >(type));
			const QString eventName = Log::translatedMessageName(type);
			row.insert(QStringLiteral("name"),
					   eventName.isEmpty() ? QObject::tr("Event %1").arg(index + 1) : eventName);
			row.insert(QStringLiteral("console"), (flags & Settings::LogConsole) != 0);
			row.insert(QStringLiteral("notification"), (flags & Settings::LogBalloon) != 0);
			row.insert(QStringLiteral("highlight"), (flags & Settings::LogHighlight) != 0);
			row.insert(QStringLiteral("tts"), (flags & Settings::LogTTS) != 0);
			row.insert(QStringLiteral("limit"), (flags & Settings::LogMessageLimit) != 0);
			row.insert(QStringLiteral("sound"), (flags & Settings::LogSoundfile) != 0);
			row.insert(QStringLiteral("soundPath"), settings.qmMessageSounds.value(type));
			rows.push_back(row);
		}
		field.insert(QStringLiteral("rows"), rows);
		field.insert(QStringLiteral("enabled"), true);
		return field;
	}

	bool updateMessageEventDraft(Settings &settings, const QVariantMap &payload) {
		const int type = payload.value(QStringLiteral("messageType"), -1).toInt();
		if (type < Log::firstMsgType || type > Log::PluginMessage) {
			return false;
		}
		const QString property = payload.value(QStringLiteral("property")).toString();
		quint32 flag = Settings::LogNone;
		if (property == QLatin1String("console")) {
			flag = Settings::LogConsole;
		} else if (property == QLatin1String("notification")) {
			flag = Settings::LogBalloon;
		} else if (property == QLatin1String("highlight")) {
			flag = Settings::LogHighlight;
		} else if (property == QLatin1String("tts")) {
			flag = Settings::LogTTS;
		} else if (property == QLatin1String("limit")) {
			flag = Settings::LogMessageLimit;
		} else if (property == QLatin1String("sound")) {
			flag = Settings::LogSoundfile;
		} else {
			return false;
		}

		quint32 flags = settings.qmMessages.value(type);
		if (payload.value(QStringLiteral("value")).toBool()) {
			flags |= flag;
			if (flag == Settings::LogTTS) {
				flags &= ~Settings::LogSoundfile;
			} else if (flag == Settings::LogSoundfile) {
				flags &= ~Settings::LogTTS;
			}
		} else {
			flags &= ~flag;
		}
		settings.qmMessages.insert(type, flags);
		return true;
	}
} // namespace

void ModernSettingsController::restoreVoiceReplayDraft() {
	if (m_voiceReplayPreviousTransmitMode) {
		m_draft.atTransmit = *m_voiceReplayPreviousTransmitMode;
	}
	if (m_voiceReplayPreviousLoopMode) {
		m_draft.lmLoopMode = *m_voiceReplayPreviousLoopMode;
	}
	if (m_voiceReplayPreviousPacketLoss) {
		m_draft.dPacketLoss = *m_voiceReplayPreviousPacketLoss;
	}
	if (m_voiceReplayPreviousMaxPacketDelay) {
		m_draft.dMaxPacketDelay = *m_voiceReplayPreviousMaxPacketDelay;
	}
	m_voiceReplayPreviousTransmitMode.reset();
	m_voiceReplayPreviousLoopMode.reset();
	m_voiceReplayPreviousPacketLoss.reset();
	m_voiceReplayPreviousMaxPacketDelay.reset();
}

ModernSettingsController::ModernSettingsController()
	: m_inputEnhancementCalibrationWorker(
		  std::make_unique< Mumble::InputEnhancement::CalibrationEvaluationWorker >()) {
}

ModernSettingsController::~ModernSettingsController() {
	cancelInputEnhancementCalibration();
	m_inputEnhancementCalibrationWorker->reset();
}

void ModernSettingsController::open(const Settings &settings, const QString &pageName, const bool audioInputOnboarding,
									const QVariantMap &stonksContext, const QVariantMap &motdContext) {
	cancelInputEnhancementCalibration();
	m_inputEnhancementCalibrationWorker->reset();
	m_inputEnhancementCalibrationControls.reset();
	m_inputEnhancementPreAutoPreference.reset();
	m_inputEnhancementReadinessUiError.clear();
	m_original = settings;
	m_draft    = settings;
	m_stonksContext = normalizedStonksContext(stonksContext);
	m_motdContext   = normalizedMotdContext(motdContext);
	m_shortcutCaptureIndex = -1;
	m_voiceReplayPreviousTransmitMode.reset();
	m_voiceReplayPreviousLoopMode.reset();
	m_voiceReplayPreviousPacketLoss.reset();
	m_voiceReplayPreviousMaxPacketDelay.reset();
	m_runtimePreviewDiffersFromOriginal = false;
	m_appearancePreviewActive = false;
	m_audioInputOnboarding = audioInputOnboarding;
	refreshModernThemeCatalog();
	forceModernLayout();
	setActivePage(pageName);
}

void ModernSettingsController::setStonksContext(const QVariantMap &stonksContext) {
	m_stonksContext = normalizedStonksContext(stonksContext);
}

void ModernSettingsController::setMotdContext(const QVariantMap &motdContext) {
	m_motdContext = normalizedMotdContext(motdContext);
}

bool ModernSettingsController::setMotdPreview(const QString &sourceHtml, const QVariantList &blocks,
											  const QString &summary) {
	const int maximumLength = m_motdContext.value(QStringLiteral("maximumLength"), 100000).toInt();
	const QString bounded = sourceHtml.left(maximumLength + 1);
	if (bounded != m_motdContext.value(QStringLiteral("html")).toString()) return false;
	const bool changed = m_motdContext.value(QStringLiteral("previewSourceHtml")).toString() != bounded
		|| m_motdContext.value(QStringLiteral("previewBlocks")).toList() != blocks
		|| m_motdContext.value(QStringLiteral("previewSummary")).toString() != summary;
	if (!changed) return false;
	m_motdContext.insert(QStringLiteral("previewSourceHtml"), bounded);
	m_motdContext.insert(QStringLiteral("previewBlocks"), blocks);
	m_motdContext.insert(QStringLiteral("previewSummary"), summary.left(4096));
	return true;
}

void ModernSettingsController::refreshModernThemeCatalog() {
	// Theme manifests are small but live on disk. Snapshot them once per dialog
	// session so ordinary field edits never synchronously rescan and reparse the
	// theme directory on the UI thread. The explicit reload action refreshes it.
	m_modernThemeCatalog = Mumble::ModernTheme::customThemes();
}

QVariantMap ModernSettingsController::state() const {
	QVariantMap dialog;
	dialog.insert(QStringLiteral("open"), true);
	dialog.insert(QStringLiteral("id"), QStringLiteral("settings"));
	dialog.insert(QStringLiteral("kind"), QStringLiteral("settings"));
	dialog.insert(QStringLiteral("title"), QObject::tr("Settings"));
	dialog.insert(QStringLiteral("subtitle"),
				  m_audioInputOnboarding && m_activePage == QLatin1String("audioInput")
					  ? QObject::tr("Set up your microphone, record one guided sample, and choose how others hear you.")
					  : QString());
	if (m_audioInputOnboarding && m_activePage == QLatin1String("audioInput")) {
		dialog.insert(QStringLiteral("initialFocusId"), QStringLiteral("voiceMeterCalibration_audio.inputMeter"));
	}
	dialog.insert(QStringLiteral("primaryActionId"), QStringLiteral("ok"));
	dialog.insert(QStringLiteral("preventBackdropClose"), true);
	dialog.insert(QStringLiteral("uiTweaks"), modernShellUiTweaksDto(m_draft, m_modernThemeCatalog));
	dialog.insert(QStringLiteral("pages"), pages());
	dialog.insert(QStringLiteral("activePage"), m_activePage);
	dialog.insert(QStringLiteral("sections"), sectionsForActivePage());

	QVariantList actions;
	actions.push_back(
		ModernShellMenuSerializer::actionItem(QStringLiteral("cancel"), QObject::tr("Cancel"), true, false));
	actions.push_back(
		ModernShellMenuSerializer::actionItem(QStringLiteral("apply"), QObject::tr("Apply"), true, false));
	actions.push_back(ModernShellMenuSerializer::actionItem(QStringLiteral("ok"), QObject::tr("Done"), true, false,
																	  QStringLiteral("accent")));
	dialog.insert(QStringLiteral("actions"), actions);
	return dialog;
}

void ModernSettingsController::updateField(const QString &fieldID, const QVariant &value) {
	const QString id = fieldID.trimmed();
	m_inputEnhancementReadinessUiError.clear();
	if (id == QLatin1String("plugins.runtimeLoaded")) {
		const QVariantMap runtimeState = value.toMap();
		reconcilePluginLoadedState(runtimeState.value(QStringLiteral("path")).toString(),
								 runtimeState.value(QStringLiteral("loaded")).toBool());
	} else if (id == QLatin1String("stonks.client.enabled")) {
		m_draft.bModernShellTickerBannerEnabled = value.toBool();
	} else if (id == QLatin1String("stonks.client.profileShortcutVisible")) {
		m_draft.bModernShellStonksProfileShortcutVisible = value.toBool();
	} else if (id == QLatin1String("stonks.client.placement")) {
		m_draft.qsModernShellTickerPlacement = normalizedStonksTickerPlacement(value);
	} else if (id == QLatin1String("stonks.client.direction")) {
		m_draft.qsModernShellTickerDirection = normalizedStonksTickerDirection(value);
	} else if (id == QLatin1String("stonks.client.speed")) {
		m_draft.qsModernShellTickerSpeed = normalizedStonksTickerSpeed(value);
	} else if (id == QLatin1String("stonks.server.enabled") && m_stonksContext.value(QStringLiteral("canAdmin")).toBool()) {
		m_stonksContext.insert(QStringLiteral("enabled"), value.toBool());
	} else if (id == QLatin1String("stonks.server.socialAnnouncementsEnabled")
			   && m_stonksContext.value(QStringLiteral("canAdmin")).toBool()) {
		m_stonksContext.insert(QStringLiteral("socialAnnouncementsEnabled"), value.toBool());
	} else if (id == QLatin1String("stonks.server.textChannelId")
			   && m_stonksContext.value(QStringLiteral("canAdmin")).toBool()) {
		m_stonksContext.insert(QStringLiteral("textChannelId"), value.toUInt());
	} else if (id == QLatin1String("motd.html")
			   && m_motdContext.value(QStringLiteral("canEdit")).toBool()) {
		const int maximumLength = m_motdContext.value(QStringLiteral("maximumLength"), 100000).toInt();
		const QString html = value.toString().left(maximumLength + 1);
		m_motdContext.insert(QStringLiteral("html"), html);
		if (m_motdContext.value(QStringLiteral("previewSourceHtml")).toString() != html) {
			m_motdContext.insert(QStringLiteral("previewSourceHtml"), QString());
			m_motdContext.insert(QStringLiteral("previewBlocks"), QVariantList());
			m_motdContext.insert(QStringLiteral("previewSummary"), QString());
		}
	} else if (id == QLatin1String("look.quitBehavior")) {
		m_draft.quitBehavior = static_cast< QuitBehavior >(value.toInt());
	} else if (id == QLatin1String("look.alwaysOnTop")) {
		m_draft.aotbAlwaysOnTop = value.toInt() == Settings::OnTopAlways ? Settings::OnTopAlways
																			  : Settings::OnTopNever;
	} else if (id == QLatin1String("look.modernTheme")) {
		m_draft.qsModernShellTheme = normalizedModernShellTheme(value, m_modernThemeCatalog);
	} else if (id == QLatin1String("look.modernDensity")) {
		m_draft.qsModernShellDensity = normalizedModernShellDensity(value);
	} else if (id == QLatin1String("look.modernClassicUserIcons")) {
		m_draft.bModernShellClassicUserIcons = value.toBool();
	} else if (id == QLatin1String("look.modernRailSide")) {
		m_draft.qsModernShellRailSide = normalizedModernShellRailSide(value);
	} else if (id == QLatin1String("look.modernAccent")) {
		m_draft.qsModernShellAccent = normalizedModernShellAccent(value);
	} else if (id == QLatin1String("look.modernCustomAccent")) {
		m_draft.qsModernShellCustomAccent = normalizedModernShellCustomAccent(value);
	} else if (id == QLatin1String("look.modernCustomAccentStrength")) {
		m_draft.iModernShellCustomAccentStrength = normalizedModernShellCustomAccentStrength(value.toInt());
	} else if (id == QLatin1String("look.hideInTray")) {
		m_draft.bHideInTray = value.toBool();
	} else if (id == QLatin1String("look.stateInTray")) {
		m_draft.bStateInTray = value.toBool();
	} else if (id == QLatin1String("look.showVolumeAdjustments")) {
		m_draft.bShowVolumeAdjustments = value.toBool();
	} else if (id == QLatin1String("look.showNicknamesOnly")) {
		m_draft.bShowNicknamesOnly = value.toBool();
	} else if (id == QLatin1String("look.filterHidesEmptyChannels")) {
		m_draft.bFilterHidesEmptyChannels = value.toBool();
	} else if (id == QLatin1String("look.presenceIdleTimeout")) {
		m_draft.iPresenceIdleTimeoutMinutes = qBound(1, value.toInt(), 240);
	} else if (id == QLatin1String("network.autoReconnect")) {
		m_draft.bReconnect = value.toBool();
	} else if (id == QLatin1String("network.autoConnect")) {
		m_draft.bAutoConnect = value.toBool();
	} else if (id == QLatin1String("network.reconnectToLastChannel")) {
		m_draft.bReconnectToLastChannel = value.toBool();
	} else if (id == QLatin1String("network.startWithPC")) {
		m_draft.bStartWithPC = value.toBool();
	} else if (id == QLatin1String("network.tcpMode")) {
		m_draft.bTCPCompat = value.toBool();
	} else if (id == QLatin1String("network.qos")) {
		m_draft.bQoS = value.toBool();
	} else if (id == QLatin1String("network.suppressIdentity")) {
		m_draft.bSuppressIdentity = value.toBool();
	} else if (id == QLatin1String("network.linkPreviews")) {
		m_draft.bEnableLinkPreviews = value.toBool();
	} else if (id == QLatin1String("network.hideOS")) {
		m_draft.bHideOS = value.toBool();
	} else if (id == QLatin1String("network.proxyType")) {
		m_draft.ptProxyType = static_cast< Settings::ProxyType >(value.toInt());
	} else if (id == QLatin1String("network.proxyHost")) {
		m_draft.qsProxyHost = value.toString().trimmed();
	} else if (id == QLatin1String("network.proxyPort")) {
		m_draft.usProxyPort = static_cast< unsigned short >(qBound(0, value.toInt(), 65535));
	} else if (id == QLatin1String("network.proxyUsername")) {
		m_draft.qsProxyUsername = value.toString();
	} else if (id == QLatin1String("network.proxyPassword")) {
		m_draft.qsProxyPassword = value.toString();
	} else if (id == QLatin1String("network.updateCheck")) {
		m_draft.bUpdateCheck = value.toBool();
	} else if (id == QLatin1String("network.pluginCheck")) {
		m_draft.bPluginCheck = value.toBool();
	} else if (id == QLatin1String("network.pluginAutoUpdate")) {
		m_draft.bPluginAutoUpdate = value.toBool();
	} else if (id == QLatin1String("network.advertisedRelease")) {
		m_draft.qsAdvertisedReleaseOverride = value.toString().trimmed();
	} else if (id == QLatin1String("network.advertisedOS")) {
		m_draft.qsAdvertisedOSOverride = value.toString().trimmed();
	} else if (id == QLatin1String("network.advertisedOSVersion")) {
		m_draft.qsAdvertisedOSVersionOverride = value.toString().trimmed();
	} else if (id == QLatin1String("screenShare.autoOpenCurrentRoom")) {
		m_draft.bScreenShareAutoOpenCurrentRoom = value.toBool();
	} else if (id == QLatin1String("screenShare.diagnostics")) {
		m_draft.bScreenShareDiagnostics = value.toBool();
	}

	// Keep the large settings dispatch in separate mutually exclusive chains.
	// MSVC counts every `else if` as a nested block and rejects the otherwise
	// valid function once the chain crosses its compiler nesting limit.
	if (id == QLatin1String("audio.inputSystem")) {
		const QList< QString > systems = inputSystemNames();
		m_draft.qsAudioInput          = systemNameAt(systems, value, m_draft.qsAudioInput);
		if (!AudioInputRegistrar::current.isEmpty() && m_draft.qsAudioInput.isEmpty()) {
			m_draft.qsAudioInput = AudioInputRegistrar::current;
		}
	} else if (id == QLatin1String("audio.inputDevice")) {
		const QList< audioDevice > choices = inputDeviceChoices(m_draft.qsAudioInput);
		const int index                    = value.toInt();
		if (index >= 0 && index < choices.size() && AudioInputRegistrar::qmNew
			&& AudioInputRegistrar::qmNew->contains(m_draft.qsAudioInput)) {
			AudioInputRegistrar::qmNew->value(m_draft.qsAudioInput)->setDeviceChoice(choices.at(index).second,
																			 m_draft);
		}
#if defined(Q_OS_WIN) && defined(USE_WASAPI)
	} else if (id == QLatin1String("audio.wasapiInputRouting")) {
		const int index = qBound(0, value.toInt(), 2);
		const Mumble::WASAPI::RoutingPolicy policy = index == 0 ? Mumble::WASAPI::RoutingPolicy::FollowDefault
			: index == 2 ? Mumble::WASAPI::RoutingPolicy::StrictSelected
							 : Mumble::WASAPI::RoutingPolicy::PreferSelected;
		m_draft.qsWASAPIInputRoutingPolicy = Mumble::WASAPI::routingPolicyName(policy);
		if (policy == Mumble::WASAPI::RoutingPolicy::FollowDefault) {
			m_draft.qsWASAPIInput.clear();
			m_draft.qsWASAPIInputDeviceIdentity.clear();
		} else if (m_draft.qsWASAPIInput.isEmpty()) {
			const Mumble::WASAPI::DeviceDescriptor descriptor =
				WASAPISystem::defaultDeviceDescriptor(eCapture, wasapiRole(m_draft));
			m_draft.qsWASAPIInput = descriptor.endpointId;
			m_draft.qsWASAPIInputDeviceIdentity = Mumble::WASAPI::serializeDeviceDescriptor(descriptor);
		}
#endif
	} else if (id == QLatin1String("audio.outputSystem")) {
		const QList< QString > systems = outputSystemNames();
		m_draft.qsAudioOutput         = systemNameAt(systems, value, m_draft.qsAudioOutput);
		if (!AudioOutputRegistrar::current.isEmpty() && m_draft.qsAudioOutput.isEmpty()) {
			m_draft.qsAudioOutput = AudioOutputRegistrar::current;
		}
	} else if (id == QLatin1String("audio.outputDevice")) {
		const QList< audioDevice > choices = outputDeviceChoices(m_draft.qsAudioOutput);
		const int index                    = value.toInt();
		if (index >= 0 && index < choices.size() && AudioOutputRegistrar::qmNew
			&& AudioOutputRegistrar::qmNew->contains(m_draft.qsAudioOutput)) {
			AudioOutputRegistrar::qmNew->value(m_draft.qsAudioOutput)->setDeviceChoice(choices.at(index).second,
																			   m_draft);
		}
#if defined(Q_OS_WIN) && defined(USE_WASAPI)
	} else if (id == QLatin1String("audio.wasapiOutputRouting")) {
		const int index = qBound(0, value.toInt(), 2);
		const Mumble::WASAPI::RoutingPolicy policy = index == 0 ? Mumble::WASAPI::RoutingPolicy::FollowDefault
			: index == 2 ? Mumble::WASAPI::RoutingPolicy::StrictSelected
							 : Mumble::WASAPI::RoutingPolicy::PreferSelected;
		m_draft.qsWASAPIOutputRoutingPolicy = Mumble::WASAPI::routingPolicyName(policy);
		if (policy == Mumble::WASAPI::RoutingPolicy::FollowDefault) {
			m_draft.qsWASAPIOutput.clear();
			m_draft.qsWASAPIOutputDeviceIdentity.clear();
		} else if (m_draft.qsWASAPIOutput.isEmpty()) {
			const Mumble::WASAPI::DeviceDescriptor descriptor =
				WASAPISystem::defaultDeviceDescriptor(eRender, wasapiRole(m_draft));
			m_draft.qsWASAPIOutput = descriptor.endpointId;
			m_draft.qsWASAPIOutputDeviceIdentity = Mumble::WASAPI::serializeDeviceDescriptor(descriptor);
		}
	} else if (id == QLatin1String("audio.wasapiLatencyProfile")) {
		const int index = qBound(0, value.toInt(), 2);
		m_draft.qsWASAPILatencyProfile = Mumble::WASAPI::latencyProfileName(
			index == 1 ? Mumble::WASAPI::LatencyProfile::Balanced
				: index == 2 ? Mumble::WASAPI::LatencyProfile::Low : Mumble::WASAPI::LatencyProfile::Stable);
#endif
	} else if (id == QLatin1String("audio.exclusiveInput")) {
		m_draft.bExclusiveInput = value.toBool();
	} else if (id == QLatin1String("audio.exclusiveOutput")) {
		m_draft.bExclusiveOutput = value.toBool();
	} else if (id == QLatin1String("audio.transmitMode")) {
		m_draft.atTransmit = static_cast< Settings::AudioTransmit >(value.toInt());
	} else if (id == QLatin1String("audio.vadSource")) {
		const auto vadSource = static_cast< Settings::VADSource >(value.toInt());
		if (vadSource == Settings::SignalToNoise || vadSource == Settings::Hybrid || vadSource == Settings::Amplitude) {
			m_draft.vsVAD = vadSource;
		}
	} else if (id == QLatin1String("audio.vadMin")) {
		m_draft.fVADmin = qMin(vadThresholdFromPercent(value), m_draft.fVADmax);
	} else if (id == QLatin1String("audio.vadMax")) {
		m_draft.fVADmax = qMax(vadThresholdFromPercent(value), m_draft.fVADmin);
	} else if (id == QLatin1String("audio.inputGateMode")) {
		m_draft.inputGateMode = static_cast< Settings::InputGateMode >(
			qBound(0, value.toInt(), static_cast< int >(Settings::InputGateStrict)));
	} else if (id == QLatin1String("audio.voiceHold")) {
		m_draft.iVoiceHold = qBound(0, value.toInt(), 250);
	} else if (id == QLatin1String("audio.doublePush")) {
		m_draft.uiDoublePush = static_cast< quint64 >(qBound(0, value.toInt(), 5000));
	} else if (id == QLatin1String("audio.pttHold")) {
		m_draft.pttHold = static_cast< quint64 >(qBound(0, value.toInt(), 5000));
	} else if (id == QLatin1String("audio.showPttWindow")) {
		m_draft.bShowPTTButtonWindow = value.toBool();
	} else if (id == QLatin1String("audio.quality")) {
		m_draft.iQuality = qBound(8000, bitrateBitsFromKbit(value), 512000);
	} else if (id == QLatin1String("audio.experimentalHighBitrate")) {
		m_draft.experimentalHighBitrateEnabled = value.toBool();
	} else if (id == QLatin1String("audio.allowLowDelay")) {
		m_draft.bAllowLowDelay = value.toBool();
	} else if (id == QLatin1String("audio.maxAmplification")) {
		m_draft.iMinLoudness = minLoudnessFromAmplification(value);
	} else if (id == QLatin1String("audio.echoMode")) {
		m_draft.echoOption = static_cast< EchoCancelOptionID >(value.toInt());
	} else if (id == QLatin1String("audio.inputEnhancementProfile")) {
		const int profileValue  = value.toInt();
		const bool knownProfile = (profileValue >= static_cast< int >(Mumble::InputEnhancement::Profile::Original)
								   && profileValue <= static_cast< int >(Mumble::InputEnhancement::Profile::Quality))
								  || profileValue == static_cast< int >(Mumble::InputEnhancement::Profile::VoiceFocus);
		if (knownProfile) {
			using namespace Mumble::InputEnhancement;
			const Profile selectedProfile = static_cast< Profile >(profileValue);
			const DefaultPreference &currentPreference = currentInputEnhancementPreference(m_draft);
			DefaultPreference candidate = currentPreference;
			candidate.profile            = selectedProfile;
			candidate.autoAdapt          = false;
			if (const std::optional< ExplicitProfileControlPreset > preset =
					qualifiedExplicitSelectionPreset(selectedProfile)) {
				candidate.reduction = preset->noiseReduction;
				candidate.character = preset->naturalCrisp;
			}
			const InputEnhancementSettingsReadiness readiness = inputEnhancementReadinessForSettings(
				m_draft, selectedProfile, candidate.reduction, candidate.character);
			if (!readiness.selectable) {
				m_inputEnhancementReadinessUiError = QObject::tr("Input enhancement was not changed: %1")
												 .arg(inputEnhancementReadinessReasonText(readiness));
				return;
			}
			disarmDraftInputEnhancementProbationForManualEdit(m_draft);
			if (Mumble::InputEnhancement::DefaultPreference *preference =
					editableCurrentInputEnhancementPreference(m_draft)) {
				*preference = candidate;
				m_inputEnhancementPreAutoPreference.reset();
				projectInputEnhancementPreference(m_draft);
			}
		}
	} else if (id == QLatin1String("audio.inputEnhancementExperimentalAuto")) {
		using namespace Mumble::InputEnhancement;
		disarmDraftInputEnhancementProbationForManualEdit(m_draft);
		DefaultPreference *preference = editableCurrentInputEnhancementPreference(m_draft);
		if (!preference) {
			return;
		}
		if (value.toBool()) {
			if (preference->profile == Profile::Auto) {
				return;
			}
			const InputEnhancementSettingsReadiness readiness = inputEnhancementReadinessForSettings(
				m_draft, Profile::Auto, preference->reduction, preference->character);
			if (!readiness.selectable) {
				m_inputEnhancementReadinessUiError = QObject::tr("Experimental Auto was not enabled: %1")
												 .arg(inputEnhancementReadinessReasonText(readiness));
				return;
			}
			m_inputEnhancementPreAutoPreference = *preference;
			preference->profile                  = Profile::Auto;
			preference->autoAdapt                = true;
		} else if (preference->profile == Profile::Auto) {
			DefaultPreference restored;
			if (m_inputEnhancementPreAutoPreference
				&& m_inputEnhancementPreAutoPreference->profile != Profile::Auto) {
				restored = *m_inputEnhancementPreAutoPreference;
			}
			restored.autoAdapt = false;
			*preference        = restored;
			m_inputEnhancementPreAutoPreference.reset();
		}
		projectInputEnhancementPreference(m_draft);
	} else if (id == QLatin1String("audio.inputEnhancementReduction")) {
		disarmDraftInputEnhancementProbationForManualEdit(m_draft);
		if (Mumble::InputEnhancement::DefaultPreference *preference =
				editableCurrentInputEnhancementPreference(m_draft)) {
			preference->reduction = qBound(0, value.toInt(), 100);
			projectInputEnhancementPreference(m_draft);
		}
	} else if (id == QLatin1String("audio.inputEnhancementCharacter")) {
		disarmDraftInputEnhancementProbationForManualEdit(m_draft);
		if (Mumble::InputEnhancement::DefaultPreference *preference =
				editableCurrentInputEnhancementPreference(m_draft)) {
			preference->character = qBound(0, value.toInt(), 100);
			projectInputEnhancementPreference(m_draft);
		}
	} else if (id == QLatin1String("audio.inputEnhancementAutoAdapt")) {
		// Kept as a no-op compatibility endpoint for a stale settings surface.
		// The schema value is retained, but fixed-profile adaptation is neither
		// exposed nor active until it has its own release qualification.
	} else if (id == QLatin1String("audio.noiseCancelMode")) {
		m_draft.noiseCancelMode = static_cast< Settings::NoiseCancel >(value.toInt());
		captureLegacyInputOverride(m_draft);
	} else if (id == QLatin1String("audio.noiseCancelBackend")) {
		m_draft.noiseCancelBackend = normalizedBackendFromValue(value);
		m_draft.noiseCancelModelId =
			normalizedSpeechCleanupModelID(m_draft.noiseCancelBackend, m_draft.noiseCancelModelId);
		if (!Mumble::SpeechCleanup::usesCustomModelPath(m_draft.noiseCancelBackend, m_draft.noiseCancelModelId)) {
			m_draft.noiseCancelCustomModelPath.clear();
		}
		captureLegacyInputOverride(m_draft);
	} else if (id == QLatin1String("audio.noiseCancelModel")) {
		m_draft.noiseCancelModelId =
			normalizedSpeechCleanupModelID(m_draft.noiseCancelBackend, value.toString());
		if (!Mumble::SpeechCleanup::usesCustomModelPath(m_draft.noiseCancelBackend, m_draft.noiseCancelModelId)) {
			m_draft.noiseCancelCustomModelPath.clear();
		}
		captureLegacyInputOverride(m_draft);
	} else if (id == QLatin1String("audio.noiseCancelCustomModelPath")) {
		m_draft.noiseCancelCustomModelPath = value.toString().trimmed();
		captureLegacyInputOverride(m_draft);
	} else if (id == QLatin1String("audio.speexNoiseStrength")) {
		m_draft.iSpeexNoiseCancelStrength = value.toInt() == 14 ? 0 : -qBound(0, value.toInt(), 100);
		captureLegacyInputOverride(m_draft);
	} else if (id == QLatin1String("audio.outputVolume")) {
		m_draft.fVolume = floatFromPercent(value);
	} else if (id == QLatin1String("audio.externalApplicationsVolume")) {
		m_draft.fOtherVolume = invertedFloatFromPercent(value);
	} else if (id == QLatin1String("audio.listenerAttenuation")) {
		m_draft.listenerAttenuationFactor = invertedFloatFromPercent(value);
	} else if (id == QLatin1String("audio.outputDelay")) {
		m_draft.iOutputDelay = qBound(0, value.toInt(), 100);
	} else if (id == QLatin1String("audio.framesPerPacket")) {
		m_draft.iFramesPerPacket = AudioInput::clampFramesPerPacket(value.toInt());
	} else if (id == QLatin1String("audio.jitterBuffer")) {
		m_draft.iJitterBufferSize = qBound(0, value.toInt(), 10);
	} else if (id == QLatin1String("audio.attenuateOthers")) {
		m_draft.bAttenuateOthers = value.toBool();
	} else if (id == QLatin1String("audio.attenuateOnTalk")) {
		m_draft.bAttenuateOthersOnTalk = value.toBool();
	} else if (id == QLatin1String("audio.attenuatePrioritySpeaker")) {
		m_draft.bAttenuateUsersOnPrioritySpeak = value.toBool();
	} else if (id == QLatin1String("audio.attenuateSameOutputOnly")) {
		m_draft.bOnlyAttenuateSameOutput = value.toBool();
	} else if (id == QLatin1String("audio.attenuateLoopbacks")) {
		m_draft.bAttenuateLoopbacks = value.toBool();
	} else if (id == QLatin1String("audio.alwaysAttenuateListeners")) {
		m_draft.alwaysAttenuateListeners = value.toBool();
	} else if (id == QLatin1String("audio.loopMode")) {
		m_draft.lmLoopMode = static_cast< Settings::LoopMode >(value.toInt());
	} else if (id == QLatin1String("audio.loopPacketDelay")) {
		m_draft.dMaxPacketDelay = static_cast< float >(qBound(0, value.toInt(), 1000));
	} else if (id == QLatin1String("audio.loopPacketLoss")) {
		m_draft.dPacketLoss = static_cast< float >(qBound(0, value.toInt(), 100)) / 100.0f;
	} else if (id == QLatin1String("audio.positional")) {
		m_draft.bPositionalAudio = value.toBool();
	} else if (id == QLatin1String("audio.positionalHeadphone")) {
		m_draft.bPositionalHeadphone = value.toBool();
	} else if (id == QLatin1String("audio.transmitPosition")) {
		m_draft.bTransmitPosition = value.toBool();
	} else if (id == QLatin1String("audio.minDistance")) {
		m_draft.fAudioMinDistance = static_cast< float >(qBound(0.0, value.toDouble(), 1000.0));
		if (m_draft.fAudioMaxDistance < m_draft.fAudioMinDistance + 1.0f) {
			m_draft.fAudioMaxDistance = m_draft.fAudioMinDistance + 1.0f;
		}
	} else if (id == QLatin1String("audio.maxDistance")) {
		m_draft.fAudioMaxDistance = static_cast< float >(qBound(1.0, value.toDouble(), 10000.0));
		if (m_draft.fAudioMinDistance > m_draft.fAudioMaxDistance - 1.0f) {
			m_draft.fAudioMinDistance = qMax(0.0f, m_draft.fAudioMaxDistance - 1.0f);
		}
	} else if (id == QLatin1String("audio.minPositionalVolume")) {
		m_draft.fAudioMaxDistVolume = static_cast< float >(qBound(0, value.toInt(), 100)) / 100.0f;
	} else if (id == QLatin1String("audio.bloom")) {
		m_draft.fAudioBloom = static_cast< float >(qBound(0, value.toInt(), 75)) / 100.0f;
	} else if (id == QLatin1String("audio.remoteCleanupEnabled")) {
		m_draft.remoteSpeechCleanupEnabled = value.toBool() && hasAvailableSpeechCleanupBackend();
	} else if (id == QLatin1String("audio.remoteCleanupBackend")) {
		m_draft.remoteSpeechCleanupBackend = normalizedBackendFromValue(value);
		m_draft.remoteSpeechCleanupModelId =
			normalizedSpeechCleanupModelID(m_draft.remoteSpeechCleanupBackend, m_draft.remoteSpeechCleanupModelId);
		if (!Mumble::SpeechCleanup::usesCustomModelPath(m_draft.remoteSpeechCleanupBackend,
														m_draft.remoteSpeechCleanupModelId)) {
			m_draft.remoteSpeechCleanupCustomModelPath.clear();
		}
	} else if (id == QLatin1String("audio.remoteCleanupModel")) {
		m_draft.remoteSpeechCleanupModelId =
			normalizedSpeechCleanupModelID(m_draft.remoteSpeechCleanupBackend, value.toString());
		if (!Mumble::SpeechCleanup::usesCustomModelPath(m_draft.remoteSpeechCleanupBackend,
														m_draft.remoteSpeechCleanupModelId)) {
			m_draft.remoteSpeechCleanupCustomModelPath.clear();
		}
	} else if (id == QLatin1String("audio.remoteCleanupCustomModelPath")) {
		m_draft.remoteSpeechCleanupCustomModelPath = value.toString().trimmed();
	} else if (id == QLatin1String("audio.remoteCleanupPreset")) {
		m_draft.remoteSpeechCleanupPreset = static_cast< Settings::RemoteSpeechCleanupPreset >(value.toInt());
	} else if (id == QLatin1String("audio.cuePtt")) {
		m_draft.audioCueEnabledPTT = value.toBool();
	} else if (id == QLatin1String("audio.cueVad")) {
		m_draft.audioCueEnabledVAD = value.toBool();
	} else if (id == QLatin1String("audio.cueOnPath")) {
		m_draft.qsTxAudioCueOn = value.toString();
	} else if (id == QLatin1String("audio.cueOffPath")) {
		m_draft.qsTxAudioCueOff = value.toString();
	} else if (id == QLatin1String("audio.muteCue")) {
		m_draft.bTxMuteCue = value.toBool();
	} else if (id == QLatin1String("audio.muteCuePath")) {
		m_draft.qsTxMuteCue = value.toString();
	} else if (id == QLatin1String("audio.idleMinutes")) {
		m_draft.iIdleTime = static_cast< unsigned int >(qBound(0, value.toInt(), 1440) * 60);
	} else if (id == QLatin1String("audio.idleAction")) {
		m_draft.iaeIdleAction = static_cast< Settings::IdleAction >(value.toInt());
	} else if (id == QLatin1String("audio.undoIdleAction")) {
		m_draft.bUndoIdleActionUponActivity = value.toBool();
	} else if (id == QLatin1String("keys.globalShortcuts")) {
		m_draft.bShortcutEnable = value.toBool();
	} else if (id == QLatin1String("keys.enableUiAccess")) {
		m_draft.bEnableUIAccess = value.toBool();
		refreshShortcutRestartFlag();
	} else if (id == QLatin1String("keys.enableGKey")) {
		m_draft.bEnableGKey = value.toBool();
		refreshShortcutRestartFlag();
	} else if (id == QLatin1String("keys.enableXboxInput")) {
		m_draft.bEnableXboxInput = value.toBool();
		refreshShortcutRestartFlag();
	} else if (id == QLatin1String("plugins.transmitPosition")) {
		m_draft.bTransmitPosition = value.toBool();
	} else if (id == QLatin1String("messages.ttsEnabled")) {
		m_draft.bTTS = value.toBool();
	} else if (id == QLatin1String("messages.ttsVolume")) {
		m_draft.iTTSVolume = qBound(0, value.toInt(), 100);
	} else if (id == QLatin1String("messages.ttsThreshold")) {
		m_draft.iTTSThreshold = qBound(0, value.toInt(), 10000);
	} else if (id == QLatin1String("messages.ttsReadOwn")) {
		m_draft.bTTSMessageReadBack = value.toBool();
	} else if (id == QLatin1String("messages.ttsNoScope")) {
		m_draft.bTTSNoScope = value.toBool();
	} else if (id == QLatin1String("messages.ttsNoAuthor")) {
		m_draft.bTTSNoAuthor = value.toBool();
	} else if (id == QLatin1String("messages.ttsLanguage")) {
		m_draft.qsTTSLanguage = value.toString().trimmed();
	} else if (id == QLatin1String("messages.notificationVolume")) {
		m_draft.notificationVolume = floatFromPercent(value);
	} else if (id == QLatin1String("messages.cueVolume")) {
		m_draft.cueVolume = floatFromPercent(value);
	} else if (id == QLatin1String("messages.limitThreshold")) {
		m_draft.iMessageLimitUserThreshold = qBound(0, value.toInt(), 10000);
	} else if (id == QLatin1String("messages.clock24Hour")) {
		m_draft.bLog24HourClock = value.toBool();
	} else if (id == QLatin1String("messages.whisperFriends")) {
		m_draft.bWhisperFriends = value.toBool();
	} else if (id.startsWith(QLatin1String("messages.sound."))) {
		bool ok = false;
		const int type = id.mid(QStringLiteral("messages.sound.").size()).toInt(&ok);
		if (ok) {
			m_draft.qmMessageSounds.insert(type, value.toString());
		}
	}

	forceModernLayout();
}

bool ModernSettingsController::reconcilePluginLoadedState(const QString &pluginPath, const bool loaded) {
	const QString path = pluginPath.trimmed();
	if (path.isEmpty()) {
		return false;
	}

	const QString key = pluginSettingsKey(path);
	PluginSetting runtimeDefaults;
	runtimeDefaults.path    = path;
	runtimeDefaults.enabled = loaded;
	// Runtime reconciliation must stay frontend-neutral and usable before the
	// global plugin manager exists (for example while restoring Settings state).
	// Existing permission flags are preserved by the value lookup below; a new
	// entry intentionally starts from the persisted PluginSetting defaults.
	const auto reconcile = [&path, &key, &runtimeDefaults, loaded](Settings &settings) {
		PluginSetting setting = settings.qhPluginSettings.value(key, runtimeDefaults);
		setting.path           = path;
		setting.enabled        = loaded;
		settings.qhPluginSettings.insert(key, setting);
	};
	reconcile(m_original);
	reconcile(m_draft);
	return true;
}

ModernSettingsController::ActionResult ModernSettingsController::invokeAction(const QString &actionID,
																			  const QVariantMap &payload) {
	ActionResult result;
	const QString action = actionID.trimmed();
	if (action == QLatin1String("selectPage")) {
		const bool leavingAudioInput = m_activePage == QLatin1String("audioInput");
		m_shortcutCaptureIndex = -1;
		setActivePage(payload.value(QStringLiteral("pageId")).toString());
		if (leavingAudioInput && m_activePage != QLatin1String("audioInput")) {
			cancelInputEnhancementCalibration();
		}
		return result;
	}

	if (action == QLatin1String("look.previewAppearance")) {
		const QString fieldID = payload.value(QStringLiteral("fieldId")).toString().trimmed();
		if (!isModernAppearancePreviewField(fieldID) || !payload.contains(QStringLiteral("value"))) {
			result.stateChanged = false;
			return result;
		}
		updateField(fieldID, payload.value(QStringLiteral("value")));
		result.appearanceToPreview = modernAppearancePreview(m_draft, m_modernThemeCatalog);
		m_appearancePreviewActive = true;
		return result;
	}

	if (action == QLatin1String("stonks.updateClient")) {
		const QString fieldID = payload.value(QStringLiteral("fieldId")).toString().trimmed();
		if (!fieldID.startsWith(QLatin1String("stonks.client."))
			|| !payload.contains(QStringLiteral("value"))) {
			result.stateChanged = false;
			return result;
		}
		updateField(fieldID, payload.value(QStringLiteral("value")));
		// These controls are intentionally immediate. Move the saved values into
		// the reset/cancel baseline so closing Settings never rolls them back.
		m_original.bModernShellTickerBannerEnabled = m_draft.bModernShellTickerBannerEnabled;
		m_original.bModernShellStonksProfileShortcutVisible =
			m_draft.bModernShellStonksProfileShortcutVisible;
		m_original.qsModernShellTickerPlacement     = m_draft.qsModernShellTickerPlacement;
		m_original.qsModernShellTickerDirection     = m_draft.qsModernShellTickerDirection;
		m_original.qsModernShellTickerSpeed         = m_draft.qsModernShellTickerSpeed;
		result.externalActionID = QStringLiteral("stonks.updateClient");
		result.externalActionPayload.insert(QStringLiteral("tickerBannerEnabled"),
										m_draft.bModernShellTickerBannerEnabled);
		result.externalActionPayload.insert(QStringLiteral("profileShortcutVisible"),
										m_draft.bModernShellStonksProfileShortcutVisible);
		result.externalActionPayload.insert(QStringLiteral("tickerPlacement"),
										normalizedStonksTickerPlacement(m_draft.qsModernShellTickerPlacement));
		result.externalActionPayload.insert(QStringLiteral("tickerDirection"),
										normalizedStonksTickerDirection(m_draft.qsModernShellTickerDirection));
		result.externalActionPayload.insert(QStringLiteral("tickerSpeed"),
										normalizedStonksTickerSpeed(m_draft.qsModernShellTickerSpeed));
		return result;
	}

	if (action == QLatin1String("stonks.openPortfolio")) {
		result.externalActionID = QStringLiteral("stonks.openPortfolio");
		return result;
	}

	if (action == QLatin1String("stonks.applyServer")) {
		if (!m_stonksContext.value(QStringLiteral("connected")).toBool()
			|| !m_stonksContext.value(QStringLiteral("supported")).toBool()
			|| !m_stonksContext.value(QStringLiteral("canAdmin")).toBool()) {
			result.stateChanged = false;
			return result;
		}
		result.externalActionID = QStringLiteral("stonks.applyServer");
		result.externalActionPayload.insert(QStringLiteral("enabled"),
										m_stonksContext.value(QStringLiteral("enabled"), true).toBool());
		result.externalActionPayload.insert(
			QStringLiteral("socialAnnouncementsEnabled"),
			m_stonksContext.value(QStringLiteral("socialAnnouncementsEnabled"), true).toBool());
		result.externalActionPayload.insert(QStringLiteral("textChannelId"),
										m_stonksContext.value(QStringLiteral("textChannelId")).toUInt());
		return result;
	}

	if (action == QLatin1String("motd.save")) {
		if (!m_motdContext.value(QStringLiteral("available")).toBool()
			|| !m_motdContext.value(QStringLiteral("canEdit")).toBool()) {
			result.stateChanged = false;
			return result;
		}
		const int maximumLength = m_motdContext.value(QStringLiteral("maximumLength"), 100000).toInt();
		const QString html = payload.value(QStringLiteral("html"),
									  m_motdContext.value(QStringLiteral("html"))).toString();
		if (html.size() > maximumLength) {
			result.stateChanged = false;
			return result;
		}
		m_motdContext.insert(QStringLiteral("html"), html);
		result.externalActionID = QStringLiteral("motd.save");
		result.externalActionPayload.insert(QStringLiteral("html"), html);
		return result;
	}

	if (action == QLatin1String("motd.preview")) {
		if (!m_motdContext.value(QStringLiteral("available")).toBool()) {
			result.stateChanged = false;
			return result;
		}
		const int maximumLength = m_motdContext.value(QStringLiteral("maximumLength"), 100000).toInt();
		const QString html = m_motdContext.value(QStringLiteral("canEdit")).toBool()
			? payload.value(QStringLiteral("html"), m_motdContext.value(QStringLiteral("html"))).toString()
			: m_motdContext.value(QStringLiteral("html")).toString();
		if (html.size() > maximumLength) {
			result.stateChanged = false;
			return result;
		}
		m_motdContext.insert(QStringLiteral("html"), html);
		result.stateChanged = false;
		result.externalActionID = QStringLiteral("motd.preview");
		result.externalActionPayload.insert(QStringLiteral("html"), html);
		return result;
	}

	if (action == QLatin1String("motd.insertImage")) {
		if (!m_motdContext.value(QStringLiteral("available")).toBool()
			|| !m_motdContext.value(QStringLiteral("canEdit")).toBool()) {
			result.stateChanged = false;
			return result;
		}
		result.externalActionID      = QStringLiteral("motd.insertImage");
		result.externalActionPayload = payload;
		result.externalActionPayload.insert(QStringLiteral("fieldId"), QStringLiteral("motd.html"));
		result.externalActionPayload.insert(QStringLiteral("maximumLength"),
										 m_motdContext.value(QStringLiteral("maximumLength"), 100000));
		return result;
	}

	if (action == QLatin1String("cancel")) {
		m_shortcutCaptureIndex = -1;
		restoreVoiceReplayDraft();
		if (m_runtimePreviewDiffersFromOriginal) {
			result.settingsToApply = m_original;
			result.announceApply   = false;
			m_runtimePreviewDiffersFromOriginal = false;
		}
		if (m_appearancePreviewActive) {
			result.appearanceToPreview = modernAppearancePreview(m_original, m_modernThemeCatalog);
			m_appearancePreviewActive = false;
		}
		cancelInputEnhancementCalibration();
		result.closeDialog = true;
		return result;
	}

	if (action == QLatin1String("reset")) {
		cancelInputEnhancementCalibration();
		m_draft = m_original;
		m_inputEnhancementReadinessUiError.clear();
		m_shortcutCaptureIndex = -1;
		m_voiceReplayPreviousTransmitMode.reset();
		m_voiceReplayPreviousLoopMode.reset();
		m_voiceReplayPreviousPacketLoss.reset();
		m_voiceReplayPreviousMaxPacketDelay.reset();
		if (m_runtimePreviewDiffersFromOriginal) {
			result.settingsToApply = m_original;
			result.announceApply   = false;
			m_runtimePreviewDiffersFromOriginal = false;
		}
		if (m_appearancePreviewActive) {
			result.appearanceToPreview = modernAppearancePreview(m_original, m_modernThemeCatalog);
			m_appearancePreviewActive = false;
		}
		forceModernLayout();
		return result;
	}

	if (action == QLatin1String("playInputEnhancementCalibration")) {
		bool validToken = false;
		const qulonglong token =
			payload.value(QStringLiteral("playbackToken")).toString().toULongLong(&validToken);
		if (!validToken || token == 0) {
			result.stateChanged = false;
			return result;
		}
		result.stateChanged = false;
		result.externalActionID = action;
		// Keep the blind token opaque and discard every other property. In
		// particular, PCM must never be serialized through QVariant/QML.
		result.externalActionPayload =
			QVariantMap { { QStringLiteral("playbackToken"), QString::number(token) } };
		return result;
	}

	if (action == QLatin1String("stopInputEnhancementCalibrationPlayback")) {
		result.stateChanged = false;
		result.externalActionID = action;
		return result;
	}

	if (action == QLatin1String("plugins.toggle")) {
		result.stateChanged = updatePluginDraft(m_draft, payload);
		return result;
	}

	if (action == QLatin1String("messages.toggleEvent")) {
		result.stateChanged = updateMessageEventDraft(m_draft, payload);
		return result;
	}

	if (action.startsWith(QLatin1String("messages."))) {
		result.externalActionID      = action;
		result.externalActionPayload = payload;
		return result;
	}

	if (action.startsWith(QLatin1String("plugins."))) {
		result.externalActionID      = action;
		result.externalActionPayload = payload;
		return result;
	}

	if (action == QLatin1String("look.openModernThemesDirectory")) {
		QString errorMessage;
		if (Mumble::ModernTheme::ensureUserThemeDirectory(&errorMessage)) {
			QDesktopServices::openUrl(QUrl::fromLocalFile(Mumble::ModernTheme::userThemeDirectory().absolutePath()));
		} else if (!errorMessage.trimmed().isEmpty()) {
			qWarning() << "Unable to open Modern themes directory:" << errorMessage;
		}
		return result;
	}

	if (action == QLatin1String("look.reloadModernThemes")) {
		refreshModernThemeCatalog();
		m_draft.qsModernShellTheme =
			normalizedModernShellTheme(m_draft.qsModernShellTheme, m_modernThemeCatalog);
		result.appearanceToPreview = modernAppearancePreview(m_draft, m_modernThemeCatalog);
		m_appearancePreviewActive = true;
		return result;
	}

	if (action == QLatin1String("keys.addShortcut")) {
		Shortcut shortcut;
		shortcut.iIndex    = -1;
		shortcut.bSuppress = false;
		m_draft.qlShortcuts.push_back(shortcut);
		m_shortcutCaptureIndex = -1;
		return result;
	}

	if (action == QLatin1String("keys.removeShortcut")) {
		const int row = shortcutPayloadIndex(payload, m_draft.qlShortcuts);
		if (row >= 0) {
			m_draft.qlShortcuts.removeAt(row);
			if (m_shortcutCaptureIndex == row) {
				m_shortcutCaptureIndex = -1;
			} else if (m_shortcutCaptureIndex > row) {
				--m_shortcutCaptureIndex;
			}
		}
		return result;
	}

	if (action == QLatin1String("keys.shortcutAction")) {
		const int row = shortcutPayloadIndex(payload, m_draft.qlShortcuts);
		if (row >= 0) {
			Shortcut &shortcut = m_draft.qlShortcuts[row];
			shortcut.iIndex    = payload.value(QStringLiteral("actionIndex"), -1).toInt();
			shortcut.qvData    = shortcutDefinition(shortcut.iIndex) ? shortcutDefinition(shortcut.iIndex)->qvDefault
																	 : QVariant();
			normalizeShortcutData(shortcut);
		}
		return result;
	}

	if (action == QLatin1String("keys.shortcutTarget")) {
		const int row = shortcutPayloadIndex(payload, m_draft.qlShortcuts);
		if (row >= 0) {
			Shortcut &shortcut = m_draft.qlShortcuts[row];
			normalizeShortcutData(shortcut);
			const QVariant data = shortcutEffectiveData(shortcut);
			if (shortcutDataType(data) == QLatin1String("target")) {
				ShortcutTarget target = data.value< ShortcutTarget >();
				const QString targetAction = payload.value(QStringLiteral("targetAction")).toString();

				if (targetAction == QLatin1String("mode")) {
					applyShortcutTargetMode(target, payload.value(QStringLiteral("mode")).toString());
				} else if (targetAction == QLatin1String("channel")) {
					target.iChannel = payload.value(QStringLiteral("channelId"), target.iChannel).toInt();
					target.bUsers = false;
					target.bCurrentSelection = false;
				} else if (targetAction == QLatin1String("group")) {
					target.qsGroup = payload.value(QStringLiteral("group")).toString().trimmed();
				} else if (targetAction == QLatin1String("links")) {
					target.bLinks = payload.value(QStringLiteral("enabled")).toBool();
				} else if (targetAction == QLatin1String("children")) {
					target.bChildren = payload.value(QStringLiteral("enabled")).toBool();
				} else if (targetAction == QLatin1String("forceCenter")) {
					target.bForceCenter = payload.value(QStringLiteral("enabled")).toBool();
				} else if (targetAction == QLatin1String("addUser")) {
					const QString hash =
						payload.value(QStringLiteral("hash"), payload.value(QStringLiteral("value"))).toString().trimmed();
					if (!hash.isEmpty() && !target.qlUsers.contains(hash)) {
						target.qlUsers.push_back(hash);
					}
					target.bUsers            = true;
					target.bCurrentSelection = false;
				} else if (targetAction == QLatin1String("removeUser")) {
					const QString hash =
						payload.value(QStringLiteral("hash"), payload.value(QStringLiteral("value"))).toString().trimmed();
					target.qlUsers.removeAll(hash);
				}

				shortcut.qvData = QVariant::fromValue(target);
			}
		}
		return result;
	}

	if (action == QLatin1String("keys.shortcutData")) {
		const int row = shortcutPayloadIndex(payload, m_draft.qlShortcuts);
		if (row >= 0) {
			Shortcut &shortcut = m_draft.qlShortcuts[row];
			normalizeShortcutData(shortcut);
			const QVariant data = shortcutEffectiveData(shortcut);
			const QString type  = shortcutDataType(data);
			const QVariant value = payload.value(QStringLiteral("value"));
			if (type == QLatin1String("toggle")) {
				shortcut.qvData = qBound(-1, value.toInt(), 1);
			} else if (type == QLatin1String("text")) {
				shortcut.qvData = value.toString();
			} else if (type == QLatin1String("channel")) {
				const int channelID = qMax(0, value.toInt());
				shortcut.qvData    = QVariant::fromValue(ChannelTarget(static_cast< unsigned int >(channelID)));
			} else if (type == QLatin1String("target")) {
				shortcut.qvData = QVariant::fromValue(shortcutTargetFromMode(value.toString()));
			}
		}
		return result;
	}

	if (action == QLatin1String("keys.shortcutSuppress")) {
		const int row = shortcutPayloadIndex(payload, m_draft.qlShortcuts);
		if (row >= 0) {
			m_draft.qlShortcuts[row].bSuppress = payload.value(QStringLiteral("suppress")).toBool();
		}
		return result;
	}

	if (action == QLatin1String("keys.beginShortcutCapture")) {
		m_shortcutCaptureIndex = shortcutPayloadIndex(payload, m_draft.qlShortcuts);
		return result;
	}

	if (action == QLatin1String("keys.cancelShortcutCapture")) {
		m_shortcutCaptureIndex = -1;
		return result;
	}

	if (action == QLatin1String("keys.finishShortcutCapture")) {
		const int row = shortcutPayloadIndex(payload, m_draft.qlShortcuts);
		if (row >= 0) {
			m_draft.qlShortcuts[row].qlButtons = payload.value(QStringLiteral("buttons")).toList();
		}
		m_shortcutCaptureIndex = -1;
		return result;
	}

	if (action == QLatin1String("keys.clearShortcut")) {
		const int row = shortcutPayloadIndex(payload, m_draft.qlShortcuts);
		if (row >= 0) {
			m_draft.qlShortcuts[row].qlButtons.clear();
			if (m_shortcutCaptureIndex == row) {
				m_shortcutCaptureIndex = -1;
			}
		}
		return result;
	}

	if (action == QLatin1String("startInputEnhancementCalibration")) {
		if (m_voiceReplayPreviousTransmitMode) {
			m_inputEnhancementCalibrationUiError =
				QObject::tr("Stop microphone replay before starting input calibration.");
			result.stateChanged = true;
			return result;
		}
		const AudioInputPtr input = currentAudioInput();
		if (!input) {
			result.stateChanged = false;
			return result;
		}
		const Mumble::InputEnhancement::DeviceIdentity runningIdentity = input->inputDeviceIdentity();
		m_inputEnhancementCalibrationWorker->reset();
		m_inputEnhancementCalibrationUiError.clear();
		const Mumble::InputEnhancement::DefaultPreference candidateControls =
			Mumble::InputEnhancement::preferenceForDevice(m_draft.inputEnhancement, runningIdentity);
		m_inputEnhancementCalibrationControls = candidateControls;
		std::uint64_t seed = payload.value(QStringLiteral("blindSeed")).toULongLong();
		if (seed == 0) {
			seed = QRandomGenerator::global()->generate64();
		}
		AudioInput::InputEnhancementCalibrationStartError startError =
			AudioInput::InputEnhancementCalibrationStartError::None;
		result.stateChanged = input->startInputEnhancementCalibration(
			candidateControls, payload.value(QStringLiteral("captureOptionalLocalNoise"), false).toBool(), seed,
			&startError);
		if (!result.stateChanged) {
			m_inputEnhancementCalibrationControls.reset();
			switch (startError) {
				case AudioInput::InputEnhancementCalibrationStartError::AlreadyRunning:
					m_inputEnhancementCalibrationUiError = QObject::tr("Input calibration is already running.");
					break;
				case AudioInput::InputEnhancementCalibrationStartError::AutoUnsupported:
					m_inputEnhancementCalibrationUiError = autoCalibrationUnavailableText();
					break;
				case AudioInput::InputEnhancementCalibrationStartError::LegacyOverrideActive:
					m_inputEnhancementCalibrationUiError = QObject::tr(
						"Apply a product input profile before calibration. The active legacy mode cannot be used as an exact rollback baseline.");
					break;
				case AudioInput::InputEnhancementCalibrationStartError::ActiveRecipeUnhealthy:
					m_inputEnhancementCalibrationUiError = QObject::tr(
						"The active input-enhancement recipe is not healthy. Apply or restart a verified profile before calibration.");
					break;
				case AudioInput::InputEnhancementCalibrationStartError::MissingExactActiveRecipe:
					m_inputEnhancementCalibrationUiError = QObject::tr(
						"Calibration needs the exact healthy recipe running on this microphone. Apply the current input settings and try again.");
					break;
				case AudioInput::InputEnhancementCalibrationStartError::RuntimeRejected:
				case AudioInput::InputEnhancementCalibrationStartError::None:
					m_inputEnhancementCalibrationUiError =
						QObject::tr("Input calibration could not start. No audio settings were changed.");
					break;
			}
			// Publish the fail-closed reason even though no runtime state changed.
			result.stateChanged = true;
		}
		return result;
	}

	if (action == QLatin1String("advanceInputEnhancementCalibration")) {
		const AudioInputPtr input = currentAudioInput();
		auto *runtime             = input ? input->inputEnhancementCalibrationRuntime() : nullptr;
		result.stateChanged       = runtime && runtime->advance();
		if (result.stateChanged && runtime
			&& runtime->state() == Mumble::InputEnhancement::CalibrationSession::State::Evaluating
			&& m_inputEnhancementCalibrationControls) {
			const auto candidates = Mumble::InputEnhancement::CalibrationRuntimeBridge::standardCandidateSet(
				*m_inputEnhancementCalibrationControls);
			m_inputEnhancementCalibrationUiError.clear();
			m_inputEnhancementCalibrationWorker->start(input, candidates);
		}
		return result;
	}

	if (action == QLatin1String("skipInputEnhancementCalibrationNoise")) {
		const AudioInputPtr input = currentAudioInput();
		auto *runtime             = input ? input->inputEnhancementCalibrationRuntime() : nullptr;
		result.stateChanged       = runtime && runtime->skipOptionalLocalNoise();
		if (result.stateChanged && runtime
			&& runtime->state() == Mumble::InputEnhancement::CalibrationSession::State::Evaluating
			&& m_inputEnhancementCalibrationControls) {
			const auto candidates = Mumble::InputEnhancement::CalibrationRuntimeBridge::standardCandidateSet(
				*m_inputEnhancementCalibrationControls);
			m_inputEnhancementCalibrationUiError.clear();
			m_inputEnhancementCalibrationWorker->start(input, candidates);
		}
		return result;
	}

	if (action == QLatin1String("evaluateInputEnhancementCalibration")) {
		const AudioInputPtr input = currentAudioInput();
		auto *runtime             = input ? input->inputEnhancementCalibrationRuntime() : nullptr;
		if (!runtime || !m_inputEnhancementCalibrationControls) {
			result.stateChanged = false;
			return result;
		}
		const auto candidates = Mumble::InputEnhancement::CalibrationRuntimeBridge::standardCandidateSet(
			*m_inputEnhancementCalibrationControls);
		m_inputEnhancementCalibrationUiError.clear();
		result.stateChanged = m_inputEnhancementCalibrationWorker->start(input, candidates);
		return result;
	}

	if (action == QLatin1String("refreshInputEnhancementCalibration")) {
		if (const AudioInputPtr input = currentAudioInput()) {
			input->synchronizeInputEnhancementCalibrationTransmissionBlock();
		}
		return result;
	}

	if (action == QLatin1String("selectInputEnhancementCalibration")) {
		const AudioInputPtr input = currentAudioInput();
		auto *runtime             = input ? input->inputEnhancementCalibrationRuntime() : nullptr;
		result.stateChanged = runtime && runtime->selectBlindWinner(
			payload.value(QStringLiteral("playbackToken")).toString().toULongLong());
		return result;
	}

	if (action == QLatin1String("applyInputEnhancementCalibration")) {
		const AudioInputPtr input = currentAudioInput();
		if (!input || !input->applyInputEnhancementCalibration(m_draft.inputEnhancement,
															 QDateTime::currentMSecsSinceEpoch())) {
			result.stateChanged = false;
			return result;
		}
		input->synchronizeInputEnhancementCalibrationTransmissionBlock();
		m_inputEnhancementCalibrationControls.reset();
		m_inputEnhancementCalibrationUiError.clear();
		projectInputEnhancementPreference(m_draft);
		result.settingsToApply = m_draft;
		// Calibration Apply arms crash-recoverable probation. Route it through
		// the existing durable settings-save branch even though the dialog stays
		// open for its probation status and Undo affordance.
		result.accepted = true;
		return result;
	}

	if (action == QLatin1String("cancelInputEnhancementCalibration")) {
		const AudioInputPtr input = currentAudioInput();
		auto *runtime             = input ? input->inputEnhancementCalibrationRuntime() : nullptr;
		const auto worker = m_inputEnhancementCalibrationWorker->snapshot();
		if (worker.active()) {
			result.stateChanged = m_inputEnhancementCalibrationWorker->cancel();
		} else {
			result.stateChanged = runtime && runtime->cancel();
			if (input) {
				input->synchronizeInputEnhancementCalibrationTransmissionBlock();
			}
		}
		m_inputEnhancementCalibrationControls.reset();
		return result;
	}

	if (action == QLatin1String("undoInputEnhancementRollback")) {
		const AudioInputPtr input = currentAudioInput();
		if (!input || !input->undoInputEnhancementProbationRollback(m_draft.inputEnhancement)) {
			result.stateChanged = false;
			return result;
		}
		projectInputEnhancementPreference(m_draft);
		result.settingsToApply = m_draft;
		// Undo starts a fresh pending validation and must be on disk before the
		// restarted audio path can fail or the process can terminate.
		result.accepted = true;
		return result;
	}

	if (action == QLatin1String("finishAudioSetupWizard") || action == QLatin1String("autoSetVoiceActivation")) {
		int silenceThreshold = qBound(0, payload.value(QStringLiteral("silenceThreshold")).toInt(), 100);
		int speechThreshold  = qBound(0, payload.value(QStringLiteral("speechThreshold")).toInt(), 100);
		const Settings::VADSource vadSource =
			static_cast< Settings::VADSource >(payload.value(QStringLiteral("vadSource"),
															 static_cast< int >(m_draft.vsVAD))
												   .toInt());
		const int voiceHold = qBound(5, payload.value(QStringLiteral("voiceHold"), m_draft.iVoiceHold).toInt(), 80);

		if (speechThreshold < silenceThreshold) {
			std::swap(silenceThreshold, speechThreshold);
		}
		if (speechThreshold <= silenceThreshold) {
			speechThreshold  = qMin(100, silenceThreshold + 1);
			silenceThreshold = qMax(0, speechThreshold - 1);
		}

		// Applying recommendations must never silently turn PTT or continuous
		// transmission into voice activation. The legacy action keeps its
		// explicit VAD semantics, while the settings action only updates the
		// draft and still requires Apply/OK.
		if (action == QLatin1String("autoSetVoiceActivation")) {
			m_draft.atTransmit = Settings::VAD;
		}
		if (vadSource == Settings::SignalToNoise || vadSource == Settings::Hybrid || vadSource == Settings::Amplitude) {
			m_draft.vsVAD = vadSource;
		}
		m_draft.fVADmin    = vadThresholdFromPercent(silenceThreshold);
		m_draft.fVADmax    = vadThresholdFromPercent(speechThreshold);
		m_draft.iVoiceHold = voiceHold;
		if (payload.contains(QStringLiteral("maxAmplification"))) {
			m_draft.iMinLoudness = minLoudnessFromAmplification(payload.value(QStringLiteral("maxAmplification")));
		}
		if (payload.contains(QStringLiteral("noiseCancelMode"))) {
			Settings::NoiseCancel requestedNoiseCancel =
				static_cast< Settings::NoiseCancel >(payload.value(QStringLiteral("noiseCancelMode")).toInt());
			if (requestedNoiseCancel < Settings::NoiseCancelOff || requestedNoiseCancel > Settings::NoiseCancelBoth) {
				requestedNoiseCancel = m_draft.noiseCancelMode;
			}
			if ((requestedNoiseCancel == Settings::NoiseCancelRNN || requestedNoiseCancel == Settings::NoiseCancelBoth)
				&& !hasAvailableSpeechCleanupBackend()) {
				requestedNoiseCancel = Settings::NoiseCancelSpeex;
			}
			m_draft.noiseCancelMode = requestedNoiseCancel;
		}
		if (payload.contains(QStringLiteral("speexNoiseStrength"))) {
			const int strength = qBound(0, payload.value(QStringLiteral("speexNoiseStrength")).toInt(), 100);
			m_draft.iSpeexNoiseCancelStrength = strength == 14 ? 0 : -strength;
		}
		if (payload.contains(QStringLiteral("inputGateMode"))) {
			m_draft.inputGateMode = static_cast< Settings::InputGateMode >(
				qBound(0, payload.value(QStringLiteral("inputGateMode")).toInt(),
					   static_cast< int >(Settings::InputGateStrict)));
		}
		if (payload.contains(QStringLiteral("noiseCancelMode"))
			|| payload.contains(QStringLiteral("speexNoiseStrength"))) {
			captureLegacyInputOverride(m_draft);
		}
		forceModernLayout();
		return result;
	}

	if (action == QLatin1String("startVoiceReplay")) {
		const AudioInputPtr input = currentAudioInput();
		const auto *runtime = input ? input->inputEnhancementCalibrationRuntime() : nullptr;
		if (m_inputEnhancementCalibrationWorker->snapshot().active()
			|| (runtime && runtime->transmissionBlocked())) {
			m_inputEnhancementCalibrationUiError =
				QObject::tr("Cancel or finish input calibration before starting microphone replay.");
			result.stateChanged = true;
			return result;
		}
		if (!m_voiceReplayPreviousTransmitMode) {
			m_voiceReplayPreviousTransmitMode = m_draft.atTransmit;
			m_voiceReplayPreviousLoopMode      = m_draft.lmLoopMode;
			m_voiceReplayPreviousPacketLoss    = m_draft.dPacketLoss;
			m_voiceReplayPreviousMaxPacketDelay = m_draft.dMaxPacketDelay;
		}
		const QString mode = payload.value(QStringLiteral("mode")).toString();
		m_draft.lmLoopMode = mode == QLatin1String("server") ? Settings::Server : Settings::Local;
		m_draft.dPacketLoss = 0.0f;
		m_draft.dMaxPacketDelay = 0.0f;
		m_draft.atTransmit = Settings::VAD;
		forceModernLayout();
		result.settingsToApply = m_draft;
		result.announceApply   = false;
		m_runtimePreviewDiffersFromOriginal = true;
		return result;
	}

	if (action == QLatin1String("stopVoiceReplay")) {
		restoreVoiceReplayDraft();
		forceModernLayout();
		result.settingsToApply = m_draft;
		result.announceApply   = false;
		m_runtimePreviewDiffersFromOriginal = true;
		return result;
	}

	if (action == QLatin1String("apply") || action == QLatin1String("ok")) {
		const bool inputEnhancementChanged = inputEnhancementSelectionChanged(m_draft, m_original);
		const bool probationReconciled = reconcileResolvedInputEnhancementProbation(m_draft, m_original);
		const Mumble::InputEnhancement::DefaultPreference &preference =
			currentInputEnhancementPreference(m_draft);
		// A migration-preserved legacy tuple is the active runtime contract until
		// the user explicitly selects a product profile. Do not block unrelated
		// settings changes by preflighting the merely descriptive mapped profile.
		if (!currentInputEnhancementUsesLegacyOverride(m_draft)) {
			const InputEnhancementSettingsReadiness readiness = inputEnhancementReadinessForSettings(
				m_draft, preference.profile, preference.reduction, preference.character);
			const bool onlyTemporarilyRuntimeBlocked =
				readiness.processingSelectable
				&& readiness.runtimeBlockReason
					   != Mumble::InputEnhancement::EnhancedRuntimeBlockReason::None;
			if (!readiness.selectable && (!onlyTemporarilyRuntimeBlocked || inputEnhancementChanged)) {
				m_inputEnhancementReadinessUiError = QObject::tr("Input enhancement settings were not saved: %1")
											 .arg(inputEnhancementReadinessReasonText(readiness));
				result.accepted    = false;
				result.closeDialog = false;
				return result;
			}
			if (readiness.processingSelectable && !probationReconciled
				&& !prepareManualInputEnhancementProbation(
					m_draft, m_original, QDateTime::currentMSecsSinceEpoch())) {
				m_inputEnhancementReadinessUiError = QObject::tr(
					"Input enhancement settings were not saved: the selected profile could not be bound to the "
					"verified recipe package for safe rollback.");
				result.accepted    = false;
				result.closeDialog = false;
				return result;
			}
		}
		m_inputEnhancementReadinessUiError.clear();
		m_shortcutCaptureIndex = -1;
		restoreVoiceReplayDraft();
		cancelInputEnhancementCalibration();
		refreshShortcutRestartFlag();
		forceModernLayout();
		result.settingsToApply = m_draft;
		if (m_motdContext.value(QStringLiteral("available")).toBool()
			&& m_motdContext.value(QStringLiteral("canEdit")).toBool()
			&& m_motdContext.value(QStringLiteral("html")).toString()
				   != m_motdContext.value(QStringLiteral("originalHtml")).toString()) {
			result.externalActionID = QStringLiteral("motd.save");
			result.externalActionPayload.insert(
				QStringLiteral("html"), m_motdContext.value(QStringLiteral("html")).toString());
		}
		// Apply commits and persists the current baseline without closing;
		// Done performs the same commit and then closes the dialog.
		result.accepted        = true;
		result.closeDialog     = action == QLatin1String("ok");
		m_original             = m_draft;
		m_runtimePreviewDiffersFromOriginal = false;
		m_appearancePreviewActive = false;
		return result;
	}

	result.stateChanged = false;
	return result;
}

const Settings &ModernSettingsController::draft() const {
	return m_draft;
}

QString ModernSettingsController::activePage() const {
	return m_activePage;
}

QVariantList ModernSettingsController::pages() const {
	const auto page = [this](const QString &id, const QString &label, const bool contentFitCompact = false) {
		QVariantMap item;
		item.insert(QStringLiteral("id"), id);
		item.insert(QStringLiteral("label"), label);
		item.insert(QStringLiteral("selected"), id == m_activePage);
		if (contentFitCompact) {
			item.insert(QStringLiteral("contentFitCompact"), true);
		}
		return item;
	};

	QVariantList result { page(QStringLiteral("audioInput"), QObject::tr("Audio Input")),
						  page(QStringLiteral("audioOutput"), QObject::tr("Audio Output")),
						  page(QStringLiteral("look"), QObject::tr("Appearance")),
						  page(QStringLiteral("ui"), QObject::tr("User Interface")),
						  page(QStringLiteral("messages"), QObject::tr("Messages & Sounds")),
						  page(QStringLiteral("stonks"), QObject::tr("Stonks")) };
	if (m_motdContext.value(QStringLiteral("available")).toBool()) {
		result.push_back(page(QStringLiteral("motd"), QObject::tr("Server MOTD"), true));
	}
	result.append(QVariantList {
						  page(QStringLiteral("keys"), QObject::tr("Key Bindings")),
						  page(QStringLiteral("network"), QObject::tr("Network")),
						  page(QStringLiteral("screenShare"), QObject::tr("Screen Sharing"), true),
						  page(QStringLiteral("plugins"), QObject::tr("Plugins")),
						  page(QStringLiteral("about"), QObject::tr("About")) });
	return result;
}

QVariantList ModernSettingsController::sectionsForActivePage() const {
	if (m_activePage == QLatin1String("motd")) {
		const bool canEdit = m_motdContext.value(QStringLiteral("canEdit")).toBool();
		QVariantMap editor = fieldItem(QStringLiteral("motd.html"), QObject::tr("Message of the day"),
									  QStringLiteral("motdEditor"),
									  m_motdContext.value(QStringLiteral("html")).toString());
		editor.insert(QStringLiteral("originalValue"),
					  m_motdContext.value(QStringLiteral("originalHtml")).toString());
		editor.insert(QStringLiteral("canEdit"), canEdit);
		editor.insert(QStringLiteral("enabled"), canEdit);
		editor.insert(QStringLiteral("showSaveAction"), canEdit);
		editor.insert(QStringLiteral("maximumLength"),
						  m_motdContext.value(QStringLiteral("maximumLength"), 100000));
		editor.insert(QStringLiteral("previewSourceHtml"),
						  m_motdContext.value(QStringLiteral("previewSourceHtml")).toString());
		editor.insert(QStringLiteral("previewBlocks"),
						  m_motdContext.value(QStringLiteral("previewBlocks")).toList());
		editor.insert(QStringLiteral("previewSummary"),
						  m_motdContext.value(QStringLiteral("previewSummary")).toString());
		editor.insert(QStringLiteral("hint"),
					  canEdit
						  ? QObject::tr("Uses the standard Mumble welcome_text setting; original Mumble clients receive the same MOTD.")
						  : QObject::tr("You can view this MOTD, but Root Write permission is required to edit it."));
		QString sectionTitle = QObject::tr("Connected server");
		const QString serverName = m_motdContext.value(QStringLiteral("serverName")).toString().trimmed();
		if (!serverName.isEmpty()) {
			sectionTitle = serverName;
		}
		return QVariantList { sectionItem(sectionTitle, QVariantList { editor }) };
	}

	if (m_activePage == QLatin1String("ui")) {
		return QVariantList {
			sectionItem(QObject::tr("Window behavior"), QVariantList {
												   selectField(QStringLiteral("look.quitBehavior"),
															   QObject::tr("Close button"),
															   static_cast< int >(m_draft.quitBehavior),
															   quitBehaviorOptions()),
												   selectField(QStringLiteral("look.alwaysOnTop"),
															   QObject::tr("Always on top"),
															   static_cast< int >(m_draft.aotbAlwaysOnTop),
															   alwaysOnTopOptions()),
												   boolField(QStringLiteral("look.hideInTray"),
															 QObject::tr("Hide in tray when minimized"),
															 m_draft.bHideInTray),
											   boolField(QStringLiteral("look.stateInTray"),
														 QObject::tr("Show talking state in tray"),
														 m_draft.bStateInTray) }),
			sectionItem(QObject::tr("Room browser and presence"), QVariantList {
															 boolField(QStringLiteral("look.showVolumeAdjustments"),
																	   QObject::tr("Show local volume badges"),
																	   m_draft.bShowVolumeAdjustments),
															 boolField(QStringLiteral("look.showNicknamesOnly"),
																	   QObject::tr("Show nicknames only"),
																	   m_draft.bShowNicknamesOnly),
													 boolField(QStringLiteral("look.filterHidesEmptyChannels"),
																	   QObject::tr("Filter hides empty rooms"),
																	   m_draft.bFilterHidesEmptyChannels),
															 selectField(QStringLiteral("look.presenceIdleTimeout"),
																		 QObject::tr("Idle presence timeout"),
																		 m_draft.iPresenceIdleTimeoutMinutes,
																		 presenceTimeoutOptions()) })
		};
	}

	if (m_activePage == QLatin1String("messages")) {
		return QVariantList {
			sectionItem(QObject::tr("Messages"), QVariantList {
										 boolField(QStringLiteral("network.linkPreviews"),
												   QObject::tr("Enable link previews"),
												   m_draft.bEnableLinkPreviews),
										 boolField(QStringLiteral("messages.clock24Hour"), QObject::tr("Use 24-hour timestamps"),
												   m_draft.bLog24HourClock),
										 boolField(QStringLiteral("messages.whisperFriends"),
												   QObject::tr("Treat friends as whisper targets"), m_draft.bWhisperFriends),
										 numberField(QStringLiteral("messages.limitThreshold"),
													 QObject::tr("Message-limit user threshold"),
													 m_draft.iMessageLimitUserThreshold, 0, 10000) }),
			sectionItem(QObject::tr("Text to speech"), QVariantList {
				boolField(QStringLiteral("messages.ttsEnabled"), QObject::tr("Enable text to speech"), m_draft.bTTS),
				rangeField(QStringLiteral("messages.ttsVolume"), QObject::tr("TTS volume"), m_draft.iTTSVolume,
						   0, 100, 1, QStringLiteral("%")),
				numberField(QStringLiteral("messages.ttsThreshold"), QObject::tr("Maximum spoken message length"),
							m_draft.iTTSThreshold, 0, 10000),
				boolField(QStringLiteral("messages.ttsReadOwn"), QObject::tr("Read back my own messages"),
						  m_draft.bTTSMessageReadBack),
				boolField(QStringLiteral("messages.ttsNoScope"), QObject::tr("Omit message scope"), m_draft.bTTSNoScope),
				boolField(QStringLiteral("messages.ttsNoAuthor"), QObject::tr("Omit message author"), m_draft.bTTSNoAuthor),
				fieldItem(QStringLiteral("messages.ttsLanguage"), QObject::tr("TTS language (BCP47)"),
						  QStringLiteral("text"), m_draft.qsTTSLanguage) }),
			sectionItem(QObject::tr("Sounds"), QVariantList {
										   rangeField(QStringLiteral("messages.notificationVolume"),
													  QObject::tr("Notification volume"),
													  percentFromFloat(m_draft.notificationVolume), 0, 100, 1,
													  QStringLiteral("%")),
										   rangeField(QStringLiteral("messages.cueVolume"), QObject::tr("Cue volume"),
													  percentFromFloat(m_draft.cueVolume), 0, 100, 1,
													  QStringLiteral("%")),
										   boolField(QStringLiteral("audio.cuePtt"),
													 QObject::tr("Play transmit cue for push-to-talk"),
													 m_draft.audioCueEnabledPTT),
										   boolField(QStringLiteral("audio.cueVad"),
													 QObject::tr("Play transmit cue for voice activity"),
													 m_draft.audioCueEnabledVAD),
										   fieldItem(QStringLiteral("audio.cueOnPath"),
													 QObject::tr("Transmit cue on file"),
													 QStringLiteral("text"), m_draft.qsTxAudioCueOn),
										   fieldItem(QStringLiteral("audio.cueOffPath"),
													 QObject::tr("Transmit cue off file"),
													 QStringLiteral("text"), m_draft.qsTxAudioCueOff),
										   boolField(QStringLiteral("audio.muteCue"),
													 QObject::tr("Play mute cue"),
													 m_draft.bTxMuteCue),
										   fieldItem(QStringLiteral("audio.muteCuePath"),
													 QObject::tr("Mute cue file"),
													 QStringLiteral("text"), m_draft.qsTxMuteCue) }),
			sectionItem(QObject::tr("Per-event behavior"), QVariantList { messageEventEditorField(m_draft) })
		};
	}

	if (m_activePage == QLatin1String("stonks")) {
		const QVariantList placementOptions {
			optionItem(QStringLiteral("windowTop"), QObject::tr("Top of window")),
			optionItem(QStringLiteral("top"), QObject::tr("Top of content")),
			optionItem(QStringLiteral("aboveComposer"), QObject::tr("Above message field")),
			optionItem(QStringLiteral("bottom"), QObject::tr("Bottom of window"))
		};
		const QVariantList directionOptions {
			optionItem(QStringLiteral("left"), QObject::tr("Move left")),
			optionItem(QStringLiteral("right"), QObject::tr("Move right")),
			optionItem(QStringLiteral("up"), QObject::tr("Move up")),
			optionItem(QStringLiteral("down"), QObject::tr("Move down"))
		};
		const QVariantList speedOptions {
			optionItem(QStringLiteral("verySlow"), QObject::tr("Very slow")),
			optionItem(QStringLiteral("slow"), QObject::tr("Slow")),
			optionItem(QStringLiteral("normal"), QObject::tr("Normal")),
			optionItem(QStringLiteral("fast"), QObject::tr("Fast"))
		};
		QVariantList sections {
			sectionItem(QObject::tr("Portfolio"), QVariantList {
				boolField(QStringLiteral("stonks.client.profileShortcutVisible"),
						  QObject::tr("Show Stonks in the profile card"),
						  m_draft.bModernShellStonksProfileShortcutVisible),
				enabledField(actionField(QStringLiteral("stonks.client.openPortfolio"),
									 QObject::tr("Portfolio, leaderboard, following, and pins"),
									 QObject::tr("Open portfolio"), QStringLiteral("stonks.openPortfolio"),
									 QStringLiteral("accent")),
						 m_stonksContext.value(QStringLiteral("connected")).toBool()
							 && m_stonksContext.value(QStringLiteral("supported")).toBool())
			}),
			sectionItem(QObject::tr("Ticker strip"), QVariantList {
				boolField(QStringLiteral("stonks.client.enabled"), QObject::tr("Show the Stonks ticker"),
						  m_draft.bModernShellTickerBannerEnabled),
				selectField(QStringLiteral("stonks.client.placement"), QObject::tr("Placement"),
							normalizedStonksTickerPlacement(m_draft.qsModernShellTickerPlacement),
							placementOptions, QStringLiteral("string")),
				selectField(QStringLiteral("stonks.client.direction"), QObject::tr("Direction"),
							normalizedStonksTickerDirection(m_draft.qsModernShellTickerDirection),
							directionOptions, QStringLiteral("string")),
				selectField(QStringLiteral("stonks.client.speed"), QObject::tr("Speed"),
							normalizedStonksTickerSpeed(m_draft.qsModernShellTickerSpeed),
							speedOptions, QStringLiteral("string")),
				noteField(QObject::tr("The ticker is off by default. Changes are saved and shown immediately."))
			})
		};

		if (m_stonksContext.value(QStringLiteral("connected")).toBool()
			&& m_stonksContext.value(QStringLiteral("supported")).toBool()
			&& m_stonksContext.value(QStringLiteral("canAdmin")).toBool()) {
			QVariantList channelOptions {
				optionItem(QVariant::fromValue< uint >(0), QObject::tr("Automatic (#stonks)"))
			};
			for (const QVariant &channelValue : m_stonksContext.value(QStringLiteral("textChannels")).toList()) {
				const QVariantMap channel = channelValue.toMap();
				const uint channelID = channel.value(QStringLiteral("textChannelId")).toUInt();
				if (channelID == 0) {
					continue;
				}
				QString label = channel.value(QStringLiteral("name")).toString().trimmed();
				if (label.isEmpty()) {
					label = QObject::tr("Text room %1").arg(channelID);
				} else if (!label.startsWith(QLatin1Char('#'))) {
					label.prepend(QLatin1Char('#'));
				}
				channelOptions.push_back(optionItem(QVariant::fromValue< uint >(channelID), label));
			}
			sections.push_back(sectionItem(QObject::tr("Server administration"), QVariantList {
				boolField(QStringLiteral("stonks.server.enabled"), QObject::tr("Enable Stonks on this server"),
						  m_stonksContext.value(QStringLiteral("enabled"), true).toBool()),
				boolField(QStringLiteral("stonks.server.socialAnnouncementsEnabled"),
						  QObject::tr("Post social announcements"),
						  m_stonksContext.value(QStringLiteral("socialAnnouncementsEnabled"), true).toBool()),
				selectField(QStringLiteral("stonks.server.textChannelId"), QObject::tr("Announcement room"),
							m_stonksContext.value(QStringLiteral("textChannelId")).toUInt(), channelOptions),
				actionField(QStringLiteral("stonks.server.apply"), QObject::tr("Server configuration"),
							QObject::tr("Apply to server"), QStringLiteral("stonks.applyServer"),
							QStringLiteral("accent")),
				noteField(QObject::tr("These controls are shown only when the server grants you Stonks administration permission."))
			}));
		}
		return sections;
	}

	if (m_activePage == QLatin1String("keys")) {
		return QVariantList {
			sectionItem(QObject::tr("Shortcut controls"), QVariantList {
													 boolField(QStringLiteral("keys.globalShortcuts"),
															   QObject::tr("Enable global shortcuts"),
															   m_draft.bShortcutEnable) }),
			sectionItem(QObject::tr("Configured shortcuts"), QVariantList { shortcutEditorField(m_draft, m_shortcutCaptureIndex) }),
			advancedSection(sectionItem(QObject::tr("Additional shortcut engines"), QVariantList {
										 boolField(QStringLiteral("keys.enableUiAccess"),
												   QObject::tr("Enable shortcuts in privileged applications"),
												   m_draft.bEnableUIAccess),
										 boolField(QStringLiteral("keys.enableGKey"),
												   QObject::tr("Enable GKey"),
												   m_draft.bEnableGKey),
										 boolField(QStringLiteral("keys.enableXboxInput"),
												   QObject::tr("Enable XInput"),
												   m_draft.bEnableXboxInput) }))
		};
	}

	if (m_activePage == QLatin1String("about")) {
		return QVariantList {
			sectionItem(QObject::tr("Mumble"), QVariantList {
										 readonlyField(QObject::tr("Version"), Version::getRelease()),
										 readonlyField(QObject::tr("Architecture"), buildArchitectureLabel()),
										 actionField(QStringLiteral("about.openMumble"),
													 QObject::tr("Project, license, and credits"),
													 QObject::tr("Open About Mumble"),
													 QStringLiteral("about.openMumble"), QStringLiteral("accent")) }),
			sectionItem(QObject::tr("Qt runtime"), QVariantList {
											 readonlyField(QObject::tr("Qt version"), QString::fromLatin1(qVersion())),
											 readonlyField(QObject::tr("Operating system"), QSysInfo::prettyProductName()),
											 actionField(QStringLiteral("about.openQt"),
														 QObject::tr("Qt details"),
														 QObject::tr("Open About Qt"),
														 QStringLiteral("about.openQt")) })
		};
	}

	if (m_activePage == QLatin1String("plugins")) {
		return QVariantList {
			sectionItem(QObject::tr("Positional audio"), QVariantList {
				boolField(QStringLiteral("plugins.transmitPosition"),
						  QObject::tr("Transmit positional information to the server"),
						  m_draft.bTransmitPosition),
				noteField(QObject::tr("Each plugin can additionally be granted positional-audio and keyboard-monitoring permissions below.")) }),
			sectionItem(QObject::tr("Plugin manager"), QVariantList { pluginEditorField(m_draft) })
		};
	}

	if (m_activePage == QLatin1String("network")) {
		return QVariantList {
			sectionItem(QObject::tr("Client connection"), QVariantList {
													 boolField(QStringLiteral("network.autoReconnect"),
															   QObject::tr("Reconnect automatically"), m_draft.bReconnect),
													 boolField(QStringLiteral("network.autoConnect"),
															   QObject::tr("Connect to the last server on startup"),
															   m_draft.bAutoConnect),
													 boolField(QStringLiteral("network.reconnectToLastChannel"),
															   QObject::tr("Reconnect to last known channel within server"),
															   m_draft.bReconnectToLastChannel),
													 boolField(QStringLiteral("network.startWithPC"),
															   QObject::tr("Start Mumble with Windows"),
															   m_draft.bStartWithPC),
													 advancedField(boolField(QStringLiteral("network.tcpMode"),
																			 QObject::tr("Force TCP mode"),
																			 m_draft.bTCPCompat)),
													 advancedField(boolField(QStringLiteral("network.qos"),
																			 QObject::tr("Use Quality of Service"),
																			 m_draft.bQoS)) }),
			advancedSection(sectionItem(QObject::tr("Chat media cache"), QVariantList {
														   hintedField(
															   tooltippedField(
																   actionField(QStringLiteral("network.clearPreviewCache"),
																			   QObject::tr("Local media cache"),
																			   QObject::tr("Clear cache"),
																			   QStringLiteral("network.clearPreviewCache"),
																			   QStringLiteral("danger")),
																   fieldTooltip(QStringLiteral("network.clearPreviewCache"))),
															   QObject::tr("Current cache: %1. Stored only on this device.")
																   .arg(PersistentChatMediaCache::formattedSize(
																	   PersistentChatMediaCache::sizeBytes()))) })),
			advancedSection(sectionItem(QObject::tr("Proxy and privacy"), QVariantList {
														   selectField(QStringLiteral("network.proxyType"),
																	   QObject::tr("Proxy type"),
																	   static_cast< int >(m_draft.ptProxyType),
																	   proxyOptions()),
														   fieldItem(QStringLiteral("network.proxyHost"),
																	 QObject::tr("Proxy host"),
																	 QStringLiteral("text"), m_draft.qsProxyHost),
														   numberField(QStringLiteral("network.proxyPort"),
																	   QObject::tr("Proxy port"),
																	   m_draft.usProxyPort, 0, 65535),
														   fieldItem(QStringLiteral("network.proxyUsername"),
																	 QObject::tr("Proxy username"),
																	 QStringLiteral("text"), m_draft.qsProxyUsername),
														   fieldItem(QStringLiteral("network.proxyPassword"),
																	 QObject::tr("Proxy password"),
																	 QStringLiteral("password"), m_draft.qsProxyPassword),
														   boolField(QStringLiteral("network.suppressIdentity"),
																	 QObject::tr("Suppress certificate identity"),
																	 m_draft.bSuppressIdentity),
														   boolField(QStringLiteral("network.hideOS"),
																	 QObject::tr("Hide operating system from servers"),
																	 m_draft.bHideOS) })),
			advancedSection(sectionItem(QObject::tr("Updates and advertised version"), QVariantList {
																		boolField(QStringLiteral("network.updateCheck"),
																				  QObject::tr("Check for client updates"),
																				  m_draft.bUpdateCheck),
																		boolField(QStringLiteral("network.pluginCheck"),
																				  QObject::tr("Check for plugin updates"),
																				  m_draft.bPluginCheck),
																		boolField(QStringLiteral("network.pluginAutoUpdate"),
																				  QObject::tr("Update plugins automatically"),
																				  m_draft.bPluginAutoUpdate),
																		fieldItem(QStringLiteral("network.advertisedRelease"),
																				  QObject::tr("Advertised release"),
																				  QStringLiteral("text"),
																				  m_draft.qsAdvertisedReleaseOverride),
																		fieldItem(QStringLiteral("network.advertisedOS"),
																				  QObject::tr("Advertised OS"),
																				  QStringLiteral("text"),
																				  m_draft.qsAdvertisedOSOverride),
																		fieldItem(QStringLiteral("network.advertisedOSVersion"),
																				  QObject::tr("Advertised OS version"),
																				  QStringLiteral("text"),
																				  m_draft.qsAdvertisedOSVersionOverride) }))
		};
	}

	if (m_activePage == QLatin1String("screenShare")) {
		return QVariantList {
			sectionItem(QObject::tr("Behavior"), QVariantList {
												 boolField(QStringLiteral("screenShare.autoOpenCurrentRoom"),
														   QObject::tr("Auto-open shares in my current voice room"),
														   m_draft.bScreenShareAutoOpenCurrentRoom),
												 advancedField(boolField(QStringLiteral("screenShare.diagnostics"),
																		 QObject::tr("Enable diagnostics logging"),
																		 m_draft.bScreenShareDiagnostics)) }),
			sectionItem(QObject::tr("Capabilities"), QVariantList {
													noteField(QObject::tr("Quality limits and relay modes are negotiated from the server and the current client runtime.")) })
		};
	}

	if (m_activePage == QLatin1String("audioInput")) {
		const QList< QString > inputSystems = inputSystemNames();
		const int inputSystem               = systemIndex(inputSystems, m_draft.qsAudioInput, AudioInputRegistrar::current);
		const QString selectedInputSystem   = systemNameAt(inputSystems, inputSystem, m_draft.qsAudioInput);
		const QVariant selectedInputDevice = inputDeviceChoiceFor(m_draft, selectedInputSystem);
		QList< audioDevice > inputDevices = inputDeviceChoices(selectedInputSystem);
		if (selectedInputSystem == QLatin1String("WASAPI")) {
			inputDevices = deviceChoicesWithUnavailableSelection(std::move(inputDevices), selectedInputDevice);
		}
		const int inputDevice = deviceIndex(inputDevices, selectedInputDevice);
		const bool inputCanExclusive =
			AudioInputRegistrar::qmNew && AudioInputRegistrar::qmNew->contains(selectedInputSystem)
			&& AudioInputRegistrar::qmNew->value(selectedInputSystem)->canExclusive();
		const Settings::SpeechCleanupBackend inputCleanupBackend =
			Mumble::SpeechCleanup::isBackendAvailable(m_draft.noiseCancelBackend)
				? m_draft.noiseCancelBackend
				: Mumble::SpeechCleanup::fallbackBackend();
		const QString inputCleanupModel =
			normalizedSpeechCleanupModelID(inputCleanupBackend, m_draft.noiseCancelModelId);
		const bool inputCustomModel =
			Mumble::SpeechCleanup::usesCustomModelPath(inputCleanupBackend, inputCleanupModel);
		const bool inputNeuralCleanupActive =
			m_draft.noiseCancelMode == Settings::NoiseCancelRNN || m_draft.noiseCancelMode == Settings::NoiseCancelBoth;
		const bool inputSpeexCleanupActive =
			m_draft.noiseCancelMode == Settings::NoiseCancelSpeex || m_draft.noiseCancelMode == Settings::NoiseCancelBoth;
		const bool voiceActivityTransmit = m_draft.atTransmit == Settings::VAD;
		const bool pushToTalkTransmit    = m_draft.atTransmit == Settings::PushToTalk;
		const Mumble::InputEnhancement::DefaultPreference &inputEnhancementPreference =
			currentInputEnhancementPreference(m_draft);
		const InputEnhancementSettingsReadiness currentInputEnhancementReadiness =
			inputEnhancementReadinessForSettings(
				m_draft, inputEnhancementPreference.profile, inputEnhancementPreference.reduction,
				inputEnhancementPreference.character, true);
		QString inputEnhancementProfileHint = m_inputEnhancementReadinessUiError;
		if (inputEnhancementProfileHint.isEmpty()
			&& inputEnhancementPreference.profile != Mumble::InputEnhancement::Profile::Original
			&& !currentInputEnhancementReadiness.selectable) {
			inputEnhancementProfileHint = QObject::tr("Effective profile: Original. %1")
				.arg(inputEnhancementReadinessReasonText(currentInputEnhancementReadiness));
		}
		const InputEnhancementSettingsReadiness inputEnhancementAutoReadiness =
			inputEnhancementReadinessForSettings(
				m_draft, Mumble::InputEnhancement::Profile::Auto,
				inputEnhancementPreference.reduction, inputEnhancementPreference.character, true);
		const bool inputEnhancementAutoSelected =
			inputEnhancementPreference.profile == Mumble::InputEnhancement::Profile::Auto;
		QString inputEnhancementAutoHint = QObject::tr(
			"Experimental: dynamically chooses among Light, Balanced, and Quality. Voice Focus is never selected.");
		const QString inputEnhancementAutoReadinessReason =
			inputEnhancementReadinessReasonText(inputEnhancementAutoReadiness);
		if (!inputEnhancementAutoReadinessReason.isEmpty()) {
			inputEnhancementAutoHint += QLatin1Char(' ');
			inputEnhancementAutoHint += QObject::tr("Unavailable: %1").arg(inputEnhancementAutoReadinessReason);
		}
		const QString inputEnhancementCalibrationUiError =
			Mumble::InputEnhancement::runtimeAutoAdaptationEnabled(inputEnhancementPreference)
				? autoCalibrationUnavailableText()
				: m_inputEnhancementCalibrationUiError;

		const auto audioInputSection = [](const QString &id, const QString &title, const QString &subtitle,
									 const QVariantList &fields) {
			QVariantMap section = sectionItem(title, fields);
			section.insert(QStringLiteral("id"), id);
			section.insert(QStringLiteral("subtitle"), subtitle);
			return section;
		};
		const auto collapsibleSection = [](QVariantMap section, const bool expandedByDefault) {
			section.insert(QStringLiteral("collapsible"), true);
			section.insert(QStringLiteral("expandedByDefault"), expandedByDefault);
			return section;
		};

		QVariantList microphoneFields {
			enabledField(selectField(QStringLiteral("audio.inputDevice"), QObject::tr("Microphone"), inputDevice,
								 deviceOptions(inputDevices)), !inputDevices.isEmpty())
		};
#if defined(Q_OS_WIN) && defined(USE_WASAPI)
		if (selectedInputSystem == QLatin1String("WASAPI")) {
			microphoneFields.push_back(wasapiRuntimeStatusField(eCapture));
		}
#endif
		microphoneFields.push_back(advancedField(selectField(QStringLiteral("audio.inputSystem"),
														 QObject::tr("Audio system"), inputSystem,
														 systemOptions(inputSystems))));
#if defined(Q_OS_WIN) && defined(USE_WASAPI)
		if (selectedInputSystem == QLatin1String("WASAPI")) {
			microphoneFields.push_back(advancedField(selectField(
				QStringLiteral("audio.wasapiInputRouting"), QObject::tr("When the microphone disconnects"),
				wasapiRoutingIndex(m_draft.qsWASAPIInputRoutingPolicy, !m_draft.qsWASAPIInput.isEmpty()),
				wasapiRoutingOptions())));
		}
#endif
		microphoneFields.push_back(advancedField(enabledField(
			boolField(QStringLiteral("audio.exclusiveInput"), QObject::tr("Use exclusive input mode"),
					  m_draft.bExclusiveInput), inputCanExclusive)));

		QString transmitSubtitle;
		switch (m_draft.atTransmit) {
			case Settings::VAD:
				transmitSubtitle = QObject::tr(
					"Recommended for hands-free chat: Mumble opens the microphone when you speak.");
				break;
			case Settings::PushToTalk:
				transmitSubtitle = QObject::tr(
					"Your microphone sends only while your push-to-talk shortcut is held.");
				break;
			case Settings::Continuous:
			default:
				transmitSubtitle = QObject::tr(
					"Your microphone stays open while unmuted. Use this only when you intend to send everything.");
				break;
		}

		QVariantList processingFields {
			hintedField(selectField(QStringLiteral("audio.inputEnhancementProfile"),
											 QObject::tr("Voice enhancement"),
											 static_cast< int >(inputEnhancementPreference.profile),
											 inputEnhancementProfileOptions(m_draft, inputEnhancementPreference.profile)),
						 inputEnhancementProfileHint)
		};
		if (inputEnhancementPreference.profile != Mumble::InputEnhancement::Profile::Original) {
			processingFields.push_back(rangeField(QStringLiteral("audio.inputEnhancementReduction"),
												  QObject::tr("Noise reduction"),
												  inputEnhancementPreference.reduction, 0, 100, 1,
												  QStringLiteral("%")));
			processingFields.push_back(rangeField(QStringLiteral("audio.inputEnhancementCharacter"),
												  QObject::tr("Natural ↔ Clear"),
												  inputEnhancementPreference.character, 0, 100, 1,
												  QStringLiteral("%")));
		}

		QVariantList sections {
			audioInputSection(QStringLiteral("microphone"), QObject::tr("Microphone"),
							  QObject::tr("Choose what Mumble listens to. The default device follows your system input."),
							  microphoneFields),
			audioInputSection(
				QStringLiteral("transmission"), QObject::tr("When should Mumble transmit?"), transmitSubtitle,
				QVariantList { presentationField(
					selectField(QStringLiteral("audio.transmitMode"), QObject::tr("Transmit mode"),
								static_cast< int >(m_draft.atTransmit), transmitModeOptions()),
					QStringLiteral("segmented")) }),
			audioInputSection(
				QStringLiteral("microphoneCheck"), QObject::tr("Check your microphone"),
				QObject::tr("Speak at your normal distance. The meter and guided setup use the same signal as transmission."),
				QVariantList { voiceMeterField(m_draft, m_inputEnhancementCalibrationWorker->snapshot(),
												inputEnhancementCalibrationUiError) }),
			audioInputSection(
				QStringLiteral("voiceProcessing"), QObject::tr("Voice processing"),
				inputEnhancementPreference.profile == Mumble::InputEnhancement::Profile::Original
					? QObject::tr("Leave your voice untouched, or choose a profile for clearer speech in noisy rooms.")
					: QObject::tr("Start with the selected profile. Adjust the two controls only if you need to."),
				processingFields)
		};

		if (voiceActivityTransmit) {
			sections.push_back(advancedSection(collapsibleSection(
				audioInputSection(
					QStringLiteral("voiceActivation"), QObject::tr("Voice activation tuning"),
					QObject::tr("Guided setup above is the safest starting point. Open this only to tune detection manually."),
					QVariantList {
						hintedField(selectField(QStringLiteral("audio.vadSource"), QObject::tr("Detection method"),
												static_cast< int >(m_draft.vsVAD), vadSourceOptions()),
									QObject::tr("Use Speech + volume when speech probability opens too easily on non-voice sounds.")),
						hintedField(selectField(QStringLiteral("audio.inputGateMode"), QObject::tr("Input gate"),
												static_cast< int >(m_draft.inputGateMode), inputGateModeOptions()),
									QObject::tr("Off is recommended after guided setup; stricter gates may clip soft words.")),
						hintedField(rangeField(QStringLiteral("audio.vadMin"), QObject::tr("Stop threshold"),
												vadThresholdFromFloat(m_draft.fVADmin), 0, 100, 1,
												QStringLiteral("%")),
									QObject::tr("Close the microphone when the live signal falls below this level.")),
						hintedField(rangeField(QStringLiteral("audio.vadMax"), QObject::tr("Start threshold"),
												vadThresholdFromFloat(m_draft.fVADmax), 0, 100, 1,
												QStringLiteral("%")),
									QObject::tr("Open the microphone when the live signal rises above this level.")),
						hintedField(numberField(QStringLiteral("audio.voiceHold"), QObject::tr("Release delay"),
												m_draft.iVoiceHold, 0, 250, 1, QObject::tr(" frames")),
									QObject::tr("Keep the microphone open after speech ends; each frame is 10 ms.")) }),
				false)));
		} else if (pushToTalkTransmit) {
			sections.push_back(advancedSection(collapsibleSection(
				audioInputSection(
					QStringLiteral("pushToTalk"), QObject::tr("Push-to-talk behavior"),
					QObject::tr("Fine-tune shortcut timing. Create or change the shortcut under Key Bindings."),
					QVariantList {
						numberField(QStringLiteral("audio.doublePush"), QObject::tr("Double-push lockout"),
									static_cast< int >(m_draft.uiDoublePush), 0, 5000, 50, QObject::tr(" ms")),
						numberField(QStringLiteral("audio.pttHold"), QObject::tr("Release delay"),
									static_cast< int >(m_draft.pttHold), 0, 5000, 50, QObject::tr(" ms")),
						boolField(QStringLiteral("audio.showPttWindow"),
								  QObject::tr("Show push-to-talk button window"), m_draft.bShowPTTButtonWindow) }),
				false)));
		}

		if (inputEnhancementPreference.profile != Mumble::InputEnhancement::Profile::Original) {
			sections.push_back(advancedSection(collapsibleSection(
				audioInputSection(
					QStringLiteral("processingCalibration"), QObject::tr("Compare processing"),
					QObject::tr("Use one private local recording to compare safe processing choices before applying one."),
					QVariantList { inputEnhancementCalibrationField(
						m_draft, m_inputEnhancementCalibrationWorker->snapshot(), inputEnhancementCalibrationUiError) }),
				false)));
		}

		sections.push_back(advancedSection(collapsibleSection(
			audioInputSection(
				QStringLiteral("processingDetails"), QObject::tr("Processing details"),
				QObject::tr("Gain, echo cancellation, experimental switching, and legacy cleanup engines."),
				QVariantList {
					rangeField(QStringLiteral("audio.maxAmplification"), QObject::tr("Maximum amplification"),
							   amplificationFromMinLoudness(m_draft.iMinLoudness), 0,
							   kMaxAmplificationSliderValue, 100, QString()),
					selectField(QStringLiteral("audio.echoMode"), QObject::tr("Echo cancellation"),
								static_cast< int >(m_draft.echoOption), echoOptionsFor(m_draft)),
					hintedField(
						enabledField(boolField(QStringLiteral("audio.inputEnhancementExperimentalAuto"),
											 QObject::tr("Experimental Auto profile"), inputEnhancementAutoSelected),
								 inputEnhancementAutoSelected || inputEnhancementAutoReadiness.selectable),
						inputEnhancementAutoHint),
					selectField(QStringLiteral("audio.noiseCancelMode"), QObject::tr("Legacy suppression mode"),
								static_cast< int >(m_draft.noiseCancelMode), noiseCancelModeOptions()),
					hiddenField(selectField(QStringLiteral("audio.noiseCancelBackend"), QObject::tr("Neural backend"),
										static_cast< int >(inputCleanupBackend), speechCleanupBackendOptions()),
							!inputNeuralCleanupActive),
					hiddenField(selectField(QStringLiteral("audio.noiseCancelModel"), QObject::tr("Neural model"),
										inputCleanupModel, speechCleanupModelOptions(inputCleanupBackend),
										QStringLiteral("string")),
							!inputNeuralCleanupActive),
					hiddenField(fieldItem(QStringLiteral("audio.noiseCancelCustomModelPath"),
									  QObject::tr("Custom model file"), QStringLiteral("text"),
									  m_draft.noiseCancelCustomModelPath),
							!(inputNeuralCleanupActive && inputCustomModel)),
					hiddenField(rangeField(QStringLiteral("audio.speexNoiseStrength"),
									   QObject::tr("Speex suppression strength"),
									   m_draft.iSpeexNoiseCancelStrength == 0
										   ? 14
										   : -m_draft.iSpeexNoiseCancelStrength,
									   0, 100, 1, QString()),
							!inputSpeexCleanupActive) }),
			false)));

		sections.push_back(advancedSection(collapsibleSection(
			audioInputSection(
				QStringLiteral("networkVoice"), QObject::tr("Network voice quality"),
				QObject::tr("Codec and latency controls. The defaults work for nearly everyone."),
				QVariantList {
					numberField(QStringLiteral("audio.framesPerPacket"), QObject::tr("Audio per packet"),
								m_draft.iFramesPerPacket, 1, 6, 1, QObject::tr(" frames")),
					rangeField(QStringLiteral("audio.quality"), QObject::tr("Voice bitrate"),
							   bitrateKbitFromBits(m_draft.iQuality), 8,
							   m_draft.experimentalHighBitrateEnabled ? 512 : 192, 1,
							   QObject::tr(" kbit/s")),
					boolField(QStringLiteral("audio.experimentalHighBitrate"),
							  QObject::tr("Enable experimental high bitrate voice"),
							  m_draft.experimentalHighBitrateEnabled),
					boolField(QStringLiteral("audio.allowLowDelay"), QObject::tr("Allow Opus low-delay mode"),
							  m_draft.bAllowLowDelay) }),
			false)));

		QVariantList idleFields {
			selectField(QStringLiteral("audio.idleAction"), QObject::tr("When I am idle"),
						static_cast< int >(m_draft.iaeIdleAction), idleActionOptions())
		};
		if (m_draft.iaeIdleAction != Settings::Nothing) {
			idleFields.push_back(numberField(QStringLiteral("audio.idleMinutes"), QObject::tr("After"),
										 static_cast< int >(m_draft.iIdleTime / 60), 0, 1440, 1,
										 QObject::tr(" min")));
			idleFields.push_back(boolField(QStringLiteral("audio.undoIdleAction"),
									   QObject::tr("Undo the idle action when activity resumes"),
									   m_draft.bUndoIdleActionUponActivity));
		}
		sections.push_back(advancedSection(collapsibleSection(
			audioInputSection(QStringLiteral("idleBehavior"), QObject::tr("Idle behavior"),
							  QObject::tr("Optionally mute or deafen after a period of inactivity."), idleFields),
			false)));

		return sections;
	}

	if (m_activePage == QLatin1String("audioOutput")) {
		const QList< QString > outputSystems = outputSystemNames();
		const int outputSystem = systemIndex(outputSystems, m_draft.qsAudioOutput, AudioOutputRegistrar::current);
		const QString selectedOutputSystem = systemNameAt(outputSystems, outputSystem, m_draft.qsAudioOutput);
		const QVariant selectedOutputDevice = outputDeviceChoiceFor(m_draft, selectedOutputSystem);
		QList< audioDevice > outputDevices = outputDeviceChoices(selectedOutputSystem);
		if (selectedOutputSystem == QLatin1String("WASAPI")) {
			outputDevices = deviceChoicesWithUnavailableSelection(std::move(outputDevices), selectedOutputDevice);
		}
		const int outputDevice = deviceIndex(outputDevices, selectedOutputDevice);
		const AudioOutputRegistrar *outputRegistrar =
			AudioOutputRegistrar::qmNew && AudioOutputRegistrar::qmNew->contains(selectedOutputSystem)
				? AudioOutputRegistrar::qmNew->value(selectedOutputSystem)
				: nullptr;
		const bool outputCanExclusive = outputRegistrar && outputRegistrar->canExclusive();
		const bool outputUsesDelay    = !outputRegistrar || outputRegistrar->usesOutputDelay();
		const bool canAttenuateApps   = !outputRegistrar || outputRegistrar->canMuteOthers();
		const bool hasRemoteCleanup   = hasAvailableSpeechCleanupBackend();
		const Settings::SpeechCleanupBackend remoteCleanupBackend =
			Mumble::SpeechCleanup::isBackendAvailable(m_draft.remoteSpeechCleanupBackend)
				? m_draft.remoteSpeechCleanupBackend
				: Mumble::SpeechCleanup::fallbackBackend();
		const QString remoteCleanupModel =
			normalizedSpeechCleanupModelID(remoteCleanupBackend, m_draft.remoteSpeechCleanupModelId);
		const bool remoteCustomModel =
			Mumble::SpeechCleanup::usesCustomModelPath(remoteCleanupBackend, remoteCleanupModel);

		QVariantList outputDeviceFields {
			selectField(QStringLiteral("audio.outputSystem"), QObject::tr("Output system"), outputSystem,
						systemOptions(outputSystems)),
			enabledField(selectField(QStringLiteral("audio.outputDevice"), QObject::tr("Output device"), outputDevice,
								 deviceOptions(outputDevices)), !outputDevices.isEmpty())
		};
#if defined(Q_OS_WIN) && defined(USE_WASAPI)
		if (selectedOutputSystem == QLatin1String("WASAPI")) {
			outputDeviceFields.push_back(selectField(
				QStringLiteral("audio.wasapiOutputRouting"), QObject::tr("When the output device disconnects"),
				wasapiRoutingIndex(m_draft.qsWASAPIOutputRoutingPolicy, !m_draft.qsWASAPIOutput.isEmpty()),
				wasapiRoutingOptions()));
			outputDeviceFields.push_back(wasapiRuntimeStatusField(eRender));
		}
#endif
		outputDeviceFields.push_back(advancedField(enabledField(
			boolField(QStringLiteral("audio.exclusiveOutput"), QObject::tr("Use exclusive output mode"),
					  m_draft.bExclusiveOutput), outputCanExclusive)));

		QVariantList outputPlaybackFields {
			rangeField(QStringLiteral("audio.outputVolume"), QObject::tr("Incoming speech volume"),
					   percentFromFloat(m_draft.fVolume), 0, 200, 5, QStringLiteral("%"))
		};
#if defined(Q_OS_WIN) && defined(USE_WASAPI)
		if (selectedOutputSystem == QLatin1String("WASAPI")) {
			outputPlaybackFields.push_back(advancedField(selectField(
				QStringLiteral("audio.wasapiLatencyProfile"), QObject::tr("WASAPI shared-mode buffer strategy"),
				wasapiLatencyIndex(m_draft.qsWASAPILatencyProfile), wasapiLatencyOptions())));
		}
#endif
		outputPlaybackFields.push_back(advancedField(enabledField(numberField(
			QStringLiteral("audio.outputDelay"), QObject::tr("Output delay"),
			outputDelaySliderFromSettings(m_draft.iOutputDelay), 0, 100, 1, QObject::tr(" x10 ms")),
			outputUsesDelay)));
		outputPlaybackFields.push_back(advancedField(numberField(
			QStringLiteral("audio.jitterBuffer"), QObject::tr("Jitter buffer"), m_draft.iJitterBufferSize, 0, 10, 1,
			QObject::tr(" steps"))));
		outputPlaybackFields.push_back(advancedField(selectField(
			QStringLiteral("audio.loopMode"), QObject::tr("Loopback mode"), static_cast< int >(m_draft.lmLoopMode),
			loopModeOptions())));
		outputPlaybackFields.push_back(advancedField(numberField(
			QStringLiteral("audio.loopPacketDelay"), QObject::tr("Loopback packet delay"),
			static_cast< int >(m_draft.dMaxPacketDelay), 0, 1000, 1, QObject::tr(" ms"))));
		outputPlaybackFields.push_back(advancedField(rangeField(
			QStringLiteral("audio.loopPacketLoss"), QObject::tr("Loopback packet loss"),
			static_cast< int >(m_draft.dPacketLoss * 100.0f), 0, 100, 1, QStringLiteral("%"))));

		return QVariantList {
			sectionItem(QObject::tr("Device"), outputDeviceFields),
			sectionItem(QObject::tr("Playback"), outputPlaybackFields),
			advancedSection(sectionItem(QObject::tr("Attenuation"), QVariantList {
												   enabledField(boolField(QStringLiteral("audio.attenuateOthers"),
																		  QObject::tr("Attenuate external applications while others talk"),
																		  m_draft.bAttenuateOthers),
																canAttenuateApps),
												   enabledField(boolField(QStringLiteral("audio.attenuateOnTalk"),
																		  QObject::tr("Attenuate external applications while I talk"),
																		  m_draft.bAttenuateOthersOnTalk),
																canAttenuateApps),
												   enabledField(rangeField(QStringLiteral("audio.externalApplicationsVolume"),
																		   QObject::tr("External applications volume"),
																		   percentFromInvertedFloat(m_draft.fOtherVolume),
																		   0, 100, 5, QStringLiteral("%")),
																canAttenuateApps
																	&& (m_draft.bAttenuateOthers
																		|| m_draft.bAttenuateOthersOnTalk)),
												   boolField(QStringLiteral("audio.attenuatePrioritySpeaker"),
															 QObject::tr("Priority speaker attenuates users"),
															 m_draft.bAttenuateUsersOnPrioritySpeak),
												   boolField(QStringLiteral("audio.alwaysAttenuateListeners"),
															 QObject::tr("Always attenuate listeners"),
															 m_draft.alwaysAttenuateListeners),
												   rangeField(QStringLiteral("audio.listenerAttenuation"),
															  QObject::tr("Listener attenuation"),
															  percentFromInvertedFloat(m_draft.listenerAttenuationFactor),
															  0, 100, 5, QStringLiteral("%")),
												   enabledField(boolField(QStringLiteral("audio.attenuateSameOutputOnly"),
																		  QObject::tr("Only attenuate the same output device"),
																		  m_draft.bOnlyAttenuateSameOutput),
																canAttenuateApps),
												   enabledField(boolField(QStringLiteral("audio.attenuateLoopbacks"),
																		  QObject::tr("Also attenuate loopbacks"),
																		  m_draft.bAttenuateLoopbacks),
																canAttenuateApps && m_draft.bOnlyAttenuateSameOutput) })),
			advancedSection(sectionItem(QObject::tr("Positional audio"), QVariantList {
													   boolField(QStringLiteral("audio.positional"),
																 QObject::tr("Enable positional audio"),
																 m_draft.bPositionalAudio),
													   boolField(QStringLiteral("audio.positionalHeadphone"),
																 QObject::tr("Use headphone mode"),
																 m_draft.bPositionalHeadphone),
													   boolField(QStringLiteral("audio.transmitPosition"),
																 QObject::tr("Transmit my position"),
																 m_draft.bTransmitPosition),
													   numberField(QStringLiteral("audio.minDistance"),
																   QObject::tr("Minimum distance"),
																   static_cast< int >(m_draft.fAudioMinDistance),
																   0, 1000, 1, QObject::tr(" m")),
													   numberField(QStringLiteral("audio.maxDistance"),
																   QObject::tr("Maximum distance"),
																   static_cast< int >(m_draft.fAudioMaxDistance),
																   1, 10000, 1, QObject::tr(" m")),
													   rangeField(QStringLiteral("audio.minPositionalVolume"),
																  QObject::tr("Minimum volume"),
																  static_cast< int >(m_draft.fAudioMaxDistVolume * 100.0f),
																  0, 100, 1, QStringLiteral("%")),
													   rangeField(QStringLiteral("audio.bloom"),
																  QObject::tr("Bloom"),
																  static_cast< int >(m_draft.fAudioBloom * 100.0f),
																  0, 75, 1, QStringLiteral("%")) })),
			sectionItem(QObject::tr("Remote speech cleanup"), QVariantList {
															enabledField(boolField(QStringLiteral("audio.remoteCleanupEnabled"),
																				   QObject::tr("Clean up incoming speech for all users"),
																				   m_draft.remoteSpeechCleanupEnabled),
																		 hasRemoteCleanup),
															advancedField(enabledField(selectField(
																QStringLiteral("audio.remoteCleanupBackend"),
																QObject::tr("Backend"),
																static_cast< int >(remoteCleanupBackend),
																speechCleanupBackendOptions()),
																					 hasRemoteCleanup)),
															advancedField(enabledField(selectField(
																QStringLiteral("audio.remoteCleanupModel"),
																QObject::tr("Model"), remoteCleanupModel,
																speechCleanupModelOptions(remoteCleanupBackend),
																QStringLiteral("string")),
																					 hasRemoteCleanup)),
															advancedField(enabledField(fieldItem(
																QStringLiteral("audio.remoteCleanupCustomModelPath"),
																QObject::tr("Custom model file"), QStringLiteral("text"),
																m_draft.remoteSpeechCleanupCustomModelPath),
																					 hasRemoteCleanup && remoteCustomModel)),
															advancedField(enabledField(selectField(
																QStringLiteral("audio.remoteCleanupPreset"),
																QObject::tr("Preset"),
																static_cast< int >(m_draft.remoteSpeechCleanupPreset),
																remoteSpeechCleanupPresetOptions()),
																					 hasRemoteCleanup)) })
		};
	}

	const bool customAccentSelected =
		normalizedModernShellAccent(m_draft.qsModernShellAccent) == Mumble::ModernTheme::customAccentId();

	return QVariantList {
		sectionItem(QObject::tr("Native interface"), QVariantList {
											 noteField(QObject::tr("Mumble uses the native Qt Quick interface for all product surfaces.")) }),
		sectionItem(QObject::tr("Tweaks"), QVariantList {
										hintedField(
											presentationField(
												selectField(
													QStringLiteral("look.modernTheme"), QObject::tr("Theme"),
													normalizedModernShellTheme(
														m_draft.qsModernShellTheme, m_modernThemeCatalog),
													modernShellThemeOptions(m_modernThemeCatalog),
													QStringLiteral("string")),
												QStringLiteral("themeGrid")),
											QObject::tr("Select a theme to preview it instantly. Apply saves without closing; Done saves and closes.")),
										actionField(QStringLiteral("look.modernThemesDirectory"),
													QObject::tr("Custom theme folder"),
													QObject::tr("Open folder"),
													QStringLiteral("look.openModernThemesDirectory")),
										actionField(QStringLiteral("look.modernThemesReload"),
													QObject::tr("Theme library"),
													QObject::tr("Reload themes"),
													QStringLiteral("look.reloadModernThemes")),
											presentationField(
												selectField(QStringLiteral("look.modernDensity"), QObject::tr("Density"),
															normalizedModernShellDensity(m_draft.qsModernShellDensity),
															modernShellDensityOptions(), QStringLiteral("string")),
												QStringLiteral("segmented")),
											boolField(QStringLiteral("look.modernClassicUserIcons"),
													  QObject::tr("Use classic user icons"),
													  m_draft.bModernShellClassicUserIcons),
											presentationField(
												selectField(QStringLiteral("look.modernRailSide"),
															QObject::tr("Rail side"),
															normalizedModernShellRailSide(m_draft.qsModernShellRailSide),
															modernShellRailSideOptions(), QStringLiteral("string")),
												QStringLiteral("segmented")),
											presentationField(
												selectField(QStringLiteral("look.modernAccent"), QObject::tr("Accent"),
															normalizedModernShellAccent(m_draft.qsModernShellAccent),
															modernShellAccentOptions(m_draft, m_modernThemeCatalog),
															QStringLiteral("string")),
												QStringLiteral("accentGrid")),
											hiddenField(colorField(QStringLiteral("look.modernCustomAccent"),
																   QObject::tr("Custom accent"),
																   normalizedModernShellCustomAccent(
																	   m_draft.qsModernShellCustomAccent)),
														!customAccentSelected),
											hiddenField(rangeField(
															QStringLiteral("look.modernCustomAccentStrength"),
															QObject::tr("Accent strength"),
															normalizedModernShellCustomAccentStrength(
																			m_draft.iModernShellCustomAccentStrength),
																	0, 100, 1, QStringLiteral("%")),
															!customAccentSelected) })
	};
}

void ModernSettingsController::setActivePage(const QString &pageID) {
	const QString normalized = pageID.trimmed();
	if (normalized == QLatin1String("NetworkConfig")) {
		m_activePage = QStringLiteral("network");
	} else if (normalized == QLatin1String("ScreenShareConfig")) {
		m_activePage = QStringLiteral("screenShare");
	} else if (normalized == QLatin1String("AudioInput")) {
		m_activePage = QStringLiteral("audioInput");
	} else if (normalized == QLatin1String("AudioOutput")) {
		m_activePage = QStringLiteral("audioOutput");
	} else if (normalized == QLatin1String("PluginConfig")) {
		m_activePage = QStringLiteral("plugins");
	} else if (normalized == QLatin1String("Stonks") || normalized == QLatin1String("StonksConfig")) {
		m_activePage = QStringLiteral("stonks");
	} else if ((normalized == QLatin1String("Motd") || normalized == QLatin1String("MOTD")
				|| normalized == QLatin1String("ServerMotd") || normalized == QLatin1String("motd"))
			   && m_motdContext.value(QStringLiteral("available")).toBool()) {
		m_activePage = QStringLiteral("motd");
	} else if (normalized == QLatin1String("network") || normalized == QLatin1String("screenShare")
			   || normalized == QLatin1String("audioInput") || normalized == QLatin1String("audioOutput")
			   || normalized == QLatin1String("look") || normalized == QLatin1String("ui")
			   || normalized == QLatin1String("messages") || normalized == QLatin1String("stonks")
			   || normalized == QLatin1String("keys")
			   || normalized == QLatin1String("plugins") || normalized == QLatin1String("about")) {
		m_activePage = normalized;
	} else if (normalized == QLatin1String("appearance")) {
		m_activePage = QStringLiteral("look");
	} else if (normalized == QLatin1String("UserInterface")) {
		m_activePage = QStringLiteral("ui");
	} else if (normalized == QLatin1String("MessagesSounds") || normalized == QLatin1String("MessagesAndSounds")) {
		m_activePage = QStringLiteral("messages");
	} else if (normalized == QLatin1String("KeyBindings") || normalized == QLatin1String("GlobalShortcutConfig")) {
		m_activePage = QStringLiteral("keys");
	} else if (normalized == QLatin1String("audio")) {
		m_activePage = QStringLiteral("audioInput");
	} else {
		m_activePage = QStringLiteral("audioInput");
	}
}

void ModernSettingsController::forceModernLayout() {
	m_draft.modernLayoutPolicy = Settings::ModernLayoutForced;
	m_draft.wlWindowLayout     = Settings::LayoutModern;
}

void ModernSettingsController::refreshShortcutRestartFlag() {
	const bool enginesChanged = m_draft.bEnableUIAccess != m_original.bEnableUIAccess
								|| m_draft.bEnableGKey != m_original.bEnableGKey
								|| m_draft.bEnableXboxInput != m_original.bEnableXboxInput;
	m_draft.requireRestartToApply = m_original.requireRestartToApply || enginesChanged;
}

void ModernSettingsController::cancelInputEnhancementCalibration() {
	const AudioInputPtr input = currentAudioInput();
	auto *runtime             = input ? input->inputEnhancementCalibrationRuntime() : nullptr;
	const bool workerActive = m_inputEnhancementCalibrationWorker->snapshot().active();
	if (workerActive) {
		m_inputEnhancementCalibrationWorker->cancel();
	} else if (runtime && runtime->transmissionBlocked()) {
		runtime->cancel();
	}
	if (input && !workerActive) {
		input->synchronizeInputEnhancementCalibrationTransmissionBlock();
	}
	m_inputEnhancementCalibrationControls.reset();
	m_inputEnhancementCalibrationUiError.clear();
}
