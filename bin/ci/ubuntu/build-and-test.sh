#!/usr/bin/env bash
set -ex

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

# Ensure APP defaults to rippled if it's not set.
: ${APP:=rippled}
echo "using APP: ${APP}"

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

if [[ -z "${MAX_TIME:-}" ]] ; then
    timeout_cmd=""
else
    timeout_cmd="timeout ${MAX_TIME}"
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
fi

#
# allow explicit setting of the name of the build
# dir, otherwise default to the compiler.build_type
#
: "${BUILD_DIR:=${COMPNAME}.${BUILD_TYPE}}"
BUILDARGS=""
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
${time} cmake ../.. -DCMAKE_BUILD_TYPE=${BUILD_TYPE} ${CMAKE_EXTRA_ARGS}
# build
export DESTDIR=$(pwd)/_INSTALLED_
time ${timeout_cmd} cmake --build . --target ${TARGET} --parallel -- $BUILDARGS
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
echo "using APP_PATH: ${APP_PATH}"

# See what we've actually built
ldd ${APP_PATH}

function join_by { local IFS="$1"; shift; echo "$*"; }

# This is a list of manual tests
# in rippled that we want to run
# ORDER matters here...sorted in approximately
# descending execution time (longest running tests at top)
declare -a manual_tests=(
    'ripple.ripple_data.digest'
    'ripple.tx.Offer_manual'
    'ripple.app.PayStrandAllPairs'
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
        ${timeout_cmd} ${APP_PATH} --unittest=${t}
        TEST_STAT=$?
        if [[ $TEST_STAT -ne 0 ]] ; then
            break
        fi
    done
else
    ${timeout_cmd} ${APP_PATH} ${APP_ARGS}
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

