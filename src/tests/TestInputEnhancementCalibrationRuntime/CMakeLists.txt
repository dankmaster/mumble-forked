# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestInputEnhancementCalibrationRuntime TestInputEnhancementCalibrationRuntime.cpp)
set_target_properties(TestInputEnhancementCalibrationRuntime PROPERTIES AUTOMOC ON)
target_link_libraries(TestInputEnhancementCalibrationRuntime PRIVATE mumble_client_object_lib Qt6::Test)

add_test(NAME TestInputEnhancementCalibrationRuntime COMMAND $<TARGET_FILE:TestInputEnhancementCalibrationRuntime>)
configure_client_test_runtime(TestInputEnhancementCalibrationRuntime TestInputEnhancementCalibrationRuntime)
