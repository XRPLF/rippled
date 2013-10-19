#!/bin/bash

# This script makes sure that every directly includable header
# file compiles stand-alone for all supported platforms.
#

for f in $1/*.h $1/*/*.h
do
{
	echo "Compilng '$f'"
g++ -xc++ - -c -o /dev/null <<EOF
#define BEAST_BEASTCONFIG_H_INCLUDED
#include "$f"
EOF
g++ -xc++ -std=c++11 - -c -o /dev/null <<EOF
#define BEAST_BEASTCONFIG_H_INCLUDED
#include "$f"
EOF
}
done

for f in $1/*/*.cpp
do
{
	echo "Compilng '$f'"
	g++ -xc++ -I$1/../config/ "$f" -c -o /dev/null
	g++ -xc++ -std=c++11 -I$1/../config/ "$f" -c -o /dev/null
}
done
