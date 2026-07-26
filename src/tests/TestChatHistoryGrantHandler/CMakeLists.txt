# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license.

add_executable(TestChatHistoryGrantHandler TestChatHistoryGrantHandler.cpp)
set_target_properties(TestChatHistoryGrantHandler PROPERTIES AUTOMOC ON)
target_link_libraries(TestChatHistoryGrantHandler PRIVATE mumble_server_object_lib shared Qt6::Test)
add_test(NAME TestChatHistoryGrantHandler COMMAND $<TARGET_FILE:TestChatHistoryGrantHandler>)
set_tests_properties(TestChatHistoryGrantHandler PROPERTIES TIMEOUT 30)
configure_client_test_runtime(TestChatHistoryGrantHandler TestChatHistoryGrantHandler)
