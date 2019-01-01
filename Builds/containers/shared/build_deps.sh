#!/usr/bin/env bash
set -ex

function build_boost()
{
    local boost_ver=$1
    local do_link=$2
    local boost_path=$(echo "${boost_ver}" | sed -e 's!\.!_!g')
    cd /tmp
    wget https://dl.bintray.com/boostorg/release/${boost_ver}/source/boost_${boost_path}.tar.bz2
    mkdir -p /opt/local
    cd /opt/local
    tar xf /tmp/boost_${boost_path}.tar.bz2
    if [ "$do_link" = true ] ; then
        ln -s ./boost_${boost_path} boost
    fi
    cd boost_${boost_path}
    ./bootstrap.sh
    ./b2 -j$(nproc)
    ./b2 stage
    cd ..
    rm -f /tmp/boost_${boost_path}.tar.bz2
}

build_boost "1.67.0" true
build_boost "1.68.0" false

# installed in opt, so won't be used
# unless specified by OPENSSL_ROOT_DIR
cd /tmp
OPENSSL_VER=1.1.1
wget https://www.openssl.org/source/openssl-${OPENSSL_VER}.tar.gz
tar xf openssl-${OPENSSL_VER}.tar.gz
cd openssl-${OPENSSL_VER}
# NOTE: add -g to the end of the following line if we want debug symbols for openssl
./config -fPIC --prefix=/opt/local/openssl --openssldir=/opt/local/openssl zlib shared
make -j$(nproc)
make install
cd ..
rm -f openssl-${OPENSSL_VER}.tar.gz
rm -rf openssl-${OPENSSL_VER}
LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:/opt/local/openssl/lib /opt/local/openssl/bin/openssl version -a

cd /tmp
wget https://github.com/doxygen/doxygen/archive/Release_1_8_14.tar.gz
tar xf Release_1_8_14.tar.gz
cd doxygen-Release_1_8_14
mkdir build
cd build
cmake -G "Unix Makefiles" ..
make -j$(nproc)
make install
cd ../..
rm -f Release_1_8_14.tar.gz
rm -rf doxygen-Release_1_8_14

mkdir -p /opt/plantuml
wget -O /opt/plantuml/plantuml.jar https://downloads.sourceforge.net/project/plantuml/plantuml.jar

cd /tmp
wget https://github.com/linux-test-project/lcov/releases/download/v1.13/lcov-1.13.tar.gz
tar xfz lcov-1.13.tar.gz
cd lcov-1.13
make install PREFIX=/usr/local
cd ..
rm -r lcov-1.13 lcov-1.13.tar.gz

pip install requests
pip install https://github.com/codecov/codecov-python/archive/master.zip

set +e
mkdir -p /opt/local/nih_cache
mkdir -p /opt/jenkins
set -e


