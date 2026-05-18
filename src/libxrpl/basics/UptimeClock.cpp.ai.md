# `UptimeClock.cpp` — Low-Cost Uptime Clock via Background Counter Thread

## Purpose and Context

`UptimeClock` provides a seconds-precision wall-clock that measures how long the `xrpld` process has been running. It is used throughout the server for diagnostics and time-based throttling — from the `get_counts` RPC handler that reports server uptime, to `LoadMonitor` tracking the interval between load events, to overlay and ledger subsystems tracking peer and ledger activity timing. Because these subsystems may call `now()` thousands of times per second collectively, the clock is designed around a single read of an atomic integer rather than a system call.

## Design: Cached Counter vs. System Clock

The fundamental tradeoff is read throughput vs. freshness granularity. A call to `std::chrono::system_clock::now()` ultimately invokes `clock_gettime`, a syscall that is fast but not free. When many components query the current time in tight loops, those calls accumulate. `UptimeClock` sidesteps this by maintaining a single `std::atomic<int>` counter (`now_`) that is incremented once per second by a dedicated background thread. A call to `UptimeClock::now()` is just an atomic load — a few nanoseconds, no kernel transition. The tradeoff is that time is only accurate to ±1 second, which is entirely acceptable for uptime reporting and coarse rate-limiting.

## Lazy Initialization via `static` Local

The update thread is started exactly once, on the first call to `now()`, using a function-local `static`:

```cpp
static auto const init = start_clock();
```

C++11 guarantees that function-local `static` initialization is thread-safe and happens exactly once, even under concurrent calls. This avoids explicit startup sequencing — no `ApplicationImpl` constructor needs to call `UptimeClock::start()`. The first caller naturally bootstraps the clock. The header comment acknowledges this means the epoch is "first use" rather than true process start, but the difference is a negligible fraction of a second.

## The `update_thread` RAII Wrapper

Rather than exposing a raw `std::thread`, `start_clock()` returns an `update_thread` — a private type that inherits privately from `std::thread` and adds a custom destructor:

```cpp
UptimeClock::update_thread::~update_thread()
{
    if (joinable())
    {
        stop_ = true;
        join();
    }
}
```

This is a clean RAII shutdown protocol. When `xrpld` exits and the `static init` object is destroyed, the destructor sets `stop_` to `true` and calls `join()`. The background thread will notice `stop_` on its next iteration — at most 1 second later — and exit cleanly. The comment in the source explicitly acknowledges this up-to-1s delay, noting it occurs only once at shutdown and is therefore acceptable.

Using a private inheritance from `std::thread` (rather than composition) allows `update_thread` to inherit `std::thread`'s constructor through `using std::thread::thread`, while keeping the `thread` interface private to prevent external callers from accidentally detaching or re-joining. Only the custom destructor and the move constructor are exposed.

## Thread and Memory Safety

Both `now_` and `stop_` are `std::atomic`, so there is no data race between the background writer and the many concurrent readers. The background thread uses `std::this_thread::sleep_until` with a fixed `next` timestamp advanced by `1s` per iteration — this is more accurate than `sleep_for(1s)` because it avoids drift from the thread's own scheduling jitter accumulating over time.

The `stop_` flag is checked in the `while` loop condition before sleeping, meaning on shutdown the thread exits after at most one more sleep cycle rather than running forever. There is no condition variable or explicit wake-up mechanism; the deliberate choice to wait up to 1 second keeps the implementation simple given the infrequency of process shutdown.

## Relationship to `UptimeClock.h`

The header defines `UptimeClock` as a type satisfying the `TrivialClock` concept from `<chrono>`: it declares `rep`, `period`, `duration`, and `time_point` type aliases and a static `now()` function. This makes `UptimeClock` usable anywhere a `std::chrono`-compatible clock is expected — for example, storing `UptimeClock::time_point` values and doing duration arithmetic with standard `chrono` operators. The `is_steady` flag mirrors `system_clock::is_steady` rather than being hardcoded, correctly reflecting that `system_clock` is not guaranteed steady on all platforms.