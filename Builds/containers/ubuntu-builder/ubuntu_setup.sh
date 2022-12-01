#!/usr/bin/env bash
set -ex

source /etc/os-release

if [[ ${VERSION_ID} =~ ^20\. || ${VERSION_ID} =~ ^22\. ]] ; then
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
apt-get -y install software-properties-common wget curl ca-certificates
apt-get -y upgrade

add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get -y clean
apt-get -y update

apt-get -y --fix-missing install \
    make cmake ninja-build autoconf automake libtool pkg-config libtool \
    openssl libssl-dev \
    liblzma-dev libbz2-dev zlib1g-dev \
    libjemalloc-dev \
    gdb gdbserver \
    libstdc++6 \
    flex bison parallel \
    libicu-dev texinfo \
    java-common javacc \
    dpkg-dev debhelper devscripts fakeroot \
    debmake git-buildpackage dh-make gitpkg debsums gnupg \
    dh-buildinfo dh-make  \
    apt-transport-https

if [[ ${VERSION_ID} =~ ^20\. ]] ; then
apt-get install -y \
    dh-systemd
fi

apt-get -y install gcc-11 g++-11
update-alternatives --install \
    /usr/bin/gcc gcc /usr/bin/gcc-11 20 \
    --slave /usr/bin/g++ g++ /usr/bin/g++-11 \
    --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-11 \
    --slave /usr/bin/gcc-nm gcc-nm /usr/bin/gcc-nm-11 \
    --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-11 \
    --slave /usr/bin/gcov gcov /usr/bin/gcov-11 \
    --slave /usr/bin/gcov-tool gcov-tool /usr/bin/gcov-dump-11 \
    --slave /usr/bin/gcov-dump gcov-dump /usr/bin/gcov-tool-11
update-alternatives --auto gcc

update-alternatives --install /usr/bin/cpp cpp /usr/bin/cpp-11 20
update-alternatives --auto cpp

if [ "${CI_USE}" = true ] ; then
    apt-get -y install gcc-11 g++-11
    update-alternatives --install \
        /usr/bin/gcc gcc /usr/bin/gcc-11 15 \
        --slave /usr/bin/g++ g++ /usr/bin/g++-11 \
        --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-11 \
        --slave /usr/bin/gcc-nm gcc-nm /usr/bin/gcc-nm-11 \
        --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-11 \
        --slave /usr/bin/gcov gcov /usr/bin/gcov-11 \
        --slave /usr/bin/gcov-tool gcov-tool /usr/bin/gcov-dump-11 \
        --slave /usr/bin/gcov-dump gcov-dump /usr/bin/gcov-tool-11
fi

wget -O - https://apt.llvm.org/llvm-snapshot.gpg.key | apt-key add -

if [[ ${VERSION_ID} =~ ^20\. ]] ; then
    cat << EOF > /etc/apt/sources.list.d/llvm.list
deb http://apt.llvm.org/focal/ llvm-toolchain-focal main
deb-src http://apt.llvm.org/focal/ llvm-toolchain-focal main
deb http://apt.llvm.org/focal/ llvm-toolchain-focal-13 main
deb-src http://apt.llvm.org/focal/ llvm-toolchain-focal-13 main
deb http://apt.llvm.org/focal/ llvm-toolchain-focal-14 main
deb-src http://apt.llvm.org/focal/ llvm-toolchain-focal-14 main
EOF
    apt-get -y install binutils clang-12
fi


apt-get -y update
if [[ ${VERSION_ID} =~ ^20\. ]] ; then

apt-get -y install \
    clang-12 libclang-common-12-dev libclang-12-dev libllvm12 llvm-12 \
    llvm-12-dev llvm-12-runtime clang-format-12 python3-clang-12 \
    lld-12 libfuzzer-12-dev libc++-12-dev python-is-python3
