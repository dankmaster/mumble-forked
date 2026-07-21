// Copyright The Mumble Developers. All rights reserved.
// Use of this source code is governed by a BSD-style license
// that can be found in the LICENSE file at the root of the
// Mumble source tree or at <https://www.mumble.info/LICENSE>.


#ifndef MUMBLE_MUMBLE_SETTINGS_MACROS_H_
#define MUMBLE_MUMBLE_SETTINGS_MACROS_H_

#include "SettingsKeys.h"

// Mappings between SettingsKey objects and the corresponding fields in the Settings struct

#define MISC_SETTINGS                                                               \
	PROCESS(misc, DATABASE_LOCATION_KEY, qsDatabaseLocation)                        \
	PROCESS(misc, IMAGE_DIRECTORY_KEY, qsImagePath)                                 \
	PROCESS(misc, AUDIO_WIZARD_SHOWN_KEY, audioWizardShown)                         \
	PROCESS(misc, MODERN_AUDIO_SETUP_VERSION_KEY, modernAudioSetupVersion)           \
	PROCESS(misc, SERVER_PING_CONSENT_MESSAGE_VIEWED_KEY, bPingServersDialogViewed) \
	PROCESS(misc, CRASH_EMAIL_ADDRESS_KEY, crashReportEmail)

#define AUDIO_SETTINGS                                                                              \
	PROCESS(audio, UNMUTE_ON_UNDEAF_KEY, unmuteOnUndeaf)                                            \
	PROCESS(audio, MUTE_KEY, bMute)                                                                 \
	PROCESS(audio, DEAF_KEY, bDeaf)                                                                 \
	PROCESS(audio, TRANSMIT_MODE_KEY, atTransmit)                                                   \
	PROCESS(audio, DOUBLE_PUSH_DELAY_KEY, uiDoublePush)                                             \
	PROCESS(audio, PTT_HOLD_KEY, pttHold)                                                           \
	PROCESS(audio, TRANSMIT_CUE_WHEN_PTT_KEY, audioCueEnabledPTT)                                   \
	PROCESS(audio, TRANSMIT_CUE_WHEN_VAD_KEY, audioCueEnabledVAD)                                   \
	PROCESS(audio, TRANSMIT_CUE_START_KEY, qsTxAudioCueOn)                                          \
	PROCESS(audio, TRANSMIT_CUE_STOP_KEY, qsTxAudioCueOff)                                          \
	PROCESS(audio, PLAY_MUTE_CUE_KEY, bTxMuteCue)                                                   \
	PROCESS(audio, MUTE_CUE_KEY, qsTxMuteCue)                                                       \
	PROCESS(audio, MUTE_CUE_POPUP_SHOWN, muteCueShown)                                              \
	PROCESS(audio, AUDIO_QUALITY_KEY, iQuality)                                                     \
	PROCESS(audio, EXPERIMENTAL_HIGH_BITRATE_ENABLED_KEY, experimentalHighBitrateEnabled)           \
	PROCESS(audio, LOUDNESS_KEY, iMinLoudness)                                                      \
	PROCESS(audio, VOLUME_KEY, fVolume)                                                             \
	PROCESS(audio, EXTERNAL_APPLICATIONS_VOLUME_KEY, fOtherVolume)                                  \
	PROCESS(audio, LISTENER_ATTENUATION_FACTOR_KEY, listenerAttenuationFactor)                      \
	PROCESS(audio, ALWAYS_ATTENUATE_LISTENERS_KEY, alwaysAttenuateListeners)                        \
	PROCESS(audio, ATTENUATE_EXTERNAL_APPLICATIONS_KEY, bAttenuateOthers)                           \
	PROCESS(audio, ATTENUATE_EXTERNAL_APPLICATIONS_ON_TALK_KEY, bAttenuateOthersOnTalk)             \
	PROCESS(audio, ATTENUATE_USERS_ON_PRIORITY_SPEAKER_KEY, bAttenuateUsersOnPrioritySpeak)         \
	PROCESS(audio, ATTENUATE_ONLY_SAME_OUTPUT_KEY, bOnlyAttenuateSameOutput)                        \
	PROCESS(audio, ATTENUATE_LOOPBACK_KEY, bAttenuateLoopbacks)                                     \
	PROCESS(audio, VAD_MODE_KEY, vsVAD)                                                             \
	PROCESS(audio, VAD_MIN_KEY, fVADmin)                                                            \
	PROCESS(audio, VAD_MAX_KEY, fVADmax)                                                            \
	PROCESS(audio, INPUT_GATE_MODE_KEY, inputGateMode)                                              \
	PROCESS(audio, NOISE_CANCEL_MODE_KEY, noiseCancelMode)                                          \
	PROCESS(audio, NOISE_CANCEL_BACKEND_KEY, noiseCancelBackend)                                    \
	PROCESS(audio, NOISE_CANCEL_MODEL_ID_KEY, noiseCancelModelId)                                   \
	PROCESS(audio, NOISE_CANCEL_CUSTOM_MODEL_PATH_KEY, noiseCancelCustomModelPath)                  \
	PROCESS(audio, SPEEX_NOISE_CANCEL_STRENGTH_KEY, iSpeexNoiseCancelStrength)                      \
	PROCESS(audio, REMOTE_SPEECH_CLEANUP_ENABLED_KEY, remoteSpeechCleanupEnabled)                   \
	PROCESS(audio, REMOTE_SPEECH_CLEANUP_BACKEND_KEY, remoteSpeechCleanupBackend)                   \
	PROCESS(audio, REMOTE_SPEECH_CLEANUP_MODEL_ID_KEY, remoteSpeechCleanupModelId)                  \
	PROCESS(audio, REMOTE_SPEECH_CLEANUP_CUSTOM_MODEL_PATH_KEY, remoteSpeechCleanupCustomModelPath) \
	PROCESS(audio, REMOTE_SPEECH_CLEANUP_PRESET_KEY, remoteSpeechCleanupPreset)                     \
	PROCESS(audio, INPUT_CHANNEL_MASK_KEY, uiAudioInputChannelMask)                                 \
	PROCESS(audio, ALLOW_LOW_DELAY_MODE_KEY, bAllowLowDelay)                                        \
	PROCESS(audio, VOICE_HOLD_KEY, iVoiceHold)                                                      \
	PROCESS(audio, OUTPUT_DELAY_KEY, iOutputDelay)                                                  \
	PROCESS(audio, ECHO_CANCEL_MODE_KEY, echoOption)                                                \
	PROCESS(audio, EXCLUSIVE_INPUT_KEY, bExclusiveInput)                                            \
	PROCESS(audio, EXCLUSIVE_OUTPUT_KEY, bExclusiveOutput)                                          \
	PROCESS(audio, INPUT_SYSTEM_KEY, qsAudioInput)                                                  \
	PROCESS(audio, OUTPUT_SYSTEM_KEY, qsAudioOutput)                                                \
	PROCESS(audio, NOTIFICATION_VOLUME_KEY, notificationVolume)                                     \
	PROCESS(audio, CUE_VOLUME_KEY, cueVolume)                                                       \
	PROCESS(audio, RESTRICT_WHISPERS_TO_FRIENDS_KEY, bWhisperFriends)                               \
	PROCESS(audio, NOTIFICATION_USER_LIMIT_KEY, iMessageLimitUserThreshold)


