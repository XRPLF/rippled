#!/bin/bash

cd /RIPPLED
if [ ! -d /RIPPLED/node_modules ]; then
    npm install    
fi

build/rippled --unittest
npm test
