# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestModernServerAdminController
	TestModernServerAdminController.cpp
	../../mumble/ModernServerAdminController.cpp
	../../mumble/ModernServerAdminController.h
)

set_target_properties(TestModernServerAdminController PROPERTIES AUTOMOC ON)

target_include_directories(TestModernServerAdminController PRIVATE ../../mumble)
target_link_libraries(TestModernServerAdminController PRIVATE shared Qt6::Test)

add_test(NAME TestModernServerAdminController COMMAND $<TARGET_FILE:TestModernServerAdminController>)

configure_client_test_runtime(TestModernServerAdminController TestModernServerAdminController)
