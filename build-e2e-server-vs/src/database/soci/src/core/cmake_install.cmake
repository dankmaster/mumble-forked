# Install script for directory: D:/Coding/Mumble-input-enhancement/3rdparty/soci/src/core

# Set the install prefix
if(NOT DEFINED CMAKE_INSTALL_PREFIX)
  set(CMAKE_INSTALL_PREFIX "C:/Program Files/mumble")
endif()
string(REGEX REPLACE "/$" "" CMAKE_INSTALL_PREFIX "${CMAKE_INSTALL_PREFIX}")

# Set the install configuration name.
if(NOT DEFINED CMAKE_INSTALL_CONFIG_NAME)
  if(BUILD_TYPE)
    string(REGEX REPLACE "^[^A-Za-z0-9_]+" ""
           CMAKE_INSTALL_CONFIG_NAME "${BUILD_TYPE}")
  else()
    set(CMAKE_INSTALL_CONFIG_NAME "Release")
  endif()
  message(STATUS "Install configuration: \"${CMAKE_INSTALL_CONFIG_NAME}\"")
endif()

# Set the component getting installed.
if(NOT CMAKE_INSTALL_COMPONENT)
  if(COMPONENT)
    message(STATUS "Install component: \"${COMPONENT}\"")
    set(CMAKE_INSTALL_COMPONENT "${COMPONENT}")
  else()
    set(CMAKE_INSTALL_COMPONENT)
  endif()
endif()

# Is this installation the result of a crosscompile?
if(NOT DEFINED CMAKE_CROSSCOMPILING)
  set(CMAKE_CROSSCOMPILING "FALSE")
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "soci_development" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/lib/Debug/soci_core.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/lib/Release/soci_core.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/lib/MinSizeRel/soci_core.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/lib/RelWithDebInfo/soci_core.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "soci_development" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/soci" TYPE FILE FILES
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/backend-loader.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/bind-values.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/blob-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/blob.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-fusion.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-gregorian-date.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-optional.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-tuple.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/callbacks.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/column-info.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/connection-parameters.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/connection-pool.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/error.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/exchange-traits.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/fixed-size-ints.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/into-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/into.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/is-detected.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/log-context.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/logger.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/noreturn.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/once-temp-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/prepare-temp-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/procedure.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/query_transformation.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/ref-counted-prepare-info.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/ref-counted-statement.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/row-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/row.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/rowid-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/rowid.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/rowset.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/session.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-backend.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-platform.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-simple.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-types.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-unicode.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/statement.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/std-optional.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/transaction.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/trivial-blob-backend.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-conversion-traits.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-conversion.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-holder.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-ptr.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-wrappers.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/use-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/use.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/values-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/values.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/version.h"
      "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/include/soci/soci-config.h"
      )
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/soci" TYPE FILE FILES
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/backend-loader.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/bind-values.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/blob-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/blob.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-fusion.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-gregorian-date.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-optional.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-tuple.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/callbacks.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/column-info.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/connection-parameters.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/connection-pool.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/error.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/exchange-traits.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/fixed-size-ints.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/into-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/into.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/is-detected.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/log-context.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/logger.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/noreturn.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/once-temp-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/prepare-temp-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/procedure.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/query_transformation.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/ref-counted-prepare-info.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/ref-counted-statement.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/row-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/row.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/rowid-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/rowid.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/rowset.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/session.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-backend.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-platform.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-simple.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-types.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-unicode.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/statement.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/std-optional.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/transaction.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/trivial-blob-backend.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-conversion-traits.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-conversion.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-holder.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-ptr.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-wrappers.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/use-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/use.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/values-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/values.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/version.h"
      "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/include/soci/soci-config.h"
      )
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/soci" TYPE FILE FILES
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/backend-loader.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/bind-values.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/blob-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/blob.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-fusion.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-gregorian-date.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-optional.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-tuple.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/callbacks.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/column-info.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/connection-parameters.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/connection-pool.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/error.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/exchange-traits.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/fixed-size-ints.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/into-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/into.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/is-detected.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/log-context.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/logger.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/noreturn.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/once-temp-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/prepare-temp-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/procedure.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/query_transformation.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/ref-counted-prepare-info.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/ref-counted-statement.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/row-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/row.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/rowid-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/rowid.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/rowset.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/session.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-backend.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-platform.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-simple.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-types.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-unicode.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/statement.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/std-optional.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/transaction.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/trivial-blob-backend.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-conversion-traits.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-conversion.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-holder.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-ptr.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-wrappers.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/use-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/use.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/values-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/values.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/version.h"
      "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/include/soci/soci-config.h"
      )
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/soci" TYPE FILE FILES
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/backend-loader.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/bind-values.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/blob-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/blob.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-fusion.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-gregorian-date.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-optional.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/boost-tuple.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/callbacks.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/column-info.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/connection-parameters.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/connection-pool.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/error.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/exchange-traits.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/fixed-size-ints.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/into-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/into.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/is-detected.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/log-context.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/logger.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/noreturn.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/once-temp-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/prepare-temp-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/procedure.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/query_transformation.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/ref-counted-prepare-info.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/ref-counted-statement.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/row-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/row.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/rowid-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/rowid.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/rowset.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/session.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-backend.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-platform.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-simple.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-types.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci-unicode.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/soci.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/statement.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/std-optional.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/transaction.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/trivial-blob-backend.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-conversion-traits.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-conversion.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-holder.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-ptr.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/type-wrappers.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/use-type.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/use.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/values-exchange.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/values.h"
      "D:/Coding/Mumble-input-enhancement/3rdparty/soci/include/soci/version.h"
      "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/include/soci/soci-config.h"
      )
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "soci_development" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/soci-4.2.0/SOCICoreTargets.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/soci-4.2.0/SOCICoreTargets.cmake"
         "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/database/soci/src/core/CMakeFiles/Export/478b6f299e0ecaefcb94bd2a9d73eb09/SOCICoreTargets.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/soci-4.2.0/SOCICoreTargets-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/lib/cmake/soci-4.2.0/SOCICoreTargets.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/soci-4.2.0" TYPE FILE FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/database/soci/src/core/CMakeFiles/Export/478b6f299e0ecaefcb94bd2a9d73eb09/SOCICoreTargets.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/soci-4.2.0" TYPE FILE FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/database/soci/src/core/CMakeFiles/Export/478b6f299e0ecaefcb94bd2a9d73eb09/SOCICoreTargets-debug.cmake")
  endif()
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/soci-4.2.0" TYPE FILE FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/database/soci/src/core/CMakeFiles/Export/478b6f299e0ecaefcb94bd2a9d73eb09/SOCICoreTargets-minsizerel.cmake")
  endif()
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/soci-4.2.0" TYPE FILE FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/database/soci/src/core/CMakeFiles/Export/478b6f299e0ecaefcb94bd2a9d73eb09/SOCICoreTargets-relwithdebinfo.cmake")
  endif()
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib/cmake/soci-4.2.0" TYPE FILE FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/database/soci/src/core/CMakeFiles/Export/478b6f299e0ecaefcb94bd2a9d73eb09/SOCICoreTargets-release.cmake")
  endif()
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/database/soci/src/core/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
