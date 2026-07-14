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
#include "ModernShellMenuSerializer.h"
#include "ModernTheme.h"
#include "PersistentChatMediaCache.h"
#include "Plugin.h"
#include "PluginManager.h"
#include "SpeechCleanup.h"
#include "UiTheme.h"
#include "Version.h"

#include <QtCore/QDebug>
#include <QtCore/QCryptographicHash>
#include <QtCore/QHash>
#include <QtCore/QMap>
#include <QtCore/QObject>
#include <QtCore/QPair>
#include <QtCore/QReadLocker>
#include <QtCore/QSet>
#include <QtCore/QSysInfo>
#include <QtCore/QUrl>
#include <QtGui/QDesktopServices>

#include <algorithm>
#include <cmath>

namespace {
	constexpr int kMaxAmplificationSliderValue = 19500;
	constexpr int kAmplificationSliderBase     = 20000;

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
			{ QStringLiteral("look.modernTickerBannerEnabled"),
			  QObject::tr("Show the Stonks ticker banner in the Modern conversation header.") },
			{ QStringLiteral("look.modernTickerAlwaysScroll"),
			  QObject::tr("Keep the Stonks ticker banner moving even when the current ticker list fits on screen.") },
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

	QVariantMap voiceMeterField(const Settings &settings) {
		QVariantMap field = fieldItem(QStringLiteral("audio.inputMeter"), QObject::tr("Current voice input"),
									  QStringLiteral("voiceMeter"), 0);
		const int currentAmplification                 = amplificationFromMinLoudness(settings.iMinLoudness);
		const Settings::NoiseCancel recommendedCleanup = recommendedNoiseCancelMode(settings);
		field.insert(QStringLiteral("vadSource"), static_cast< int >(settings.vsVAD));
		field.insert(QStringLiteral("transmitMode"), static_cast< int >(settings.atTransmit));
		field.insert(QStringLiteral("active"), settings.atTransmit == Settings::VAD);
		field.insert(QStringLiteral("calibrationState"), QStringLiteral("idle"));
		field.insert(QStringLiteral("calibrationActionId"), QStringLiteral("finishAudioSetupWizard"));
		field.insert(QStringLiteral("calibrationLabel"), QObject::tr("Use recommended settings"));
		field.insert(QStringLiteral("calibrationTooltip"),
					 QObject::tr("Stage recommended voice detection, input gate, amplification, and cleanup settings. Apply or save to activate them."));
		field.insert(QStringLiteral("calibrationStatusText"), QObject::tr("Ready to tune voice input."));
		field.insert(QStringLiteral("replayStartActionId"), QStringLiteral("startVoiceReplay"));
		field.insert(QStringLiteral("replayStopActionId"), QStringLiteral("stopVoiceReplay"));
		field.insert(QStringLiteral("replayLabel"), QObject::tr("Replay"));
		field.insert(QStringLiteral("replayTooltip"),
					 QObject::tr("Listen to your microphone through server loopback when connected, or local loopback when offline."));
		field.insert(QStringLiteral("sourceLabel"), vadSourceLabel(settings.vsVAD));
		field.insert(QStringLiteral("silenceThreshold"), vadThresholdFromFloat(settings.fVADmin));
		field.insert(QStringLiteral("speechThreshold"), vadThresholdFromFloat(settings.fVADmax));
		field.insert(QStringLiteral("loopbackMode"), static_cast< int >(settings.lmLoopMode));
		field.insert(QStringLiteral("maxAmplification"), currentAmplification);
		field.insert(QStringLiteral("noiseCancelMode"), static_cast< int >(settings.noiseCancelMode));
		field.insert(QStringLiteral("inputGateMode"), static_cast< int >(settings.inputGateMode));
		field.insert(QStringLiteral("speexNoiseStrength"),
					 settings.iSpeexNoiseCancelStrength == 0 ? 14 : -settings.iSpeexNoiseCancelStrength);
		field.insert(QStringLiteral("neuralCleanupAvailable"), hasAvailableSpeechCleanupBackend());
		field.insert(QStringLiteral("recommendedVadSource"), static_cast< int >(Settings::Hybrid));
		field.insert(QStringLiteral("recommendedInputGateMode"), static_cast< int >(Settings::InputGateBalanced));
		field.insert(QStringLiteral("recommendedNoiseCancelMode"), static_cast< int >(recommendedCleanup));
		field.insert(QStringLiteral("recommendedMaxAmplification"), currentAmplification);
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
		dto.insert(QStringLiteral("tickerBannerEnabled"), settings.bModernShellTickerBannerEnabled);
		dto.insert(QStringLiteral("tickerBannerAlwaysScroll"), settings.bModernShellTickerBannerAlwaysScroll);
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

void ModernSettingsController::open(const Settings &settings, const QString &pageName) {
	m_original = settings;
	m_draft    = settings;
	m_shortcutCaptureIndex = -1;
	m_voiceReplayPreviousTransmitMode.reset();
	m_voiceReplayPreviousLoopMode.reset();
	m_voiceReplayPreviousPacketLoss.reset();
	m_voiceReplayPreviousMaxPacketDelay.reset();
	m_runtimePreviewDiffersFromOriginal = false;
	m_appearancePreviewActive = false;
	refreshModernThemeCatalog();
	forceModernLayout();
	setActivePage(pageName);
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
	dialog.insert(QStringLiteral("subtitle"), QString());
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
	if (id == QLatin1String("plugins.runtimeLoaded")) {
		const QVariantMap runtimeState = value.toMap();
		reconcilePluginLoadedState(runtimeState.value(QStringLiteral("path")).toString(),
								 runtimeState.value(QStringLiteral("loaded")).toBool());
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
	} else if (id == QLatin1String("look.modernTickerBannerEnabled")) {
		m_draft.bModernShellTickerBannerEnabled = value.toBool();
	} else if (id == QLatin1String("look.modernTickerAlwaysScroll")) {
		m_draft.bModernShellTickerBannerAlwaysScroll = value.toBool();
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
	} else if (id == QLatin1String("audio.inputSystem")) {
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
	} else if (id == QLatin1String("audio.noiseCancelMode")) {
		m_draft.noiseCancelMode = static_cast< Settings::NoiseCancel >(value.toInt());
	} else if (id == QLatin1String("audio.noiseCancelBackend")) {
		m_draft.noiseCancelBackend = normalizedBackendFromValue(value);
		m_draft.noiseCancelModelId =
			normalizedSpeechCleanupModelID(m_draft.noiseCancelBackend, m_draft.noiseCancelModelId);
		if (!Mumble::SpeechCleanup::usesCustomModelPath(m_draft.noiseCancelBackend, m_draft.noiseCancelModelId)) {
			m_draft.noiseCancelCustomModelPath.clear();
		}
	} else if (id == QLatin1String("audio.noiseCancelModel")) {
		m_draft.noiseCancelModelId =
			normalizedSpeechCleanupModelID(m_draft.noiseCancelBackend, value.toString());
		if (!Mumble::SpeechCleanup::usesCustomModelPath(m_draft.noiseCancelBackend, m_draft.noiseCancelModelId)) {
			m_draft.noiseCancelCustomModelPath.clear();
		}
	} else if (id == QLatin1String("audio.noiseCancelCustomModelPath")) {
		m_draft.noiseCancelCustomModelPath = value.toString().trimmed();
	} else if (id == QLatin1String("audio.speexNoiseStrength")) {
		m_draft.iSpeexNoiseCancelStrength = value.toInt() == 14 ? 0 : -qBound(0, value.toInt(), 100);
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
		m_shortcutCaptureIndex = -1;
		setActivePage(payload.value(QStringLiteral("pageId")).toString());
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
		result.closeDialog = true;
		return result;
	}

	if (action == QLatin1String("reset")) {
		m_draft = m_original;
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
		forceModernLayout();
		return result;
	}

	if (action == QLatin1String("startVoiceReplay")) {
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
		m_shortcutCaptureIndex = -1;
		restoreVoiceReplayDraft();
		refreshShortcutRestartFlag();
		forceModernLayout();
		result.settingsToApply = m_draft;
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
	const auto page = [this](const QString &id, const QString &label) {
		QVariantMap item;
		item.insert(QStringLiteral("id"), id);
		item.insert(QStringLiteral("label"), label);
		item.insert(QStringLiteral("selected"), id == m_activePage);
		return item;
	};

	return QVariantList { page(QStringLiteral("audioInput"), QObject::tr("Audio Input")),
						  page(QStringLiteral("audioOutput"), QObject::tr("Audio Output")),
						  page(QStringLiteral("look"), QObject::tr("Appearance")),
						  page(QStringLiteral("ui"), QObject::tr("User Interface")),
						  page(QStringLiteral("messages"), QObject::tr("Messages & Sounds")),
						  page(QStringLiteral("keys"), QObject::tr("Key Bindings")),
						  page(QStringLiteral("network"), QObject::tr("Network")),
						  page(QStringLiteral("screenShare"), QObject::tr("Screen Sharing")),
						  page(QStringLiteral("plugins"), QObject::tr("Plugins")),
						  page(QStringLiteral("about"), QObject::tr("About")) };
}

QVariantList ModernSettingsController::sectionsForActivePage() const {
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
																			 m_draft.bQoS)),
													 boolField(QStringLiteral("network.linkPreviews"),
															   QObject::tr("Enable link previews"),
															   m_draft.bEnableLinkPreviews) }),
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
		const QList< audioDevice > inputDevices = inputDeviceChoices(selectedInputSystem);
		const int inputDevice = deviceIndex(inputDevices, inputDeviceChoiceFor(m_draft, selectedInputSystem));
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

		return QVariantList {
			sectionItem(QObject::tr("Input device"), QVariantList {
											   selectField(QStringLiteral("audio.inputSystem"),
														   QObject::tr("Input system"), inputSystem,
														   systemOptions(inputSystems)),
											   enabledField(selectField(QStringLiteral("audio.inputDevice"),
																		QObject::tr("Input device"), inputDevice,
																		deviceOptions(inputDevices)),
															!inputDevices.isEmpty()),
											   advancedField(enabledField(boolField(QStringLiteral("audio.exclusiveInput"),
																				  QObject::tr("Use exclusive input mode"),
																				  m_draft.bExclusiveInput),
																	 inputCanExclusive)) }),
			sectionItem(QObject::tr("Voice activation"), QVariantList {
												selectField(QStringLiteral("audio.transmitMode"),
															QObject::tr("Transmit mode"),
															static_cast< int >(m_draft.atTransmit),
															transmitModeOptions()),
												hintedField(enabledField(selectField(QStringLiteral("audio.vadSource"),
																					 QObject::tr("Detection method"),
																					 static_cast< int >(m_draft.vsVAD),
																					 vadSourceOptions()),
																		 voiceActivityTransmit),
															QObject::tr("Use Speech + volume when speech probability opens too easily on non-voice sounds.")),
												advancedField(hintedField(enabledField(
													selectField(QStringLiteral("audio.inputGateMode"),
																QObject::tr("Input gate"),
																static_cast< int >(m_draft.inputGateMode),
																inputGateModeOptions()),
													voiceActivityTransmit),
																		   QObject::tr("Optional extra guard after cleanup; Off keeps the original behavior."))),
												voiceMeterField(m_draft),
												advancedField(hintedField(enabledField(
													rangeField(QStringLiteral("audio.vadMin"),
															   QObject::tr("Stop transmitting below"),
															   vadThresholdFromFloat(m_draft.fVADmin), 0, 100, 1,
															   QStringLiteral("%")),
													voiceActivityTransmit),
																		   QObject::tr("Lower boundary for closing the microphone."))),
												advancedField(hintedField(enabledField(
													rangeField(QStringLiteral("audio.vadMax"),
															   QObject::tr("Start transmitting above"),
															   vadThresholdFromFloat(m_draft.fVADmax), 0, 100, 1,
															   QStringLiteral("%")),
													voiceActivityTransmit),
																		   QObject::tr("Upper boundary for opening the microphone."))),
												advancedField(enabledField(numberField(QStringLiteral("audio.voiceHold"),
																					  QObject::tr("Voice hold"),
																					  m_draft.iVoiceHold, 0, 250, 1,
																					  QObject::tr(" frames")),
																		 voiceActivityTransmit)) }),
			advancedSection(sectionItem(QObject::tr("Push-to-talk"), QVariantList {
												enabledField(numberField(QStringLiteral("audio.doublePush"),
																		QObject::tr("Double-push lockout"),
																		static_cast< int >(m_draft.uiDoublePush), 0, 5000,
																		50, QObject::tr(" ms")),
															 pushToTalkTransmit),
												enabledField(numberField(QStringLiteral("audio.pttHold"),
																		QObject::tr("Push-to-talk hold"),
																		static_cast< int >(m_draft.pttHold), 0, 5000, 50,
																		QObject::tr(" ms")),
															 pushToTalkTransmit),
												enabledField(boolField(QStringLiteral("audio.showPttWindow"),
																	   QObject::tr("Show push-to-talk button window"),
																	   m_draft.bShowPTTButtonWindow),
															 pushToTalkTransmit) })),
			advancedSection(sectionItem(QObject::tr("Compression"), QVariantList {
												   numberField(QStringLiteral("audio.framesPerPacket"),
															   QObject::tr("Audio per packet"),
															   m_draft.iFramesPerPacket, 1, 6, 1,
															   QObject::tr(" frames")),
												   rangeField(QStringLiteral("audio.quality"),
															  QObject::tr("Voice bitrate"),
															  bitrateKbitFromBits(m_draft.iQuality), 8,
															  m_draft.experimentalHighBitrateEnabled ? 512 : 192, 1,
															  QObject::tr(" kbit/s")),
												   boolField(QStringLiteral("audio.experimentalHighBitrate"),
															 QObject::tr("Enable experimental high bitrate voice"),
															 m_draft.experimentalHighBitrateEnabled),
												   boolField(QStringLiteral("audio.allowLowDelay"),
															 QObject::tr("Allow Opus low-delay mode"),
															 m_draft.bAllowLowDelay) })),
			sectionItem(QObject::tr("Audio processing"), QVariantList {
													   advancedField(rangeField(QStringLiteral("audio.maxAmplification"),
																				QObject::tr("Maximum amplification"),
																				amplificationFromMinLoudness(
																					m_draft.iMinLoudness),
																				0, kMaxAmplificationSliderValue, 100,
																				QString())),
													   selectField(QStringLiteral("audio.echoMode"),
																   QObject::tr("Echo cancellation"),
																   static_cast< int >(m_draft.echoOption),
																   echoOptionsFor(m_draft)),
													   selectField(QStringLiteral("audio.noiseCancelMode"),
																   QObject::tr("Noise suppression"),
																   static_cast< int >(m_draft.noiseCancelMode),
																   noiseCancelModeOptions()),
													   hiddenField(advancedField(selectField(
																	   QStringLiteral("audio.noiseCancelBackend"),
																	   QObject::tr("Neural backend"),
																	   static_cast< int >(inputCleanupBackend),
																	   speechCleanupBackendOptions())),
																   !inputNeuralCleanupActive),
													   hiddenField(advancedField(selectField(
																	   QStringLiteral("audio.noiseCancelModel"),
																	   QObject::tr("Neural model"), inputCleanupModel,
																	   speechCleanupModelOptions(inputCleanupBackend),
																	   QStringLiteral("string"))),
																   !inputNeuralCleanupActive),
													   hiddenField(advancedField(
																	   fieldItem(QStringLiteral("audio.noiseCancelCustomModelPath"),
																				 QObject::tr("Custom model file"),
																				 QStringLiteral("text"),
																				 m_draft.noiseCancelCustomModelPath)),
																   !(inputNeuralCleanupActive && inputCustomModel)),
													   hiddenField(advancedField(rangeField(
																	   QStringLiteral("audio.speexNoiseStrength"),
																	   QObject::tr("Speex suppression strength"),
																	   m_draft.iSpeexNoiseCancelStrength == 0
																		   ? 14
																		   : -m_draft.iSpeexNoiseCancelStrength,
																	   0, 100, 1, QString())),
																   !inputSpeexCleanupActive) }),
			advancedSection(sectionItem(QObject::tr("Cues and idle behavior"), QVariantList {
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
																	  QStringLiteral("text"), m_draft.qsTxMuteCue),
															numberField(QStringLiteral("audio.idleMinutes"),
																		QObject::tr("Idle after"),
																		static_cast< int >(m_draft.iIdleTime / 60),
																		0, 1440, 1, QObject::tr(" min")),
															selectField(QStringLiteral("audio.idleAction"),
																		QObject::tr("Idle action"),
																		static_cast< int >(m_draft.iaeIdleAction),
																		idleActionOptions()),
															boolField(QStringLiteral("audio.undoIdleAction"),
																	  QObject::tr("Undo idle action on activity"),
																	  m_draft.bUndoIdleActionUponActivity) }))
		};
	}

	if (m_activePage == QLatin1String("audioOutput")) {
		const QList< QString > outputSystems = outputSystemNames();
		const int outputSystem = systemIndex(outputSystems, m_draft.qsAudioOutput, AudioOutputRegistrar::current);
		const QString selectedOutputSystem = systemNameAt(outputSystems, outputSystem, m_draft.qsAudioOutput);
		const QList< audioDevice > outputDevices = outputDeviceChoices(selectedOutputSystem);
		const int outputDevice = deviceIndex(outputDevices, outputDeviceChoiceFor(m_draft, selectedOutputSystem));
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

		return QVariantList {
			sectionItem(QObject::tr("Device"), QVariantList {
											   selectField(QStringLiteral("audio.outputSystem"),
														   QObject::tr("Output system"), outputSystem,
														   systemOptions(outputSystems)),
											   enabledField(selectField(QStringLiteral("audio.outputDevice"),
																		QObject::tr("Output device"), outputDevice,
																		deviceOptions(outputDevices)),
															!outputDevices.isEmpty()),
											   advancedField(enabledField(boolField(QStringLiteral("audio.exclusiveOutput"),
																				  QObject::tr("Use exclusive output mode"),
																				  m_draft.bExclusiveOutput),
																	 outputCanExclusive)) }),
			sectionItem(QObject::tr("Playback"), QVariantList {
												rangeField(QStringLiteral("audio.outputVolume"),
														   QObject::tr("Incoming speech volume"),
														   percentFromFloat(m_draft.fVolume), 0, 200, 5,
														   QStringLiteral("%")),
												advancedField(enabledField(numberField(
													QStringLiteral("audio.outputDelay"), QObject::tr("Output delay"),
													outputDelaySliderFromSettings(m_draft.iOutputDelay), 0, 100, 1,
													QObject::tr(" x10 ms")),
																		 outputUsesDelay)),
												advancedField(numberField(QStringLiteral("audio.jitterBuffer"),
																		  QObject::tr("Jitter buffer"),
																		  m_draft.iJitterBufferSize, 0, 10, 1,
																		  QObject::tr(" steps"))),
												advancedField(selectField(QStringLiteral("audio.loopMode"),
																		  QObject::tr("Loopback mode"),
																		  static_cast< int >(m_draft.lmLoopMode),
																		  loopModeOptions())),
												advancedField(numberField(QStringLiteral("audio.loopPacketDelay"),
																		  QObject::tr("Loopback packet delay"),
																		  static_cast< int >(m_draft.dMaxPacketDelay), 0,
																		  1000, 1, QObject::tr(" ms"))),
												advancedField(rangeField(QStringLiteral("audio.loopPacketLoss"),
																		 QObject::tr("Loopback packet loss"),
																		 static_cast< int >(m_draft.dPacketLoss * 100.0f),
																		 0, 100, 1, QStringLiteral("%"))) }),
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
		sectionItem(QObject::tr("Modern layout"), QVariantList {
											 noteField(QObject::tr("This fork now uses the Modern layout as the visible client shell. Classic layout switching is disabled.")) }),
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
														!customAccentSelected),
											boolField(QStringLiteral("look.modernTickerBannerEnabled"),
													  QObject::tr("Show ticker bar"),
													  m_draft.bModernShellTickerBannerEnabled),
											enabledField(boolField(QStringLiteral("look.modernTickerAlwaysScroll"),
																   QObject::tr("Always scroll ticker banner"),
																   m_draft.bModernShellTickerBannerAlwaysScroll),
														 m_draft.bModernShellTickerBannerEnabled) })
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
	} else if (normalized == QLatin1String("network") || normalized == QLatin1String("screenShare")
			   || normalized == QLatin1String("audioInput") || normalized == QLatin1String("audioOutput")
			   || normalized == QLatin1String("look") || normalized == QLatin1String("ui")
			   || normalized == QLatin1String("messages") || normalized == QLatin1String("keys")
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
