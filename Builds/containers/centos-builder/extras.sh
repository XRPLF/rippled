#!/usr/bin/env bash
set -ex

if [ "${CI_USE}" = true ] ; then
    cd /tmp
    wget https://ftp.gnu.org/gnu/gdb/gdb-8.3.1.tar.xz
    tar xf gdb-8.3.1.tar.xz
    cd gdb-8.3
    ./configure CFLAGS="-w -O2" CXXFLAGS="-std=gnu++11 -g -O2 -w" --prefix=/opt/local/gdb-8.3
    make -j$(nproc)
    make install
    ln -s /opt/local/gdb-8.3 /opt/local/gdb
    cd ..
    rm -f gdb-8.3.tar.xz
    rm -rf gdb-8.3

    # clang from source
    cd /tmp
    git clone https://github.com/llvm/llvm-project.git
    cd llvm-project
    git checkout llvmorg-9.0.0
    INSTALL=/opt/llvm-9/
    mkdir mybuilddir && cd mybuilddir
    # TODO figure out necessary options
    cmake ../llvm -G Ninja \
      -DCMAKE_BUILD_TYPE=Release \
      -DLLVM_ENABLE_PROJECTS='clang;clang-tools-extra;libcxx;libcxxabi;lldb;compiler-rt;lld;polly' \
      -DCMAKE_INSTALL_PREFIX=${INSTALL} \
      -DLLVM_LIBDIR_SUFFIX=64
    cmake --build . --parallel --target install
    cd /tmp
    rm -rf llvm-project
fi
