#!/bin/bash

# Remove all the compiled .pyc files at or below this directory.

find . -name \*.pyc | xargs rm
