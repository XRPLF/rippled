# Fuzz targets

In-process, coverage-guided fuzz targets for libxrpl, driven by libFuzzer.

## Layout

- `FuzzTargets.h`, `FuzzTargets.cpp`: the target registry. Each target has a name, a
  function that consumes one input, and a function that builds its seed inputs in code.
- `FuzzCheck.h`: shared oracle helpers (`check`, `tryOrEmpty`, `checkSerializationFixedPoint`).
- `targets/`: one file per target.
- `main/FuzzMain.cpp`: the libFuzzer entry point. Built only into `xrpl_fuzz`.
- `Fuzz_test.cpp`: the `FuzzReplay` unit test, which runs every target's seeds in a
  normal `xrpld --unittest` run, on any compiler.

A target returns 0 for every input, including ones the parser rejects. It reports a bug
by crashing (sanitizer, `XRPL_ASSERT`) or by throwing `OracleFailure`.

## Building

Fuzzing needs LLVM Clang on Linux, at least one sanitizer (see
[docs/build/sanitizers.md](../../../docs/build/sanitizers.md)), and asserts. Build inside
the Clang dev shell (`nix develop .#clang`), which provides a Clang whose libstdc++ and
compiler-rt match the project. Use a separate build directory, because the dependencies
are built with sanitizers:

```
nix develop .#clang
SANITIZERS=address,undefinedbehavior conan install . --output-folder .build-fuzz \
    --build missing --settings build_type=Debug --profile:all sanitizers
cmake -B .build-fuzz -G Ninja -DCMAKE_TOOLCHAIN_FILE=build/generators/conan_toolchain.cmake \
    -DCMAKE_BUILD_TYPE=Debug -Dfuzzing=ON -Dxrpld=OFF -Dtests=OFF -Dbenchmark=OFF
cmake --build .build-fuzz --target xrpl_fuzz
```

Before running, load the project's sanitizer suppressions. Without them, UBSan stops on
known reports in Boost before the first input runs:

```
export ASAN_OPTIONS="include=sanitizers/suppressions/runtime-asan-options.txt:suppressions=sanitizers/suppressions/asan.supp"
export LSAN_OPTIONS="include=sanitizers/suppressions/runtime-lsan-options.txt:suppressions=sanitizers/suppressions/lsan.supp"
export UBSAN_OPTIONS="include=sanitizers/suppressions/runtime-ubsan-options.txt:suppressions=sanitizers/suppressions/ubsan.supp"
```

## Running

`XRPL_FUZZ_TARGET` selects the target. With `XRPL_FUZZ_SEED_DIR` set, the binary first
writes the target's seeds into that directory; pass the same directory as the corpus so
libFuzzer starts from them:

```
XRPL_FUZZ_TARGET=st_validation XRPL_FUZZ_SEED_DIR=/tmp/corpus-st_validation \
    .build-fuzz/xrpl_fuzz -timeout=5 -rss_limit_mb=2048 /tmp/corpus-st_validation
```

To reproduce a crash, pass the crash file instead of a directory:

```
XRPL_FUZZ_TARGET=st_validation .build-fuzz/xrpl_fuzz crash-<hash>
```

## After a finding

Minimize the input (`-minimize_crash=1 -runs=100000`), fix the bug, and add a unit test
that passes the minimized bytes, written as a hex literal, to the target's function, so
the input is replayed on every run from then on.
