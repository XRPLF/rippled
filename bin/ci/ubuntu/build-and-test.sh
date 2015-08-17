#!/bin/bash -u
# We use set -e and bash with -u to bail on first non zero exit code of any
# processes launched or upon any unbound variable
set -e
__dirname=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
echo "using CC: $CC"
echo "using TARGET: $TARGET"
export RIPPLED_PATH="$PWD/build/$CC.$TARGET/rippled"
echo "using RIPPLED_PATH: $RIPPLED_PATH"
# Make sure vcxproj is up to date
scons vcxproj
git diff --exit-code
# $CC will be either `clang` or `gcc` (If only we could do -j12 ;)
scons $CC.$TARGET -j${NUM_PROCESSORS:-1}
# We can be sure we're using the build/$CC.$TARGET variant
# (-f so never err)
rm -f build/rippled

# See what we've actually built
ldd $RIPPLED_PATH
if [[ $TARGET == "coverage" ]]; then
  $RIPPLED_PATH --unittest
  # We pass along -p to keep path segments so as to avoid collisions
  codecov --gcov-args=-p --gcov-source-match='^src/(ripple|beast)'
else
  # Run unittests (under gdb)
  cat $__dirname/unittests.gdb | gdb \
      --return-child-result \
      --args $RIPPLED_PATH --unittest
fi

# Run NPM tests
npm install
npm test --rippled=$RIPPLED_PATH
