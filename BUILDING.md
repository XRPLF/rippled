## Platforms

We do not recommend Windows for rippled production use at this time. Currently,
the Ubuntu platform has received the highest level of quality assurance,
testing, and support. Additionally, 32-bit Windows development is not supported.


## Prerequisites

To build this package, you will need Python (>= 3.7),
[Conan][] (>= 1.46), and [CMake][] (>= 3.16).
These instructions assume a basic understanding of those tools.

[Conan]: https://conan.io/downloads.html
[CMake]: https://cmake.org/download/

Linux developers will commonly have a default Conan [profile][1] that compiles
with GCC and links with libstdc++.
If you are linking with libstdc++ (see profile setting `compiler.libcxx`),
then you will need to choose the `libstdc++11` ABI:

```
conan profile update settings.compiler.libcxx=libstdc++11 default
```

Windows developers will commonly have a default Conan profile that is missing
a selection for the [MSVC runtime library][2].
You must set the Conan setting `compiler.runtime`
to ensure a consistent choice among all binaries in the dependency graph.
Which value you must use depends on which configuration you are trying to
build: `MT` for release and `MTd` for debug.

We find it necessary to use the x64 native build tools on Windows.
An easy way to do that is to run the shortcut "x64 Native Tools Command
Prompt" for the version of Visual Studio that you have installed.

Windows developers must build rippled and its dependencies for the x64
architecture:

```
conan profile update settings.compiler.arch=x86_64 default
```

Visual Studio 2022 is not yet supported.
This is because rippled is not compatible with [Boost][] versions 1.78 or 1.79,
but Conan cannot build Boost versions released earlier than them with VS 2022.
We expect that rippled will be compatible with Boost 1.80, which should be
released in August 2022.
Until then, we advise Windows developers to use Visual Studio 2019.

[Boost]: https://www.boost.org/


## Branches

For a stable release, choose the `master` branch or one of the tagged releases
listed on [rippled's GitHub page](https://github.com/ripple/rippled/releases).

```
git checkout master
```

To test the latest release candidate, choose the `release` branch.

```
git checkout release
```

If you are contributing or want the latest set of untested features,
then use the `develop` branch instead.

```
git checkout develop
```


## How to build and test

This is the general structure of the workflow to build and test rippled:

1. Export [our Conan recipe for RocksDB](./external/rocksdb).
1. Create a build directory. You can choose any name you want.
1. Build and install dependencies using Conan.
1. Configure CMake and generate the build system.
1. Build rippled.
1. Test rippled.
The executable's exact location depends on your choice of CMake generator.

Here is an example workflow commonly used to build with a single-configuration
generator (e.g. Unix Makefiles) on Linux or OSX:

```
conan export external/rocksdb
mkdir .build
cd .build
conan install .. --output-folder . --build missing --settings build_type=Release
cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
./rippled --unittest
```

Here is its equivalent for a multi-configuration generator (e.g. Visual Studio)
on Windows:

```
conan export external/rocksdb
mkdir .build
cd .build
conan install .. --output-folder . --build missing --settings build_type=Release --settings compiler.runtime=MT
cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake ..
cmake --build . --config Release
./Release/rippled --unittest
```

In addition to choosing a different build configuration, there are a few options
you can pass to CMake.
In particular, the `unity` option allows you to select between [unity][5] and
non-unity builds. Unity builds may be faster for the first build (at the cost of much
more memory) since they bundle sources into fewer translation
units.
Non-unity builds may be faster for incremental builds, and can be helpful for
detecting `#include` omissions, but aren't generally needed for testing or running.

Below are the most commonly used options,
with their default values in parentheses.

- `assert` (OFF): Enable assertions.
- `reporting` (OFF): Build the reporting mode feature.
- `tests` (ON): Build tests.
- `unity` (ON): Configure a [unity build][5].


## How to add a dependency

If you want to experiment with a new package, here are the steps to get it
working:

1. Search for the package on [Conan Center](https://conan.io/center/).
1. In [`conanfile.py`](./conanfile.py):
    1. Add a version of the package to the `requires` property.
    1. Change any default options for the package by adding them to the
    `default_options` property (with syntax `'$package:$option': $value`)
1. In [`CMakeLists.txt`](./CMakeLists.txt):
    1. Add a call to `find_package($package REQUIRED)`.
    1. Link a library from the package to the target `ripple_libs` (search for
    the existing call to `target_link_libraries(ripple_libs INTERFACE ...)`).
1. Start coding! Don't forget to include whatever headers you need from the
   package.


[1]: https://docs.conan.io/en/latest/reference/profiles.html
[2]: https://docs.microsoft.com/en-us/cpp/build/reference/md-mt-ld-use-run-time-library
[3]: https://cmake.org/cmake/help/git-stage/variable/CMAKE_MSVC_RUNTIME_LIBRARY.html
[4]: https://github.com/boostorg/build/issues/735#issuecomment-943377899
[5]: https://en.wikipedia.org/wiki/Unity_build
