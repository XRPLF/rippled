#!/bin/sh
# Assumptions:
# 1) BOOST_ROOT and BOOST_URL are already defined,
# and contain valid values.
# 2) The last namepart of BOOST_ROOT matches the
# folder name internal to boost's .tar.gz
set -e
if [ ! -d "$BOOST_ROOT/lib" ]
then
  wget $BOOST_URL -O /tmp/boost.tar.gz
  cd `dirname $BOOST_ROOT`
  tar xzf /tmp/boost.tar.gz
  cd $BOOST_ROOT && \
    ./bootstrap.sh --prefix=$BOOST_ROOT && \
      ./b2 -d1 && ./b2 -d0 install
else
  echo "Using cached boost at $BOOST_ROOT"
fi

