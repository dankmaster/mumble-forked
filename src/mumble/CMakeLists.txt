# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(MUMBLE_RC "${CMAKE_CURRENT_BINARY_DIR}/mumble.rc")
set(MUMBLE_PLIST "${CMAKE_CURRENT_BINARY_DIR}/mumble.plist")

set(MUMBLE_ICON "${CMAKE_SOURCE_DIR}/icons/mumble.ico")
set(MUMBLE_ICNS "${CMAKE_SOURCE_DIR}/icons/mumble.icns")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/mumble.rc.in" "${MUMBLE_RC}")
configure_file("${CMAKE_CURRENT_SOURCE_DIR}/mumble.plist.in" "${MUMBLE_PLIST}")

include(qt-utils)
include(install-library)

option(update "Check for updates by default." ON)

option(translations "Include languages other than English." ON)
option(bundle-qt-translations "Bundle Qt's translations as well" ${static}) 

option(bundled-speex "Build the included version of Speex instead of looking for one on the system." ON)
option(rnnoise "Use RNNoise for machine learning noise reduction." ON)
option(bundled-rnnoise "Build the included version of RNNoise instead of looking for one on the system." ${rnnoise})
option(dtln "Use DTLN for machine learning speech cleanup." OFF)
option(deepfilternet "Use DeepFilterNet for machine learning speech cleanup." OFF)
option(webrtc-aec "Expose WebRTC AEC as an experimental echo cancellation option." OFF)
set(MUMBLE_MODERN_LAYOUT_TOOLING_DEFAULT ON)
if(packaging AND "$ENV{GITHUB_ACTIONS}" STREQUAL "true")
	set(MUMBLE_MODERN_LAYOUT_TOOLING_DEFAULT OFF)
endif()
option(modern-layout-automation "Include the Modern UI automation server and JavaScript automation hooks." ${MUMBLE_MODERN_LAYOUT_TOOLING_DEFAULT})
option(modern-layout-mockups "Include Modern shell mockup and walkthrough functionality." ${MUMBLE_MODERN_LAYOUT_TOOLING_DEFAULT})
# Developer-only chat performance tracer. Compiled into local dev clients but kept
# out of GitHub/CI packaging builds (same default policy as the Modern tooling above).
# Even when compiled in, it stays dormant until MUMBLE_CHAT_PERF_TRACE is set at runtime.
option(chat-perf-trace "Include the developer chat performance tracer (mumble::chatperf)." ${MUMBLE_MODERN_LAYOUT_TOOLING_DEFAULT})

function(mumble_strip_marked_shell_block content_var begin_marker end_marker replacement)
	set(content "${${content_var}}")
	string(FIND "${content}" "${begin_marker}" begin_index)
	if(begin_index EQUAL -1)
		message(FATAL_ERROR "Unable to find Modern shell marker '${begin_marker}'")
	endif()
	string(FIND "${content}" "${end_marker}" end_index)
	if(end_index EQUAL -1 OR end_index LESS begin_index)
		message(FATAL_ERROR "Unable to find Modern shell marker '${end_marker}' after '${begin_marker}'")
	endif()

	string(LENGTH "${end_marker}" end_marker_length)
	math(EXPR after_index "${end_index} + ${end_marker_length}")
	string(SUBSTRING "${content}" 0 ${begin_index} before_block)
	string(SUBSTRING "${content}" ${after_index} -1 after_block)
	set(${content_var} "${before_block}${replacement}${after_block}" PARENT_SCOPE)
endfunction()

option(manual-plugin "Include the built-in \"manual\" positional audio plugin." ON)

option(qtspeech "Use Qt's text-to-speech system (part of the Qt Speech module) instead of Mumble's own OS-specific text-to-speech implementations." OFF)

option(jackaudio "Build support for JackAudio." ON)
option(portaudio "Build support for PortAudio" ON)

option(plugin-debug "Build Mumble with debug output for plugin developers." OFF)
option(plugin-callback-debug "Build Mumble with debug output for plugin callbacks inside of Mumble." OFF)

if(WIN32)
	option(wasapi "Build support for WASAPI." ON)
	option(xboxinput "Build support for global shortcuts from Xbox controllers via the XInput DLL." ON)
	option(gkey "Build support for Logitech G-Keys. Note: This feature does not require any build-time dependencies, and requires Logitech Gaming Software to be installed to have any effect at runtime." ON)
