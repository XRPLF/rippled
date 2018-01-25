###############################################################################
#
# This file is part of CMake configuration for SOCI library
#
# Copyright (C) 2009-2013 Mateusz Loskot <mateusz@loskot.net>
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#
###############################################################################
# General settings
###############################################################################
cmake_minimum_required(VERSION 2.8.10 FATAL_ERROR)

project(SOCI)

###############################################################################
# Build features and variants
##############################################################################

option(SOCI_SHARED "Enable build of shared libraries" ON)
option(SOCI_STATIC "Enable build of static libraries" ON)
option(SOCI_TESTS "Enable build of collection of SOCI tests" ON)
option(SOCI_ASAN "Enable address sanitizer on GCC v4.8+/Clang v 3.1+" OFF)


###############################################################################
# SOCI CMake modules
###############################################################################

# Path to additional CMake modules
set(CMAKE_MODULE_PATH ${SOCI_SOURCE_DIR}/cmake ${CMAKE_MODULE_PATH})
set(CMAKE_MODULE_PATH ${SOCI_SOURCE_DIR}/cmake/modules ${CMAKE_MODULE_PATH})

include(SociUtilities)
include(SociConfig)

colormsg(_HIBLUE_ "Configuring SOCI:")

###############################################################################
# SOCI version information
###############################################################################
include(SociVersion)

soci_version()

###############################################################################
# Build features and variants
##############################################################################

boost_report_value(SOCI_SHARED)
boost_report_value(SOCI_STATIC)
boost_report_value(SOCI_TESTS)
boost_report_value(SOCI_ASAN)

# from SociConfig.cmake
boost_report_value(SOCI_CXX_C11)
boost_report_value(LIB_SUFFIX)

# Put the libaries and binaries that get built into directories at the
# top of the build tree rather than in hard-to-find leaf
# directories. This simplifies manual testing and the use of the build
# tree rather than installed Boost libraries.
set(CMAKE_LIBRARY_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_ARCHIVE_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/lib)
set(CMAKE_RUNTIME_OUTPUT_DIRECTORY ${CMAKE_BINARY_DIR}/bin)

###############################################################################
# Find SOCI dependencies
###############################################################################

set(SOCI_CORE_TARGET)
set(SOCI_CORE_TARGET_STATIC)
set(SOCI_CORE_DEPS_LIBS)

include(SociDependencies)

get_property(SOCI_INCLUDE_DIRS DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  PROPERTY INCLUDE_DIRECTORIES)

if(Threads_FOUND)
  list(APPEND SOCI_CORE_DEPS_LIBS ${CMAKE_THREAD_LIBS_INIT})
else()
  message(FATAL_ERROR "No thread library found")
endif()

if(NOT MSVC)
  set(DL_FIND_QUIETLY TRUE)
  find_package(DL)
  if(DL_FOUND)
    list(APPEND SOCI_CORE_DEPS_LIBS ${DL_LIBRARY})
    set_directory_properties(PROPERTIES INCLUDE_DIRECTORIES ${DL_INCLUDE_DIR})
    add_definitions(-DHAVE_DL=1)
  endif()
endif()

if(Boost_FOUND)
  get_property(SOCI_COMPILE_DEFINITIONS
    DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
    PROPERTY COMPILE_DEFINITIONS)

  set(SOCI_HAVE_BOOST ON)

  list(APPEND SOCI_COMPILE_DEFINITIONS "BOOST_ALL_NO_LIB")

  if(Boost_DATE_TIME_FOUND)
    list(APPEND SOCI_CORE_DEPS_LIBS ${Boost_DATE_TIME_LIBRARY})
    set(SOCI_HAVE_BOOST_DATE_TIME ON)
  endif()

  list(APPEND SOCI_INCLUDE_DIRS ${Boost_INCLUDE_DIRS})
  list(APPEND SOCI_CORE_INCLUDE_DIRS ${Boost_INCLUDE_DIRS})

  set_directory_properties(PROPERTY COMPILE_DEFINITIONS "${SOCI_COMPILE_DEFINITIONS}")

  set_property(DIRECTORY ${SOCI_SOURCE_DIR}
    PROPERTY COMPILE_DEFINITIONS "${SOCI_COMPILE_DEFINITIONS}")
