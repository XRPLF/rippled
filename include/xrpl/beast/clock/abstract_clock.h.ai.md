# `abstract_clock.h` — Dependency-Injectable Clock Interface

## Role and Motivation

`abstract_clock<Clock>` exists to solve a pervasive testability problem in systems that rely on time. The C++ standard library clock types (`std::chrono::steady_clock`, `std::chrono::system_clock`, etc.) expose `now()` as a **static** member function — there is no instance to swap out. Any component that calls `steady_clock::now()` directly is hardwired to wall time and cannot be controlled from a test harness.

This header, part of the `beast` utility layer embedded inside the XRPL codebase, breaks that dependency by wrapping the clock concept behind a virtual interface whose `now()` is a **virtual instance method**. The clock can then be passed by reference as a constructor argument, and a test can supply a `manual_clock` instead of the real clock.

## Design Walkthrough

`abstract_clock<Clock>` inherits the full suite of nested types from the template parameter — `rep`, `period`, `duration`, `time_point`, and `clock_type` — so downstream code can continue to use standard `<chrono>` vocabulary (`clock_type::time_point`, etc.) without caring whether the underlying clock is real or simulated. The single `is_steady` constant is also mirrored as a `static constexpr` member so callers can inspect it at compile time.

The interface itself is minimal by design: only one pure virtual method, `now()`, marked `[[nodiscard]]`. That's all a clock needs to provide. The destructor is virtual to ensure correct cleanup through a base pointer, and the copy constructor is defaulted so subclasses can still be copied if they choose.

## The `abstract_clock_wrapper` Adapter

`detail::abstract_clock_wrapper<Facade, Clock>` is a thin concrete subclass that delegates `now()` to a separate `Clock` type's static `now()`. This introduces the **Facade vs. Clock** split: the public interface presents itself as `abstract_clock<Facade>`, but the actual time sampling goes through `Clock`. In practice this allows, for example, wrapping `beast::basic_seconds_clock` (a coarse, cached clock) behind a `std::chrono::steady_clock`-typed interface — so callers deal only in standard `time_point` types while the implementation trades syscall frequency for throughput.

## Global Instance Factory

`get_abstract_clock<Facade, Clock>()` returns a reference to a `static` instance of `abstract_clock_wrapper`. The `Clock` template parameter defaults to `Facade`, so the common case — wrapping a real standard clock — requires only one type argument. The static-local variable gives the singleton lifetime without requiring explicit initialization order management.

The broader codebase consumes this factory through `xrpl::stopwatch()` in `include/xrpl/basics/chrono.h`:

```cpp
using Stopwatch = beast::abstract_clock<std::chrono::steady_clock>;
using TestStopwatch = beast::manual_clock<std::chrono::steady_clock>;

inline Stopwatch& stopwatch() {
    return beast::get_abstract_clock<
        beast::basic_seconds_clock::Clock,  // Facade = steady_clock
        beast::basic_seconds_clock>();      // Clock  = coarse cached clock
}
```

This means production code throughout the XRPL node holds a `Stopwatch&` (i.e., `abstract_clock<steady_clock>&`) and calls `clock_.now()` on it, while unit tests replace it with a `TestStopwatch` (i.e., `manual_clock<steady_clock>`) whose time can be advanced deterministically.

## Concrete Implementations

- **`manual_clock<Clock>`** (in `manual_clock.h`) — the test double. Stores time as a `time_point` member and exposes `set()`, `advance()`, and `operator++()` to move it forward. An `XRPL_ASSERT` enforces monotonicity when `Clock::is_steady` is true, preventing tests from accidentally reversing a steady clock.

- **`basic_seconds_clock`** (in `basic_seconds_clock.h`) — the production implementation. Its `now()` is backed by a background thread that samples `std::chrono::steady_clock` at least once per second and caches the result, reducing the per-call cost for high-frequency callers throughout the networking and consensus layers.

## Usage in XRPL Components

`abstract_clock` shows up widely across the XRPL node's core layers. `TimeKeeper` extends `beast::abstract_clock<NetClock>` directly, making the network-adjusted clock injectable. Consensus (`Validations.h`, `Consensus.h`), peer management (`PeerfinderManager.h`, `Slot.h`), and ledger acquisition (`InboundLedgers.h`, `InboundTransactions.h`) all accept an `abstract_clock` reference, enabling their time-dependent logic to be driven by a `manual_clock` in unit tests without any stubbing framework.

The pattern is a textbook application of the Dependency Injection principle applied specifically to time — a dependency that is easy to ignore until you need deterministic test coverage of timeout, expiry, and rate-limiting logic.