#----------------------------------------------------------------
# Generated CMake target import file for configuration "MinSizeRel".
#----------------------------------------------------------------

# Commands may need to know the format version.
set(CMAKE_IMPORT_FILE_VERSION 1)

# Import target "SOCI::SQLite3" for configuration "MinSizeRel"
set_property(TARGET SOCI::SQLite3 APPEND PROPERTY IMPORTED_CONFIGURATIONS MINSIZEREL)
set_target_properties(SOCI::SQLite3 PROPERTIES
  IMPORTED_LINK_INTERFACE_LANGUAGES_MINSIZEREL "CXX"
  IMPORTED_LOCATION_MINSIZEREL "${_IMPORT_PREFIX}/lib/soci_sqlite3.lib"
  )

list(APPEND _cmake_import_check_targets SOCI::SQLite3 )
list(APPEND _cmake_import_check_files_for_SOCI::SQLite3 "${_IMPORT_PREFIX}/lib/soci_sqlite3.lib" )

# Commands beyond this point should not need to know the version.
set(CMAKE_IMPORT_FILE_VERSION)
