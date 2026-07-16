if(NOT DEFINED DUMPBIN OR "${DUMPBIN}" STREQUAL "")
	message(FATAL_ERROR "mumble-updater dependency verification requires CMAKE_DUMPBIN")
endif()
if(NOT DEFINED UPDATER OR NOT EXISTS "${UPDATER}")
	message(FATAL_ERROR "mumble-updater dependency verification received no executable")
endif()

execute_process(
	COMMAND "${DUMPBIN}" /dependents "${UPDATER}"
	RESULT_VARIABLE dumpbin_result
	OUTPUT_VARIABLE dumpbin_output
	ERROR_VARIABLE dumpbin_error
)
if(NOT dumpbin_result EQUAL 0)
	message(FATAL_ERROR "dumpbin /dependents failed for ${UPDATER}: ${dumpbin_error}")
endif()
string(TOLOWER "${dumpbin_output}" normalized_dependencies)
if(normalized_dependencies MATCHES "(^|[\r\n ])zlib1[.]dll([\r\n ]|$)")
	message(FATAL_ERROR "${UPDATER} dynamically imports zlib1.dll; updater recovery must be self-contained")
endif()
message(STATUS "Verified self-contained updater imports: ${UPDATER}")
