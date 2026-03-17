# Sanitizer Configuration for Rippled

This document explains how to properly configure and run sanitizers (AddressSanitizer, undefinedbehaviorSanitizer, ThreadSanitizer) with the xrpld project.
Corresponding suppression files are located in the `sanitizers/suppressions` directory.

- [Sanitizer Configuration for Rippled](#sanitizer-configuration-for-rippled)
  - [Building with Sanitizers](#building-with-sanitizers)
    - [Summary](#summary)
    - [Build steps:](#build-steps)
      - [Install dependencies](#install-dependencies)
      - [Call CMake](#call-cmake)
      - [Build](#build)
  - [Running Tests with Sanitizers](#running-tests-with-sanitizers)
    - [AddressSanitizer (ASAN)](#addresssanitizer-asan)
    - [ThreadSanitizer (TSan)](#threadsanitizer-tsan)
    - [LeakSanitizer (LSan)](#leaksanitizer-lsan)
    - [UndefinedBehaviorSanitizer (UBSan)](#undefinedbehaviorsanitizer-ubsan)
  - [Suppression Files](#suppression-files)
    - [`asan.supp`](#asansupp)
    - [`lsan.supp`](#lsansupp)
    - [`ubsan.supp`](#ubsansupp)
    - [`tsan.supp`](#tsansupp)
    - [`sanitizer-ignorelist.txt`](#sanitizer-ignorelisttxt)
  - [Troubleshooting](#troubleshooting)
    - ["ASAN is ignoring requested \_\_asan_handle_no_return" warnings](#asan-is-ignoring-requested-__asan_handle_no_return-warnings)
    - [Sanitizer Mismatch Errors](#sanitizer-mismatch-errors)
  - [References](#references)

## Building with Sanitizers

### Summary

Follow the same instructions as mentioned in [BUILD.md](../../BUILD.md) but with the following changes:

1. Make sure you have a clean build directory.
2. Set the `SANITIZERS` environment variable before calling conan install and cmake. Only set it once. Make sure both conan and cmake read the same values.
   Example: `export SANITIZERS=address,undefinedbehavior`
3. Optionally use `--profile:all sanitizers` with Conan to build dependencies with sanitizer instrumentation. [!NOTE]Building with sanitizer-instrumented dependencies is slower but produces fewer false positives.
4. Set `ASAN_OPTIONS`, `LSAN_OPTIONS`, `UBSAN_OPTIONS` and `TSAN_OPTIONS` environment variables to configure sanitizer behavior when running executables. [More details below](#running-tests-with-sanitizers).

---

### Build steps:

```bash
cd /path/to/rippled
rm -rf .build
mkdir .build
cd .build
```

#### Install dependencies

The `SANITIZERS` environment variable is used by both Conan and CMake.

```bash
export SANITIZERS=address,undefinedbehavior
# Standard build (without instrumenting dependencies)
conan install .. --output-folder . --build missing --settings build_type=Debug

# Or with sanitizer-instrumented dependencies (takes longer but fewer false positives)
conan install .. --output-folder . --profile:all sanitizers --build missing --settings build_type=Debug
```

[!CAUTION]
Do not mix Address and Thread sanitizers - they are incompatible.

Since you already set the `SANITIZERS` environment variable when running Conan, same values will be read for the next part.

#### Call CMake

```bash
cmake .. -G Ninja \
    -DCMAKE_TOOLCHAIN_FILE:FILEPATH=build/generators/conan_toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Debug \
    -Dtests=ON -Dxrpld=ON
```

#### Build

```bash
cmake --build . --parallel 4
```

## Running Tests with Sanitizers

### AddressSanitizer (ASAN)

**IMPORTANT**: ASAN with Boost produces many false positives. Use these options:

```bash
export ASAN_OPTIONS="include=sanitizers/suppressions/runtime-asan-options.txt:suppressions=sanitizers/suppressions/asan.supp"
export LSAN_OPTIONS="include=sanitizers/suppressions/runtime-lsan-options.txt:suppressions=sanitizers/suppressions/lsan.supp"

# Run tests
./xrpld --unittest --unittest-jobs=5
```

**Why `detect_container_overflow=0`?**

- Boost intrusive containers (used in `aged_unordered_container`) trigger false positives
- Boost context switching (used in `Workers.cpp`) confuses ASAN's stack tracking
- Since we usually don't build Boost (because we don't want to instrument Boost and detect issues in Boost code) with ASAN but use Boost containers in ASAN instrumented rippled code, it generates false positives.
- Building dependencies with ASAN instrumentation reduces false positives. But we don't want to instrument dependencies like Boost with ASAN because it is slow (to compile as well as run tests) and not necessary.
- See: https://github.com/google/sanitizers/wiki/AddressSanitizerContainerOverflow
- More such flags are detailed [here](https://github.com/google/sanitizers/wiki/AddressSanitizerFlags)

### ThreadSanitizer (TSan)

```bash
export TSAN_OPTIONS="include=sanitizers/suppressions/runtime-tsan-options.txt:suppressions=sanitizers/suppressions/tsan.supp"

# Run tests
./xrpld --unittest --unittest-jobs=5
```

More details [here](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual).

### LeakSanitizer (LSan)

LSan is automatically enabled with ASAN. To disable it:

```bash
export ASAN_OPTIONS="detect_leaks=0"
```

More details [here](https://github.com/google/sanitizers/wiki/AddressSanitizerLeakSanitizer).

### UndefinedBehaviorSanitizer (UBSan)

```bash
export UBSAN_OPTIONS="include=sanitizers/suppressions/runtime-ubsan-options.txt:suppressions=sanitizers/suppressions/ubsan.supp"

# Run tests
./xrpld --unittest --unittest-jobs=5
```

More details [here](https://clang.llvm.org/docs/undefinedbehaviorSanitizer.html).

## Suppression Files

[!NOTE] Attached files contain more details.

### [`asan.supp`](../../sanitizers/suppressions/asan.supp)

- **Purpose**: Suppress AddressSanitizer (ASAN) errors only
- **Format**: `interceptor_name:<pattern>` where pattern matches file names. Supported suppression types are:
  - interceptor_name
  - interceptor_via_fun
  - interceptor_via_lib
  - odr_violation
- **More info**: [AddressSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizer)
- **Note**: Cannot suppress stack-buffer-overflow, container-overflow, etc.

### [`lsan.supp`](../../sanitizers/suppressions/lsan.supp)

- **Purpose**: Suppress LeakSanitizer (LSan) errors only
- **Format**: `leak:<pattern>` where pattern matches function/file names
- **More info**: [LeakSanitizer](https://github.com/google/sanitizers/wiki/AddressSanitizerLeakSanitizer)

### [`ubsan.supp`](../../sanitizers/suppressions/ubsan.supp)

- **Purpose**: Suppress undefinedbehaviorSanitizer errors
- **Format**: `<error_type>:<pattern>` (e.g., `unsigned-integer-overflow:protobuf`)
- **Covers**: Intentional overflows in sanitizers/suppressions libraries (protobuf, gRPC, stdlib)
- More info [UBSan suppressions](https://clang.llvm.org/docs/SanitizerSpecialCaseList.html).

### [`tsan.supp`](../../sanitizers/suppressions/tsan.supp)

- **Purpose**: Suppress ThreadSanitizer data race warnings
- **Format**: `race:<pattern>` where pattern matches function/file names
- **More info**: [ThreadSanitizer suppressions](https://github.com/google/sanitizers/wiki/ThreadSanitizerSuppressions)

### [`sanitizer-ignorelist.txt`](../../sanitizers/suppressions/sanitizer-ignorelist.txt)

- **Purpose**: Compile-time ignorelist for all sanitizers
- **Usage**: Passed via `-fsanitize-ignorelist=absolute/path/to/sanitizer-ignorelist.txt`
- **Format**: `<level>:<pattern>` (e.g., `src:Workers.cpp`)

## Troubleshooting

### "ASAN is ignoring requested \_\_asan_handle_no_return" warnings

These warnings appear when using Boost context switching and are harmless. They indicate potential false positives.

### Sanitizer Mismatch Errors

If you see undefined symbols like `___tsan_atomic_load` when building with ASAN:

**Problem**: Dependencies were built with a different sanitizer than the main project.

**Solution**: Rebuild everything with the same sanitizer:

```bash
rm -rf .build
# Then follow the build instructions above
```

Then review the log files: `asan.log.*`, `ubsan.log.*`, `tsan.log.*`

## References

- [AddressSanitizer Wiki](https://github.com/google/sanitizers/wiki/AddressSanitizer)
- [AddressSanitizer Flags](https://github.com/google/sanitizers/wiki/AddressSanitizerFlags)
- [Container Overflow Detection](https://github.com/google/sanitizers/wiki/AddressSanitizerContainerOverflow)
- [UndefinedBehavior Sanitizer](https://clang.llvm.org/docs/UndefinedBehaviorSanitizer.html)
- [ThreadSanitizer](https://github.com/google/sanitizers/wiki/ThreadSanitizerCppManual)
