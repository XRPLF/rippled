###############################################################################
#
# This file is part of CMake configuration for SOCI library
#
# Copyright (C) 2009-2010 Mateusz Loskot <mateusz@loskot.net>
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#
###############################################################################

colormsg(_HIBLUE_ "Configuring SOCI core library:")

# Core dependencies
set(SOCI_CORE_DEPENDENCIES)

include(FindThreads)

if(Threads_FOUND OR CMAKE_USE_WIN32_THREADS_INIT OR CMAKE_THREAD_LIBS_INIT)
  list(APPEND SOCI_CORE_DEPENDENCIES ${CMAKE_THREAD_LIBS_INIT})
else()
  message(FATAL_ERROR "No thread library found")
endif()

if(NOT MSVC)
  set(DL_FIND_QUIETLY TRUE)
  find_package(DL)
  if(DL_FOUND)
    list(APPEND SOCI_CORE_DEPENDENCIES ${DL_LIBRARY})
    set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES ${DL_INCLUDE_DIR})
    add_definitions(-DHAVE_DL=1)
  endif()
endif()

if(Boost_FOUND)

  get_property(SOCI_COMPILE_DEFINITIONS
    DIRECTORY ${CMAKE_SOURCE_DIR}
    PROPERTY COMPILE_DEFINITIONS)

  list(APPEND SOCI_COMPILE_DEFINITIONS "HAVE_BOOST=1")

  if(Boost_DATE_TIME_FOUND)
    list(APPEND SOCI_CORE_DEPENDENCIES ${Boost_DATE_TIME_LIBRARY})
    list(APPEND SOCI_COMPILE_DEFINITIONS "HAVE_BOOST_DATE_TIME=1")
  endif()

  set_property(DIRECTORY ${CMAKE_SOURCE_DIR}
    PROPERTY COMPILE_DEFINITIONS ${SOCI_COMPILE_DEFINITIONS})

  set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIRS})

  set_property(DIRECTORY ${CMAKE_SOURCE_DIR}
    PROPERTY INCLUDE_DIRECTORIES ${Boost_INCLUDE_DIRS})

endif()

# Core source files
file(GLOB SOCI_CORE_HEADERS *.h)
file(GLOB SOCI_CORE_SOURCES *.cpp)

# Group source files for IDE source explorers (e.g. Visual Studio)
source_group("Header Files" FILES ${SOCI_CORE_HEADERS})
source_group("Source Files" FILES ${SOCI_CORE_SOURCES})
source_group("CMake Files" FILES CMakeLists.txt)

# Core targets configuration
string(TOLOWER "${PROJECT_NAME}" PROJECTNAMEL)
set(SOCI_CORE_TARGET ${PROJECTNAMEL}_core)

soci_target_output_name(${SOCI_CORE_TARGET} SOCI_CORE_TARGET_OUTPUT_NAME)

# Configure SOCI backend loader to also use default install directory.
set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR})
set_property(DIRECTORY ${CMAKE_SOURCE_DIR} PROPERTY INCLUDE_DIRECTORIES ${CMAKE_CURRENT_BINARY_DIR})
configure_file(soci_backends_config.h.in ${CMAKE_CURRENT_BINARY_DIR}/soci_backends_config.h)

#
# Core shared library
#
if (SOCI_SHARED)
  add_library(${SOCI_CORE_TARGET} SHARED ${SOCI_CORE_HEADERS} ${SOCI_CORE_SOURCES})

  target_link_libraries(${SOCI_CORE_TARGET} ${SOCI_CORE_DEPENDENCIES})

  if(WIN32)
    set_target_properties(${SOCI_CORE_TARGET}
      PROPERTIES
      DEFINE_SYMBOL SOCI_DLL
      OUTPUT_NAME "${SOCI_CORE_TARGET_OUTPUT_NAME}"
      VERSION ${SOCI_VERSION}
      CLEAN_DIRECT_OUTPUT 1)
  else()
    set_target_properties(${SOCI_CORE_TARGET}
      PROPERTIES
      VERSION ${SOCI_VERSION}
      SOVERSION ${SOCI_SOVERSION}
      INSTALL_NAME_DIR ${CMAKE_INSTALL_PREFIX}/lib
      CLEAN_DIRECT_OUTPUT 1)
  endif()
endif()

add_definitions(-DSOCI_LIB_PREFIX="${CMAKE_SHARED_LIBRARY_PREFIX}soci_"
                -DSOCI_LIB_SUFFIX="${CMAKE_SHARED_LIBRARY_SUFFIX}")

#
# Core static library
#
if (SOCI_STATIC)
  set(SOCI_CORE_TARGET_STATIC ${SOCI_CORE_TARGET}_static)

  add_library(${SOCI_CORE_TARGET_STATIC} STATIC ${SOCI_CORE_HEADERS} ${SOCI_CORE_SOURCES})

  set_target_properties(${SOCI_CORE_TARGET_STATIC}
    PROPERTIES
    OUTPUT_NAME ${SOCI_CORE_TARGET_OUTPUT_NAME}
    PREFIX "lib"
    CLEAN_DIRECT_OUTPUT 1)
endif()

#
# Core installation
#
install(FILES ${SOCI_CORE_HEADERS} DESTINATION ${INCLUDEDIR}/${PROJECTNAMEL})

if (SOCI_SHARED)
  install(TARGETS ${SOCI_CORE_TARGET}
    RUNTIME DESTINATION ${BINDIR}
    LIBRARY DESTINATION ${LIBDIR}
    ARCHIVE DESTINATION ${LIBDIR})
endif()

if (SOCI_STATIC)
  install(TARGETS ${SOCI_CORE_TARGET_STATIC}
    RUNTIME DESTINATION ${BINDIR}
    LIBRARY DESTINATION ${LIBDIR}
    ARCHIVE DESTINATION ${LIBDIR})
endif()

#
# Core configuration summary
#
boost_report_value(SOCI_CORE_TARGET)
boost_report_value(SOCI_CORE_TARGET_OUTPUT_NAME)
boost_report_value(SOCI_CORE_DEPENDENCIES)
boost_report_value(WITH_BOOST)
soci_report_directory_property(COMPILE_DEFINITIONS)

# Export core target name to make it visible by backends
set(SOCI_CORE_TARGET ${SOCI_CORE_TARGET} PARENT_SCOPE)
if (SOCI_STATIC)
  set(SOCI_CORE_TARGET_STATIC ${SOCI_CORE_TARGET_STATIC} PARENT_SCOPE)
endif()
set(SOCI_CORE_STATIC_DEPENDENCIES ${SOCI_CORE_DEPENDENCIES} PARENT_SCOPE)
