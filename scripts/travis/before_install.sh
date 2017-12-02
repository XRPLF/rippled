#!/bin/bash
# Run before_install actions for SOCI build at travis-ci.org
#
# Copyright (c) 2013 Mateusz Loskot <mateusz@loskot.net>
#
source ${TRAVIS_BUILD_DIR}/scripts/travis/common.sh

sudo apt-key adv --recv-keys --keyserver keyserver.ubuntu.com 16126D3A3E5C1192
sudo add-apt-repository -y ppa:apt-fast/stable

sudo apt-get update -qq -y
sudo apt-get install -qq -y apt-fast
sudo apt-fast update -qq -y
sudo apt-fast install -qq -y libboost-dev libboost-date-time-dev valgrind

before_install="${TRAVIS_BUILD_DIR}/scripts/travis/before_install_${SOCI_TRAVIS_BACKEND}.sh"
if [ -x ${before_install} ]; then
	echo "Running ${before_install}"
    ${before_install}
fi
