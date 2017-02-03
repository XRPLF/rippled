#!/usr/bin/env bash

#
# This script builds boost with the correct ABI flags for ubuntu
#

version=63
patch=0

if hash lsb_release 2>/dev/null; then
    if [ $(lsb_release -si) == "Ubuntu" ]; then
        ubuntu_release=$(lsb_release -sr)
    fi
fi

if [ -z "${ubuntu_release}" ]; then
    echo "System not supported"
    exit 1
fi

extra_defines=""
if (( $(bc <<< "${ubuntu_release} < 15.1") )); then
    extra_defines="define=_GLIBCXX_USE_CXX11_ABI=0"
fi
num_procs=$(lscpu -p | grep -v '^#' | sort -u -t, -k 2,4 | wc -l) # pysical cores
printf "\nBuild command will be: ./b2 -j${num_procs} ${extra_defines}\n\n"

boost_dir="boost_1_${version}_${patch}"
boost_tag="boost-1.${version}.${patch}"
git clone -b "${boost_tag}" --recursive https://github.com/boostorg/boost.git "${boost_dir}"

cd ${boost_dir}
git checkout --force ${boost_tag}
git submodule foreach git checkout --force ${boost_tag}
./bootstrap.sh
./b2 headers
./b2 -j${num_procs} ${extra_defines}
echo "Build command was: ./b2 -j${num_procs} ${extra_defines}"
echo "Don't forget to set BOOST_ROOT!"
