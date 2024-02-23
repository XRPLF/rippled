#!/usr/bin/env bash
set -ex

source /etc/os-release

yum -y upgrade
yum -y update
yum -y install epel-release centos-release-scl
yum -y install \
    wget curl time gcc-c++ yum-utils autoconf automake pkgconfig libtool \
    libstdc++-static rpm-build gnupg which make cmake \
    devtoolset-11 devtoolset-11-gdb devtoolset-11-binutils devtoolset-11-libstdc++-devel \
    devtoolset-11-libasan-devel devtoolset-11-libtsan-devel devtoolset-11-libubsan-devel devtoolset-11-liblsan-devel \
    flex flex-devel bison bison-devel parallel \
    ncurses ncurses-devel ncurses-libs graphviz graphviz-devel \
    lzip p7zip bzip2 bzip2-devel lzma-sdk lzma-sdk-devel xz-devel \
    zlib zlib-devel zlib-static texinfo openssl openssl-static \
    jemalloc jemalloc-devel \
    libicu-devel htop \
    rh-python38 \
    ninja-build git svn \
    swig perl-Digest-MD5
