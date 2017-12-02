#!/bin/bash
# Run test script actions for SOCI build at travis-ci.org
#
# Copyright (c) 2013 Mateusz Loskot <mateusz@loskot.net>
#
source ${TRAVIS_BUILD_DIR}/scripts/travis/common.sh

# prepare build directory
builddir="${TRAVIS_BUILD_DIR}/_build"
mkdir -p ${builddir}
cd ${builddir}

# build and run tests
SCRIPT=${TRAVIS_BUILD_DIR}/scripts/travis/script_${SOCI_TRAVIS_BACKEND}.sh
echo "Running ${SCRIPT}"
${SCRIPT}
