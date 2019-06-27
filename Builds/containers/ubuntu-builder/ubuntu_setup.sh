#!/usr/bin/env bash
set -ex

source /etc/os-release

if [[ ${VERSION_ID} =~ ^18\. || ${VERSION_ID} =~ ^16\. ]] ; then
    echo "setup for ${PRETTY_NAME}"
else
    echo "${VERSION} not supported"
    exit 1
fi

export DEBIAN_FRONTEND="noninteractive"
echo "Acquire::Retries 3;" > /etc/apt/apt.conf.d/80-retries
echo "Acquire::http::Pipeline-Depth 0;" >> /etc/apt/apt.conf.d/80-retries
echo "Acquire::http::No-Cache true;" >> /etc/apt/apt.conf.d/80-retries
echo "Acquire::BrokenProxy    true;" >> /etc/apt/apt.conf.d/80-retries
apt-get update -o Acquire::CompressionTypes::Order::=gz

apt-get -y update
apt-get -y install apt-utils
apt-get -y install software-properties-common wget
apt-get -y upgrade
if [[ ${VERSION_ID} =~ ^18\. ]] ; then
    apt-add-repository -y multiverse
    apt-add-repository -y universe
elif [[ ${VERSION_ID} =~ ^16\. ]] ; then
    add-apt-repository -y ppa:ubuntu-toolchain-r/test
fi
apt-get -y clean
apt-get -y update

apt-get -y --fix-missing install  \
    make cmake ninja-build ccache \
    protobuf-compiler libprotobuf-dev openssl libssl-dev \
    liblzma-dev libbz2-dev zlib1g-dev \
    libjemalloc-dev \
    python-pip \
    gdb gdbserver \
    libstdc++6 \
    flex bison \
    libicu-dev texinfo \
    java-common javacc \
    gcc-7 g++-7 \
    gcc-8 g++-8 \
    dpkg-dev debhelper devscripts fakeroot \
    debmake git-buildpackage dh-make gitpkg debsums gnupg \
    dh-buildinfo dh-make dh-systemd

update-alternatives --install \
    /usr/bin/gcc gcc /usr/bin/gcc-7 40 \
    --slave /usr/bin/g++ g++ /usr/bin/g++-7 \
    --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-7 \
    --slave /usr/bin/gcc-nm gcc-nm /usr/bin/gcc-nm-7 \
    --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-7 \
    --slave /usr/bin/gcov gcov /usr/bin/gcov-7 \
    --slave /usr/bin/gcov-tool gcov-tool /usr/bin/gcov-dump-7 \
    --slave /usr/bin/gcov-dump gcov-dump /usr/bin/gcov-tool-7
update-alternatives --install \
    /usr/bin/gcc gcc /usr/bin/gcc-8 20 \
    --slave /usr/bin/g++ g++ /usr/bin/g++-8 \
    --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-8 \
    --slave /usr/bin/gcc-nm gcc-nm /usr/bin/gcc-nm-8 \
    --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-8 \
    --slave /usr/bin/gcov gcov /usr/bin/gcov-8 \
    --slave /usr/bin/gcov-tool gcov-tool /usr/bin/gcov-dump-8 \
    --slave /usr/bin/gcov-dump gcov-dump /usr/bin/gcov-tool-8
update-alternatives --auto gcc

update-alternatives --install /usr/bin/cpp cpp /usr/bin/cpp-7 40
update-alternatives --install /usr/bin/cpp cpp /usr/bin/cpp-8 20
update-alternatives --auto cpp

if [[ ${VERSION_ID} =~ ^18\. ]] ; then
    apt-get -y install binutils
elif [[ ${VERSION_ID} =~ ^16\. ]] ; then
    apt-get -y install python-software-properties  binutils-gold
fi

wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -
if [[ ${VERSION_ID} =~ ^18\. ]] ; then
    cat << EOF > /etc/apt/sources.list.d/llvm.list
deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic main
deb-src http://apt.llvm.org/bionic/ llvm-toolchain-bionic main
deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-6.0 main
deb-src http://apt.llvm.org/bionic/ llvm-toolchain-bionic-6.0 main
deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-7 main
deb-src http://apt.llvm.org/bionic/ llvm-toolchain-bionic-7 main
EOF
elif [[ ${VERSION_ID} =~ ^16\. ]] ; then
    cat << EOF > /etc/apt/sources.list.d/llvm.list
deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial main
deb-src http://apt.llvm.org/xenial/ llvm-toolchain-xenial main
deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-6.0 main
deb-src http://apt.llvm.org/xenial/ llvm-toolchain-xenial-6.0 main
deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-7 main
deb-src http://apt.llvm.org/xenial/ llvm-toolchain-xenial-7 main
EOF
fi
apt-get -y update

apt-get -y install  \
    clang-7 libclang-common-7-dev libclang-7-dev libllvm7 lldb-7 llvm-7 \
    llvm-7-dev llvm-7-runtime clang-format-7 python-clang-7 python-lldb-7 \
    liblldb-7-dev lld-7 libfuzzer-7-dev libc++-7-dev
update-alternatives --install \
  /usr/bin/clang clang /usr/bin/clang-7 40 \
  --slave /usr/bin/clang++ clang++ /usr/bin/clang++-7 \
  --slave /usr/bin/llvm-profdata llvm-profdata /usr/bin/llvm-profdata-7 \
  --slave /usr/bin/asan-symbolize asan-symbolize /usr/bin/asan_symbolize-7 \
  --slave /usr/bin/clang-format clang-format /usr/bin/clang-format-7 \
  --slave /usr/bin/lldb lldb /usr/bin/lldb-7 \
  --slave /usr/bin/lldb-server lldb-server /usr/bin/lldb-server-7 \
  --slave /usr/bin/llvm-ar llvm-ar /usr/bin/llvm-ar-7 \
  --slave /usr/bin/llvm-cov llvm-cov /usr/bin/llvm-cov-7 \
  --slave /usr/bin/llvm-nm llvm-nm /usr/bin/llvm-nm-7
update-alternatives --auto clang

apt-get -y autoremove

