#!/bin/bash -u
# Exit if anything fails. Echo commands to aid debugging.
set -ex

# Target working dir - defaults to current dir.
# Can be set from caller, or in the first parameter
TWD=$( cd ${TWD:-${1:-${PWD:-$( pwd )}}}; pwd )
echo "Target path is: $TWD"
# Override gcc version to $GCC_VER.
# Put an appropriate symlink at the front of the path.
mkdir -v $HOME/bin
for g in gcc g++ gcov gcc-ar gcc-nm gcc-ranlib
do
  test -x $( type -p ${g}-$GCC_VER )
  ln -sv $(type -p ${g}-$GCC_VER) $HOME/bin/${g}
done

if [[ -n ${CLANG_VER:-} ]]; then
    # There are cases where the directory exists, but the exe is not available.
    # Use this workaround for now.
    if [[ ! -x ${TWD}/llvm-${LLVM_VERSION}/bin/llvm-config && -d ${TWD}/llvm-${LLVM_VERSION} ]]; then
        rm -fr ${TWD}/llvm-${LLVM_VERSION}
    fi
    if [[ ! -d ${TWD}/llvm-${LLVM_VERSION} ]]; then
        mkdir ${TWD}/llvm-${LLVM_VERSION}
        LLVM_URL="http://llvm.org/releases/${LLVM_VERSION}/clang+llvm-${LLVM_VERSION}-x86_64-linux-gnu-ubuntu-14.04.tar.xz"
        wget -O - ${LLVM_URL} | tar -Jxvf - --strip 1 -C ${TWD}/llvm-${LLVM_VERSION}
    fi
    ${TWD}/llvm-${LLVM_VERSION}/bin/llvm-config --version;
    export LLVM_CONFIG="${TWD}/llvm-${LLVM_VERSION}/bin/llvm-config";
fi

if [[ ${BUILD:-} == cmake ]]; then
    # There are cases where the directory exists, but the exe is not available.
    # Use this workaround for now.
    if [[ ! -x ${TWD}/cmake/bin/cmake && -d ${TWD}/cmake ]]; then
        rm -fr ${TWD}/cmake
    fi
    if [[ ! -d ${TWD}/cmake ]]; then
      CMAKE_URL="https://www.cmake.org/files/v3.6/cmake-3.6.1-Linux-x86_64.tar.gz"
      wget --version
      # wget version 1.13.4 thinks this certificate is invalid, even though it's fine.
      # "ERROR: no certificate subject alternative name matches"
      # See also: https://github.com/travis-ci/travis-ci/issues/5059
      mkdir ${TWD}/cmake &&
        wget -O - --no-check-certificate ${CMAKE_URL} | tar --strip-components=1 -xz -C ${TWD}/cmake
      cmake --version
    fi
fi

# What versions are we ACTUALLY running?
if [ -x $HOME/bin/g++ ]; then
    $HOME/bin/g++ -v
fi

pip install --user requests==2.13.0
pip install --user https://github.com/codecov/codecov-python/archive/master.zip

bash bin/sh/install-boost.sh

# Install lcov
# Download the archive
wget https://github.com/linux-test-project/lcov/releases/download/v1.12/lcov-1.12.tar.gz
# Extract to ~/lcov-1.12
tar xfvz lcov-1.12.tar.gz -C $HOME
# Set install path
mkdir -p $LCOV_ROOT
cd $HOME/lcov-1.12 && make install PREFIX=$LCOV_ROOT
