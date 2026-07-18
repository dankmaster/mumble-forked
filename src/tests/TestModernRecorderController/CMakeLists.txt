# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestModernRecorderController
	TestModernRecorderController.cpp
	../../mumble/ModernRecorderController.cpp
	../../mumble/ModernRecorderController.h
)

set_target_properties(TestModernRecorderController PROPERTIES AUTOMOC ON)

target_include_directories(TestModernRecorderController PRIVATE ../../mumble)
target_link_libraries(TestModernRecorderController PRIVATE Qt6::Core Qt6::Test)

add_test(NAME TestModernRecorderController COMMAND $<TARGET_FILE:TestModernRecorderController>)

configure_client_test_runtime(TestModernRecorderController TestModernRecorderController)
