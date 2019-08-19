#!/usr/bin/env bash
set -ex

function build_boost()
{
    local boost_ver=$1
    local do_link=$2
    local boost_path=$(echo "${boost_ver}" | sed -e 's!\.!_!g')
    mkdir -p /opt/local
    cd /opt/local
    BOOST_ROOT=/opt/local/boost_${boost_path}
    BOOST_URL="https://dl.bintray.com/boostorg/release/${boost_ver}/source/boost_${boost_path}.tar.bz2"
    BOOST_BUILD_ALL=true
    . /tmp/install_boost.sh
    if [ "$do_link" = true ] ; then
        ln -s ./boost_${boost_path} boost
    fi
}

build_boost "1.70.0" true

# installed in opt, so won't be used
# unless specified by OPENSSL_ROOT_DIR
cd /tmp
OPENSSL_VER=1.1.1
wget https://www.openssl.org/source/openssl-${OPENSSL_VER}.tar.gz
tar xf openssl-${OPENSSL_VER}.tar.gz
cd openssl-${OPENSSL_VER}
# NOTE: add -g to the end of the following line if we want debug symbols for openssl
SSLDIR=$(openssl version -d | cut -d: -f2 | tr -d [:space:]\")
./config -fPIC --prefix=/opt/local/openssl --openssldir=${SSLDIR} zlib shared
make -j$(nproc)
make install
cd ..
rm -f openssl-${OPENSSL_VER}.tar.gz
rm -rf openssl-${OPENSSL_VER}
LD_LIBRARY_PATH=${LD_LIBRARY_PATH:-}:/opt/local/openssl/lib /opt/local/openssl/bin/openssl version -a

if [ "${CI_USE}" = true ] ; then
    cd /tmp
    wget https://github.com/doxygen/doxygen/archive/Release_1_8_16.tar.gz
    tar xf Release_1_8_16.tar.gz
    cd doxygen-Release_1_8_16
    mkdir build
    cd build
    cmake -G "Unix Makefiles" ..
    make -j$(nproc)
    make install
    cd ../..
    rm -f Release_1_8_16.tar.gz
    rm -rf doxygen-Release_1_8_16

    mkdir -p /opt/plantuml
    wget -O /opt/plantuml/plantuml.jar https://downloads.sourceforge.net/project/plantuml/plantuml.jar

    cd /tmp
    wget https://github.com/linux-test-project/lcov/releases/download/v1.14/lcov-1.14.tar.gz
    tar xfz lcov-1.14.tar.gz
    cd lcov-1.14
    make install PREFIX=/usr/local
    cd ..
    rm -r lcov-1.14 lcov-1.14.tar.gz

    cd /tmp
    wget https://github.com/ccache/ccache/releases/download/v3.7.3/ccache-3.7.3.tar.gz
    tar xf ccache-3.7.3.tar.gz
    cd ccache-3.7.3
    ./configure --prefix=/usr/local
    make
    make install
    cd ..
    rm -f ccache-3.7.3.tar.gz
    rm -rf ccache-3.7.3

    pip install requests
    pip install https://github.com/codecov/codecov-python/archive/master.zip
fi

