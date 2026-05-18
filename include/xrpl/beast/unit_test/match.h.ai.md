# `include/xrpl/beast/unit_test/match.h`

## Role and Purpose

This header provides the test-suite selection mechanism for the `beast::unit_test` framework embedded in the XRPL codebase. Its central abstraction is the `selector` class — a stateful, callable predicate that takes a `suite_info` object and returns `true` if that suite should be run. The `selector` is designed to be passed to a runner's iteration loop over a `suite_list`, filtering in or out individual test suites based on a user-supplied string pattern.

The file also exposes four convenience factory functions — `match_all()`, `match_auto()`, `match_suite()`, and `match_library()` — that construct `selector` instances with the appropriate mode, providing a readable API at call sites in `Main.cpp`.

## The `mode_t` Enum and Its Lifecycle

The `selector` carries a `mode_t` enum that governs exactly how each call to `operator()` is evaluated. Six modes are defined, but they split into two groups:

- **User-facing modes** (`all`, `automatch`, `suite`, `library`): constructed directly via the factory functions.
- **Internal transition modes** (`module`, `none`): entered only through state changes that `automatch` performs on itself during iteration.

This means a single `selector` object can change its own behavior mid-iteration. The `module` and `none` modes are not constructible directly via the public API — they emerge dynamically.

## `automatch`: Stateful Pattern Resolution

The most architecturally significant mode is `automatch`. Rather than statically committing to one field to compare, it applies a priority-ordered discovery process each time `operator()` is called:

1. **Exact suite name or full name** (`library.module.suite`) — if matched, the selector transitions to `none` and returns `true`. This is a one-shot match: subsequent calls always return `false`, ensuring exactly one suite runs.

2. **Module name** — if matched, the selector transitions to `module` mode and caches the suite's `library_`. Subsequent calls enter the `module` branch, matching all non-manual suites sharing that module name.

3. **Library name** — if matched, the selector transitions to `library` mode. Subsequent calls match all non-manual suites in that library.

4. **Prefix match** on suite name or full name — the selector stays in `automatch` and returns `true` for non-manual suites. No mode transition occurs, so the pattern continues to match prefixes across all remaining suites.

This cascade means the *first* association found dictates interpretation for all subsequent calls, an important subtlety when the pattern is ambiguous. A pattern like `"XRPL"` could be a module or library prefix; whichever is encountered first in sorted `suite_list` order wins.

The `automatch` constructor also handles the degenerate case: if the pattern is empty, it demotes itself to `all`, running all non-manual suites. This is the behavior when `rippled --unittest` is invoked with no argument.

## Manual Test Exclusion

Only two paths ever include manually-tagged suites:

- An exact `suite` mode match (where the user typed the exact name).
- An `automatch` exact name or full-name hit (the one-shot case).

All other paths — `all`, `library`, `module`, and prefix matches in `automatch` — gate on `!s.manual()`. This ensures that suites marked manual are invisible to broad sweeps and require explicit opt-in.

## Template `<class = void>` Pattern

Both the constructor and `operator()` use the `template <class = void>` idiom. This defers template instantiation, allowing the method bodies to live in a header without triggering ODR violations when multiple translation units include the file. It is functionally equivalent to an `inline` definition but relies on the template instantiation rules rather than `inline` linkage. This is a recurring pattern throughout the beast unit test headers.

## Usage in `Main.cpp`

The `multi_selector` class in `Main.cpp` wraps a `std::vector<selector>` to support comma-separated patterns passed to `--unittest`. Each token becomes an independent `automatch` selector. When `operator()` is called on `multi_selector`, it iterates the vector and returns `true` on the first individual selector that matches — providing logical OR semantics across patterns. This shows that `selector` is designed to be both composable and independently stateful, since each `selector` in the vector transitions its own mode independently.

## Relationship to `suite_info`

The `selector` is entirely dependent on `suite_info`'s public interface: `name()`, `module()`, `library()`, `full_name()`, and `manual()`. The full name is constructed as `library + "." + module + "." + name`, which the `automatch` exact match checks directly. This three-level hierarchy (library → module → suite) maps to the organisational structure of the test registry, and the `selector`'s mode cascade mirrors it precisely.