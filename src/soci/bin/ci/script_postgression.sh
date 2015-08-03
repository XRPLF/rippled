#!/bin/bash -e
# Builds and tests SOCI backend PostgreSQL at travis-ci.org
#
# Copyright (C) 2014 Vadim Zeitlin
# Copyright (C) 2015 Mateusz Loskot <mateusz@loskot.net>
#
source ${TRAVIS_BUILD_DIR}/bin/ci/common.sh

# Get Postgression's PostgreSQL connection parameters as URI
SOCI_POSTGRESQL_CONNSTR=$(curl http://api.postgression.com)
# or old-style conninfo string, both should work.
#SOCI_POSTGRESQL_CONNSTR=$(curl http://api.postgression.com | \
#sed 's|postgres://\([^:]\+\):\([^@]\+\)@\([^:]\+\):\([0-9]\+\)/\(.*\)|user=\1 password=\2 host=\3 port=\4 dbname=\5|')

# Before proceeding with build, check Postgression availability
echo $SOCI_POSTGRESQL_CONNSTR | grep NO_DATABASES_AVAILABLE
if [ $? -eq 0 ];then
  echo ${SOCI_POSTGRESQL_CONNSTR}
  exit 1
fi

echo "Postgression connection parameters: $SOCI_POSTGRESQL_CONNSTR"

echo "PostgreSQL client version:"
psql --version
# WARNING: Somehow, connecting to Postgression service with psql
# seems to terminate Travis CI session preventing the job to
# continue with build and tests.
#psql -c 'select version();' "$SOCI_POSTGRESQL_CONNSTR"

cmake \
    -DCMAKE_VERBOSE_MAKEFILE=ON \
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
    -DSOCI_POSTGRESQL_TEST_CONNSTR:STRING="$SOCI_POSTGRESQL_CONNSTR" \
    ..

run_make
run_test
