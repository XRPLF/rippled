#!/bin/bash -e
# Install Oracle client libraries for SOCI at travis-ci.org
#
# Copyright (c) 2013 Mateusz Loskot <mateusz@loskot.net>
#
source ${TRAVIS_BUILD_DIR}/bin/ci/common.sh

sudo apt-get install -qq tar bzip2 libaio1 

wget http://brzuchol.loskot.net/software/oracle/instantclient_11_2-linux-x64-mloskot.tar.bz2
tar -jxf instantclient_11_2-linux-x64-mloskot.tar.bz2
sudo mkdir -p /opt
sudo mv instantclient_11_2 /opt
sudo ln -s ${ORACLE_HOME}/libclntsh.so.11.1 ${ORACLE_HOME}/libclntsh.so
sudo ln -s ${ORACLE_HOME}/libocci.so.11.1 ${ORACLE_HOME}/libocci.so
