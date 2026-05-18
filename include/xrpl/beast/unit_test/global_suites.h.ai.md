# `global_suites.h` — Static Registration Infrastructure for Unit Test Suites

This file provides the two-layer mechanism that accumulates all unit test suites into a process-wide registry during C++ static initialization, before `main()` ever runs. It is a small but load-bearing piece of the beast unit test framework used throughout XRPL.

## The Registry: `detail::global_suites()`

The mutable registry lives inside a function-local `static suite_list` returned by `detail::global_suites()`. Using a function-local static rather than a plain global variable is the canonical C++ solution to the *static initialization order fiasco*: because the `suite_list` object is constructed on first use, it is guaranteed to exist before any other translation unit's static initializer calls `insert<Suite>()` on it, regardless of link order. This matters because test suites across many `.cpp` files all register themselves via static constructors, and none of them can be ordered relative to one another.

The public overload `beast::unit_test::global_suites()` (outside `detail`) returns a `const suite_list&` to the same underlying object. This const-at-the-boundary design means the runner, reporter, and matcher infrastructure can iterate suites freely but cannot accidentally mutate the registry after startup is complete. Only `insert_suite`, which lives inside `detail` and directly accesses `detail::global_suites()`, can ever write to it.

## The Registration Mechanism: `insert_suite<Suite>`

`insert_suite<Suite>` is a trivial template struct whose sole purpose is to perform a side effect in its constructor:

```cpp
static beast::unit_test::detail::insert_suite<Class##_test>
    Library##Module##Class##_test_instance(#Class, #Module, #Library, manual, priority);
```

This is exactly how `BEAST_DEFINE_TESTSUITE_INSERT` uses it in `suite.h`. When a translation unit is loaded, the static instance of `insert_suite<MyClass_test>` is constructed, which calls `global_suites().insert<Suite>(name, module, library, manual, priority)` — funneling the suite's type and metadata into the global `suite_list`. The template parameter carries the concrete suite *type*, while the constructor arguments carry its human-readable identity (`name`, `module`, `library`), whether it requires explicit invocation (`manual`), and a scheduling hint (`priority`).

The `priority` field was added specifically to support parallel test execution: suites known to be slow get higher priority values so they are scheduled earlier, preventing long-tail stragglers from stalling the overall run.

## Relationship to the Macro Layer

`suite.h` defines four public macros on top of this file:
- `BEAST_DEFINE_TESTSUITE` — automatic, priority 0
- `BEAST_DEFINE_TESTSUITE_MANUAL` — manual, priority 0
- `BEAST_DEFINE_TESTSUITE_PRIO` — automatic, with explicit priority
- `BEAST_DEFINE_TESTSUITE_MANUAL_PRIO` — manual, with explicit priority

All four collapse to `BEAST_DEFINE_TESTSUITE_INSERT`, which declares the static `insert_suite` variable. The `#include <xrpl/beast/unit_test/global_suites.h>` inside that `#else` block is what makes the type visible at each call site. When the preprocessor symbol `BEAST_NO_UNIT_TEST_INLINE` is defined, all four macros expand to nothing — allowing a build to compile test classes without registering them, useful when a custom registration strategy is preferred.

## Duplicate Detection

`suite_list::insert<Suite>()` (in `suite_list.h`) maintains two debug-only `unordered_set` members — one keyed on the fully-qualified string `"library.module.name"`, one on `std::type_index(typeid(Suite))`. Both are checked via `BOOST_ASSERT` before insertion, catching double-registration bugs that could arise from ODR violations or accidental macro reuse. These checks are stripped in release builds, so the only overhead at runtime is a single `emplace` into a `std::set<suite_info>`.

## Design Summary

The file deliberately has no public state and no mutable API surface. The division between `detail::global_suites()` (mutable, used only at registration time) and `beast::unit_test::global_suites()` (const, used by runners and reporters at test time) enforces a clean lifecycle: write during static init, read thereafter. The function-local static pattern eliminates initialization-order risk without requiring any synchronization, since all static constructors run on the main thread before `main()`.