# Copyright The Mumble Developers. All rights reserved.
# Use of this source code is governed by a BSD-style license
# that can be found in the LICENSE file at the root of the
# Mumble source tree or at <https://www.mumble.info/LICENSE>.

if(NOT DEFINED TEST_EXECUTABLE OR "${TEST_EXECUTABLE}" STREQUAL "")
	message(FATAL_ERROR "TEST_EXECUTABLE is required")
endif()

execute_process(
	COMMAND
		"${CMAKE_COMMAND}" -E env
		"--unset=QT_QPA_PLATFORM"
		"--unset=QT_QPA_PLATFORM_PLUGIN_PATH"
		"--unset=QT_PLUGIN_PATH"
		"${TEST_EXECUTABLE}" -functions
	RESULT_VARIABLE direct_launch_result
	OUTPUT_VARIABLE direct_launch_stdout
	ERROR_VARIABLE direct_launch_stderr
	TIMEOUT 10
)

if("${direct_launch_result}" STREQUAL "0")
	message(FATAL_ERROR "Direct launch unexpectedly succeeded")
endif()

set(direct_launch_output "${direct_launch_stdout}\n${direct_launch_stderr}")
string(FIND "${direct_launch_output}" "MUMBLE_CLIENT_TEST_DIRECT_LAUNCH_BLOCKED" guard_marker)
if(guard_marker EQUAL -1)
	message(
		FATAL_ERROR
		"Direct launch did not emit the fail-closed guard marker.\n"
		"Result: ${direct_launch_result}\n"
		"Output:\n${direct_launch_output}"
	)
endif()

message(STATUS "Direct launch failed closed before Qt initialization")
