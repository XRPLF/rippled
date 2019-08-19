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
fi
add-apt-repository -y ppa:ubuntu-toolchain-r/test
apt-get -y clean
apt-get -y update

apt-get -y --fix-missing install \
    make cmake ninja-build \
    protobuf-compiler libprotobuf-dev openssl libssl-dev \
    liblzma-dev libbz2-dev zlib1g-dev \
    libjemalloc-dev \
    python-pip \
    gdb gdbserver \
    libstdc++6 \
    flex bison parallel \
    libicu-dev texinfo \
    java-common javacc \
    dpkg-dev debhelper devscripts fakeroot \
    debmake git-buildpackage dh-make gitpkg debsums gnupg \
    dh-buildinfo dh-make dh-systemd

apt-get -y install gcc-7 g++-7
update-alternatives --install \
    /usr/bin/gcc gcc /usr/bin/gcc-7 40 \
    --slave /usr/bin/g++ g++ /usr/bin/g++-7 \
    --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-7 \
    --slave /usr/bin/gcc-nm gcc-nm /usr/bin/gcc-nm-7 \
    --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-7 \
    --slave /usr/bin/gcov gcov /usr/bin/gcov-7 \
    --slave /usr/bin/gcov-tool gcov-tool /usr/bin/gcov-dump-7 \
    --slave /usr/bin/gcov-dump gcov-dump /usr/bin/gcov-tool-7

apt-get -y install gcc-8 g++-8
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

if [ "${CI_USE}" = true ] ; then
    apt-get -y install gcc-6 g++-6
    update-alternatives --install \
        /usr/bin/gcc gcc /usr/bin/gcc-6 10 \
        --slave /usr/bin/g++ g++ /usr/bin/g++-6 \
        --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-6 \
        --slave /usr/bin/gcc-nm gcc-nm /usr/bin/gcc-nm-6 \
        --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-6 \
        --slave /usr/bin/gcov gcov /usr/bin/gcov-6 \
        --slave /usr/bin/gcov-tool gcov-tool /usr/bin/gcov-dump-6 \
        --slave /usr/bin/gcov-dump gcov-dump /usr/bin/gcov-tool-6

    apt-get -y install gcc-9 g++-9
    update-alternatives --install \
        /usr/bin/gcc gcc /usr/bin/gcc-9 15 \
        --slave /usr/bin/g++ g++ /usr/bin/g++-9 \
        --slave /usr/bin/gcc-ar gcc-ar /usr/bin/gcc-ar-9 \
        --slave /usr/bin/gcc-nm gcc-nm /usr/bin/gcc-nm-9 \
        --slave /usr/bin/gcc-ranlib gcc-ranlib /usr/bin/gcc-ranlib-9 \
        --slave /usr/bin/gcov gcov /usr/bin/gcov-9 \
        --slave /usr/bin/gcov-tool gcov-tool /usr/bin/gcov-dump-9 \
        --slave /usr/bin/gcov-dump gcov-dump /usr/bin/gcov-tool-9
fi

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
deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-7 main
deb-src http://apt.llvm.org/bionic/ llvm-toolchain-bionic-7 main
deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-8 main
deb-src http://apt.llvm.org/bionic/ llvm-toolchain-bionic-8 main
deb http://apt.llvm.org/bionic/ llvm-toolchain-bionic-9 main
deb-src http://apt.llvm.org/bionic/ llvm-toolchain-bionic-9 main
EOF
elif [[ ${VERSION_ID} =~ ^16\. ]] ; then
    cat << EOF > /etc/apt/sources.list.d/llvm.list
deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial main
deb-src http://apt.llvm.org/xenial/ llvm-toolchain-xenial main
deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-7 main
deb-src http://apt.llvm.org/xenial/ llvm-toolchain-xenial-7 main
deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-8 main
deb-src http://apt.llvm.org/xenial/ llvm-toolchain-xenial-8 main
deb http://apt.llvm.org/xenial/ llvm-toolchain-xenial-9 main
deb-src http://apt.llvm.org/xenial/ llvm-toolchain-xenial-9 main
EOF
fi
apt-get -y update

apt-get -y install \
    clang-7 libclang-common-7-dev libclang-7-dev libllvm7 llvm-7 \
    llvm-7-dev llvm-7-runtime clang-format-7 python-clang-7 \
    lld-7 libfuzzer-7-dev libc++-7-dev