#define IDLE_SETTINGS                             \
	PROCESS(idle, IDLE_TIME_KEY, iIdleTime)       \
	PROCESS(idle, IDLE_ACTION_KEY, iaeIdleAction) \
	PROCESS(idle, UNDO_IDLE_ACTION_UPON_ACTIVITY, bUndoIdleActionUponActivity)


#define POSITIONAL_AUDIO_SETTINGS                                                  \
	PROCESS(positional_audio, ENABLE_POSITIONAL_AUDIO_KEY, bPositionalAudio)       \
	PROCESS(positional_audio, POSITIONAL_MIN_DISTANCE_KEY, fAudioMinDistance)      \
	PROCESS(positional_audio, POSITIONAL_MAX_DISTANCE_KEY, fAudioMaxDistance)      \
	PROCESS(positional_audio, POSITIONAL_MIN_VOLUME_KEY, fAudioMaxDistVolume)      \
	PROCESS(positional_audio, POSITIONAL_BLOOM_KEY, fAudioBloom)                   \
	PROCESS(positional_audio, POSITIONAL_HEADPHONE_MODE_KEY, bPositionalHeadphone) \
	PROCESS(positional_audio, POSITIONAL_TRANSMIT_POSITION_KEY, bTransmitPosition)


#define NETWORK_SETTINGS                                                     \
	PROCESS(network, JITTER_BUFFER_SIZE_KEY, iJitterBufferSize)              \
	PROCESS(network, FRAMES_PER_PACKET_KEY, iFramesPerPacket)                \
	PROCESS(network, RESTRICT_TO_TCP_KEY, bTCPCompat)                        \
	PROCESS(network, USE_QUALITY_OF_SERVICE_KEY, bQoS)                       \
	PROCESS(network, AUTO_RECONNECT_KEY, bReconnect)                         \
	PROCESS(network, AUTO_CONNECT_LAST_SERVER_KEY, bAutoConnect)             \
	PROCESS(network, RECONNECT_TO_LAST_CHANNEL_KEY, bReconnectToLastChannel) \
	PROCESS(network, START_WITH_PC_KEY, bStartWithPC)                        \
	PROCESS(network, PROXY_TYPE_KEY, ptProxyType)                            \
	PROCESS(network, PROXY_HOST_KEY, qsProxyHost)                            \
	PROCESS(network, PROXY_PORT_KEY, usProxyPort)                            \
	PROCESS(network, PROXY_USERNAME_KEY, qsProxyUsername)                    \
	PROCESS(network, PROXY_PASSWORD_KEY, qsProxyPassword)                    \
	PROCESS(network, MAX_IMAGE_WIDTH_KEY, iMaxImageWidth)                    \
	PROCESS(network, MAX_IMAGE_HEIGHT_KEY, iMaxImageHeight)                  \
	PROCESS(network, SERVICE_PREFIX_KEY, qsServicePrefix)                    \
	PROCESS(network, MAX_IN_FLIGHT_TCP_PINGS_KEY, iMaxInFlightTCPPings)      \
	PROCESS(network, PING_INTERVAL_KEY, iPingIntervalMsec)                   \
	PROCESS(network, CONNECTION_TIMEOUT_KEY, iConnectionTimeoutDurationMsec) \
	PROCESS(network, FORCE_UDP_BIND_TO_TCP_ADDRESS_KEY, bUdpForceTcpAddr)    \
	PROCESS(network, SSL_CIPHERS_KEY, qsSslCiphers)                          \
	PROCESS(network, ENABLE_LINK_PREVIEWS_KEY, bEnableLinkPreviews)          \
	PROCESS(network, SCREEN_SHARE_DIAGNOSTICS_KEY, bScreenShareDiagnostics)


