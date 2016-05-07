#!/bin/bash -u
set -e
gdb --silent \
    --batch \
    --return-child-result \
    -ex="set print thread-events off" \
    -ex=run \
    -ex="thread apply all bt full" \
    --args $@
