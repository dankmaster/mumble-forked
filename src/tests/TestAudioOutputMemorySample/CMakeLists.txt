# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestAudioOutputMemorySample
	TestAudioOutputMemorySample.cpp
)

set_target_properties(TestAudioOutputMemorySample PROPERTIES AUTOMOC ON)
target_link_libraries(TestAudioOutputMemorySample PRIVATE mumble_client_object_lib shared Qt6::Test)

add_test(NAME TestAudioOutputMemorySample COMMAND $<TARGET_FILE:TestAudioOutputMemorySample>)
configure_client_test_runtime(TestAudioOutputMemorySample TestAudioOutputMemorySample)