update-alternatives --install \
    /usr/bin/clang clang /usr/bin/clang-12 40 \
    --slave /usr/bin/clang++ clang++ /usr/bin/clang++-12 \
    --slave /usr/bin/llvm-profdata llvm-profdata /usr/bin/llvm-profdata-12 \
    --slave /usr/bin/asan-symbolize asan-symbolize /usr/bin/asan_symbolize-12 \
    --slave /usr/bin/llvm-symbolizer llvm-symbolizer /usr/bin/llvm-symbolizer-12 \
    --slave /usr/bin/clang-format clang-format /usr/bin/clang-format-12 \
    --slave /usr/bin/llvm-ar llvm-ar /usr/bin/llvm-ar-12 \
    --slave /usr/bin/llvm-cov llvm-cov /usr/bin/llvm-cov-12 \
    --slave /usr/bin/llvm-nm llvm-nm /usr/bin/llvm-nm-12

apt-get -y install \
    clang-14 libclang-common-14-dev libclang-14-dev libllvm14 llvm-14 \
    llvm-14-dev llvm-14-runtime clang-format-14 python3-clang-14 \
    lld-14 libfuzzer-14-dev libc++-14-dev

update-alternatives --install \
    /usr/bin/clang clang /usr/bin/clang-14 20 \
    --slave /usr/bin/clang++ clang++ /usr/bin/clang++-14 \
    --slave /usr/bin/llvm-profdata llvm-profdata /usr/bin/llvm-profdata-14 \
    --slave /usr/bin/asan-symbolize asan-symbolize /usr/bin/asan_symbolize-14 \
    --slave /usr/bin/llvm-symbolizer llvm-symbolizer /usr/bin/llvm-symbolizer-14 \
    --slave /usr/bin/clang-format clang-format /usr/bin/clang-format-14 \
    --slave /usr/bin/llvm-ar llvm-ar /usr/bin/llvm-ar-14 \
    --slave /usr/bin/llvm-cov llvm-cov /usr/bin/llvm-cov-14 \
    --slave /usr/bin/llvm-nm llvm-nm /usr/bin/llvm-nm-14
update-alternatives --auto clang
fi

if [ "${CI_USE}" = true ] ; then
    apt-get -y install \
        clang-9 libclang-common-9-dev libclang-9-dev libllvm9 llvm-9 \
        llvm-9-dev llvm-9-runtime clang-format-9 python-clang-9 \
        lld-9 libfuzzer-9-dev libc++-9-dev
    update-alternatives --install \
        /usr/bin/clang clang /usr/bin/clang-9 20 \
        --slave /usr/bin/clang++ clang++ /usr/bin/clang++-9 \
        --slave /usr/bin/llvm-profdata llvm-profdata /usr/bin/llvm-profdata-9 \
        --slave /usr/bin/asan-symbolize asan-symbolize /usr/bin/asan_symbolize-9 \
        --slave /usr/bin/llvm-symbolizer llvm-symbolizer /usr/bin/llvm-symbolizer-9 \
        --slave /usr/bin/clang-format clang-format /usr/bin/clang-format-9 \
        --slave /usr/bin/llvm-ar llvm-ar /usr/bin/llvm-ar-9 \
        --slave /usr/bin/llvm-cov llvm-cov /usr/bin/llvm-cov-9 \
        --slave /usr/bin/llvm-nm llvm-nm /usr/bin/llvm-nm-9

    # only install latest lldb
    apt-get -y install lldb-9 python-lldb-9 liblldb-9-dev
    update-alternatives --install \
        /usr/bin/lldb lldb /usr/bin/lldb-9 50 \
        --slave /usr/bin/lldb-server lldb-server /usr/bin/lldb-server-9 \
        --slave /usr/bin/lldb-argdumper lldb-argdumper /usr/bin/lldb-argdumper-9 \
        --slave /usr/bin/lldb-instr lldb-instr /usr/bin/lldb-instr-9 \
        --slave /usr/bin/lldb-mi lldb-mi /usr/bin/lldb-mi-9
    update-alternatives --auto clang
fi

apt-get -y autoremove
