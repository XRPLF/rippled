#!/bin/bash
sudo apt-get update -qq
sudo apt-get install -qq python-software-properties
sudo add-apt-repository -y ppa:ubuntu-toolchain-r/test
sudo add-apt-repository -y ppa:afrank/boost
sudo apt-get update -qq
sudo apt-get install -qq g++-4.8
sudo apt-get install -qq libboost1.57-all-dev
sudo apt-get install -qq mlocate
sudo updatedb
sudo locate libboost | grep /lib | grep -e ".a$"
sudo apt-get install -qq protobuf-compiler libprotobuf-dev libssl-dev exuberant-ctags
# We need gcc >= 4.8 for some c++11 features
sudo apt-get install -qq gcc-4.8
sudo update-alternatives --install /usr/bin/gcc gcc /usr/bin/gcc-4.8 40 --slave /usr/bin/g++ g++ /usr/bin/g++-4.8
sudo update-alternatives --install /usr/bin/gcov gcov /usr/bin/gcov-4.8 90
sudo update-alternatives --set gcc /usr/bin/gcc-4.8
sudo update-alternatives --set gcov /usr/bin/gcov-4.8
# Stuff is gold. Nuff said ;)
sudo apt-get -y install binutils-gold
# We can get a backtrace if the guy crashes
sudo apt-get -y install gdb
# What versions are we ACTUALLY running?
g++ -v
clang -v
# Avoid `spurious errors` caused by ~/.npm permission issues
# Does it already exist? Who owns? What permissions?
ls -lah ~/.npm || mkdir ~/.npm
# Make sure we own it
sudo chown -R $USER ~/.npm
# We use this so we can filter the subtrees from our coverage report
pip install --user https://github.com/sublimator/codecov-python/zipball/source-match
