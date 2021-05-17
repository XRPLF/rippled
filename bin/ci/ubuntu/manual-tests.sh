#!/usr/bin/env bash
set -e

function join_by { local IFS="$1"; shift; echo -n "$*"; }

if [[ $# -gt 0 && -e "${1}" ]]
then
    # Get the list of manual tests from rippled, and exclude the
    # troublemakers
    # DetectCrash intentionally fails
    exclude="grep -v -e beast.unit_test.DetectCrash"
    # DatabaseShard test broke in a 1.8 beta
    exclude+=" -e ripple.NodeStore.DatabaseShard"
    if [[ ${TRAVIS:-false} == "true" || ${GITHUB_ACTIONS:-false} == "true" ]]
    then
        exclude+=" -e ripple.consensus.ScaleFreeSim \
            -e ripple.tx.FindOversizeCross"
    fi
    if type -p dos2unix >& /dev/null
    then
      dos2unix="dos2unix"
    else
      dos2unix="cat"
    fi

    declare -a manual_tests=( $( "${1}" --unittest=beast.unit_test.print | \
        grep '|M|' | cut -d\  -f2 | ${exclude} | ${dos2unix} ) )
    join_by , "${manual_tests[@]}"
    exit 0
fi

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
    'ripple.ripple_data.compression'
    'ripple.NodeStore.Timing'
    'ripple.consensus.ByzantineFailureSim'
    'beast.chrono.abstract_clock'
    'beast.unit_test.print'
)
if [[ ${TRAVIS:-false} != "true" && ${GITHUB_ACTIONS:-false} != "true" ]]
then
    # these two tests cause travis CI to run out of memory.
    # TODO: investigate possible workarounds.
    manual_tests=(
        'ripple.consensus.ScaleFreeSim'
        'ripple.tx.FindOversizeCross'
        "${manual_tests[@]}"
    )
fi

join_by , "${manual_tests[@]}"
