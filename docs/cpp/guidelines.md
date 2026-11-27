# C++ Guidelines

This is a non-exhaustive list of recommended style guidelines. They are not
always strictly enforced; they serve to keep the codebase coherent rather than
as a set of _thou shalt not_ commandments.

Existing code may violate these guidelines; such places are usually good
candidates for refactoring.

Formatting and static analysis are enforced by tooling: see
[Formatting](./formatting.md), [Clang-tidy](./clang-tidy.md), and
[Pre-commit hooks](../../CONTRIBUTING.md#pre-commit-hooks) in `CONTRIBUTING.md`.
Rules those tools enforce (e.g. `#pragma once`, include-what-you-use, identifier
naming, gtest test names) are not repeated here.

## Source layout

New source files must be organized as follows:

- Files in any of the `libxrpl` modules: headers (`.h`) go under
  `include/xrpl`, and source files (`.cpp`) go under `src/libxrpl`.
- All other non-test files go under `src/xrpld`.
- Benchmark source files go under `src/benchmarks`.
- Implementation details of a header (e.g. template definitions in an `.ipp`
  file) go in a `detail/` subdirectory next to that header.
- Helpers that exist only to implement a header go in the `detail` namespace.
- Tests go under `src/tests` (gtest) or `src/test` (legacy test framework).

Header includes must be [levelized](../../.github/scripts/levelization).

## General

- Avoid out and in-out parameters. Return by value instead, relying on RVO/NRVO and
  move semantics.
- Don't rely on empty value. Prefer `std::optional` instead.
- Prefer `std::format` to string concatenation.

## Error handling

- Avoid exceptions as much as possible. Throw only when there is no other way
  to report the error (e.g. from a constructor).
- Prefer `std::expected` for error handling.
- Use `noexcept` carefully. An exception thrown from a function/method marked as `noexcept` causes crash.

## Concurrency

- Use `xrpl::Mutex` (`include/xrpl/basics/Mutex.hpp`) instead of a plain
  `std::mutex` wherever possible.

## Naming

- Name getters with a noun, or with a `get`, `is` or `are` prefix.
- Name other methods with a verb or action.

## Attributes

- Mark a type `[[nodiscard]]` if its values should never be discarded.
  Functions returning such a type should also be marked `[[nodiscard]]`.

## Tests

- Write new tests with `gtest`. Use the legacy test framework only where
  `gtest` isn't possible.
- Declare a gtest fixture as a `struct` with only public members, derived from
  `::testing::Test` or from another fixture:

  ```cpp
  struct LedgerTest : ::testing::Test
  {
      // No private or protected sections.
  };
  ```

- Put test helper classes, constants and functions inside the fixture or in an
  anonymous namespace in the `.cpp` file. All gtest tests of a binary are
  linked together, so two same-named helpers at namespace scope in different
  test files are a silent ODR violation: the linker keeps one definition and
  both files use it.
- When several test cases repeat the same precondition setup, create a new
  fixture that does that setup in its constructor.
- Use
  [value-parameterized tests](https://google.github.io/googletest/advanced.html#value-parameterized-tests)
  when test cases differ only in their input and expected output values.
- Do per-test setup and teardown in the fixture's constructor and destructor
  rather than in `SetUp()` and `TearDown()`, unless that isn't possible (see
  the [gtest FAQ](https://google.github.io/googletest/faq.html#CtorVsSetUp)).

## Contracts

- Use `XRPL_ASSERT` instead of plain `assert`, except in the locations listed
  in [Antithesis](./antithesis.md).
- Mark all unreachable code with `UNREACHABLE` and exclude it from code
  coverage, with `// LCOV_EXCL_LINE` on the same line or with
  `// LCOV_EXCL_START` and `// LCOV_EXCL_STOP` around the whole block.

See [Antithesis](./antithesis.md) for how contracts are named and described.
