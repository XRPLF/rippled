#!/usr/bin/env bash

function error {
    echo $1
    exit 1
}

cd /opt/rippled_bld/pkg/rippled
export RIPPLED_VERSION=$(egrep -i -o "\b(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(-[0-9a-z\-]+(\.[0-9a-z\-]+)*)?(\+[0-9a-z\-]+(\.[0-9a-z\-]+)*)?\b" src/ripple/protocol/impl/BuildInfo.cpp)

: ${PKG_OUTDIR:=/opt/rippled_bld/pkg/out}
export PKG_OUTDIR
if [ ! -d ${PKG_OUTDIR} ]; then
    error "${PKG_OUTDIR} is not mounted"
fi

if [ -x ${OPENSSL_ROOT}/bin/openssl ]; then
    LD_LIBRARY_PATH=${LD_LIBRARY_PATH}:${OPENSSL_ROOT}/lib ${OPENSSL_ROOT}/bin/openssl version -a
fi

