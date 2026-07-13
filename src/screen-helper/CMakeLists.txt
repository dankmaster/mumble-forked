# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(mumble-screen-helper
	"main.cpp"
	"ScreenShareExternalProcess.cpp"
	"ScreenShareExternalProcess.h"
	"ScreenShareHelperServer.cpp"
	"ScreenShareHelperServer.h"
	"ScreenShareMediaSupport.cpp"
	"ScreenShareMediaSupport.h"
	"ScreenShareRelayClient.cpp"
	"ScreenShareRelayClient.h"
	"ScreenShareSessionPlanner.cpp"
	"ScreenShareSessionPlanner.h"
	"ScreenShareWindowFollowPipeline.cpp"
	"ScreenShareWindowFollowPipeline.h"
	"ScreenShareWindowsNativeCapture.cpp"
	"ScreenShareWindowsNativeCapture.h"
)

set_target_properties(mumble-screen-helper
	PROPERTIES
		AUTOMOC ON
		OUTPUT_NAME "mumble-screen-helper"
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
		UNITY_BUILD OFF
)

target_link_libraries(mumble-screen-helper
	PRIVATE
		shared
		Qt6::Core
		Qt6::Network
)

if(APPLE AND screen-helper-screencapturekit)
	enable_language(OBJCXX)
	find_library(MUMBLE_SCREEN_CAPTURE_KIT_FRAMEWORK ScreenCaptureKit REQUIRED)
	find_library(MUMBLE_CORE_GRAPHICS_FRAMEWORK CoreGraphics REQUIRED)
	find_library(MUMBLE_CORE_MEDIA_FRAMEWORK CoreMedia REQUIRED)
	find_library(MUMBLE_CORE_VIDEO_FRAMEWORK CoreVideo REQUIRED)

	add_library(mumble-screen-share-macos-capture STATIC
		"ScreenShareMacOSNativeCapture.h"
		"ScreenShareMacOSNativeCapture.mm"
	)
	set_source_files_properties("ScreenShareMacOSNativeCapture.mm" PROPERTIES COMPILE_OPTIONS "-fobjc-arc")
	target_include_directories(mumble-screen-share-macos-capture PUBLIC "${CMAKE_CURRENT_SOURCE_DIR}")
	target_link_libraries(mumble-screen-share-macos-capture
		PUBLIC Qt6::Core
		PRIVATE
			${MUMBLE_CORE_GRAPHICS_FRAMEWORK}
			${MUMBLE_CORE_MEDIA_FRAMEWORK}
			${MUMBLE_CORE_VIDEO_FRAMEWORK}
	)
	# Keep the deployment target at 10.15. The implementation runtime-gates all
	# ScreenCaptureKit calls at macOS 12.3, so the framework must be weak-linked.
	target_link_options(mumble-screen-share-macos-capture INTERFACE "LINKER:-weak_framework,ScreenCaptureKit")
	target_compile_definitions(mumble-screen-helper PRIVATE MUMBLE_SCREENSHARE_ENABLE_SCREENCAPTUREKIT=1)
	target_link_libraries(mumble-screen-helper PRIVATE mumble-screen-share-macos-capture)
endif()

if(WIN32 AND screen-helper-cppwinrt-capture)
	target_compile_definitions(mumble-screen-helper PRIVATE MUMBLE_SCREENSHARE_ENABLE_CPPWINRT_CAPTURE=1)
	target_link_libraries(mumble-screen-helper PRIVATE runtimeobject)
endif()

if(APPLE AND TARGET mumble)
	set(MUMBLE_SCREEN_HELPER_INSTALL_DIR "${MUMBLE_INSTALL_EXECUTABLEDIR}/Mumble.app/Contents/MacOS")
	# The development app resolves helpers relative to QCoreApplication::applicationDirPath().
	# Mirror the installed bundle layout in the build tree as well.
	add_custom_command(TARGET mumble-screen-helper POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_BUNDLE_DIR:mumble>/Contents/MacOS"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"$<TARGET_FILE:mumble-screen-helper>"
			"$<TARGET_BUNDLE_DIR:mumble>/Contents/MacOS/$<TARGET_FILE_NAME:mumble-screen-helper>"
		COMMENT "Embedding the macOS screen-share helper in Mumble.app"
	)
else()
	set(MUMBLE_SCREEN_HELPER_INSTALL_DIR "${MUMBLE_INSTALL_EXECUTABLEDIR}")
endif()

install(TARGETS mumble-screen-helper RUNTIME DESTINATION "${MUMBLE_SCREEN_HELPER_INSTALL_DIR}" COMPONENT mumble_client)
