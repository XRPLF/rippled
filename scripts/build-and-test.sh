#!/bin/bash -u
# We use set -e and bash with -u to bail on first non zero exit code of any
# processes launched or upon any unbound variable
set -e
__dirname=$( cd "$( dirname "${BASH_SOURCE[0]}" )" && pwd )
echo "using toolset: $CC"
echo "using variant: $VARIANT"

$BOOST_ROOT/bjam toolset=$CC variant=$VARIANT
