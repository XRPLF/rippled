| :warning: **WARNING** :warning:                                                                                                                                                                                                                                                                                                                                                                                                                                               |
| ----------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------------- |
| These instructions assume you have a C++ development environment ready with Git, Python, Conan, CMake, and a C++ compiler. For help setting one up on Linux, macOS, or Windows, [see this guide](./docs/build/environment.md).<br><br>These instructions also assume a basic familiarity with Conan and CMake. If you are unfamiliar with Conan, you can read our [crash course](./docs/build/conan.md) or the official [Getting Started][conan-getting-started] walkthrough. |

## Minimum Requirements

See [System Requirements](https://xrpl.org/system-requirements.html).

Building xrpld generally requires Git, Python, Conan, CMake, and a C++
compiler.

- [Python](https://www.python.org/downloads/)
- [Conan](https://conan.io/downloads.html)
- [CMake](https://cmake.org/download/)

You can verify that the required tools are installed and runnable with:

```bash
./bin/check-tools.sh
```

`xrpld` is written in the C++23 dialect. The [tested compiler versions][cpp23-support] are:

| Compiler    | Version         |
| ----------- | --------------- |
| GCC         | 15.2            |
| Clang       | 22              |
| Apple Clang | 17              |
| MSVC        | 19.44[^windows] |

## Operating Systems

Please see the [environment setup guide](./docs/build/environment.md) for detailed instructions for all platforms.

### Linux

The Ubuntu Linux distribution has received the highest level of quality
assurance, testing, and support. We also support Red Hat and use Debian
internally.
Our Linux CI tooling is distro-independent and uses a Nix-based environment, so it should be possible to build on other Linux distributions as well, although we have not tested them.

### macOS

Many `xrpld` engineers use macOS for development.

### Windows

Windows is used by some engineers for development only.

[^windows]: Windows is not recommended for production use.

## Steps

### Branches

For the latest set of untested features, or to contribute, choose the `develop`
branch.

```bash
git checkout develop
```

For a release candidate, choose the relevant release branch, e.g.
`release/3.2.x`.

```bash
git checkout release/3.2.x
```

For a stable release, choose one of the [tagged
releases](https://github.com/XRPLF/rippled/releases).

### Set Up Conan

After you have a [C++ development environment](./docs/build/environment.md) ready with Git, Python,
Conan, CMake, and a C++ compiler, you may need to set up your Conan profile.

These instructions assume a basic familiarity with Conan and CMake. If you are
unfamiliar with Conan, then please read [this crash course](./docs/build/conan.md) or the official
[Getting Started][conan-getting-started] walkthrough.

#### Profiles

We recommend that you install our Conan profiles:

```bash
conan config install conan/profiles/ -tf $(conan config home)/profiles/
```

You can check your Conan profile by running:

```bash
conan profile show
```

If the default profile is not suitable for your environment, you can create a custom profile and pass it to Conan.
More information on customizing Conan can be found in the [Advanced Conan configuration](./docs/build/advanced_conan.md).

#### Add xrplf remote

Run the following command to add the `xrplf` remote, which hosts some of our dependencies:

```bash
conan remote add --index 0 --force xrplf https://conan.ripplex.io
```

### Set Up Ccache

To speed up repeated compilations, we recommend that you install
[ccache](https://ccache.dev), a tool that wraps your compiler so that it can
cache build objects locally.

On Linux and macOS, `ccache` is included in the [Nix development shell](./docs/build/nix.md).

#### Windows

You can install it using Chocolatey, i.e. `choco install ccache`. If you already
have Ccache installed, then `choco upgrade ccache` will update it to the latest
version. However, if you see an error such as:

```
terminate called after throwing an instance of 'std::bad_alloc'
      what():  std::bad_alloc
C:\Program Files\Microsoft Visual Studio\2022\Community\MSBuild\Microsoft\VC\v170\Microsoft.CppCommon.targets(617,5): error MSB6006: "cl.exe" exited with code 3.
```

then please install a specific version of Ccache that we know works, via: `choco
install ccache --version 4.11.3 --allow-downgrade`.

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

   **Note:** You can pass build options for `xrpld` in this step.

4. Build `xrpld`.

   For a single-configuration generator, it will build whatever configuration
   you passed for `CMAKE_BUILD_TYPE`. For a multi-configuration generator, you
   must pass the option `--config` to select the build configuration.

   Single-config generators:

   ```
   cmake --build . --parallel N
   ```

   Multi-config generators:

   ```
   cmake --build . --config Release --parallel N
   cmake --build . --config Debug --parallel N
   ```

   Replace the `--parallel` parameter N with the desired number of parallel jobs. A common starting point is half of the number of available CPU
   cores.

5. Test xrpld.

   Single-config generators:

   ```
   ./xrpld --unittest --unittest-jobs N
   ```

   Multi-config generators:

   ```
   ./Release/xrpld --unittest --unittest-jobs N
   ./Debug/xrpld --unittest --unittest-jobs N
   ```

   Replace the `--unittest-jobs` parameter N with the desired unit tests
   concurrency. Recommended setting is half of the number of available CPU
   cores.

   The location of `xrpld` binary in your build directory depends on your
   CMake generator. Pass `--help` to see the rest of the command line options.

## Code generation

The protocol wrapper classes in `include/xrpl/protocol_autogen/` are generated
from macro definition files in `include/xrpl/protocol/detail/`. If you modify
the macro files (e.g. `transactions.macro`, `ledger_entries.macro`) or the
generation scripts/templates in `cmake/scripts/codegen/`, you need to regenerate the
files:

```
cmake --build . --target setup_code_gen  # create venv and install dependencies (once)
cmake --build . --target code_gen        # regenerate code
```

The regenerated files should be committed alongside your changes.

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

1. `xrpld` binary built with instrumentation data, enabled by the `coverage`
   option mentioned above
2. completed one or more run of the unit tests, which populates coverage capture data
3. completed run of the `gcovr` tool (which internally invokes either `gcov` or `llvm-cov`)
   to assemble both instrumentation data and the coverage capture data into a coverage report

The last step of the above is automated into a single target `coverage`. The instrumented
`xrpld` binary can also be used for regular development or testing work, at
the cost of extra disk space utilization and a small performance hit
(to store coverage capture data). Since `xrpld` binary is simply a dependency of the
coverage report target, it is possible to re-run the `coverage` target without
rebuilding the `xrpld` binary. Note, running of the unit tests before the `coverage`
target is left to the developer. Each such run will append to the coverage data
collected in the build directory.

The default coverage report format is `html-details`, but the user
can override it to any of the formats listed in `Builds/CMake/CodeCoverage.cmake`
by setting the `coverage_format` variable in `cmake`. It is also possible
to generate more than one format at a time by setting the `coverage_extra_args`
variable in `cmake`. The specific command line used to run the `gcovr` tool will be
displayed if the `CODE_COVERAGE_VERBOSE` variable is set.

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

## Sanitizers

To build dependencies and xrpld with sanitizer instrumentation, set the
`SANITIZERS` environment variable when running `conan install` and use the `sanitizers` profile:

```bash
export SANITIZERS=address,undefinedbehavior

conan install .. --output-folder . --profile:all sanitizers --build missing --settings build_type=Debug
```

You can then build and test as usual, with the generated `xrpld` binary containing the sanitizer instrumentation. When you run it, it will report any sanitizer errors it detects in the console output.

See [Sanitizers docs](./docs/build/sanitizers.md) for more details.

## Options

| Option     | Default Value | Description                                                    |
| ---------- | ------------- | -------------------------------------------------------------- |
| `assert`   | OFF           | Force enabling assertions.                                     |
| `coverage` | OFF           | Prepare the coverage report.                                   |
| `tests`    | OFF           | Build tests.                                                   |
| `unity`    | OFF           | Configure a unity build.                                       |
| `xrpld`    | OFF           | Build the xrpld application, and not just the libxrpl library. |
| `werr`     | OFF           | Treat compilation warnings as errors                           |
| `wextra`   | OFF           | Enable additional compilation warnings                         |

[Unity builds][unity-build] may be faster for the first build (at the cost of much more
memory) since they concatenate sources into fewer translation units. Non-unity
builds may be faster for incremental builds, and can be helpful for detecting
`#include` omissions.

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

3. Re-run [conan export](./docs/build/advanced_conan.md#patched-recipes) if needed.
4. [Regenerate lockfile](./docs/build/advanced_conan.md#conan-lockfile).
5. Re-run [conan install](#build-and-test).

#### ERROR: Package not resolved

If you're seeing an error like `ERROR: Package 'snappy/1.1.10' not resolved: Unable to find 'snappy/1.1.10#968fef506ff261592ec30c574d4a7809%1756234314.246' in remotes.`,
please [add `xrplf` remote](#add-xrplf-remote) or re-run `conan export` for [patched recipes](./docs/build/advanced_conan.md#patched-recipes).

### `protobuf/port_def.inc` file not found

If `cmake --build .` results in an error due to a missing a protobuf file, then
you might have generated CMake files for a different `build_type` than the
`CMAKE_BUILD_TYPE` you passed to Conan.

```
/xrpld/.build/pb-xrpl.libpb/xrpl/proto/xrpl.pb.h:10:10: fatal error: 'google/protobuf/port_def.inc' file not found
   10 | #include <google/protobuf/port_def.inc>
      |          ^~~~~~~~~~~~~~~~~~~~~~~~~~~~~~
1 error generated.
```

For example, if you want to build Debug:

1. For conan install, pass `--settings build_type=Debug`
2. For cmake, pass `-DCMAKE_BUILD_TYPE=Debug`

[cpp23-support]: https://en.cppreference.com/w/cpp/compiler_support/23
[conan-getting-started]: https://docs.conan.io/en/latest/getting_started.html
[unity-build]: https://en.wikipedia.org/wiki/Unity_build
[gcovr]: https://gcovr.com/en/stable/getting-started.html
[python-pip]: https://packaging.python.org/en/latest/guides/installing-using-pip-and-virtual-environments/
[build_type]: https://cmake.org/cmake/help/latest/variable/CMAKE_BUILD_TYPE.html
