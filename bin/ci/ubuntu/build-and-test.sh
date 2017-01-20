#!/bin/bash -u
# We use set -e and bash with -u to bail on first non zero exit code of any
# processes launched or upon any unbound variable.
# We use set -x to print commands before running them to help with
# debugging.
set -ex
__dirname=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
echo "using CC: $CC"
echo "using TARGET: $TARGET"

# Ensure APP defaults to rippled if it's not set.
: ${APP:=rippled}
if [[ ${BUILD:-scons} == "cmake" ]]; then
  echo "cmake building ${APP}"
  CMAKE_TARGET=$CC.$TARGET
  if [[ ${CI:-} == true ]]; then
    CMAKE_TARGET=$CMAKE_TARGET.ci
  fi
  mkdir -p "build/${CMAKE_TARGET}"
  pushd "build/${CMAKE_TARGET}"
  cmake ../.. -Dtarget=$CMAKE_TARGET
  cmake --build . -- -j${NUM_PROCESSORS:-2}
  popd
  export APP_PATH="$PWD/build/${CMAKE_TARGET}/${APP}"
  echo "using APP_PATH: $APP_PATH"

else
  export APP_PATH="$PWD/build/$CC.$TARGET/${APP}"
  echo "using APP_PATH: $APP_PATH"
  # Make sure vcxproj is up to date
  scons vcxproj
  git diff --exit-code
  # $CC will be either `clang` or `gcc`
  # http://docs.travis-ci.com/user/migrating-from-legacy/?utm_source=legacy-notice&utm_medium=banner&utm_campaign=legacy-upgrade
  #   indicates that 2 cores are available to containers.
  scons -j${NUM_PROCESSORS:-2} $CC.$TARGET
fi
# We can be sure we're using the build/$CC.$TARGET variant
# (-f so never err)
rm -f build/${APP}

# See what we've actually built
ldd $APP_PATH

if [[ ${APP} == "rippled" ]]; then
  export APP_ARGS="--unittest"
  # Only report on src/ripple files
  export LCOV_FILES="*/src/ripple/*"
  # Nothing to explicitly exclude
  export LCOV_EXCLUDE_FILES="LCOV_NO_EXCLUDE"
else
  : ${APP_ARGS:=}
  : ${LCOV_FILES:="*/src/*"}
  # Don't exclude anything
  : ${LCOV_EXCLUDE_FILES:="LCOV_NO_EXCLUDE"}
fi

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
    --args $APP_PATH --unittest

if [[ $TARGET == "coverage" ]]; then
  # Create test coverage data file
  lcov --no-external -c -d . -o tests.info

  # Combine baseline and test coverage data
  lcov -a baseline.info -a tests.info -o lcov-all.info

  # Included files
  lcov -e "lcov-all.info" "${LCOV_FILES}" -o lcov.pre.info

  # Excluded files
  lcov --remove lcov.pre.info "${LCOV_EXCLUDE_FILES}" -o lcov.info

  # Push the results (lcov.info) to codecov
  codecov -X gcov # don't even try and look for .gcov files ;)
fi

if [[ ${APP} == "rippled" ]]; then
  # Run NPM tests
  npm install --progress=false
  npm test --rippled=$APP_PATH
fi
