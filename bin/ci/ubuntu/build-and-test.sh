#!/usr/bin/env bash
set -ex

function version_ge() { test "$(echo "$@" | tr " " "\n" | sort -rV | head -n 1)" == "$1"; }

__dirname=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
echo "using CC: ${CC}"
"${CC}" --version
export CC

COMPNAME=$(basename $CC)
echo "using CXX: ${CXX:-notset}"
if [[ $CXX ]]; then
   "${CXX}" --version
   export CXX
fi
: ${BUILD_TYPE:=Debug}
echo "BUILD TYPE: ${BUILD_TYPE}"

: ${TARGET:=install}
echo "BUILD TARGET: ${TARGET}"

JOBS=${NUM_PROCESSORS:-2}
if [[ ${TRAVIS:-false} != "true" ]]; then
    JOBS=$((JOBS+1))
fi

if [[ ! -z "${CMAKE_EXE:-}" ]] ; then
    export PATH="$(dirname ${CMAKE_EXE}):$PATH"
fi

if [ -x /usr/bin/time ] ; then
    : ${TIME:="Duration: %E"}
    export TIME
    time=/usr/bin/time
else
    time=
fi

echo "Building rippled"
: ${CMAKE_EXTRA_ARGS:=""}
if [[ ${NINJA_BUILD:-} == true ]]; then
    CMAKE_EXTRA_ARGS+=" -G Ninja"
fi

coverage=false
if [[ "${TARGET}" == "coverage_report" ]] ; then
    echo "coverage option detected."
    coverage=true
fi

cmake --version
CMAKE_VER=$(cmake --version | cut -d " " -f 3 | head -1)

#
# allow explicit setting of the name of the build
# dir, otherwise default to the compiler.build_type
#
: "${BUILD_DIR:=${COMPNAME}.${BUILD_TYPE}}"
BUILDARGS="--target ${TARGET}"
BUILDTOOLARGS=""
if version_ge $CMAKE_VER "3.12.0" ; then
    BUILDARGS+=" --parallel"
fi

if [[ ${NINJA_BUILD:-} == false ]]; then
    if version_ge $CMAKE_VER "3.12.0" ; then
        BUILDARGS+=" ${JOBS}"
    else
        BUILDTOOLARGS+=" -j ${JOBS}"
    fi
fi

if [[ ${VERBOSE_BUILD:-} == true ]]; then
    CMAKE_EXTRA_ARGS+=" -DCMAKE_VERBOSE_MAKEFILE=ON"
    if version_ge $CMAKE_VER "3.14.0" ; then
        BUILDARGS+=" --verbose"
    else
        if [[ ${NINJA_BUILD:-} == false ]]; then
            BUILDTOOLARGS+=" verbose=1"
        else
            BUILDTOOLARGS+=" -v"
        fi
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

# cleanup possible artifacts
rm -fv CMakeFiles/CMakeOutput.log CMakeFiles/CMakeError.log
for nih_path in ${NIH_CACHE_ROOT}/*/*/*/src ${NIH_CACHE_ROOT}/*/*/src
do
  for dir in lz4 snappy rocksdb
  do
    if [ -e ${nih_path}/${dir} -a \! -e ${nih_path}/${dir}/.git ]
    then
      ls -la ${nih_path}/${dir}*
      rm -rfv ${nih_path}/${dir}*
    fi
  done
done

# generate
${time} cmake ../.. -DCMAKE_BUILD_TYPE=${BUILD_TYPE} ${CMAKE_EXTRA_ARGS}
for file in CMakeOutput.log CMakeError.log
do
  if [ -f CMakeFiles/${file} ]
  then
    ls -l CMakeFiles/${file}
    cat CMakeFiles/${file}
  fi
done
# build
export DESTDIR=$(pwd)/_INSTALLED_
${time} eval cmake --build . ${BUILDARGS} -- ${BUILDTOOLARGS}

if [[ ${TARGET} == "docs" ]]; then
    ## mimic the standard test output for docs build
    ## to make controlling processes like jenkins happy
    if [ -f docs/html/index.html ]; then
        echo "1 case, 1 test total, 0 failures"
    else
        echo "1 case, 1 test total, 1 failures"
    fi
    exit
fi
popd

if [[ "${TARGET}" == "validator-keys" ]] ; then
    export APP_PATH="$PWD/build/${BUILD_DIR}/validator-keys/validator-keys"
else
    export APP_PATH="$PWD/build/${BUILD_DIR}/rippled"
