# `basic_seconds_clock.h` — Low-Cost Cached Steady Clock

## Role and Motivation

`basic_seconds_clock` exists to make `now()` calls cheap. In a ledger server like rippled, dozens of subsystems (rate limiters, cache eviction, connection timeouts, fee escalation windows) query the current time on nearly every incoming message or transaction. Each call to `std::chrono::steady_clock::now()` crosses into the OS kernel. That cost is individually small but collectively significant under high load.

The solution is a cached clock: a background thread samples the real clock at most once per second and stores the result atomically. Every call to `basic_seconds_clock::now()` from the application is then reduced to a single lock-free atomic load — no syscall, no lock acquisition, no kernel transition. The tradeoff is that the returned time may be up to one second stale, which is acceptable given that the ledger's natural time granularity (close intervals, expiry timers, network time) is measured in seconds.

## Interface Design

The class mirrors the `std::chrono::Clock` concept exactly — exposing `rep`, `period`, `duration`, `time_point`, and `is_steady` all aliased from `std::chrono::steady_clock`. This makes it a drop-in replacement anywhere a clock type is expected, and it satisfies the `Clock` named requirement so it can be used as a template argument.

The `Clock` typedef (`std::chrono::steady_clock`) is publicly accessible, which matters for the `xrpl::stopwatch()` adapter (see `chrono.h`): it uses `Clock::Clock` as the facade type and `Clock` itself as the concrete implementation when calling `beast::get_abstract_clock<Facade, Clock>()`, bridging the concrete cached clock into the `abstract_clock` dependency-injection interface that most XRPL subsystems depend on.

## Implementation: `seconds_clock_thread`

The real work lives in the unnamed namespace of `basic_seconds_clock.cpp` inside `seconds_clock_thread`. The design has three components:

**Shared state**: A single `std::atomic<Clock::time_point::rep>` named `tp_` holds the raw integer representation of the last sampled `time_point`. A compile-time `static_assert` confirms `std::atomic<std::chrono::steady_clock::rep>::is_always_lock_free`, ensuring the atomic load in `now()` is truly lock-free on every supported platform — not just sometimes.

**Background thread**: `run()` holds a `std::unique_lock` on `mut_` for its entire lifetime (the lock guards `stop_`, not `tp_`). On each iteration it calls `Clock::now()`, stores the result into `tp_` with a relaxed-ish atomic write, then calls `cv_.wait_until()` targeting the next second boundary (`floor<seconds>(now) + 1s`). The predicate `[this] { return stop_; }` means the thread wakes up either at the next second tick or immediately when `stop_` is set. Targeting the floor-of-next-second rather than sleeping for a fixed one second ensures the cached time is refreshed at consistent wall-clock boundaries regardless of sampling jitter.

**Shutdown**: The destructor sets `stop_ = true` under the mutex, releases the lock immediately so the background thread can observe it at the earliest moment (the comment calls this out explicitly), then calls `cv_.notify_one()` to unblock the `wait_until`, and finally `thread_.join()`. The `XRPL_ASSERT` in the destructor guards against double-destruction or misuse scenarios where the thread was never started.

## Singleton Lifetime

`basic_seconds_clock::now()` uses a function-local static:

```cpp
static seconds_clock_thread clk;
return clk.now();
```

This is the Meyer's singleton pattern. The `seconds_clock_thread` is constructed on the first call to `now()` and destroyed at program exit. The `clk.now()` call itself is just `Clock::time_point{Clock::duration{tp_.load()}}` — reconstructing a `time_point` from the atomic integer. This is the entire hot path: one atomic load and a trivial cast.

## Integration with `xrpl::stopwatch()`

`include/xrpl/basics/chrono.h` wires `basic_seconds_clock` into the broader system:

```cpp
inline Stopwatch& stopwatch() {
    using Clock = beast::basic_seconds_clock;
    using Facade = Clock::Clock;                        // steady_clock
    return beast::get_abstract_clock<Facade, Clock>();  // cached impl
}
```

`Stopwatch` is defined as `beast::abstract_clock<std::chrono::steady_clock>`, so callers type-check against the standard steady clock interface while the runtime delegates to the background-sampled implementation. Code under test substitutes `TestStopwatch` (a `manual_clock`) via the same `abstract_clock` polymorphism, completely bypassing `basic_seconds_clock`.

## Concurrency and Safety

`tp_` is the only state shared between `seconds_clock_thread` and any number of callers. Because it is a lock-free atomic, concurrent `now()` calls from multiple threads never contend with each other or with the background updater. The mutex and condition variable are exclusively internal to the background thread's sleep/wake cycle and shutdown handshake — they are never held during a `now()` call from application code.