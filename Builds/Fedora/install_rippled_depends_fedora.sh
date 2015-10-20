#!/usr/bin/env bash

#
# This scripts installs the dependencies needed by rippled. It should be run
# with sudo.
#

if [ ! -f /etc/fedora-release ]; then
    echo "This script is meant to be run on fedora"
    exit 1
fi

fedora_release=$(grep -o '[0-9]*' /etc/fedora-release)

if (( $(bc <<< "${fedora_release} < 22") )); then
    echo "This script is meant to run on fedora 22 or greater"
    exit 1
fi

yum -y update
yum -y group install "Development Tools"
yum -y install gcc-c++ scons openssl-devel openssl-static protobuf-devel protobuf-static boost-devel boost-static libstdc++-static
