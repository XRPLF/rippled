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
# $CC will be either `clang` or `gcc`
# http://docs.travis-ci.com/user/migrating-from-legacy/?utm_source=legacy-notice&utm_medium=banner&utm_campaign=legacy-upgrade
#   indicates that 2 cores are available to containers.
scons -j${NUM_PROCESSORS:-2} $CC.$TARGET
# We can be sure we're using the build/$CC.$TARGET variant
# (-f so never err)
rm -f build/rippled

# See what we've actually built
ldd $RIPPLED_PATH

if [[ $TARGET == "coverage" ]]; then
  export PATH=$PATH:$LCOV_ROOT/usr/bin

  # Create baseline coverage data file
  lcov --no-external -c -i -d . -o baseline.info
fi

# Execute unit tests under gdb, printing a call stack
# if we get a crash.
gdb -return-child-result -quiet -batch \
    -ex "set env MALLOC_CHECK_=3" \
    -ex "set print thread-events off" \
    -ex run \
    -ex "thread apply all backtrace full" \
    -ex "quit" \
    --args $RIPPLED_PATH --unittest

if [[ $TARGET == "coverage" ]]; then
  # Create test coverage data file
  lcov --no-external -c -d . -o tests.info

  # Combine baseline and test coverage data
  lcov -a baseline.info -a tests.info -o lcov-all.info

  # Only report on src/ripple files
  lcov -e "lcov-all.info" "*/src/ripple/*" -o lcov.pre.info

  # Exclude */src/test directory
  lcov --remove lcov.pre.info "*/src/ripple/test/*" -o lcov.info

  # Push the results (lcov.info) to codecov
  codecov -X gcov # don't even try and look for .gcov files ;)
fi

# Run NPM tests
npm install --progress=false
npm test --rippled=$RIPPLED_PATH
