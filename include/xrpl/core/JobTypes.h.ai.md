# `include/xrpl/core/JobTypes.h`

## Role in the System

`JobTypes` is the central registry for all job-type metadata in the XRPL job queue subsystem. It is a compile-time-defined, read-only catalog that answers a single question for any `JobType` enum value: what are the static properties of this kind of work? Every other component that needs to schedule, monitor, or report on jobs queries this singleton for concurrency limits, human-readable names, and latency expectations.

The file sits at the intersection of three subsystems: the `JobQueue` (which schedules and dispatches work), `JobTypeData` (which tracks runtime statistics per job category), and `PerfLog` (which emits telemetry). Each of those reads from `JobTypes` but none writes to it, keeping the registry strictly immutable after construction.

## Design: Meyers Singleton with a Private Registrar

The constructor is private and populates a `std::map<JobType, JobTypeInfo>` using a local `add` lambda rather than any kind of registration API or macro magic. This was an explicit design choice: the entire catalog is defined in one place, in one constructor, making it impossible to accidentally register the same type twice (both `XRPL_ASSERT` calls would fire in debug builds) and easy to audit the full set of job types at a glance.

`instance()` returns a `const` reference to a function-local static, which gives thread-safe initialization for free under C++11 and beyond — important because `JobQueue` and `PerfLog` both call it during startup from potentially concurrent contexts.

## The Two Job Categories

All registered entries fall into one of two behavioral categories based on the `limit` field passed to `add`:

**Dispatchable jobs** (limit > 0): These are actual work items placed on the queue. A limit of `std::numeric_limits<int>::max()` means "unbounded concurrency" — the queue will run as many of these simultaneously as threads allow. A small integer limit caps concurrency to prevent expensive operations from dominating the thread pool; `jtPACK` (fetch pack generation) is capped at 1, `jtLEDGER_DATA` at 3, and `jtREPLAY_REQ` at 10. The `JobQueue::getJobLimit()` function queries this via `JobTypes::instance().get(type).limit()` before deciding whether to defer a new job.

**Special jobs** (limit == 0): These are not dispatched through the queue at all. Types like `jtPEER`, `jtDISK`, `jtHO_READ`, and the node-store variants (`jtNS_SYNC_READ`, etc.) exist purely so that their execution times can be tracked by `LoadMonitor`. `JobTypeInfo::special()` returns `true` when `m_limit == 0`, and `JobTypeData` uses this flag to skip creating insight-collector events for them.

## Latency Thresholds as SLAs

Each job type carries an average-latency and peak-latency target, specified in milliseconds. These are passed into `JobTypeData`, which forwards them to a per-type `LoadMonitor` via `setTargetLatency`. The `LoadMonitor` uses them to determine whether a job type is running "hot" — the average and peak thresholds effectively encode the intended SLA for each work category.

The choice of zero for both latency fields on many types (e.g. `jtPACK`, `jtLEDGER_DATA`, `jtACCEPT`) means those types opt out of latency alerting entirely. High-value interactive paths like `jtCLIENT` (2 s / 5 s) and `jtPUBLEDGER` (3 s / 4.5 s) have explicit thresholds reflecting the observable impact of delays on end users.

## Iteration and Fallback

`JobTypes` exposes `begin()`/`cbegin()` and `end()`/`cend()` so that `JobQueue`'s constructor can range-iterate over all registered types and create a corresponding `JobTypeData` for each one. This iterator-based design avoids any hard-coded list in `JobQueue` itself.

The `m_unknown` member — initialized with `jtINVALID`, name `"invalid"`, zero limit, and zero latencies — acts as a safe fallback return value from `get()`. In debug builds, calling `get()` with an unregistered type triggers `XRPL_ASSERT`; in release builds, `m_unknown` is returned rather than dereferencing a null pointer. The public `getInvalid()` accessor lets `JobQueue` set up a dedicated `JobTypeData` for the invalid sentinel without special-casing it in iteration.

## Relationship to `Job.h`

The `JobType` enum in `Job.h` is worth noting: enum values are ordered by ascending priority, so earlier entries (like `jtPACK`) have lower priority than later entries (like `jtADMIN`). This ordering is used by `Job`'s comparison operators to sort pending work in the queue's priority set. `JobTypes` does not duplicate this ordering — it only adds the metadata layer on top of the enum values defined in `Job.h`.