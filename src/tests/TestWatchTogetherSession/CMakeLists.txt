# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestWatchTogetherSession TestWatchTogetherSession.cpp)

set_target_properties(TestWatchTogetherSession PROPERTIES AUTOMOC ON)

target_include_directories(TestWatchTogetherSession PRIVATE ../../murmur)
target_link_libraries(TestWatchTogetherSession PRIVATE shared Qt6::Test)

add_test(NAME TestWatchTogetherSession COMMAND $<TARGET_FILE:TestWatchTogetherSession>)
