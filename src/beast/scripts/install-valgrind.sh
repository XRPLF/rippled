#!/usr/bin/env bash
# Assumptions:
# 1) VALGRIND_ROOT is already defined, and contains a valid values
set -eu
if [ ! -d "$VALGRIND_ROOT/bin" ]
then
  # These are specified in the addons/apt section of .travis.yml
  # sudo apt-get install subversion automake autotools-dev libc6-dbg
  export PATH=$PATH:$VALGRIND_ROOT/bin
  svn co svn://svn.valgrind.org/valgrind/trunk valgrind-co
  cd valgrind-co
  ./autogen.sh
  ./configure --prefix=$VALGRIND_ROOT
  make
  make install
  # test it
  valgrind ls -l
else
  echo "Using cached valgrind at $VALGRIND_ROOT"
fi
