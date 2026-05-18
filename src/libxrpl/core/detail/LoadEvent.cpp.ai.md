# `LoadEvent.cpp` — Scoped Job Latency Measurement

`LoadEvent` is a two-phase stopwatch that attributes elapsed time across a job's lifecycle into two buckets — *waiting* (time spent queued before execution) and *running* (time spent actively executing) — and reports both figures to a `LoadMonitor` when the measurement is closed. It exists because the XRPL job-queue needs to distinguish queue-stall latency from actual CPU work time in order to detect overload conditions at the right level.

## Two-Phase Timing Model

The class maintains three mutable state fields: a `running_` boolean, a `mark_` timestamp, and two accumulators `timeWaiting_` and `timeRunning_`. The `mark_` field always records the most recent state transition. On `start()`, the delta since the previous `mark_` is added to `timeWaiting_`, and `mark_` is reset to now. On `stop()`, the delta is added to `timeRunning_`, `mark_` is reset again, and the accumulated figures are pushed to `LoadMonitor::addLoadSample()`.

This design means `start()` is idempotent in a useful way: calling it multiple times without an intervening `stop()` is explicitly supported. Each extra call simply reclassifies the additional elapsed time as waiting time and advances the mark. The comment in the source says "any time accumulated will be counted as 'waiting'" — this is not a bug, it's the intended model for jobs that get re-queued or delayed before they actually start executing.

## Lifecycle in Practice

In `Job.cpp`, a `LoadEvent` is constructed with `shouldStart = false`. At that point, `running_` is false, but `mark_` is already set to `now`. Time begins accumulating immediately in the *waiting* bucket — any delay before `start()` is called is queue-wait time. When the `JobQueue` dispatches the job to a worker thread, it calls `start()`, which flushes the queued time into `timeWaiting_` and begins tracking execution time. When the job finishes, `stop()` is called, flushing the execution time into `timeRunning_` and invoking `monitor_.addLoadSample(*this)`.

`JobQueue::makeLoadEvent()` provides a parallel factory path for ad-hoc measurements and constructs with `shouldStart = true`, bypassing the wait phase.

## RAII Guarantee via Destructor

The destructor checks `running_` and calls `stop()` if still active. This ensures that a `LoadEvent` which goes out of scope during an exception unwind or early return will still report its timing data to the monitor, rather than silently dropping a sample. Because `stop()` contains an `XRPL_ASSERT(running_, ...)` guard, the destructor avoids a redundant stop by only firing when the event is actually still running. The copy constructor is deleted, preventing the monitor from ever receiving a sample twice from the same logical event.

## Relationship to `LoadMonitor`

`LoadMonitor::addLoadSample()` (in the sibling `.cpp`) receives the completed `LoadEvent` and computes `runTime() + waitTime()` as the total latency for that sample. Sub-2ms totals are treated as noise and collapsed to zero to avoid jitter inflating averages. Values above 500ms trigger a log warning (above 1s, a `warn`-level log). The latency is then fed into `addSamples()`, which applies an exponential-decay model to rolling average and peak figures — those rolled-up statistics are what ultimately feed the `isOver()` overload detector.

## Design Observations

The `setName()` method on a live event exists to allow job names to be refined after construction — useful when a generic job type is initially dispatched but the specific operation name only becomes known once it starts executing. The name is passed through to `LoadMonitor`'s log output, so late-binding the name improves diagnostic quality.

A surviving comment in the header (`VFALCO TODO Rename LoadEvent to ScopedLoadSample`) accurately describes the intent: this is fundamentally a scoped RAII measurement, not an "event" in the observer-pattern sense. The `LoadMonitor` reference stored by the class creates a hard lifetime dependency — `LoadEvent` must never outlive its monitor, a constraint enforced structurally by the `JobQueue` owning both.