#!/usr/bin/env bash

#
# This scripts installs boost and protobuf built with clang. This is needed on
# ubuntu 15.10 when building with clang
# It will build these in a 'clang' subdirectory that it creates below the directory
# this script is run from. If a clang directory already exists the script will refuse
# to run.

if hash lsb_release 2>/dev/null; then
    if [ $(lsb_release -si) == "Ubuntu" ]; then
        ubuntu_release=$(lsb_release -sr)
    fi
fi

if [ -z "${ubuntu_release}" ]; then
    echo "System not supported"
    exit 1
fi

if ! hash clang 2>/dev/null; then
    clang_version=3.7
    if [ ${ubuntu_release} == "16.04" ]; then
        clang_version=3.8
    fi
    sudo apt-get -y install clang-${clang_version}
    update-alternatives --install /usr/bin/clang clang /usr/bin/clang-${clang_version} 99 clang++
    hash -r
    if ! hash clang 2>/dev/null; then
        echo "Please install clang"
        exit 1
    fi
fi

if [ ${ubuntu_release} != "16.04" ] && [ ${ubuntu_release} != "15.10" ]; then
    echo "clang specific boost and protobuf not needed"
    exit 0
fi

if [ -d clang ]; then
    echo "clang directory already exists. Cowardly refusing to run"
    exit 1
fi

if ! hash wget 2>/dev/null; then
    sudo apt-get -y install wget
    hash -r
    if ! hash wget 2>/dev/null; then
        echo "Please install wget"
        exit 1
    fi
fi

num_procs=$(lscpu -p | grep -v '^#' | sort -u -t, -k 2,4 | wc -l) # pysical cores

mkdir clang
pushd clang > /dev/null

# Install protobuf
pb=protobuf-2.6.1
pb_tar=${pb}.tar.gz 
wget -O ${pb_tar} https://github.com/google/protobuf/releases/download/v2.6.1/${pb_tar}
tar xf ${pb_tar}
rm ${pb_tar}
pushd ${pb} > /dev/null
./configure CC=clang CXX=clang++ CXXFLAGS='-std=c++14 -O3 -g'
make -j${num_procs}
popd > /dev/null

# Install boost
boost_ver=1.60.0
bd=boost_${boost_ver//./_}
bd_tar=${bd}.tar.gz
wget -O ${bd_tar} http://sourceforge.net/projects/boost/files/boost/${boost_ver}/${bd_tar}
tar xf ${bd_tar}
rm ${bd_tar}
pushd ${bd} > /dev/null
./bootstrap.sh
./b2 toolset=clang -j${num_procs}
popd > /dev/null

popd > /dev/null
