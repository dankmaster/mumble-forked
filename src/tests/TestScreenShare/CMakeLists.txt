# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestScreenShare TestScreenShare.cpp ../../mumble/ScreenShareViewBackend.cpp)

set_target_properties(TestScreenShare PROPERTIES AUTOMOC ON)
target_compile_definitions(TestScreenShare PRIVATE MUMBLE_HAS_MODERN_UI_MOCKUPS)

find_pkg(Qt6 COMPONENTS Gui REQUIRED)

target_include_directories(TestScreenShare PRIVATE ../../mumble)
target_link_libraries(TestScreenShare PRIVATE shared Qt6::Gui Qt6::Test)

if(TARGET mumble-screen-share-macos-capture)
	target_link_libraries(TestScreenShare PRIVATE mumble-screen-share-macos-capture)
	target_compile_definitions(TestScreenShare PRIVATE MUMBLE_TEST_SCREENCAPTUREKIT=1)
endif()

add_test(NAME TestScreenShare COMMAND $<TARGET_FILE:TestScreenShare>)
