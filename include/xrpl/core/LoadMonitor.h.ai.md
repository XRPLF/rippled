# `LoadMonitor` — Per-Job-Type Latency Tracking with Exponential Decay

## Role in the System

`LoadMonitor` is the statistical engine behind XRPL's job-queue backpressure and health monitoring. Each distinct job category in the system (network I/O, consensus, transaction processing, etc.) owns exactly one `LoadMonitor` instance, embedded inside a `JobTypeData` struct. The monitor accumulates timing samples from completed jobs, computes rolling average and peak latencies using exponential decay, and compares them against configurable thresholds to determine whether a given job category is "overloaded."

It exists alongside `LoadEvent`, which is the RAII timing companion. A `LoadEvent` records how long a job spent waiting in the queue and how long it spent actually executing. When the event is destroyed, it calls back into its owning `LoadMonitor` to report the sample.

## The Exponential Decay Model

The most important design decision in `LoadMonitor` is how it maintains a rolling window without an explicit circular buffer or timestamp-indexed history. Instead, the private `update()` method is called at the start of every mutating operation and reads the current time from `UptimeClock` (a second-precision program-uptime clock). If more than one second has elapsed since the last update, the accumulators are decayed by 1/4 per elapsed second in a `do-while` loop:

```cpp
mCounts -= ((mCounts + 3) / 4);       // integer ceiling divide
mLatencyEvents -= ((mLatencyEvents + 3) / 4);
mLatencyMSAvg  -= (mLatencyMSAvg / 4);
mLatencyMSPeak -= (mLatencyMSPeak / 4);
```

This is a deliberate choice: rather than keeping a window of the last N samples or the last N seconds, the monitor uses a leaky bucket. As the inline comment (attributed to David Schwartz) explains: if you add 10 units per second and reduce by 1/4 per second, the value stabilizes around 40, which represents 10 per second. The "true" rate is recovered at read time by dividing by 4. This design is lightweight, allocation-free, and naturally ages out stale data without bookkeeping.

A second guard handles clock anomalies: if the current time is more than 8 seconds ahead of `mLastUpdate`, or if it goes backwards (clock reset), all accumulators are zeroed and the epoch is reset. This prevents stale data from persisting through long pauses or restarts.

## The Factor of 4

The "×4 normalization" permeates the implementation and deserves explicit explanation. Raw accumulators hold values scaled by 4 at steady state because of the decay formula. When `getStats()` reads them out it divides by 4:

```cpp
stats.count      = mCounts / 4;
stats.latencyAvg = mLatencyMSAvg / (mLatencyEvents * 4);
stats.latencyPeak = mLatencyMSPeak / (mLatencyEvents * 4);
```

Similarly, `isOver()` computes the check using the same normalization. The `mLatencyMSPeak` accumulator gets an additional upward push on each sample — it is set to the maximum of its current value and `mLatencyEvents * latency * 4 / count`, which biases the peak toward recent high-latency events. This asymmetry (slow to decay via the normal path, fast to spike on a bad sample) makes the peak metric more sensitive to bursts than the average.

## Jitter Filtering and Logging

`addLoadSample()`, the entry point called from a `LoadEvent` destructor, suppresses sub-2ms samples entirely by treating them as zero-latency ("jitter"). Samples above 500ms trigger an info-level log entry; above 1 second, a warning. This is the only place in the monitor where the `Journal` is used — all other paths are silent.

## Thread Safety

All state mutations go through `mutex_`. The `update()` method is documented as requiring the caller to hold the lock, and every public mutating method (`addSamples`, `isOver`, `getStats`) acquires it before calling `update()`. `setTargetLatency` is the exception — it writes `mTargetLatencyAvg` and `mTargetLatencyPk` without the lock, which is safe only during initialization (before any samples arrive). `isOverTarget()` is a pure read of the target values and is only ever called while the lock is already held.

## Integration with `JobTypeData`

`JobTypeData` constructs its embedded `LoadMonitor` with the job category's journal, then immediately calls `setTargetLatency` with latency thresholds from `JobTypeInfo`. These thresholds differ by job type — consensus jobs tolerate longer latencies than RPC-serving jobs. The `JobQueue` infrastructure calls `load().addLoadSample(event)` after each completed job and checks `stats().isOverloaded` when deciding whether to defer new jobs in that category. This makes `LoadMonitor` a key input to the job admission control loop.

## Design Notes and Known Rough Edges

The codebase contains several `VFALCO TODO` comments acknowledging unresolved design debt: the name collision between `LoadMonitor` and `LoadManager` is flagged as confusing, `LoadEvent` is noted as a candidate for renaming to `ScopedLoadSample`, and the magic number 8 (the "way out of date" threshold in seconds) has no documented rationale. The `Stats` struct was already acknowledged as a partial improvement over a previous multi-out-parameter approach, with a note to complete the refactoring. Despite these rough edges, the core exponential-decay mechanism is compact, effective, and correctly integrated into the broader job scheduling system.