#!/bin/sh

cd /RIPPLED
NO_STRIP_BUILD_OPTS=`echo "$BUILD_OPTS" | sed -e "s/strip//g"`
scons $NO_STRIP_BUILD_OPTS
if [ "$NO_STRIP_BUILD_OPTS" != "$BUILD_OPTS" ]; then
    echo "Strip final build ..."
    strip build/rippled
fi
