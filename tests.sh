#!/bin/bash

# Runs all the tests in bin/

for f in bin/*.test
do
{
    echo -e "\033[94m$f\033[0m"
    $f
}
done