#define AUDIO_BACKEND_SETTINGS                                                               \
	PROCESS(audio_backend, WASAPI_INPUT_KEY, qsWASAPIInput)                                  \
	PROCESS(audio_backend, WASAPI_OUTPUT_KEY, qsWASAPIOutput)                                \
	PROCESS(audio_backend, WASAPI_ROLE_KEY, qsWASAPIRole)                                    \
	PROCESS(audio_backend, WASAPI_INPUT_IDENTITY_KEY, qsWASAPIInputDeviceIdentity)           \
	PROCESS(audio_backend, WASAPI_OUTPUT_IDENTITY_KEY, qsWASAPIOutputDeviceIdentity)         \
	PROCESS(audio_backend, WASAPI_INPUT_ROUTING_POLICY_KEY, qsWASAPIInputRoutingPolicy)      \
	PROCESS(audio_backend, WASAPI_OUTPUT_ROUTING_POLICY_KEY, qsWASAPIOutputRoutingPolicy)    \
	PROCESS(audio_backend, WASAPI_LATENCY_PROFILE_KEY, qsWASAPILatencyProfile)               \
	PROCESS(audio_backend, ALSA_INPUT_KEY, qsALSAInput)               \
	PROCESS(audio_backend, ALSA_OUTPUT_KEY, qsALSAOutput)             \
	PROCESS(audio_backend, PIPEWIRE_INPUT_KEY, pipeWireInput)         \
	PROCESS(audio_backend, PIPEWIRE_OUTPUT_KEY, pipeWireOutput)       \
	PROCESS(audio_backend, PULSEAUDIO_INPUT_KEY, qsPulseAudioInput)   \
	PROCESS(audio_backend, PULSEAUDIO_OUTPUT_KEY, qsPulseAudioOutput) \
	PROCESS(audio_backend, JACK_OUTPUT_KEY, qsJackAudioOutput)        \
	PROCESS(audio_backend, JACK_START_SERVER_KEY, bJackStartServer)   \
	PROCESS(audio_backend, JACK_AUTOCONNECT_KEY, bJackAutoConnect)    \
	PROCESS(audio_backend, JACK_CLIENT_NAME_KEY, qsJackClientName)    \
	PROCESS(audio_backend, OSS_INPUT_KEY, qsOSSInput)                 \
	PROCESS(audio_backend, OSS_OUTPUT_KEY, qsOSSOutput)               \
	PROCESS(audio_backend, COREAUDIO_INPUT_KEY, qsCoreAudioInput)     \
	PROCESS(audio_backend, COREAUDIO_OUTPUT_KEY, qsCoreAudioOutput)   \
	PROCESS(audio_backend, PORTAUDIO_INPUT_KEY, iPortAudioInput)      \
	PROCESS(audio_backend, PORTAUDIO_OUTPUT_KEY, iPortAudioOutput)


