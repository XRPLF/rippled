################################################################################
# SociVersion.cmake - part of CMake configuration of SOCI library
################################################################################
# Copyright (C) 2010 Mateusz Loskot <mateusz@loskot.net>
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
################################################################################
# Macros in this module:
#
#   soci_version - defines version information for SOCI library
#
################################################################################

# Defines version information for SOCI library
#
# soci_version(MAJOR major_version MINOR minor_version PATCH patch_level)
#
#    MAJOR.MINOR version is used to set SOVERSION
#
macro(soci_version)
  # get version from soci/version.h
  file(
    STRINGS
    "${PROJECT_SOURCE_DIR}/include/soci/version.h"
    _VERSION
    REGEX
    "#define SOCI_VERSION ([0-9]+)"
  )
  string(REGEX MATCH "([0-9]+)" _VERSION "${_VERSION}")

  math(EXPR ${PROJECT_NAME}_VERSION_MAJOR "${_VERSION} / 100000")
  math(EXPR ${PROJECT_NAME}_VERSION_MINOR "${_VERSION} / 100 % 1000")
  math(EXPR ${PROJECT_NAME}_VERSION_PATCH "${_VERSION} % 100")

  # Set VERSION string
  set(${PROJECT_NAME}_VERSION
    "${${PROJECT_NAME}_VERSION_MAJOR}.${${PROJECT_NAME}_VERSION_MINOR}.${${PROJECT_NAME}_VERSION_PATCH}")

  # Set SOVERSION based on major and minor
  set(${PROJECT_NAME}_SOVERSION
    "${${PROJECT_NAME}_VERSION_MAJOR}.${${PROJECT_NAME}_VERSION_MINOR}")

  # Set ABI version string used to name binary output and, by SOCI loader, to find binaries.
  # On Windows, ABI version is specified using binary file name suffix.
  # On Unix, suffix ix empty and SOVERSION is used instead.
  if (UNIX)
    set(${PROJECT_NAME}_ABI_VERSION ${${PROJECT_NAME}_SOVERSION})
  elseif(WIN32)
    set(${PROJECT_NAME}_ABI_VERSION
      "${${PROJECT_NAME}_VERSION_MAJOR}_${${PROJECT_NAME}_VERSION_MINOR}")
  else()
    message(FATAL_ERROR "Ambiguous target platform with unknown ABI version scheme. Giving up.")
  endif()

  boost_report_value(${PROJECT_NAME}_VERSION)
  boost_report_value(${PROJECT_NAME}_ABI_VERSION)

  add_definitions(-DSOCI_ABI_VERSION="${${PROJECT_NAME}_ABI_VERSION}")

endmacro()
