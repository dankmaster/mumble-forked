# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(mumble-updater WIN32
	"main.cpp"
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
		shell32
)

install(TARGETS mumble-updater RUNTIME DESTINATION "${MUMBLE_INSTALL_EXECUTABLEDIR}" COMPONENT mumble_client)
