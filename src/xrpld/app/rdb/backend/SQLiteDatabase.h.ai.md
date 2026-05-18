# `SQLiteDatabase.h` — SQLite Backend for the XRPL Relational Database Layer

## Role in the System

`SQLiteDatabase` is the concrete, `final` implementation of the abstract `RelationalDatabase` interface for nodes that persist ledger and transaction history in local SQLite files. It sits in `src/xrpld/app/rdb/backend/` and forms the production storage layer that the rest of `rippled` uses when querying ledger headers, transaction records, and account transaction history via the `account_tx`, `tx`, and `ledger` RPCs.

The design separates the SQLite-specific backend from the rest of the codebase through two boundaries: the abstract `RelationalDatabase` interface (defined in `include/xrpl/rdb/RelationalDatabase.h`), and an internal `detail` namespace (declared in `detail/Node.h`) that encapsulates all raw SQL execution behind typed free functions. `SQLiteDatabase` acts as a coordinator: it checks preconditions, checks out a database session, and delegates to the appropriate `detail::` function.

## Dual-Database Architecture

The class manages two distinct SQLite connections held as `std::unique_ptr<DatabaseCon>` members:

- `ledgerDb_` — stores the `Ledgers` table containing ledger headers and hash chains.
- `txdb_` — stores the `Transactions` and `AccountTransactions` tables.

Splitting ledger metadata from transaction data is deliberate. Validators and history-less nodes often operate without transaction tables at all. The `useTxTables_` boolean guards every transaction-related method: when false, those methods return empty or zero immediately without touching the database. This means a node can be configured to suppress all transaction storage with no code-path overhead beyond a single branch check.

The private `existsLedger()` and `existsTransaction()` helpers check whether their respective `unique_ptr` is non-null before any operation. `makeLedgerDBs()` can partially succeed — opening the ledger DB while failing the transaction DB — and the existence guards ensure that no method ever dereferences a null pointer. This is a defensive pattern that makes partial initialization safe rather than requiring a two-phase "open or throw" model.

## Session Checkout Pattern

`checkoutLedger()` and `checkoutTransaction()` both call `checkoutDb()` on the underlying `DatabaseCon`, returning an RAII session handle scoped to the calling operation. This is a connection-pool pattern adapted for SQLite's single-writer model. By holding the session only for the duration of a single query or write, concurrent callers serialize their access at the `DatabaseCon` level rather than at the `SQLiteDatabase` level, keeping the public API free of explicit locking. The `DatabaseCon` abstraction (via SOCI) is responsible for the pool management and WAL-mode checkpointing setup.

## Account Transaction Query Surface

Eight closely related methods form the account transaction query API, mapping to the `account_tx` RPC:

- **Offset-based variants** (`getOldestAccountTxs`, `getNewestAccountTxs`, `getOldestAccountTxsB`, `getNewestAccountTxsB`) accept `AccountTxOptions` containing an account, ledger range, numeric offset, and limit. They return full result sets in either deserialized form (`AccountTxs` — vector of `Transaction`/`TxMeta` pairs) or binary form (`MetaTxsList` — vector of raw blob tuples).

- **Marker-based paging variants** (`oldestAccountTxPage`, `newestAccountTxPage`, `oldestAccountTxPageB`, `newestAccountTxPageB`) accept `AccountTxPageOptions` with an `optional<AccountTxMarker>` cursor and return both a result set and a new marker for the next page. The page length is hardcoded as a `static const` inside each method: 200 entries for deserialized variants, 500 for binary. The higher binary limit reflects that binary results skip deserialization overhead and can be streamed more cheaply to the caller.

The paging implementations pass two callbacks into the `detail::` layer: `onUnsavedLedger` (which triggers async ledger saves for ledgers found in the DB but not yet applied to the in-memory ledger store) and `onTransaction` (which accumulates results). This inversion of control keeps the SQL cursor logic entirely within `detail::` while letting `SQLiteDatabase` determine how to convert raw data into the appropriate return type.

## `getTransaction` and the `TxSearched` Sentinel

`getTransaction()` returns `std::variant<AccountTx, TxSearched>` rather than `optional`. When a transaction is not found by hash, the absence could be definitive (the ledger that would contain this transaction is present in the DB and the transaction isn't there) or ambiguous (the DB has gaps). The `TxSearched` enum encodes this: `TxSearched::All` means the caller-supplied ledger range is fully covered and the transaction definitively does not exist; `TxSearched::Some` means coverage is partial; `TxSearched::Unknown` means no range was provided, or the transaction DB is disabled. This three-state result allows the RPC handler to give callers a precise answer about search coverage rather than a generic "not found."

## `saveValidatedLedger` — The Write Path

`saveValidatedLedger()` is the single write entry point, called as the node accepts each new validated ledger. It passes both database handles directly to `detail::saveValidatedLedger`, which writes the ledger header to `ledgerDb_` and any associated transactions to `txdb_` within a single coordinated write. The `current` parameter distinguishes real-time validated ledgers from historical ledgers being replayed during catchup, allowing the detail layer to skip certain async-save side effects during history backfill.

## Ownership and Lifecycle

Copy construction and copy assignment are deleted; move construction is permitted but move assignment is deleted. The move constructor uses `std::exchange` to transfer ownership of the two `unique_ptr` database connections. Deleting move assignment avoids a partially-moved-from state after the registry reference wrapper (which is non-owning) and the moved database handles would be inconsistent.

`closeLedgerDB()` and `closeTransactionDB()` allow the application to explicitly release the file handles, used during graceful shutdown to ensure SQLite flushes WAL frames before process exit.

## `setup_RelationalDatabase` Factory Function

The free function `setup_RelationalDatabase()` constructs a `SQLiteDatabase` by value, reading the database path and checkpointer configuration from the node's `Config`. Its docstring notes it is "recommended to use as a singleton, but not enforced," which allows test harnesses to instantiate multiple independent databases without a global state requirement.