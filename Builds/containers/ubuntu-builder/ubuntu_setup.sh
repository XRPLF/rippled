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
apt-get -y install python3-pip
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
for v in 12 14; do
    apt-get -y install \
        clang-$v libclang-common-$v-dev libclang-$v-dev libllvm$v llvm-$v \
        llvm-$v-dev llvm-$v-runtime clang-format-$v python3-clang-$v \
        lld-$v libfuzzer-$v-dev libc++-$v-dev python-is-python3
    update-alternatives --install \
        /usr/bin/clang clang /usr/bin/clang-$v 40 \
        --slave /usr/bin/clang++ clang++ /usr/bin/clang++-$v \
        --slave /usr/bin/llvm-profdata llvm-profdata /usr/bin/llvm-profdata-$v \
        --slave /usr/bin/asan-symbolize asan-symbolize /usr/bin/asan_symbolize-$v \
        --slave /usr/bin/llvm-symbolizer llvm-symbolizer /usr/bin/llvm-symbolizer-$v \
        --slave /usr/bin/clang-format clang-format /usr/bin/clang-format-$v \
        --slave /usr/bin/llvm-ar llvm-ar /usr/bin/llvm-ar-$v \
        --slave /usr/bin/llvm-cov llvm-cov /usr/bin/llvm-cov-$v \
        --slave /usr/bin/llvm-nm llvm-nm /usr/bin/llvm-nm-$v
    done
fi

pip install "conan<2" && \
    conan profile new default --detect && \
    conan profile update settings.compiler.cppstd=20 default && \
    conan profile update settings.compiler.libcxx=libstdc++11 default

apt-get -y autoremove
