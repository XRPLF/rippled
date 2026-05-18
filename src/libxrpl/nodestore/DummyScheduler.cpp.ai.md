# `DummyScheduler` — Synchronous No-Op Scheduler for NodeStore

## Role and Purpose

`DummyScheduler` is a minimal, concrete implementation of the abstract `Scheduler` interface in the `xrpl::NodeStore` namespace. Its sole purpose is to satisfy the `Scheduler` contract without introducing any real asynchrony or performance monitoring — making it the standard stand-in for test harnesses, benchmarks, and any context where backend scheduling overhead is unwanted or irrelevant.

The `Scheduler` interface exists because the NodeStore backend supports asynchronous batch writes: rather than flushing every ledger object to disk immediately, the `BatchWriter` can queue writes and schedule them on a background thread. This requires a `Scheduler` to arbitrate when and how tasks run, and to receive telemetry reports (`FetchReport`, `BatchWriteReport`) that a production scheduler could use to adapt its behavior. `DummyScheduler` collapses all of that complexity to nothing.

## Design of the Three Methods

`scheduleTask(Task& task)` is the heart of the class. The `Scheduler` interface explicitly documents that a task *may* be invoked on the calling thread or on a foreign thread — the implementation decides. `DummyScheduler` always chooses the calling thread: it calls `task.performScheduledTask()` inline before returning. This turns every "scheduled" write into a synchronous, blocking call, which eliminates concurrency entirely. The consequence is that any `BatchWriter` backed by a `DummyScheduler` loses its batching advantage, but gains predictable, sequential behavior that is far easier to reason about in tests.

`onFetch(FetchReport const& report)` and `onBatchWrite(BatchWriteReport const& report)` are both no-ops. In a production scheduler these callbacks carry timing data (`std::chrono::milliseconds elapsed`) and outcome flags (`wasFound`, `writeCount`) that could feed adaptive I/O strategies or metrics pipelines. `DummyScheduler` ignores them entirely — appropriate when the goal is correctness testing rather than performance tuning.

## Why a Separate Class Instead of Nullptr or a Lambda?

The `Scheduler` interface is passed by reference throughout the NodeStore subsystem (e.g., into `BatchWriter`). A null pointer would require defensive checks everywhere, and a lambda or `std::function` wrapper would still need a type to satisfy the virtual dispatch contract. A named concrete class like `DummyScheduler` cleanly satisfies the interface, is self-documenting at call sites, and costs nothing beyond a stack-allocated object. Its trivial constructor and destructor (`= default`) reinforce that it carries no state.

## Usage Pattern

`DummyScheduler` appears consistently in the NodeStore test suite — `Backend_test.cpp`, `Database_test.cpp`, `NuDBFactory_test.cpp`, and `Timing_test.cpp` all instantiate it on the stack before constructing a backend or database under test. The pattern is always the same: create a `DummyScheduler`, pass it (by reference) to the component under test, run assertions, and let the scheduler go out of scope. The synchronous `scheduleTask` behavior means test assertions about database state are valid immediately after any write operation, without needing barriers or condition variables.

## Relationship to the Broader NodeStore Scheduler System

The `Scheduler` abstraction exists at the boundary between the NodeStore's high-level database interface (`Database`) and its pluggable backends (NuDB, RocksDB, etc.). A real `Scheduler` implementation — such as the one wired into `Application` — runs a thread pool and uses the `onFetch`/`onBatchWrite` reports to surface latency metrics. `DummyScheduler` sits at the opposite end of this spectrum: zero threads, zero metrics, maximum simplicity. The interface's virtual dispatch means the NodeStore core never needs to know which scheduler it is talking to, making `DummyScheduler` a drop-in replacement that requires no changes to any of the code under test.