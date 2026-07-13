# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestServerHandlerState TestServerHandlerState.cpp)

set_target_properties(TestServerHandlerState PROPERTIES AUTOMOC ON)

target_link_libraries(TestServerHandlerState PRIVATE mumble_client_object_lib shared Qt6::Test)

add_test(NAME TestServerHandlerState COMMAND $<TARGET_FILE:TestServerHandlerState>)

configure_client_test_runtime(TestServerHandlerState TestServerHandlerState)
