#!/bin/bash -e
# Common definitions used by SOCI build scripts at travis-ci.org
#
# Copyright (c) 2013 Mateusz Loskot <mateusz@loskot.net>
#
if [[ "$TRAVIS" != "true" ]] ; then
	echo "Running this script makes no sense outside of travis-ci.org"
	exit 1
fi
#
# Environment
#
TCI_NUMTHREADS=2
if [[ -f /sys/devices/system/cpu/online ]]; then
	# Calculates 1.5 times physical threads
	TCI_NUMTHREADS=$(( ( $(cut -f 2 -d '-' /sys/devices/system/cpu/online) + 1 ) * 15 / 10  ))
fi
export ORACLE_HOME=/opt/instantclient_11_2
export LD_LIBRARY_PATH=${ORACLE_HOME}:${LD_LIBRARY_PATH}
#
# Functions
#
tmstamp()
{
    echo -n "[$(date '+%H:%M:%S')]" ;
}

run_make()
{
    [ $TCI_NUMTHREADS -gt 0 ] && make -j $TCI_NUMTHREADS ] || make 
}

run_test()
{
    ctest -V --output-on-failure .
}
