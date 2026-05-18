# `include/xrpl/beast/unit_test/suite.h`

## Role in the System

`suite.h` defines the foundational `suite` base class for the Beast unit test framework embedded in the XRPL codebase. Every test in the rippled test suite ultimately derives from `beast::unit_test::suite`, making this the central contract between test authors and the framework's execution machinery. The file also defines the macro infrastructure (`BEAST_DEFINE_TESTSUITE`, `BEAST_EXPECT`, etc.) that wires derived test classes into the global test registry with zero boilerplate at the call site.

## Two-Phase Initialization

A deliberate design choice separates *construction* from *execution*. `suite` keeps a raw `runner*` member that starts null. Rather than requiring derived classes to accept and forward a `runner&` in their constructors, the framework injects the runner at execution time via `operator()(runner& r)`. This sets `*p_this_suite()` to the current suite, calls the private `run(runner&)`, then clears the thread-local pointer — even under exceptions. The result is that test authors write a plain default-constructible struct with a single `run()` override, free of plumbing.

## The `runner` Interface

`suite` never writes output directly. Every observed event — `pass()`, `fail()`, `log()`, `testcase()` — delegates to the injected `runner*`. The `runner` class (`runner.h`) in turn dispatches to overridable `on_pass()`, `on_fail()`, `on_case_begin()`, etc. virtual methods. This decouples the test logic from any particular output format (console, XML, recorder), following the Strategy pattern. The `runner`'s internal mutex protects its state, which matters when tests spawn worker threads.

## Abort-on-Fail Mechanism

The `abort_` / `aborted_` pair implements an early-exit facility. When a testcase is opened with `abort_on_fail`, any subsequent `fail()` call sets `aborted_ = true` and throws an internal `abort_exception`. This exception is caught by the private `run(runner&)` overload, which silently ends the suite. The subtle part is `propagate_abort()`, called at the *start* of every `pass()` and `fail()`. This ensures that if a worker thread (via `beast::unit_test::thread`) caused the failure, subsequent calls from *any* thread re-throw — preventing the suite from recording spurious passes after the abort signal. Using a dedicated private exception type avoids interfering with real `std::exception` subclasses being tested.

## Testcase Naming and `scoped_testcase`

Opening a testcase via `testcase("name")` immediately forwards the name to the runner. But the framework also supports stream-style dynamic names: `testcase << "iteration: " << i`. The `testcase_t::operator<<` method returns a `scoped_testcase` RAII object that accumulates the full name in a `std::stringstream` and, upon destruction, calls `runner_->testcase(name)`. This deferred commit-on-destruction pattern means the name is finalized at the end of the full expression, after all `<<` operators have run, without any heap allocation or manual `str()` gymnastics in the derived class.

## Logging Stream

The `log` member is a `log_os<char>`, a custom `std::basic_ostream` backed by `log_buf`. The `log_buf::sync()` override — called both when `std::endl` flushes the buffer and at destructor time — forwards the buffered text to `runner_->log()`. This lets test authors use idiomatic `log << "value = " << v << std::endl` without knowing the runner's concrete output target, and ensures all buffered data is flushed even if the log stream goes out of scope unexpectedly.

## `BEAST_EXPECT` Macros and Source Location

The `expect()` function has four overloads. The two-argument overloads accepting `char const* file, int line` produce failure messages annotated with the source location, formatted by the `detail::make_reason()` helper (which strips the directory from the path via `boost::filesystem::path::filename()`). In practice, test code should always use the `BEAST_EXPECT(cond)` and `BEAST_EXPECTS(cond, reason)` macros, which inject `__FILE__` and `__LINE__` automatically. The plain `expect(cond)` overload without file/line still records pass/fail, but failure messages lack location context — mainly kept for programmatic use.

## Global Registration Macros

The `BEAST_DEFINE_TESTSUITE(Class, Module, Library)` family of macros expands into a static `detail::insert_suite<Class##_test>` object. Its constructor calls `global_suites().insert<Suite>(...)` during static initialization, registering the test in the global `suite_list` before `main()` runs. The `MANUAL` variants mark a suite as opt-in (excluded from the default run), and the `PRIO` variants supply a scheduling priority so longer-running suites can be scheduled earlier in parallel execution. Defining `BEAST_NO_UNIT_TEST_INLINE` suppresses all insertions, useful for translation units that want to cherry-pick which suites to register.

## Thread Safety and `beast::unit_test::thread`

The `friend class thread` declaration gives `thread.h` access to `suite::abort_exception` and `propagate_abort()`. `beast::unit_test::thread` wraps `std::thread` and routes any `abort_exception` thrown in a worker thread into a silent swallow (the abort is recorded via `aborted_`), while routing any other exception into a `suite::fail()` call. When the test body calls `join()`, `propagate_abort()` is called again, re-throwing if the worker caused an abort — ensuring the main test fiber sees the failure. This design means multi-threaded tests interact safely with the abort mechanism without any additional synchronization in the test body itself.