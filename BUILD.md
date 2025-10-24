| :warning: **WARNING** :warning:
|---|
| These instructions assume you have a C++ development environment ready with Git, Python, Conan, CMake, and a C++ compiler. For help setting one up on Linux, macOS, or Windows, [see this guide](./docs/build/environment.md). |

> These instructions also assume a basic familiarity with Conan and CMake.
> If you are unfamiliar with Conan, you can read our
> [crash course](./docs/build/conan.md) or the official [Getting Started][3]
> walkthrough.

## Branches

For a stable release, choose the `master` branch or one of the [tagged
releases](https://github.com/ripple/rippled/releases).

```bash
git checkout master
```

For the latest release candidate, choose the `release` branch.

```bash
git checkout release
```

For the latest set of untested features, or to contribute, choose the `develop`
branch.

```bash
git checkout develop
```

## Minimum Requirements

See [System Requirements](https://xrpl.org/system-requirements.html).

Building rippled generally requires git, Python, Conan, CMake, and a C++
compiler. Some guidance on setting up such a [C++ development environment can be
found here](./docs/build/environment.md).

- [Python 3.11](https://www.python.org/downloads/), or higher
- [Conan 2.17](https://conan.io/downloads.html)[^1], or higher
- [CMake 3.22](https://cmake.org/download/), or higher

[^1]:
    It is possible to build with Conan 1.60+, but the instructions are
    significantly different, which is why we are not recommending it.

`rippled` is written in the C++20 dialect and includes the `<concepts>` header.
The [minimum compiler versions][2] required are:

| Compiler    | Version   |
| ----------- | --------- |
| GCC         | 12        |
| Clang       | 16        |
| Apple Clang | 16        |
| MSVC        | 19.44[^3] |

### Linux

The Ubuntu Linux distribution has received the highest level of quality
assurance, testing, and support. We also support Red Hat and use Debian
internally.

Here are [sample instructions for setting up a C++ development environment on
Linux](./docs/build/environment.md#linux).

### Mac

Many rippled engineers use macOS for development.

Here are [sample instructions for setting up a C++ development environment on
macOS](./docs/build/environment.md#macos).

### Windows

Windows is used by some engineers for development only.

[^3]: Windows is not recommended for production use.

## Steps

### Set Up Conan

After you have a [C++ development environment](./docs/build/environment.md) ready with Git, Python,
Conan, CMake, and a C++ compiler, you may need to set up your Conan profile.

These instructions assume a basic familiarity with Conan and CMake. If you are
unfamiliar with Conan, then please read [this crash course](./docs/build/conan.md) or the official
[Getting Started][3] walkthrough.

#### Default profile

We recommend that you import the provided `conan/profiles/default` profile:

```bash
conan config install conan/profiles/ -tf $(conan config home)/profiles/
```

You can check your Conan profile by running:

```bash
conan profile show
```

#### Custom profile

If the default profile does not work for you and you do not yet have a Conan
profile, you can create one by running:

```bash
conan profile detect
```

You may need to make changes to the profile to suit your environment. You can
refer to the provided `conan/profiles/default` profile for inspiration, and you
may also need to apply the required [tweaks](#conan-profile-tweaks) to this
default profile.

### Patched recipes

The recipes in Conan Center occasionally need to be patched for compatibility
with the latest version of `rippled`. We maintain a fork of the Conan Center
[here](https://github.com/XRPLF/conan-center-index/) containing the patches.

To ensure our patched recipes are used, you must add our Conan remote at a
higher index than the default Conan Center remote, so it is consulted first. You
can do this by running:

```bash
conan remote add --index 0 xrplf https://conan.ripplex.io
```

Alternatively, you can pull the patched recipes into the repository and use them
locally:

```bash
cd external
git init
git remote add origin git@github.com:XRPLF/conan-center-index.git
git sparse-checkout init
git sparse-checkout set recipes/snappy
git sparse-checkout add recipes/soci
git fetch origin master
git checkout master
conan export --version 1.1.10 recipes/snappy/all
conan export --version 4.0.3 recipes/soci/all
rm -rf .git
```

In the case we switch to a newer version of a dependency that still requires a
patch, it will be necessary for you to pull in the changes and re-export the
updated dependencies with the newer version. However, if we switch to a newer
version that no longer requires a patch, no action is required on your part, as
the new recipe will be automatically pulled from the official Conan Center.

> [!NOTE]
> You might need to add `--lockfile=""` to your `conan install` command
> to avoid automatic use of the existing `conan.lock` file when you run `conan export` manually on your machine

### Conan profile tweaks

#### Missing compiler version

If you see an error similar to the following after running `conan profile show`:

```bash
ERROR: Invalid setting '17' is not a valid 'settings.compiler.version' value.
Possible values are ['5.0', '5.1', '6.0', '6.1', '7.0', '7.3', '8.0', '8.1',
'9.0', '9.1', '10.0', '11.0', '12.0', '13', '13.0', '13.1', '14', '14.0', '15',
'15.0', '16', '16.0']
Read "http://docs.conan.io/2/knowledge/faq.html#error-invalid-setting"
```

you need to amend the list of compiler versions in
`$(conan config home)/settings.yml`, by appending the required version number(s)
to the `version` array specific for your compiler. For example:

```yaml
apple-clang:
  version:
    [
      "5.0",
      "5.1",
      "6.0",
      "6.1",
      "7.0",
      "7.3",
      "8.0",
      "8.1",
      "9.0",
      "9.1",
      "10.0",
      "11.0",
      "12.0",
      "13",
      "13.0",
      "13.1",
      "14",
      "14.0",
      "15",
      "15.0",
      "16",
      "16.0",
      "17",
      "17.0",
    ]
```

#### Multiple compilers

If you have multiple compilers installed, make sure to select the one to use in
your default Conan configuration **before** running `conan profile detect`, by
setting the `CC` and `CXX` environment variables.

For example, if you are running MacOS and have [homebrew
LLVM@18](https://formulae.brew.sh/formula/llvm@18), and want to use it as a
compiler in the new Conan profile:

```bash
export CC=$(brew --prefix llvm@18)/bin/clang
export CXX=$(brew --prefix llvm@18)/bin/clang++
conan profile detect
```

You should also explicitly set the path to the compiler in the profile file,
which helps to avoid errors when `CC` and/or `CXX` are set and disagree with the
selected Conan profile. For example:

```text
[conf]
tools.build:compiler_executables={'c':'/usr/bin/gcc','cpp':'/usr/bin/g++'}
```

#### Multiple profiles

You can manage multiple Conan profiles in the directory
`$(conan config home)/profiles`, for example renaming `default` to a different
name and then creating a new `default` profile for a different compiler.

#### Select language

The default profile created by Conan will typically select different C++ dialect
than C++20 used by this project. You should set `20` in the profile line
starting with `compiler.cppstd=`. For example:

```bash
sed -i.bak -e 's|^compiler\.cppstd=.*$|compiler.cppstd=20|' $(conan config home)/profiles/default
```

#### Select standard library in Linux

**Linux** developers will commonly have a default Conan [profile][] that
compiles with GCC and links with libstdc++. If you are linking with libstdc++
(see profile setting `compiler.libcxx`), then you will need to choose the
`libstdc++11` ABI:

```bash
sed -i.bak -e 's|^compiler\.libcxx=.*$|compiler.libcxx=libstdc++11|' $(conan config home)/profiles/default
```

#### Select architecture and runtime in Windows

**Windows** developers may need to use the x64 native build tools. An easy way
to do that is to run the shortcut "x64 Native Tools Command Prompt" for the
version of Visual Studio that you have installed.

Windows developers must also build `rippled` and its dependencies for the x64
architecture:

```bash
sed -i.bak -e 's|^arch=.*$|arch=x86_64|' $(conan config home)/profiles/default
```

**Windows** developers also must select static runtime:

```bash
sed -i.bak -e 's|^compiler\.runtime=.*$|compiler.runtime=static|' $(conan config home)/profiles/default
```

#### Clang workaround for grpc

If your compiler is clang, version 19 or later, or apple-clang, version 17 or
later, you may encounter a compilation error while building the `grpc`
dependency:

```text
In file included from .../lib/promise/try_seq.h:26:
.../lib/promise/detail/basic_seq.h:499:38: error: a template argument list is expected after a name prefixed by the template keyword [-Wmissing-template-arg-list-after-template-kw]
  499 |                     Traits::template CallSeqFactory(f_, *cur_, std::move(arg)));
      |                                      ^
```

The workaround for this error is to add two lines to profile:

```text
[conf]
tools.build:cxxflags=['-Wno-missing-template-arg-list-after-template-kw']
```

#### Workaround for gcc 12

If your compiler is gcc, version 12, and you have enabled `werr` option, you may
encounter a compilation error such as:

```text
/usr/include/c++/12/bits/char_traits.h:435:56: error: 'void* __builtin_memcpy(void*, const void*, long unsigned int)' accessing 9223372036854775810 or more bytes at offsets [2, 9223372036854775807] and 1 may overlap up to 9223372036854775813 bytes at offset -3 [-Werror=restrict]
  435 |         return static_cast<char_type*>(__builtin_memcpy(__s1, __s2, __n));
      |                                        ~~~~~~~~~~~~~~~~^~~~~~~~~~~~~~~~~
cc1plus: all warnings being treated as errors
```

The workaround for this error is to add two lines to your profile:

```text
[conf]
tools.build:cxxflags=['-Wno-restrict']
```

#### Workaround for clang 16

If your compiler is clang, version 16, you may encounter compilation error such
as:

```text
In file included from .../boost/beast/websocket/stream.hpp:2857:
.../boost/beast/websocket/impl/read.hpp:695:17: error: call to 'async_teardown' is ambiguous
                async_teardown(impl.role, impl.stream(),
                ^~~~~~~~~~~~~~
```

The workaround for this error is to add two lines to your profile:

```text
[conf]
tools.build:cxxflags=['-DBOOST_ASIO_DISABLE_CONCEPTS']
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

4. Build `rippled`.

   For a single-configuration generator, it will build whatever configuration
   you passed for `CMAKE_BUILD_TYPE`. For a multi-configuration generator, you
   must pass the option `--config` to select the build configuration.

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
   ./rippled --unittest --unittest-jobs N
   ```

   Multi-config generators:

   ```
   ./Release/rippled --unittest --unittest-jobs N
   ./Debug/rippled --unittest --unittest-jobs N
   ```

   Replace the `--unittest-jobs` parameter N with the desired unit tests
   concurrency. Recommended setting is half of the number of available CPU
   cores.

   The location of `rippled` binary in your build directory depends on your
   CMake generator. Pass `--help` to see the rest of the command line options.

#### Conan lockfile

To achieve reproducible dependencies, we use [Conan lockfile](https://docs.conan.io/2/tutorial/versioning/lockfiles.html).

The `conan.lock` file in the repository contains a "snapshot" of the current dependencies.
It is implicitly used when running `conan` commands, you don't need to specify it.

You have to update this file every time you add a new dependency or change a revision or version of an existing dependency.

> [!NOTE]
> Conan uses local cache by default when creating a lockfile.
>
> To ensure, that lockfile creation works the same way on all developer machines, you should clear the local cache before creating a new lockfile.

To create a new lockfile, run the following commands in the repository root:

```bash
conan remove '*' --confirm
rm conan.lock
# This ensure that xrplf remote is the first to be consulted
conan remote add --force --index 0 xrplf https://conan.ripplex.io
conan lock create . -o '&:jemalloc=True' -o '&:rocksdb=True'
```

> [!NOTE]
> If some dependencies are exclusive for some OS, you may need to run the last command for them adding `--profile:all <PROFILE>`.

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

- file named `coverage.`_extension_, with a suitable extension for the report format, or
- directory named `coverage`, with the `index.html` and other files inside, for the `html-details` or `html-nested` report formats.

## Options

| Option     | Default Value | Description                                                                |
| ---------- | ------------- | -------------------------------------------------------------------------- |
| `assert`   | OFF           | Enable assertions.                                                         |
| `coverage` | OFF           | Prepare the coverage report.                                               |
| `san`      | N/A           | Enable a sanitizer with Clang. Choices are `thread` and `address`.         |
| `tests`    | OFF           | Build tests.                                                               |
| `unity`    | OFF           | Configure a unity build.                                                   |
| `xrpld`    | OFF           | Build the xrpld (`rippled`) application, and not just the libxrpl library. |
| `werr`     | OFF           | Treat compilation warnings as errors                                       |
| `wextra`   | OFF           | Enable additional compilation warnings                                     |

[Unity builds][5] may be faster for the first build
(at the cost of much more memory) since they concatenate sources into fewer
translation units. Non-unity builds may be faster for incremental builds,
and can be helpful for detecting `#include` omissions.

## Troubleshooting

### Conan

After any updates or changes to dependencies, you may need to do the following:

1. Remove your build directory.
2. Remove individual libraries from the Conan cache, e.g.

   ```bash
   conan remove 'grpc/*'
   ```

   **or**

   Remove all libraries from Conan cache:

   ```bash
   conan remove '*'
   ```

3. Re-run [conan export](#patched-recipes) if needed.
4. [Regenerate lockfile](#conan-lockfile).
5. Re-run [conan install](#build-and-test).

#### ERROR: Package not resolved

If you're seeing an error like `ERROR: Package 'snappy/1.1.10' not resolved: Unable to find 'snappy/1.1.10#968fef506ff261592ec30c574d4a7809%1756234314.246' in remotes.`,
please add `xrplf` remote or re-run `conan export` for [patched recipes](#patched-recipes).

### `protobuf/port_def.inc` file not found

If `cmake --build .` results in an error due to a missing a protobuf file, then
you might have generated CMake files for a different `build_type` than the
`CMAKE_BUILD_TYPE` you passed to Conan.

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
