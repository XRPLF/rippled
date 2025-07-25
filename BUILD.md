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
- [Conan 1.60](https://conan.io/downloads.html)[^1]
- [CMake 3.16](https://cmake.org/download/)

[^1]: It is possible to build with Conan 2.x,
but the instructions are significantly different,
which is why we are not recommending it yet.
Notably, the `conan profile update` command is removed in 2.x.
Profiles must be edited by hand.

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

Configure Conan (1.x only) to use recipe revisions:

   ```
   conan config set general.revisions_enabled=1
   ```

**Linux** developers will commonly have a default Conan [profile][] that compiles
with GCC and links with libstdc++.
If you are linking with libstdc++ (see profile setting `compiler.libcxx`),
then you will need to choose the `libstdc++11` ABI:

   ```
   conan profile update settings.compiler.libcxx=libstdc++11 default
   ```


Ensure inter-operability between `boost::string_view` and `std::string_view` types:

```
conan profile update 'conf.tools.build:cxxflags+=["-DBOOST_BEAST_USE_STD_STRING_VIEW"]' default
conan profile update 'env.CXXFLAGS="-DBOOST_BEAST_USE_STD_STRING_VIEW"' default
```

If you have other flags in the `conf.tools.build` or `env.CXXFLAGS` sections, make sure to retain the existing flags and append the new ones. You can check them with:
```
conan profile show default
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
   # Conan 2.x
   conan export --version 1.1.10 external/snappy
   ```

Export our [Conan recipe for SOCI](./external/soci).
It patches their CMake to correctly import its dependencies.

   ```
   # Conan 2.x
   conan export --version 4.0.3 external/soci
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

2. Use conan to generate CMake files for every configuration you want to build:

    ```
    conan install .. --output-folder . --build missing --settings build_type=Release
    conan install .. --output-folder . --build missing --settings build_type=Debug
    ```

    To build Debug, in the next step, be sure to set `-DCMAKE_BUILD_TYPE=Debug`

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

    Pass the CMake variable [`CMAKE_BUILD_TYPE`][build_type]
    and make sure it matches the one of the `build_type` settings
    you chose in the previous step.

    For example, to build Debug, in the next command, replace "Release" with "Debug"

    ```
    cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -DCMAKE_BUILD_TYPE=Release -Dxrpld=ON -Dtests=ON ..
    ```


    Multi-config generators:

    ```
    cmake -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake -Dxrpld=ON -Dtests=ON  ..
    ```

    **Note:** You can pass build options for `rippled` in this step.

5. Build `rippled`.

   For a single-configuration generator, it will build whatever configuration
   you passed for `CMAKE_BUILD_TYPE`. For a multi-configuration generator,
   you must pass the option `--config` to select the build configuration. 

   Single-config generators:

   ```
   cmake --build . -j $(nproc)
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

   The location of `rippled` in your build directory depends on your CMake
   generator. Pass `--help` to see the rest of the command line options.


## Coverage report

The coverage report is intended for developers using compilers GCC
or Clang (including Apple Clang). It is generated by the build target `coverage`,
which is only enabled when the `coverage` option is set, e.g. with
`--options coverage=True` in `conan` or `-Dcoverage=ON` variable in `cmake`

Prerequisites for the coverage report:

- [gcovr tool][gcovr] (can be installed e.g. with [pip][python-pip])
- `gcov` for GCC (installed with the compiler by default) or
- `llvm-cov` for Clang (installed with the compiler by default)
- `Debug` build type

A coverage report is created when the following steps are completed, in order:

1. `rippled` binary built with instrumentation data, enabled by the `coverage`
   option mentioned above
2. completed run of unit tests, which populates coverage capture data
3. completed run of the `gcovr` tool (which internally invokes either `gcov` or `llvm-cov`)
   to assemble both instrumentation data and the coverage capture data into a coverage report

The above steps are automated into a single target `coverage`. The instrumented
`rippled` binary can also be used for regular development or testing work, at
the cost of extra disk space utilization and a small performance hit
(to store coverage capture). In case of a spurious failure of unit tests, it is
possible to re-run the `coverage` target without rebuilding the `rippled` binary
(since it is simply a dependency of the coverage report target). It is also possible
to select only specific tests for the purpose of the coverage report, by setting
the `coverage_test` variable in `cmake`

The default coverage report format is `html-details`, but the user
can override it to any of the formats listed in `Builds/CMake/CodeCoverage.cmake`
by setting the `coverage_format` variable in `cmake`. It is also possible
to generate more than one format at a time by setting the `coverage_extra_args`
variable in `cmake`. The specific command line used to run the `gcovr` tool will be
displayed if the `CODE_COVERAGE_VERBOSE` variable is set.

By default, the code coverage tool runs parallel unit tests with `--unittest-jobs`
 set to the number of available CPU cores. This may cause spurious test
errors on Apple. Developers can override the number of unit test jobs with
the `coverage_test_parallelism` variable in `cmake`.

Example use with some cmake variables set:

```
cd .build
conan install .. --output-folder . --build missing --settings build_type=Debug
cmake -DCMAKE_BUILD_TYPE=Debug -Dcoverage=ON -Dxrpld=ON -Dtests=ON -Dcoverage_test_parallelism=2 -Dcoverage_format=html-details -Dcoverage_extra_args="--json coverage.json" -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake ..
cmake --build . --target coverage
```

After the `coverage` target is completed, the generated coverage report will be
stored inside the build directory, as either of:

- file named `coverage.`_extension_ , with a suitable extension for the report format, or
- directory named `coverage`, with the `index.html` and other files inside, for the `html-details` or `html-nested` report formats.


## Options

| Option | Default Value | Description |
| --- | ---| ---|
| `assert` | OFF | Enable assertions.
| `coverage` | OFF | Prepare the coverage report. |
| `san` | N/A | Enable a sanitizer with Clang. Choices are `thread` and `address`. |
| `tests` | OFF | Build tests. |
| `unity` | ON | Configure a unity build. |
| `xrpld` | OFF | Build the xrpld (`rippled`) application, and not just the libxrpl library. |

[Unity builds][5] may be faster for the first build
(at the cost of much more memory) since they concatenate sources into fewer
translation units. Non-unity builds may be faster for incremental builds,
and can be helpful for detecting `#include` omissions.


## Troubleshooting

### Conan

After any updates or changes to dependencies, you may need to do the following:

1. Remove your build directory.
2. Remove the Conan cache: `conan remove "*" -c`
3. Re-run [conan install](#build-and-test).

### 'protobuf/port_def.inc' file not found

If `cmake --build .` results in an error due to a missing a protobuf file, then you might have generated CMake files for a different `build_type` than the `CMAKE_BUILD_TYPE` you passed to conan.

```
/rippled/.build/pb-xrpl.libpb/xrpl/proto/ripple.pb.h:10:10: fatal error: 'google/protobuf/port_def.inc' file not found
   10 | #include <google/protobuf/port_def.inc>
      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

For example, if you want to build Debug:

1. For conan install, pass `--settings build_type=Debug`
2. For cmake, pass `-DCMAKE_BUILD_TYPE=Debug`

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
[gcovr]: https://gcovr.com/en/stable/getting-started.html
[python-pip]: https://packaging.python.org/en/latest/guides/installing-using-pip-and-virtual-environments/
[build_type]: https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html
[profile]: https://docs.conan.io/en/latest/reference/profiles.html