#define TTS_SETTINGS                                    \
	PROCESS(tts, TTS_ENABLE_KEY, bTTS)                  \
	PROCESS(tts, TTS_VOLUME_KEY, iTTSVolume)            \
	PROCESS(tts, TTS_THRESHOLD_KEY, iTTSThreshold)      \
	PROCESS(tts, TTS_READBACK_KEY, bTTSMessageReadBack) \
	PROCESS(tts, TTS_IGNORE_SCOPE_KEY, bTTSNoScope)     \
	PROCESS(tts, TTS_IGNORE_AUTHOR_KEY, bTTSNoAuthor)   \
	PROCESS(tts, TTS_LANGAGE_KEY, qsTTSLanguage)


#define PRIVACY_SETTINGS PROCESS(privacy, HIDE_OS_FROM_SERVER_KEY, bHideOS)


#define DANK_MUMBLE_SETTINGS PROCESS(dank_mumble, MODERN_LAYOUT_POLICY_KEY, modernLayoutPolicy)


#define UI_SETTINGS                                                                       \
	PROCESS(ui, LANGUAGE_KEY, qsLanguage)                                                 \
	PROCESS(ui, THEME_KEY, themeName)                                                     \
	PROCESS(ui, THEME_STYLE_KEY, themeStyleName)                                          \
	PROCESS(ui, THEME_DARK_KEY, themeDarkName)                                            \
	PROCESS(ui, THEME_DARK_STYLE_KEY, themeDarkStyleName)                                 \
	PROCESS(ui, THEME_METHOD_KEY, styleType)                                              \
	PROCESS(ui, CHANNEL_EXPANSION_MODE_KEY, ceExpand)                                     \
	PROCESS(ui, CHANNEL_DRAG_MODE_KEY, ceChannelDrag)                                     \
	PROCESS(ui, USER_DRAG_MODE_KEY, ceUserDrag)                                           \
	PROCESS(ui, ALWAYS_ON_TOP_KEY, aotbAlwaysOnTop)                                       \
	PROCESS(ui, QUIT_BEHAVIOR_KEY, quitBehavior)                                          \
	PROCESS(ui, SHOW_DEVELOPER_MENU_KEY, bEnableDeveloperMenu)                            \
	PROCESS(ui, LOCK_LAYOUT_KEY, bLockLayout)                                             \
	PROCESS(ui, MINIMAL_VIEW_KEY, bMinimalView)                                           \
	PROCESS(ui, HIDE_FRAME_KEY, bHideFrame)                                               \
	PROCESS(ui, DISPLAY_USERS_BEFORE_CHANNELS, bUserTop)                                  \
	PROCESS(ui, WINDOW_GEOMETRY_KEY, qbaMainWindowGeometry)                               \
	PROCESS(ui, WINDOW_GEOMETRY_MINIMAL_VIEW_KEY, qbaMinimalViewGeometry)                 \
	PROCESS(ui, WINDOW_STATE_KEY, qbaMainWindowState)                                     \
	PROCESS(ui, WINDOW_STATE_MINIMAL_VIEW_KEY, qbaMinimalViewState)                       \
	PROCESS(ui, MODERN_WINDOW_GEOMETRY_KEY, qbaModernMainWindowGeometry)                  \
	PROCESS(ui, MODERN_AUXILIARY_WINDOW_GEOMETRIES_KEY, qbaModernAuxiliaryWindowGeometries) \
	PROCESS(ui, MODERN_MINIMAL_VIEW_GEOMETRY_KEY, qbaModernMinimalViewGeometry)           \
	PROCESS(ui, MODERN_WINDOW_STATE_KEY, qbaModernMainWindowState)                        \
	PROCESS(ui, MODERN_MINIMAL_VIEW_STATE_KEY, qbaModernMinimalViewState)                 \
	PROCESS(ui, MODERN_SHELL_MOTD_EXPANDED_KEY, bModernShellMotdExpanded)                 \
	PROCESS(ui, MODERN_SHELL_MOTD_DISMISSED_SIGNATURE_KEY, qsModernShellMotdDismissedSignature) \
	PROCESS(ui, MODERN_SHELL_MOTD_LAST_SEEN_SIGNATURE_KEY, qsModernShellMotdLastSeenSignature) \
	PROCESS(ui, MODERN_SHELL_MOTD_SERVER_STATES_KEY, qsModernShellMotdServerStates)       \
	PROCESS(ui, MODERN_SHELL_THEME_KEY, qsModernShellTheme)                               \
	PROCESS(ui, MODERN_SHELL_DENSITY_KEY, qsModernShellDensity)                           \
	PROCESS(ui, MODERN_SHELL_CLASSIC_USER_ICONS_KEY, bModernShellClassicUserIcons)         \
	PROCESS(ui, MODERN_SHELL_RAIL_SIDE_KEY, qsModernShellRailSide)                        \
	PROCESS(ui, MODERN_SHELL_ACCENT_KEY, qsModernShellAccent)                             \
	PROCESS(ui, MODERN_SHELL_CUSTOM_ACCENT_KEY, qsModernShellCustomAccent)                 \
	PROCESS(ui, MODERN_SHELL_CUSTOM_ACCENT_STRENGTH_KEY, iModernShellCustomAccentStrength) \
	PROCESS(ui, MODERN_SHELL_STONKS_PROFILE_SHORTCUT_VISIBLE_KEY,                         \
			bModernShellStonksProfileShortcutVisible)                                      \
	PROCESS(ui, MODERN_SHELL_TICKER_BANNER_ENABLED_KEY, bModernShellTickerBannerEnabled)  \
	PROCESS(ui, MODERN_SHELL_TICKER_PLACEMENT_KEY, qsModernShellTickerPlacement)          \
	PROCESS(ui, MODERN_SHELL_TICKER_DIRECTION_KEY, qsModernShellTickerDirection)          \
	PROCESS(ui, MODERN_SHELL_TICKER_SPEED_KEY, qsModernShellTickerSpeed)                  \
	PROCESS(ui, CONFIG_GEOMETRY_KEY, qbaConfigGeometry)                                   \
	PROCESS(ui, IMAGE_PREVIEW_GEOMETRY_KEY, qbaImagePreviewGeometry)                      \
	PROCESS(ui, WINDOW_LAYOUT_KEY, wlWindowLayout)                                        \
	PROCESS(ui, SERVER_FILTER_MODE_KEY, ssFilter)                                         \
	PROCESS(ui, HIDE_IN_TRAY_KEY, bHideInTray)                                            \
	PROCESS(ui, DISPLAY_TALKING_STATE_IN_TRAY_KEY, bStateInTray)                          \
	PROCESS(ui, SEND_USAGE_STATISTICS_KEY, bUsage)                                        \
	PROCESS(ui, DISPLAY_VOLUME_ADJUSTMENTS_KEY, bShowVolumeAdjustments)                   \
	PROCESS(ui, DISPLAY_NICKNAMES_ONLY_KEY, bShowNicknamesOnly)                           \
	PROCESS(ui, SELECTED_ITEM_AS_CHATBAR_TARGET_KEY, bChatBarUseSelection)                \
	PROCESS(ui, AUTO_SWITCH_MODERN_LAYOUT_KEY, bAutoSwitchModernOnCompatibleServers)      \
	PROCESS(ui, PRESENCE_IDLE_TIMEOUT_MINUTES_KEY, iPresenceIdleTimeoutMinutes)           \
	PROCESS(ui, FILTER_HIDES_EMPTY_CHANNEL_KEY, bFilterHidesEmptyChannels)                \
	PROCESS(ui, FILTER_ACTIVE_KEY, bFilterActive)                                         \
	PROCESS(ui, CONTEXT_MENU_ENTRIES_IN_MENU_BAR_KEY, bShowContextMenuInMenuBar)          \
	PROCESS(ui, CONNECT_DIALOG_GEOMETRY_KEY, qbaConnectDialogGeometry)                    \
	PROCESS(ui, CONNECT_DIALOG_HEADER_STATE_KEY, qbaConnectDialogHeader)                  \
	PROCESS(ui, DISPLAY_TRANSMIT_MODE_COMBOBOX_KEY, bShowTransmitModeComboBox)            \
	PROCESS(ui, SCREEN_SHARE_AUTO_OPEN_CURRENT_ROOM_KEY, bScreenShareAutoOpenCurrentRoom) \
	PROCESS(ui, SCREEN_SHARE_PREFER_IN_APP_RELAY_KEY, bScreenSharePreferInAppRelay)       \
	PROCESS(ui, HIGH_CONTRAST_MODE_KEY, bHighContrast)                                    \
	PROCESS(ui, MAX_LOG_LENGTH_KEY, iMaxLogBlocks)                                        \
	PROCESS(ui, USE_24H_CLOCK_KEY, bLog24HourClock)                                       \
	PROCESS(ui, LOG_MESSAGE_MARGINS_KEY, iChatMessageMargins)                             \
	PROCESS(ui, DISABLE_PUBLIC_SERVER_LIST_KEY, bDisablePublicList)


