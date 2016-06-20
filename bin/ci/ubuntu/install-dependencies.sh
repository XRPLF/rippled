#!/bin/bash -u
# Exit if anything fails.
set -e
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

# What versions are we ACTUALLY running?
if [ -x $HOME/bin/g++ ]; then
    $HOME/bin/g++ -v
fi

# Avoid `spurious errors` caused by ~/.npm permission issues
# Does it already exist? Who owns? What permissions?
ls -lah ~/.npm || mkdir ~/.npm
# Make sure we own it
chown -Rc $USER ~/.npm
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
