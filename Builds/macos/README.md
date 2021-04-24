# macos Build Instructions

## Important

We don't recommend macos for rippled production use at this time. Currently, the
Ubuntu platform has received the highest level of quality assurance and
testing. That said, macos is suitable for many development/test tasks.

## Prerequisites

You'll need macos 10.8 or later.

To clone the source code repository, create branches for inspection or
modification, build rippled using clang, and run the system tests you will need
these software components:

* [XCode](https://developer.apple.com/xcode/)
* [Homebrew](http://brew.sh/)
* [Boost](http://boost.org/)
* other misc utilities and libraries installed via homebrew

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

For more info, see "Step 2: Install Homebrew"
[here](http://www.moncefbelyamani.com/how-to-install-xcode-homebrew-git-rvm-ruby-on-mac#step-2)

### Install Dependencies Using Homebrew

`brew` will generally install the latest stable version of any package, which
should satisfy the the minimum version requirements for rippled.

```
brew update
brew install git cmake pkg-config protobuf openssl ninja
```

### Build Boost

Boost 1.70 or later is required.

We want to compile boost with clang/libc++

Download [a release](https://dl.bintray.com/boostorg/release/1.70.0/source/boost_1_70_0.tar.bz2)

Extract it to a folder, making note of where, open a terminal, then:

```
./bootstrap.sh
./b2 cxxflags="-std=c++14"  visibility=global
```

Create an environment variable `BOOST_ROOT` in one of your `rc` files, pointing
to the root of the extracted directory.

### Dependencies for Building Source Documentation

Source code documentation is not required for running/debugging rippled. That
said, the documentation contains some helpful information about specific
components of the application. For more information on how to install and run
the necessary components, see [this document](../../docs/README.md)

## Build

### Clone the rippled repository

From a shell:

```
git clone git@github.com:ripple/rippled.git
cd rippled
```

For a stable release, choose the `master` branch or one of the tagged releases
listed on [GitHub](https://github.com/ripple/rippled/releases GitHub). 

```
git checkout master
```

or to test the latest release candidate, choose the `release` branch.

```
git checkout release
```

If you are doing development work and want the latest set of untested
features, you can consider using the `develop` branch instead.

```
git checkout develop
```

### Configure Library Paths

If you didn't persistently set the `BOOST_ROOT` environment variable to the
root of the extracted directory above, then you should set it temporarily.

For example, assuming your username were `Abigail` and you extracted Boost
1.70.0 in `/Users/Abigail/Downloads/boost_1_70_0`, you would do for any
shell in which you want to build:

```
export BOOST_ROOT=/Users/Abigail/Downloads/boost_1_70_0
```

### Generate and Build

For simple command line building we recommend using the *Unix Makefile* or
*Ninja* generator with cmake. All builds should be done in a separate directory
from the source tree root (a subdirectory is fine). For example, from the root
of the ripple source tree:

```
mkdir my_build
cd my_build
```

followed by:

```
cmake -G "Unix Makefiles" -DCMAKE_BUILD_TYPE=Debug ..
```

or

```
cmake -G "Ninja" -DCMAKE_BUILD_TYPE=Debug ..
```

`CMAKE_BUILD_TYPE` can be changed as desired for `Debug` vs.
`Release` builds (all four standard cmake build types are supported).

Once you have generated the build system, you can run the build via cmake:

```
cmake --build . -- -j 4
```

the `-j` parameter in this example tells the build tool to compile several
files in parallel. This value should be chosen roughly based on the number of
cores you have available and/or want to use for building.

When the build completes successfully, you will have a `rippled` executable in
the current directory, which can be used to connect to the network (when
properly configured) or to run unit tests.

If you prefer to have an XCode project to use for building, ask CMake to
generate that instead:

```
cmake -GXcode ..
```

After generation succeeds, the xcode project file can be opened and used to
build/debug. However, just as with other generators, cmake knows how to build
using the xcode project as well:

```
cmake --build . -- -jobs 4
```

This will invoke the `xcodebuild` utility to compile the project. See `xcodebuild
--help` for details about build options.

#### Optional installation

If you'd like to install the artifacts of the build, we have preliminary
support for standard CMake installation targets. We recommend explicitly
setting the installation location when configuring, e.g.:

```
cmake -DCMAKE_INSTALL_PREFIX=/opt/local ..
```

(change the destination as desired), and then build the `install` target:

```
cmake --build . --target install -- -jobs 4
```

#### Options During Configuration:

The CMake file defines a number of configure-time options which can be
examined by running `cmake-gui` or `ccmake` to generated the build. In
particular, the `unity` option allows you to select between the unity and
non-unity builds. `unity` builds are faster to compile since they combine
multiple sources into a single compiliation unit - this is the default if you
don't specify. `nounity` builds can be helpful for detecting include omissions
or for finding other build-related issues, but aren't generally needed for
testing and running.

* `-Dunity=ON` to enable/disable unity builds (defaults to ON) 
* `-Dassert=ON` to enable asserts
* `-Djemalloc=ON` to enable jemalloc support for heap checking
* `-Dsan=thread` to enable the thread sanitizer with clang
* `-Dsan=address` to enable the address sanitizer with clang

Several other infrequently used options are available - run `ccmake` or
`cmake-gui` for a list of all options.

## Unit Tests (Recommended)

`rippled` builds a set of unit tests into the server executable. To run these unit
tests after building, pass the `--unittest` option to the compiled `rippled`
executable. The executable will exit with summary info after running the unit tests.


