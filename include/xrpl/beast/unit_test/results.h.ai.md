# `include/xrpl/beast/unit_test/results.h`

## Purpose

This file defines the three-level data hierarchy used to persist test outcomes after a test run completes in the beast unit test framework. The three classes — `case_results`, `suite_results`, and `results` — correspond directly to the three organizational levels of the framework: individual test conditions grouped into named test cases, test cases grouped into suites, and all suites collected into a single top-level report.

These types are pure passive data holders. They store what happened; they do not participate in running tests or producing output. That separation is what allows different consumers (the `recorder` that retains all detail, or the streaming `reporter` that discards it) to exist independently.

## Three-Level Hierarchy

### `case_results`

A `case_results` holds the accumulated data for one named test case within a suite. It contains two public "memberspaces": `tests` (of private type `tests_t`) and `log` (of private type `log_t`). The term memberspace is the author's idiom for a public member object used to scope related operations — externally these look like `m_case.tests.pass()` and `m_case.log.insert(s)`, keeping the mutation API explicit while avoiding free functions or polluting the outer namespace.

Both `tests_t` and `log_t` derive from `detail::const_container<std::vector<...>>`. The key property of `const_container` is that it exposes only `const_iterator` to external callers, making the vector contents read-only from outside the class — but its `cont()` accessor is `protected`, available to derived classes, so `tests_t` and `log_t` can mutate their vectors internally through `pass()`, `fail()`, and `insert()`. The individual test result is the nested `case_results::test` struct — simply a `bool pass` and an optional `std::string reason` for failures.

`tests_t` maintains a separate `failed_` counter that it increments in `fail()` and reads back in O(1) via `failed()`. This avoids iterating the full vector to count failures when computing suite totals.

### `suite_results`

A `suite_results` is itself a `const_container<std::vector<case_results>>` and adds `total_` and `failed_` counters. These are aggregated eagerly in both `insert()` overloads — when a `case_results` is pushed in, its `tests.total()` and `tests.failed()` values are immediately folded into the suite totals. This design means `suite_results::total()` and `suite_results::failed()` are always O(1), regardless of how many cases have been inserted. The `size()` method inherited from `const_container` returns the number of test cases (not conditions), which is how `results` tracks its `m_cases` counter.

### `results`

The top-level `results` class follows the same pattern: it is a `const_container<std::vector<suite_results>>` and carries its own `total_`, `failed_`, and `m_cases` counters updated during `insert()`. Note that `m_cases` accumulates `r.size()` (the number of `case_results` items in the incoming suite), while `total_` and `failed_` accumulate condition counts — giving callers three distinct levels of granularity: number of conditions, number of cases, and (implicitly through `size()`) number of suites.

The naming inconsistency — `m_cases` using the old `m_` Hungarian prefix while the same class uses `total_` and `failed_` with a trailing underscore — is a minor historical artifact with no functional consequence.

## Move and Copy Insert Overloads

Both `suite_results::insert()` and `results::insert()` provide rvalue and lvalue overloads. The rvalue paths use `emplace_back(std::move(r))`, which matters because both `suite_results` and `results` contain vectors of potentially large nested objects. The `recorder` class, which builds the hierarchy through runner callbacks, always uses the move overload (`m_results.insert(std::move(m_suite))`), so in practice the copy path is a correctness fallback rather than a performance path.

There is a subtle ordering issue in the move overloads worth noting: both `suite_results::insert(case_results&&)` and `results::insert(suite_results&&)` read aggregate counts from `r` *before* moving it, which is correct since the move constructor for these standard containers leaves the source in a valid-but-empty state after the counts are captured.

## Relationship to `recorder` and `runner`

The `recorder` class in `recorder.h` is the primary consumer. It holds one `results`, one `suite_results`, and one `case_results` as live accumulators. The runner's virtual callbacks drive mutations — `on_suite_begin` resets `m_suite`, `on_case_end` inserts the completed `m_case` into `m_suite`, `on_suite_end` inserts `m_suite` into `m_results`. After the run completes, `recorder::report()` returns a `const results&` for post-run inspection.

The `reporter` in `reporter.h` intentionally does not use these classes — it defines its own local shadow types with the same names inside a template scope. The reporter is designed for streaming output during a run and discards individual test records, so it has no need for the full storage these classes provide.

## Read-Only External Interface

The `const_container` base is the linchpin of the access model. External consumers iterate over suites, cases, and individual `test` structs using range-for or the provided `begin()`/`end()` iterators, all of which are `const_iterator`. Mutation is only available through the explicit `insert()`, `pass()`, `fail()`, and `log.insert()` methods, which are defined on the concrete classes, not exposed through the base. This creates a clear invariant: once the runner has finished and the `results` object is handed to a reporting consumer, that consumer cannot accidentally mutate it.