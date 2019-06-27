#!/usr/bin/env bash
set -ex

if [ "${CI_USE}" = true ] ; then
    cd /tmp
    wget https://ftp.gnu.org/gnu/gdb/gdb-8.2.tar.xz
    tar xf gdb-8.2.tar.xz
    cd gdb-8.2
    ./configure CFLAGS="-w -O2" CXXFLAGS="-std=gnu++11 -g -O2 -w" --prefix=/opt/local/gdb-8.2
    make -j$(nproc)
    make install
    ln -s /opt/local/gdb-8.2 /opt/local/gdb
    cd ..
    rm -f gdb-8.2.tar.xz
    rm -rf gdb-8.2

    # clang from source
    RELEASE=tags/RELEASE_701/final
    INSTALL=/opt/llvm-7.0.1/
    mkdir -p /tmp/clang-src
    cd /tmp/clang-src
    TOPDIR=`pwd`
    svn co -q http://llvm.org/svn/llvm-project/llvm/${RELEASE} llvm
    cd ${TOPDIR}/llvm/tools
    svn co -q http://llvm.org/svn/llvm-project/cfe/${RELEASE} clang
    cd ${TOPDIR}/llvm/tools/clang/tools
    svn co -q http://llvm.org/svn/llvm-project/clang-tools-extra/${RELEASE} extra
    cd ${TOPDIR}/llvm/tools
    svn co -q http://llvm.org/svn/llvm-project/lld/${RELEASE} lld
    cd ${TOPDIR}/llvm/tools
    svn co -q http://llvm.org/svn/llvm-project/polly/${RELEASE} polly
    cd ${TOPDIR}/llvm/projects
    svn co -q http://llvm.org/svn/llvm-project/compiler-rt/${RELEASE} compiler-rt
    cd ${TOPDIR}/llvm/projects
    svn co -q http://llvm.org/svn/llvm-project/openmp/${RELEASE} openmp
    cd ${TOPDIR}/llvm/projects
    svn co -q http://llvm.org/svn/llvm-project/libcxx/${RELEASE} libcxx
    svn co -q http://llvm.org/svn/llvm-project/libcxxabi/${RELEASE} libcxxabi
    cd ${TOPDIR}/llvm/projects
    ## config/build
    cd ${TOPDIR}
    mkdir mybuilddir && cd mybuilddir
    cmake ../llvm -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DCMAKE_INSTALL_PREFIX=${INSTALL} \
      -DLLVM_LIBDIR_SUFFIX=64 \
      -DLLVM_ENABLE_EH=ON \
      -DLLVM_ENABLE_RTTI=ON
    cmake --build . --parallel --target install
    cd /tmp
    rm -rf clang-src
fi
