| :warning: **WARNING** :warning:
|---|
| These instructions assume you have a C++ development environment ready with Git, Python, Conan, CMake, and a C++ compiler. For help setting one up on Linux, macOS, or Windows, [see this guide](./docs/build/environment.md). |

> These instructions also assume a basic familiarity with Conan and CMake.
> If you are unfamiliar with Conan,
> you can read our [crash course](./docs/build/conan.md)
> or the official [Getting Started][3] walkthrough.

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

For the latest set of untested features, or to contribute, choose the `develop`
branch.

```
git checkout develop
```

## Minimum Requirements

See [System Requirements](https://xrpl.org/system-requirements.html).

Building rippled generally requires git, Python, Conan, CMake, and a C++ compiler. Some guidance on setting up such a [C++ development environment can be found here](./docs/build/environment.md).

- [Python 3.7](https://www.python.org/downloads/)
- [Conan 1.55](https://conan.io/downloads.html)
- [CMake 3.16](https://cmake.org/download/)

`rippled` is written in the C++20 dialect and includes the `<concepts>` header.
The [minimum compiler versions][2] required are:

| Compiler    | Version |
|-------------|---------|
| GCC         | 11      |
| Clang       | 13      |
| Apple Clang | 13.1.6  |
| MSVC        | 19.23   |

### Linux

The Ubuntu operating system has received the highest level of
quality assurance, testing, and support.

Here are [sample instructions for setting up a C++ development environment on Linux](./docs/build/environment.md#linux).

### Mac

Many rippled engineers use macOS for development.

Here are [sample instructions for setting up a C++ development environment on macOS](./docs/build/environment.md#macos).

### Windows

Windows is not recommended for production use at this time.

- Additionally, 32-bit Windows development is not supported.
- Visual Studio 2022 is not yet supported.
  - rippled generally requires [Boost][] 1.77, which Conan cannot build with VS 2022.
  - Until rippled is updated for compatibility with later versions of Boost, Windows developers may need to use Visual Studio 2019.

[Boost]: https://www.boost.org/

## Steps

### Set Up Conan

After you have a [C++ development environment](./docs/build/environment.md) ready with Git, Python, Conan, CMake, and a C++ compiler, you may need to set up your Conan profile.

These instructions assume a basic familiarity with Conan and CMake.

If you are unfamiliar with Conan, then please read [this crash course](./docs/build/conan.md) or the official [Getting Started][3] walkthrough.

You'll need at least one Conan profile:

   ```
   conan profile new default --detect
   ```

Update the compiler settings:

   ```
   conan profile update settings.compiler.cppstd=20 default
   ```

**Linux** developers will commonly have a default Conan [profile][] that compiles
with GCC and links with libstdc++.
If you are linking with libstdc++ (see profile setting `compiler.libcxx`),
then you will need to choose the `libstdc++11` ABI:

   ```
   conan profile update settings.compiler.libcxx=libstdc++11 default
   ```

**Windows** developers may need to use the x64 native build tools.
An easy way to do that is to run the shortcut "x64 Native Tools Command
Prompt" for the version of Visual Studio that you have installed.

   Windows developers must also build `rippled` and its dependencies for the x64
   architecture:

   ```
   conan profile update settings.arch=x86_64 default
   ```

### Multiple compilers

When `/usr/bin/g++` exists on a platform, it is the default cpp compiler. This
default works for some users.

However, if this compiler cannot build rippled or its dependencies, then you can
install another compiler and set Conan and CMake to use it.
Update the `conf.tools.build:compiler_executables` setting in order to set the correct variables (`CMAKE_<LANG>_COMPILER`) in the
generated CMake toolchain file.
For example, on Ubuntu 20, you may have gcc at `/usr/bin/gcc` and g++ at `/usr/bin/g++`; if that is the case, you can select those compilers with:
```
conan profile update 'conf.tools.build:compiler_executables={"c": "/usr/bin/gcc", "cpp": "/usr/bin/g++"}' default
```

Replace `/usr/bin/gcc` and `/usr/bin/g++` with paths to the desired compilers.

It should choose the compiler for dependencies as well,
but not all of them have a Conan recipe that respects this setting (yet).
For the rest, you can set these environment variables.
Replace `<path>` with paths to the desired compilers:

- `conan profile update env.CC=<path> default`
- `conan profile update env.CXX=<path> default`

Export our [Conan recipe for Snappy](./external/snappy).
It does not explicitly link the C++ standard library,
which allows you to statically link it with GCC, if you want.

   ```
   conan export external/snappy snappy/1.1.10@
   ```

Export our [Conan recipe for SOCI](./external/soci).
It patches their CMake to correctly import its dependencies.

   ```
   conan export external/soci soci/4.0.3@
   ```

### Build and Test

1. Create a build directory and move into it.

   ```
   mkdir .build
   cd .build
   ```

   You can use any directory name. Conan treats your working directory as an
   install folder and generates files with implementation details.
   You don't need to worry about these files, but make sure to change
   your working directory to your build directory before calling Conan.

   **Note:** You can specify a directory for the installation files by adding
   the `install-folder` or `-if` option to every `conan install` command
   in the next step.

2. Generate CMake files for every configuration you want to build. 

    ```
    conan install .. --output-folder . --build missing --settings build_type=Release
    conan install .. --output-folder . --build missing --settings build_type=Debug
    ```

    For a single-configuration generator, e.g. `Unix Makefiles` or `Ninja`,
    you only need to run this command once.
    For a multi-configuration generator, e.g. `Visual Studio`, you may want to
    run it more than once.

    Each of these commands should also have a different `build_type` setting.
    A second command with the same `build_type` setting will overwrite the files
    generated by the first. You can pass the build type on the command line with
    `--settings build_type=$BUILD_TYPE` or in the profile itself,
    under the section `[settings]` with the key `build_type`.
    
    If you are using a Microsoft Visual C++ compiler,
    then you will need to ensure consistency between the `build_type` setting
    and the `compiler.runtime` setting.
    
    When `build_type` is `Release`, `compiler.runtime` should be `MT`.
    
    When `build_type` is `Debug`, `compiler.runtime` should be `MTd`.

    ```
    conan install .. --output-folder . --build missing --settings build_type=Release --settings compiler.runtime=MT
    conan install .. --output-folder . --build missing --settings build_type=Debug --settings compiler.runtime=MTd
    ```

3. Configure CMake and pass the toolchain file generated by Conan, located at
   `$OUTPUT_FOLDER/build/generators/conan_toolchain.cmake`.

    Single-config generators:

    ```
    cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release ..
    ```

    Pass the CMake variable [`CMAKE_BUILD_TYPE`][build_type]
    and make sure it matches the `build_type` setting you chose in the previous
    step.

    Multi-config generators:

    ```
    cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake ..
    ```

    **Note:** You can pass build options for `rippled` in this step.

4. Build `rippled`.

   For a single-configuration generator, it will build whatever configuration
   you passed for `CMAKE_BUILD_TYPE`. For a multi-configuration generator,
   you must pass the option `--config` to select the build configuration. 

   Single-config generators:

   ```
   cmake --build .
   ```

   Multi-config generators:

   ```
   cmake --build . --config Release
   cmake --build . --config Debug
   ```

5. Test rippled.

   Single-config generators:

   ```
   ./rippled --unittest
   ```

   Multi-config generators:

   ```
   ./Release/rippled --unittest
   ./Debug/rippled --unittest
   ```

   The location of `rippled` in your build directory depends on your CMake
   generator. Pass `--help` to see the rest of the command line options.


## Options

| Option | Default Value | Description |
| --- | ---| ---|
| `assert` | OFF | Enable assertions.
| `reporting` | OFF | Build the reporting mode feature. |
| `tests` | ON | Build tests. |
| `unity` | ON | Configure a unity build. |
| `san` | N/A | Enable a sanitizer with Clang. Choices are `thread` and `address`. |

[Unity builds][5] may be faster for the first build
(at the cost of much more memory) since they concatenate sources into fewer
translation units. Non-unity builds may be faster for incremental builds,
and can be helpful for detecting `#include` omissions.


## Troubleshooting


### Conan

After any updates or changes to dependencies, you may need to do the following:

1. Remove your build directory.
2. Remove the Conan cache:
   ```
   rm -rf ~/.conan/data
   ```
4. Re-run [conan install](#build-and-test).


### no std::result_of

If your compiler version is recent enough to have removed `std::result_of` as
part of C++20, e.g. Apple Clang 15.0, then you might need to add a preprocessor
definition to your build.

```
conan profile update 'options.boost:extra_b2_flags="define=BOOST_ASIO_HAS_STD_INVOKE_RESULT"' default
conan profile update 'env.CFLAGS="-DBOOST_ASIO_HAS_STD_INVOKE_RESULT"' default
conan profile update 'env.CXXFLAGS="-DBOOST_ASIO_HAS_STD_INVOKE_RESULT"' default
conan profile update 'conf.tools.build:cflags+=["-DBOOST_ASIO_HAS_STD_INVOKE_RESULT"]' default
conan profile update 'conf.tools.build:cxxflags+=["-DBOOST_ASIO_HAS_STD_INVOKE_RESULT"]' default
```


### call to 'async_teardown' is ambiguous

If you are compiling with an early version of Clang 16, then you might hit
a [regression][6] when compiling C++20 that manifests as an [error in a Boost
header][7]. You can workaround it by adding this preprocessor definition:

```
conan profile update 'env.CXXFLAGS="-DBOOST_ASIO_DISABLE_CONCEPTS"' default
conan profile update 'conf.tools.build:cxxflags+=["-DBOOST_ASIO_DISABLE_CONCEPTS"]' default
```


### recompile with -fPIC

If you get a linker error suggesting that you recompile Boost with
position-independent code, such as:

```
/usr/bin/ld.gold: error: /home/username/.conan/data/boost/1.77.0/_/_/package/.../lib/libboost_container.a(alloc_lib.o):
  requires unsupported dynamic reloc 11; recompile with -fPIC
```

Conan most likely downloaded a bad binary distribution of the dependency.
This seems to be a [bug][1] in Conan just for Boost 1.77.0 compiled with GCC
for Linux. The solution is to build the dependency locally by passing
`--build boost` when calling `conan install`.

```
conan install --build boost ...
```


## Add a Dependency

If you want to experiment with a new package, follow these steps:

1. Search for the package on [Conan Center](https://conan.io/center/).
2. Modify [`conanfile.py`](./conanfile.py):
    - Add a version of the package to the `requires` property.
    - Change any default options for the package by adding them to the
    `default_options` property (with syntax `'$package:$option': $value`).
3. Modify [`CMakeLists.txt`](./CMakeLists.txt):
    - Add a call to `find_package($package REQUIRED)`.
    - Link a library from the package to the target `ripple_libs`
    (search for the existing call to `target_link_libraries(ripple_libs INTERFACE ...)`).
4. Start coding! Don't forget to include whatever headers you need from the package.


[1]: https://github.com/conan-io/conan-center-index/issues/13168
[2]: https://en.cppreference.com/w/cpp/compiler_support/20
[3]: https://docs.conan.io/en/latest/getting_started.html
[5]: https://en.wikipedia.org/wiki/Unity_build
[6]: https://github.com/boostorg/beast/issues/2648
[7]: https://github.com/boostorg/beast/issues/2661
[build_type]: https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html
[runtime]: https://cmake.org/cmake/help/latest/variable/CMAKE_MSVC_RUNTIME_LIBRARY.html
[toolchain]: https://cmake.org/cmake/help/latest/manual/cmake-toolchains.7.html
[pcf]: https://cmake.org/cmake/help/latest/manual/cmake-packages.7.html#package-configuration-file
[pvf]: https://cmake.org/cmake/help/latest/manual/cmake-packages.7.html#package-version-file
[find_package]: https://cmake.org/cmake/help/latest/command/find_package.html
[search]: https://cmake.org/cmake/help/latest/command/find_package.html#search-procedure
[prefix_path]: https://cmake.org/cmake/help/latest/variable/CMAKE_PREFIX_PATH.html
[profile]: https://docs.conan.io/en/latest/reference/profiles.html
