# `include/xrpl/git/Git.h` — Build-Time Git Provenance Interface

This header is the public face of a narrow compile-time provenance mechanism: it gives the rest of the `rippled` codebase a stable, type-safe API to query the exact git commit and branch that produced the running binary.

## Why It Exists

Distributed ledger software is particularly sensitive to version skew. Operators running `rippled` nodes need to confirm exactly which source revision is deployed, and the RPC layer needs to surface that information to network monitoring tools. Rather than embedding raw preprocessor macros throughout the codebase, the `xrpl::git` namespace wraps those macros behind two well-typed functions, hiding all build-system coupling behind a single include.

## The Interface

```cpp
namespace xrpl::git {
    std::string const& getCommitHash();
    std::string const& getBuildBranch();
}
```

Both functions return `const` references to process-lifetime strings. The implementation in `src/libxrpl/git/Git.cpp` backs each with a `static const std::string`, so the reference is always valid and the heap allocation happens at most once per process. The choice of `const&` over value return is a small but deliberate efficiency — callers like `NetworkOPs` call these functions repeatedly while building JSON responses, and avoiding copies is natural given the values never change.

## Build-System Wiring

The real work happens in `cmake/GitInfo.cmake`, which is included once (guarded by `include_guard()`) during CMake configuration. It shells out to `git rev-parse HEAD` and `git rev-parse --abbrev-ref HEAD` at *configure time*, captures the output into CMake variables `GIT_COMMIT_HASH` and `GIT_BUILD_BRANCH`, and propagates them as preprocessor definitions to the `Git.cpp` translation unit. If git is not installed, both variables are set to empty strings with a warning rather than failing the build.

The implementation file enforces this contract with hard `#error` directives: if either macro is absent at compile time, the build fails immediately rather than silently producing a binary with missing provenance. This is a deliberate fail-loud design — an accidental misconfiguration cannot produce a quietly wrong binary.

## Callers

There are two call sites in the codebase. `src/xrpld/app/main/Main.cpp` prints both values to standard output when `--version` is passed on the command line, alongside `BuildInfo::getVersionString()`. This is the primary operator-facing diagnostic path. `src/xrpld/app/misc/NetworkOPs.cpp` includes both values in the JSON object returned by the `server_info` RPC command under the `git` key, but only when the strings are non-empty — a defensive guard that handles the "git not found at configure time" case gracefully at runtime without crashing or emitting null fields.

## Design Notes

The separation of the header from `BuildInfo.h` (`src/libxrpl/protocol/BuildInfo.cpp`) is intentional: `BuildInfo` tracks the semantic version string and amendment information, while `xrpl::git` tracks raw VCS state. The two concerns are independently useful and independently testable. Keeping the git provenance in its own `xrpl::git` namespace signals clearly that this information comes from a different source (the VCS) than the manually maintained version number.

The configure-time capture means the strings reflect the tree state at CMake invocation, not at compile or link time. In a typical CI pipeline these are effectively the same moment, but developers who reconfigure without re-running git operations could observe stale values — a known, accepted trade-off of keeping the CMake logic simple.