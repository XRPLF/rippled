## Branches

For a stable release, choose the `master` branch or one of the [tagged
releases](https://github.com/ripple/rippled/releases).

```
git checkout master
```

For the latest release candidate, choose the `release` branch.

```
git checkout release
```

If you are contributing or want the latest set of untested features,
then use the `develop` branch.

```
git checkout develop
```


## Platforms

rippled is written in the C++20 dialect and includes the `<concepts>` header.
The [minimum compiler versions][2] that can compile this dialect are given
below:

| Compiler | Minimum Version
|---|---
| GCC         | 10
| Clang       | 13
| Apple Clang | 13.1.6
| MSVC        | 19.23

We do not recommend Windows for rippled production use at this time.
As of January 2023, the Ubuntu platform has received the highest level of
quality assurance, testing, and support.
Additionally, 32-bit Windows development is not supported.

Visual Studio 2022 is not yet supported.
This is because rippled is not compatible with [Boost][] versions 1.78 or 1.79,
but Conan cannot build Boost versions released earlier than them with VS 2022.
We expect that rippled will be compatible with Boost 1.80, which should be
released in August 2022.
Until then, we advise Windows developers to use Visual Studio 2019.

[Boost]: https://www.boost.org/


## Prerequisites

> **Warning**
> These instructions assume you have a C++ development environment ready
> with Git, Python, Conan, CMake, and a C++ compiler.
> For help setting one up on Linux, macOS, or Windows,
> please see [our guide](./docs/build/environment.md).
>
> These instructions further assume a basic familiarity with Conan and CMake.
> If you are unfamiliar with Conan,
> then please read our [crash course](./docs/build/conan.md)
> or the official [Getting Started][3] walkthrough.

To build this package, you will need Python (>= 3.7),
[Conan][] (>= 1.55, < 2), and [CMake][] (>= 3.16).

[Conan]: https://conan.io/downloads.html
[CMake]: https://cmake.org/download/

You'll need at least one Conan profile:

```
conan profile new default --detect
```

You'll need to compile in the C++20 dialect:

```
conan profile update settings.compiler.cppstd=20 default
```

Linux developers will commonly have a default Conan [profile][] that compiles
with GCC and links with libstdc++.
If you are linking with libstdc++ (see profile setting `compiler.libcxx`),
then you will need to choose the `libstdc++11` ABI:

```
conan profile update settings.compiler.libcxx=libstdc++11 default
```

We find it necessary to use the x64 native build tools on Windows.
An easy way to do that is to run the shortcut "x64 Native Tools Command
Prompt" for the version of Visual Studio that you have installed.

Windows developers must build rippled and its dependencies for the x64
architecture:

```
conan profile update settings.arch=x86_64 default
```

If you have multiple compilers installed on your platform,
then you'll need to make sure that Conan and CMake select the one you want to
use.
This setting will set the correct variables (`CMAKE_<LANG>_COMPILER`) in the
generated CMake toolchain file:

```
conan profile update 'conf.tools.build:compiler_executables={"c": "<path>", "cpp": "<path>"}' default
```

It should choose the compiler for dependencies as well,
but not all of them have a Conan recipe that respects this setting (yet).
For the rest, you can set these environment variables:

```
conan profile update env.CC=<path> default
conan profile update env.CXX=<path> default
```

Export our [Conan recipe for Snappy](./external/snappy).
It does not explicitly link the C++ standard library,
which allows you to statically link it with GCC, if you want.

```
conan export external/snappy snappy/1.1.9@
```

Export our [Conan recipe for SOCI](./external/soci).
It patches their CMake to correctly import its dependencies.

```
conan export external/soci soci/4.0.3@
```

## How to build and test

Let's start with a couple of examples of common workflows.
The first is for a single-configuration generator (e.g. Unix Makefiles) on
Linux or macOS:

```
mkdir .build
cd .build
conan install .. --output-folder . --build missing --settings build_type=Release
cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release ..
cmake --build .
./rippled --unittest
```

The second is for a multi-configuration generator (e.g. Visual Studio) on
Windows:

```
mkdir .build
cd .build
conan install .. --output-folder . --build missing --settings build_type=Release --settings compiler.runtime=MT
conan install .. --output-folder . --build missing --settings build_type=Debug --settings compiler.runtime=MTd
cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake ..
cmake --build . --config Release
cmake --build . --config Debug
./Release/rippled --unittest
./Debug/rippled --unittest
```

Now to explain the individual steps in each example:

1. Create a build directory (and move into it).

    You can choose any name you want.

    Conan will generate some files in what it calls the "install folder".
    These files are implementation details that you don't need to worry about.
    By default, the install folder is your current working directory.
    If you don't move into your build directory before calling Conan,
    then you may be annoyed to see it polluting your project root directory
    with these files.
    To make Conan put them in your build directory,
    you'll have to add the option
    `--install-folder` or `-if` to every `conan install` command.

1. Generate CMake files for every configuration you want to build.

    For a single-configuration generator, e.g. `Unix Makefiles` or `Ninja`,
    you only need to run this command once.
    For a multi-configuration generator, e.g. `Visual Studio`, you may want to
    run it more than once.

    Each of these commands should have a different `build_type` setting.
    A second command with the same `build_type` setting will just overwrite
    the files generated by the first.
    You can pass the build type on the command line with `--settings
    build_type=$BUILD_TYPE` or in the profile itself, under the section
    `[settings]`, with the key `build_type`.

    If you are using a Microsoft Visual C++ compiler, then you will need to
    ensure consistency between the `build_type` setting and the
    `compiler.runtime` setting.
    When `build_type` is `Release`, `compiler.runtime` should be `MT`.
    When `build_type` is `Debug`, `compiler.runtime` should be `MTd`.

1. Configure CMake once.

    For all choices of generator, pass the toolchain file generated by Conan.
    It will be located at
    `$OUTPUT_FOLDER/build/generators/conan_toolchain.cmake`.
    If you are using a single-configuration generator, then pass the CMake
    variable [`CMAKE_BUILD_TYPE`][build_type] and make sure it matches the
    `build_type` setting you chose in the previous step.

    This step is where you may pass build options for rippled.

1. Build rippled.

    For a multi-configuration generator, you must pass the option `--config`
    to select the build configuration.
    For a single-configuration generator, it will build whatever configuration
    you passed for `CMAKE_BUILD_TYPE`.

1. Test rippled.

    The exact location of rippled in your build directory
    depends on your choice of CMake generator.
    You can run unit tests by passing `--unittest`.
    Pass `--help` to see the rest of the command line options.


### Options

The `unity` option allows you to select between [unity][5] and non-unity
builds.
Unity builds may be faster for the first build (at the cost of much
more memory) since they concatenate sources into fewer translation
units.
Non-unity builds may be faster for incremental builds, and can be helpful for
detecting `#include` omissions.

Below are the most commonly used options,
with their default values in parentheses.

- `assert` (OFF): Enable assertions.
- `reporting` (OFF): Build the reporting mode feature.
- `tests` (ON): Build tests.
- `unity` (ON): Configure a [unity build][5].
- `san` (): Enable a sanitizer with Clang. Choices are `thread` and `address`.


### Troubleshooting

#### Conan

If you find trouble building dependencies after changing Conan settings,
then you should retry after removing the Conan cache:

```
rm -rf ~/.conan/data
```


#### no std::result_of

If your compiler version is recent enough to have removed `std::result_of` as
part of C++20, e.g. Apple Clang 15.0,
then you might need to add a preprocessor definition to your bulid:

```
conan profile update 'options.boost:extra_b2_flags="define=BOOST_ASIO_HAS_STD_INVOKE_RESULT"' default
conan profile update 'env.CFLAGS="-DBOOST_ASIO_HAS_STD_INVOKE_RESULT"' default
conan profile update 'env.CXXFLAGS="-DBOOST_ASIO_HAS_STD_INVOKE_RESULT"' default
conan profile update 'conf.tools.build:cflags+=["-DBOOST_ASIO_HAS_STD_INVOKE_RESULT"]' default
conan profile update 'conf.tools.build:cxxflags+=["-DBOOST_ASIO_HAS_STD_INVOKE_RESULT"]' default
```


#### recompile with -fPIC

```
/usr/bin/ld.gold: error: /home/username/.conan/data/boost/1.77.0/_/_/package/.../lib/libboost_container.a(alloc_lib.o):
  requires unsupported dynamic reloc 11; recompile with -fPIC
```

If you get a linker error like the one above suggesting that you recompile
Boost with position-independent code, the reason is most likely that Conan
downloaded a bad binary distribution of the dependency.
For now, this seems to be a [bug][1] in Conan just for Boost 1.77.0 compiled
with GCC for Linux.
The solution is to build the dependency locally by passing `--build boost`
when calling `conan install`:

```
conan install --build boost ...
```


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


[1]: https://github.com/conan-io/conan-center-index/issues/13168
[2]: https://en.cppreference.com/w/cpp/compiler_support/20
[3]: https://docs.conan.io/en/latest/getting_started.html
[5]: https://en.wikipedia.org/wiki/Unity_build
[build_type]: https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html
[runtime]: https://cmake.org/cmake/help/latest/variable/CMAKE_MSVC_RUNTIME_LIBRARY.html
[toolchain]: https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html
[pcf]: https://cmake.org/cmake/help/latest/manual/cmake-packages.7.html#package-configuration-file
[pvf]: https://cmake.org/cmake/help/latest/manual/cmake-packages.7.html#package-version-file
[find_package]: https://cmake.org/cmake/help/latest/command/find_package.html
[search]: https://cmake.org/cmake/help/latest/command/find_package.html#search-procedure
[prefix_path]: https://cmake.org/cmake/help/latest/variable/CMAKE_PREFIX_PATH.html
[profile]: https://docs.conan.io/en/latest/reference/profiles.html
