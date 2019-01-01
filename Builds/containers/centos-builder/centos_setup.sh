#!/usr/bin/env bash
set -ex

source /etc/os-release

yum -y upgrade
yum -y update
yum -y install epel-release centos-release-scl
yum -y install \
    wget curl time gcc-c++ time yum-utils \
    libstdc++-static rpm-build gnupg which make cmake \
    devtoolset-4 devtoolset-4-gdb devtoolset-4-libasan-devel devtoolset-4-libtsan-devel devtoolset-4-libubsan-devel \
    devtoolset-6 devtoolset-6-gdb devtoolset-6-libasan-devel devtoolset-6-libtsan-devel devtoolset-6-libubsan-devel \
    devtoolset-7 devtoolset-7-gdb devtoolset-7-libasan-devel devtoolset-7-libtsan-devel devtoolset-7-libubsan-devel \
    llvm-toolset-7 llvm-toolset-7-runtime llvm-toolset-7-build llvm-toolset-7-clang \
    llvm-toolset-7-clang-analyzer llvm-toolset-7-clang-devel llvm-toolset-7-clang-libs \
    llvm-toolset-7-clang-tools-extra llvm-toolset-7-compiler-rt llvm-toolset-7-lldb \
    llvm-toolset-7-lldb-devel llvm-toolset-7-python-lldb \
    flex flex-devel bison bison-devel \
    ncurses ncurses-devel ncurses-libs graphviz graphviz-devel \
    lzip p7zip bzip2 \
    zlib zlib-devel zlib-static texinfo openssl-static \
    jemalloc jemalloc-devel \
    libicu-devel htop \
    python27-python rh-python35-python \
    python-devel python27-python-devel rh-python35-python-devel \
    python27 rh-python35 \
    ninja-build git svn \
    protobuf protobuf-static protobuf-c-devel \
    protobuf-compiler protobuf-devel \
    swig ccache perl-Digest-MD5 python2-pip

# TODO need permanent link
yum -y install ftp://ftp.pbone.net/mirror/archive.fedoraproject.org/fedora-secondary/updates/26/i386/Packages/p/python2-six-1.10.0-9.fc26.noarch.rpm

