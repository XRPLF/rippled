# `include/xrpl/rdb/DBInit.h` — SQLite Database Schema and Pragma Definitions

`DBInit.h` is the single authoritative declaration of every SQLite schema and configuration string used by the three persistent relational databases in the XRPL node: the ledger database, the transaction database, and the wallet database. It contains no executable code — only `inline constexpr` string literals and compile-time arrays — so it functions as a machine-readable schema registry that is guaranteed to agree with the actual database files at every call site.

## Why a Header-Only Schema?

Embedding DDL and pragma strings as `constexpr` constants rather than in `.cpp` files or external SQL files is a deliberate tradeoff. It keeps the schema visible to the compiler for type checking on array sizes (the `DatabaseCon` constructor is templated on `std::size_t M` to enforce that the caller passes a properly-sized array), avoids file-system reads at startup, and makes it impossible for a deployment to diverge from the compiled binary's expectations. Every call to `DatabaseCon(...)` that passes one of these arrays is statically checked against the template parameter at compile time.

## SQLite Pragma Format Strings

Three format strings serve as the basis for the runtime SQLite tuning layer:

- `CommonDBPragmaJournal` — controls journaling mode (`DELETE`, `WAL`, `MEMORY`, etc.)
- `CommonDBPragmaSync` — controls `fsync` discipline (`OFF`, `NORMAL`, `FULL`, `EXTRA`)
- `CommonDBPragmaTemp` — controls where SQLite stores its temporary tables (`DEFAULT`, `FILE`, `MEMORY`)

All three use `%s` placeholders and are formatted by `boost::format` in `Config.cpp` before being stored in `DatabaseCon::Setup::globalPragma`. The populated strings are then applied to every new database connection via `DatabaseCon`'s private constructor, which iterates over the pragma list before running the DDL initialization SQL.

`SQLITE_TUNING_CUTOFF` (10,000,000 ledgers) acts as a guard in `Config.cpp`: if any of the higher-risk options are selected (e.g. `journal_mode=memory` or `synchronous=off`) and the node is configured for at least this much ledger history, a warning is emitted. The comment in the header makes the reasoning explicit — a large dataset makes corruption recovery far more expensive, so operators should be aware before sacrificing durability for performance.

## Ledger Database (`ledger.db`)

`LgrDBInit` is a 5-element `std::array<char const*, 5>` that wraps all DDL in a single transaction. It creates the `Ledgers` table, which stores one row per validated ledger with the chain-linking fields needed to reconstruct ledger history: `LedgerHash` (primary key), `LedgerSeq`, `PrevHash`, `TotalCoins`, closing time metadata, and the two state/transaction set hashes (`AccountSetHash`, `TransSetHash`). The `SeqLedger` index on `LedgerSeq` supports efficient lookups by sequence number rather than by hash, which is the more common query pattern during sync and history serving.

A `DROP TABLE IF EXISTS Validations` statement is retained but no longer creates anything — it is an artifact that removes a legacy table from pre-existing databases on first open. This kind of schema migration-in-place is common throughout the init arrays.

## Transaction Database (`transaction.db`)

`TxDBInit` is an 8-element array and the most index-heavy of the three schemas. It contains two tables and four indexes:

The `Transactions` table holds raw ledger transactions. `TransID` is the primary key; `RawTxn` and `TxnMeta` are BLOBs holding the serialized transaction and its metadata. `TxLgrIndex` on `LedgerSeq` supports bulk access by ledger (used when serving full ledger data).

`AccountTransactions` is a join table that maps every transaction to every account it touched — enabling the "account transaction history" API. Its three indexes reflect the three common query shapes: lookup by `TransID`, paginated history for an `Account` ordered by `(LedgerSeq, TxnSeq, TransID)`, and a secondary access path keyed on `(LedgerSeq, Account)`. The account history index in particular uses a composite covering index with `TransID` as the trailing key to allow efficient pagination without secondary lookups.

## Wallet Database (`wallet.db`)

`WalletDBInit` is a 6-element array covering three tables, all created in a single transaction. `NodeIdentity` stores the one-row keypair that identifies this server on the peer-to-peer network; the comment notes that this can be overridden by a `[node_seed]` config entry. `PeerReservations` associates peer public keys with human-readable descriptions, implementing the reserved-slot feature that lets operators guarantee connection slots for specific peers. `ValidatorManifests` and `PublisherManifests` persist raw signed manifest blobs, used by the validator-trust machinery to track ephemeral key rotations for both validators and manifest publishers.

## Relationship to `DatabaseCon`

`DatabaseCon.h` directly includes `DBInit.h` and uses the init arrays as template arguments. The flow at startup is: `Config.cpp` builds the global pragma vector from operator config using the format strings defined here; `Node.cpp` and `Wallet.cpp` call `DatabaseCon(...)` constructors passing `LgrDBInit`/`TxDBInit`/`WalletDBInit`; the constructor applies pragmas first, then the DDL SQL under an explicit `BEGIN TRANSACTION / END TRANSACTION` wrapping present in each array — ensuring that partial schema creation never leaves a database in an inconsistent state.