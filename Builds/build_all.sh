#!/usr/bin/env bash

num_procs=$(lscpu -p | grep -v '^#' | sort -u -t, -k 2,4 | wc -l) # number of physical cores

cd ..
./Builds/Test.py -a -c -- -j${num_procs}
