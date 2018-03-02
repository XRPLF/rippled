#!/bin/bash -u
# We use set -e and bash with -u to bail on first non zero exit code of any
# processes launched or upon any unbound variable.
# We use set -x to print commands before running them to help with
# debugging.
set -ex
__dirname=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
echo "using CC: $CC"
"${CC}" --version
COMPNAME=$(basename $CC)
echo "using CXX: ${CXX:-notset}"
if [[ $CXX ]]; then
  "${CXX}" --version
fi
echo "using TARGET: $TARGET"

# Ensure APP defaults to rippled if it's not set.
: ${APP:=rippled}
echo "using APP: $APP"

JOBS=${NUM_PROCESSORS:-2}
if [[ ${TRAVIS:-false} != "true" ]]; then
  JOBS=$((JOBS+1))
fi

if [ -x /usr/bin/time ] ; then
  : ${TIME:="Duration: %E"}
  export TIME
  time=/usr/bin/time
else
  time=
fi

echo "cmake building ${APP}"
: ${CMAKE_EXTRA_ARGS:=""}
if [[ ${NINJA_BUILD:-} == true ]]; then
  CMAKE_EXTRA_ARGS+=" -G Ninja"
fi
CMAKE_TARGET=${COMPNAME}.${TARGET}
if [[ ${CI:-} == true ]]; then
  CMAKE_TARGET=$CMAKE_TARGET.ci
fi
#
# allow explicit setting of the name of the build
# dir, otherwise default to the CMAKE_TARGET value
#
: "${BUILD_DIR:=$CMAKE_TARGET}"
BUILDARGS=" -j${JOBS}"
if [[ ${VERBOSE_BUILD:-} == true ]]; then
  CMAKE_EXTRA_ARGS+=" -DCMAKE_VERBOSE_MAKEFILE=ON"

  # TODO: if we use a different generator, this
  # option to build verbose would need to change:
  if [[ ${NINJA_BUILD:-} == true ]]; then
    BUILDARGS+=" -v"
  else
    BUILDARGS+=" verbose=1"
  fi
fi
if [[ ${USE_CCACHE:-} == true ]]; then
  echo "using ccache with basedir [${CCACHE_BASEDIR:-}]"
  CMAKE_EXTRA_ARGS+=" -DCMAKE_C_COMPILER_LAUNCHER=ccache -DCMAKE_CXX_COMPILER_LAUNCHER=ccache"
fi
if [ -d "build/${BUILD_DIR}" ]; then
  rm -rf "build/${BUILD_DIR}"
fi

mkdir -p "build/${BUILD_DIR}"
pushd "build/${BUILD_DIR}"
$time cmake ../.. -Dtarget=$CMAKE_TARGET ${CMAKE_EXTRA_ARGS}
if [[ ${TARGET} == "docs" ]]; then
  $time cmake --build . --target docs -- $BUILDARGS
  ## mimic the standard test output for docs build
  ## to make controlling processes like jenkins happy
  if [ -f html_doc/index.html ]; then
      echo "1 case, 1 test total, 0 failures"
  else
      echo "1 case, 1 test total, 1 failures"
  fi
  exit
else
  $time cmake --build . -- $BUILDARGS
  if [[ ${BUILD_BOTH:-} == true ]]; then
    if [[ ${TARGET} == *.unity ]]; then
      cmake --build . --target rippled_classic -- $BUILDARGS
    else
      cmake --build . --target rippled_unity -- $BUILDARGS
    fi
  fi
fi
popd
export APP_PATH="$PWD/build/${BUILD_DIR}/${APP}"
echo "using APP_PATH: $APP_PATH"


# See what we've actually built
ldd $APP_PATH

if [[ ${APP} == "rippled" ]]; then
  APP_ARGS+="--unittest --quiet --unittest-log"
  # Only report on src/ripple files
  export LCOV_FILES="*/src/ripple/*"
  # Nothing to explicitly exclude
  export LCOV_EXCLUDE_FILES="LCOV_NO_EXCLUDE"
  if [[ $TARGET != "coverage" && ${PARALLEL_TESTS:-} == true ]]; then
    APP_ARGS+=" --unittest-jobs ${JOBS}"
  fi
else
  : ${APP_ARGS:=}
  : ${LCOV_FILES:="*/src/*"}
  # Don't exclude anything
  : ${LCOV_EXCLUDE_FILES:="LCOV_NO_EXCLUDE"}
fi

if [[ $TARGET == "coverage" ]]; then
  export PATH=$PATH:$LCOV_ROOT/usr/bin

  # Create baseline coverage data file
  lcov --no-external -c -i -d . -o baseline.info | grep -v "ignoring data for external file"
fi

if [[ ${SKIP_TESTS:-} == true ]]; then
  echo "skipping tests for ${TARGET}"
  exit
fi

if [[ $TARGET == debug* && -v GDB_ROOT && -x $GDB_ROOT/bin/gdb ]]; then
  $GDB_ROOT/bin/gdb -v
  # Execute unit tests under gdb, printing a call stack
  # if we get a crash.
  export APP_ARGS
  $GDB_ROOT/bin/gdb -return-child-result -quiet -batch \
                    -ex "set env MALLOC_CHECK_=3" \
                    -ex "set print thread-events off" \
                    -ex run \
                    -ex "thread apply all backtrace full" \
                    -ex "quit" \
                    --args $APP_PATH $APP_ARGS
else
  $APP_PATH $APP_ARGS
fi

if [[ $TARGET == "coverage" ]]; then
  # Create test coverage data file
  lcov --no-external -c -d . -o tests.info | grep -v "ignoring data for external file"

  # Combine baseline and test coverage data
  lcov -a baseline.info -a tests.info -o lcov-all.info

  # Included files
  lcov -e "lcov-all.info" "${LCOV_FILES}" -o lcov.pre.info

  # Excluded files
  lcov --remove lcov.pre.info "${LCOV_EXCLUDE_FILES}" -o lcov.info

  # Push the results (lcov.info) to codecov
  codecov -X gcov # don't even try and look for .gcov files ;)

  find . -name "*.gcda" | xargs rm -f
fi


