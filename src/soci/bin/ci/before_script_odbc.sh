#!/bin/bash -e
# Sets up environment for SOCI backend ODBC at travis-ci.org
#
# Mateusz Loskot <mateusz@loskot.net>, http://github.com/SOCI
#
source ${TRAVIS_BUILD_DIR}/bin/ci/common.sh

mysql --version
mysql -e 'create database soci_test;'
psql --version  
psql -c 'create database soci_test;' -U postgres
