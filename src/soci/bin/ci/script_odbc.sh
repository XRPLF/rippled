#!/bin/bash -e
# Builds and tests SOCI backend ODBC at travis-ci.org
#
# Copyright (c) 2013 Mateusz Loskot <mateusz@loskot.net>
#
source ${TRAVIS_BUILD_DIR}/bin/ci/common.sh

cmake \
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
    -DSOCI_ODBC_TEST_POSTGRESQL_CONNSTR="FILEDSN=${PWD}/../backends/odbc/test/test-postgresql.dsn;" \
    -DSOCI_ODBC_TEST_MYSQL_CONNSTR="FILEDSN=${PWD}/../backends/odbc/test/test-mysql.dsn;" \
   ..

run_make
run_test
