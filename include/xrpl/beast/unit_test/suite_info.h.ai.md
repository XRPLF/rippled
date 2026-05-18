# `suite_info.h` — Test Suite Metadata and Type Erasure

## Role in the System

`suite_info` is the central value type of the Beast unit test framework's registry. Every test suite in the XRPL codebase is reduced to a `suite_info` instance before being stored in the global registry, and those instances are what the runner iterates over to execute tests. This file sits at the junction between the statically-typed test class hierarchy (everything derives from `beast::unit_test::suite`) and the dynamically-operated registry (`suite_list`, `runner`), providing the type erasure that makes a heterogeneous container of test types possible.

## What `suite_info` Holds

A `suite_info` instance carries six fields:

- `name_`, `module_`, `library_` — three-level naming hierarchy used to form the canonical dotted name `library.module.name` returned by `full_name()`.
- `manual_` — a flag indicating the suite should not run automatically; it must be explicitly selected by name or filter.
- `priority_` — an integer scheduling hint used by parallel test runners. Higher values mean longer-running suites that benefit from early scheduling.
- `run_` — a `std::function<void(runner&)>` that captures the concrete test type and knows how to instantiate and invoke it.

The class is a plain value type: copyable, movable, and all members are set in the constructor via `std::move`. There is no virtual dispatch, no inheritance. All polymorphic behavior lives inside the `run_` callable.

## Type Erasure via `make_suite_info`

The factory function `make_suite_info<Suite>` is where the magic happens:

```cpp
template <class Suite>
suite_info make_suite_info(std::string name, std::string module,
                           std::string library, bool manual, int priority)
{
    return suite_info(..., [](runner& r) { Suite{}(r); });
}
```

The lambda `[](runner& r) { Suite{}(r); }` captures nothing, but it permanently encodes the knowledge of which concrete `Suite` type to default-construct and invoke. Once this lambda is wrapped inside `std::function<void(runner&)>`, the specific type is gone — `suite_info` only sees a `run_type`. This is the standard "type erasure through callable" idiom: it avoids virtual functions on test classes while still permitting a homogeneous container of different test types.

The `suite` base class defines `operator()(runner&)` which sets up internal state, calls the derived `run()` method, and handles `abort_exception`. So `Suite{}(r)` creates a fresh instance on each invocation, runs it to completion (or exception), and destroys it — each `run()` call on a `suite_info` starts a clean test instance with no shared state across runs.

## Ordering and Priority

`suite_info` defines `operator<` for use in `std::set<suite_info>`:

```cpp
friend bool operator<(suite_info const& lhs, suite_info const& rhs)
{
    return std::forward_as_tuple(-lhs.priority_, lhs.library_, lhs.module_, lhs.name_) <
           std::forward_as_tuple(-rhs.priority_, rhs.library_, rhs.module_, rhs.name_);
}
```

The negation of `priority_` inverts the normal ascending sort: higher numeric priority sorts earlier in the set. Within the same priority tier, the ordering falls back to alphabetical by library, then module, then name. The comment in the code is explicit about the negation trick. The rationale (from `suite.h`) is that long-running suites get higher priorities so they can be dispatched first to parallel workers, minimizing wall-clock time in parallel test runs. Suites declared without an explicit priority default to 0 and sort last within their alphabetical group.

## Integration with the Registry

`suite_list` stores `suite_info` values in a `std::set<suite_info>`, exploiting the ordering above. Its template `insert<Suite>` method calls `make_suite_info<Suite>` to produce a `suite_info` and emplaces it. In debug builds it additionally checks for duplicate names and duplicate types using a pair of `unordered_set`s, asserting via `BOOST_ASSERT`.

The `detail::insert_suite<Suite>` struct in `global_suites.h` triggers this insertion from its constructor, which fires at program startup via a file-static instance. The macro chain in `suite.h` (`BEAST_DEFINE_TESTSUITE`, `BEAST_DEFINE_TESTSUITE_PRIO`, etc.) expands to create exactly such a static instance, so any translation unit that defines a test suite and invokes the macro will have its suite registered before `main()` runs.

## The `manual` Flag

The `manual()` accessor exposes whether a suite is opt-in only. Manual suites are skipped when running the full suite automatically (e.g., `runner::run_each`) but can be targeted by a filter predicate (e.g., `runner::run_each_if`). This lets developers define stress tests, network-dependent tests, or long-running benchmarks that would be inappropriate in CI without needing a separate binary — they live in the same registry but require explicit selection.

## Design Trade-offs

Using `std::function` for the callable introduces heap allocation and a potential virtual dispatch inside the function wrapper, but this is entirely acceptable for a test registry: suites are registered once at startup and the allocation is amortized over the entire suite run. The alternative — storing a `suite*` base pointer — would require factory functions or virtual clone methods, which is more invasive and still allocates. The lambda approach keeps test classes simple: they only need to derive from `suite` and implement `run()`, with no registration boilerplate beyond a single macro call at the bottom of the file.