#define UPDATE_SETTINGS                                         \
	PROCESS(update, CHECK_FOR_UPDATES_KEY, bUpdateCheck)        \
	PROCESS(update, CHECK_FOR_PLUGIN_UPDATES_KEY, bPluginCheck) \
	PROCESS(update, AUTO_UPDATE_PLUGINS_KEY, bPluginAutoUpdate) \
	PROCESS(update, FORK_UPDATE_SNOOZED_SIGNATURE_KEY, qsForkUpdateSnoozedSignature) \
	PROCESS(update, FORK_UPDATE_SNOOZED_UNTIL_MS_KEY, iForkUpdateSnoozedUntilMs)


#define LAST_CONNECTION_SETTINGS                            \
	PROCESS(last_connection, LAST_USERNAME_KEY, qsUsername) \
	PROCESS(last_connection, LAST_SERVER_NAME_KEY, qsLastServer)


#define CHANNEL_HIERARCHY_SETTINGS PROCESS(channel_hierarchy, CHANNEL_NAME_SEPARATOR_KEY, qsHierarchyChannelSeparator)


#define MANUAL_PLUGIN_SETTINGS \
	PROCESS(manual_plugin, MANUALPLUGIN_SILENT_USER_LIFETIME_KEY, manualPlugin_silentUserDisplaytime)


