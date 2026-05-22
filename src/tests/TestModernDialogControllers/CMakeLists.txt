# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestModernDialogControllers
	TestModernDialogControllers.cpp
)

set_target_properties(TestModernDialogControllers PROPERTIES AUTOMOC ON)

target_link_libraries(TestModernDialogControllers PRIVATE mumble_client_object_lib shared Qt6::Test)

add_test(NAME TestModernDialogControllers COMMAND $<TARGET_FILE:TestModernDialogControllers>)

configure_client_test_runtime(TestModernDialogControllers TestModernDialogControllers)