update-alternatives --install \
    /usr/bin/clang clang /usr/bin/clang-7 40 \
    --slave /usr/bin/clang++ clang++ /usr/bin/clang++-7 \
    --slave /usr/bin/llvm-profdata llvm-profdata /usr/bin/llvm-profdata-7 \
    --slave /usr/bin/asan-symbolize asan-symbolize /usr/bin/asan_symbolize-7 \
    --slave /usr/bin/llvm-symbolizer llvm-symbolizer /usr/bin/llvm-symbolizer-7 \
    --slave /usr/bin/clang-format clang-format /usr/bin/clang-format-7 \
    --slave /usr/bin/llvm-ar llvm-ar /usr/bin/llvm-ar-7 \
    --slave /usr/bin/llvm-cov llvm-cov /usr/bin/llvm-cov-7 \
    --slave /usr/bin/llvm-nm llvm-nm /usr/bin/llvm-nm-7
apt-get -y install \
    clang-8 libclang-common-8-dev libclang-8-dev libllvm8 llvm-8 \
    llvm-8-dev llvm-8-runtime clang-format-8 python-clang-8 \
    lld-8 libfuzzer-8-dev libc++-8-dev
update-alternatives --install \
    /usr/bin/clang clang /usr/bin/clang-8 20 \
    --slave /usr/bin/clang++ clang++ /usr/bin/clang++-8 \
    --slave /usr/bin/llvm-profdata llvm-profdata /usr/bin/llvm-profdata-8 \
    --slave /usr/bin/asan-symbolize asan-symbolize /usr/bin/asan_symbolize-8 \
    --slave /usr/bin/llvm-symbolizer llvm-symbolizer /usr/bin/llvm-symbolizer-8 \
    --slave /usr/bin/clang-format clang-format /usr/bin/clang-format-8 \
    --slave /usr/bin/llvm-ar llvm-ar /usr/bin/llvm-ar-8 \
    --slave /usr/bin/llvm-cov llvm-cov /usr/bin/llvm-cov-8 \
    --slave /usr/bin/llvm-nm llvm-nm /usr/bin/llvm-nm-8
update-alternatives --auto clang

if [ "${CI_USE}" = true ] ; then
    apt-get -y install \
        clang-5.0 libclang-common-5.0-dev libclang-5.0-dev libllvm5.0 llvm-5.0 \
        llvm-5.0-dev llvm-5.0-runtime clang-format-5.0 python-clang-5.0 \
        lld-5.0 libfuzzer-5.0-dev
    update-alternatives --install \
        /usr/bin/clang clang /usr/bin/clang-5.0 10 \
        --slave /usr/bin/clang++ clang++ /usr/bin/clang++-5.0 \
        --slave /usr/bin/llvm-profdata llvm-profdata /usr/bin/llvm-profdata-5.0 \
        --slave /usr/bin/asan-symbolize asan-symbolize /usr/bin/asan_symbolize-5.0 \
        --slave /usr/bin/llvm-symbolizer llvm-symbolizer /usr/bin/llvm-symbolizer-5.0 \
        --slave /usr/bin/clang-format clang-format /usr/bin/clang-format-5.0 \
        --slave /usr/bin/llvm-ar llvm-ar /usr/bin/llvm-ar-5.0 \
        --slave /usr/bin/llvm-cov llvm-cov /usr/bin/llvm-cov-5.0 \
        --slave /usr/bin/llvm-nm llvm-nm /usr/bin/llvm-nm-5.0

    apt-get -y install \
        clang-6.0 libclang-common-6.0-dev libclang-6.0-dev libllvm6.0 llvm-6.0 \
        llvm-6.0-dev llvm-6.0-runtime clang-format-6.0 python-clang-6.0 \
        lld-6.0 libfuzzer-6.0-dev
    update-alternatives --install \
        /usr/bin/clang clang /usr/bin/clang-6.0 12 \
        --slave /usr/bin/clang++ clang++ /usr/bin/clang++-6.0 \
        --slave /usr/bin/llvm-profdata llvm-profdata /usr/bin/llvm-profdata-6.0 \
        --slave /usr/bin/asan-symbolize asan-symbolize /usr/bin/asan_symbolize-6.0 \
        --slave /usr/bin/llvm-symbolizer llvm-symbolizer /usr/bin/llvm-symbolizer-6.0 \
        --slave /usr/bin/clang-format clang-format /usr/bin/clang-format-6.0 \
        --slave /usr/bin/llvm-ar llvm-ar /usr/bin/llvm-ar-6.0 \
        --slave /usr/bin/llvm-cov llvm-cov /usr/bin/llvm-cov-6.0 \
        --slave /usr/bin/llvm-nm llvm-nm /usr/bin/llvm-nm-6.0

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

