# `manual_clock.h` — Controllable Test Clock for the XRPL Beast Framework

## Role and Purpose

`manual_clock.h` provides `beast::manual_clock<Clock>`, a concrete implementation of `abstract_clock<Clock>` whose internal time never advances on its own. Instead, the caller drives all time progression explicitly through mutation methods. This makes it the standard tool for unit tests across the XRPL codebase wherever time-sensitive logic needs deterministic, reproducible behavior without waiting for wall-clock time to pass.

## Design Relationship with `abstract_clock`

The `abstract_clock<Clock>` interface exists specifically to enable dependency injection of a clock — it promotes `now()` from a static member function (as in standard C++ clock types) to a virtual instance method, so production code can accept an `abstract_clock<Clock>&` and tests can substitute a `manual_clock` without changing any other code. `manual_clock` is the canonical other half of that contract: the only concrete subclass that doesn't wrap a real system clock.

The template parameter `Clock` must satisfy the C++ `Clock` concept (e.g., `std::chrono::steady_clock` or `std::chrono::system_clock`). This allows `manual_clock` to inherit the correct `rep`, `period`, `duration`, and `time_point` types from the underlying clock family, keeping type compatibility with code that depends on those associated types without ever calling the real clock.

## State and Construction

The class holds a single private member, `now_`, of type `time_point`. The constructor defaults `now_` to the epoch (`time_point(duration(0))`), but an explicit starting point can be supplied. This zero-epoch default is intentional: tests that want absolute timestamps set them explicitly with `set()`, while tests that only care about elapsed duration can work from zero without any setup.

## Mutation API

Three methods advance or position the clock:

- **`set(time_point const& when)`** — Assigns an absolute time. For steady clocks (`Clock::is_steady == true`), an `XRPL_ASSERT` fires if `when` is earlier than the current time, enforcing the monotonicity guarantee that steady clocks provide in the real world. This check is compile-time bypassed for non-steady clocks (e.g., `system_clock`), which are allowed to go backwards.

- **`set(Integer seconds_from_epoch)`** — A convenience overload that converts an integer second count into a `time_point` and delegates to the primary `set`. This allows test code to write terse statements like `c.set(1000)` instead of constructing a `time_point` explicitly.

- **`advance(std::chrono::duration<Rep, Period> const& elapsed)`** — Adds a duration to the current time. The same monotonicity assertion applies for steady clocks. The template parameters `Rep` and `Period` make this compatible with any `std::chrono` duration literal (milliseconds, seconds, etc.), so tests can write `c.advance(std::chrono::milliseconds(500))` without casts.

- **`operator++()`** — A prefix-increment shorthand that calls `advance(std::chrono::seconds(1))`, returning `*this`. This supports chaining and enables expressive test loops where time is ticked forward one second per iteration.

## Assertion Strategy

Both `set()` and `advance()` use `XRPL_ASSERT`, which resolves to `ALWAYS_OR_UNREACHABLE` from the Antithesis instrumentation framework — in non-Antithesis debug builds this is equivalent to a standard `assert`. The guard condition (`!Clock::is_steady || ...`) is a compile-time short-circuit: if the clock family is non-steady, the check is never evaluated, so there is zero overhead and no false positives when deliberately moving a system clock backwards in tests.

## Usage Patterns in Tests

In `beast_abstract_clock_test.cpp`, the manual clock is instantiated as `manual_clock<std::chrono::steady_clock>`, time is positioned with `set()`, and `now()` is called to observe the current value — all without sleeping, making the test instant and deterministic.

In the Consensus Simulation Framework (`src/test/csf/SimTime.h`), the entire simulation time axis is defined as a type alias: `using SimClock = beast::manual_clock<std::chrono::steady_clock>`. This makes `manual_clock` the backbone of a multi-node ledger simulation, where hundreds of virtual nodes operate against a shared `SimClock` whose time is advanced by the scheduler to model network delays, timeouts, and proposal rounds — demonstrating that the class's lightweight design scales comfortably to complex simulation workloads.

## Thread Safety

`manual_clock` provides no internal synchronization. `now_` is a plain value type; concurrent reads are safe in practice because `time_point` is typically an integer alias, but concurrent writes from `set()` or `advance()` while another thread calls `now()` are a data race. For single-threaded test harnesses — the intended context — this is not a concern.