# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

include(FetchContent)

FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.6.0
)

set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "")
# The benchmark binaries are qualification tools, not distributable client
# payload.  Leaving googlebenchmark's install rules enabled makes a
# `cmake --install --prefix <stage>` release build try to install its docs via
# the dependency's cached absolute prefix (and also pollutes the staged app
# with development libraries and headers).
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "Do not install the googlebenchmark dependency" FORCE)

FetchContent_MakeAvailable(googlebenchmark)

if(MSVC)
	target_compile_definitions(benchmark PRIVATE _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
	if(TARGET benchmark_main)
		target_compile_definitions(benchmark_main PRIVATE _SILENCE_CXX17_CODECVT_HEADER_DEPRECATION_WARNING)
	endif()
endif()

add_subdirectory(protocol)
add_subdirectory(AudioReceiverBuffer)
add_subdirectory(SpeechCleanup)
