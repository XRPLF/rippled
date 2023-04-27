#!/usr/bin/env bash

set -o errexit
set -o nounset
set -o xtrace

# Parameters

gcc_version=${GCC_VERSION:-10}
cmake_version=${CMAKE_VERSION:-3.25.1}
conan_version=${CONAN_VERSION:-1.59}

apt update
# Iteratively build the list of packages to install so that we can interleave
# the lines with comments explaining their inclusion.
dependencies=''
# - to identify the Ubuntu version
dependencies+=' lsb-release'
# - for add-apt-repository
dependencies+=' software-properties-common'
# - to download CMake
dependencies+=' curl'
# - to build CMake
dependencies+=' libssl-dev'
# - Python headers for Boost.Python
dependencies+=' python3-dev'
# - to install Conan
dependencies+=' python3-pip'
# - to download rippled
dependencies+=' git'
# - CMake generators (but not CMake itself)
dependencies+=' make ninja-build'
apt install --yes ${dependencies}

add-apt-repository --yes ppa:ubuntu-toolchain-r/test
apt install --yes gcc-${gcc_version} g++-${gcc_version} \
  debhelper debmake debsums gnupg dh-buildinfo dh-make dh-systemd cmake \
  ninja-build zlib1g-dev make cmake ninja-build autoconf automake \
  pkg-config apt-transport-https

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

# Download and unpack CMake.
cmake_slug="cmake-${cmake_version}"
curl --location --remote-name \
  "https://github.com/Kitware/CMake/releases/download/v${cmake_version}/${cmake_slug}.tar.gz"
tar xzf ${cmake_slug}.tar.gz
rm ${cmake_slug}.tar.gz

# Build and install CMake.
cd ${cmake_slug}
./bootstrap --parallel=$(nproc)
make --jobs $(nproc)
make install
cd ..
rm --recursive --force ${cmake_slug}

# Install Conan.
pip3 install conan==${conan_version}

conan profile new --detect gcc
conan profile update settings.compiler=gcc gcc
conan profile update settings.compiler.version=${gcc_version} gcc
conan profile update settings.compiler.libcxx=libstdc++11 gcc
conan profile update env.CC=/usr/bin/gcc gcc
conan profile update env.CXX=/usr/bin/g++ gcc
