# `include/xrpl/beast/utility/instrumentation.h`

## Role and Purpose

This header is the single point of control for assertion and fuzzing-instrumentation macros across the XRPL codebase. It serves two distinct audiences simultaneously: the production codebase, where the macros behave like hardened `assert` calls, and the [Antithesis](https://antithesis.com/) continuous fuzzing platform, where the same macros become first-class *test properties* that the fuzzer can observe, track, and try to violate.

The file contains no classes or functions — it is entirely macro-driven and pulls in at most one external header.

## Two Compile-Time Personalities

The entire file pivots on a single preprocessor guard:

```cpp
#ifdef ENABLE_VOIDSTAR
#ifdef NDEBUG
#error "Antithesis instrumentation requires Debug build"
#endif
#include <antithesis_sdk.h>
#else
// fallback definitions using assert(...)
#endif
```

When `ENABLE_VOIDSTAR` is defined (activated by the `-Dvoidstar=ON` CMake option in CI on Linux/amd64 with Clang 16), the vendored `external/antithesis-sdk/antithesis_sdk.h` is included. This SDK — which requires C++20 and Clang ≥ 16 — instruments the binary so that when `xrpld` runs under the Antithesis platform, every contract call is forwarded via `libvoidstar.so` (loaded at `/usr/lib/libvoidstar.so`). The platform then treats each contract as a trackable property it can try to falsify.

The `NDEBUG` guard enforces that Antithesis mode is Debug-only: the SDK's contract tracking is meaningless in an optimized build where code paths are eliminated.

In all other configurations (Release builds, Windows/MSVC toolchains, developer machines) the else-branch provides local fallback stubs. The comment explains the duplication: "Visual Studio 2019 cannot compile that header even with the option `-Zc:__cplusplus` added." Rather than add a MSVC compatibility shim to the upstream SDK, the project simply copies the simplified fallback forms inline.

## The Macro Set

The fallback definitions reveal the contract semantics clearly:

- **`ALWAYS(cond, message, ...)`** — asserts the condition is true *and* signals to the fuzzer that this line must be reached. Fallback: `assert((message) && (cond))`. The message string is ANDed so that a failure displays the contract name before aborting.
- **`ALWAYS_OR_UNREACHABLE(cond, message)`** — same assertion, but does not require the line to be reached during fuzzing. This distinction matters in dead-code or rare-path guards.
- **`SOMETIMES(cond, message, ...)`** — a fuzzer *hint* only: "try to find an execution where this is true." Fallback is a complete no-op, which is correct — it has no runtime effect in normal builds.
- **`REACHABLE(message, ...)`** — tells the fuzzer that this line should be reachable. No-op in fallback.
- **`UNREACHABLE(message, ...)`** — fallback: `assert((message) && false)`. Critically, this does **not** map to `std::unreachable`: execution continues past a failed `UNREACHABLE` in both Release builds and during fuzzing, rather than triggering undefined behaviour or an immediate process abort. The semantics are "this situation is contractually impossible; if it happens, report it — but don't create UB."

## XRPL-Specific Aliases

`XRPL_ASSERT` and `XRPL_ASSERT_PARTS` are thin wrappers over `ALWAYS_OR_UNREACHABLE` that exist to enforce the project's contract-naming convention:

```cpp
#define XRPL_ASSERT ALWAYS_OR_UNREACHABLE
#define XRPL_ASSERT_PARTS(cond, function, description, ...) \
    XRPL_ASSERT(cond, function " : " description)
```

`XRPL_ASSERT_PARTS` is purely ergonomic: it separates the qualified function name from the brief description and joins them with `" : "` at the preprocessor level, producing the canonical form `"xrpl::LedgerTrie::insert : valid input ledger"`. In practice, both forms appear throughout the codebase; `LedgerTrie.h` for instance uses bare `XRPL_ASSERT` with pre-formatted string literals.

## Contract Naming as Stable Identity

The use of `ALWAYS_OR_UNREACHABLE` rather than `ALWAYS` for `XRPL_ASSERT` is intentional: many internal assertions guard conditions that may not be exercised in every fuzzing run, so demanding reachability would generate spurious failures. The "OR_UNREACHABLE" variant says "if this line is reached, the condition must hold" without penalising paths that skip it.

The mandatory unique name serves a purpose beyond documentation. Contract names are stable identifiers on the Antithesis platform — unlike line numbers, they survive refactoring. CONTRIBUTING.md mandates the form `"qualified::scope::function : brief description"` and explicitly warns against renaming contracts without cause, since doing so severs the historical record of whether that property has ever been violated. `XRPL_ASSERT_PARTS` makes it syntactically harder to accidentally produce a malformed name.

## Usage Boundaries

The project draws a clear line about where these macros apply. Regular `assert` and `assert(false)` remain correct inside `constexpr` functions (where macros cannot be evaluated at compile time), inside unit tests under `src/test`, and in beast test infrastructure — contexts where Antithesis property tracking is either impossible or undesirable. Everywhere else, `XRPL_ASSERT` replaces `assert` and `UNREACHABLE` replaces `assert(false)`, with `std::unreachable` explicitly forbidden throughout the codebase.