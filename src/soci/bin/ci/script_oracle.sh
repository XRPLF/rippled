#!/bin/bash -e
# Builds and tests SOCI backend Oracle at travis-ci.org
#
# Copyright (c) 2013 Mateusz Loskot <mateusz@loskot.net>
#
source ${TRAVIS_BUILD_DIR}/bin/ci/common.sh

if [ "${CXX}" == "g++" ]
then
    ORACLE_USER="soci_tester"
else
    ORACLE_USER="soci_tester1"
fi

cmake \
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
    -DSOCI_ORACLE_TEST_CONNSTR:STRING="service=brzuchol.loskot.net user=${ORACLE_USER} password=soci_secret" \
    ..  

run_make
run_test
