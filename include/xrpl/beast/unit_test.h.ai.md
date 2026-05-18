# `include/xrpl/beast/unit_test.h` — Beast Unit Test Framework Entry Point

## Role in the System

`unit_test.h` is the single-include umbrella for the Beast unit testing framework embedded inside the XRPL codebase. Rather than depending on an external framework such as Google Test or Boost.Test, rippled ships its own lightweight xUnit-style harness under the `beast::unit_test` namespace. This header stitches together all subsystem headers and defines the `BEAST_EXPECT` diagnostic macro, giving any translation unit full access to the framework with one `#include`.

## Architecture Overview

The framework is layered into four distinct concerns, each in its own header, all assembled here:

**Registration** (`global_suites.h`, `suite_list.h`, `suite_info.h`) — Test suites are registered into a process-global singleton at static initialization time. `suite_info` bundles a suite's name, module, library, a `manual` flag, a numeric priority, and a `std::function<void(runner&)>` factory callable. The `operator<` on `suite_info` sorts by descending priority first, then alphabetically by library/module/name — a deliberate design choice to run longer-running suites earlier when tests execute in parallel. The `global_suites()` function returns a `const` reference to the singleton; a separate `detail::global_suites()` returns the mutable variant, used only during registration. This const/mutable split prevents tests from accidentally mutating the registry at runtime.

**Suite base class** (`suite.h`) — Every test inherits from `beast::unit_test::suite` and overrides the pure-virtual `run()` method. The class provides three categories of assertion helpers: `expect()` for boolean conditions, `except()`/`unexcept()` (deprecated) for exception presence/absence, and `unexpected()` (deprecated, inverse of `expect`). All modern code should use `BEAST_EXPECT` or `BEAST_EXPECTS` macros, which forward `__FILE__` and `__LINE__` through to `expect(..., file, line)` so failure messages show the source location rather than the generic call site.

The suite's `fail()` implementation checks an `abort_` flag set by the caller via `testcase(name, abort_on_fail)`. When set, a failure throws the private `abort_exception`, which is caught and silently swallowed by `suite::run(runner&)`, terminating the current suite without propagating outward. This lets a suite author declare that a particular testcase is so fundamental that further assertions are meaningless after the first failure, without killing the entire test run.

**Runner interface** (`runner.h`) — `runner` is a pure-observer abstract base that receives a stream of lifecycle events: `on_suite_begin`, `on_suite_end`, `on_case_begin`, `on_case_end`, `on_pass`, `on_fail`, and `on_log`. All `pass()`, `fail()`, and `log()` entry points on the runner are mutex-protected with a `std::recursive_mutex`, supporting concurrent test execution from `thread` objects within a suite. The runner also carries a freeform argument string (`arg()`) that suites may read to customize behaviour — a lightweight parameterization mechanism that avoids the complexity of a full parameterized-test system.

**Results data model** (`results.h`) — `case_results`, `suite_results`, and `results` form a three-level nested hierarchy mirroring Suite → Testcase → Condition. Each level maintains separate `total` and `failed` counters, updated incrementally as results are inserted. The `detail::const_container` adapter (in `detail/const_container.h`) exposes a read-only view of the underlying `std::vector` to outside code while granting write access only to the class itself — a common encapsulation pattern in this codebase.

**Reporting** (`recorder.h`, `reporter.h`) — Concrete `runner` subclasses that collect results or render them to an output stream.

## The Registration Macros

The `BEAST_DEFINE_TESTSUITE` family of macros is the glue between user-written test classes and the global registry:

```cpp
#define BEAST_DEFINE_TESTSUITE(Class, Module, Library)            \
    BEAST_DEFINE_TESTSUITE_INSERT(Class, Module, Library, false, 0)
```

`BEAST_DEFINE_TESTSUITE_INSERT` expands to a namespace-scope `static` variable of type `detail::insert_suite<Class_test>`. Because its constructor calls `global_suites().insert<Suite>(...)`, registration happens at program startup before `main()` runs — the classic self-registering test pattern that requires no central list of test classes. The variants `_MANUAL` and `_PRIO` set the `manual` flag (opt-in from the command line) and the numeric priority, respectively. Setting `BEAST_NO_UNIT_TEST_INLINE` disables all four macros, allowing build systems to exclude test suites from production binaries without conditional compilation scattered through source files.

## The `BEAST_EXPECT` Macro

The umbrella header defines `BEAST_EXPECT` in a slightly unusual way:

```cpp
#define BEAST_EXPECT_S1(x) #x
#define BEAST_EXPECT_S2(x) BEAST_EXPECT_S1(x)
#define BEAST_EXPECT(cond) expect(cond, __FILE__ ":" BEAST_EXPECT_S2(__LINE__))
```

The double-stringification trick (`S1`/`S2`) forces the preprocessor to expand `__LINE__` to its integer value before stringifying, which is the standard idiom for converting `__LINE__` to a string literal. The result is passed as the `reason` string (e.g., `"foo_test.cpp:42"`) to the two-argument `expect()` overload. `suite.h` also provides `BEAST_EXPECTS(cond, reason)` as a ternary-expression macro that adds user-supplied text alongside the location.

## Key Design Decisions

The framework avoids dynamic dispatch on the hot path for condition checking. `runner::pass()` and `runner::fail()` call the virtual `on_pass()`/`on_fail()` hooks, but the suite-level `expect()` is non-virtual and inlined — it only reaches the runner through direct pointer indirection set up at suite invocation time. This keeps assertion overhead minimal for the thousands of conditions exercised in each test run.

The `p_this_suite()` accessor returns a pointer to a `static suite*`, establishing a per-process "currently running suite" slot. The `thread` class (declared in `suite.h` and defined in `thread.h`) is a friend of `suite` precisely so it can call `propagate_abort()` — thread-spawned work within a suite participates in the abort-on-fail protocol through this channel.