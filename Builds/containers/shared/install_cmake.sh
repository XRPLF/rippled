#!/usr/bin/env bash
set -ex

cd /tmp
CM_INSTALLER=cmake-3.13.2-Linux-x86_64.sh
CM_VER_DIR=/opt/local/cmake-3.13
wget https://cmake.org/files/v3.13/$CM_INSTALLER
chmod a+x $CM_INSTALLER
mkdir -p $CM_VER_DIR
ln -s $CM_VER_DIR /opt/local/cmake
./$CM_INSTALLER --prefix=$CM_VER_DIR --exclude-subdir
rm -f /tmp/$CM_INSTALLER

export PATH="/opt/local/cmake/bin:${PATH}"


