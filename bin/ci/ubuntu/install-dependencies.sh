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
export PATH=$PWD/bin:$PATH

# What versions are we ACTUALLY running?
g++ -v
clang -v
# Avoid `spurious errors` caused by ~/.npm permission issues
# Does it already exist? Who owns? What permissions?
ls -lah ~/.npm || mkdir ~/.npm
# Make sure we own it
chown -Rc $USER ~/.npm
# We use this so we can filter the subtrees from our coverage report
pip install --user https://github.com/sublimator/codecov-python/zipball/source-match

bash bin/sh/install-boost.sh
