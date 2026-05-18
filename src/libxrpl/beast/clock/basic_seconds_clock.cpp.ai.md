# `basic_seconds_clock.cpp` — Low-Cost Second-Resolution Clock

## Purpose

This file implements a single-writer / many-reader optimization for time queries in the XRPL node. The public surface is tiny: one static `now()` function on `basic_seconds_clock`. The point is not precision — it is throughput. Hot code paths (network message timestamping, cache expiry checks, ledger timing) may call `now()` thousands of times per second. Routing each of those calls through the OS via `std::chrono::steady_clock::now()` is unnecessary when the required resolution is only one second. This implementation pre-computes the time point on a background thread and lets callers read it with a single atomic load.

## The `seconds_clock_thread` Internal Class

The entire implementation lives in an anonymous namespace, hidden from all translation units. `seconds_clock_thread` owns three concurrency primitives: a `std::mutex`, a `std::condition_variable`, and a `std::thread`, plus an `std::atomic<Clock::time_point::rep>` called `tp_` that is the sole shared value exposed to callers.

A compile-time `static_assert` verifies that `std::atomic<std::chrono::steady_clock::rep>` is always lock-free. This is critical: if it were not lock-free, reading `tp_` from calling threads could silently involve an internal mutex, undermining the entire performance rationale.

### Construction and Initialization

The constructor initialises `tp_` to `Clock::now().time_since_epoch().count()` before launching the background thread. This ensures that any call to `now()` between construction and the first loop iteration returns a valid, current timestamp rather than a zero-epoch value.

### The `run()` Loop

```
auto now = Clock::now();
tp_ = now.time_since_epoch().count();
auto const when = floor<seconds>(now) + 1s;
cv_.wait_until(lock, when, [this] { return stop_; });
```

Each iteration of the loop records the current time atomically, then computes the next second boundary (`floor<seconds>(now) + 1s`) and sleeps until that moment. This is more precise than a naive `sleep_for(1s)`: it synchronises wake-ups to wall-clock second boundaries, preventing drift from accumulated sleep overhead over long uptimes.

The `wait_until` predicate checks `stop_`, so a shutdown notification always wakes the thread immediately without waiting for the next second to roll over. If the timeout fires naturally, the predicate returns `false` and the loop continues.

### Shutdown Sequence

```cpp
{
    std::lock_guard const lock(mut_);
    stop_ = true;
}  // release lock before notify
cv_.notify_one();
thread_.join();
```

The destructor acquires the mutex, sets `stop_`, then immediately releases the lock before calling `notify_one()`. The inline comment explains the intent: publish `stop_` as quickly as possible so that if the sleeping thread happens to time out and re-evaluate the predicate before receiving the notification, it still sees the flag and exits. Releasing the lock before notifying is a standard pattern that reduces unnecessary contention. `XRPL_ASSERT(thread_.joinable())` guards against double-destruction or a corrupted object state before the blocking `join()`.

## The Public `now()` Function

```cpp
basic_seconds_clock::time_point
basic_seconds_clock::now()
{
    static seconds_clock_thread clk;
    return clk.now();
}
```

The singleton is a function-local `static`, constructed on first call. C++11 guarantees this initialization is thread-safe, so multiple threads racing on the very first `now()` call will not create multiple threads. Once alive, every call reduces to `Clock::time_point{Clock::duration{tp_.load()}}` — a single lock-free load from an `int64_t` register with no kernel involvement.

The staleness bound is at most one second: the background thread wakes up at each second boundary and stores the new value. For the ledger-timing, cache-expiry, and connection-age use cases that consume this clock (see `include/xrpl/basics/chrono.h`, which aliases `beast::basic_seconds_clock` as the `Clock` type for ledger operations), one-second granularity is entirely sufficient.

## Design Trade-offs

The obvious alternative — just calling `std::chrono::steady_clock::now()` directly — is also fast on modern Linux kernels via the VDSO (no context switch), but still requires a memory barrier and a 64-bit multiply/shift for nanosecond conversion. Under high call rates the difference accumulates. The background-thread pattern trades one extra thread and one extra second of maximum time lag for O(1) atomic reads with no arithmetic, which is the right trade-off for a server that may run millions of time queries per second.

One subtle correctness property: `tp_` is written by exactly one thread (the clock thread) and read by arbitrarily many callers. The `std::atomic` store uses the default `memory_order_seq_cst`, which provides a sequentially consistent view — every reader that completes a load sees a value no older than the most recent completed store.