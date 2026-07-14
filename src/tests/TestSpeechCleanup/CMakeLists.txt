# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

add_executable(TestSpeechCleanup TestSpeechCleanup.cpp)
set_target_properties(TestSpeechCleanup PROPERTIES AUTOMOC ON)
target_link_libraries(TestSpeechCleanup PRIVATE mumble_client_object_lib Qt6::Test)
if(rnnoise)
	target_compile_definitions(TestSpeechCleanup PRIVATE TEST_EXPECT_RNNOISE=1)
endif()
if(TARGET rnnoise)
	target_link_libraries(TestSpeechCleanup PRIVATE rnnoise)
	target_compile_definitions(TestSpeechCleanup PRIVATE TEST_BUNDLED_RNNOISE=1)
endif()
if(TARGET rnnoise_little_weights_blob)
	add_dependencies(TestSpeechCleanup rnnoise_little_weights_blob)
	add_custom_command(
		TARGET TestSpeechCleanup
		POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "$<TARGET_FILE_DIR:TestSpeechCleanup>/rnnoise"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${CMAKE_BINARY_DIR}/rnnoise/rnnoise_little.weights_blob.bin"
			"$<TARGET_FILE_DIR:TestSpeechCleanup>/rnnoise/rnnoise_little.weights_blob.bin"
	)
endif()

if(dtln)
	target_compile_definitions(TestSpeechCleanup PRIVATE TEST_EXPECT_DTLN=1)
	set(TEST_DTLN_MODEL_DIR "$<TARGET_FILE_DIR:TestSpeechCleanup>/dtln")
	add_custom_command(
		TARGET TestSpeechCleanup
		POST_BUILD
		COMMAND ${CMAKE_COMMAND} -E make_directory "${TEST_DTLN_MODEL_DIR}/baseline"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${TEST_DTLN_MODEL_DIR}/norm_500h"
		COMMAND ${CMAKE_COMMAND} -E make_directory "${TEST_DTLN_MODEL_DIR}/norm_40h"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${PROJECT_SOURCE_DIR}/src/mumble/dtln/model_1.onnx"
			"${TEST_DTLN_MODEL_DIR}/baseline/model_1.onnx"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${PROJECT_SOURCE_DIR}/src/mumble/dtln/model_2.onnx"
			"${TEST_DTLN_MODEL_DIR}/baseline/model_2.onnx"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${PROJECT_SOURCE_DIR}/src/mumble/dtln/norm_500h/model_1.onnx"
			"${TEST_DTLN_MODEL_DIR}/norm_500h/model_1.onnx"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${PROJECT_SOURCE_DIR}/src/mumble/dtln/norm_500h/model_2.onnx"
			"${TEST_DTLN_MODEL_DIR}/norm_500h/model_2.onnx"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${PROJECT_SOURCE_DIR}/src/mumble/dtln/norm_40h/model_1.onnx"
			"${TEST_DTLN_MODEL_DIR}/norm_40h/model_1.onnx"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${PROJECT_SOURCE_DIR}/src/mumble/dtln/norm_40h/model_2.onnx"
			"${TEST_DTLN_MODEL_DIR}/norm_40h/model_2.onnx"
	)
endif()

if(deepfilternet)
	target_compile_definitions(TestSpeechCleanup PRIVATE TEST_EXPECT_DEEPFILTERNET=1)
	set(TEST_DEEPFILTERNET_MODEL_DIR "$<TARGET_FILE_DIR:TestSpeechCleanup>/deepfilternet")
	set(TEST_DEEPFILTERNET_RUNTIME "")
	if(TARGET deepfilternet_runtime)
		set(TEST_DEEPFILTERNET_RUNTIME
			"${CMAKE_BINARY_DIR}/src/mumble/deepfilternet-libdf/cargo-target/release/deepfilter.dll"
		)
		add_dependencies(TestSpeechCleanup deepfilternet_runtime)
	elseif(EXISTS "${PROJECT_SOURCE_DIR}/src/mumble/deepfilternet/deepfilter.dll")
		set(TEST_DEEPFILTERNET_RUNTIME "${PROJECT_SOURCE_DIR}/src/mumble/deepfilternet/deepfilter.dll")
	endif()

	set(TEST_DEEPFILTERNET_RUNTIME_COPY_COMMANDS "")
	if(NOT "${TEST_DEEPFILTERNET_RUNTIME}" STREQUAL "")
		list(APPEND TEST_DEEPFILTERNET_RUNTIME_COPY_COMMANDS
			COMMAND ${CMAKE_COMMAND} -E copy_if_different
				"${TEST_DEEPFILTERNET_RUNTIME}"
				"$<TARGET_FILE_DIR:TestSpeechCleanup>/deepfilter.dll"
		)
	endif()
	add_custom_command(
		TARGET TestSpeechCleanup
		POST_BUILD
		${TEST_DEEPFILTERNET_RUNTIME_COPY_COMMANDS}
		COMMAND ${CMAKE_COMMAND} -E make_directory "${TEST_DEEPFILTERNET_MODEL_DIR}"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${PROJECT_SOURCE_DIR}/src/mumble/deepfilternet/DeepFilterNet3_onnx.tar.gz"
			"${TEST_DEEPFILTERNET_MODEL_DIR}/DeepFilterNet3_onnx.tar.gz"
		COMMAND ${CMAKE_COMMAND} -E copy_if_different
			"${PROJECT_SOURCE_DIR}/src/mumble/deepfilternet/DeepFilterNet3_ll_onnx.tar.gz"
			"${TEST_DEEPFILTERNET_MODEL_DIR}/DeepFilterNet3_ll_onnx.tar.gz"
	)
endif()

add_test(NAME TestSpeechCleanup COMMAND $<TARGET_FILE:TestSpeechCleanup>)
configure_client_test_runtime(TestSpeechCleanup TestSpeechCleanup)
