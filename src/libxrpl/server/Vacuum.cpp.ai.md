# `src/libxrpl/server/Vacuum.cpp`

## Role in the System

This file implements `doVacuumDB()`, a maintenance utility that performs a SQLite `VACUUM` operation on the XRPL transaction database (`transaction.db`). It exists as a purpose-built administrative path invoked exclusively from `Main.cpp` when the operator runs `rippled --vacuum` — never during normal server operation. The placement under `libxrpl/server/` rather than `libxrpl/rdb/` reflects its nature: it is a node administration concern, not a general database-layer concern.

## What SQLite VACUUM Requires

SQLite's `VACUUM` command rebuilds the entire database file from scratch into a new file, then replaces the original. This defragments storage, reclaims free pages left behind by deletions, and compacts the file. Because it writes a complete second copy of the database before swapping, it requires free disk space roughly equal to the current database size. The transaction database on production XRPL nodes can grow very large, making this precondition non-trivial. The function enforces it explicitly.

## The `doVacuumDB()` Function

The function's logic falls into three phases: pre-flight checks, the VACUUM itself, and post-VACUUM configuration restoration.

**Pre-flight.** The function builds the path to `transaction.db` from `setup.dataDir` and queries its size via `boost::filesystem::file_size`. An `XRPL_ASSERT` confirms the call succeeded (a return value of `(uintmax_t)-1` signals failure in the Boost filesystem API). It then queries `boost::filesystem::space` on the parent directory. If available space is less than the database size, it prints a diagnostic to `std::cerr` and returns `false` — a graceful, user-facing failure rather than an assert, because this is an operator-correctable condition rather than a programming error.

**Opening the database and forcing disk-backed temp storage.** The function constructs a `DatabaseCon` for `transaction.db` using `TxDBName`, `setup.txPragma`, and `TxDBInit` — the same pragmas and DDL schema used during normal node startup — and obtains a SOCI session. Before issuing `VACUUM`, it unconditionally forces `PRAGMA temp_store=file`. The comment explains why: SQLite's VACUUM generates substantial temporary data, and the typical hardware recommendation for XRPL nodes means this data will not fit in memory. Regardless of what the operator configured for `temp_store` in `rippled.cfg`, this path overrides it. Using in-memory temp storage during VACUUM on a multi-gigabyte database would risk OOM.

**VACUUM and pragma restoration.** After logging the pre-VACUUM page size, the function issues `VACUUM;` and then re-applies every pragma in `setup.globalPragma`. This restoration step is necessary because SQLite resets certain pragmas when a VACUUM rebuilds the database file — notably journal mode and synchronous settings. Without re-applying them, the connection would operate with SQLite defaults rather than the node's configured settings. An `XRPL_ASSERT` guards the dereference of `setup.globalPragma`, which is expected to be non-null when `doVacuumDB` is called (the assert would fire in debug builds if `globalPragma` was never populated). The function then queries and logs the post-VACUUM page size as confirmation.

## Error Handling Design

Two distinct error handling strategies are used, reflecting two distinct failure categories. `XRPL_ASSERT` handles conditions that indicate a logic or environment error — a filesystem call returning an error sentinel, or a null `globalPragma` pointer — both of which should not occur in a correctly configured system. The `return false` path handles an operator-correctable condition (insufficient disk space) where a clean failure with a descriptive error message is more useful than an abort. Exceptions from `DatabaseCon` construction or SOCI operations propagate to the caller in `Main.cpp`, which catches `std::exception` and prints its message before returning `-1`.

## Relationship to Surrounding Infrastructure

`DatabaseCon::Setup` (from `xrpl/rdb/DatabaseCon.h`) carries `dataDir`, `txPragma`, and `globalPragma` — the shared, node-wide pragma strings built once from `rippled.cfg` during startup and stored as a `static std::unique_ptr<std::vector<std::string> const>`. `TxDBName`, `TxDBInit`, and `CommonDBPragmaTemp` come from `xrpl/rdb/DBInit.h`, which defines the schema and tuning constants for all three XRPL SQLite databases.

The caller in `Main.cpp` additionally rejects the `--vacuum` flag if the node is in standalone mode, since standalone mode does not use the transaction database in the same way and vacuuming it makes no operational sense. This guard lives in the caller rather than `doVacuumDB` itself, keeping the function's scope strictly to the filesystem and database mechanics.