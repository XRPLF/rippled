# `DecayingSample.h` — Exponential Decay Accumulators

This header provides two small template classes that maintain a running accumulation of values that automatically decay over time. Both are used throughout the XRPL node to answer the question "how much activity has happened recently?" without needing to store timestamped histories — the decay does the windowing implicitly.

## `DecayingSample<Window, Clock>`

`DecayingSample` maintains an integer accumulator that decays by approximately `1/Window` of its current value each second, producing a rate estimate normalized over the window length. It drives the resource manager's per-peer charge tracking: `Entry.h` declares `local_balance` as `DecayingSample<decayWindowSeconds, clock_type>` where `decayWindowSeconds = 32` (a power of two, per a comment in `Tuning.h`, so the division can be optimized to a bit-shift by the compiler).

The core decay step is deliberately integer arithmetic with ceiling division:

```cpp
m_value -= (m_value + Window - 1) / Window;
```

This subtracts at least 1 when `m_value` is positive, so the value cannot stall at a non-zero integer indefinitely — a safety property important for rate limiting. Adding `Window - 1` before dividing implements ceiling division, meaning the decay rounds up rather than down. The practical effect is the balance decays slightly faster than the mathematically ideal `m_value *= (1 - 1/Window)^elapsed`, which is a conservative choice for load balancing: erring toward under-charging rather than over-charging.

The `decay()` fast-path cuts off long idle periods: if more than `4 * Window` seconds have elapsed since the last update (which would leave the value at less than ~2% of its original magnitude), `m_value` is simply zeroed. This prevents the per-second loop from iterating hundreds of times on a reconnecting peer.

`add()` ages the accumulator first, then adds the new sample, and returns `m_value / Window` — the normalized balance representing average load per second across the window. `value()` does the same without adding anything. Both methods demand a `time_point now` from the caller rather than reading a clock themselves; this makes the class testable and clock-agnostic.

## `DecayWindow<HalfLife, Clock>`

`DecayWindow` takes a different approach: it stores a `double` and applies the mathematically exact exponential half-life formula:

```cpp
value_ *= std::pow(2.0, -elapsed / HalfLife);
```

After exactly `HalfLife` seconds of inactivity, the accumulated value halves. After two half-lives it quarters, and so on. Unlike `DecayingSample`, which loops through whole seconds, `DecayWindow` casts the elapsed duration to `duration<double>`, giving it sub-second precision — appropriate when the caller's clock has higher resolution or when calls are frequent.

`InboundLedgers.cpp` uses this class as `DecayWindow<30, clock_type> fetchRate_` to measure the rate at which ledgers are being fetched from peers. Each fetch fires `fetchRate_.add(1, now)`. The `fetchRate()` accessor returns `60 * fetchRate_.value(now)`, converting the per-second average to a per-minute rate for reporting.

The `static_assert(HalfLife > 0)` guards against a zero divisor in `std::pow`, which would produce undefined floating-point behavior.

## Design Rationale: Two Classes Rather Than One

The two classes reflect different use cases that have incompatible requirements. `DecayingSample` works with integer `value_type` (derived from the clock's duration representation), which matters for the resource manager where charges are counted in discrete units and the result feeds integer comparison thresholds. Integer arithmetic also avoids floating-point instability in tight loops. `DecayWindow` accepts `double` inputs and uses `std::pow`, accepting the floating-point cost in exchange for smooth decay curves and sub-second accuracy — the right tradeoff when measuring continuous rates rather than discrete charges.

Neither class is thread-safe on its own; callers are responsible for synchronization. `InboundLedgersImp` wraps `fetchRate_` with `fetchRateMutex_`, and the resource `Entry` is similarly protected by the table's lock.