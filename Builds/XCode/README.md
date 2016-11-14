# macos Build Instructions

## Important

We don't recommend OS X for rippled production use at this time. Currently, the
Ubuntu platform has received the highest level of quality assurance and
testing.

## Prerequisites

You'll need OSX 10.8 or later

To clone the source code repository, create branches for inspection or
modification, build rippled using clang, and run the system tests you will need
these software components:

* [XCode](https://developer.apple.com/xcode/)
* [Homebrew](http://brew.sh/)
* [Git](http://git-scm.com/)
* [Scons](http://www.scons.org/)
* [Node.js](http://nodejs.org/download/)

## Install Software

### Install XCode

If not already installed on your system, download and install XCode using the
appstore or by using [this link](https://developer.apple.com/xcode/).

For more info, see "Step 1: Download and Install the Command Line Tools"
[here](http://www.moncefbelyamani.com/how-to-install-xcode-homebrew-git-rvm-ruby-on-mac)

The command line tools can be installed through the terminal with the command:

```
xcode-select --install
```

### Install Homebrew

> "[Homebrew](http://brew.sh/) installs the stuff you need that Apple didn’t."

Open a terminal and type:

```
ruby -e "$(curl -fsSL https://raw.githubusercontent.com/Homebrew/install/master/install)"
```

For more info, see "Step 3: Install Homebrew"
[here](http://www.moncefbelyamani.com/how-to-install-xcode-homebrew-git-rvm-ruby-on-mac)

### Install Git

```
brew update brew install git
```

For more info, see "Step 4: Install Git"
[here](http://www.moncefbelyamani.com/how-to-install-xcode-homebrew-git-rvm-ruby-on-mac)

**NOTE**: To gain full featured access to the
[git-subtree](http://blogs.atlassian.com/2013/05/alternatives-to-git-submodule-git-subtree/)
functionality used in the rippled repository, we suggest Git version 1.8.3.2 or
later.

### Install Scons

Requires version 2.3.0 or later

```
brew install scons
```

`brew` will generally install the latest stable version of any package, which
will satisfy the scons minimum version requirement for rippled.

### Install Package Config

```
brew install pkg-config
```

## Install/Build/Configure Dependencies

### Build Google Protocol Buffers Compiler

Building rippled on osx requires `protoc` version 2.5.x or 2.6.x (later versions
do not work with rippled at this time).

Download [this](https://github.com/google/protobuf/releases/download/v2.6.1/protobuf-2.6.1.tar.bz2)

We want to compile protocol buffers with clang/libc++:

```
tar xfvj protobuf-2.6.1.tar.bz2
cd protobuf-2.6.1
./configure CC=clang CXX=clang++ CXXFLAGS='-std=c++11 -stdlib=libc++ -O3 -g' LDFLAGS='-stdlib=libc++' LIBS="-lc++ -lc++abi"
make -j 4
sudo make install
```

If you have installed `protobuf` via brew - either directly or indirectly as a
dependency of some other package - this is likely to conflict with our specific
version requirements. The simplest way to avoid conflicts is to uninstall it.
`brew ls --versions protobuf` will list any versions of protobuf
you currently have installed.

### Install OpenSSL

```
brew install openssl
```

### Build Boost

We want to compile boost with clang/libc++

Download [a release](https://sourceforge.net/projects/boost/files/boost/1.61.0/boost_1_61_0.tar.bz2)

Extract it to a folder, making note of where, open a terminal, then:

```
./bootstrap.sh ./b2 toolset=clang threading=multi runtime-link=static link=static cxxflags="-stdlib=libc++" linkflags="-stdlib=libc++" address-model=64
```

Create an environment variable `BOOST_ROOT` in one of your `rc` files, pointing
to the root of the extracted directory.

### Clone the rippled repository

From the terminal

```
git clone git@github.com:ripple/rippled.git
cd rippled
```

Choose the master branch or one of the tagged releases listed on
[GitHub](https://github.com/ripple/rippled/releases GitHub).

```
git checkout master
```

or to test the latest release candidate, choose the `release` branch.

```
git checkout release
```

### Configure Library Paths

If you didn't persistently set the `BOOST_ROOT` environment variable to the
root of the extracted directory above, then you should set it temporarily.

For example, assuming your username were `Abigail` and you extracted Boost
1.61.0 in `/Users/Abigail/Downloads/boost_1_61_0`, you would do for any
shell in which you want to build:

```
export BOOST_ROOT=/Users/Abigail/Downloads/boost_1_61_0
```

## Build

```
scons
```

See: [here](https://ripple.com/wiki/Rippled_build_instructions#Building)

## Unit Tests (Recommended)

rippled builds a set of unit tests into the server executable. To run these unit
tests after building, pass the `--unittest` option to the compiled `rippled`
executable. The executable will exit after running the unit tests.

## System Tests (Recommended)

The external rippled system tests are written in Javascript using Node.js, and
utilize the buster system test framework. To run the system tests, it will be
necessary to perform the following steps:

### Install Node.js

Install [Node.js](http://nodejs.org/download/). We recommend the macos
installer (`.pkg` file) since it takes care of updating the `PATH`
environment variable so that scripts can find the command. On macos systems,
`Node.js` comes with `npm`. A separate installation of `npm` is not
necessary.

### Create node_modules

From the root of your local rippled repository, invoke `npm` to
bring in the necessary components:

```
npm install
```

### Run Tests

```
npm test
```

### Development ripple-lib

If you want to use the latest branch of `ripple-lib` during the system tests:

1. clone the repository in a new location outside of your rippled repository.
2. update the submodules in that repo.
3. run `npm install` to set up the `node_modules` directory.
4. install the `grunt` command line tools required to run `grunt` and build `ripple-lib`.

i.e.:

```
git clone https://github.com/ripple/rippled.git
cd ripple-lib
git submodule update --init
npm install
npm install -g grunt-cli
grunt
```

Now link this version of `ripple-lib` into the global packages:

```
sudo npm link
```

To make rippled use the newly linked global `ripple-lib` package instead of
the one installed under `node_modules`, change directories to the local
rippled repository and delete the old `ripple-lib` then link to the new
one:

```
rm -rf node_modules/ripple-lib
npm link ripple-lib
```

