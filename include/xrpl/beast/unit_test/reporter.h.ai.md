# `include/xrpl/beast/unit_test/reporter.h`

## Role and Purpose

`reporter.h` provides a concrete, streaming test runner for the Beast unit test framework embedded in the XRPL codebase. Where the sibling `recorder` collects structured test results for later programmatic inspection, `reporter` is the human-facing counterpart: it writes progress directly to an `std::ostream` as tests execute and emits a formatted summary when destroyed. This real-time output model is deliberate — developers watching a long test run see each suite and case name immediately as it begins, and failures are printed inline rather than queued for a final dump.

## Class Hierarchy and the Template-Void Pattern

`reporter` extends `runner`, the abstract base that defines the event-notification interface (`on_suite_begin`, `on_case_begin`, `on_pass`, `on_fail`, `on_log`, etc.) and the thread-safe dispatch logic. Rather than being a plain class, `reporter` is declared as `template<class = void>`, with `using reporter = detail::reporter<>` aliased at namespace scope. This is a standard header-only library technique: by making the class a template, the full method bodies can be defined in the header without violating the One Definition Rule, since each translation unit will instantiate the same `reporter<void>` specialization. The real class lives in `namespace detail` and the public alias exposes it cleanly.

## Three-Level Aggregation

The class maintains three nested private structs that mirror the three tiers of the test hierarchy:

- **`case_results`** — tracks `total` assertions and `failed` assertions within a single named test case, reset on each `on_case_begin` call.
- **`suite_results`** — accumulates case counts and totals across all cases in a suite, plus a `start` timestamp captured at construction. Reset on each `on_suite_begin` call.
- **`results`** — the aggregate across all suites, also with a `start` timestamp for overall elapsed time.

This three-level nesting means no intermediate state is ever lost: the live `case_results_` member is merged into `suite_results_` when a case ends, which in turn is merged into `results_` when a suite ends. The three member variables (`results_`, `suite_results_`, `case_results_`) are mutable state in the reporter, updated by the virtual callbacks.

## Slowest-Suite Tracking

`results::add` implements a bounded top-10 list of the slowest suites. Only suites taking at least one second are candidates. The insertion uses `std::lower_bound` against a descending-sorted vector of `(name, duration)` pairs, maintaining sorted order by inserting at the correct position and trimming to `max_top = 10` entries when the list is full. This avoids sorting the entire list on every suite completion — an insertion into a 10-element vector is effectively free. If a suite finishes too slowly to displace any entry and the list is already full, it is simply discarded. The resulting list is printed in the destructor as "Longest suite times:".

## Destructor-Based Summary

The `~reporter()` destructor is responsible for printing the final summary line. This RAII pattern ensures the summary appears even if test execution exits early — as long as the `reporter` goes out of scope normally, the totals are printed. The summary uses the `amount` utility (from `amount.h`) for grammatically correct pluralization: `amount{n, "suite"}` produces `"1 suite"` or `"5 suites"` depending on `n`. The elapsed time is formatted by `fmtdur`, which renders sub-second durations as milliseconds (`"42ms"`) and longer durations as a fixed-precision decimal (`"3.7s"`).

## Real-Time Output Behavior

`on_case_begin` immediately flushes the suite and case name to the stream — providing the critical property that if a test hangs or crashes, the last visible output identifies exactly which test case was running. `on_fail` writes the failure reason prefixed with the sequential assertion number (`#3 failed: ...`), letting developers distinguish multiple failures within the same case. `on_pass` is silent, keeping noise low for passing assertions. `on_log` forwards the string verbatim, with no additional formatting, leaving log message structure entirely to the test author.

## Relationship to `recorder`

`recorder` and `reporter` are parallel `runner` implementations with different consumers. `recorder` accumulates a structured `results` object (containing per-case `test_results` collections) intended for programmatic querying — for example, feeding into a higher-level framework that needs to know which specific assertion failed. `reporter` discards the same information after counting it, because its only consumer is a human reading a terminal. The two are compositionally independent: neither knows about the other, and both receive the same event stream from the shared `runner` base.

## Non-Copyability

The copy constructor and copy-assignment operator are explicitly deleted. This is necessary because `reporter` holds a reference to an external `std::ostream` (`os_`). Allowing copies would create dangling reference hazards; the deletion makes the constraint visible and enforced at compile time rather than discovered at runtime.