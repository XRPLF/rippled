#!/usr/bin/env bash
# Assumptions:
# 1) BOOST_ROOT and BOOST_URL are already defined,
# and contain valid values.
# 2) The last namepart of BOOST_ROOT matches the
# folder name internal to boost's .tar.gz
# When testing you can force a boost build by clearing travis caches:
# https://travis-ci.org/ripple/rippled/caches
set -eu
#if [ ! -d "$BOOST_ROOT" ]
#then
  wget $BOOST_URL -O /tmp/boost.tar.gz
  cd `dirname $BOOST_ROOT`
  rm -fr ${BOOST_ROOT}
  tar xzf /tmp/boost.tar.gz

  params="define=_GLIBCXX_USE_CXX11_ABI=0 \
          address-model=$ADDRESS_MODEL --with-program_options \
          --with-system --with-coroutine --with-filesystem"
  cd $BOOST_ROOT && \
    ./bootstrap.sh --prefix=$BOOST_ROOT && \
    ./b2 -d1 $params && \
    ./b2 -d0 $params install
#else
#  echo "Using cached boost at $BOOST_ROOT"
#fi

