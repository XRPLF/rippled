#!/usr/bin/env bash
# Assumptions:
# 1) BOOST_ROOT and BOOST_URL are already defined,
# and contain valid values. BOOST_URL2 may be defined
# as a fallback. BOOST_WGET_OPTIONS may be defined with
# retry options if the download(s) fail on the first try.
# 2) The last namepart of BOOST_ROOT matches the
# folder name internal to boost's .tar.gz
# When testing you can force a boost build by clearing travis caches:
# https://travis-ci.org/ripple/rippled/caches
#
# Will pass any command line params through to setup-msvc.sh
set -exu

odir=$(pwd)
: ${MSVC_VER:=14.16}
: ${BOOST_TOOLSET:=msvc-${MSVC_VER}}
: ${BOOST_WGET_OPTIONS:=}

if [[ -d "$BOOST_ROOT/lib" || -d "${BOOST_ROOT}/stage/lib" ]] ; then
    echo "Using cached boost at $BOOST_ROOT"
    exit
fi

if [[ ! -v BOOST_FILE ]]
then
    #fetch/unpack:
    fn=$(basename -- "$BOOST_URL")
    ext="${fn##*.}"
    BOOST_FILE=/tmp/boost.tar.${ext}
    wopt="--quiet"
    wget ${wopt} $BOOST_URL -O "${BOOST_FILE}" || \
      ( [ -n "${BOOST_URL2}" ] && \
        wget ${wopt} $BOOST_URL2 -O "${BOOST_FILE}" ) || \
      ( [ -n "${BOOST_WGET_OPTIONS}" ] &&
        ( wget ${wopt} ${BOOST_WGET_OPTIONS} $BOOST_URL -O "${BOOST_FILE}" || \
          ( [ -n "${BOOST_URL2}" ] && \
            wget ${wopt} ${BOOST_WGET_OPTIONS} $BOOST_URL2 -O "${BOOST_FILE}" )
        )
      )
fi
cd $(dirname $BOOST_ROOT)
rm -fr ${BOOST_ROOT}
mkdir -pv ${BOOST_ROOT}
cd ${BOOST_ROOT}
pwd
tar xf ${BOOST_FILE} --strip-components 1
ls -l

BLDARGS=()
if [[ ${BOOST_BUILD_ALL:-false} == "true" ]]; then
    # we never need boost-python...so even for ALL
    # option we can skip it
    BLDARGS+=(--without-python)
else
    BLDARGS+=(--with-chrono)
    BLDARGS+=(--with-container)
    BLDARGS+=(--with-context)
    BLDARGS+=(--with-coroutine)
    BLDARGS+=(--with-date_time)
    BLDARGS+=(--with-filesystem)
    BLDARGS+=(--with-program_options)
    BLDARGS+=(--with-regex)
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

    cat project-config.jam
    for file in \
      /c/Program\ Files\ \(x86\)/Microsoft\ Visual\ Studio/*/*/VC/Tools/MSVC/*/bin/Hostx86/x86/cl.exe
    do
      if [[ ! -e "${file}" ]]
      then
        continue
      fi
      winfile=$( cygpath --windows "${file}" )
      grep -v "using msvc" project-config.jam > project-config.tmp
      echo "using msvc : ${MSVC_VER} : \"${winfile}\" ;" >> project-config.tmp
      diff project-config.jam project-config.tmp || true
      mv -fv project-config.tmp project-config.jam
      break
    done

    . "${odir}/bin/sh/setup-msvc.sh" "${@}"

    ./b2.exe "${BLDARGS[@]}" stage
    ./b2.exe "${BLDARGS[@]}" install
fi

if [[ ${CI:-false} == "true" ]]; then
    # save some disk space...these are mostly
    # obj files and don't need to be kept in CI contexts
    rm -rf bin.v2
fi

cd $odir

