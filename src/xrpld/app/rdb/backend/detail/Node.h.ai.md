# `src/xrpld/app/rdb/backend/detail/Node.h`

## Role in the System

`Node.h` is the internal function contract for the node-mode SQLite relational database in the XRPL server. It sits inside the `xrpl::detail` namespace — a deliberate signal that nothing outside the `rdb/backend` layer should include it directly. The public-facing surface lives one level up in `SQLiteDatabase`, which implements the `RelationalDatabase` abstract interface consumed by the rest of the application. `Node.h` and its counterpart `Node.cpp` exist to keep all SQLite-specific query construction, schema management, and result deserialization out of both the public interface and callers' compilation units.

The header declares the full vocabulary of SQL operations needed to manage two SQLite databases: a ledger DB (holding the `Ledgers` table) and a transaction DB (holding `Transactions` and `AccountTransactions`). Every public function in `SQLiteDatabase` delegates into one of the routines declared here, forwarding its internally held `soci::session` references as explicit parameters rather than relying on member state. This stateless design is the key architectural choice: the `detail` functions are pure in the sense that they have no coupling to the owning `SQLiteDatabase` object and can therefore be tested or composed independently.

## Supporting Types

`DatabasePairValid` is the startup return value of `makeLedgerDBs`. It bundles two `std::unique_ptr<DatabaseCon>` — one for ledgers, one for transactions — with a `bool valid` flag. The `valid` field encodes the result of a schema compatibility probe: old database files had a primary key on `AccountTransactions` that was removed for write-performance reasons, and `valid = false` tells the caller that a migration is needed before the database can be used. Collapsing these three pieces into a single struct avoids output-parameter clutter on a factory function that already takes four inputs.

`TableType` is a scoped enum covering the three tables the detail layer manages: `Ledgers`, `Transactions`, and `AccountTransactions`. The companion constant `TableTypeCount = 3` is coupled to it by a comment warning that it must be updated whenever the enum changes, and the implementation enforces this at compile time via `static_assert`. This pattern is common throughout the codebase: the assert turns a silent mismatch between the enum and a table-name switch into a build error.

## Database Initialization

`makeLedgerDBs` is the entry point for opening both databases. It accepts the server `Config`, a `DatabaseCon::Setup` (which encodes filesystem paths and tuning parameters), a `DatabaseCon::CheckpointerSetup` (WAL-mode checkpointing configuration), and a journal. The `valid` flag in the returned `DatabasePairValid` represents an irreversible pass/fail check — if the existing schema is incompatible the caller must act before any read or write operations proceed.

## Table-Level Introspection and Pruning

Six generic utilities operate on an arbitrary table identified by `TableType`:

- `getMinLedgerSeq` and `getMaxLedgerSeq` return the ledger-sequence bounds of whatever data is currently stored, returning `std::optional<LedgerIndex>` to distinguish an empty table from sequence zero.
- `getRows` and `getRowsMinMax` provide row counts; `getRowsMinMax` additionally returns the sequence bounds in a single `RelationalDatabase::CountMinMax` to avoid a three-round-trip cost for callers that need all three values.
- `deleteByLedgerSeq` removes all rows matching a specific ledger sequence, and `deleteBeforeLedgerSeq` removes all rows at or below a given sequence. These are the workhorses of the online-delete subsystem, which continuously prunes old ledgers to keep database size bounded.

All six accept a raw `soci::session&` rather than a higher-level connection object, consistent with the overall design that keeps session lifetime management outside this layer.

## Ledger Persistence

`saveValidatedLedger` is the most critical write path. It receives both database handles, the `Application` object (needed for the node store), and the validated `Ledger`. The asymmetric signature — `ldgDB` as a plain reference but `txnDB` as a `const unique_ptr&` — reflects the fact that transaction DB writes are conditional: if `useTxTables()` is false in the config, the pointer is null and the transaction-writing path is skipped entirely. The function returns `bool` to indicate whether the save succeeded, allowing upstream retry logic to operate on soft failures like missing tree nodes.

## Ledger Lookup Functions

Eight functions retrieve ledger header data through various keying strategies:

- `getLedgerInfoByIndex` and `getLedgerInfoByHash` are the primary lookup paths for a single ledger.
- `getNewestLedgerInfo`, `getLimitedOldestLedgerInfo`, and `getLimitedNewestLedgerInfo` handle boundary queries, with the "limited" variants applying a minimum-sequence floor — useful during startup when the server wants to discover what ledgers are available within a usable range.
- `getHashByIndex` returns just the ledger hash, while `getHashesByIndex` returns both the ledger hash and its parent hash as a `LedgerHashPair`. The overload that accepts `minSeq`/`maxSeq` returns a `std::map<LedgerIndex, LedgerHashPair>` for bulk range queries, avoiding N round-trips when the hash chain needs to be validated over many ledgers.

## Account Transaction Queries

This is where the function surface becomes broader. The API provides four parametric query functions controlled by `RelationalDatabase::AccountTxOptions`, which specifies the target account, ledger range, result count, and pagination offset:

- `getOldestAccountTxs` and `getNewestAccountTxs` return deserialized `Transaction` + `TxMeta` pairs in ascending or descending order respectively, consuming `LedgerMaster` to resolve ledger context during deserialization.
- `getOldestAccountTxsB` and `getNewestAccountTxsB` are the binary variants — they return raw serialized blobs via `txnMetaLedgerType` tuples without deserializing, which is significantly cheaper and used when the RPC caller has requested the `binary` flag.

The sign convention on the integer return value is noteworthy: a non-negative value means "number of transactions processed," while a negative value means "number of transactions skipped." This encodes the pagination skip count compactly without an additional out-parameter.

## Cursor-Based Pagination

`oldestAccountTxPage` and `newestAccountTxPage` implement stateless marker-based pagination — the preferred approach for streaming large account histories across multiple RPC calls. Rather than using SQL `OFFSET` (which has O(offset) cost), the marker encodes a `(ledgerSeq, txnSeq)` position that is translated into a `WHERE` predicate on the next call. Both functions accept two callbacks rather than returning a container: `onUnsavedLedger` is called for any ledger sequence in the range that has no database row (allowing the caller to trigger background saves), and `onTransaction` receives each result blob by move. Returning a `std::optional<RelationalDatabase::AccountTxMarker>` signals whether more results exist: a populated marker means the caller should issue a follow-up request, while an empty optional means the range is exhausted.

## Disk Space Guard

`dbHasSpace` is a defensive pre-flight check called before write operations. It tests two independent failure modes: OS-level free space on the database filesystem (via `boost::filesystem::space`) and SQLite's internal page-count ceiling. The journal is used to emit actionable operator guidance if space is low, telling administrators to run the server with the `vacuum` parameter to reclaim SQLite free pages.

## Relationship to Surrounding Files

`Node.h` is included only by `Node.cpp` (the implementation) and by `SQLiteDatabase.cpp` (the `SQLiteDatabase` method bodies). This strict include discipline ensures that SQLite and SOCI details never leak into the broader application build graph. The `xrpl::detail` namespace reinforces this boundary: any code that tries to call these functions directly is visibly reaching into an implementation detail, making such coupling obvious during review.