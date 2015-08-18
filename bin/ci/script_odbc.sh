#!/bin/bash -e
# Builds and tests SOCI backend ODBC at travis-ci.org
#
# Copyright (c) 2013 Mateusz Loskot <mateusz@loskot.net>
#
source ${TRAVIS_BUILD_DIR}/bin/ci/common.sh

ODBC_TEST=${PWD}/../tests/odbc
if test ! -d ${ODBC_TEST}; then
    echo "ERROR: '${ODBC_TEST}' directory not found"
    exit 1
fi

cmake \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DSOCI_TESTS=ON \
    -DSOCI_STATIC=OFF \
    -DSOCI_DB2=OFF \
    -DSOCI_EMPTY=OFF \
    -DSOCI_FIREBIRD=OFF \
    -DSOCI_MYSQL=OFF \
    -DSOCI_ODBC=ON \
    -DSOCI_ORACLE=OFF \
    -DSOCI_POSTGRESQL=OFF \
    -DSOCI_SQLITE3=OFF \
    -DSOCI_ODBC_TEST_POSTGRESQL_CONNSTR="FILEDSN=${ODBC_TEST}/test-postgresql.dsn;" \
    -DSOCI_ODBC_TEST_MYSQL_CONNSTR="FILEDSN=${ODBC_TEST}/test-mysql.dsn;" \
   ..

run_make

# Exclude the test which can't be run as there is no MS SQL server available.
run_test -E soci_odbc_test_mssql
