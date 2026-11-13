# Clang-tidy

All code must pass `clang-tidy` checks according to the settings in [`.clang-tidy`](../../.clang-tidy).

There is a Continuous Integration job that runs clang-tidy on pull requests. The CI will check:

- All changed C++ files (`.cpp`, `.h`, `.ipp`) when only code files are modified
- **All files in the repository** when the `.clang-tidy` configuration file is changed

This ensures that configuration changes don't introduce new warnings across the codebase.

## Installing clang-tidy

The [Nix development shell](../build/nix.md) provides `clang-tidy` at the [tested version](../build/environment.md#tested-compiler-versions).

## Running clang-tidy locally

Before running clang-tidy, you must generate the files it depends on (protobuf headers, and, when the project is configured with `-Drust=ON`, the cxxbridge headers from the Rust crates). Configure the project as described in [`BUILD.md`](../../BUILD.md), then build the `tidy_prerequisites` target, which generates all of them:

```bash
cmake --build build --target tidy_prerequisites
```

### Via pre-commit (recommended)

If you have already installed the [`pre-commit`](../../CONTRIBUTING.md#pre-commit-hooks) hooks, you can run clang-tidy on your staged files using:

```
TIDY=1 pre-commit run clang-tidy
```

This runs clang-tidy locally with the same configuration/flags as CI, scoped to your staged C++ files. The `TIDY=1` environment variable is required to opt in — without it the hook is skipped.

You can also have clang-tidy run automatically on every `git commit` by setting `TIDY=1` in your shell environment:

```
export TIDY=1
```

With this set, the hook will run as part of `git commit` alongside the other pre-commit checks.

### Manually

Then run clang-tidy on your local changes:

```
run-clang-tidy -p build -allow-no-checks include src tests
```

This will check all source files in the `include`, `src` and `tests` directories using the compile commands from your `build` directory.
Headers are checked on their own, which requires the `verify_headers` CMake option (ON by default).
If you wish to automatically fix whatever clang-tidy finds _and_ is capable of fixing, add `-fix -format` to the above command:

```
run-clang-tidy -p build -quiet -fix -format -allow-no-checks include src tests
```

`-format` reformats the fixed code with [`.clang-format`](../../.clang-format); without it the fixes are inserted in LLVM style and the `clang-format` hook rewrites them afterwards.
