#!/bin/sh
# $1: the subfolder name
basename -a src/ripple/$1/*.h src/ripple/$1/*.cpp src/ripple/$1/impl/*.h src/ripple/$1/impl/*.cpp | cut -d. -f1 | sort -u