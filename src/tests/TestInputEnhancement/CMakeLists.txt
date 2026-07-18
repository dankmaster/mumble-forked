# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestInputEnhancement TestInputEnhancement.cpp)
set_target_properties(TestInputEnhancement PROPERTIES AUTOMOC ON)
target_link_libraries(TestInputEnhancement PRIVATE mumble_client_object_lib Qt6::Test)

set(INPUT_ENHANCEMENT_RECIPE_DESCRIPTOR_PATH "${CMAKE_CURRENT_BINARY_DIR}/input-recipes.descriptor.json")
configure_file(
	"${PROJECT_SOURCE_DIR}/src/mumble/input-enhancement/input-recipes.descriptor.json"
	"${INPUT_ENHANCEMENT_RECIPE_DESCRIPTOR_PATH}"
	COPYONLY
)
target_compile_definitions(
	TestInputEnhancement
	PRIVATE MUMBLE_INPUT_ENHANCEMENT_RECIPE_DESCRIPTOR_PATH="${INPUT_ENHANCEMENT_RECIPE_DESCRIPTOR_PATH}"
)

add_test(NAME TestInputEnhancement COMMAND $<TARGET_FILE:TestInputEnhancement>)
configure_client_test_runtime(TestInputEnhancement TestInputEnhancement)