elseif(UNIX)
	if(APPLE)
		option(coreaudio "Build support for CoreAudio." ON)
	elseif(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
		option(oss "Build support for OSS." ON)
	elseif(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
		option(alsa "Build support for ALSA." ON)
		option(pipewire "Build support for PipeWire." ON)
		option(pulseaudio "Build support for PulseAudio." ON)
		option(speechd "Build support for Speech Dispatcher." ON)
	endif()

	option(dbus "Enable DBus support/interface" ON)

	# scripts/generate_cmake_options_docs.py does not cope with duplicate option() lines,
	# so single out common options into combined conditions.
	# https://github.com/mumble-voip/mumble/issues/5488
	if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux" OR
	   ${CMAKE_SYSTEM_NAME} STREQUAL "OpenBSD")
		option(xinput2 "Build support for XI2." ON)
	endif()
endif()

if(NOT APPLE)
endif()

if(WIN32 OR APPLE)
	option(crash-report "Include support for reporting crashes to the Mumble developers." ON)
endif()

if(MSVC)
	option(elevation "Set \"uiAccess=true\", required for global shortcuts to work with privileged applications. Requires the client's executable to be signed with a trusted code signing certificate." OFF)
endif()

find_pkg(Qt6
	COMPONENTS
		Concurrent
		Network
		Sql
		Svg
		Widgets
	REQUIRED
)

find_pkg(Qt6
	COMPONENTS
		Qml
		Quick
		QuickControls2
		QuickDialogs2
		WebEngineQuick
	REQUIRED
)

if(TARGET Qt6::WebEngineCore)
	get_target_property(MUMBLE_QT_WEBENGINE_ENABLED_PRIVATE_FEATURES Qt6::WebEngineCore QT_ENABLED_PRIVATE_FEATURES)
	if(MUMBLE_QT_WEBENGINE_ENABLED_PRIVATE_FEATURES
		AND webengine_proprietary_codecs IN_LIST MUMBLE_QT_WEBENGINE_ENABLED_PRIVATE_FEATURES)
		set(MUMBLE_QT_WEBENGINE_PROPRIETARY_CODECS 1)
	else()
		set(MUMBLE_QT_WEBENGINE_PROPRIETARY_CODECS 0)
	endif()
else()
	set(MUMBLE_QT_WEBENGINE_PROPRIETARY_CODECS 0)
endif()

set(MUMBLE_SOURCES
	"Accessibility.cpp"
	"Accessibility.h"
	"API_v_1_x_x.cpp"
	"API.h"
	"Audio.cpp"
	"Audio.h"
	"AudioOutputCache.cpp"
	"AudioOutputCache.h"
	"AudioInput.cpp"
	"AudioInput.h"
	"AudioOutput.cpp"
	"AudioOutput.h"
	"AudioOutputSample.cpp"
	"AudioOutputSample.h"
	"AudioOutputSpeech.cpp"
	"AudioOutputSpeech.h"
	"AudioOutputBuffer.cpp"
	"AudioOutputBuffer.h"
	"AudioOutputToken.h"
	"AudioPreprocessor.cpp"
	"AudioPreprocessor.h"
	"AudioStats.cpp"
	"AudioStats.h"
	"CertService.cpp"
	"CertService.h"
	"ChannelFilterMode.h"
	"ClientUser.cpp"
	"ClientUser.h"
	"ServerConnectionUtils.cpp"
	"ServerConnectionUtils.h"
	"QuitBehavior.h"
	"Database.cpp"
	"Database.h"
	"DeepFilterNetSpeechCleanup.cpp"
	"DeepFilterNetSpeechCleanup.h"
	"DeveloperConsole.cpp"
	"DeveloperConsole.h"
	"DTLNSpeechCleanup.cpp"
	"DTLNSpeechCleanup.h"
	"EchoCancelOption.cpp"
	"EchoCancelOption.h"
	"EnumStringConversions.cpp"
	"EnumStringConversions.h"
	"ScreenShareViewBackend.cpp"
	"ScreenShareViewBackend.h"
	"ScreenShareVideoItem.cpp"
	"ScreenShareVideoItem.h"
	"ScreenShareOperationTracker.h"
	"Global.cpp"
	"Global.h"
	"GlobalShortcut.cpp"
	"GlobalShortcut.h"
	"GlobalShortcutTypes.h"
	"JSONSerialization.cpp"
	"JSONSerialization.h"
	"LegacyPlugin.cpp"
	"LegacyPlugin.h"
	"ListenerVolumeSlider.cpp"
	"ListenerVolumeSlider.h"
	"Log.cpp"
	"Log.h"
	"ClientActionRegistry.cpp"
	"ClientActionRegistry.h"
	"ComposerController.cpp"
	"ComposerController.h"
	"MainWindow.cpp"
	"MainWindow.h"
	"Markdown.cpp"
	"Markdown.h"
	"MenuLabel.cpp"
	"MenuLabel.h"
	"Messages.cpp"
	"ModernConnectController.cpp"
	"ModernConnectController.h"
	"ModernDialogController.cpp"
	"ModernDialogController.h"
	"ModernSettingsController.cpp"
	"ModernSettingsController.h"
	"ModernShellMenuSerializer.cpp"
	"ModernShellMenuSerializer.h"
	"ModernTheme.cpp"
	"ModernTheme.h"
	"MumbleApplication.cpp"
	"MumbleApplication.h"
	"NetworkConfig.cpp"
	"NetworkConfig.h"
	"Plugin.cpp"
	"Plugin.h"
	"PluginInstallService.cpp"
	"PluginInstallService.h"
	"PluginManager.cpp"
	"PluginManager.h"
	"PluginManifest.cpp"
	"PluginManifest.h"
	"PluginUpdater.cpp"
	"PluginUpdatePreparation.cpp"
	"PluginUpdater.h"
	"PositionalData.cpp"
	"PositionalData.h"
	"PersistentChatRender.cpp"
	"PersistentChatRender.h"
	"PersistentChatController.cpp"
	"PersistentChatController.h"
	"PersistentChatGateway.cpp"
	"PersistentChatGateway.h"
	"PersistentChatRenderTypes.h"
	"PersistentChatMediaCache.cpp"
	"PersistentChatMediaCache.h"
	"PersistentChatState.h"
	"PersistentChatStore.cpp"
	"PersistentChatStore.h"
	"QtWidgetUtils.cpp"
	"QtWidgetUtils.h"
	"RNNoiseSpeechCleanup.cpp"
	"RNNoiseSpeechCleanup.h"
	"RichTextEditor.cpp"
	"RichTextEditor.h"
	"Screen.cpp"
	"Screen.h"
	"ScreenShareHelperClient.cpp"
	"ScreenShareHelperClient.h"
	"ScreenShareManager.cpp"
	"ScreenShareManager.h"
	"SearchTypes.cpp"
	"SearchTypes.h"
	"ServerHandler.cpp"
	"ServerHandler.h"
	"SettingsKeys.cpp"
	"SettingsKeys.h"
	"Settings.cpp"
	"Settings.h"
	"SharedMemory.cpp"
	"SharedMemory.h"
	"SocketRPC.cpp"
	"SocketRPC.h"
	"SpeechCleanupProcessor.cpp"
	"SpeechCleanupProcessor.h"
	"SvgIcon.cpp"
	"SvgIcon.h"
	"TextToSpeech.h"
	"ThemeInfo.cpp"
	"ThemeInfo.h"
	"Themes.cpp"
	"Themes.h"
	"UiTheme.cpp"
	"UiTheme.h"
	"Translations.cpp"
	"Translations.h"
	"Usage.cpp"
	"Usage.h"
	"UserListModel.cpp"
	"UserListModel.h"
	"UserLocalVolumeSlider.cpp"
	"UserLocalVolumeSlider.h"
	"UserModel.cpp"
	"UserModel.h"
	"VersionCheck.cpp"
	"VersionCheck.h"
	"ViewCert.cpp"
	"ViewCert.h"
	"VoiceRecorder.cpp"
	"VoiceRecorder.h"
	"VolumeAdjustmentController.cpp"
	"VolumeAdjustmentController.h"
	"WebRTCAudioEchoCanceller.cpp"
	"WebRTCAudioEchoCanceller.h"
	"VolumeSliderWidgetAction.cpp"
	"VolumeSliderWidgetAction.h"
	"WebFetch.cpp"
	"WebFetch.h"
	"XMLTools.cpp"
	"XMLTools.h"

	"widgets/AccessibleQGroupBox.cpp"
	"widgets/AccessibleQGroupBox.h"
	"widgets/ClickableLabel.h"
	"widgets/CompletablePage.cpp"
	"widgets/CompletablePage.h"
	"widgets/EventFilters.cpp"
	"widgets/EventFilters.h"
	"ConnectionFailTypes.h"
	"widgets/MUComboBox.cpp"
	"widgets/MUComboBox.h"
	"widgets/MultiStyleWidgetWrapper.cpp"
	"widgets/MultiStyleWidgetWrapper.h"
	"widgets/MultiColumnTreeWidget.cpp"
	"widgets/MultiColumnTreeWidget.h"
	"widgets/RichTextItemDelegate.cpp"
	"widgets/RichTextItemDelegate.h"
	"widgets/SemanticSlider.cpp"
	"widgets/SemanticSlider.h"
	"widgets/TrayIcon.cpp"
	"widgets/TrayIcon.h"


	"${SHARED_SOURCE_DIR}/ACL.cpp"
	"${SHARED_SOURCE_DIR}/ACL.h"
	"${SHARED_SOURCE_DIR}/Channel.cpp"
	"${SHARED_SOURCE_DIR}/Channel.h"
	"${SHARED_SOURCE_DIR}/ChannelListenerManager.cpp"
	"${SHARED_SOURCE_DIR}/ChannelListenerManager.h"
	"${SHARED_SOURCE_DIR}/Connection.cpp"
	"${SHARED_SOURCE_DIR}/Connection.h"
	"${SHARED_SOURCE_DIR}/Group.cpp"
	"${SHARED_SOURCE_DIR}/Group.h"
	"${SHARED_SOURCE_DIR}/SignalCurry.h"
	"${SHARED_SOURCE_DIR}/User.cpp"
	"${SHARED_SOURCE_DIR}/User.h"

	"mumble.qrc"
	"qml-shell.qrc"
	"${CMAKE_SOURCE_DIR}/themes/DefaultTheme.qrc"
	"${CMAKE_SOURCE_DIR}/themes/CatppuccinTheme.qrc"
)

list(APPEND MUMBLE_SOURCES
	"QmlClientModels.cpp"
	"QmlAccessibilitySnapshot.cpp"
	"QmlAccessibilitySnapshot.h"
	"QmlVisualFixtureController.cpp"
	"QmlVisualFixtureController.h"
	"QmlPerformanceMonitor.cpp"
	"QmlClientModels.h"
	"QmlShellHost.cpp"
	"QmlShellHost.h"
	"QmlImageProvider.cpp"
	"QmlImageProvider.h"
	"QmlThemeController.cpp"
	"QmlThemeController.h"
	"QmlWindowStateController.cpp"
	"QmlWindowStateController.h"
)

if(modern-layout-automation)
	list(APPEND MUMBLE_SOURCES
		"ModernUiAutomationServer.cpp"
		"ModernUiAutomationServer.h"
	)
endif()

add_library(smallft STATIC "${3RDPARTY_DIR}/smallft/smallft.cpp")
target_include_directories(smallft PUBLIC "${3RDPARTY_DIR}/smallft")
target_disable_warnings(smallft)

add_custom_command(
	OUTPUT "${CMAKE_BINARY_DIR}/mumble_flags.qrc"
	COMMAND Python::Interpreter
	ARGS "${CMAKE_SOURCE_DIR}/scripts/generate_flag_qrc.py" --flag-dir "${CMAKE_SOURCE_DIR}/3rdparty/flag-icons/flags/4x3" --output "${CMAKE_BINARY_DIR}/mumble_flags.qrc"
	COMMENT "Generating mumble_flags.qrc"
)
list(APPEND MUMBLE_SOURCES "${CMAKE_BINARY_DIR}/mumble_flags.qrc")

add_library(mumble_client_object_lib OBJECT ${MUMBLE_SOURCES})
target_link_libraries(mumble_client_object_lib PUBLIC smallft)

if(WIN32)
	# The Windows client pulls in a large, macro-heavy source set (WinSDK).
	# Building it outside of unity mode avoids batch-sensitive MSVC failures on the preview runner image.
	set_target_properties(mumble_client_object_lib PROPERTIES UNITY_BUILD OFF)
endif()

if(WIN32 AND NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
	# We don't want the console to appear in release builds.
	add_executable(mumble WIN32 "main.cpp")
else()
	add_executable(mumble "main.cpp")
endif()

if(WIN32)
	target_sources(mumble_client_object_lib
		PRIVATE
			"${CMAKE_SOURCE_DIR}/auxiliary_files/mumble.appcompat.manifest"
			"${MUMBLE_RC}"
	)

	if(elevation)
		set_property(TARGET mumble APPEND_STRING PROPERTY LINK_FLAGS " /MANIFESTUAC:\"level=\'asInvoker\' uiAccess=\'true\'\"")
	endif()
elseif(APPLE)
	set_source_files_properties("${MUMBLE_ICNS}" PROPERTIES MACOSX_PACKAGE_LOCATION "Resources")
	target_sources(mumble PRIVATE "${MUMBLE_ICNS}")

	set_target_properties(mumble
		PROPERTIES
			OUTPUT_NAME "Mumble"
			MACOSX_BUNDLE TRUE
			MACOSX_BUNDLE_INFO_PLIST ${MUMBLE_PLIST}
	)
endif()

add_custom_command(
	OUTPUT "ApplicationPalette.h"
	COMMAND Python::Interpreter
	ARGS ${CMAKE_SOURCE_DIR}/scripts/generate-ApplicationPalette-class.py --template ${CMAKE_CURRENT_SOURCE_DIR}/ApplicationPaletteTemplate.h
		--output ApplicationPalette.h
	COMMENT "Generating ApplicationPalette.h"
	DEPENDS "${CMAKE_CURRENT_SOURCE_DIR}/ApplicationPaletteTemplate.h"
)
target_sources(mumble PRIVATE "ApplicationPalette.h")
target_include_directories(mumble PRIVATE "${CMAKE_CURRENT_BINARY_DIR}")

if(bundled-spdlog)
	target_include_directories(mumble BEFORE PRIVATE "${3RDPARTY_DIR}/spdlog/include")
endif()

target_link_libraries(mumble PRIVATE mumble_client_object_lib shared spdlog::spdlog)
target_link_libraries(mumble PRIVATE CLI11::CLI11)

target_compile_definitions(mumble_client_object_lib
	PUBLIC
	"MUMBLE_LIBRARY_PATH=${MUMBLE_INSTALL_ABS_LIBDIR}"
	"MUMBLE_PLUGIN_PATH=${MUMBLE_INSTALL_ABS_PLUGINDIR}"
)

set_target_properties(mumble PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})

set(MUMBLE_MODERN_THEME_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/themes")
add_custom_command(TARGET mumble POST_BUILD
	COMMAND ${CMAKE_COMMAND} -E remove_directory "$<TARGET_FILE_DIR:mumble>/ModernThemes"
	COMMAND ${CMAKE_COMMAND} -E copy_directory
		"${MUMBLE_MODERN_THEME_SOURCE_DIR}"
		"$<TARGET_FILE_DIR:mumble>/ModernThemes"
	COMMENT "Copying bundled Modern theme examples"
)

set_target_properties(mumble_client_object_lib
	PROPERTIES
		AUTOMOC ON
		AUTORCC ON
		AUTOUIC ON
)

set_target_properties(mumble
	PROPERTIES
		AUTOMOC ON
		AUTORCC ON
		AUTOUIC ON
)

if(WIN32)
	install(TARGETS mumble RUNTIME DESTINATION "${MUMBLE_INSTALL_EXECUTABLEDIR}" COMPONENT mumble_client)
else()
	if(NOT APPLE)
		install(TARGETS mumble RUNTIME DESTINATION "${MUMBLE_INSTALL_EXECUTABLEDIR}" COMPONENT mumble_client)
	else()
		install(TARGETS mumble BUNDLE DESTINATION "${MUMBLE_INSTALL_EXECUTABLEDIR}" COMPONENT mumble_client)
	endif()
endif()

if(APPLE)
	set(MUMBLE_MODERN_THEME_INSTALL_DIR
		"${MUMBLE_INSTALL_EXECUTABLEDIR}/Mumble.app/Contents/MacOS/ModernThemes")
else()
	set(MUMBLE_MODERN_THEME_INSTALL_DIR "${MUMBLE_INSTALL_EXECUTABLEDIR}/ModernThemes")
endif()

install(DIRECTORY "${MUMBLE_MODERN_THEME_SOURCE_DIR}/"
	DESTINATION "${MUMBLE_MODERN_THEME_INSTALL_DIR}"
	COMPONENT mumble_client
	FILES_MATCHING PATTERN "*.mumble-theme.json"
)

target_compile_definitions(mumble_client_object_lib
	PUBLIC
		"MUMBLE"
		"QT_RESTRICTED_CAST_FROM_ASCII"
)

target_include_directories(mumble_client_object_lib
	PUBLIC
		${CMAKE_CURRENT_SOURCE_DIR} # This is required for includes in current folder to be found by files from the shared directory.
		"widgets"
		${SHARED_SOURCE_DIR}
		"${PLUGINS_DIR}"
)

find_pkg(Poco
	COMPONENTS
		XML
		Zip
)

if(TARGET Poco::Zip)
	target_link_libraries(mumble_client_object_lib
		PUBLIC
			Poco::XML
			Poco::Zip
	)
else()
	message(STATUS "Regular Poco search failed - looking for Poco include dir manually...")

	if(MINGW)
		# These are the paths for our MXE environment
		if(32_BIT)
			set(POCO_INCLUDE_DIR_HINT "/usr/lib/mxe/usr/i686-w64-mingw32.static/include/")
		else()
			set(POCO_INCLUDE_DIR_HINT "/usr/lib/mxe/usr/x86_64-w64-mingw32.static/include/")
		endif()
	else()
		set(POCO_INCLUDE_DIR_HINT "/usr/include")
	endif()

	find_path(POCO_INCLUDE_DIR "Poco/Poco.h" HINTS ${POCO_INCLUDE_DIR_HINT})

	if(POCO_INCLUDE_DIR)
		message(STATUS "Found Poco include dir at \"${POCO_INCLUDE_DIR}\"")
	else()
		message(FATAL_ERROR "Unable to locate Poco include directory")
	endif()

	find_library(POCO_LIB_FOUNDATION NAMES PocoFoundation PocoFoundationmd REQUIRED)
	find_library(POCO_LIB_UTIL NAMES PocoUtil PocoUtilmd REQUIRED)
	find_library(POCO_LIB_XML NAMES PocoXML PocoXMLmd REQUIRED)
	find_library(POCO_LIB_ZIP NAMES PocoZip PocoZipmd REQUIRED)

	if(POCO_LIB_ZIP)
		message(STATUS "Found Poco Zip library at \"${POCO_LIB_ZIP}\"")
	else()
		message(FATAL_ERROR "Unable to find Poco Zip library")
	endif()


	# Now use the found include dir and libraries by linking it to the target
	target_include_directories(mumble_client_object_lib
		PUBLIC
			${POCO_INCLUDE_DIR}
	)

	target_link_libraries(mumble_client_object_lib
		PUBLIC
			${POCO_LIB_ZIP}
			${POCO_LIB_XML}
			${POCO_LIB_UTIL}
			${POCO_LIB_FOUNDATION}
	)

	if(static)
		target_compile_definitions(mumble_client_object_lib
			PUBLIC
				POCO_STATIC
		)
	endif()
endif()


if (NOT TARGET nlohmann_json)
	if(bundled-json)
		set(JSON_BuildTests OFF CACHE INTERNAL "")
		set(JSON_ImplicitConversions ON CACHE INTERNAL "")
		set(JSON_SystemInclude ON CACHE INTERNAL "")
		add_subdirectory("${3RDPARTY_DIR}/nlohmann_json/" "${CMAKE_CURRENT_BINARY_DIR}/nlohmann_json/" EXCLUDE_FROM_ALL)
	else()
		find_pkg("nlohmann_json" REQUIRED)
	endif()
endif()

target_link_libraries(mumble_client_object_lib PUBLIC nlohmann_json::nlohmann_json)

find_pkg("SndFile;LibSndFile;sndfile" REQUIRED)

# Check if sndfile version supports opus
if("${sndfile_VERSION}" VERSION_GREATER_EQUAL "1.0.29")
	target_compile_definitions(mumble_client_object_lib PUBLIC USE_SNDFILE_OPUS)
else()
	message(WARNING "libsndfile is missing Opus-support -> No Opus-format recording")
endif()

# Check if sndfile version supports mp3
if("${sndfile_VERSION}" VERSION_GREATER_EQUAL "1.1.0")
	target_compile_definitions(mumble_client_object_lib PUBLIC USE_SNDFILE_MP3)
else()
	message(WARNING "libsndfile is missing Mp3-support -> No Mp3-format recording")
endif()

# Look for various targets as they are named differently on different platforms
if(static AND TARGET sndfile-static)
	target_link_libraries(mumble_client_object_lib PUBLIC sndfile-static)
elseif(TARGET SndFile::sndfile)
	target_link_libraries(mumble_client_object_lib PUBLIC SndFile::sndfile)
elseif(TARGET sndfile)
	target_link_libraries(mumble_client_object_lib PUBLIC sndfile)
else()
	target_link_libraries(mumble_client_object_lib PUBLIC ${sndfile_LIBRARIES})
endif()

target_link_libraries(mumble_client_object_lib
	PUBLIC
		shared
		utfcpp
		Qt6::Concurrent
		Qt6::Network
		Qt6::Sql
		Qt6::Svg
		Qt6::Widgets
		Qt6::Qml
		Qt6::Quick
		Qt6::QuickControls2
		Qt6::QuickDialogs2
)

target_compile_definitions(mumble_client_object_lib
	PUBLIC
		MUMBLE_QT_WEBENGINE_PROPRIETARY_CODECS=${MUMBLE_QT_WEBENGINE_PROPRIETARY_CODECS}
)
if(modern-layout-automation)
	target_compile_definitions(mumble_client_object_lib PUBLIC MUMBLE_HAS_MODERN_UI_AUTOMATION)
endif()
if(modern-layout-mockups)
	target_compile_definitions(mumble_client_object_lib PUBLIC MUMBLE_HAS_MODERN_UI_MOCKUPS)
endif()
target_link_libraries(mumble_client_object_lib
	PUBLIC
		Qt6::WebEngineQuick
)
target_link_libraries(mumble
	PRIVATE
		Qt6::WebEngineQuick
)

if(chat-perf-trace)
	target_compile_definitions(mumble_client_object_lib PUBLIC MUMBLE_HAS_CHAT_PERF_TRACE)
endif()

if(static)
	if(TARGET Qt6::QSQLiteDriverPlugin)
		include_qt_plugin(mumble_client_object_lib PRIVATE "QSQLiteDriverPlugin")
		target_link_libraries(mumble_client_object_lib PUBLIC Qt6::QSQLiteDriverPlugin)
	endif()
	if(TARGET Qt6::QSvgIconPlugin)
		include_qt_plugin(mumble_client_object_lib PRIVATE "QSvgIconPlugin")
		target_link_libraries(mumble_client_object_lib PUBLIC Qt6::QSvgIconPlugin)
	endif()
	if(TARGET Qt6::QSvgPlugin)
		include_qt_plugin(mumble_client_object_lib PRIVATE "QSvgPlugin")
		target_link_libraries(mumble_client_object_lib PUBLIC Qt6::QSvgPlugin)
	endif()
endif()

if(MSVC)
	target_compile_definitions(mumble_client_object_lib PUBLIC "RESTRICT=")
else()
	target_compile_definitions(mumble_client_object_lib PUBLIC "RESTRICT=__restrict__")
endif()

if(WIN32)
	target_sources(mumble_client_object_lib PRIVATE
		"GlobalShortcut_win.cpp"
		"GlobalShortcut_win.h"
		"SharedMemory_win.cpp"
		"TaskList.cpp"
		"UserLockFile_win.cpp"
		"WinGUIDs.cpp"
		"os_early_win.cpp"
		"os_win.cpp"
	)

	# WinGUIDs.cpp includes initguid.h which causes a macro definition in guiddef.h to be
	# changed in such a way that subsequent usages break (redefinition errors).
	# Thus we must not package this source file in a unity build allowing other files
	# to include plain guiddef.h without having to worry about side-effects.
	set_source_files_properties("WinGUIDs.cpp" PROPERTIES SKIP_UNITY_BUILD_INCLUSION TRUE)

	find_pkg(Boost
		COMPONENTS
			system
			thread
		REQUIRED
	)

	if(static AND TARGET Qt6::QWindowsIntegrationPlugin)
		include_qt_plugin(mumble_client_object_lib PRIVATE QWindowsIntegrationPlugin)
		target_link_libraries(mumble_client_object_lib PUBLIC Qt6::QWindowsIntegrationPlugin)
	endif()

	add_subdirectory("${3RDPARTY_DIR}/SPSCQueue" "${CMAKE_CURRENT_BINARY_DIR}/SPSCQueue" EXCLUDE_FROM_ALL)
	add_subdirectory("${3RDPARTY_DIR}/xinputcheck-build" "${CMAKE_CURRENT_BINARY_DIR}/xinputcheck" EXCLUDE_FROM_ALL)

	# Disable all warnings that the xinputcheck code may emit
	disable_warnings_for_all_targets_in("${3RDPARTY_DIR}/xinputcheck-build")

	target_link_libraries(mumble_client_object_lib
		PUBLIC
			SPSCQueue
			xinputcheck
	)

	if(MSVC)
		target_link_libraries(mumble_client_object_lib
			PUBLIC
				Boost::dynamic_linking
				Delayimp.lib
			)
		set_property(TARGET mumble_client_object_lib APPEND_STRING PROPERTY LINK_FLAGS " /DELAYLOAD:user32.dll /DELAYLOAD:qwave.dll")
	endif()

	target_link_libraries(mumble_client_object_lib
		PUBLIC
			Boost::system
			Boost::thread
	)

	target_link_libraries(mumble_client_object_lib
		PUBLIC
			crypt32.lib
			dbghelp.lib
			dnsapi.lib
			dxguid.lib
			hid.lib
			wintrust.lib
	)
else()
	target_sources(mumble_client_object_lib PRIVATE "SharedMemory_unix.cpp")

	if(NOT APPLE)
		find_pkg(X11 COMPONENTS Xext REQUIRED)

		if(xinput2)
			find_pkg(X11 COMPONENTS Xi REQUIRED)
			target_link_libraries(mumble_client_object_lib PUBLIC X11::Xi)
		else()
			target_compile_definitions(mumble_client_object_lib PUBLIC "NO_XINPUT2")
		endif()

		if(static AND TARGET Qt6::QXcbIntegrationPlugin)
			include_qt_plugin(mumble_client_object_lib PRIVATE QXcbIntegrationPlugin)
			target_link_libraries(mumble_client_object_lib PUBLIC Qt6::QXcbIntegrationPlugin)
		endif()

		target_sources(mumble_client_object_lib
			PRIVATE
				"GlobalShortcut_unix.cpp"
				"GlobalShortcut_unix.h"
				"os_unix.cpp"
		)

		if(${CMAKE_SYSTEM_NAME} STREQUAL "Linux")
			find_library(LIB_RT rt)
			target_link_libraries(mumble_client_object_lib PUBLIC ${LIB_RT})
		endif()

		target_link_libraries(mumble_client_object_lib PUBLIC X11::Xext)
	else()
		find_library(LIB_APPKIT "AppKit")
		find_library(LIB_APPLICATIONSERVICES "ApplicationServices")
		find_library(LIB_CARBON "Carbon")
		find_library(LIB_SCRIPTINGBRIDGE "ScriptingBridge")
		find_library(LIB_SECURITY "Security")
		find_library(LIB_XAR "xar")

		target_sources(mumble_client_object_lib
			PRIVATE
				"AppNap.h"
				"AppNap.mm"
				"ThemeSwitch_macx.mm"
				"GlobalShortcut_macx.h"
				"GlobalShortcut_macx.mm"
				"os_macx.mm"
		)

		if(static AND TARGET Qt6::QCocoaIntegrationPlugin)
			include_qt_plugin(mumble_client_object_lib PRIVATE QCocoaIntegrationPlugin)
			target_link_libraries(mumble_client_object_lib PUBLIC Qt6::QCocoaIntegrationPlugin)
		endif()

		target_link_libraries(mumble_client_object_lib
			PUBLIC
				${LIB_APPKIT}
				${LIB_APPLICATIONSERVICES}
				${LIB_CARBON}
				${LIB_SCRIPTINGBRIDGE}
				${LIB_SECURITY}
				${LIB_XAR}
		)
	endif()
endif()

find_pkg("opus;Opus" REQUIRED)
target_include_directories(mumble_client_object_lib PUBLIC ${opus_INCLUDE_DIRS})
target_link_libraries(mumble_client_object_lib PUBLIC ${opus_LIBRARIES})
if(TARGET opus)
	target_link_libraries(mumble_client_object_lib PUBLIC opus)
elseif(TARGET Opus)
	target_link_libraries(mumble_client_object_lib PUBLIC Opus)
elseif(TARGET Opus::opus)
	target_link_libraries(mumble_client_object_lib PUBLIC Opus::opus)
endif()

if(bundled-speex)
	add_subdirectory("${3RDPARTY_DIR}/speexdsp-build" "${CMAKE_CURRENT_BINARY_DIR}/speexdsp" EXCLUDE_FROM_ALL)

	# Disable all warnings that the speexdsp code may emit
	disable_warnings_for_all_targets_in("${3RDPARTY_DIR}/speexdsp-build")

	target_link_libraries(mumble_client_object_lib PUBLIC speexdsp)

	if(WIN32)
		# Shared library on Windows (e.g. ".dll")
		set_target_properties(speexdsp PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
	else()
		# Shared library on UNIX (e.g. ".so")
		set_target_properties(speexdsp PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
	endif()

	install_library(speexdsp mumble_client)
else()
	find_pkg(speexdsp REQUIRED)

	target_link_libraries(mumble_client_object_lib
		PUBLIC
			${speexdsp_LIBRARIES}
	)
endif()

if(rnnoise)
	target_compile_definitions(mumble_client_object_lib PRIVATE "USE_RNNOISE")

	if(bundled-rnnoise)
		add_subdirectory("${3RDPARTY_DIR}/rnnoise-build" "${CMAKE_CURRENT_BINARY_DIR}/rnnoise" EXCLUDE_FROM_ALL)

		# Disable all warnings that the RNNoise code may emit
		disable_warnings_for_all_targets_in("${3RDPARTY_DIR}/rnnoise-build")

		target_link_libraries(mumble_client_object_lib PRIVATE rnnoise)

		if(WIN32)
			# Shared library on Windows (e.g. ".dll")
			set_target_properties(rnnoise PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
		else()
			# Shared library on UNIX (e.g. ".so")
			set_target_properties(rnnoise PROPERTIES LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
		endif()

		if(TARGET rnnoise_little_weights_blob)
			add_dependencies(mumble rnnoise_little_weights_blob)
		endif()

		if(DEFINED RNNOISE_LITTLE_WEIGHTS_BLOB AND NOT "${RNNOISE_LITTLE_WEIGHTS_BLOB}" STREQUAL "")
			install(FILES "${RNNOISE_LITTLE_WEIGHTS_BLOB}"
				DESTINATION "${MUMBLE_INSTALL_EXECUTABLEDIR}/rnnoise"
				COMPONENT mumble_client)
		endif()

		install_library(rnnoise mumble_client)
	else()
		find_pkg(rnnoise REQUIRED)

		target_link_libraries(mumble_client_object_lib PRIVATE ${rnnoise_LIBRARIES})
	endif()
endif()

if(dtln)
	find_package(ONNXRuntime REQUIRED)

	target_compile_definitions(mumble_client_object_lib PRIVATE "USE_DTLN")
	target_include_directories(mumble_client_object_lib SYSTEM PRIVATE ${ONNXRUNTIME_INCLUDE_DIR})
	target_link_libraries(mumble_client_object_lib PRIVATE ONNXRuntime::ONNXRuntime)

	set(DTLN_MODEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/dtln")
	set(DTLN_MODEL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/dtln")
	set(DTLN_MODEL_VARIANTS baseline norm_500h norm_40h)

	foreach(DTLN_MODEL_VARIANT ${DTLN_MODEL_VARIANTS})
		if(DTLN_MODEL_VARIANT STREQUAL "baseline")
			set(DTLN_VARIANT_SOURCE_DIR "${DTLN_MODEL_SOURCE_DIR}")
		else()
			set(DTLN_VARIANT_SOURCE_DIR "${DTLN_MODEL_SOURCE_DIR}/${DTLN_MODEL_VARIANT}")
		endif()

		set(DTLN_VARIANT_MODEL_FILES
			"${DTLN_VARIANT_SOURCE_DIR}/model_1.onnx"
			"${DTLN_VARIANT_SOURCE_DIR}/model_2.onnx"
		)

		foreach(DTLN_VARIANT_MODEL_FILE ${DTLN_VARIANT_MODEL_FILES})
			if(NOT EXISTS "${DTLN_VARIANT_MODEL_FILE}")
				message(FATAL_ERROR "DTLN model file missing: ${DTLN_VARIANT_MODEL_FILE}")
			endif()
		endforeach()

		add_custom_command(TARGET mumble POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E make_directory "${DTLN_MODEL_OUTPUT_DIR}/${DTLN_MODEL_VARIANT}"
			COMMAND ${CMAKE_COMMAND} -E copy_if_different ${DTLN_VARIANT_MODEL_FILES}
				"${DTLN_MODEL_OUTPUT_DIR}/${DTLN_MODEL_VARIANT}"
		)

		install(FILES ${DTLN_VARIANT_MODEL_FILES}
			DESTINATION "${MUMBLE_INSTALL_EXECUTABLEDIR}/dtln/${DTLN_MODEL_VARIANT}"
			COMPONENT mumble_client)
	endforeach()

	if(DEFINED ONNXRUNTIME_RUNTIME AND NOT "${ONNXRUNTIME_RUNTIME}" STREQUAL "")
		add_custom_command(TARGET mumble POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ONNXRUNTIME_RUNTIME}" "$<TARGET_FILE_DIR:mumble>"
		)

		install(FILES "${ONNXRUNTIME_RUNTIME}" DESTINATION "${MUMBLE_INSTALL_EXECUTABLEDIR}" COMPONENT mumble_client)
	endif()
endif()

if(deepfilternet)
	set(DEEPFILTERNET_MODEL_SOURCE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/deepfilternet")
	set(DEEPFILTERNET_MODEL_OUTPUT_DIR "${CMAKE_BINARY_DIR}/deepfilternet")
	file(GLOB DEEPFILTERNET_MODEL_FILES
		"${DEEPFILTERNET_MODEL_SOURCE_DIR}/*.tar.gz"
	)
	file(GLOB DEEPFILTERNET_PACKAGED_RUNTIME_FILES
		"${DEEPFILTERNET_MODEL_SOURCE_DIR}/*.dll"
	)
	set(DEEPFILTERNET_RUNTIME_FILES ${DEEPFILTERNET_PACKAGED_RUNTIME_FILES})
	set(DEEPFILTERNET_RUNTIME_ENABLED OFF)

	if(DEEPFILTERNET_MODEL_FILES)
		foreach(DEEPFILTERNET_MODEL_FILE ${DEEPFILTERNET_MODEL_FILES})
			add_custom_command(TARGET mumble POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E make_directory "${DEEPFILTERNET_MODEL_OUTPUT_DIR}"
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DEEPFILTERNET_MODEL_FILE}" "${DEEPFILTERNET_MODEL_OUTPUT_DIR}"
			)
		endforeach()

		install(FILES ${DEEPFILTERNET_MODEL_FILES} DESTINATION "${MUMBLE_INSTALL_EXECUTABLEDIR}/deepfilternet" COMPONENT mumble_client)
	else()
		message(WARNING "DeepFilterNet support was enabled but no model archives were found in ${DEEPFILTERNET_MODEL_SOURCE_DIR}")
	endif()

	if(WIN32)
		add_subdirectory("${3RDPARTY_DIR}/deepfilternet-libdf-build" "${CMAKE_CURRENT_BINARY_DIR}/deepfilternet-libdf" EXCLUDE_FROM_ALL)
		if(DEEPFILTERNET_RUNTIME_ENABLED AND NOT "${DEEPFILTERNET_RUNTIME_FILE}" STREQUAL "")
			list(APPEND DEEPFILTERNET_RUNTIME_FILES "${DEEPFILTERNET_RUNTIME_FILE}")
		endif()
	endif()

	if(DEEPFILTERNET_RUNTIME_FILES)
		target_compile_definitions(mumble_client_object_lib PRIVATE "USE_DEEPFILTERNET")

		if(TARGET deepfilternet_runtime AND DEEPFILTERNET_RUNTIME_ENABLED)
			add_dependencies(mumble deepfilternet_runtime)
		endif()

		list(REMOVE_DUPLICATES DEEPFILTERNET_RUNTIME_FILES)

		foreach(DEEPFILTERNET_RUNTIME_FILE ${DEEPFILTERNET_RUNTIME_FILES})
			add_custom_command(TARGET mumble POST_BUILD
				COMMAND ${CMAKE_COMMAND} -E copy_if_different "${DEEPFILTERNET_RUNTIME_FILE}" "$<TARGET_FILE_DIR:mumble>"
			)
		endforeach()

		if(DEEPFILTERNET_PACKAGED_RUNTIME_FILES)
			install(FILES ${DEEPFILTERNET_PACKAGED_RUNTIME_FILES} DESTINATION "${MUMBLE_INSTALL_EXECUTABLEDIR}" COMPONENT mumble_client)
		endif()
	else()
		message(WARNING "DeepFilterNet support was enabled but no runtime DLLs were found or configured.")
	endif()

endif()

if(webrtc-aec)
	if(WIN32)
		add_subdirectory("${3RDPARTY_DIR}/webrtc-audio-processing-build" "${CMAKE_CURRENT_BINARY_DIR}/webrtc-audio-processing" EXCLUDE_FROM_ALL)

		# Disable all warnings that the vendored WebRTC APM code may emit.
		disable_warnings_for_all_targets_in("${3RDPARTY_DIR}/webrtc-audio-processing-build")

		target_compile_definitions(mumble_client_object_lib PRIVATE "USE_WEBRTC_AEC")
		target_link_libraries(mumble_client_object_lib PRIVATE webrtc_audio_processing)
	else()
		message(WARNING "WebRTC AEC support is currently only available on Windows builds.")
	endif()
endif()

if(qtspeech)
	find_pkg(Qt6 COMPONENTS TextToSpeech REQUIRED)
	target_sources(mumble_client_object_lib PRIVATE "TextToSpeech.cpp")
	target_link_libraries(mumble_client_object_lib PUBLIC Qt6::TextToSpeech)
elseif(WIN32)
	target_sources(mumble_client_object_lib PRIVATE "TextToSpeech_win.cpp")
	if(MINGW)
		target_link_libraries(mumble_client_object_lib PUBLIC sapi.lib)
	endif()
elseif(APPLE)
	target_sources(mumble_client_object_lib PRIVATE "TextToSpeech_macx.mm")
else()
	target_sources(mumble_client_object_lib PRIVATE "TextToSpeech_unix.cpp")

	if(speechd)
		find_pkg("speech-dispatcher" REQUIRED)

		target_compile_definitions(mumble_client_object_lib
			PUBLIC
				"USE_SPEECHD"
				"USE_SPEECHD_PKGCONFIG"
		)
		target_link_libraries(mumble_client_object_lib PUBLIC ${speech-dispatcher_LIBRARIES})
	else()
		target_compile_definitions(mumble_client_object_lib PUBLIC "USE_NO_TTS")
	endif()
endif()

if(crash-report)
	target_sources(mumble_client_object_lib
		PRIVATE
			"CrashReporter.cpp"
			"CrashReporter.h"
	)
else()
	target_compile_definitions(mumble_client_object_lib PUBLIC "NO_CRASH_REPORT")
endif()

if(manual-plugin)
	target_sources(mumble_client_object_lib
		PRIVATE
			"ManualPlugin.cpp"
			"ManualPlugin.h"
	)

target_compile_definitions(mumble_client_object_lib PUBLIC "USE_MANUAL_PLUGIN")
endif()

if(NOT update)
	target_compile_definitions(mumble_client_object_lib PUBLIC "NO_UPDATE_CHECK")
endif()

if(dbus AND (NOT WIN32 AND NOT APPLE))
	find_pkg(Qt6 COMPONENTS DBus REQUIRED)

	target_sources(mumble_client_object_lib
		PRIVATE
			"DBus.cpp"
			"DBus.h"
	)
	
	target_compile_definitions(mumble_client_object_lib PUBLIC "USE_DBUS")
	target_link_libraries(mumble_client_object_lib PUBLIC Qt6::DBus)
endif()

if(translations)
	# Glob for all translation files in the source dir
	file(GLOB TS_FILES "${CMAKE_CURRENT_SOURCE_DIR}/*.ts")

	include_translations(QRC_FILE ${CMAKE_CURRENT_BINARY_DIR} "${TS_FILES}")
	target_sources(mumble_client_object_lib PRIVATE "${QRC_FILE}")

	if(bundle-qt-translations)
		bundle_qt_translations(mumble_client_object_lib)
	endif()
elseif(bundle-qt-translations)
	message(WARNING "Can't bundle Qt translations if translations are disabled!")
endif()

if(plugins)
	add_dependencies(mumble plugins)
endif()

if(xboxinput)
	target_sources(mumble_client_object_lib
		PRIVATE
			"XboxInput.cpp"
			"XboxInput.h"
	)

	target_compile_definitions(mumble_client_object_lib PUBLIC "USE_XBOXINPUT")
endif()

if(gkey)
	target_sources(mumble_client_object_lib
		PRIVATE
			"GKey.cpp"
			"GKey.h"
	)

	target_compile_definitions(mumble_client_object_lib PUBLIC "USE_GKEY")
endif()

if(zeroconf)
	if(NOT APPLE)
		find_pkg(avahi-compat-libdns_sd QUIET)
		if(avahi-compat-libdns_sd_FOUND)
			target_include_directories(mumble_client_object_lib PUBLIC ${avahi-compat-libdns_sd_INCLUDE_DIRS})
			target_link_libraries(mumble_client_object_lib PUBLIC ${avahi-compat-libdns_sd_LIBRARIES})
		else()
			find_library(LIB_DNSSD "dnssd")
			if(${LIB_DNSSD} STREQUAL "LIB_DNSSD-NOTFOUND")
				message(FATAL_ERROR "DNS-SD library not found!")
			endif()
			target_link_libraries(mumble_client_object_lib PUBLIC ${LIB_DNSSD})
		endif()
	endif()

	target_sources(mumble_client_object_lib
		PRIVATE
			"Zeroconf.cpp"
			"Zeroconf.h"
			# Unlike what the name implies, this 3rdparty helper is not actually related to Bonjour.
			# It just uses the API provided by mDNSResponder, making it compatible with Avahi too.
			"${3RDPARTY_DIR}/qqbonjour/BonjourRecord.h"
			"${3RDPARTY_DIR}/qqbonjour/BonjourServiceBrowser.cpp"
			"${3RDPARTY_DIR}/qqbonjour/BonjourServiceBrowser.h"
			"${3RDPARTY_DIR}/qqbonjour/BonjourServiceResolver.cpp"
			"${3RDPARTY_DIR}/qqbonjour/BonjourServiceResolver.h"
	)

	target_compile_definitions(mumble_client_object_lib PUBLIC "USE_ZEROCONF")
	target_include_directories(mumble_client_object_lib PUBLIC "${3RDPARTY_DIR}/qqbonjour")
endif()

if(alsa)
	find_pkg(ALSA REQUIRED)
	target_sources(mumble_client_object_lib
		PRIVATE
			"ALSAAudio.cpp"
			"ALSAAudio.h"
	)

	target_compile_definitions(mumble_client_object_lib PUBLIC "USE_ALSA")
	target_link_libraries(mumble_client_object_lib PUBLIC ALSA::ALSA)
endif()

if(coreaudio)
	find_library(LIB_AUDIOUNIT "AudioUnit")
	find_library(LIB_COREAUDIO "CoreAudio")

	target_sources(mumble_client_object_lib
		PRIVATE
			"CoreAudio.mm"
			"CoreAudio.h"
	)

	target_link_libraries(mumble_client_object_lib
		PUBLIC
			${LIB_AUDIOUNIT}
			${LIB_COREAUDIO}
			"-framework AVFoundation"
	)
endif()

if(jackaudio)
	target_sources(mumble_client_object_lib
		PRIVATE
			"JackAudio.cpp"
			"JackAudio.h"
	)

	target_compile_definitions(mumble_client_object_lib PUBLIC "USE_JACKAUDIO")
	target_include_directories(mumble_client_object_lib SYSTEM PUBLIC "${3RDPARTY_DIR}/jack")
endif()

if(oss)
	target_sources(mumble_client_object_lib
		PRIVATE
			"OSS.cpp"
			"OSS.h"
	)

	target_compile_definitions(mumble_client_object_lib PUBLIC "USE_OSS")
	target_include_directories(mumble_client_object_lib SYSTEM PUBLIC "/usr/lib/oss/include")
endif()

if(pipewire)
	target_sources(mumble_client_object_lib
		PRIVATE
			"PipeWire.cpp"
			"PipeWire.h"
	)

	target_compile_definitions(mumble_client_object_lib PUBLIC "USE_PIPEWIRE")
	target_include_directories(mumble_client_object_lib SYSTEM PUBLIC "${3RDPARTY_DIR}/pipewire")
endif()

if(portaudio)
	target_sources(mumble_client_object_lib
		PRIVATE
			"PAAudio.cpp"
			"PAAudio.h"
	)

	target_compile_definitions(mumble_client_object_lib PUBLIC "USE_PORTAUDIO")
	target_include_directories(mumble_client_object_lib SYSTEM PUBLIC "${3RDPARTY_DIR}/portaudio")
endif()

if(pulseaudio)
	target_sources(mumble_client_object_lib
		PRIVATE
			"PulseAudio.cpp"
			"PulseAudio.h"
	)

	target_compile_definitions(mumble_client_object_lib PUBLIC "USE_PULSEAUDIO")
	target_include_directories(mumble_client_object_lib SYSTEM PUBLIC "${3RDPARTY_DIR}/pulseaudio")
endif()

if(wasapi)
	target_sources(mumble_client_object_lib
		PRIVATE
			"WASAPI.cpp"
			"WASAPI.h"
			"WASAPINotificationClient.cpp"
			"WASAPINotificationClient.h"
	)

	target_compile_definitions(mumble_client_object_lib PUBLIC "USE_WASAPI")
	target_link_libraries(mumble_client_object_lib PUBLIC avrt.lib)
	if(MINGW)
		target_link_libraries(mumble_client_object_lib PUBLIC ksuser.lib)
	endif()

	if(MSVC)
		set_property(TARGET mumble_client_object_lib APPEND_STRING PROPERTY LINK_FLAGS " /DELAYLOAD:avrt.dll")
	endif()
endif()

if(${CMAKE_VERSION} VERSION_GREATER_EQUAL "3.16.0")
	if (APPLE)
		# Prevent objective C files from being included in unity builds as that causes issues
		set_source_files_properties(
			"AppNap.mm"
			"ThemeSwitch_macx.mm"
			"GlobalShortcut_macx.mm"
			"os_macx.mm"
			"TextToSpeech_macx.mm"
			"CoreAudio.mm"
			PROPERTIES
				SKIP_UNITY_BUILD_INCLUSION TRUE
		)
	elseif(UNIX)
		# Exclude source files that include the X11 headers as these define
		# an awful lot of macros that can conflict with other code
		set_source_files_properties(
			"GlobalShortcut_unix.cpp"
			PROPERTIES
				SKIP_UNITY_BUILD_INCLUSION TRUE
		)
	endif()
endif()

if(plugin-debug)
	target_compile_definitions(mumble_client_object_lib PUBLIC "MUMBLE_PLUGIN_DEBUG")
endif()

if(plugin-callback-debug)
	target_compile_definitions(mumble_client_object_lib PUBLIC "MUMBLE_PLUGIN_CALLBACK_DEBUG")
endif()

if(UNIX)
	if(${CMAKE_SYSTEM_NAME} STREQUAL "FreeBSD")
		# On FreeBSD we need the util library for src/ProcessResolver.cpp to work
		target_link_libraries(mumble_client_object_lib PUBLIC util)
	elseif(${CMAKE_SYSTEM_NAME} MATCHES ".*BSD")
		# On any other BSD we need the kvm library for src/ProcessResolver.cpp to work
		target_link_libraries(mumble_client_object_lib PUBLIC kvm)
	endif()
endif()


# Workaround for a cmake bug that causes the auto-generated UI files (AutoUIC) to not be added
# to the target's interface include directories. This can cause missing include errors in targets
# linking to mumble_client_object_lib.
# cmake bug report: https://gitlab.kitware.com/cmake/cmake/-/issues/17456
set(TARGET_NAME "mumble_client_object_lib")
get_property(_isMultiConfig GLOBAL PROPERTY GENERATOR_IS_MULTI_CONFIG)
if(_isMultiConfig)
  set(AUTOGEN_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_autogen/include_$<CONFIG>)
else()
  set(AUTOGEN_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/${TARGET_NAME}_autogen/include)
endif()

target_include_directories(${TARGET_NAME} INTERFACE
  $<BUILD_INTERFACE:${AUTOGEN_INCLUDE_DIR}>
)
