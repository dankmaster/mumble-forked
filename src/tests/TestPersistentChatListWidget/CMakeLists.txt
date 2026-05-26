# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestPersistentChatListWidget
	TestPersistentChatListWidget.cpp
	../../mumble/PersistentChatHistoryModel.cpp
	../../mumble/widgets/PersistentChatListWidget.cpp
	../../mumble/widgets/PersistentChatListWidget.h
)

set_target_properties(TestPersistentChatListWidget PROPERTIES AUTOMOC ON)

find_pkg(Qt6 COMPONENTS Widgets REQUIRED)

target_include_directories(TestPersistentChatListWidget PRIVATE
	"${CMAKE_SOURCE_DIR}/src"
	"${CMAKE_SOURCE_DIR}/src/mumble"
)
target_link_libraries(TestPersistentChatListWidget PRIVATE shared Qt6::Test Qt6::Widgets)

add_test(NAME TestPersistentChatListWidget COMMAND $<TARGET_FILE:TestPersistentChatListWidget>)
