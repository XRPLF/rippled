# `runner.h` — Unit Test Runner Interface

`runner.h` defines the central coordination class for the beast unit test framework embedded in the XRPL rippled codebase. It serves as the boundary between *test execution mechanics* and *result reporting*: the `runner` base class owns all state tracking, concurrency management, and invariant enforcement, while derived classes customize what happens at each event through a set of virtual `on_*` hooks.

## Role in the Framework

The beast unit test system has three primary collaborators: `suite_info` (metadata + a type-erased callable that constructs and runs a test), `suite` (the abstract base for test implementations), and `runner`. The `runner` is injected into every suite at execution time — `suite_info::run(runner&)` constructs a fresh suite instance and calls `operator()(runner&)` on it. The suite then drives the runner through the private interface: `testcase()`, `pass()`, `fail()`, and `log()`. By declaring `friend class suite`, `runner` keeps these four methods hidden from all other callers, ensuring that only a `suite` can advance the runner's state machine.

## State Machine and Invariants

The `runner` maintains three boolean flags that form a small protocol around testcase lifecycle:

- `default_` starts `true` at the beginning of each suite run and collapses to `false` the first time a named testcase is opened. It represents an implicit unnamed testcase that exists before any explicit `testcase(name)` call.
- `cond_` is a "has this testcase recorded at least one result?" guard. It is reset to `false` each time a testcase opens and set to `true` on any `pass()` or `fail()` call. At the end of `run()`, a `BOOST_ASSERT(cond_)` fires in debug builds if a suite somehow reached its end without making any assertion in its last open testcase.
- `failed_` accumulates across an entire suite run; it is set `true` by any `fail()` call and is what `run()` returns to the caller.

The auto-open behavior for the implicit default testcase is handled in `pass()`, `fail()`, and `log()`: if `default_` is still `true` when any of these is called, they first call `testcase("")` to formalize the unnamed case. This means test authors who write a single-case suite and never call `testcase()` explicitly get correct lifecycle callbacks without special-casing.

## Thread Safety via Recursive Mutex

All four private methods — `testcase()`, `pass()`, `fail()`, `log()` — acquire a `std::recursive_mutex` before touching shared state. A plain `std::mutex` would deadlock here because `pass()` and `fail()` may call `testcase("")` internally while already holding the lock. The recursive mutex avoids this without restructuring the locking. The `thread` helper (see `thread.h`) uses this same runner to let suites spin up internal threads that record concurrent pass/fail results safely.

## Template-Based Header Definitions

Every non-virtual method that has a body in this header uses the idiom `template <class = void>`. This is a well-known trick for header-only libraries: function templates are implicitly `inline`-equivalent from the linker's perspective, so they can be defined in a header included by multiple translation units without violating the One Definition Rule. It avoids the need for a companion `.cpp` file while still keeping the full implementation visible in the header for inlining.

## The `run` Family

The public `run*` methods provide a convenience layer over the core single-suite `run(suite_info const&)`:

- `run(FwdIter, FwdIter)` iterates over any range of `suite_info`-convertible values.
- `run_if(FwdIter, FwdIter, Pred)` adds a predicate filter, which pairs with the `match` predicates in `match.h` to run only suites whose name pattern matches a command-line argument.
- `run_each` and `run_each_if` operate on sequence containers rather than raw iterator pairs.

A subtle but important detail in all of these: the accumulation is written as `failed = run(*first) || failed` rather than `failed || run(*first)`. The latter would short-circuit after the first failure and silently skip remaining suites. The chosen form ensures every suite runs to completion and their individual failure results are ORed together.

## Concrete Implementations

`reporter.h` provides the primary concrete subclass, `detail::reporter<>`, which overrides every `on_*` hook to stream human-readable output in real time and print timing summaries on destruction. The `recorder` (in `recorder.h`) captures structured results as data for programmatic inspection. Both demonstrate how the virtual hook set — `on_suite_begin`, `on_suite_end`, `on_case_begin`, `on_case_end`, `on_pass`, `on_fail`, `on_log` — cleanly separates what the test framework observes from how those observations are reported. The default no-op implementations in the base class mean derived classes only override the events they care about.

## Argument Passthrough

The `arg_` string is a simple parameterization mechanism allowing the test harness to pass a single string to all running suites. Each suite retrieves it via `runner_->arg()` (exposed through `suite::arg()`). The meaning is entirely suite-defined — one suite might treat it as a file path, another as a numeric seed — making it a lightweight escape hatch for test customization without requiring multiple runner configurations.