# `src/xrpld/app/rdb/backend/detail/Node.cpp`

## Role in the System

`Node.cpp` is the implementation layer for the SQLite-backed *node-mode* relational database in the XRPL server. It lives in the `xrpl::detail` namespace and is consumed by the higher-level `NodeStore`-adjacent relational DB classes. The file is the direct bridge between ledger/transaction domain objects and the underlying SQLite tables (`Ledgers`, `Transactions`, `AccountTransactions`) via the SOCI database abstraction library.

The split between this file and its header (`Node.h`) is clean: the header defines the `TableType` enum, the `DatabasePairValid` return struct, and the public function signatures, while the `.cpp` contains all query construction, result deserialization, and error-handling logic. This keeps the SQLite-specific details out of consumers' compilation units.

## Database Initialization: `makeLedgerDBs`

`makeLedgerDBs` is the startup entry point. It opens two SQLite databases — the ledger DB (`Ledgers` table) and, when `config.useTxTables()` is true, the transaction DB (`Transactions` and `AccountTransactions` tables) — wrapping each in a `DatabaseCon` and returning them as `std::unique_ptr`s inside the `DatabasePairValid` struct. After opening each database it immediately issues a `PRAGMA cache_size` command, converting the config-specified value through `kilobytes()` to set SQLite's page cache in KB (the negative sign tells SQLite to interpret the argument as kilobytes rather than page count).

The most architecturally interesting part of `makeLedgerDBs` is its schema probe. When the node is not in stand-alone mode, or when starting from a snapshot (`Load`, `LoadFile`, `Replay`), it runs `PRAGMA table_info(AccountTransactions)` and inspects each column's `pk` field. If any column is marked as a primary key, it returns `valid = false` in the result struct. This is a backwards-compatibility check: old schema versions of `AccountTransactions` had a primary key that was later removed for write-performance reasons. The caller uses the `valid` flag to signal that a schema migration is required before this database can be used safely.

## `TableType` Enum and `to_string`

The file uses a small `TableType` enum (`Ledgers`, `Transactions`, `AccountTransactions`) to parameterize the generic table-management utilities. A `static_assert(TableTypeCount == 3)` inside the file-private `to_string` ensures at compile time that the switch statement covers every enum value. The `default` branch calls `UNREACHABLE`, making it impossible to silently query the wrong table name at runtime. This is the standard pattern in the codebase for guarding enums that must stay in sync with their switch tables.

## Generic Table Utilities

`getMinLedgerSeq`, `getMaxLedgerSeq`, `getRows`, and `getRowsMinMax` are stateless helpers that all accept a `TableType` and delegate table-name resolution to `to_string`. A recurring pattern is the use of `boost::optional` rather than `std::optional` as the SOCI bind target: SOCI's type system predates standard optional support, so every result that can be SQL `NULL` must go through `boost::optional`, then be converted on the way out.

`deleteByLedgerSeq` and `deleteBeforeLedgerSeq` provide point and range deletions respectively (`WHERE LedgerSeq == N` vs `WHERE LedgerSeq < N`). These are used by the online-delete subsystem to prune old ledgers.

## Ledger Persistence: `saveValidatedLedger`

This is the most complex and critical function. When a ledger is validated, it must be written atomically across two databases and also stored in the node store (the key-value store for raw ledger data). The function:

1. Validates ledger integrity up-front — checks that `accountHash` is non-zero and matches the state map hash, asserts that `txHash` matches the transaction map hash. These failures call `UNREACHABLE` because they signal ledger corruption rather than a recoverable error.
2. Serializes the ledger header with `HashPrefix::ledgerMaster` and stores it in the node store via `app.getNodeStore().store(hotLEDGER, ...)`.
3. Fetches or constructs an `AcceptedLedger` from the cache. A cache miss triggers full traversal of the ledger's transaction tree. If any nodes are missing (exception), it calls `ledgerMaster.failedSave` and signals `PendingSaves` before returning `false` — a clean failure that lets upstream retry logic operate.
4. Within a single SOCI transaction on the transaction DB, it deletes any previously saved rows for the same sequence (idempotent re-save), then inserts one row in `AccountTransactions` per affected account per transaction, batching all affected-account rows into a single bulk `INSERT INTO AccountTransactions ... VALUES (...)` string. The 128-byte-per-row size estimate in `sql.reserve` is a micro-optimization to avoid repeated heap reallocations.
5. In a separate transaction on the ledger DB, writes the `Ledgers` row via parameterized SOCI `use()` bindings (safe from SQL injection).

