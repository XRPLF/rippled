#!/bin/bash

mocha --ui tdd --reporter xunit --timeout 10000 test/*-test.js

