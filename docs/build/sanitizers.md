# Sanitizer Configuration for Xrpld

This document explains how to properly configure and run sanitizers (`AddressSanitizer`, `UndefinedBehaviorSanitizer`, `ThreadSanitizer`) with the xrpld project.
Corresponding suppression files are located in the `sanitizers/suppressions` directory.

> [!CAUTION]
> Do not mix Address and Thread sanitizers - they are incompatible.
> Also, we don't yet support MSVC sanitizers, so this is only for Clang/GCC builds.

- [Sanitizer Configuration for Xrpld](#sanitizer-configuration-for-xrpld)
  - [Building with Sanitizers](#building-with-sanitizers)
    - [Summary](#summary)
    - [Build steps:](#build-steps)
      - [Install dependencies](#install-dependencies)
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
2. Set the `SANITIZERS` environment variable before calling `conan install`. Only set it once.
   Example: `export SANITIZERS=address,undefinedbehavior`
3. Use `--profile:all sanitizers` with Conan to build dependencies with sanitizer instrumentation.

   > [!NOTE]
   > Building with sanitizer-instrumented dependencies is slower but produces fewer false positives.

   > [!NOTE]
   > The profile adds `-O1` ahead of the build type's own flags, so a Release
   > build keeps its `-O3` and a clang Debug build compiles at `-O1`. A gcc Debug
   > build compiles at `-O0`, which `cmake/XrplCompiler.cmake` adds after the
   > profile's flags.

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

The `SANITIZERS` environment variable is used during `conan install` command.

```bash
SANITIZERS=address,undefinedbehavior conan install .. --output-folder . --build missing --settings build_type=Debug --profile:all sanitizers
```

Proceed with the rest of the build instructions as mentioned in [BUILD.md](../../BUILD.md).

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

- Boost intrusive containers (used in `AgedUnorderedContainer`) trigger false positives
- Boost context switching (used in `Workers.cpp`) confuses ASAN's stack tracking
- Since we usually don't build Boost (because we don't want to instrument Boost and detect issues in Boost code) with ASAN but use Boost containers in ASAN instrumented xrpld code, it generates false positives.
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

> [!IMPORTANT]
> The `debian-clang-release-amd64-tsan` CI config lists only the `schedule`
> and `workflow_dispatch` events, so TSan runs on the Trigger workflow's weekday
> schedule and on a manual run of it, not on a pull request, in the merge queue
> or in a tag build. A finding cannot fail the job yet:
> `halt_on_error=false` continues past it, and the workflow adds `exitcode=0`
> to `TSAN_OPTIONS`. The job counts the findings into a warning annotation and
> uploads the test logs as the `tsan-logs-<config>` artifact. A crash under TSan
> does fail the job, since the workflow also sets `abort_on_error=1`. Once the
> findings are known and stable, drop `exitcode=0` so a new one fails the job,
> and suppress the rest.

> [!IMPORTANT]
> Check lock order on Linux, where TSan reports
> `WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock)`. macOS
> arm64 reports no inversion, even for a double lock of a non-recursive mutex,
> though it does report races.

> [!TIP]
> The build defines `XRPL_ASAN`, `XRPL_TSAN` or `XRPL_UBSAN` per active
> sanitizer (`address`, `thread` or `undefinedbehavior`). Test them with
> `#ifdef` inside a test body to skip a check that only applies under one
> sanitizer, so every build still compiles the test.

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

- **Purpose**: Suppress ThreadSanitizer warnings
- **Format**: `<type>:<pattern>` where pattern matches function/file names, and
  type is `race`, `deadlock`, `signal`, `mutex` or `called_from_lib`
- **More info**: [ThreadSanitizer suppressions](https://github.com/google/sanitizers/wiki/ThreadSanitizerSuppressions)

### [`sanitizer-ignorelist.txt`](../../sanitizers/suppressions/sanitizer-ignorelist.txt)

- **Purpose**: Compile-time ignorelist for all sanitizers
- **Usage**: Passed via `-fsanitize-ignorelist=absolute/path/to/sanitizer-ignorelist.txt`
- **Format**: `<entity>:<glob>` (e.g. `src:*Workers.cpp`)
- **Note**: Not a suppressions file. Clang looks up only the entities `src`,
  `fun`, `global`, `type` and `mainfile`, so a `race:` or `deadlock:` entry
  parses but does nothing. Those belong in `tsan.supp`.
- **Note**: The glob must match the whole absolute path the compiler receives.
  `src:core/detail/Workers.cpp` matches nothing; `src:*core/detail/Workers.cpp`
  does.
- **Note**: To confirm an entry works, compile the file with and without it and
  compare the calls the sanitizer planted, e.g.
  `nm -u <file>.o | grep -cE '__tsan_(read|write)'`. Ignore `__tsan_func_entry`
  and `__tsan_init`, which an ignored file still has.

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
