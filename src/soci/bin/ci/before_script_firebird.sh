#!/bin/bash -e
# Configure Firebird database for SOCI build at travis-ci.org
#
# Mateusz Loskot <mateusz@loskot.net>, http://github.com/SOCI
#
source ${TRAVIS_BUILD_DIR}/bin/ci/common.sh

isql-fb -z -q -i /dev/null # --version
echo 'CREATE DATABASE "LOCALHOST:/tmp/soci_test.fdb" PAGE_SIZE = 16384;' > /tmp/create_soci_test.sql
isql-fb -u SYSDBA -p masterkey -i /tmp/create_soci_test.sql -q
cat /tmp/create_soci_test.sql
