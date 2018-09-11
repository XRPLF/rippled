#!/bin/bash -u
# We use set -e and bash with -u to bail on first non zero exit code of any
# processes launched or upon any unbound variable.
# We use set -x to print commands before running them to help with
# debugging.
set -ex
__dirname=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
echo "using CC: $CC"
"${CC}" --version
export CC
COMPNAME=$(basename $CC)
echo "using CXX: ${CXX:-notset}"
if [[ $CXX ]]; then
  "${CXX}" --version
  export CXX
fi
: ${BUILD_TYPE:=Debug}
echo "BUILD TYPE: $BUILD_TYPE"

: ${TARGET:=install}
echo "BUILD TARGET: $TARGET"

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

if [[ "${MAX_TIME:-}" == "" ]] ; then
  tcmd=""
else
  tcmd="timeout ${MAX_TIME}"
fi

echo "cmake building ${APP}"
: ${CMAKE_EXTRA_ARGS:=""}
if [[ ${NINJA_BUILD:-} == true ]]; then
  CMAKE_EXTRA_ARGS+=" -G Ninja"
fi

coverage=false
if [[ "${TARGET}" == "coverage_report" ]] ; then
    echo "coverage option detected."
    coverage=true
    export PATH=$PATH:$LCOV_ROOT/usr/bin
fi

#
# allow explicit setting of the name of the build
# dir, otherwise default to the compiler.build_type
#
: "${BUILD_DIR:=${COMPNAME}.${BUILD_TYPE}}"
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
# generate
$time cmake ../.. -DCMAKE_BUILD_TYPE=${BUILD_TYPE} ${CMAKE_EXTRA_ARGS}
# build
export DESTDIR=$(pwd)/_INSTALLED_
time $tcmd cmake --build . --target ${TARGET} -- $BUILDARGS
if [[ ${TARGET} == "docs" ]]; then
  ## mimic the standard test output for docs build
  ## to make controlling processes like jenkins happy
  if [ -f html_doc/index.html ]; then
      echo "1 case, 1 test total, 0 failures"
  else
      echo "1 case, 1 test total, 1 failures"
  fi
  exit
fi
popd
export APP_PATH="$PWD/build/${BUILD_DIR}/${APP}"
echo "using APP_PATH: $APP_PATH"

# See what we've actually built
ldd $APP_PATH

function join_by { local IFS="$1"; shift; echo "$*"; }

# This is a list of manual tests
# in rippled that we want to run
declare -a manual_tests=(
  "beast.chrono.abstract_clock"
  "beast.unit_test.print"
  "ripple.NodeStore.Timing"
  "ripple.app.Flow_manual"
  "ripple.app.NoRippleCheckLimits"
  "ripple.app.PayStrandAllPairs"
  "ripple.consensus.ByzantineFailureSim"
  "ripple.consensus.DistributedValidators"
  "ripple.consensus.ScaleFreeSim"
  "ripple.ripple_data.digest"
  "ripple.tx.CrossingLimits"
  "ripple.tx.FindOversizeCross"
  "ripple.tx.Offer_manual"
  "ripple.tx.OversizeMeta"
  "ripple.tx.PlumpBook"
)

: ${APP_ARGS:=}
if [[ ${APP} == "rippled" ]]; then
  if [[ ${MANUAL_TESTS:-} == true ]]; then
    APP_ARGS+=" --unittest=$(join_by , "${manual_tests[@]}")"
  else
    APP_ARGS+=" --unittest --quiet --unittest-log"
  fi
  if [[ ${coverage} == false && ${PARALLEL_TESTS:-} == true ]]; then
    APP_ARGS+=" --unittest-jobs ${JOBS}"
  fi
fi

if [[ $coverage == true ]]; then
  # Push the results (lcov.info) to codecov
  codecov -X gcov # don't even try and look for .gcov files ;)
  find . -name "*.gcda" | xargs rm -f
fi

if [[ ${SKIP_TESTS:-} == true ]]; then
  echo "skipping tests."
  exit
fi

if [[ ${DEBUGGER:-true} == "true" && -v GDB_ROOT && -x $GDB_ROOT/bin/gdb ]]; then
  $GDB_ROOT/bin/gdb -v
  # Execute unit tests under gdb, printing a call stack
  # if we get a crash.
  export APP_ARGS
  $tcmd $GDB_ROOT/bin/gdb -return-child-result -quiet -batch \
                    -ex "set env MALLOC_CHECK_=3" \
                    -ex "set print thread-events off" \
                    -ex run \
                    -ex "thread apply all backtrace full" \
                    -ex "quit" \
                    --args $APP_PATH $APP_ARGS
else
  $tcmd $APP_PATH $APP_ARGS
fi


