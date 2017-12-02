#!/bin/bash -e
# Builds and tests SOCI backend Oracle at travis-ci.org
#
# Copyright (c) 2013 Mateusz Loskot <mateusz@loskot.net>
#
source ${TRAVIS_BUILD_DIR}/scripts/travis/common.sh
source ${TRAVIS_BUILD_DIR}/scripts/travis/oracle.sh

cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -DSOCI_ASAN=ON \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
    -DWITH_BOOST=OFF \
    -DSOCI_TESTS=ON \
    -DSOCI_STATIC=OFF \
    -DSOCI_DB2=OFF \
    -DSOCI_EMPTY=OFF \
    -DSOCI_FIREBIRD=OFF \
    -DSOCI_MYSQL=OFF \
    -DSOCI_ODBC=OFF \
    -DSOCI_ORACLE=ON \
    -DSOCI_POSTGRESQL=OFF \
    -DSOCI_SQLITE3=OFF \
    -DSOCI_ORACLE_TEST_CONNSTR:STRING="service=XE user=travis password=travis" \
    ..

run_make
run_test
