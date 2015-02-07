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

colormsg(_HIBLUE_ "Configuring SOCI:")

###############################################################################
# SOCI version information
###############################################################################
include(SociVersion)

soci_version(MAJOR 3 MINOR 2 PATCH 2)

###############################################################################
# Build features and variants
###############################################################################
include(SociSystemInfo)
include(SociConfig)

boost_report_value(SOCI_PLATFORM_NAME)
boost_report_value(SOCI_COMPILER_NAME)

option(SOCI_STATIC "Enable build of static libraries" ON)
boost_report_value(SOCI_STATIC)

option(SOCI_TESTS "Enable build of collection of SOCI tests" ON)
boost_report_value(SOCI_TESTS)

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

set(SOCI_CORE_DEPENDENCIES)

include(SociDependencies)

#message("${SOCI_CORE_DEPENDENCIES}")

###############################################################################
# Installation
###############################################################################

if(APPLE OR CMAKE_SIZEOF_VOID_P EQUAL 4)
  set(SOCI_LIBDIR "lib")
else()
  set(SOCI_LIBDIR "lib64")
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

include_directories(${SOCI_SOURCE_DIR}/core)

add_subdirectory(core)
add_subdirectory(backends)
