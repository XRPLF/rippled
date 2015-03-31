#!/usr/bin/env bash
cd ../..
for file in $(ls -a Builds/EclipseCDT|egrep "^\.\w+"); do rm -rf $file; done;
for file in $(ls -a Builds/EclipseCDT|egrep "^\.\w+"); do ln -s Builds/EclipseCDT/$file $file; done;
