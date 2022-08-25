#!/usr/bin/env bash
set -ex

source /etc/os-release

yum -y upgrade
yum -y update
yum -y install epel-release centos-release-scl
yum -y install \
    wget curl time gcc-c++ time yum-utils autoconf automake pkgconfig libtool \
    libstdc++-static rpm-build gnupg which make cmake \
    devtoolset-11 devtoolset-11-gdb devtoolset-11-binutils devtoolset-11-libstdc++-devel \
    devtoolset-11-libasan-devel devtoolset-11-libtsan-devel devtoolset-11-libubsan-devel devtoolset-11-liblsan-devel \
    flex flex-devel bison bison-devel parallel \
    ncurses ncurses-devel ncurses-libs graphviz graphviz-devel \
    lzip p7zip bzip2 bzip2-devel lzma-sdk lzma-sdk-devel xz-devel \
    zlib zlib-devel zlib-static texinfo openssl openssl-static \
    jemalloc jemalloc-devel \
    libicu-devel htop \
    rh-python35-python \
    python-devel rh-python35-python-devel \
    rh-python35 \
    ninja-build git svn \
    swig perl-Digest-MD5 python2-pip

## Do we need this?
# if [ "${CI_USE}" = true ] ; then
#     # TODO need permanent link
#     yum -y install ftp://ftp.pbone.net/mirror/archive.fedoraproject.org/fedora-secondary/updates/26/i386/Packages/p/python2-six-1.10.0-9.fc26.noarch.rpm

#     yum -y install \
#         llvm-toolset-7 llvm-toolset-7-runtime llvm-toolset-7-build llvm-toolset-7-clang \
#         llvm-toolset-7-clang-analyzer llvm-toolset-7-clang-devel llvm-toolset-7-clang-libs \
#         llvm-toolset-7-clang-tools-extra llvm-toolset-7-compiler-rt llvm-toolset-7-lldb \
#         llvm-toolset-7-lldb-devel llvm-toolset-7-python-lldb

# fi
