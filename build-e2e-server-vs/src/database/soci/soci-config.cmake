
####### Expanded from @PACKAGE_INIT@ by configure_package_config_file() #######
####### Any changes to this file will be overwritten by the next CMake run ####
####### The input file was soci-config.cmake.in                            ########

get_filename_component(PACKAGE_PREFIX_DIR "${CMAKE_CURRENT_LIST_DIR}/../../../" ABSOLUTE)

macro(set_and_check _var _file)
  set(${_var} "${_file}")
  if(NOT EXISTS "${_file}")
    message(FATAL_ERROR "File or directory ${_file} referenced by variable ${_var} does not exist !")
  endif()
endmacro()

macro(check_required_components _NAME)
  foreach(comp ${${_NAME}_FIND_COMPONENTS})
    if(NOT ${_NAME}_${comp}_FOUND)
      if(${_NAME}_FIND_REQUIRED_${comp})
        set(${_NAME}_FOUND FALSE)
      endif()
    endif()
  endforeach()
endmacro()

####################################################################################


# Auto-generated stub to handle component-wise dependencies
set(__dep_soci_comps  "SOCI::MySQL;SOCI::PostgreSQL;SOCI::SQLite3")
set(__dep_names       "MySQL;PostgreSQL;SQLite3")
set(__dep_dep_targets "MySQL::MySQL;PostgreSQL::PostgreSQL;SQLite::SQLite3")

set(__dep_soci_boost "TRUE")
set(__dep_soci_boost_components "date_time")

set(__prev_module_path "${CMAKE_MODULE_PATH}")
list(APPEND CMAKE_MODULE_PATH "${CMAKE_CURRENT_LIST_DIR}/find_package_files/")

if (${CMAKE_FIND_PACKAGE_NAME}_FIND_QUIETLY)
  set(__quiet "QUIET")
else()
  set(__quiet "")
endif()


if (NOT DEFINED SOCI_FIND_COMPONENTS OR SOCI_FIND_COMPONENTS STREQUAL "")
    # Use all available SOCI components
  set(SOCI_FIND_COMPONENTS "${__dep_soci_comps}")
  list(REMOVE_DUPLICATES SOCI_FIND_COMPONENTS)
  list(TRANSFORM SOCI_FIND_COMPONENTS REPLACE "SOCI::" "")
endif()

# Ensure that the Core target is always included (and as first component)
list(REMOVE_ITEM SOCI_FIND_COMPONENTS Core)
list(INSERT SOCI_FIND_COMPONENTS 0 Core)

# Check (optional) Core dependency on Boost.
if (${__dep_soci_boost})
  if (NOT "${__dep_soci_boost_components}" STREQUAL "")
    set(SOCI_BOOST_COMPONENTS COMPONENTS ${__dep_soci_boost_components})
  endif()

  # Don't use REQUIRED to be able to give a better error message below.
  find_package(Boost ${SOCI_BOOST_COMPONENTS} ${__quiet})

  if (NOT Boost_FOUND)
    set(SOCI_FOUND FALSE)
    set(SOCI_NOT_FOUND_MESSAGE "Unmet dependency 'Boost' for SOCI component 'Core'")
  else()
    # Check for all the components too
    foreach (__soci_boost_component IN LISTS __dep_soci_boost_components)
      if (NOT TARGET Boost::${__soci_boost_component})
        set(SOCI_FOUND FALSE)
        set(SOCI_NOT_FOUND_MESSAGE "Unmet dependency 'Boost::${__soci_boost_component}' for SOCI component 'Core'")
      endif()
    endforeach()
  endif()
endif()

list(LENGTH __dep_soci_comps __list_size)
foreach (__item IN ITEMS __dep_names __dep_dep_targets)
    list(LENGTH ${__item} __current_size)
    if (NOT (__list_size EQUAL __current_size))
        message(FATAL_ERROR "SociConfig is invalid -> dependency lists have different sizes")
    endif()
endforeach()
unset(__current_size)

foreach(__comp IN LISTS SOCI_FIND_COMPONENTS)
  if (NOT EXISTS "${CMAKE_CURRENT_LIST_DIR}/SOCI${__comp}Targets.cmake")
    set(SOCI_FOUND FALSE)
    set(SOCI_NOT_FOUND_MESSAGE "'${__comp}' is not a known SOCI component")
    continue()
  endif()

  # Handle component-specific dependencies
  set(__link_targets)
  set(__skip_dependency FALSE)
  foreach (__i RANGE ${__list_size})
    # We want to iterate up to and excluding __list_size, but CMake RANGE
    # doesn't allow this, so do it manually here.
    if (${__i} EQUAL ${__list_size})
      break()
    endif()

    list(GET __dep_soci_comps ${__i} __dep_comp)
    if (__dep_comp MATCHES "::${__comp}$")
      # This entry matches the current component
      list(GET __dep_names ${__i} __dep)
      list(GET __dep_dep_targets ${__i} __targets)

      # Split list-valued entries to become actual lists
      string(REPLACE "|" ";" __targets "${__targets}")

      set(__already_found)
      foreach (__tgt IN LISTS __targets)
        if (TARGET ${__tgt})
          set(__already_found ON)
        else()
          set(__already_found OFF)
          break()
        endif()
      endforeach()

      if (__already_found)
        continue()
      endif()

      find_package(
        ${__dep}
        ${__quiet}
      )

      if (NOT ${__dep}_FOUND)
        set(SOCI_FOUND FALSE)
        set(SOCI_NOT_FOUND_MESSAGE "Unmet dependency '${__dep}' for SOCI component '${__comp}'")
        set(__skip_dependency TRUE)
      endif()

      list(APPEND __link_targets  ${__targets})
    endif()
  endforeach()
  unset(__i)

  if (__skip_dependency)
    continue()
  endif()

  include("${CMAKE_CURRENT_LIST_DIR}/SOCI${__comp}Targets.cmake")

  set_property(
    TARGET SOCI::${__comp}
    APPEND
    PROPERTY INTERFACE_LINK_LIBRARIES "${__link_targets}"
  )
  set(${CMAKE_FIND_PACKAGE_NAME}_${__comp}_FOUND ON)
endforeach()
unset(__comp)


unset(__dep_soci_comps)
unset(__dep_names)
unset(__dep_dep_targets)


check_required_components(SOCI)

if (NOT DEFINED SOCI_FOUND OR SOCI_FOUND)
  add_library(soci_interface INTERFACE)

  foreach (__comp IN LISTS SOCI_FIND_COMPONENTS)
      target_link_libraries(soci_interface INTERFACE SOCI::${__comp})
  endforeach()

  add_library(SOCI::soci ALIAS soci_interface)

  if (NOT SOCI_FIND_QUIETLY)
      list(JOIN SOCI_FIND_COMPONENTS ", " __components)
      message(STATUS "Found SOCI: ${CMAKE_CURRENT_LIST_FILE} (found version \"4.2.0\") found components: ${__components}")
  endif()
endif()

unset(__quiet)

set(CMAKE_MODULE_PATH "${__prev_module_path}")
unset(__prev_module_path)
