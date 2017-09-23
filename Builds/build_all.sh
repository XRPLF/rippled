#!/usr/bin/env bash

num_procs=$(lscpu -p | grep -v '^#' | sort -u -t, -k 2,4 | wc -l) # number of physical cores

path=$(cd $(dirname $0) && pwd)
cd $(dirname $path)
${path}/Test.py -a -c --test=TxQ -- -j${num_procs}
${path}/Test.py -a -c -k --test=TxQ --cmake -- -j${num_procs}
