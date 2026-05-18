# `src/libxrpl/nodestore/Database.cpp`

## Role in the System

`Database.cpp` implements the non-virtual concrete behavior of the `Database` base class, which serves as XRPL's persistence layer for ledger node objects. Every piece of ledger state — accounts, transactions, ledger headers — is stored as a `NodeObject` identified by its 256-bit SHA-512/256 hash. The `Database` class abstracts over different storage backends (RocksDB, NuDB, SQLite, etc.) and provides the asynchronous read infrastructure shared by all of them.

The file contains no backend-specific logic. It provides the thread pool management, async fetch queuing, batch import, performance instrumentation, and graceful shutdown that all concrete subclasses inherit. The actual storage and retrieval are delegated to the pure virtual `fetchNodeObject(hash, seq, fetchReport, duplicate)` and `for_each()` methods.

## Constructor and Read Thread Pool

The constructor validates three configuration parameters before starting the thread pool: `readThreads` (number of prefetch workers), `earliest_seq` (the minimum ledger sequence the store will serve, defaulting to `XRP_LEDGER_EARLIEST_SEQ` = 32570), and `rq_bundle` (the batch dequeue size, clamped between 1 and 64, defaulting to 4).

Each read thread is spawned and immediately **detached** rather than stored for joining. This is a deliberate trade-off: the threads have a well-defined lifetime governed by the `readStopping_` atomic flag, and detachment avoids storing thread handles that would complicate the class layout. The downside is that shutdown must spin-wait rather than join, but the 30-second `XRPL_ASSERT` in `stop()` provides a hard upper bound on how long that can take.

Within the thread loop, each worker acquires `readLock_`, checks for the stop signal, and if the queue is empty, calls `readCondVar_.wait()` — atomically releasing the lock and suspending. This is the standard condition-variable pattern, but the XRPL variant increments `runningThreads_` on wake and decrements before waiting, so `getCountsJson()` can distinguish threads that are actively processing from threads blocked on I/O.

## Batched Dequeue: The `requestBundle_` Optimization

Rather than processing one request at a time, each thread extracts up to `requestBundle_` entries from `read_` in a single lock acquisition. The comment in the code explains this clearly: the goal is to amortize mutex overhead. The default of 4 is a conservative choice that limits latency jitter while still providing meaningful throughput improvement under load.

The extracted batch is a local `std::map<uint256, vector<...>>` (same type as `read_`), and the extraction uses `read_.extract()` — the C++17 node-handle operation — to move entries without copying keys or values.

## Hash-Coalesced Async Fetch

The `read_` map has a subtle but important invariant: the **map key is the object hash**, and the value is a vector of `(ledgerSeq, callback)` pairs. Multiple callers requesting the *same* hash result in a single map entry with multiple callbacks. When the worker thread processes that entry, it fetches the object once and then fires all callbacks.

The `isSameDB()` virtual method provides a further optimization. When a multi-backend setup (such as `DatabaseRotatingImp` with a writable backend and an archive backend) receives requests for the same hash at different sequence numbers, the worker checks whether both sequence numbers map to the same physical backend. If they do, a single fetch result is reused for all callbacks. If they map to different backends, an additional fetch is issued for the mismatched sequence. The comment in the code flags this as an area for further optimization: grouping all requests to the same backend before issuing I/O could reduce round-trips further.

`asyncFetch()` itself is minimal: it locks `readLock_`, inserts or appends to the `read_` entry, and signals one waiting thread via `readCondVar_.notify_one()`. It silently discards the request if `isStopping()` is already true — callers during shutdown simply get no callback.

## Shutdown Protocol and the Derived-Class Ordering Problem

`stop()` sets `readStopping_` under `readLock_`, clears the pending queue, broadcasts on `readCondVar_`, then spin-waits until `readThreads_` drops to zero. The threads decrement `readThreads_` on exit (not `runningThreads_`), so reaching zero guarantees all threads have fully exited.

The header comment in the destructor calls out a critical design constraint: **any derived class must call `stop()` in its own destructor**. The read threads hold a raw pointer to `this` and will call the virtual `fetchNodeObject()` while running. If the derived class is destroyed before the threads exit, the vtable is partially dismantled and the call resolves to a destroyed object. Calling `stop()` in the derived destructor — before the derived members are torn down — is the correct fix. The base `~Database()` calls `stop()` as a safety net, but by that point the derived portion is already gone, which is why derived classes cannot rely solely on the base destructor.

## Instrumented Fetch Wrapper

The public `fetchNodeObject(hash, ledgerSeq, fetchType, duplicate)` is a non-virtual instrumentation shim around the private pure-virtual `fetchNodeObject(hash, ledgerSeq, fetchReport, duplicate)`. It measures wall-clock duration with `steady_clock`, accumulates `fetchDurationUs_`, counts hits via `fetchHitCount_`, tracks total bytes via `fetchSz_`, and reports the completed fetch to the `Scheduler` via `scheduler_.onFetch(fetchReport)`. The `Scheduler::onFetch()` hook allows the scheduling layer to monitor backend performance and tune task prioritization dynamically.

## Batch Import

`importInternal()` drives ledger data migration between databases. It iterates the source database using `for_each()` and accumulates objects into a `Batch` (a `std::vector<std::shared_ptr<NodeObject>>`), flushing to the destination `Backend::storeBatch()` every `batchWritePreallocationSize` objects. Exceptions from `storeBatch()` are caught and logged but do not abort the overall import — an intentional choice that favors partial progress over a complete rollback in what may be a long-running migration. After each flush, byte counts are accumulated via `storeStats()`.

## Diagnostics

`getCountsJson()` populates a JSON object with live operational metrics: read queue depth, total and running thread counts, the `rq_bundle` setting, write count and bytes, read count and bytes, cache hit counts, and total read duration in microseconds. This data surfaces through the XRPL server's `get_counts` RPC command and is valuable for diagnosing I/O bottlenecks in production nodes.