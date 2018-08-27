#!/bin/bash -u
# Exit if anything fails. Echo commands to aid debugging.
set -ex

# Target working dir - defaults to current dir.
# Can be set from caller, or in the first parameter
TWD=$( cd ${TWD:-${1:-${PWD:-$( pwd )}}}; pwd )
echo "Target path is: $TWD"
# Override gcc version to $GCC_VER.
# Put an appropriate symlink at the front of the path.
mkdir -pv $HOME/bin
for g in gcc g++ gcov gcc-ar gcc-nm gcc-ranlib
do
  test -x $( type -p ${g}-$GCC_VER )
  ln -sv $(type -p ${g}-$GCC_VER) $HOME/bin/${g}
done

# What versions are we ACTUALLY running?
if [ -x $HOME/bin/g++ ]; then
    $HOME/bin/g++ -v
else
    g++ -v
fi

pip install --user requests==2.13.0
pip install --user https://github.com/codecov/codecov-python/archive/master.zip

bash bin/sh/install-boost.sh

