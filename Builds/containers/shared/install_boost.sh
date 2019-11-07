#!/usr/bin/env bash
# Assumptions:
# 1) BOOST_ROOT and BOOST_URL are already defined,
# and contain valid values.
# 2) The last namepart of BOOST_ROOT matches the
# folder name internal to boost's .tar.gz
# When testing you can force a boost build by clearing travis caches:
# https://travis-ci.org/ripple/rippled/caches
set -exu

odir=$(pwd)
: ${BOOST_TOOLSET:=msvc-14.1}

if [[ -d "$BOOST_ROOT/lib" || -d "${BOOST_ROOT}/stage/lib" ]] ; then
    echo "Using cached boost at $BOOST_ROOT"
    exit
fi

#fetch/unpack:
fn=$(basename -- "$BOOST_URL")
ext="${fn##*.}"
wget --quiet $BOOST_URL -O /tmp/boost.tar.${ext}
cd $(dirname $BOOST_ROOT)
rm -fr ${BOOST_ROOT}
tar xf /tmp/boost.tar.${ext}
cd $BOOST_ROOT

BLDARGS=()
if [[ ${BOOST_BUILD_ALL:-false} == "true" ]]; then
    # we never need boost-python...so even for ALL
    # option we can skip it
    BLDARGS+=(--without-python)
else
    BLDARGS+=(--with-chrono)
    BLDARGS+=(--with-context)
    BLDARGS+=(--with-coroutine)
    BLDARGS+=(--with-date_time)
    BLDARGS+=(--with-filesystem)
    BLDARGS+=(--with-program_options)
    BLDARGS+=(--with-regex)
    BLDARGS+=(--with-serialization)
    BLDARGS+=(--with-system)
    BLDARGS+=(--with-atomic)
    BLDARGS+=(--with-thread)
fi
BLDARGS+=(-j$((2*${NUM_PROCESSORS:-2})))
BLDARGS+=(--prefix=${BOOST_ROOT}/_INSTALLED_)
BLDARGS+=(-d0) # suppress messages/output

if [[ -z ${COMSPEC:-} ]]; then
    if [[ "$(uname)" == "Darwin" ]] ; then
        BLDARGS+=(cxxflags="-std=c++14 -fvisibility=default")
    else
        BLDARGS+=(cxxflags="-std=c++14")
        BLDARGS+=(runtime-link="static,shared")
    fi
    BLDARGS+=(--layout=tagged)
    ./bootstrap.sh
    ./b2 "${BLDARGS[@]}" stage
    ./b2 "${BLDARGS[@]}" install
else
    BLDARGS+=(runtime-link="static,shared")
    BLDARGS+=(--layout=versioned)
    BLDARGS+=(--toolset="${BOOST_TOOLSET}")
    BLDARGS+=(address-model=64)
    BLDARGS+=(architecture=x86)
    BLDARGS+=(link=static)
    BLDARGS+=(threading=multi)
    cmd /E:ON /D /S /C"bootstrap.bat"
    ./b2.exe "${BLDARGS[@]}" stage
    ./b2.exe "${BLDARGS[@]}" install
fi

if [[ ${CI:-false} == "true" ]]; then
    # save some disk space...these are mostly
    # obj files and don't need to be kept in CI contexts
    rm -rf bin.v2
fi

cd $odir

