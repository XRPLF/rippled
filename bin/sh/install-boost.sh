#!/bin/sh
# Assumptions:
# 1) BOOST_ROOT and BOOST_URL are already defined,
# and contain valid values.
# 2) The last namepart of BOOST_ROOT matches the
# folder name internal to boost's .tar.gz
# When testing you can force a boost build by clearing travis caches:
# https://travis-ci.org/ripple/rippled/caches
set -e

if [ -x /usr/bin/time ] ; then
  : ${TIME:="Duration: %E"}
  export TIME
  time=/usr/bin/time
else
  time=
fi

if [ ! -d "$BOOST_ROOT/lib" ]
then
  wget $BOOST_URL -O /tmp/boost.tar.gz
  cd `dirname $BOOST_ROOT`
  rm -fr ${BOOST_ROOT}
  tar xzf /tmp/boost.tar.gz
  cd $BOOST_ROOT && \
    $time ./bootstrap.sh --prefix=$BOOST_ROOT && \
    $time ./b2 cxxflags="-std=c++14" -j$((2*${NUM_PROCESSORS:-2})) &&\
    $time ./b2 install
  
else
  echo "Using cached boost at $BOOST_ROOT"
fi

