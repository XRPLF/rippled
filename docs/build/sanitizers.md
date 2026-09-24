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
> The `ubuntu-clang-debug-amd64-tsan` CI config runs TSan in the extended matrix
> only, which is the nightly schedule and a manual run, and it reports nothing
> back. `runtime-tsan-options.txt` sets `halt_on_error=false`, so the run
> continues past a finding, and the workflow appends `exitcode=0` to
> `TSAN_OPTIONS`, so the finding does not fail the job.
> Both are needed: TSan exits 66 on its own once it has reported anything. Read
> the job log to see findings. A test that must fail on one has to run in its own
> step with `halt_on_error=1` and `exitcode=66`.
>
> `exitcode=0` lives in the workflow and not in `runtime-tsan-options.txt`,
> because the local command above reads that same file. A local run keeps the
> default nonzero exit, so a finding fails the command. `exitcode=0` also only
> covers TSan's own exit path: a test that fails on its own still fails the job.
>
> Reporting nothing back is a first stage, not the end state. Once the set of
> findings the job produces is known and stable, drop `exitcode=0` from the
> workflow so that a new finding fails the job, and suppress what is left in
> third-party code.

> [!IMPORTANT]
> Run TSan on Linux to check lock order. Linux reports an inversion as
> `WARNING: ThreadSanitizer: lock-order-inversion (potential deadlock)`, with no
> `TSAN_OPTIONS` needed. macOS arm64 reports nothing, not even a genuine double
> lock of a non-recursive mutex, and not with `detect_deadlocks=1` or raw
> `pthread_mutex_t` either. The report strings are present in its runtime and the
> flag defaults to true, so that is a platform limit rather than a configuration
> mistake. Data race detection does work on macOS.

> [!TIP]
> The build defines `XRPL_TSAN` when TSan is active, and `XRPL_ASAN` and
> `XRPL_UBSAN` for the other two. Use them to skip a test that only means
> something under one sanitizer. Skip at run time rather than with `#ifdef`
> around the body, so that every build still compiles the test.

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
- **Note**: Every `deadlock:` pattern must name a source file. One that names a
  locking primitive instead, such as `pthread_rwlock_rdlock`, turns lock-order
  checking off for every lock of that kind in the tree, which for that example is
  every `std::shared_mutex` read lock. Suppress the file that reports the
  inversion instead. A pattern of any type that names a file which has since
  moved matches nothing, so check the paths when a subsystem is relocated.

### [`sanitizer-ignorelist.txt`](../../sanitizers/suppressions/sanitizer-ignorelist.txt)

- **Purpose**: Compile-time ignorelist for all sanitizers
- **Usage**: Passed via `-fsanitize-ignorelist=absolute/path/to/sanitizer-ignorelist.txt`
- **Format**: `<entity>:<glob>` (e.g. `src:*Workers.cpp`)
- **Note**: This file is not a suppressions file, and the syntax differs. Clang
  looks up only the entities `src`, `fun`, `global`, `type` and `mainfile`. A
  `race:`, `deadlock:` or `signal:` entry parses without an error and is then
  never consulted, so it does nothing; those types belong in `tsan.supp`.
- **Note**: The glob must match the whole path as the compiler receives it, which
  the build makes absolute. So `src:core/detail/Workers.cpp` matches nothing,
  while `src:*core/detail/Workers.cpp` matches.
- **Note**: To confirm that an entry works, compile the file with and without the
  entry and compare how many times the object references the calls the sanitizer
  plants at an instrumented operation: `__tsan_read` and `__tsan_write`,
  `__asan_report`, or `__ubsan_handle`. For example
  `nm -u <file>.o | grep -cE '__tsan_(read|write)'`. A working entry lowers the
  count, usually to zero, though code inlined from a header still counts because
  a `src:` glob matches the file a function is defined in. Do not look for
  `__tsan_func_entry`, which survives an ignorelist, or `__tsan_init`, which only
  shows the runtime is present; either one reports an ignored file as
  instrumented.

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
