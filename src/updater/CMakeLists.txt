# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

set(MUMBLE_UPDATER_RC "${CMAKE_CURRENT_BINARY_DIR}/mumble-updater.rc")
set(MUMBLE_UPDATER_ICON "${CMAKE_SOURCE_DIR}/icons/mumble.ico")

configure_file("${CMAKE_CURRENT_SOURCE_DIR}/mumble-updater.rc.in" "${MUMBLE_UPDATER_RC}")

find_package(ZLIB REQUIRED)

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
			ZLIB::ZLIB
			nlohmann_json::nlohmann_json
			advapi32
			dwmapi
			shell32
	)
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
	endif()
endif()
