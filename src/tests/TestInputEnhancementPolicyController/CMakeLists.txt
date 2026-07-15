# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

find_pkg(Qt6 COMPONENTS Network Test REQUIRED)

add_executable(
	TestInputEnhancementPolicyController
	TestInputEnhancementPolicyController.cpp
	../../mumble/InputEnhancementPolicy.cpp
	../../mumble/InputEnhancementPolicyController.cpp
)
target_include_directories(TestInputEnhancementPolicyController PRIVATE ../../mumble)
target_link_libraries(TestInputEnhancementPolicyController PRIVATE Qt6::Network Qt6::Test OpenSSL::Crypto)
set_target_properties(TestInputEnhancementPolicyController PROPERTIES AUTOMOC ON)

add_test(NAME TestInputEnhancementPolicyController COMMAND $<TARGET_FILE:TestInputEnhancementPolicyController>)
configure_client_test_runtime(TestInputEnhancementPolicyController TestInputEnhancementPolicyController)
