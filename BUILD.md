- [Build `rippled`](#build-rippled)
  - [Branches](#branches)
  - [Minimum Requirements](#minimum-requirements)
  - [Set Up Conan](#set-up-conan)
    - [All Platforms](#all-platforms)
    - [Additional Linux Set Up](#additional-linux-set-up)
    - [Additional Windows Set Up](#additional-windows-set-up)
  - [Build Steps](#build-steps)
  - [Options](#options)
  - [Add a Dependency](#add-a-dependency)
  - [Troubleshooting](#troubleshooting)
    - [Conan](#conan)
    - [no std::result\_of](#no-stdresult_of)
    - [recompile with -fPIC](#recompile-with--fpic)

# Build `rippled`

> **Warning:**
>
> The commands used in this document are examples and will differ depending on your circumstances. Don't copy and paste them without a basic understanding of _Conan_ and _CMake_. See:
> - [CMake Getting Started][]
> - [Conan Getting Started][]

We recommend using CMake and Conan to build this project. You can manually build it yourself, but the process is tedious and error-prone, requiring you to:

- Compile every translation unit into an object file.
- Link those objects together.
- Download, configure, build, and install all dependencies.
- Check this project and its dependencies are using the same linker and compiler options.

CMake and Conan automate most of these steps, using our `CMakeLists.txt` and `conanfile.py` configuration files.


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


## Minimum Requirements

- [Python 3.7][]
- [Conan 1.58][]
- [CMake 3.16][]

| Compiler    | Version |
| ----------- | ------- |
| GCC         | 10      |
| Clang       | 13      |
| Apple Clang | 13.1.6  |
| MSVC        | 19.23   |

We recommend _Ubuntu_ for `rippled` production use because it has the highest level of QA, testing, and support. You can use _Windows_, but 32-bit development isn't supported.

<!-- Is Visual Studio '22 supported now that Boost 1.8 is out?

Visual Studio 2022 is not yet supported.
This is because rippled is not compatible with [Boost][] versions 1.78 or 1.79,
but Conan cannot build Boost versions released earlier than them with VS 2022.
We expect that rippled will be compatible with Boost 1.80, which should be
released in August 2022.
Until then, we advise Windows developers to use Visual Studio 2019.

[Boost]: https://www.boost.org/

-->

## Set Up Conan

### All Platforms

1. (Optional) If you've never used _Conan_, use autodetect to set up a default profile.

    ```
    conan profile new default --detect
    ```

2. Compile in the C++20 dialect.

    ```
    conan profile update settings.compiler.cppstd=20 default
    ```

3. (Optional) If you have multiple compilers installed, make sure Conan and CMake select the one you want to use.

    ```
    conan profile update 'conf.tools.build:compiler_executables={"c": "<path>", "cpp": "<path>"}' default
    ```

    This command should choose the compiler for dependencies as well, but not all of them have a Conan recipe that respects this setting (yet). For the rest, you can set these environment variables:
    
    ```
    conan profile update env.CC=<path> default
    conan profile update env.CXX=<path> default
    ```

### Additional Linux Set Up 

Linux developers commonly have a default Conan [profile][] that compiles with GCC and links with libstdc++. If you are linking with libstdc++ (see profile setting `compiler.libcxx`), then you will need to choose the `libstdc++11` ABI:

```
conan profile update settings.compiler.libcxx=libstdc++11 default
```

### Additional Windows Set Up

We recommended you use _PowerShell_ in administrator mode to run commands. See an [example script](./Builds/windows/install.ps1) that utilizes the [chocolately](https://chocolatey.org/) package manager.

You should also use the x64 native build tools on Windows. Run _x64 Native Tools Command Prompt_ for your version of Visual Studio.

Windows developers must build rippled and its dependencies for the x64 architecture:

```
conan profile update settings.arch=x86_64 default
```


## Build Steps

<!-- I'm seeing 6.27.3 on Conan Center. Can we insert a linkt to it? Or is it still better to use what's in the repo? -->
1. Export our [Conan recipe for RocksDB](./external/rocksdb). It builds version 6.27.3, which, as of July 8, 2022, is not available in [Conan Center](https://conan.io/center/rocksdb).

    ```
    conan export external/rocksdb
    ```

2. Create a build directory and move into it.

    ```
    mkdir .build
    cd .build
    ```

    You can use any directory name. Conan treats your working directory as an install folder and generates files with implementation details. You don't need to worry about these files, but make sure to change your working directory to your build directory before calling Conan.

    > **Note:** You can specify a directory to put the Conan files in by adding the `install-folder` or `-if` option to every `conan install` command in the next step.

3. Generate CMake files for every configuration you want to build. 

    ```
    conan install .. --output-folder . --build missing --settings build_type=Release
    conan install .. --output-folder . --build missing --settings build_type=Debug
    ```

    Each of these commands should have a different `build_type` setting. A second command with the same `build_type` setting will overwrite the files generated by the first.
    
    If you are using a Microsoft Visual C++ compiler, then you will need to ensure consistency between the `build_type` setting and the `compiler.runtime` setting.
    
    When `build_type` is `Release`, `compiler.runtime` should be `MT`.
    
    When `build_type` is `Debug`, `compiler.runtime` should be `MTd`.

    ```
    conan install .. --output-folder . --build missing --settings build_type=Release --settings compiler.runtime=MT
    conan install .. --output-folder . --build missing --settings build_type=Debug --settings compiler.runtime=MTd
    ```

4. Configure CMake and pass the toolchain file generated by Conan, located at `$OUTPUT_FOLDER/build/generators/conan_toolchain.cmake`:

    Single-config generators:
    
    ```
    cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release ..
    ```

    > **Note:** Pass the CMake variable [`CMAKE_BUILD_TYPE`][build_type] and make sure it matches the `build_type` setting you chose in the previous step.

    Multi-config gnerators:

    ```
    cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake ..
    ```

5. Build rippled.

    Single-config generators:

    ```
    cmake --build .
    ```

    Multi-config generators:
    
    ```
    cmake --build . --config Release
    cmake --build . --config Debug
    ```

6. Test rippled.

    Single-config generators:

    ```
    ./rippled --unittest
    ```

    Multi-config generators:

    ```
    ./Release/rippled --unittest
    ./Debug/rippled --unittest
    ```

    The location of `rippled` in your build directory depends on your CMake generator. Pass `--help` to see the rest of the command line options.


## Options

The `unity` option allows you to select between [unity][5] and non-unity builds. Unity builds may be faster for the first build (at the cost of much more memory) since they concatenate sources into fewer translation units. Non-unity builds may be faster for incremental builds, and can be helpful for detecting `#include` omissions.

| Option | Default Value | Description |
| --- | ---| ---|
| `assert` | OFF | Enable assertions.
| `reporting` | OFF | Build the reporting mode feature. |
| `tests` | ON | Build tests. |
| `unity` | ON | Configure a [unity build][5]. |
| `san` | N/A | Enable a sanitizer with Clang. Choices are `thread` and `address`. |


## Add a Dependency

If you want to experiment with a new package, follow these steps:

1. Search for the package on [Conan Center](https://conan.io/center/).
2. Modify [`conanfile.py`](./conanfile.py):
    - Add a version of the package to the `requires` property.
    - Change any default options for the package by adding them to the `default_options` property (with syntax `'$package:$option': $value`).
3. Modify [`CMakeLists.txt`](./CMakeLists.txt):
    - Add a call to `find_package($package REQUIRED)`.
    - Link a library from the package to the target `ripple_libs` (search for the existing call to `target_link_libraries(ripple_libs INTERFACE ...)`).
4. Start coding! Don't forget to include whatever headers you need from the package.


## Troubleshooting

### Conan

If you have trouble building dependencies after changing Conan settings, try removing the Conan cache:

```
rm -rf ~/.conan/data
```

### no std::result_of

If your compiler version is recent enough to have removed `std::result_of` as part of C++20, then you might need to add a preprocessor definition to your bulid:

```
conan profile update 'env.CFLAGS="-DBOOST_ASIO_HAS_STD_INVOKE_RESULT"' default
conan profile update 'env.CXXFLAGS="-DBOOST_ASIO_HAS_STD_INVOKE_RESULT"' default
conan profile update 'tools.build:cflags+=["-DBOOST_ASIO_HAS_STD_INVOKE_RESULT"]' default
conan profile update 'tools.build:cxxflags+=["-DBOOST_ASIO_HAS_STD_INVOKE_RESULT"]' default
```

### recompile with -fPIC

If you get a linker error suggesting that you recompile Boost with position-independent code, such as:

```
/usr/bin/ld.gold: error: /home/username/.conan/data/boost/1.77.0/_/_/package/.../lib/libboost_container.a(alloc_lib.o):
  requires unsupported dynamic reloc 11; recompile with -fPIC
```

Conan most likely downloaded a bad binary distribution of the dependency. This seems to be a [bug][1] in Conan just for Boost 1.77.0 compiled with GCC for Linux. The solution is to build the dependency locally by passing `--build boost` when calling `conan install`:

```
conan install --build boost ...
```


<!-- Link Defs -->

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
[Conan 1.58]: https://conan.io/downloads.html
[CMake 3.16]: https://cmake.org/download/
[Python 3.7]: https://www.python.org/downloads/
[CMake Getting Started]: https://cmake.org/cmake/help/latest/guide/tutorial/A%20Basic%20Starting%20Point.html
[Conan Getting Started]: https://docs.conan.io/en/latest/getting_started.html
