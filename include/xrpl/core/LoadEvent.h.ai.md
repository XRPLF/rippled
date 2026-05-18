# `LoadEvent.h` — Scoped Latency Measurement for the Job Queue

`LoadEvent` is a scoped elapsed-time instrument that records how long a unit of work spends waiting to be dispatched versus actually executing. It exists to give `LoadMonitor` the per-sample data it needs to compute aggregate latency statistics and detect system overload.

The in-source TODO comment captures the intent precisely: this class should eventually be renamed `ScopedLoadSample`. It acts as a RAII wrapper around a two-phase timing model — waiting and running — and automatically reports its accumulated measurements to the owning `LoadMonitor` when it stops.

## Two-Phase Timing Model

The distinguishing design choice is the split between *wait time* and *run time*. Most stopwatch abstractions track a single elapsed duration; `LoadEvent` tracks two:

- **Wait time** (`timeWaiting_`) accumulates from object construction (or a second call to `start()`) up to the moment `start()` is called to begin the active phase.
- **Run time** (`timeRunning_`) accumulates from the `start()` call until `stop()` is called.

A single `mark_` timestamp records the last state transition. On `start()`, the elapsed time since `mark_` is added to `timeWaiting_` and `mark_` is reset; on `stop()`, the elapsed time since `mark_` is added to `timeRunning_`. If `start()` is called a second time before `stop()`, the intermediate interval is again classified as waiting — a deliberate design that supports re-queuing or preemption scenarios without losing timing data.

## Lifecycle in the Job Queue

The primary consumer is `Job`. When a `Job` is constructed with a callable, it immediately creates a `LoadEvent` with `shouldStart=false`:

```cpp
m_loadEvent = std::make_shared<LoadEvent>(std::ref(lm), name, false);
```

The event begins tracking from the moment the job enters the queue. Time spent sitting in the priority queue before a worker thread picks it up accumulates as wait time. When `Job::doJob()` executes, it calls `m_loadEvent->start()`, promoting the measurement from the waiting phase to the running phase. The `LoadEvent` is stored in a `shared_ptr` precisely because `Job` objects are movable and copied into the job map, yet the event must remain at a stable address when `doJob()` borrows a reference to it across thread boundaries.

The destructor calls `stop()` if the event is still running. This ensures that even if a job throws or an early return bypasses an explicit `stop()`, the timing sample is always reported to `LoadMonitor`.

## Reporting to `LoadMonitor`

`stop()` calls `monitor_.addLoadSample(*this)`, passing itself by `const` reference. `LoadMonitor::addLoadSample()` computes total latency as `runTime() + waitTime()`, discards jitter below 2 ms, and logs a warning at `info` level for jobs exceeding 500 ms and at `warn` level for those exceeding 1 second. The sample then feeds the exponentially-decayed moving average and peak latency tracking maintained by `LoadMonitor`.

This tight coupling — `LoadEvent` holds a non-owning reference to its `LoadMonitor` — is a recognised design debt flagged in the header's VFALCO comments. The coupling creates a lifetime requirement: the `LoadMonitor` must outlive every `LoadEvent` it is associated with. In practice this is satisfied because `LoadMonitor` instances are owned at application scope while `LoadEvent` instances are tied to individual jobs.

## Copy Suppression

The copy constructor is explicitly deleted. Because `LoadEvent` accumulates time state and holds a reference to an external `LoadMonitor`, copying would produce two objects that both attempt to report to the same monitor when stopped — double-counting latency. The delete forces callers to manage lifetime explicitly, which in practice means using `shared_ptr` (as `Job` does) or embedding directly in a fixed-address owner.

## Relationship to `LoadMonitor`

`LoadMonitor` is the aggregate consumer; `LoadEvent` is the per-sample producer. `LoadMonitor` maintains exponentially-decayed counts and latency histograms protected by a `mutex_`, while `LoadEvent` performs all its timing on the calling thread with no synchronisation overhead. The lock is only acquired at the moment `addSamples()` is called inside `stop()`, keeping the hot path — timing the work itself — lock-free.