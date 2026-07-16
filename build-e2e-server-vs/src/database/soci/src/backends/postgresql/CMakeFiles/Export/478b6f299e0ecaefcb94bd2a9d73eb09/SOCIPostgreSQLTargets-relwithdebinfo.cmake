#----------------------------------------------------------------
# Generated CMake target import file for configuration "RelWithDebInfo".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "SOCI::PostgreSQL" for configuration "RelWithDebInfo"
set_property(TARGET SOCI::PostgreSQL APPEND PROPERTY IMPORTED_CONFIGURATIONS RELWITHDEBINFO)
set_target_properties(SOCI::PostgreSQL PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_RELWITHDEBINFO "CXX"
  IMPORTED_LOCATION_RELWITHDEBINFO "${_IMPORT_PREFIX}/lib/soci_postgresql.lib"
  )

list(APPEND _cmake_import_check_targets SOCI::PostgreSQL )
list(APPEND _cmake_import_check_files_for_SOCI::PostgreSQL "${_IMPORT_PREFIX}/lib/soci_postgresql.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
