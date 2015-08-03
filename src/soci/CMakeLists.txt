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
cmake_minimum_required(VERSION 2.8.0 FATAL_ERROR)

project(SOCI)

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

# The version here should be in sync with the one in include/soci/version.h.
soci_version(MAJOR 4 MINOR 0 PATCH 0)

###############################################################################
# Build features and variants
##############################################################################

option(SOCI_SHARED "Enable build of shared libraries" ON)
boost_report_value(SOCI_SHARED)

option(SOCI_STATIC "Enable build of static libraries" ON)
boost_report_value(SOCI_STATIC)

option(SOCI_TESTS "Enable build of collection of SOCI tests" ON)
boost_report_value(SOCI_TESTS)

# from SociConfig.cmake
boost_report_value(SOCI_CXX_C11)

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

get_property(SOCI_INCLUDE_DIRS DIRECTORY ${CMAKE_SOURCE_DIR}
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
else() #MSVC
  # This flag enables multi process compiling for Visual Studio 2005 and above
  SET(CMAKE_CXX_FLAGS "${CMAKE_CXX_FLAGS} /MP")
endif()

if(Boost_FOUND)
  get_property(SOCI_COMPILE_DEFINITIONS
    DIRECTORY ${CMAKE_SOURCE_DIR}
    PROPERTY COMPILE_DEFINITIONS)

  list(APPEND SOCI_COMPILE_DEFINITIONS "HAVE_BOOST=1")

  if(Boost_DATE_TIME_FOUND)
    list(APPEND SOCI_CORE_DEPS_LIBS ${Boost_DATE_TIME_LIBRARY})
    list(APPEND SOCI_COMPILE_DEFINITIONS "HAVE_BOOST_DATE_TIME=1")
  endif()

  list(APPEND SOCI_INCLUDE_DIRS ${Boost_INCLUDE_DIRS})
  list(APPEND SOCI_CORE_INCLUDE_DIRS ${Boost_INCLUDE_DIRS})

  set_directory_properties(PROPERTY COMPILE_DEFINITIONS "${SOCI_COMPILE_DEFINITIONS}")

  set_property(DIRECTORY ${SOCI_SOURCE_DIR}
    PROPERTY COMPILE_DEFINITIONS "${SOCI_COMPILE_DEFINITIONS}")
endif()

list(APPEND SOCI_INCLUDE_DIRS ${CMAKE_CURRENT_BINARY_DIR})

set_property(DIRECTORY ${CMAKE_SOURCE_DIR}
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
# Enable tests
###############################################################################
enable_testing()
# Configure for testing with dashboard submissions to CDash
#include(CTest) # disabled as unused

# Define "make check" as alias for "make test"
add_custom_target(check COMMAND ctest)

###############################################################################
# Build configured components
###############################################################################
include(SociBackend)

include_directories(${SOCI_SOURCE_DIR}/include)

add_subdirectory(src)
add_subdirectory(tests)

message(STATUS "")

