# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestInputEnhancementAuto TestInputEnhancementAuto.cpp)
set_target_properties(TestInputEnhancementAuto PROPERTIES AUTOMOC ON)
target_link_libraries(TestInputEnhancementAuto PRIVATE mumble_client_object_lib Qt6::Test)

add_test(NAME TestInputEnhancementAuto COMMAND $<TARGET_FILE:TestInputEnhancementAuto>)
configure_client_test_runtime(TestInputEnhancementAuto TestInputEnhancementAuto)

add_executable(TestInputEnhancementAutoV2 TestInputEnhancementAutoV2.cpp)
set_target_properties(TestInputEnhancementAutoV2 PROPERTIES AUTOMOC ON)
target_link_libraries(TestInputEnhancementAutoV2 PRIVATE mumble_client_object_lib Qt6::Test)

add_test(NAME TestInputEnhancementAutoV2 COMMAND $<TARGET_FILE:TestInputEnhancementAutoV2>)
configure_client_test_runtime(TestInputEnhancementAutoV2 TestInputEnhancementAutoV2)
