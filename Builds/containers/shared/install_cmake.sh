#!/usr/bin/env bash
set -e

IFS=. read cm_maj cm_min cm_rel <<<"$1"
: ${cm_rel:-0}
CMAKE_ROOT=${2:-"${HOME}/cmake"}

function cmake_version ()
{
    if [[ -d ${CMAKE_ROOT} ]] ; then
        local perms=$(test $(uname) = "Linux" && echo "/111" || echo "+111")
        local installed=$(find ${CMAKE_ROOT} -perm ${perms} -type f -name cmake)
        if [[ "${installed}" != "" ]] ; then
            echo "$(${installed} --version | head -1)"
        fi
    fi
}

installed=$(cmake_version)
if [[ "${installed}" != "" && ${installed} =~ ${cm_maj}.${cm_min}.${cm_rel} ]] ; then
    echo "cmake already installed: ${installed}"
    exit
fi
# From CMake 20+ "Linux" is lowercase so using `uname` won't create be the correct path
if [ ${cm_min} -gt 19 ]; then
    linux="linux"
else
    linux=$(uname)
fi
pkgname="cmake-${cm_maj}.${cm_min}.${cm_rel}-${linux}-x86_64.tar.gz"
tmppkg="/tmp/cmake.tar.gz"
wget --quiet https://cmake.org/files/v${cm_maj}.${cm_min}/${pkgname} -O ${tmppkg}
mkdir -p ${CMAKE_ROOT}
cd ${CMAKE_ROOT}
tar --strip-components 1 -xf ${tmppkg}
rm -f ${tmppkg}
echo "installed: $(cmake_version)"
