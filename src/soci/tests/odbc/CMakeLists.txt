###############################################################################
#
# This file is part of CMake configuration for SOCI library
#
# Copyright (C) 2010-2013 Mateusz Loskot <mateusz@loskot.net>
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
    DEPENDS ODBC
    SOURCE test-odbc-access.cpp ${SOCI_TESTS_COMMON}
    CONNSTR "test-access.dsn")
else()
    message(STATUS "MS Access test disabled on non-Windows platform")
endif()

soci_backend_test(
  NAME mssql
  BACKEND ODBC
  DEPENDS ODBC
  SOURCE test-odbc-mssql.cpp ${SOCI_TESTS_COMMON}
  CONNSTR "test-mssql.dsn")

soci_backend_test(
  NAME mysql
  BACKEND ODBC
  DEPENDS ODBC
  SOURCE test-odbc-mysql.cpp ${SOCI_TESTS_COMMON}
  CONNSTR "test-mysql.dsn")

if(WIN32)
  set(TEST_PGSQL_DSN "test-postgresql-win64.dsn")
else()
  set(TEST_PGSQL_DSN "test-postgresql.dsn")
endif()
soci_backend_test(
  NAME postgresql
  BACKEND ODBC
  DEPENDS ODBC
  SOURCE test-odbc-postgresql.cpp ${SOCI_TESTS_COMMON}
  CONNSTR ${TEST_PGSQL_DSN})

# TODO: DB2 backend is tested by Travis CI on dedicated VM, separate from ODBC,
# in order to test DB2 with ODBC, it would be best to install DB2 driver only.
if (NOT $ENV{TRAVIS})
soci_backend_test(
  NAME db2
  BACKEND ODBC
  SOURCE test-odbc-db2.cpp
  CONNSTR "test-db2.dsn")
endif()
