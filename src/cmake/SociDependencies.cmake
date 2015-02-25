################################################################################
# SociDependencies.cmake - part of CMake configuration of SOCI library
#
# Based on BoostExternals.cmake from CMake configuration for Boost
################################################################################
# Copyright (C) 2010 Mateusz Loskot <mateusz@loskot.net>
# Copyright (C) 2009 Troy Straszheim
#
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
################################################################################
# Macros in this module:
#   
#   soci_backend - defines a database backend for SOCI library
#
################################################################################

#
# List of SOCI dependncies
#
set(SOCI_BACKENDS_DB_DEPENDENCIES
  MySQL
  ODBC
  Oracle
  PostgreSQL
  SQLite3
  Firebird
  DB2)

set(SOCI_BACKENDS_ALL_DEPENDENCIES
  Boost
  ${SOCI_BACKENDS_DB_DEPENDENCIES})

#
# Perform checks
# 
colormsg(_HIBLUE_ "Looking for SOCI dependencies:")

macro(boost_external_report NAME)

  set(VARNAME ${NAME})
  set(SUCCESS ${${VARNAME}_FOUND})

  set(VARNAMES ${ARGV})
  list(REMOVE_AT VARNAMES 0)

  # Test both, given original name and uppercase version too
  if(NOT SUCCESS) 
    string(TOUPPER ${NAME} VARNAME)
    set(SUCCESS ${${VARNAME}_FOUND})
    if(NOT SUCCESS)
     colormsg(_RED_ "WARNING:")
     colormsg(RED "${NAME} not found, some libraries or features will be disabled.")
     colormsg(RED "See the documentation for ${NAME} or manually set these variables:")
    endif()
  endif()

  foreach(variable ${VARNAMES})
    boost_report_value(${VARNAME}_${variable})
  endforeach()
endmacro()

#
#  Some externals default to OFF
#
option(WITH_VALGRIND "Run tests under valgrind" OFF)

#
# Detect available dependencies
#
foreach(external ${SOCI_BACKENDS_ALL_DEPENDENCIES})
  string(TOUPPER "${external}" EXTERNAL)
  option(WITH_${EXTERNAL} "Attempt to find and configure ${external}" ON)
  if(WITH_${EXTERNAL})
    colormsg(HICYAN "${external}:")
    include(${CMAKE_CURRENT_SOURCE_DIR}/cmake/dependencies/${external}.cmake)
  else()
    set(${EXTERNAL}_FOUND FALSE CACHE BOOL "${external} found" FORCE)
    colormsg(HIRED "${external}:" RED "disabled, since WITH_${EXTERNAL}=OFF")
  endif()
endforeach()
