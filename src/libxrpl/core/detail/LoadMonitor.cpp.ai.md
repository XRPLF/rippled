# `LoadMonitor.cpp` — Exponential-Decay Load Statistics Tracker

`LoadMonitor` tracks the latency and throughput of the XRPL job-processing pipeline using an exponentially-decayed statistics model. It sits between the raw event timing produced by `LoadEvent` and the policy decisions made by `JobQueue`, answering the question: "Is this subsystem overloaded right now?"

## Role in the System

`LoadEvent` is a RAII-style scoped timer. When it destructs (or when `stop()` is called explicitly), it calls `monitor_.addLoadSample(*this)`, handing its accumulated wait/run durations back to the `LoadMonitor` that owns it. `JobQueue` holds one `LoadMonitor` per job type and consults `isOver()` and `getStats()` to throttle work or expose overload state to callers. The `LoadMonitor` is the bridge between raw nanosecond-precision timing and a smoothed, human-interpretable load signal.

## Two Clocks, Two Purposes

A deliberate asymmetry in clock choice runs through this design. `LoadEvent` measures actual elapsed time with `std::chrono::steady_clock` — high-resolution, monotonic, suitable for microsecond intervals. `LoadMonitor::update()`, by contrast, uses `UptimeClock::now()`, which returns whole seconds since process start from a cached atomic integer. This coarse granularity is intentional: `update()` is called on every sample insertion, and `UptimeClock` avoids system-call overhead when nothing needs to change. The second-precision boundary is exactly the decay granularity, so there is no resolution loss in practice.

## The Exponential Decay Model

The central technique is a per-second exponential decay loop inside `update()`. Every accumulated counter — `mCounts`, `mLatencyEvents`, `mLatencyMSAvg`, `mLatencyMSPeak` — is reduced by one quarter each second:

```cpp
mCounts -= ((mCounts + 3) / 4);
```

The `+3` provides rounding-up integer arithmetic equivalent to `ceil(x/4)`. As the code comment explains: if you add 10 to a value every second and subtract 1/4 per second, the value reaches equilibrium at 40. This means all accumulators idle at **4× their instantaneous per-second rate**. That factor of 4 propagates everywhere: `getStats()` divides by `mLatencyEvents * 4` to recover the true average, and `stats.count` is `mCounts / 4`. This stored-at-4x convention is not arbitrary — it is the mathematical consequence of the decay ratio chosen, and callers must not interpret raw fields directly.

The loop advances `mLastUpdate` by one second per iteration, catching up to the current second. If the clock jumps backwards or more than 8 seconds have elapsed since the last update, all state is reset to zero. This protects against stale data after process pause or extended idle periods; the 8-second threshold is a heuristic without documented rationale (marked `// VFALCO TODO Why 8?`).

## Latency Jitter Filtering and Logging

`addLoadSample()` receives a `LoadEvent` and computes `total = runTime() + waitTime()`. Totals below 2ms are collapsed to zero before being submitted to `addSamples()`:

```cpp
auto const latency = total < 2ms ? 0ms : round<milliseconds>(total);
```

This suppresses scheduling jitter that would otherwise inflate the average. The rounding to whole milliseconds is also deliberate — the decay arithmetic depends on integer-compatible durations.

When a sample exceeds 500ms, the event is logged: at `warn` level above 1 second, `info` level between 500ms and 1s. This is the only place in `LoadMonitor` that uses the `beast::Journal` passed at construction, and it logs per-event rather than throttling repeated slow events.

## Peak Estimation

Peak tracking inside `addSamples()` is more subtle than the average:

```cpp
auto const latencyPeak = mLatencyEvents * latency * 4 / count;
if (mLatencyMSPeak < latencyPeak)
    mLatencyMSPeak = latencyPeak;
```

This scales the incoming latency by the current event density (`mLatencyEvents / count`), amplified by 4, before comparing against the running peak. Effectively, a high-latency event arriving when there are already many events in flight registers as a higher peak than the same latency arriving in isolation. This means the peak metric captures burst conditions rather than just isolated slow events.

## Locking Model

The `mutex_` protects `mCounts`, `mLatencyEvents`, `mLatencyMSAvg`, `mLatencyMSPeak`, and `mLastUpdate`. The comment above `update()` notes it must be called with the mutex already held — `update()` itself does not acquire it. This means all three entry points that call `update()` — `addSamples()`, `isOver()`, and `getStats()` — acquire the lock first and then call `update()` internally.

`setTargetLatency()` and `isOverTarget()` do **not** acquire the mutex. The target thresholds (`mTargetLatencyAvg`, `mTargetLatencyPk`) are written without synchronization and read in `isOverTarget()` while the caller may or may not hold the lock. This is a known design gap flagged in the original code comments.

`addLoadSample()` is the one public entry point that does not directly lock — it delegates entirely to `addSamples()`, which does. The jitter-filtering and logging in `addLoadSample()` happen without holding the lock, which is safe since they only read from the immutable `LoadEvent` argument.

## Overload Detection

`isOver()` computes derived average and peak values and passes them to `isOverTarget()`. The threshold check in `isOverTarget()` short-circuits when a target is zero — a zero target means "no limit." This allows either the average or peak threshold to be independently disabled.

The `Stats` struct returned by `getStats()` packages count, average latency, peak latency, and a pre-computed `isOverloaded` flag into a single snapshot taken under the lock, giving callers a consistent view without needing to hold the lock themselves.