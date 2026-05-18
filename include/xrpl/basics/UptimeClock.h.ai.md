# `UptimeClock` — Seconds-Precision Process Uptime Clock

## Role and Motivation

`UptimeClock` is a C++ named clock type (satisfying the `<chrono>` `TrivialClock` concept) that tracks how many seconds have elapsed since the `xrpld` process first called `now()`. Its defining characteristic is that it never queries the OS for the current time on each call — instead, a single background thread wakes up once per second to increment an `std::atomic<int>` counter, and every call to `now()` simply reads that cached integer. This makes it extremely cheap to call from hot paths anywhere in the server, at the cost of one-second granularity and a possible ~1 s error at the start of the process.

The motivation is straightforward: many subsystems — load monitoring, overlay slot management, peer metrics, the uptime display in `get_counts` — need to compare elapsed time but do not need sub-second precision. Using `std::chrono::system_clock::now()` for each comparison would involve a syscall; using `UptimeClock::now()` costs a single atomic load.

## Type Design

`UptimeClock` is designed as a drop-in clock type for the `<chrono>` framework:

- `rep` is plain `int`, meaning the uptime counter fits in a 32-bit signed integer. This gives a practical maximum of ~68 years — more than sufficient for a network daemon.
- `period` is `std::ratio<1>`, meaning the tick unit is one second.
- `time_point` is `std::chrono::time_point<UptimeClock>`, carrying the seconds-since-start value.
- `is_steady` mirrors `std::chrono::system_clock::is_steady`. This is a minor implementation detail — the clock is not truly steady in the C++ sense (it is not guaranteed to never go backwards), but the value follows from its backing clock.

Because `UptimeClock` satisfies the named clock requirements, it can be used as a template argument wherever the standard library or library code expects a clock, such as `reduce_relay::Slots<UptimeClock>` in `OverlayImpl.h` and `UptimeClock::time_point mLastUpdate` in `LoadMonitor`.

## The Background Thread Mechanism

The clock uses a lazy-initialization pattern: the first call to `now()` constructs a `static update_thread` via `start_clock()`. The function-local `static` ensures this happens exactly once, and C++11 guarantees that static-local initialization is thread-safe, so no explicit lock is needed.

The `update_thread` inner class is a thin RAII wrapper around `std::thread`. It inherits from `std::thread` privately, exposes `std::thread::thread` constructors via a `using` declaration, and overrides the destructor to perform a clean shutdown: it sets the shared `stop_` atomic to `true` and calls `join()`, waiting up to 1 second for the thread to notice the flag and exit. The comment in the implementation is honest about this: the join may take up to 1 s but happens only once at shutdown, so the latency is acceptable.

Inside the thread itself, the loop uses `std::this_thread::sleep_until` rather than `sleep_for`. This avoids drift: the next wake time is computed as `next += 1s` before sleeping, so accumulated scheduler jitter does not cause the counter to fall progressively behind wall time.

Both `now_` and `stop_` are `std::atomic` — `now_` because it is read by multiple calling threads while being written by the update thread, and `stop_` because it must be visible across thread boundaries without a data race.

## Epoch and Precision Caveats

The `now()` function initializes the update thread on first call, not at process startup. The implementation comment acknowledges this: the epoch is strictly "time since first use" rather than "time since xrpld start". However, the first call to `now()` happens very early in initialization (e.g., when `LoadMonitor` is constructed), so the discrepancy is a small fraction of a second and does not matter for any current consumer.

Separately, because the counter increments after each 1-second sleep, the value returned by `now()` starts at 0 and reaches 1 only after the first full second has elapsed. Consumers should treat the value as a lower bound on elapsed seconds, accurate to ±1 s.

## Usage in the Codebase

The primary consumers fall into two categories. First, display/reporting: `GetCounts.cpp` calls `UptimeClock::now()` to compute a human-readable uptime string ("3 days 4 hours 20 minutes"), decomposing the `time_point` via repeated `time_since_epoch() / unitVal` operations. Second, time-keyed data structures: `LoadMonitor` stores a `UptimeClock::time_point mLastUpdate` to rate-limit logging, and `OverlayImpl` instantiates `reduce_relay::Slots<UptimeClock>` to manage per-peer transmission windows that expire after a fixed number of seconds.

In all these cases the one-second resolution is exactly what is needed, and the atomic-load cost of `now()` is negligible compared to the work being gated on it.