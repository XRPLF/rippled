# `JobTypeInfo.h` — Immutable Metadata Descriptor for Job Queue Categories

## Role in the System

`JobTypeInfo` is the static configuration record for a single job type in the XRPL job queue subsystem. It answers three questions the scheduler needs before it ever touches a running thread: *what is this job called*, *how many can run at once*, and *what latency thresholds are considered healthy*. Nothing in this class changes after construction, which is enforced by declaring every member `const`.

The class exists because the job queue distinguishes two kinds of information: the fixed, compile-time-stable attributes of a category (captured here), and the live runtime counters that fluctuate as jobs are enqueued, run, and finish (captured in `JobTypeData`). Keeping them separate lets the runtime state in `JobTypeData` hold a plain `const&` to its corresponding `JobTypeInfo` without any ownership complexity, while making it impossible to accidentally mutate the policy parameters at runtime.

## What It Holds

The four meaningful fields are:

- **`m_type`** (`JobType` enum) — the canonical identifier, defined in `Job.h`. The enum's ordinal position doubles as dispatch priority: later entries in the enum have higher priority in the job set's sort order.
- **`m_name`** (`std::string`) — a human-readable label used in log output and metric event names. `JobTypeData` appends `_q` to this name for the queue-depth insight event.
- **`m_limit`** (`int`) — the maximum number of simultaneously running jobs of this type that the job queue will permit. Zero is a sentinel meaning "special" (see below).
- **`m_avgLatency` / `m_peakLatency`** (`std::chrono::milliseconds`) — target thresholds passed directly to `LoadMonitor::setTargetLatency()`. Zero means no threshold is tracked for this type.

## The `special()` Predicate

The most non-obvious design choice in this file is using `m_limit == 0` as the signal that a job type is *not dispatched through the job pool at all*. These special types — `jtPEER`, `jtDISK`, `jtTXN_PROC`, `jtPATH_FIND`, etc. — represent work that occurs on dedicated or external threads, but whose latency and activity the system still wants to monitor via `LoadMonitor`. By registering them in `JobTypes` with a zero limit, they participate in the metrics infrastructure without being subject to concurrency capping.

`JobTypeData` uses `special()` to guard the creation of insight events: a special job type gets no `dequeue` or `execute` events, since there is no queue to measure.

## Population and Lifetime

All `JobTypeInfo` instances are created once inside `JobTypes::JobTypes()` (in `JobTypes.h`), which is a private constructor backing a Meyer's singleton. The constructor calls a local `add` lambda for each known `JobType`, assembling a `std::map<JobType, JobTypeInfo>`. An assertion guards against duplicate registrations. Because the singleton is `const` and `JobTypeInfo` members are all `const`, this data is effectively read-only for the lifetime of the process.

`JobTypes::name(JobType)` is a common call site, used throughout the codebase when a string label is needed for a `JobType` without requiring a full `JobTypeData` context.

## Relationship to `JobTypeData`

`JobTypeData` is the runtime sibling: it holds a `JobTypeInfo const& info` reference alongside mutable `waiting`, `running`, and `deferred` counters, a `LoadMonitor`, and insight metric handles. The `LoadMonitor` is configured directly from `info.getAverageLatency()` and `info.getPeakLatency()` at `JobTypeData` construction. The clear separation means that the policy defined in `JobTypeInfo` is never accidentally modified when the runtime increments a counter or fires a metric.

## Design Notes

The class is non-default-constructible by explicit `= delete`, preventing accidental zero-initialized instances that would carry nonsensical metadata. The `std::string` name is moved in the constructor, avoiding a copy when called from the initializer lambda in `JobTypes`. Because all accessors return by value or `const&` and the class carries no mutable state, it is trivially thread-safe to read from multiple threads without synchronization.