fi
echo "using APP_PATH: ${APP_PATH}"

# See what we've actually built
ldd ${APP_PATH}

: ${APP_ARGS:=}

if [[ "${TARGET}" == "validator-keys" ]] ; then
    APP_ARGS="--unittest"
else
    function join_by { local IFS="$1"; shift; echo "$*"; }

    # This is a list of manual tests
    # in rippled that we want to run
    # ORDER matters here...sorted in approximately
    # descending execution time (longest running tests at top)
    declare -a manual_tests=(
        'ripple.ripple_data.reduce_relay_simulate'
        'ripple.tx.Offer_manual'
        'ripple.tx.CrossingLimits'
        'ripple.tx.PlumpBook'
        'ripple.app.Flow_manual'
        'ripple.tx.OversizeMeta'
        'ripple.consensus.DistributedValidators'
        'ripple.app.NoRippleCheckLimits'
        'ripple.NodeStore.Timing'
        'ripple.consensus.ByzantineFailureSim'
        'beast.chrono.abstract_clock'
        'beast.unit_test.print'
    )
    if [[ ${TRAVIS:-false} != "true" ]]; then
        # these two tests cause travis CI to run out of memory.
        # TODO: investigate possible workarounds.
        manual_tests=(
            'ripple.consensus.ScaleFreeSim'
            'ripple.tx.FindOversizeCross'
            "${manual_tests[@]}"
        )
    fi

    if [[ ${MANUAL_TESTS:-} == true ]]; then
        APP_ARGS+=" --unittest=$(join_by , "${manual_tests[@]}")"
    else
        APP_ARGS+=" --unittest --quiet --unittest-log"
    fi
    if [[ ${coverage} == false && ${PARALLEL_TESTS:-} == true ]]; then
        APP_ARGS+=" --unittest-jobs ${JOBS}"
    fi

    if [[ ${IPV6_TESTS:-} == true ]]; then
        APP_ARGS+=" --unittest-ipv6"
    fi
fi

if [[ ${coverage} == true && $CC =~ ^gcc ]]; then
    # Push the results (lcov.info) to codecov
    codecov -X gcov # don't even try and look for .gcov files ;)
    find . -name "*.gcda" | xargs rm -f
fi

if [[ ${SKIP_TESTS:-} == true ]]; then
    echo "skipping tests."
    exit
fi

ulimit -a
corepat=$(cat /proc/sys/kernel/core_pattern)
if [[ ${corepat} =~ ^[:space:]*\| ]] ; then
    echo "WARNING: core pattern is piping - can't search for core files"
    look_core=false
else
    look_core=true
    coredir=$(dirname ${corepat})
fi
if [[ ${look_core} == true ]]; then
    before=$(ls -A1 ${coredir})
fi

set +e
echo "Running tests for ${APP_PATH}"
if [[ ${MANUAL_TESTS:-} == true && ${PARALLEL_TESTS:-} != true ]]; then
    for t in "${manual_tests[@]}" ; do
        ${APP_PATH} --unittest=${t}
        TEST_STAT=$?
        if [[ $TEST_STAT -ne 0 ]] ; then
            break
        fi
    done
else
    ${APP_PATH} ${APP_ARGS}
    TEST_STAT=$?
fi
set -e

if [[ ${look_core} == true ]]; then
    after=$(ls -A1 ${coredir})
    oIFS="${IFS}"
    IFS=$'\n\r'
    found_core=false
    for l in $(diff -w --suppress-common-lines <(echo "$before") <(echo "$after")) ; do
        if [[ "$l" =~ ^[[:space:]]*\>[[:space:]]*(.+)$ ]] ; then
            corefile="${BASH_REMATCH[1]}"
            echo "FOUND core dump file at '${coredir}/${corefile}'"
            gdb_output=$(/bin/mktemp /tmp/gdb_output_XXXXXXXXXX.txt)
            found_core=true
            gdb \
                -ex "set height 0" \
                -ex "set logging file ${gdb_output}" \
                -ex "set logging on" \
                -ex "print 'ripple::BuildInfo::versionString'" \
                -ex "thread apply all backtrace full" \
                -ex "info inferiors" \
                -ex quit \
                "$APP_PATH" \
                "${coredir}/${corefile}" &> /dev/null

            echo -e "CORE INFO: \n\n $(cat ${gdb_output}) \n\n)"
        fi
    done
    IFS="${oIFS}"
fi

if [[ ${found_core} == true ]]; then
    exit -1
else
    exit $TEST_STAT
fi

