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
    BOOST_URL="https://boostorg.jfrog.io/artifactory/main/release/${boost_ver}/source/boost_${boost_path}.tar.gz"
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
OPENSSL_VER=1.1.1d
wget https://www.openssl.org/source/openssl-${OPENSSL_VER}.tar.gz
tar xf openssl-${OPENSSL_VER}.tar.gz
cd openssl-${OPENSSL_VER}
# NOTE: add -g to the end of the following line if we want debug symbols for openssl
SSLDIR=$(openssl version -d | cut -d: -f2 | tr -d [:space:]\")
./config -fPIC --prefix=/opt/local/openssl --openssldir=${SSLDIR} zlib shared
make -j$(nproc) >> make_output.txt 2>&1
make install >> make_output.txt 2>&1
cd ..
rm -f openssl-${OPENSSL_VER}.tar.gz
rm -rf openssl-${OPENSSL_VER}
LD_LIBRARY_PATH=${LD_LIBRARY_PATH:-}:/opt/local/openssl/lib /opt/local/openssl/bin/openssl version -a

cd /tmp
wget https://libarchive.org/downloads/libarchive-3.4.1.tar.gz
tar xzf libarchive-3.4.1.tar.gz
cd libarchive-3.4.1
mkdir _bld && cd _bld
cmake -DCMAKE_BUILD_TYPE=Release ..
make -j$(nproc) >> make_output.txt 2>&1
make install >> make_output.txt 2>&1
cd ../..
rm -f libarchive-3.4.1.tar.gz
rm -rf libarchive-3.4.1

cd /tmp
wget https://github.com/protocolbuffers/protobuf/releases/download/v3.10.1/protobuf-all-3.10.1.tar.gz
tar xf protobuf-all-3.10.1.tar.gz
cd protobuf-3.10.1
./autogen.sh
./configure
make -j$(nproc) >> make_output.txt 2>&1
make install >> make_output.txt 2>&1
ldconfig
cd ..
rm -f protobuf-all-3.10.1.tar.gz
rm -rf protobuf-3.10.1

cd /tmp
wget https://c-ares.haxx.se/download/c-ares-1.15.0.tar.gz
tar xf c-ares-1.15.0.tar.gz
cd c-ares-1.15.0
mkdir _bld && cd _bld
cmake \
  -DHAVE_LIBNSL=OFF \
  -DCMAKE_BUILD_TYPE=Release \
  -DCARES_STATIC=ON \
  -DCARES_SHARED=OFF \
  -DCARES_INSTALL=ON \
  -DCARES_STATIC_PIC=ON \
  -DCARES_BUILD_TOOLS=OFF \
  -DCARES_BUILD_TESTS=OFF \
  -DCARES_BUILD_CONTAINER_TESTS=OFF \
  ..
make -j$(nproc) >> make_output.txt 2>&1
make install >> make_output.txt 2>&1
cd ../..
rm -f c-ares-1.15.0.tar.gz
rm -rf c-ares-1.15.0

cd /tmp
wget https://github.com/grpc/grpc/archive/v1.25.0.tar.gz
tar xf v1.25.0.tar.gz
cd grpc-1.25.0
mkdir _bld && cd _bld
cmake \
  -DCMAKE_BUILD_TYPE=Release \
  -DBUILD_SHARED_LIBS=OFF \
  -DgRPC_ZLIB_PROVIDER=package \
  -DgRPC_CARES_PROVIDER=package \
  -DgRPC_SSL_PROVIDER=package \
  -DgRPC_PROTOBUF_PROVIDER=package \
  -DProtobuf_USE_STATIC_LIBS=ON \
  ..
make -j$(nproc) >> make_output.txt 2>&1
make install >> make_output.txt 2>&1
cd ../..
rm -f xf v1.25.0.tar.gz
rm -rf grpc-1.25.0

if [ "${CI_USE}" = true ] ; then

    build_boost "1.71.0" false

    cd /tmp
    wget https://github.com/doxygen/doxygen/archive/Release_1_8_16.tar.gz
    tar xf Release_1_8_16.tar.gz
    cd doxygen-Release_1_8_16
    mkdir build
    cd build
    cmake -G "Unix Makefiles" ..
    make -j$(nproc) >> make_output.txt 2>&1
    make install >> make_output.txt 2>&1
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
    wget https://github.com/ccache/ccache/releases/download/v3.7.6/ccache-3.7.6.tar.gz
    tar xf ccache-3.7.6.tar.gz
    cd ccache-3.7.6
    ./configure --prefix=/usr/local
    make >> make_output.txt 2>&1
    make install >> make_output.txt 2>&1
    cd ..
    rm -f ccache-3.7.6.tar.gz
    rm -rf ccache-3.7.6

    pip install requests
    pip install https://github.com/codecov/codecov-python/archive/master.zip
fi