#define PTT_WINDOW_SETTINGS                                          \
	PROCESS(ptt_window, DISPLAY_PTTWINDOW_KEY, bShowPTTButtonWindow) \
	PROCESS(ptt_window, PTTWINDOW_GEOMETRY_KEY, qbaPTTButtonWindowGeometry)


#define RECORDING_SETTINGS                                  \
	PROCESS(recording, RECORDING_PATH_KEY, qsRecordingPath) \
	PROCESS(recording, RECORDING_FILE_KEY, qsRecordingFile) \
	PROCESS(recording, RECORDING_MODE_KEY, rmRecordingMode) \
	PROCESS(recording, RECORDING_FORMAT_KEY, iRecordingFormat)


#define HIDDEN_SETTINGS                                                              \
	PROCESS(hidden, DISABLE_CONNECT_DIALOG_EDITING_KEY, disableConnectDialogEditing) \
	PROCESS(hidden, ADVERTISED_RELEASE_OVERRIDE_KEY, qsAdvertisedReleaseOverride)    \
	PROCESS(hidden, ADVERTISED_OS_OVERRIDE_KEY, qsAdvertisedOSOverride)              \
	PROCESS(hidden, ADVERTISED_OS_VERSION_OVERRIDE_KEY, qsAdvertisedOSVersionOverride)


