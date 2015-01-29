#!/bin/bash -e
# Builds and tests SOCI backend SQLite3 at travis-ci.org
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
    -DSOCI_ODBC=OFF \
    -DSOCI_ORACLE=OFF \
    -DSOCI_POSTGRESQL=ON \
    -DSOCI_SQLITE3=OFF \
    -DSOCI_POSTGRESQL_TEST_CONNSTR:STRING="dbname=soci_test user=postgres" \
    ..  

run_make
run_test
