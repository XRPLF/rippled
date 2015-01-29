#!/bin/bash -e
# Run before_install actions for SOCI build at travis-ci.org
#
# Copyright (c) 2013 Mateusz Loskot <mateusz@loskot.net>
#
source ${TRAVIS_BUILD_DIR}/bin/ci/common.sh

sudo apt-key adv --recv-keys --keyserver keyserver.ubuntu.com 16126D3A3E5C1192
sudo apt-get update -qq
sudo apt-get install -qq libboost-dev libboost-date-time-dev

before_install="${TRAVIS_BUILD_DIR}/bin/ci/before_install_${SOCI_TRAVIS_BACKEND}.sh"
[ -x ${before_install} ] && ${before_install} || echo "nothing to run"