#define SHORTCUTS_SETTINGS                                                                    \
	PROCESS(shortcuts, ENABLE_GLOBAL_SHORTCUTS_KEY, bShortcutEnable)                          \
	PROCESS(shortcuts, SUPPRESS_MACOS_EVENT_TAPPING_WARNING_KEY, bSuppressMacEventTapWarning) \
	PROCESS(shortcuts, ENABLE_EVDEV_KEY, bEnableEvdev)                                        \
	PROCESS(shortcuts, ENABLE_XINPUT2_KEY, bEnableXInput2)                                    \
	PROCESS(shortcuts, ENABLE_GKEY_KEY, bEnableGKey)                                          \
	PROCESS(shortcuts, ENABLE_XBOX_WIN_KEY, bEnableXboxInput)                                 \
	PROCESS(shortcuts, WIN_UIACCESS_KEY, bEnableUIAccess)


#define SEARCH_SETTINGS                                             \
	PROCESS(search, SEARCH_FOR_USERS_KEY, searchForUsers)           \
	PROCESS(search, SEARCH_FOR_CHANNELS_KEY, searchForChannels)     \
	PROCESS(search, SEARCH_CASE_SENSITIVE_KEY, searchCaseSensitive) \
	PROCESS(search, SEARCH_REGEX_KEY, searchAsRegex)                \
	PROCESS(search, DISPLAY_SEARCH_OPTIONS_KEY, searchOptionsShown) \
	PROCESS(search, SEARCH_USER_ACTION_KEY, searchUserAction)       \
	PROCESS(search, SEARCH_CHANNEL_ACTION_KEY, searchChannelAction) \
	PROCESS(search, SEARCH_WINDOW_POSITION_KEY, searchDialogPosition)


#define PROCESS_ALL_SETTINGS   \
	MISC_SETTINGS              \
	AUDIO_SETTINGS             \
	IDLE_SETTINGS              \
	POSITIONAL_AUDIO_SETTINGS  \
	NETWORK_SETTINGS           \
	AUDIO_BACKEND_SETTINGS     \
	TTS_SETTINGS               \
	PRIVACY_SETTINGS           \
	UI_SETTINGS                \
	DANK_MUMBLE_SETTINGS       \
	UPDATE_SETTINGS            \
	LAST_CONNECTION_SETTINGS   \
	CHANNEL_HIERARCHY_SETTINGS \
	MANUAL_PLUGIN_SETTINGS     \
	PTT_WINDOW_SETTINGS        \
	RECORDING_SETTINGS         \
	HIDDEN_SETTINGS            \
	SHORTCUTS_SETTINGS         \
	SEARCH_SETTINGS


#define PROCESS_ALL_SETTINGS_WITH_INTERMEDIATE_OPERATION \
	MISC_SETTINGS                                        \
	INTERMEDIATE_OPERATION                               \
	AUDIO_SETTINGS                                       \
	INTERMEDIATE_OPERATION                               \
	IDLE_SETTINGS                                        \
	INTERMEDIATE_OPERATION                               \
	POSITIONAL_AUDIO_SETTINGS                            \
	INTERMEDIATE_OPERATION                               \
	NETWORK_SETTINGS                                     \
	INTERMEDIATE_OPERATION                               \
	AUDIO_BACKEND_SETTINGS                               \
	INTERMEDIATE_OPERATION                               \
	TTS_SETTINGS                                         \
	INTERMEDIATE_OPERATION                               \
	PRIVACY_SETTINGS                                     \
	INTERMEDIATE_OPERATION                               \
	UI_SETTINGS                                          \
	INTERMEDIATE_OPERATION                               \
	DANK_MUMBLE_SETTINGS                                 \
	INTERMEDIATE_OPERATION                               \
	UPDATE_SETTINGS                                      \
	INTERMEDIATE_OPERATION                               \
	LAST_CONNECTION_SETTINGS                             \
	INTERMEDIATE_OPERATION                               \
	CHANNEL_HIERARCHY_SETTINGS                           \
	INTERMEDIATE_OPERATION                               \
	MANUAL_PLUGIN_SETTINGS                               \
	INTERMEDIATE_OPERATION                               \
	PTT_WINDOW_SETTINGS                                  \
	INTERMEDIATE_OPERATION                               \
	RECORDING_SETTINGS                                   \
	INTERMEDIATE_OPERATION                               \
	HIDDEN_SETTINGS                                      \
	INTERMEDIATE_OPERATION                               \
	SHORTCUTS_SETTINGS                                   \
	INTERMEDIATE_OPERATION                               \
	SEARCH_SETTINGS                                      \
	INTERMEDIATE_OPERATION


#endif // MUMBLE_MUMBLE_SETTINGS_MACROS_H_
