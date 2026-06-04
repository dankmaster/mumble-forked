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

add_executable(mumble-updater WIN32
	"main.cpp"
	"${MUMBLE_UPDATER_RC}"
)

set_target_properties(mumble-updater
	PROPERTIES
		OUTPUT_NAME "mumble-updater"
		RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}
		UNITY_BUILD OFF
)

if(MSVC)
	set_property(TARGET mumble-updater PROPERTY MSVC_RUNTIME_LIBRARY "MultiThreaded$<$<CONFIG:Debug>:Debug>")
endif()

target_link_libraries(mumble-updater
	PRIVATE
		ZLIB::ZLIB
		nlohmann_json::nlohmann_json
		advapi32
		dwmapi
		shell32
)

install(TARGETS mumble-updater RUNTIME DESTINATION "${MUMBLE_INSTALL_EXECUTABLEDIR}" COMPONENT mumble_client)
