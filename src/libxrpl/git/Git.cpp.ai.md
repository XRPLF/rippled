# `src/libxrpl/git/Git.cpp` — Build-time Git Metadata Provider

This file solves a narrow but important problem: making the exact source identity of a running `rippled` binary inspectable at runtime. It provides two accessor functions that return the Git commit hash and branch name baked into the binary at compile time, enabling diagnostic output, version strings, and log messages to carry precise provenance information.

## Compile-time Injection via Preprocessor Macros

The entire mechanism relies on two preprocessor macros, `GIT_COMMIT_HASH` and `GIT_BUILD_BRANCH`, being defined before the translation unit is compiled. The file enforces this contract hard — both macros are guarded by `#ifndef`/`#error` directives, so any attempt to compile without them is an immediate build failure rather than a silent empty string. This is the right design choice: a build that silently omits version information is worse than one that refuses to compile, because silently empty version strings would make deployed binaries indistinguishable from each other in production logs.

The macros are populated by `cmake/GitInfo.cmake`, which is included early in the build via `cmake/XrplCore.cmake`. That CMake script shells out to `git rev-parse HEAD` for the commit hash and `git rev-parse --abbrev-ref HEAD` for the branch, then passes the resulting strings to the compiler as `target_compile_definitions` on the `xrpl.libxrpl.git` module. If Git is not found on the build host, CMake emits a warning and leaves both variables as empty strings — which will still satisfy the preprocessor guard (the macros are defined, just empty), though the resulting version information will be uninformative.

## Static Local String Pattern

Each accessor function — `getCommitHash()` and `getBuildBranch()` — follows the same two-step initialization pattern. First, the macro value is captured into a `static constexpr char[]` array at namespace scope; then inside each function, a `static const std::string` is initialized from that array on first call and returned by `const&` on every subsequent call.

The intermediate `constexpr` char array (`kGIT_COMMIT_HASH`, `kGIT_BUILD_BRANCH`) is not strictly required — the macro could be used directly in the function — but it serves as a named, typed binding that makes the value inspectable in a debugger and removes the raw macro token from the function body. The function-local `static std::string` is the canonical C++ idiom for a lazily initialized singleton: it is thread-safe since C++11 (the standard guarantees exactly-once initialization for function-local statics under concurrent access), and it avoids the static-initialization-order-fiasco that would arise from a global `std::string`. Returning by `const&` to the static instance avoids copying and lets callers hold a stable reference.

## Usage in the Broader System

`BuildInfo.cpp` calls `xrpl::git::getCommitHash()` when constructing the version metadata string appended to the version number in debug and sanitizer builds. This surfaces in the `server_info` RPC response and in startup log messages, allowing operators and developers to match a running node's behavior directly to a specific commit. The `getBuildBranch()` function provides complementary context — knowing the branch name distinguishes release builds from development or feature-branch builds even when two commits have similar hashes.

The `xrpl::git` namespace cleanly separates this low-level build metadata from higher-level version policy in `BuildInfo`, reflecting a correct layering: `BuildInfo` owns the semantics of the version string, while `Git.cpp` owns only the raw source-control facts.