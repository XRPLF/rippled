# `SQLiteDatabase.cpp` — SQLite Backend for the XRPL Relational Database

## Role in the System

`SQLiteDatabase` is the concrete SQLite implementation of the abstract `RelationalDatabase` interface defined in `xrpl/rdb/RelationalDatabase.h`. It manages two distinct SQLite database files — a **ledger database** (`ledgerDb_`) that stores ledger headers and metadata, and a **transaction database** (`txdb_`) that stores raw transaction records and the `AccountTransactions` index table. This separation allows nodes configured without transaction history to operate using only the ledger database.

The class sits between the application layer (ledger validation, RPC handlers) and the raw SOCI SQL execution layer in `detail::` (implemented in `Node.cpp`). Every public method in this file is a thin dispatch wrapper: it guards against missing databases and then hands off to the corresponding `detail::` function. No SQL is executed here.

## The Two-Guard Pattern

Nearly every method follows the same three-step idiom before touching any database:

1. Check `useTxTables_` (transaction methods only): if the node is configured not to store transactions, return immediately with an empty/zero/false result.
2. Call `existsLedger()` or `existsTransaction()`: these simply cast the respective `std::unique_ptr<DatabaseCon>` to `bool`, providing a null-safe check without dereferencing.
3. Call `checkoutLedger()` or `checkoutTransaction()`, which calls `DatabaseCon::checkoutDb()` to obtain a RAII-scoped session from the connection pool.

This pattern is deliberately defensive. Databases can be absent because they failed to open (during startup) or were explicitly closed via `closeLedgerDB()` / `closeTransactionDB()`. Rather than crashing, every method gracefully returns an empty optional, an empty container, or a zero count.

## `useTxTables_` and the Lite-Node Configuration

The `useTxTables_` flag, set from `config.useTxTables()` at construction time, controls whether all transaction-related methods are active. When false, methods like `getTransactionsMinLedgerSeq()`, `deleteTransactionByLedgerSeq()`, `getTransactionCount()`, and the full account-transaction query family all short-circuit immediately. This allows XRPL nodes operating in a "history-lite" mode — where on-disk transaction storage is disabled — to share the same `RelationalDatabase` interface without special casing at call sites.

## Transaction Query API Design

The transaction query surface is organized along two orthogonal axes:

**Direction** — Oldest vs. Newest: the `getOldestAccountTxs` / `getNewestAccountTxs` family queries in ascending vs. descending account-sequence order.

**Serialization** — Plain vs. Binary ("B"): the plain variants (`getOldestAccountTxs`, `getNewestAccountTxs`) return deserialized `std::shared_ptr<Transaction>` and `std::shared_ptr<TxMeta>` objects via the `AccountTxs` type alias. The binary variants (suffixed `B`, like `getOldestAccountTxsB`) return raw `Blob` tuples as `MetaTxsList`, skipping deserialization entirely. The binary path is more efficient for RPC handlers that will re-serialize the data anyway.

**Paging** — List vs. Page: the list variants accept an `AccountTxOptions` with `offset` and `limit` fields. The paging variants (`oldestAccountTxPage`, `newestAccountTxPage`, and their `B` counterparts) accept an `AccountTxPageOptions` with a cursor `marker` (ledger sequence + transaction sequence pair) for stateless pagination. The paging methods use a callback-based design: the detail layer iterates the database and fires `onTransaction` lambdas for each result, decoupling result accumulation from SQL iteration. This means the same `detail::oldestAccountTxPage` / `detail::newestAccountTxPage` functions are reused by both the plain and binary page methods, with different lambdas passed in.

The page lengths are constants local to each paging method: 200 for the deserialized `AccountTx` pages (heavier, due to object construction) and 500 for the binary `MetaTxsList` pages (lighter, since they are raw blobs).

## Paging Callbacks and `saveLedgerAsync`

The paging methods bind a second callback, `onUnsavedLedger`, constructed via `std::bind(saveLedgerAsync, std::ref(registry_.get().getApp()), _1)`. This allows the detail layer to asynchronously persist any ledger it encounters during the scan that is not yet in the database, without blocking the query. This is a correctness safeguard: account transaction queries walk ledger ranges, and a ledger not yet durably saved would create gaps in the pagination marker chain.

## Construction and Initialization

The constructor takes a `ServiceRegistry&`, `Config const&`, and `JobQueue&`. It calls `makeLedgerDBs()` which delegates to `detail::makeLedgerDBs()` — a function that opens both SQLite files, returning a `DatabasePairValid` struct containing two `unique_ptr<DatabaseCon>` values and a success flag. The returned pointers are moved into `ledgerDb_` and `txdb_`. If setup fails, the constructor logs at fatal severity and throws `std::runtime_error`, making database failure a hard startup error rather than a silent degraded mode.

The move constructor uses `std::exchange` to transfer `ledgerDb_` and `txdb_`, which sets the source members to null as a side effect — important to ensure the moved-from object's `existsLedger()` and `existsTransaction()` will correctly return false.

## Disk Space and Metrics

`getKBUsedAll()` calls the SOCI-level `xrpl::getKBUsedAll()` on the ledger session to report total database disk usage. `getKBUsedLedger()` and `getKBUsedTransaction()` call `xrpl::getKBUsedDB()` against their respective sessions. These functions are exposed for monitoring and administrative RPCs that report server resource consumption.

`ledgerDbHasSpace()` and `transactionDbHasSpace()` check against configured thresholds, allowing the server to reject new work before disk exhaustion causes write failures.

## Relationship to `detail::` and `DatabaseCon`

All SQL execution lives in `detail::` functions declared in `Node.h`. The `DatabaseCon` class (from `xrpl/rdb/DatabaseCon.h`) manages the underlying SOCI connection pool and provides `checkoutDb()`, which returns a RAII session guard. The `SQLiteDatabase` class never manipulates SOCI sessions directly — it only holds the `DatabaseCon` unique pointers and passes checked-out sessions down to detail functions. This layering keeps the dispatch logic here free of SQL syntax and makes both layers independently testable.