else()
  set(SOCI_HAVE_BOOST OFF)
  set(SOCI_HAVE_BOOST_DATE_TIME OFF)
endif()

set(SOCI_HAVE_BOOST ${SOCI_HAVE_BOOST} CACHE INTERNAL "Boost library")
set(SOCI_HAVE_BOOST_DATE_TIME ${SOCI_HAVE_BOOST_DATE_TIME} CACHE INTERNAL "Boost date_time library")

list(APPEND SOCI_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR})

set_property(DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  PROPERTY
  INCLUDE_DIRECTORIES ${SOCI_INCLUDE_DIRS})

###############################################################################
# Installation
###############################################################################

if(NOT DEFINED SOCI_LIBDIR)
  if(APPLE OR CMAKE_SIZEOF_VOID_P EQUAL 4)
    set(SOCI_LIBDIR "lib")
  else()
    set(SOCI_LIBDIR "lib64")
  endif()
endif()

set(BINDIR "bin" CACHE PATH "The directory to install binaries into.")
set(LIBDIR ${SOCI_LIBDIR} CACHE PATH "The directory to install libraries into.")
set(DATADIR "share" CACHE PATH "The directory to install data files into.")
set(INCLUDEDIR "include" CACHE PATH "The directory to install includes into.")

###############################################################################
# Configuration files
###############################################################################
set(CONFIG_INCLUDE_DIR ${CMAKE_CURRENT_BINARY_DIR}/include)
install(DIRECTORY ${CONFIG_INCLUDE_DIR}/soci DESTINATION ${INCLUDEDIR})
set(CONFIG_FILE_IN "include/soci/soci-config.h.in")
set(CONFIG_FILE_OUT "${CONFIG_INCLUDE_DIR}/soci/soci-config.h")



###############################################################################
# Build configured components
###############################################################################
include(SociBackend)

include_directories(${SOCI_SOURCE_DIR}/include ${CONFIG_INCLUDE_DIR})
add_subdirectory(src)

if(SOCI_TESTS)
  ###############################################################################
  # Enable tests
  ###############################################################################
  enable_testing()

  file(TO_NATIVE_PATH ${PROJECT_SOURCE_DIR} TEST_ACCESS_PATH)
  configure_file(${PROJECT_SOURCE_DIR}/cmake/configs/test-access.cmake ${PROJECT_SOURCE_DIR}/tests/odbc/test-access.dsn @ONLY)

  set(MYSQL_DRIVER_NAME "MySQL")
  if(WIN32)
    set(MYSQL_DRIVER_NAME "MySQL ODBC 5.3 ANSI Driver")
  endif()
  configure_file(${PROJECT_SOURCE_DIR}/cmake/configs/test-mysql.cmake ${PROJECT_SOURCE_DIR}/tests/odbc/test-mysql.dsn @ONLY)

  # Define "make check" as alias for "make test"
  add_custom_target(check COMMAND ctest)
  add_subdirectory(tests)
endif()

###############################################################################
# build config file
###############################################################################

get_cmake_property(ALL_VARIABLES CACHE_VARIABLES)
set(CONFIGURED_VARIABLES)
foreach(v ${ALL_VARIABLES})
    if (v MATCHES "^SOCI_HAVE.*")
	get_property(CACHE_HELPSTRING CACHE ${v} PROPERTY HELPSTRING)
		set(CONFIGURED_VARIABLES "${CONFIGURED_VARIABLES}\n// ${CACHE_HELPSTRING}\n")
        if (${${v}})
            set(CONFIGURED_VARIABLES "${CONFIGURED_VARIABLES}#define ${v}\n")
        else()
            set(CONFIGURED_VARIABLES "${CONFIGURED_VARIABLES}/* #undef ${v} */\n")
        endif()
    endif()
endforeach()
configure_file("${CONFIG_FILE_IN}" "${CONFIG_FILE_OUT}")

message(STATUS "")

