# `recorder.h` — Silent Result Accumulator for Beast Unit Tests

## Role in the System

`recorder` is a concrete implementation of the `runner` interface that silently accumulates all test outcomes into an in-memory data structure, producing a queryable `results` object after the run completes. It is the storage-focused counterpart to the stream-printing `reporter` class: where `reporter` writes human-readable output to a stream in real time, `recorder` captures everything in a structured form suitable for programmatic inspection — summary counts, per-case outcomes, failure reasons, and log messages.

The class lives at the intersection of `runner.h` (the lifecycle interface) and `results.h` (the hierarchical data model), bridging the event-driven execution model with a persistent, queryable representation.

## Design: Three-Level Accumulator

`recorder` maintains three private state fields that each correspond to one level of the test hierarchy:

- `m_results` — the top-level aggregate across all suites
- `m_suite` — the in-progress `suite_results` for the currently-executing suite
- `m_case` — the in-progress `case_results` for the currently-executing test case

The six virtual overrides implement a straightforward state machine: `on_suite_begin` resets `m_suite` with the suite's full name; `on_suite_end` moves the completed suite into `m_results`. The same pattern repeats one level down for cases. This is a classic "build-and-commit" accumulator — live data is assembled in a temporary and ownership is transferred to the parent container at close time using `std::move`, avoiding any copies.

Individual `on_pass()` and `on_fail(reason)` calls simply delegate to `m_case.tests.pass()` and `m_case.tests.fail(reason)`, which maintain both the per-condition record and a running failure count in `case_results::tests_t`. Log messages via `on_log` are appended to `m_case.log`.

## Non-Obvious Design Decision: Empty Case Pruning

`on_case_end` contains a guard that most readers might miss:

```cpp
if (m_case.tests.size() > 0)
    m_suite.insert(std::move(m_case));
```

This silently drops cases that recorded no test conditions at all. The reason lies in the `runner` base class: `runner::run()` implicitly enters a "default" unnamed testcase at the start of every suite before the suite body can call `testcase()` itself. If the suite immediately opens a named testcase, the default one closes with zero conditions. Without this guard, every suite would litter the results with a spurious empty case entry. `reporter` makes the same choice independently by not printing anything until an actual condition fires.

## Relationship to `runner`

`runner` is a non-copyable abstract class that owns the execution lifecycle, including a `std::recursive_mutex` protecting the `pass()`, `fail()`, and `testcase()` methods. All of that concurrency machinery lives in the base. `recorder` adds no concurrency of its own — it only implements the virtual hooks that fire after the mutex is already released. This means `m_results`, `m_suite`, and `m_case` are written exclusively from the `runner`'s locked dispatch path, keeping `recorder` inherently thread-safe without any explicit synchronization of its own.

## Public Interface

`recorder` exposes a single public method beyond construction:

```cpp
results const& report() const;
```

This returns a const reference to the accumulated `results` object. Callers typically invoke `run()` (or `run_each()`) on the recorder to drive execution, then call `report()` afterwards. The `results` type, along with its nested `suite_results` and `case_results` types, provides summary counts (`total()`, `failed()`, `cases()`) as well as full iteration over individual test records via the `const_container` adapter pattern used throughout `results.h`.

## Usage Context

`reporter.h` includes `recorder.h` directly, signalling that `recorder` is the foundational storage primitive. The two classes are siblings in the runner hierarchy — both derive from `runner` independently — and either can be used wherever a `runner&` is expected. `recorder` is the right choice when the test harness needs to programmatically inspect outcomes (e.g., comparing failure counts, extracting failure reasons, or presenting results in a custom format). `reporter` is the right choice for interactive terminal output.