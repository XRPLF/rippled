# SQL Database

SQLite via SOCI for ledger/transaction history. Only SQLite is supported; Postgres has no implementation despite interface comments.

## Key Invariants

- Two main databases: `lgrdb_` (ledger) and `txdb_` (transactions, optional via `useTxTables` config)
- Transaction tables are optional; disabling them means no transaction history or account_tx queries
- WAL checkpointing triggers when WAL file grows beyond threshold; scheduled via job queue
- Database init failure is fatal (throws exception, prevents construction)
- Free disk space < 512MB triggers fatal error on write operations

## Schema

- `Ledgers` table: seq, hash, parent hash, total coins, close time, etc. Indexed by `LedgerSeq`
- `Transactions` table: TransID, TransType, FromAcct, FromSeq, LedgerSeq, Status, RawTxn, TxnMeta. Indexed by `LedgerSeq`
- `AccountTransactions` table: TransID, Account, LedgerSeq, TxnSeq. Triple-indexed for account_tx queries
- Secondary DBs: Wallet (node identity, manifests), PeerFinder (bootstrap cache), State (deletion tracking)

## Common Bug Patterns

- No schema migration system; `CREATE TABLE IF NOT EXISTS` means old schemas silently persist with missing columns
- PeerFinder DB is the exception -- it has schema versioning via `SchemaVersion` table
- `safety_level` config affects journal_mode and synchronous; "low" can lose data on crash
- `page_size` must be power of 2 between 512-65536; invalid values cause init failure
- Online deletion coordinates between NodeStore rotation and SQL table pruning; race conditions here lose history

## Configuration

| Option | Section | Values | Default |
|--------|---------|--------|---------|
| `backend` | `[relational_db]` | `sqlite` only | sqlite |
| `page_size` | `[sqlite]` | 512-65536, power of 2 | 4096 |
| `safety_level` | `[sqlite]` | high, medium, low | high |
| `journal_size_limit` | `[sqlite]` | integer >= 0 | 1582080 |

## Key Patterns

### Schema Evolution Caveat
```cpp
// WARNING: no migration system — old databases keep old schemas
// CREATE TABLE IF NOT EXISTS silently skips if table exists with old columns
// New columns on existing tables require manual ALTER TABLE or
// documentation that the column is optional and may be absent
```

### Disk Space Guard
```cpp
// REQUIRED on write paths: < 512MB triggers fatal to prevent corruption
if (freeDiskSpace < minDiskFree)
    Throw<std::runtime_error>("Not enough disk space for database write");
```

## Key Files

- `src/xrpld/app/rdb/backend/detail/SQLiteDatabase.cpp` - main implementation
- `src/xrpld/app/main/DBInit.h` - schema definitions
- `src/xrpld/core/detail/DatabaseCon.cpp` - connection setup and pragmas
- `src/xrpld/app/rdb/backend/detail/Node.cpp` - ledger/tx operations
- `src/xrpld/app/rdb/detail/State.cpp` - deletion state tracking
