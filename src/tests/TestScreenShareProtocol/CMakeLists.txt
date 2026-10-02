# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestScreenShareProtocol TestScreenShareProtocol.cpp
	../../screen-helper/ScreenShareProcessJob.cpp
	../../screen-helper/ScreenShareProcessOwner.cpp
	../../screen-helper/ScreenShareHelperServer.cpp)

set_target_properties(TestScreenShareProtocol PROPERTIES AUTOMOC ON)
target_include_directories(TestScreenShareProtocol PRIVATE ../../screen-helper)

target_link_libraries(TestScreenShareProtocol PRIVATE shared Qt6::Test Qt6::Network)

add_test(NAME TestScreenShareProtocol COMMAND $<TARGET_FILE:TestScreenShareProtocol>)
configure_client_test_runtime(TestScreenShareProtocol TestScreenShareProtocol)
