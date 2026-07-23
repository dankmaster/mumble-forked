# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license.

add_executable(TestServerLogRedaction TestServerLogRedaction.cpp)

set_target_properties(TestServerLogRedaction PROPERTIES AUTOMOC ON)

target_link_libraries(TestServerLogRedaction PRIVATE shared Qt6::Test)

add_test(NAME TestServerLogRedaction COMMAND $<TARGET_FILE:TestServerLogRedaction>)
configure_client_test_runtime(TestServerLogRedaction TestServerLogRedaction)
