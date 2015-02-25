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

if (WIN32)
  # MDBTools driver seems unreliable
  soci_backend_test(
    NAME access
    BACKEND ODBC
    SOURCE test-odbc-access.cpp
    CONNSTR "test-access.dsn")

  # We have no means to test SQL Server at travis-ci.org
  soci_backend_test(
    NAME mssql
    BACKEND ODBC
    SOURCE test-odbc-mssql.cpp
    CONNSTR "test-mssql.dsn")
endif()

soci_backend_test(
  NAME mysql
  BACKEND ODBC
  SOURCE test-odbc-mysql.cpp
  CONNSTR "test-mysql.dsn")

soci_backend_test(
  NAME postgresql
  BACKEND ODBC
  SOURCE test-odbc-postgresql.cpp
  CONNSTR "test-postgresql.dsn")

# TODO: DB2 backend is tested by Travis CI on dedicated VM, separate from ODBC,
# in order to test DB2 with ODBC, it would be best to install DB2 driver only.
if (NOT $ENV{TRAVIS})
soci_backend_test(
  NAME db2
  BACKEND ODBC
  SOURCE test-odbc-db2.cpp
  CONNSTR "test-db2.dsn")
endif()
