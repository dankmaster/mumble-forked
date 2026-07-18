# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(MUMBLE_UPDATER_RC "${CMAKE_CURRENT_BINARY_DIR}/mumble-updater.rc")
set(MUMBLE_UPDATER_ICON "${CMAKE_SOURCE_DIR}/icons/mumble.ico")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/mumble-updater.rc.in" "${MUMBLE_UPDATER_RC}")

# The updater must remain self-contained while it replaces the application
# payload and while its detached recovery copy runs after a reboot. The shared
# Windows dependency lane exposes ZLIB::ZLIB as an import library for
# zlib1.dll, which is unsafe for that job. Build a private, hash-pinned copy.
include(FetchContent)
set(MUMBLE_UPDATER_ZLIB_URLS
	"https://github.com/madler/zlib/releases/download/v1.3.1/zlib-1.3.1.tar.gz"
	"https://zlib.net/fossils/zlib-1.3.1.tar.gz"
	CACHE STRING "Ordered mirrors for the pinned source archive used only by mumble-updater")
set(MUMBLE_UPDATER_ZLIB_SHA256
	"9a93b2b7dfdac77ceba5a558a580e74667dd6fede4585b91eefb60f03b72df23"
	CACHE STRING "SHA256 of the private mumble-updater zlib source archive")

FetchContent_Declare(mumble_updater_zlib
	URL ${MUMBLE_UPDATER_ZLIB_URLS}
	URL_HASH "SHA256=${MUMBLE_UPDATER_ZLIB_SHA256}"
	DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(ZLIB_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(SKIP_INSTALL_ALL ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(mumble_updater_zlib)
if(NOT TARGET zlibstatic)
	message(FATAL_ERROR "The pinned updater zlib source did not provide zlibstatic")
endif()
if(TARGET zlib)
	set_target_properties(zlib PROPERTIES EXCLUDE_FROM_ALL TRUE)
endif()
set_target_properties(zlibstatic PROPERTIES
	OUTPUT_NAME "mumble-updater-zlib-static"
	MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>"
	# zlib's C translation units intentionally share private identifiers and
	# internal headers that are not safe to concatenate into one unity source.
	# The top-level Windows build enables unity globally, so opt this pinned
	# third-party target out explicitly.
	UNITY_BUILD OFF
)
if(MSVC)
	# The project enables /WX globally. Keep upstream zlib warnings isolated to
	# this pinned third-party target without weakening updater warnings.
	target_compile_options(zlibstatic PRIVATE /W0 /WX-)
endif()
add_library(mumble_updater_zlib_private ALIAS zlibstatic)

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

set(MUMBLE_UPDATER_SOURCES
	"main.cpp"
	"../UpdateHealth.cpp"
	"../UpdateHealth.h"
	"${MUMBLE_UPDATER_RC}"
)

function(configure_mumble_updater_target target output_name)
	target_include_directories(${target} PRIVATE "${CMAKE_SOURCE_DIR}/src")

	set_target_properties(${target}
		PROPERTIES
			OUTPUT_NAME "${output_name}"
			RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
			UNITY_BUILD OFF
	)

	if(MSVC)
		set_property(TARGET ${target} PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
	endif()

	target_link_libraries(${target}
		PRIVATE
			mumble_updater_zlib_private
			nlohmann_json::nlohmann_json
			advapi32
			dwmapi
			shell32
	)

	if(MSVC)
		set(mumble_updater_dumpbin "${CMAKE_DUMPBIN}")
		if(NOT mumble_updater_dumpbin AND CMAKE_LINKER)
			get_filename_component(mumble_msvc_tool_directory "${CMAKE_LINKER}" DIRECTORY)
			find_program(mumble_updater_dumpbin NAMES dumpbin.exe dumpbin
				HINTS "${mumble_msvc_tool_directory}" NO_DEFAULT_PATH)
		endif()
		if(NOT mumble_updater_dumpbin)
			find_program(mumble_updater_dumpbin NAMES dumpbin.exe dumpbin)
		endif()
		if(mumble_updater_dumpbin)
			add_custom_command(TARGET ${target} POST_BUILD
				COMMAND "${CMAKE_COMMAND}"
					"-DDUMPBIN=${mumble_updater_dumpbin}"
					"-DUPDATER=$<TARGET_FILE:${target}>"
					-P "${CMAKE_CURRENT_SOURCE_DIR}/VerifyNoDynamicZlib.cmake"
				VERBATIM
			)
		else()
			message(WARNING
				"dumpbin was unavailable at configure time; release validation must run "
				"scripts/windows/assert-mumble-updater-static-runtime.ps1")
		endif()
	endif()
endfunction()

add_executable(mumble-updater WIN32 ${MUMBLE_UPDATER_SOURCES})
configure_mumble_updater_target(mumble-updater "mumble-updater")

install(TARGETS mumble-updater RUNTIME DESTINATION "${MUMBLE_INSTALL_EXECUTABLEDIR}" COMPONENT mumble_client)

if(tests)
	# Crash injection must never be compiled into the installed/signed updater.
	# Keep an otherwise identical, non-installed test executable for recovery
	# integration coverage.
	add_executable(mumble-updater-test WIN32 ${MUMBLE_UPDATER_SOURCES})
	configure_mumble_updater_target(mumble-updater-test "mumble-updater-test")
	target_compile_definitions(mumble-updater-test PRIVATE MUMBLE_UPDATER_TEST_HOOKS)
	find_program(MUMBLE_PWSH_EXECUTABLE NAMES pwsh powershell)
	if(MUMBLE_PWSH_EXECUTABLE)
		add_test(
			NAME TestUpdaterHealthIntegration
			COMMAND "${MUMBLE_PWSH_EXECUTABLE}" -NoProfile -File
				"${CMAKE_CURRENT_SOURCE_DIR}/tests/invoke-update-health-integration.ps1"
				-UpdaterPath "$<TARGET_FILE:mumble-updater-test>"
				-ProductUpdaterPath "$<TARGET_FILE:mumble-updater>"
		)
		add_test(
			NAME TestUpdaterProtocolV4Simulation
			COMMAND "${MUMBLE_PWSH_EXECUTABLE}" -NoProfile -File
				"${CMAKE_SOURCE_DIR}/scripts/windows/test-input-enhancement-update-protocol-v4.ps1"
		)
	endif()
endif()
