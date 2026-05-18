# `TimeKeeper.h` — Network Time Management for XRPL Consensus

`TimeKeeper` solves a specific distributed-systems problem: nodes in the XRP Ledger network each have their own wall clock, yet consensus requires every node to agree on a single "close time" for each ledger round. This class maintains two related but distinct time values — the local wall-clock expressed in XRPL network time, and a consensus-adjusted estimate of what the rest of the network considers the current time.

## Inheritance and the `NetClock` Epoch

`TimeKeeper` inherits from `beast::abstract_clock<NetClock>`, which is a simple interface providing a virtual `now()` method. The `abstract_clock` pattern exists specifically to support dependency injection: callers that only need `now()` can hold a reference to `abstract_clock<NetClock>` and receive either the real `TimeKeeper` in production or a `ManualTimeKeeper` in tests.

`NetClock` itself is defined in `chrono.h` with an unusual epoch: January 1, 2000 rather than the Unix epoch of January 1, 1970. The 946,684,800-second offset between the two is computed at compile time using Howard Hinnant's `date` library and confirmed with a `static_assert`. The `adjust()` helper (a private `constexpr` static) applies this translation on every call to `now()`, converting `std::chrono::system_clock::time_point` to `NetClock::time_point` by subtracting `epoch_offset`. The code's own comment notes that this epoch was "arbitrarily defined" by Arthur Britto and David Schwartz during early XRPL development with no stated rationale — it has no semantic significance, but every timestamp in the protocol inherits this convention.

## Two Time Concepts: `now()` and `closeTime()`

`now()` returns the server's local wall clock expressed in `NetClock` units. It is the server's unilateral view of the current time and does not incorporate any network feedback. Other nodes can infer this value indirectly from published proposals and validations, but it is not transmitted directly.

`closeTime()` returns `now() + closeOffset_`. The `closeOffset_` member is a running correction accumulated from peer observations. It represents the server's estimate of how far its own clock deviates from the network's consensus view of time. The "predicted close time" that results is what the server uses as the notional center of the network — an important distinction because it decouples the server's actual clock from the value it uses when participating in ledger close negotiations.

## The `adjustCloseTime()` Algorithm

`adjustCloseTime()` is called by `RCLConsensus` at the end of each consensus round after computing a weighted average of peers' close-time votes. The `by` argument is the difference between that network estimate and the server's own time — positive if the network appears to be ahead, negative if behind.

The adjustment logic implements a damped proportional controller:

- **Small offsets (|by| ≤ 1s):** Treated as noise. Rather than applying the tiny correction, the existing `closeOffset_` is decayed by multiplying by `3/4`. This causes the offset to converge toward zero when the node is approximately in sync, preventing jitter from minor timing disagreements between peers from perturbing the estimate.

- **Larger offsets (|by| > 1s):** The offset moves by `(by + 3s) / 4` for positive values and `(by - 3s) / 4` for negative values. The ±3-second bias before the quarter-division means corrections lean slightly away from zero before dampening, ensuring meaningful drift is corrected rather than washed out. The quarter-step is conservative enough to avoid overcorrection but aggressive enough to converge over a few rounds.

The early-exit when both `by` and `offset` are zero avoids the atomic read-modify-write entirely when the server is already synchronized, a minor but intentional optimization.

## Concurrency Design

`closeOffset_` is a `std::atomic<std::chrono::seconds>`. The `adjustCloseTime()` implementation loads the current value, computes the new value via a lambda, and applies it with `compare_exchange_strong`. The code comment is explicit that this CAS is a "weak check" — the caller serializes calls to `adjustCloseTime()` externally so a CAS failure is safe to ignore without retry. The atomic type is used not to protect against races between multiple concurrent adjusters, but to ensure that readers of `closeOffset_` (via `closeTime()` or `closeOffset()`) always see a consistent value without needing a mutex.

## Test Support

`ManualTimeKeeper` in `src/test/jtx/ManualTimeKeeper.h` subclasses `TimeKeeper` and overrides `now()` with an atomically settable `time_point`. Tests call `set()` to advance or rewind time, which affects `closeTime()` as well since the offset is additive. The test harness uses this for scenarios like forcing a ledger expiration check to trigger by jumping the clock forward by weeks.