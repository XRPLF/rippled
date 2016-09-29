#!/usr/bin/env bash

set -euxo pipefail
# The above bash options do the following:

# -e When this option is on, if a simple command fails for any of the reasons
#    listed in Consequences of Shell Errors or returns an exit status value >0,
#    and is not part of the compound list following a while, until, or if
#    keyword, and is not a part of an AND or OR list, and is not a pipeline
#    preceded by the ! reserved word, then the shell shall immediately exit.
# -u The shell shall write a message to standard error when it tries to expand a
#    variable that is not set and immediately exit. An interactive shell shall
#    not exit.
# -x The shell shall write to standard error a trace for each command after it
#    expands the command and before it executes it. It is unspecified
#    whether the command that turns tracing off is traced.
# -o pipefail
#    Pipelines fail on the first command which fails instead of dying later on
#    down the pipeline.

HERE=${PWD}

# Override gcc version to $GCC_VER.
# Put an appropriate symlink at the front of the path.
mkdir -v ${HOME}/bin
for g in gcc g++ gcov gcc-ar gcc-nm gcc-ranlib
do
  test -x $( type -p ${g}-${GCC_VER} )
  ln -sv $(type -p ${g}-${GCC_VER}) $HOME/bin/${g}
done

if [[ -n ${CLANG_VER:-} ]]; then
    # There are cases where the directory exists, but the exe is not available.
    # Use this workaround for now.
    if [[ ! -x llvm-${LLVM_VERSION}/bin/llvm-config ]] && [[ -d llvm-${LLVM_VERSION} ]]; then
        rm -fr llvm-${LLVM_VERSION}
    fi
    if [[ ! -d llvm-${LLVM_VERSION} ]]; then
        mkdir llvm-${LLVM_VERSION}
        LLVM_URL="http://llvm.org/releases/${LLVM_VERSION}/clang+llvm-${LLVM_VERSION}-x86_64-linux-gnu-ubuntu-14.04.tar.xz"
        wget -O - ${LLVM_URL} | tar -Jxvf - --strip 1 -C llvm-${LLVM_VERSION}
    fi
    llvm-${LLVM_VERSION}/bin/llvm-config --version;
    export LLVM_CONFIG="llvm-${LLVM_VERSION}/bin/llvm-config";
fi

# There are cases where the directory exists, but the exe is not available.
# Use this workaround for now.
if [[ ! -x cmake/bin/cmake && -d cmake ]]; then
    rm -fr cmake
fi
if [[ ! -d cmake && ${BUILD_SYSTEM:-} == cmake ]]; then
  CMAKE_URL="http://www.cmake.org/files/v3.5/cmake-3.5.2-Linux-x86_64.tar.gz"
  mkdir cmake && wget --no-check-certificate -O - ${CMAKE_URL} | tar --strip-components=1 -xz -C cmake
fi

# NOTE, changed from PWD -> HOME
export PATH=${HOME}/bin:${PATH}

# What versions are we ACTUALLY running?
if [ -x $HOME/bin/g++ ]; then
    ${HOME}/bin/g++ -v
fi
if [ -x ${HOME}/bin/clang ]; then
    ${HOME}/bin/clang -v
fi
# Avoid `spurious errors` caused by ~/.npm permission issues
# Does it already exist? Who owns? What permissions?
ls -lah ~/.npm || mkdir ~/.npm
# Make sure we own it
chown -Rc ${USER} ~/.npm
# We use this so we can filter the subtrees from our coverage report
pip install --user https://github.com/codecov/codecov-python/archive/master.zip

bash scripts/install-boost.sh
bash scripts/install-valgrind.sh

# Install lcov
# Download the archive
wget http://downloads.sourceforge.net/ltp/lcov-1.12.tar.gz
# Extract to ~/lcov-1.12
tar xfvz lcov-1.12.tar.gz -C ${HOME}
# Set install path
mkdir -p ${LCOV_ROOT}
cd ${HOME}/lcov-1.12 && make install PREFIX=${LCOV_ROOT}

# Install coveralls reporter
cd ${HERE}
mkdir -p node_modules
npm install coveralls
