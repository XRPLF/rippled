#!/usr/bin/env bash
# Part of Vagrant virtual development environments for SOCI

# Builds and tests SOCI from git master branch
source /vagrant/scripts/vagrant/common.env

# Build SOCI in /home/vagrant on Linux filesystem,
# outside /vagrant which is VM shared directory.
# Otherwise, CMake will fail:
# CMake Error: cmake_symlink_library: System Error: Protocol error
# Explanation from https://github.com/mitchellh/vagrant/issues/713
# The VirtualBox shared folder filesystem doesn't allow symlinks, unfortunately.
# Your only option is to deploy outside of the shared folders.
if [[ ! -d "${SOCI_BUILD}" ]] ; then
  mkdir -p ${SOCI_BUILD}
fi

echo "Build: building SOCI from sources in ${SOCI_HOME} to build in ${SOCI_BUILD}"
cd ${SOCI_BUILD} && \
cmake \
    -DSOCI_CXX_C11=ON \
    -DSOCI_TESTS=ON \
    -DSOCI_STATIC=OFF \
    -DSOCI_DB2=ON \
    -DSOCI_ODBC=OFF \
    -DSOCI_ORACLE=OFF \
    -DSOCI_EMPTY=ON \
    -DSOCI_FIREBIRD=ON \
    -DSOCI_MYSQL=ON \
    -DSOCI_POSTGRESQL=ON \
    -DSOCI_SQLITE3=ON \
    -DSOCI_DB2_TEST_CONNSTR:STRING="DATABASE=${SOCI_USER}\\;hostname=${SOCI_DB2_HOST}\\;UID=${SOCI_DB2_USER}\\;PWD=${SOCI_DB2_PASS}\\;ServiceName=50000\\;Protocol=TCPIP\\;" \
    -DSOCI_FIREBIRD_TEST_CONNSTR:STRING="service=LOCALHOST:/tmp/soci.fdb user=${SOCI_USER} password=${SOCI_PASS}" \
    -DSOCI_MYSQL_TEST_CONNSTR:STRING="host=localhost db=${SOCI_USER} user=${SOCI_USER} password=${SOCI_PASS}" \
    -DSOCI_POSTGRESQL_TEST_CONNSTR:STRING="host=localhost port=5432 dbname=${SOCI_USER} user=${SOCI_USER} password=${SOCI_PASS}" \
    ${SOCI_HOME} && \
make
echo "Build: building DONE"

# Do not run tests during provisioning, thay may fail terribly, so just build
# and let to run them manually after developer vagrant ssh'ed to the VM.
echo "Build: ready to test SOCI by running: cd ${SOCI_BUILD}; ctest -V --output-on-failure ."
