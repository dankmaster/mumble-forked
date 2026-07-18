# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

find_pkg(Qt6 COMPONENTS Test REQUIRED)

add_executable(
	TestInputEnhancementPackageVerifier
	TestInputEnhancementPackageVerifier.cpp
)
target_include_directories(TestInputEnhancementPackageVerifier PRIVATE ../../mumble)
target_link_libraries(TestInputEnhancementPackageVerifier PRIVATE mumble_client_object_lib Qt6::Test OpenSSL::Crypto)
set_target_properties(TestInputEnhancementPackageVerifier PROPERTIES AUTOMOC ON)

add_test(NAME TestInputEnhancementPackageVerifier COMMAND $<TARGET_FILE:TestInputEnhancementPackageVerifier>)
configure_client_test_runtime(TestInputEnhancementPackageVerifier TestInputEnhancementPackageVerifier)
