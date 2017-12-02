#!/bin/bash
# Script performs non-interactive installation of Oracle XE on Linux
#
# Uses Oracle downloader and installer from https://github.com/cbandy/travis-oracle
#
# set -ex
source ${TRAVIS_BUILD_DIR}/scripts/travis/oracle.sh

# Install Oracle and travis-oracle requirements
sudo apt-get install -y libaio1 rpm

curl -s -o $HOME/.nvm/nvm.sh https://raw.githubusercontent.com/creationix/nvm/v0.31.0/nvm.sh
source $HOME/.nvm/nvm.sh
nvm install stable
node --version

# Install travis-oracle
wget 'https://github.com/cbandy/travis-oracle/archive/v2.0.3.tar.gz'
mkdir -p .travis/oracle
tar x -C .travis/oracle --strip-components=1 -f v2.0.3.tar.gz

# Download Oracle (do not use Travis CI secure environment!)
export ORACLE_LOGIN_pass="T$(echo $ORACLE_LOGIN_userid | rev)#2017"
bash .travis/oracle/download.sh

# Install Oracle
bash .travis/oracle/install.sh
