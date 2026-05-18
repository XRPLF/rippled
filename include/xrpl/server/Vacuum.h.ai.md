# `include/xrpl/server/Vacuum.h`

This header is the public interface for a single administrative utility: `doVacuumDB`. It exposes one free function that performs a SQLite `VACUUM` operation on the XRPL node's transaction database, reclaiming fragmented space and rebuilding the file in a compact, optimally-structured form.

## Role in the System

As an XRPL node runs, its `transaction.db` SQLite file accumulates dead pages from deletions and updates. SQLite does not automatically reclaim this space; the file can grow significantly larger than its live data set. `doVacuumDB` is the offline remedy — it is invoked as a standalone CLI command (`rippled --vacuum`) against a non-running node, not as part of normal ledger processing. The `Main.cpp` call site guards it explicitly: vacuum is rejected in standalone mode and wrapped in exception handling that propagates failure as a non-zero exit code.

## Interface

```cpp
bool doVacuumDB(DatabaseCon::Setup const& setup, beast::Journal j);
```

`DatabaseCon::Setup` carries the `dataDir` path, the `txPragma` array (WAL mode, sync level, etc.), and a pointer to `globalPragma` (the shared journal/sync settings applied to every database connection). The function returns `true` on success and `false` on any preflight failure, with diagnostic messages written to `std::cerr`.

## What the Implementation Does

The implementation (in `src/libxrpl/server/Vacuum.cpp`) follows a careful sequence:

1. **Disk space preflight.** Before opening the database, it calls `boost::filesystem::file_size` on `transaction.db` and compares it to the available space on the same partition via `boost::filesystem::space`. SQLite's `VACUUM` rewrites the entire database into a temporary file before replacing the original, so it needs roughly as much free space as the current file size. If the check fails, the function returns `false` immediately rather than risk a partial vacuum that could corrupt the database.

2. **Connection with `temp_store=file`.** A `DatabaseCon` is created using the standard `txPragma` array, then immediately overrides the `temp_store` pragma to `"file"`. The comment explains why: typical XRPL deployments have transaction databases that are too large to fit in RAM. Without this override, SQLite might attempt to keep its internal sort and index rebuild buffers in memory, risking an OOM failure mid-vacuum. Forcing disk-based temp storage trades speed for safety.

3. **VACUUM execution.** The call `session << "VACUUM;"` rewrites the database file in place. SQLite performs the full repack — eliminating free pages, defragmenting B-tree nodes, and potentially shrinking the file by substantial margins.

4. **Global pragma reapplication.** After `VACUUM`, the function iterates `setup.globalPragma` and re-executes each pragma statement. This is necessary because SQLite resets certain per-connection settings as a side effect of `VACUUM`, and the node's configured journal mode and sync level must be restored before the connection is handed back. The `XRPL_ASSERT` that `globalPragma` is non-null here is significant — in any non-vacuum startup path, `globalPragma` may legitimately be absent, but this code path asserts that it must be present when vacuum runs.

5. **Diagnostic output.** The page size is read before and after the vacuum and printed to `std::cout`, giving operators a concrete signal that the operation completed and that the page layout was affected (a page size change indicates that global pragmas altered the page configuration after the vacuum).

## Design Notes

The function deliberately operates only on `transaction.db` (the `TxDBName` constant), not on the ledger database. This is because the transaction database is typically the one that accumulates the most fragmentation under normal operation, and vacuuming the ledger database while it contains WAL data that hasn't been checkpointed would be hazardous.

The header lives under `include/xrpl/server/` rather than `include/xrpl/rdb/` because its concern is node administration (a server-level concept) rather than raw database connectivity. The dependency on `DatabaseCon.h` is direct since `Setup` is defined there, but the function itself is a thin orchestration layer, not part of the database abstraction.