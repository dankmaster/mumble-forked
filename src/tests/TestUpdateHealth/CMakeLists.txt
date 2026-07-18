# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(
	TestUpdateHealth
	TestUpdateHealth.cpp
	"../../UpdateHealth.cpp"
	"../../UpdateHealth.h"
	"../../mumble/UpdateHealthMonitor.cpp"
	"../../mumble/UpdateHealthMonitor.h"
)
set_target_properties(TestUpdateHealth PROPERTIES AUTOMOC ON)
target_include_directories(TestUpdateHealth PRIVATE "${CMAKE_SOURCE_DIR}/src" "${CMAKE_SOURCE_DIR}/src/mumble")
target_link_libraries(TestUpdateHealth PRIVATE nlohmann_json::nlohmann_json Qt6::Test)

add_test(NAME TestUpdateHealth COMMAND $<TARGET_FILE:TestUpdateHealth>)
configure_client_test_runtime(TestUpdateHealth TestUpdateHealth)
