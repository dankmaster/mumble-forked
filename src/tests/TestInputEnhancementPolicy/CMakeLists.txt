# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

find_pkg(Qt6 COMPONENTS Test REQUIRED)

add_executable(
	TestInputEnhancementPolicy
	TestInputEnhancementPolicy.cpp
	../../mumble/InputEnhancementPolicy.cpp
	../../mumble/InputEnhancementChannelPointer.cpp
)
target_include_directories(TestInputEnhancementPolicy PRIVATE ../../mumble)
target_link_libraries(TestInputEnhancementPolicy PRIVATE Qt6::Test OpenSSL::Crypto)
target_compile_definitions(TestInputEnhancementPolicy PRIVATE
	"MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX=\"${MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX}\"")
set_target_properties(TestInputEnhancementPolicy PROPERTIES AUTOMOC ON)

add_test(NAME TestInputEnhancementPolicy COMMAND $<TARGET_FILE:TestInputEnhancementPolicy>)
configure_client_test_runtime(TestInputEnhancementPolicy TestInputEnhancementPolicy)

# Compile the same tests a second time with a deterministic RFC 8032 public
# key. This covers the configured-release accessor while the primary target
# preserves and checks the normal fail-closed developer-build default.
add_executable(
	TestInputEnhancementPolicyConfiguredKey
	TestInputEnhancementPolicy.cpp
	../../mumble/InputEnhancementPolicy.cpp
	../../mumble/InputEnhancementChannelPointer.cpp
)
target_include_directories(TestInputEnhancementPolicyConfiguredKey PRIVATE ../../mumble)
target_link_libraries(TestInputEnhancementPolicyConfiguredKey PRIVATE Qt6::Test OpenSSL::Crypto)
target_compile_definitions(TestInputEnhancementPolicyConfiguredKey PRIVATE
	MUMBLE_INPUT_ENHANCEMENT_POLICY_PUBLIC_KEY_HEX="d75a980182b10ab7d54bfed3c964073a0ee172f3daa62325af021a68f707511a")
set_target_properties(TestInputEnhancementPolicyConfiguredKey PROPERTIES AUTOMOC ON)
add_test(NAME TestInputEnhancementPolicyConfiguredKey COMMAND $<TARGET_FILE:TestInputEnhancementPolicyConfiguredKey>)
configure_client_test_runtime(TestInputEnhancementPolicyConfiguredKey TestInputEnhancementPolicyConfiguredKey)
