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

- Don't rely on empty value. Prefer `std::optional` instead.
- Before writing a helper, check whether one already exists, e.g. call
  `getFlags()` rather than `getFieldU32(sfFlags)`. When two places must stay
  consistent, make both call one helper instead of keeping two copies in sync.
- Delete dead code rather than disabling it with `#if 0` or commenting it out.
  Don't add functions, overloads or parameters that nothing uses yet.
- Give magic numbers a named constant, or reuse the existing one.
- Define non-trivial (not one-liner) non-template functions in the `.cpp`
  file, not in the header.
- Before converting a value to a narrower or differently-signed integer type,
  check that it fits. Use `safeCast` (`include/xrpl/basics/safe_cast.h`) when
  every source value fits, so the compiler verifies it, and `unsafeCast` after
  checking the range when it doesn't.
- Fix the code rather than silence clang-tidy. When a `NOLINT` is unavoidable,
  name the check and say why it's needed.
- Don't use CRTP to call into the derived class; use an explicit object
  parameter (deducing `this`) instead. CRTP for per-type state, as in
  `CountedObject<T>`, is fine.

## Function parameters

- Avoid out and in-out parameters. Return by value instead, relying on RVO/NRVO and
  move semantics.
- Pass view types (`std::span`, `std::string_view`, `Slice`), iterators and
  `std::reference_wrapper` by value, not by `const&`.
- Use an `enum class` instead of a `bool` parameter or a sentinel argument
  (such as a `nullptr` that means "this object"), so call sites say what they
  mean:

  ```cpp
  enum class WaiveTransferFee : bool { No = false, Yes };

  accountSendMulti(view, sender, asset, receivers, j, WaiveTransferFee::Yes);
  ```

- Don't take a parameter that the function can derive from its other
  parameters or from the object's state; a caller can pass a value that
  disagrees with them.

## Standard library

- Prefer `std::ranges` algorithms to loops that reimplement them (`find_if`,
  `any_of`, `transform`, ...).
- Use `std::ranges::contains` rather than comparing the result of `find` with
  `end()` on a sequence. For associative containers, use their member
  `contains()`, which doesn't scan linearly.
- Use the C++23 range member functions (`insert_range`, `append_range`) rather
  than inserting an iterator pair.
- Use `std::to_array` to declare a `std::array` without spelling out its size.
- Prefer standard containers to their Boost equivalents (e.g. `std::flat_set`
  over `boost::container::flat_set`), unless the standard library doesn't have
  one yet.
- Prefer `std::format` to string concatenation.
- Use `std::filesystem::path` for filesystem paths, and join them with `/`
  rather than string concatenation.
- Use `std::size_t` for sizes, loop counters and indexes.
- Be careful when brace-initializing a type that has a `std::initializer_list`
  constructor, because braces prefer it: `std::vector<int>{3, 0}` holds two
  elements, while `std::vector<int>(3, 0)` holds three. Think twice before
  adding such a constructor to a type.

## Error handling

- Avoid exceptions as much as possible. Throw only when there is no other way
  to report the error (e.g. from a constructor).
- Prefer `std::expected` for error handling.
- Use `noexcept` carefully. An exception thrown from a function/method marked as `noexcept` causes crash.

## Resource management

- Restore state and release resources with RAII guards, not with manual calls
  at the end of a scope, which an early return, an exception or a failed
  `ASSERT_*` skips. Use the existing guards where they fit, e.g. `ScopeExit`
  and `ScopeUnlock` (`include/xrpl/basics/scope.h`).
- Return `std::unique_ptr` from factory functions. Return `std::shared_ptr`
  only when ownership is inherently shared: from a caching factory, which keeps
  its own reference to the objects it creates, or for objects that use
  `shared_from_this()`.

## Concurrency

- Use `xrpl::Mutex` (`include/xrpl/basics/Mutex.hpp`) instead of a plain
  `std::mutex` wherever possible.
- Hold a lock only while touching the state it protects. Allocate, log and do
  other expensive work before taking the lock or after releasing it (see
  `TaggedCache::getKeys()`), and don't copy under the lock what you could
  reference or move.

## Naming

- Name getters with a noun, or with a `get`, `is` or `are` prefix.
- Name other methods with a verb or action.
- Use descriptive names. Avoid single letters other than loop counters,
  unexplained abbreviations, and symbols copied from a spec.
- Name a function for everything it does, including side effects, and rename
  it when what it does changes.

## Comments

- Comment code whose purpose or behavior isn't obvious from reading it.
- Prefer clearer code to a comment: rename a function or variable, or extract
  a well-named helper or variable (e.g. for a complex condition), instead of
  explaining it in a comment.
- Describe the code as it is. Don't mention the PR, the issue, the old
  behavior or the diff; a reader of the merged code has none of them.
- Don't restate facts that go stale: lists of enumerators, counts, callers,
  constant values or spec section numbers. When you change behavior, update
  the comments that describe it.
- Keep comments short.
- Give every new public function or method a Doxygen comment. Document every
  `@tparam`, `@param` and `@return`, and separate the brief from the details
  with a blank line. Document each overload; use `@copydoc` for the parts they
  share.

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
  when test cases differ only in their input and expected output values. In
  the legacy framework, loop over a table of cases instead of copy-pasting
  blocks or passing `bool` flags in from `run()`.
- Do per-test setup and teardown in the fixture's constructor and destructor
  rather than in `SetUp()` and `TearDown()`, unless that isn't possible (see
  the [gtest FAQ](https://google.github.io/googletest/faq.html#CtorVsSetUp)).
- Before writing a test helper, look for an existing one in `src/test/jtx` or
  `src/tests/libxrpl/helpers`.
- Make sure a test can fail. Assert the effect (balances, ledger entries,
  output), not only the result code or that nothing threw, and check that a
  regression test fails without the fix.
- Derive expected values from the same named constants the code uses instead
  of repeating literals.
- Make a negative test break exactly one precondition, with every other input
  valid, so it fails for the reason it claims whatever order the checks run
  in.
- Use `ASSERT_*` for conditions the rest of the test depends on. A failed
  `ASSERT_*` returns only from the current function, so wrap calls to helpers
  that use it in `ASSERT_NO_FATAL_FAILURE`. In the legacy framework, write
  `if (!BEAST_EXPECT(cond)) return;`.
- Don't let a test's outcome depend on timing or thread scheduling. Don't
  sleep, poll wall-clock time or busy-wait. Use a manual clock
  (`beast::ManualClock`), `std::latch` or `JobQueue::rendezvous()`. When
  waiting for something that might never happen, use a wait with a timeout
  (e.g. `std::condition_variable::wait_for`) so the test fails instead of
  hanging.

## Contracts

- Use `XRPL_ASSERT` instead of plain `assert`, except in the locations listed
  in [Antithesis](./antithesis.md).
- Mark all unreachable code with `UNREACHABLE` and exclude it from code
  coverage, with `// LCOV_EXCL_LINE` on the same line or with
  `// LCOV_EXCL_START` and `// LCOV_EXCL_STOP` around the whole block.
- `XRPL_ASSERT` and `UNREACHABLE` don't stop a release build, so the code after
  them still runs. When guarding a condition that should never happen, still
  return an error (e.g. `tecINTERNAL` from a transactor's `doApply`), never
  success or a permissive default.

See [Antithesis](./antithesis.md) for how contracts are named and described.
