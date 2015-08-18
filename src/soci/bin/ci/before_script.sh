#!/bin/bash
# Run before_script actions for SOCI build at travis-ci.org
#
# Copyright (c) 2013 Mateusz Loskot <mateusz@loskot.net>
#
source ${TRAVIS_BUILD_DIR}/bin/ci/common.sh

before_script="${TRAVIS_BUILD_DIR}/bin/ci/before_script_${SOCI_TRAVIS_BACKEND}.sh"
if [ -x ${before_script} ]; then
	echo "Running ${before_script}"
    ${before_script}
fi
