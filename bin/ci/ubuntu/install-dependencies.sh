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
if [ -n "${CLANG_VER:-}" ]
then
  for c in clang clang++
  do
    test -x $( type -p ${c}-$CLANG_VER )
    ln -sv $(type -p ${c}-$CLANG_VER) $HOME/bin/${c}
  done
fi
export PATH=$HOME/bin:$PATH

# What versions are we ACTUALLY running?
if [ -x $HOME/bin/g++ ]; then
    $HOME/bin/g++ -v
fi
if [ -x "$(type -p clang)" ]; then
    clang -v
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
