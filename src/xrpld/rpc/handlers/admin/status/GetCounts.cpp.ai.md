## `GetCounts.cpp` — Runtime Diagnostics and Cache Statistics

This file implements the `get_counts` RPC command, an administrative endpoint that provides a single-call snapshot of the runtime health of a running `rippled` instance. It surfaces live object reference counts, database disk usage, multiple cache hit rates, node store write pressure, and formatted uptime — essentially everything an operator or developer needs to assess internal performance state without digging through logs or external monitoring tools.

The file is registered in `Handler.cpp` as `{"get_counts", byRef(&doGetCounts), Role::ADMIN, NO_CONDITION}`, meaning it is accessible only to trusted admin connections and carries no ledger-state precondition.

### `textTime` — Destructive Uptime Formatter

The static helper `textTime` is an unusual design worth noting: it takes `seconds` by reference and modifies it in place, stripping off the largest time unit on each call. The caller in `getCountsJson` chains five calls for years, days, hours, minutes, and seconds. Each call divides the remaining time by the unit's value, appends to the output string, and subtracts what it consumed. Because the remainder decreases at each step, there is no risk of double-counting, and units with a zero count produce no output — so "2 hours, 30 seconds" is emitted cleanly without a spurious "0 minutes" in the middle. The plural suffix is handled inline: `if (i > 1) text += "s"`. This is a tightly scoped utility with no external exposure.

### `getCountsJson` — The Aggregation Core

`getCountsJson(Application& app, int minObjectCount)` is the real work of the file, and it is deliberately factored out from the RPC dispatch layer. Its declaration in `GetCounts.h` means it can be called by non-RPC paths — tests or internal monitoring code — without going through the handler's parameter-parsing machinery.

**Object counts via `CountedObjects`.** The first thing the function does is call `CountedObjects::getInstance().getCounts(minObjectCount)`, which walks a lock-free singly-linked list of `Counter` objects registered by every class that inherits from `CountedObject<T>`. The CRTP template registers a static counter at program startup using an atomic compare-and-swap to prepend to the list head. `getCounts` returns only the entries whose live count meets the `minObjectCount` threshold — this filters out object types with very few active instances, reducing noise for operators. The result is flattened directly into the response JSON as `{ClassName: liveCount}` pairs.

**Relational database usage.** The block that queries `app.getRelationalDatabase()` is guarded by `app.config().useTxTables()`. When the node is configured without transaction tables (for example, a reporting-only node or a stripped-down configuration), this entire block is skipped. When enabled, it reports `dbKBTotal`, `dbKBLedger`, and `dbKBTransaction` — kilobyte usage for the full SQLite database, the ledger table, and the transaction table respectively. Each value is only written into the response if it is nonzero, keeping the output compact. `local_txs` (the count of transactions held in the local queue via `NetworkOPs::getLocalTxCount`) is bundled in the same guard block because it is only meaningful when the transaction subsystem is active.

**Write pressure and historical sync rate.** `write_load` from `NodeStore::Database::getWriteLoad()` is an estimate of pending write operations — useful for detecting a falling-behind node store. `historical_perminute` from `InboundLedgers::fetchRate()` measures how many historical ledgers are being fetched per minute, which spikes during catchup and drops to near-zero on a synced node.

**Cache health.** Four distinct caches are probed:
- `SLE_hit_rate` — the hit rate on the cached State Ledger Entries (SLEs), the on-ledger account/object records.
- `ledger_hit_rate` — `LedgerMaster`'s internal ledger cache hit rate.
- `AL_size` and `AL_hit_rate` — the size and hit rate of the `AcceptedLedger` cache, which holds recently finalized ledger structures.
- `fullbelow_size`, `treenode_cache_size`, and `treenode_track_size` — statistics from the `NodeFamily`'s SHAMap tree caches. `fullbelow_size` reflects how many nodes are known to have no missing children (avoiding unnecessary fetches). `treenode_cache_size` is the number of cached `SHAMapTreeNode` objects; `treenode_track_size` is the total tracked (including weaker references).

After these, the function delegates back to `NodeStore::Database::getCountsJson(ret)` to let the node store append its own internal counters (I/O stats, fetch counts, and similar) directly into the same JSON object.

### `doGetCounts` — RPC Entry Point

`doGetCounts` is a thin dispatch wrapper. It reads an optional `min_count` parameter from the request (defaulting to `10` if absent) and calls `getCountsJson`. The `asUInt()` call on the JSON parameter performs implicit type coercion — a non-numeric or negative value would produce `0`, silently widening to "show everything". The default of `10` exists as a practical noise filter: object types with fewer than 10 live instances are rarely interesting in production diagnostics.

The separation of `doGetCounts` from `getCountsJson` is a consistent pattern across this `admin/status` directory. The RPC handler layer deals only with request parsing and context; the actual data aggregation is independently accessible. This makes `getCountsJson` directly usable from places like server-info dump routines or internal health checks without having to construct a fake `RPC::JsonContext`.