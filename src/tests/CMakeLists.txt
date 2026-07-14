# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

include(project-utils)

find_pkg(Qt6 COMPONENTS Test REQUIRED)
if(client)
	# Shared-Qt GUI tests need the imported offscreen platform target in the
	# parent directory so every client-test subdirectory can resolve it.
	find_pkg(Qt6 COMPONENTS Gui REQUIRED)
endif()

option(online-tests "Whether or not tests that need a working internet connection should be included" OFF)
option(test-lto "Whether to use LTO when building test cases" ${lto})

set(CMAKE_INTERPROCEDURAL_OPTIMIZATION ${test-lto})

# Set output directory
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY "${CMAKE_BINARY_DIR}/tests")

function(configure_client_test_runtime TEST_NAME TARGET_NAME)
	if(WIN32 AND dtln AND DEFINED ONNXRUNTIME_RUNTIME AND NOT "${ONNXRUNTIME_RUNTIME}" STREQUAL "")
		add_custom_command(
			TARGET ${TARGET_NAME}
			POST_BUILD
			COMMAND ${CMAKE_COMMAND} -E copy_if_different "${ONNXRUNTIME_RUNTIME}" "$<TARGET_FILE_DIR:${TARGET_NAME}>"
		)
	endif()

	set_tests_properties(
		${TEST_NAME}
		PROPERTIES
			WORKING_DIRECTORY "${CMAKE_BINARY_DIR}"
			ENVIRONMENT
				"QT_QPA_PLATFORM=offscreen;QT_QPA_PLATFORM_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>;QT_PLUGIN_PATH=$<TARGET_FILE_DIR:Qt6::QOffscreenIntegrationPlugin>/.."
			# Client tests can directly import in-tree runtime libraries such as
			# rnnoise and speexdsp, which are emitted next to the client executable.
			ENVIRONMENT_MODIFICATION
				"PATH=path_list_prepend:${CMAKE_BINARY_DIR};PATH=path_list_prepend:$<TARGET_FILE_DIR:Qt6::Core>"
	)
endfunction()

if (static AND client)
	find_pkg(Qt6 VERSION 6 COMPONENTS Widgets REQUIRED)
	if (NOT TARGET Qt6::QOffscreenIntegrationPlugin)
		message(FATAL_ERROR "Unable to locate offscreen plugin (needed for tests)")
	endif()
	add_library(mumble_test_qt_plugins STATIC)
	include_qt_plugin(mumble_test_qt_plugins PRIVATE "QOffscreenIntegrationPlugin")
	target_link_libraries(mumble_test_qt_plugins PUBLIC Qt6::Widgets Qt6::QOffscreenIntegrationPlugin)

	link_libraries(mumble_test_qt_plugins)
endif()

if(client)
	add_subdirectory("TestModernDialogControllers")
	add_subdirectory("TestModernShellMenuSerializer")
	add_subdirectory("TestModernTheme")
	add_subdirectory("TestQmlClientModels")
	add_subdirectory("TestQmlImageProvider")
	add_subdirectory("TestQmlMediaProfileFactory")
	add_subdirectory("TestQmlAccessibilitySnapshot")
	add_subdirectory("TestComposerController")
	add_subdirectory("TestQmlQuickComponents")
	add_subdirectory("TestQmlThemeController")
	add_subdirectory("TestQmlPerformanceMonitor")
	add_subdirectory("TestQmlWindowStateController")
	add_subdirectory("TestPluginOperation")
	add_subdirectory("TestPluginAbiWorker")
	add_subdirectory("TestPluginUpdatePreparation")
	add_subdirectory("TestPersistentChatController")
	add_subdirectory("TestPersistentChatRender")
	add_subdirectory("TestScreenShare")
	add_subdirectory("TestScreenShareVideoItem")
	add_subdirectory("TestXMLTools")
	if(NOT "${CMAKE_SYSTEM_NAME}" STREQUAL "FreeBSD")
		# For some reason Qt segfaults when executing this test on FreeBSD without a display (even when using the offscreen plugin)
		add_subdirectory("TestSettingsJSONSerialization")
	endif()
	add_subdirectory("TestServerHandlerState")
	add_subdirectory("TestUserModelNavigator")
endif()

if(server)
	add_subdirectory("TestCrypt")
	add_subdirectory("TestAudioReceiverBuffer")
endif()

# Shared tests
add_subdirectory("TestCaseInsensitiveQString")
add_subdirectory("TestCryptographicHash")
add_subdirectory("TestCryptographicRandom")
add_subdirectory("TestDatabase")
add_subdirectory("TestFFDHE")
add_subdirectory("TestFeedbackReport")
add_subdirectory("TestFinanceQuote")
add_subdirectory("TestChatFeature")
add_subdirectory("TestForkFeature")
add_subdirectory("TestPacketDataStream")
add_subdirectory("TestPasswordGenerator")
add_subdirectory("TestMumbleProtocol")
add_subdirectory("TestSelfSignedCertificate")
add_subdirectory("TestServerAddress")
add_subdirectory("TestScreenShareProtocol")
add_subdirectory("TestSSLLocks")
add_subdirectory("TestStonksCommand")
add_subdirectory("TestStdAbs")
add_subdirectory("TestTimer")
add_subdirectory("TestUnresolvedServerAddress")
add_subdirectory("TestVersion")

if(online-tests)
	message(STATUS "Including online tests - These will fail if you don't have a working internet connection when running them")
	add_subdirectory("TestServerResolver")
else()
	message(STATUS "Omitting online tests - Testing can be performed without an active internet connection")
endif()
