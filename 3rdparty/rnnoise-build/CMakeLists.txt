# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

include(FetchContent)

set(RNNOISE_SRC_DIR "${CMAKE_CURRENT_SOURCE_DIR}/../rnnoise-src")
set(RNNOISE_MODEL_URL_PREFIX "https://media.xiph.org/rnnoise/models/rnnoise_data-" CACHE STRING "RNNoise model archive URL prefix")
set(RNNOISE_MODEL_ARCHIVE "" CACHE FILEPATH "Optional local RNNoise model archive to use instead of downloading during configure")

if(NOT EXISTS "${RNNOISE_SRC_DIR}/COPYING")
	message(FATAL_ERROR
		"${RNNOISE_SRC_DIR} was not found.\n"
		"Please checkout the submodule:\n"
		"git submodule update --init --recursive"
	)
endif()

if(WIN32)
	add_library(rnnoise SHARED)
	set_target_properties(rnnoise PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
	if(MINGW)
		# Remove "lib" prefix.
		set_target_properties(rnnoise PROPERTIES PREFIX "")
	endif()
	target_compile_definitions(rnnoise
		PRIVATE
			"WIN32"
			"DLL_EXPORT"
	)
else()
	add_library(rnnoise STATIC)
endif()

target_compile_definitions(rnnoise PRIVATE "HAVE_CONFIG_H")

target_include_directories(rnnoise
	PRIVATE SYSTEM
		${CMAKE_CURRENT_SOURCE_DIR}
		"${CMAKE_CURRENT_BINARY_DIR}/generated"
	PUBLIC SYSTEM
		"${RNNOISE_SRC_DIR}/include"
		"${RNNOISE_SRC_DIR}/src"
)

file(READ "${RNNOISE_SRC_DIR}/model_version" rnnoise_model_hash)
string(STRIP "${rnnoise_model_hash}" rnnoise_model_hash)

if(NOT RNNOISE_MODEL_ARCHIVE AND DEFINED ENV{MUMBLE_RNNOISE_MODEL_ARCHIVE})
	set(RNNOISE_MODEL_ARCHIVE "$ENV{MUMBLE_RNNOISE_MODEL_ARCHIVE}")
endif()

if(NOT RNNOISE_MODEL_ARCHIVE AND DEFINED ENV{RNNOISE_MODEL_ARCHIVE})
	set(RNNOISE_MODEL_ARCHIVE "$ENV{RNNOISE_MODEL_ARCHIVE}")
endif()

if(RNNOISE_MODEL_ARCHIVE)
	file(TO_CMAKE_PATH "${RNNOISE_MODEL_ARCHIVE}" rnnoise_model_archive)
	if(NOT EXISTS "${rnnoise_model_archive}")
		message(FATAL_ERROR "RNNoise model archive was requested but does not exist: ${rnnoise_model_archive}")
	endif()

	file(SHA256 "${rnnoise_model_archive}" rnnoise_model_archive_hash)
	string(TOLOWER "${rnnoise_model_archive_hash}" rnnoise_model_archive_hash)
	if(NOT rnnoise_model_archive_hash STREQUAL rnnoise_model_hash)
		message(FATAL_ERROR
			"RNNoise model archive checksum mismatch for ${rnnoise_model_archive}.\n"
			"Expected: ${rnnoise_model_hash}\n"
			"Actual:   ${rnnoise_model_archive_hash}"
		)
	endif()

	set(rnnoise_model_url "${rnnoise_model_archive}")
	message(STATUS "Using cached RNNoise model archive: ${rnnoise_model_archive}")
else()
	string(CONCAT rnnoise_model_url "${RNNOISE_MODEL_URL_PREFIX}" "${rnnoise_model_hash}" ".tar.gz")
endif()

FetchContent_Declare(
  rnnoise_model
  URL      "${rnnoise_model_url}"
  URL_HASH SHA256=${rnnoise_model_hash}
  DOWNLOAD_EXTRACT_TIMESTAMP 1
)

FetchContent_MakeAvailable(rnnoise_model)

set(RNNOISE_GENERATED_DIR "${CMAKE_CURRENT_BINARY_DIR}/generated")
set(RNNOISE_ASSET_OUTPUT_DIR "${CMAKE_BINARY_DIR}/rnnoise")
file(MAKE_DIRECTORY "${RNNOISE_GENERATED_DIR}")

add_custom_command(
    OUTPUT
        "${RNNOISE_GENERATED_DIR}/rnnoise_data.c"
        "${RNNOISE_GENERATED_DIR}/rnnoise_data.h"
        "${RNNOISE_GENERATED_DIR}/rnnoise_data_little.c"
        "${RNNOISE_GENERATED_DIR}/rnnoise_data_little.h"

    WORKING_DIRECTORY "${RNNOISE_SRC_DIR}"
    COMMAND "${CMAKE_COMMAND}" -E copy
        "${rnnoise_model_SOURCE_DIR}/src/rnnoise_data.c"
        "${rnnoise_model_SOURCE_DIR}/src/rnnoise_data.h"
        "${rnnoise_model_SOURCE_DIR}/src/rnnoise_data_little.c"
        "${rnnoise_model_SOURCE_DIR}/src/rnnoise_data_little.h"
        "${RNNOISE_GENERATED_DIR}"
    COMMENT "Downloading RNNoise model files"
)

add_custom_target(rnnoise_generated_model_files
	DEPENDS
		"${RNNOISE_GENERATED_DIR}/rnnoise_data.c"
		"${RNNOISE_GENERATED_DIR}/rnnoise_data.h"
		"${RNNOISE_GENERATED_DIR}/rnnoise_data_little.c"
		"${RNNOISE_GENERATED_DIR}/rnnoise_data_little.h"
)

set(RNNOISE_CORE_SOURCES
	"${RNNOISE_SRC_DIR}/src/rnnoise_tables.c"
	"${RNNOISE_SRC_DIR}/src/rnn.c"
	"${RNNOISE_SRC_DIR}/src/pitch.c"
	"${RNNOISE_SRC_DIR}/src/nnet.c"
	"${RNNOISE_SRC_DIR}/src/nnet_default.c"
	"${RNNOISE_SRC_DIR}/src/parse_lpcnet_weights.c"
	"${RNNOISE_SRC_DIR}/src/kiss_fft.c"
	"${RNNOISE_SRC_DIR}/src/denoise.c"
	"${RNNOISE_SRC_DIR}/src/celt_lpc.c"
)

# MSVC does not define the GNU-style __SSE2__ feature macro for x64 even
# though SSE2 is part of the x86-64 ABI. RNNoise consequently selected its
# scalar kernels in Windows release builds. Keep the x64 baseline portable
# and add the upstream runtime dispatch sources so SSE4.1 and AVX2 are only
# entered after CPUID selection; no /arch flag is applied to the library as a
# whole.
set(RNNOISE_X86_64 FALSE)
set(RNNOISE_ARCHITECTURE_HINTS
	"${CMAKE_SYSTEM_PROCESSOR};${CMAKE_C_COMPILER_ARCHITECTURE_ID};${MSVC_C_ARCHITECTURE_ID};${CMAKE_GENERATOR_PLATFORM}"
)
if(CMAKE_SIZEOF_VOID_P EQUAL 8 AND
	RNNOISE_ARCHITECTURE_HINTS MATCHES "(^|;)(AMD64|amd64|x86_64|X86_64|x64)(;|$)")
	set(RNNOISE_X86_64 TRUE)
endif()

if(RNNOISE_X86_64)
	set(RNNOISE_SSE4_1_SOURCE "${RNNOISE_SRC_DIR}/src/x86/nnet_sse4_1.c")
	set(RNNOISE_AVX2_SOURCE "${RNNOISE_SRC_DIR}/src/x86/nnet_avx2.c")
	list(APPEND RNNOISE_CORE_SOURCES
		"${CMAKE_CURRENT_SOURCE_DIR}/x86cpu_mumble.c"
		"${RNNOISE_SRC_DIR}/src/x86/x86_dnn_map.c"
		"${RNNOISE_SSE4_1_SOURCE}"
		"${RNNOISE_AVX2_SOURCE}"
	)

	# MSVC accepts SSE4.1 intrinsics without a whole-target architecture flag.
	# The source-local definition is safe because this object is only reached
	# through RNNoise's CPUID dispatch table.
	set_property(SOURCE "${RNNOISE_SSE4_1_SOURCE}" APPEND PROPERTY
		COMPILE_DEFINITIONS "OPUS_X86_MAY_HAVE_SSE4_1"
	)
	if(MSVC)
		set_property(SOURCE "${RNNOISE_AVX2_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS "/arch:AVX2")
	else()
		set_property(SOURCE "${RNNOISE_SSE4_1_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS "-msse4.1")
		set_property(SOURCE "${RNNOISE_AVX2_SOURCE}" APPEND PROPERTY COMPILE_OPTIONS "-mavx2" "-mfma")
	endif()

	message(STATUS "RNNoise x86 SIMD: SSE2 baseline with runtime SSE4.1/AVX2 dispatch")
endif()

if(BUILD_TESTING)
	add_executable(rnnoise_simd_dispatch_test "${CMAKE_CURRENT_SOURCE_DIR}/simd_dispatch_test.c")
	target_include_directories(rnnoise_simd_dispatch_test PRIVATE "${CMAKE_CURRENT_SOURCE_DIR}")
	add_test(NAME rnnoise_simd_dispatch_guard COMMAND rnnoise_simd_dispatch_test)
	set_tests_properties(rnnoise_simd_dispatch_guard PROPERTIES LABELS "audio;input-enhancement;rnnoise")
endif()

# This translation unit includes the same vec.h selection used by nnet.c and
# turns an accidental scalar x64 configuration into a hard build failure.
list(APPEND RNNOISE_CORE_SOURCES "${CMAKE_CURRENT_SOURCE_DIR}/simd_build_check.c")

function(configure_rnnoise_simd target_name)
	if(NOT RNNOISE_X86_64)
		return()
	endif()

	target_compile_definitions(${target_name} PRIVATE
		"MUMBLE_RNNOISE_REQUIRE_X86_SIMD"
		"OPUS_X86_MAY_HAVE_SSE"
		"OPUS_X86_MAY_HAVE_SSE2"
		"RNN_ENABLE_X86_RTCD"
	)
	if(NOT MSVC)
		target_compile_definitions(${target_name} PRIVATE "CPU_INFO_BY_C")
	endif()
endfunction()

target_sources(rnnoise PRIVATE
	"${RNNOISE_GENERATED_DIR}/rnnoise_data.c"
	${RNNOISE_CORE_SOURCES}
)

configure_rnnoise_simd(rnnoise)

add_dependencies(rnnoise rnnoise_generated_model_files)

file(READ "${RNNOISE_SRC_DIR}/src/write_weights.c" RNNOISE_WRITE_WEIGHTS_SOURCE)
string(REPLACE "\"rnnoise_data.c\"" "\"rnnoise_data_little.c\""
	RNNOISE_WRITE_WEIGHTS_LITTLE_SOURCE "${RNNOISE_WRITE_WEIGHTS_SOURCE}")
string(REPLACE "\"weights_blob.bin\"" "\"rnnoise_little.weights_blob.bin\""
	RNNOISE_WRITE_WEIGHTS_LITTLE_SOURCE "${RNNOISE_WRITE_WEIGHTS_LITTLE_SOURCE}")
string(REPLACE "fopen(\"rnnoise_little.weights_blob.bin\", \"w\")" "fopen(\"rnnoise_little.weights_blob.bin\", \"wb\")"
	RNNOISE_WRITE_WEIGHTS_LITTLE_SOURCE "${RNNOISE_WRITE_WEIGHTS_LITTLE_SOURCE}")
file(WRITE "${RNNOISE_GENERATED_DIR}/write_weights_little.c" "${RNNOISE_WRITE_WEIGHTS_LITTLE_SOURCE}")

add_executable(rnnoise_write_little_weights EXCLUDE_FROM_ALL
	"${RNNOISE_GENERATED_DIR}/write_weights_little.c"
	${RNNOISE_CORE_SOURCES}
)
configure_rnnoise_simd(rnnoise_write_little_weights)
add_dependencies(rnnoise_write_little_weights rnnoise_generated_model_files)
target_compile_definitions(rnnoise_write_little_weights PRIVATE "HAVE_CONFIG_H")
target_include_directories(rnnoise_write_little_weights
	PRIVATE SYSTEM
		${CMAKE_CURRENT_SOURCE_DIR}
		"${RNNOISE_GENERATED_DIR}"
		"${RNNOISE_SRC_DIR}/include"
		"${RNNOISE_SRC_DIR}/src"
)
set_target_properties(rnnoise_write_little_weights PROPERTIES RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR})
target_disable_warnings(rnnoise_write_little_weights)

set(RNNOISE_LITTLE_WEIGHTS_BLOB "${RNNOISE_ASSET_OUTPUT_DIR}/rnnoise_little.weights_blob.bin")
add_custom_command(
	OUTPUT "${RNNOISE_LITTLE_WEIGHTS_BLOB}"
	COMMAND "${CMAKE_COMMAND}" -E make_directory "${RNNOISE_ASSET_OUTPUT_DIR}"
	COMMAND rnnoise_write_little_weights
	WORKING_DIRECTORY "${RNNOISE_ASSET_OUTPUT_DIR}"
	DEPENDS rnnoise_write_little_weights
	COMMENT "Generating RNNoise little weights blob"
)
add_custom_target(rnnoise_little_weights_blob ALL DEPENDS "${RNNOISE_LITTLE_WEIGHTS_BLOB}")

set(RNNOISE_LITTLE_WEIGHTS_BLOB "${RNNOISE_LITTLE_WEIGHTS_BLOB}" PARENT_SCOPE)

target_disable_warnings(rnnoise)
