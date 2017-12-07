###############################################################################
#
# This file is part of CMake configuration for SOCI library
#
# Copyright (C) 2010 Mateusz Loskot <mateusz@loskot.net>
# Distributed under the Boost Software License, Version 1.0.
# (See accompanying file LICENSE_1_0.txt or copy at
# http://www.boost.org/LICENSE_1_0.txt)
#
###############################################################################

include(CMakeDependentOption)

option(SOCI_POSTGRESQL_NOSINLGEROWMODE
  "Do not use single row mode. PostgreSQL <9 portability."
  OFF)

option(SOCI_POSTGRESQL_NOPARAMS
  "Do not use input parameters. PostgreSQL 7.x portability."
  OFF)

option(SOCI_POSTGRESQL_NOBINDBYNAME
  "Disable query rewriting to native form. PostgreSQL 7.0 portability."
  OFF)

cmake_dependent_option(SOCI_POSTGRESQL_NOPREPARE
  "Disable prepared statements. Set ON if SOCI_POSTGRESQL_NOBINDBYNAME is ON. PostgreSQL 7.0 portability." ON
  SOCI_POSTGRESQL_NOBINDBYNAME OFF)

if(SOCI_POSTGRESQL_NOPARAMS)
  add_definitions(-DSOCI_POSTGRESQL_NOPARAMS=1)
endif()

if(SOCI_POSTGRESQL_NOBINDBYNAME)
  add_definitions(-DSOCI_POSTGRESQL_NOBINDBYNAME=1)
endif()

if(SOCI_POSTGRESQL_NOPREPARE)
  add_definitions(-DSOCI_POSTGRESQL_NOPREPARE=1)
endif()

if (POSTGRESQL_VERSION VERSION_LESS "9.0.0")
  set(SOCI_POSTGRESQL_NOSINLGEROWMODE ON CACHE BOOL "Use single row mode for PostgreSQL 9+" FORCE)
endif()

if(SOCI_POSTGRESQL_NOSINLGEROWMODE)
  add_definitions(-DSOCI_POSTGRESQL_NOSINLGEROWMODE=1)
endif()

soci_backend(PostgreSQL
  DEPENDS PostgreSQL
  DESCRIPTION "SOCI backend for PostgreSQL"
  AUTHORS "Maciej Sobczak, Stephen Hutton"
  MAINTAINERS "Mateusz Loskot")

boost_report_value(SOCI_POSTGRESQL_NOPARAMS)
boost_report_value(SOCI_POSTGRESQL_NOBINDBYNAME)
boost_report_value(SOCI_POSTGRESQL_NOPREPARE)
boost_report_value(SOCI_POSTGRESQL_NOSINLGEROWMODE)
