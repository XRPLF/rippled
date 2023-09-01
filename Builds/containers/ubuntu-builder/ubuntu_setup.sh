#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o xtrace

# Parameters

gcc_version=${GCC_VERSION:-11}

export DEBIAN_FRONTEND=noninteractive

apt update
# Iteratively build the list of packages to install so that we can interleave
# the lines with comments explaining their inclusion.
dependencies=''
# - for add-apt-repository
dependencies+=' software-properties-common'
# - to download CMake
dependencies+=' curl'
# - to build CMake
dependencies+=' libssl-dev'
# - for Python
dependencies+=' libbz2-dev liblzma-dev libsqlite3-dev'
# - to download rippled
dependencies+=' git'
# - CMake generators (but not CMake itself)
dependencies+=' make ninja-build'
apt-get install --yes ${dependencies}

add-apt-repository --yes ppa:ubuntu-toolchain-r/test
apt-get install --yes gcc-${gcc_version} g++-${gcc_version}
apt-get install --yes build-essential libssl-dev zlib1g-dev \
libbz2-dev libreadline-dev libsqlite3-dev curl \
libncursesw5-dev xz-utils tk-dev libxml2-dev libxmlsec1-dev libffi-dev liblzma-dev

# Give us nice unversioned aliases for gcc and company.
update-alternatives --install \
  /usr/bin/gcc gcc /usr/bin/gcc-${gcc_version} 100 \
  --slave /usr/bin/g++ g++ /usr/bin/g++-${gcc_version} \
  --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-${gcc_version} \
  --slave /usr/bin/gcc-nm gcc-nm /usr/bin/gcc-nm-${gcc_version} \
  --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-${gcc_version} \
  --slave /usr/bin/gcov gcov /usr/bin/gcov-${gcc_version} \
  --slave /usr/bin/gcov-tool gcov-tool /usr/bin/gcov-dump-${gcc_version} \
  --slave /usr/bin/gcov-dump gcov-dump /usr/bin/gcov-tool-${gcc_version}
update-alternatives --auto gcc
