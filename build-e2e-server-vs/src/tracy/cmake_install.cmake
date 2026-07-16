# Install script for directory: D:/Coding/Mumble-input-enhancement/3rdparty/tracy

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

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/tracy/Debug/TracyClient.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/tracy/Release/TracyClient.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/tracy/MinSizeRel/TracyClient.lib")
  elseif(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/lib" TYPE STATIC_LIBRARY FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/tracy/RelWithDebInfo/TracyClient.lib")
  endif()
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/tracy" TYPE FILE FILES
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/tracy/TracyC.h"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/tracy/Tracy.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/tracy/TracyD3D11.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/tracy/TracyD3D12.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/tracy/TracyLua.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/tracy/TracyOpenCL.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/tracy/TracyOpenGL.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/tracy/TracyVulkan.hpp"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/client" TYPE FILE FILES
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/tracy_concurrentqueue.h"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/tracy_rpmalloc.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/tracy_SPSCQueue.h"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracyArmCpuTable.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracyCallstack.h"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracyCallstack.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracyDebug.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracyDxt1.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracyFastVector.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracyLock.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracyProfiler.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracyRingBuffer.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracyScoped.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracyStringHelpers.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracySysTime.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracySysTrace.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/client/TracyThread.hpp"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/include/common" TYPE FILE FILES
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/tracy_lz4.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/tracy_lz4hc.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/TracyAlign.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/TracyAlloc.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/TracyApi.h"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/TracyColor.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/TracyForceInline.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/TracyMutex.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/TracyProtocol.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/TracyQueue.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/TracySocket.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/TracyStackFrames.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/TracySystem.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/TracyUwp.hpp"
    "D:/Coding/Mumble-input-enhancement/3rdparty/tracy/public/common/TracyYield.hpp"
    )
endif()

if(CMAKE_INSTALL_COMPONENT STREQUAL "Unspecified" OR NOT CMAKE_INSTALL_COMPONENT)
  if(EXISTS "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/Tracy/TracyConfig.cmake")
    file(DIFFERENT _cmake_export_file_changed FILES
         "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/Tracy/TracyConfig.cmake"
         "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/tracy/CMakeFiles/Export/7430802ac276f58e70c46cf34d169c6f/TracyConfig.cmake")
    if(_cmake_export_file_changed)
      file(GLOB _cmake_old_config_files "$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/Tracy/TracyConfig-*.cmake")
      if(_cmake_old_config_files)
        string(REPLACE ";" ", " _cmake_old_config_files_text "${_cmake_old_config_files}")
        message(STATUS "Old export file \"$ENV{DESTDIR}${CMAKE_INSTALL_PREFIX}/share/Tracy/TracyConfig.cmake\" will be replaced.  Removing files [${_cmake_old_config_files_text}].")
        unset(_cmake_old_config_files_text)
        file(REMOVE ${_cmake_old_config_files})
      endif()
      unset(_cmake_old_config_files)
    endif()
    unset(_cmake_export_file_changed)
  endif()
  file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/Tracy" TYPE FILE FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/tracy/CMakeFiles/Export/7430802ac276f58e70c46cf34d169c6f/TracyConfig.cmake")
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Dd][Ee][Bb][Uu][Gg])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/Tracy" TYPE FILE FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/tracy/CMakeFiles/Export/7430802ac276f58e70c46cf34d169c6f/TracyConfig-debug.cmake")
  endif()
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Mm][Ii][Nn][Ss][Ii][Zz][Ee][Rr][Ee][Ll])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/Tracy" TYPE FILE FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/tracy/CMakeFiles/Export/7430802ac276f58e70c46cf34d169c6f/TracyConfig-minsizerel.cmake")
  endif()
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ww][Ii][Tt][Hh][Dd][Ee][Bb][Ii][Nn][Ff][Oo])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/Tracy" TYPE FILE FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/tracy/CMakeFiles/Export/7430802ac276f58e70c46cf34d169c6f/TracyConfig-relwithdebinfo.cmake")
  endif()
  if(CMAKE_INSTALL_CONFIG_NAME MATCHES "^([Rr][Ee][Ll][Ee][Aa][Ss][Ee])$")
    file(INSTALL DESTINATION "${CMAKE_INSTALL_PREFIX}/share/Tracy" TYPE FILE FILES "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/tracy/CMakeFiles/Export/7430802ac276f58e70c46cf34d169c6f/TracyConfig-release.cmake")
  endif()
endif()

string(REPLACE ";" "\n" CMAKE_INSTALL_MANIFEST_CONTENT
       "${CMAKE_INSTALL_MANIFEST_FILES}")
if(CMAKE_INSTALL_LOCAL_ONLY)
  file(WRITE "D:/Coding/Mumble-input-enhancement/build-e2e-server-vs/src/tracy/install_local_manifest.txt"
     "${CMAKE_INSTALL_MANIFEST_CONTENT}")
endif()
