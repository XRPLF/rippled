# `include/xrpl/basics/chrono.h`

This header is the central time-abstraction layer for the XRPL codebase. It defines three conceptually distinct things: the network's own clock type (`NetClock`) with its unusual epoch, a pair of stopwatch types for measuring elapsed time in production and test contexts, and formatting utilities that bridge between `NetClock` timestamps and human-readable strings. Nearly every subsystem that cares about ledger close times, validation timestamps, or consensus durations imports this file.

## The `NetClock` and Its Epoch

`NetClock` is a C++ *Clock* named type — a class with the required nested `rep`, `period`, `duration`, and `time_point` typedefs — that serves as the tag for all network-relative timestamps on the XRP Ledger. Its `rep` is `std::uint32_t` and its `period` is `std::ratio<1>`, meaning each tick is one second and values are unsigned 32-bit integers. This gives roughly 136 years of range from the epoch, expiring around the year 2136.

The epoch itself is January 1, 2000 00:00:00 UTC, not the Unix epoch. The offset `epoch_offset` (946684800 seconds) is computed at compile time using the `date::sys_days` facility and verified with a `static_assert`. According to a comment in `TimeKeeper.h`, this epoch was chosen arbitrarily by Arthur Britto and David Schwartz during early development and "no rationale has been provided for this curious and annoying, but otherwise unimportant, choice." The compile-time assertion exists precisely because this magic constant appears throughout serialized protocol data and on-ledger structures — any accidental change would be a silent protocol-breaking bug.

`is_steady = false` is the correct declaration because `NetClock` tracks wall time, which can be stepped or skewed by NTP. Downstream code cannot assume monotonicity, and this flag ensures standard library machinery treats it accordingly.

## Epoch Conversion and String Formatting

Because `NetClock::time_point` values are seconds since 2000, converting them to display strings requires shifting by `epoch_offset` back to the Unix epoch so that the `date` library can format them correctly. Both overloads of `to_string` and `to_string_iso` handle this conversion:

```cpp
return to_string(system_clock::time_point{tp.time_since_epoch() + epoch_offset});
```

The `to_string` variant produces a human-friendly `"YYYY-Mon-DD HH:MM:SS UTC"` format; `to_string_iso` produces ISO 8601 `"YYYY-MM-DDTHH:MM:SSZ"`. Template overloads accepting `date::sys_time<Duration>` allow the same functions to be called with standard UTC time points, while the `NetClock::time_point` overloads handle the epoch shift before delegating. A `static_assert` in `to_string_iso` guards that `NetClock::duration::period` is still `std::ratio<1>`, preventing silent precision loss if the clock's resolution were ever changed.

## Stopwatch Types for Elapsed-Time Measurement

`Stopwatch` (`beast::abstract_clock<std::chrono::steady_clock>`) and `TestStopwatch` (`beast::manual_clock<std::chrono::steady_clock>`) form a dependency-injection pair. Production components accept a `Stopwatch&` reference; unit tests supply a `TestStopwatch`, which exposes `set()`, `advance()`, and `operator++()` to move time forward deterministically without sleeping.

The design follows the pattern documented in `abstract_clock.h`: making `now()` a virtual instance method rather than a static function so the clock can be injected as a dependency. This is why production code never calls `std::chrono::steady_clock::now()` directly — it always goes through the abstraction, enabling full time-control in tests.

The `stopwatch()` free function returns a global singleton backed by `beast::basic_seconds_clock`. That class uses a background thread to sample `std::chrono::steady_clock` at most once per second and caches the result. Callers that need the current time repeatedly in a tight loop therefore pay only a single atomic load rather than a syscall per iteration, at the cost of up to one second of staleness — acceptable for the consensus and network-overlay subsystems that are the primary consumers.

## Convenience Duration Aliases

`days` and `weeks` are `std::chrono::duration` specialisations that fill a gap in C++14/17's `<chrono>` (these became standard in C++20 as `std::chrono::days` and `std::chrono::weeks`). They use `std::ratio_multiply` to derive their periods from `std::chrono::hours::period`, keeping them interoperable with the rest of `<chrono>` arithmetic. They appear in ledger aging, amendment timeouts, and fee-escalation calculations across the codebase.

## Relationship to `TimeKeeper`

`TimeKeeper` in `src/xrpld/core/TimeKeeper.h` is the sole concrete implementation of `beast::abstract_clock<NetClock>`. It uses `epoch_offset` from this header to convert `std::chrono::system_clock::now()` into a `NetClock::time_point`, and maintains an atomic `closeOffset_` that nudges the reported close time toward the network-wide consensus view. The split between the clock *type* definition here and the running clock *implementation* in `TimeKeeper` is intentional: it lets protocol-level code reference `NetClock::time_point` without depending on the application-layer time-synchronization logic.