The two-database design means there is no single cross-database SOCI transaction. The function handles this by writing to the transaction DB first; if the ledger DB write fails after that, the transaction rows are orphaned but the downstream startup check (`PRAGMA table_info`) and the delete-before-insert pattern ensure correctness on restart.

## Ledger Lookup Functions

All ledger-by-various-criteria queries share the file-private `getLedgerInfo` function, which accepts a raw SQL suffix string. This is a deliberate internal factoring: `getLedgerInfoByIndex`, `getNewestLedgerInfo`, `getLimitedOldestLedgerInfo`, `getLimitedNewestLedgerInfo`, and `getLedgerInfoByHash` each build a WHERE/ORDER BY suffix and delegate to the common implementation. Each hash field returned from the DB is parsed via `uint256::parseHex`, and any parse failure returns an empty optional with a debug-level journal entry rather than crashing or returning a corrupted header.

`getHashByIndex` and the two `getHashesByIndex` overloads use the `SeqLedger` index hint explicitly (`INDEXED BY SeqLedger`) to ensure SQLite uses the index on `LedgerSeq` rather than a full table scan — an important performance consideration for databases that may hold millions of ledger rows.

## Account Transaction Queries

`transactionsSQL` is a private SQL builder that constructs the `INNER JOIN AccountTransactions ... Transactions` query for account-level lookups. It enforces two hard page-size caps: 200 rows for decoded (JSON-deserialized) results and 500 for binary results. These caps exist because decoded transactions are significantly more expensive to deserialize and because RPC clients cannot be trusted to supply reasonable `limit` values. The `bUnlimited` flag (admin-only) bypasses the per-query cap.

`getAccountTxs` (accessed via the public `getOldestAccountTxs`/`getNewestAccountTxs` wrappers) deserializes each row into a `Transaction` + `TxMeta` pair. If `txnMeta` is empty it triggers `pendSaveValidated` on the ledger — a defensive workaround for a historic database bug where metadata could be written as NULL. The binary variant `getAccountTxsB` skips deserialization and returns raw blobs.

## Cursor-Based Pagination: `accountTxPage`

The `accountTxPage` function implements stateless marker-based pagination, which is essential for streaming large account transaction histories across multiple RPC calls. The marker encodes `(ledgerSeq, txnSeq)`. When a marker is present, the SQL query becomes a `UNION` of two subqueries: one covering the range *beyond* the marker ledger and one covering the *same* marker ledger but with a `TxnSeq` comparison (`>=` or `<=` depending on direction). This avoids the O(offset) cost of a SQL `OFFSET` clause.

The function always queries for `numberOfResults + 1` rows; if the extra row is found, it becomes the new marker and is not returned to the caller. The result blobs are explicitly cleared after each `onTransaction` callback invocation — the comment explains why: some callbacks move the blob data out, some copy it, and clearing ensures the next `convert()` call can reuse the allocation without depending on move semantics.

## Disk Space Check: `dbHasSpace`

`dbHasSpace` guards against two distinct failure modes. First, it checks OS-level free disk space via `boost::filesystem::space` against a 512 MB threshold. Second, if transaction tables are in use, it queries SQLite's internal free-page counter (`PRAGMA page_count` vs `PRAGMA max_page_count`) to detect SQLite's own file-size ceiling. The page size and max-page-count values are cached in `static` lambdas because they do not change between calls, while the current page count is queried fresh each time. If the SQLite free space falls below 512 MB, the log message explicitly tells operators to run `xrpld` with the `vacuum` parameter, because SQLite cannot reclaim space without a full `VACUUM` rewrite.