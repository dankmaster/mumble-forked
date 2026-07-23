# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license.

add_executable(TestMurmurCredentialHandling TestMurmurCredentialHandling.cpp)

set_target_properties(TestMurmurCredentialHandling PROPERTIES AUTOMOC ON)

target_link_libraries(TestMurmurCredentialHandling PRIVATE mumble_server_object_lib shared Qt6::Test)

add_test(NAME TestMurmurCredentialHandling COMMAND $<TARGET_FILE:TestMurmurCredentialHandling>)
set_tests_properties(TestMurmurCredentialHandling PROPERTIES TIMEOUT 30)
configure_client_test_runtime(TestMurmurCredentialHandling TestMurmurCredentialHandling)
