# C++ Guidelines

This is a non-exhaustive list of recommended style guidelines. They are not
always strictly enforced; they serve to keep the codebase coherent rather than
as a set of _thou shalt not_ commandments.

Existing code may violate these guidelines; such places are usually good
candidates for refactoring.

Formatting and static analysis are enforced by tooling: see
[Formatting](./formatting.md), and
[Pre-commit hooks](../../CONTRIBUTING.md#pre-commit-hooks) and
[Clang-tidy](../../CONTRIBUTING.md#clang-tidy) in `CONTRIBUTING.md`.

## Source layout

New source files must be organized as follows:

- Files in any of the `libxrpl` modules: headers (`.h`) go under
  `include/xrpl`, and source files (`.cpp`) go under `src/libxrpl`.
- All other non-test files go under `src/xrpld`.
- Benchmark source files go under `src/benchmarks`.
- Implementation details of a header (e.g. template definitions in an `.ipp`
  file) go in a `detail/` subdirectory next to that header, and in the
  `detail` namespace.
- Tests go under `src/tests` (gtest) or `src/test` (legacy test framework).

Header includes must be [levelized](../../.github/scripts/levelization).

## General

- Avoid out and in-out parameters. Use move semantics and return instead.
- Don't rely on empty value. Prefer `std::optional` instead.
- Prefer `std::format` to string concatenation.

## Headers and includes

- Use `#pragma once`; the `fix-pragma-once` pre-commit hook adds it if missing.
- Include what you use; clang-tidy enforces this with `misc-include-cleaner`.

## Error handling

- Avoid exceptions as much as possible. Throw only when there is no other way
  to report the error (e.g. from a constructor).
- Prefer `std::expected` for error handling.
- Use `noexcept` carefully. An exception thrown from a function/method marked as `noexcept` causes crash.

## Concurrency

- Use `xrpl::Mutex` (`include/xrpl/basics/Mutex.hpp`) instead of a plain
  `std::mutex` wherever possible.

## Naming

- Naming is mostly enforced by clang-tidy (`readability-identifier-naming`).
- Name getters with a noun, or with a `get`, `is` or `are` prefix.
- Name other methods with a verb or action.

## Attributes

- Mark a type `[[nodiscard]]` if its values should never be discarded.
  Functions returning such a type should also be marked `[[nodiscard]]`.

## Tests

- Write new tests with `gtest`. Use the legacy test framework only where
  `gtest` isn't possible.
- Use `snake_case` for gtest test names (the second argument of `TEST` and
  `TEST_F`).

## Contracts

- Use `XRPL_ASSERT` instead of plain `assert`, except in the locations listed
  in [Antithesis](./antithesis.md).
- Mark all unreachable code with `UNREACHABLE` and exclude it from code
  coverage, with `// LCOV_EXCL_LINE` on the same line or with
  `// LCOV_EXCL_START` and `// LCOV_EXCL_STOP` around the whole block.

See [Antithesis](./antithesis.md) for how contracts are named and described.
