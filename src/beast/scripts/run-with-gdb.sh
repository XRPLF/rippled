#!/bin/bash -u
set -e
gdb --silent \
    --batch \
    --return-child-result \
    -ex=run \
    -ex="thread apply all bt full" \
    --args $@
