# `include/xrpl/rdb/RelationalDatabase.h`

## Role in the System

`RelationalDatabase` is the abstract interface that sits at the centre of the XRPL node's relational-database layer. It exists because the XRP Ledger software persists two categories of mutable data in a relational store — ledger headers and transaction records — and must expose a single, backend-agnostic surface to the rest of the application. By concentrating every SQL-touching query behind a pure-virtual class, the codebase enforces a strict boundary: no hard-coded SQL is permitted anywhere outside the `xrpld/app/rdb` directory tree.

The class models three underlying tables — `Ledgers`, `Transactions`, and `AccountTransactions` — whose physical layout is owned by the concrete implementation. Presently the only production implementation is `SQLiteDatabase`, which keeps two separate `DatabaseCon` handles (`ledgerDb_` and `txdb_`), allowing the ledger-header and transaction stores to be managed as distinct SQLite files. The interface is deliberately database-agnostic: the README notes that a `PostgresDatabase` variant once existed alongside `SQLiteDatabase`, and the design accommodates future alternatives.

## Query Vocabulary: Structs and Type Aliases

Rather than proliferating function parameters, the interface defines a small set of descriptive value types that serve as a structured query vocabulary.

`LedgerHashPair` bundles a ledger's own hash with its parent's hash into a single return value, which is the canonical output when the caller needs chain-continuity information. `LedgerRange` captures an inclusive `[min, max]` sequence-number window; a value of `0` for either bound means unbounded, letting callers express open-ended range queries without special-casing.

`CountMinMax` is a combined aggregation result containing the row count, minimum sequence, and maximum sequence of the `Ledgers` table — useful for health and monitoring endpoints.

The account-transaction side of the interface has a richer vocabulary. `AccountTxOptions` is the offset-based query descriptor: it carries the account ID, a `LedgerRange`, an `offset`, a `limit`, and a flag `bUnlimited`. The corresponding marker-based variant is `AccountTxPageOptions`, which replaces `offset` with an `std::optional<AccountTxMarker>`. `AccountTxMarker` is a `(ledgerSeq, txnSeq)` pair that encodes a cursor into the result set: when a paged query does not exhaust results, the returned optional marker lets the caller resume exactly where it stopped. The split into two option structs reflects the fact that offset-based and marker-based pagination impose different semantics on the underlying SQL.

The return-type aliases complete the vocabulary. `AccountTx` is a `std::pair<shared_ptr<Transaction>, shared_ptr<TxMeta>>` — the deserialized, object-form result. `AccountTxs` is a vector of those pairs. For callers that need the raw bytes without incurring deserialization, `MetaTxsList` is a vector of `tuple<Blob, Blob, uint32_t>` (raw transaction bytes, raw metadata bytes, ledger sequence). The "B" suffix on several method names consistently marks the binary variants.

`LedgerSpecifier` is a `std::variant<LedgerRange, LedgerShortcut, LedgerSequence, LedgerHash>` that covers every way a caller might identify a ledger. `AccountTxArgs` and `AccountTxResult` aggregate inputs and outputs for the highest-level `account_tx` RPC path, where the caller additionally signals whether it wants binary output and which traversal direction (forward vs. backward) to use.

## API Design: Eight Account-Transaction Accessors

The account-transaction query surface is deliberately symmetric. Two access patterns (offset-based, marker-based) × two orderings (oldest-first, newest-first) × two output formats (object, binary) yields eight methods. Offset-based variants (`getOldestAccountTxs`, `getNewestAccountTxs`, `getOldestAccountTxsB`, `getNewestAccountTxsB`) return a plain vector; marker-based page variants (`oldestAccountTxPage`, `newestAccountTxPage`, `oldestAccountTxPageB`, `newestAccountTxPageB`) return a `std::pair<results, optional<marker>>` so callers can detect whether the result set was truncated and where to resume. This explicit continuation rather than a separate "has more" boolean prevents ambiguity when the limit happens to divide the result set evenly.

## `getTransaction` and Definitive Absence

`getTransaction()` has an unusual signature: it returns `std::variant<AccountTx, TxSearched>`. This encodes a three-way distinction that is important for RPC correctness. If the transaction is found, the variant holds the `AccountTx` pair. If not found, the `TxSearched` enum signals how thorough the search was: `TxSearched::All` means the caller provided a range and every ledger in that range is present in the database (authoritative absence), `TxSearched::Some` means the range has gaps so absence is non-conclusive, and `TxSearched::Unknown` means no range was provided or a deserialization error occurred — in this last case the `error_code_i& ec` out-parameter is populated. Returning an enum rather than `bool` lets callers reason about the quality of a negative result, which is essential for serving `tx` RPC calls that need to distinguish "definitely not in any ledger we have" from "we just don't know."

## Deletion Methods

Four deletion operations expose the table-by-table structure of the store: `deleteTransactionByLedgerSeq`, `deleteBeforeLedgerSeq`, `deleteTransactionsBeforeLedgerSeq`, and `deleteAccountTransactionsBeforeLedgerSeq`. These are kept separate because the three tables may lag each other during online database rotation (managed by `SHAMapStore`), and ledger headers, transaction records, and account-transaction index rows must sometimes be pruned independently.

## Disk-Space Monitoring

`getKBUsedAll()`, `getKBUsedLedger()`, and `getKBUsedTransaction()` surface disk usage in kilobytes, allowing the application to make capacity decisions without reaching into the underlying `DatabaseCon`. `closeLedgerDB()` and `closeTransactionDB()` are needed during the database rotation cycle, where the write-ahead log is flushed and a fresh database file takes over.

## `rangeCheckedCast`: Defensive Numeric Conversion

The free function template `rangeCheckedCast<T>(C c)` is defined here rather than in a generic utilities header because it is tightly coupled to the database layer's habit of reading integer columns into types whose width differs from the column's actual precision. It enforces that the value fits in the target type at all three problematic boundaries — unsigned underflow, signed underflow, and overflow — and triggers `UNREACHABLE` plus an error log if any constraint is violated. The `LCOV_EXCL` markers confirm this is treated as an impossible path in normal execution, consistent with "this is a programming error, not a runtime error" semantics.

## Relationship to Sibling Files

`DatabaseCon.h` provides the `LockedSociSession` RAII wrapper that concrete implementations use to safely check out a `soci::session` under a recursive mutex. `DBInit.h` and `SociDB.h` supply schema initialization and the SOCI-backed session pool. The sole concrete class `SQLiteDatabase` (declared in `src/xrpld/app/rdb/backend/SQLiteDatabase.h`) holds two `std::unique_ptr<DatabaseCon>` members and delegates all SQL to helper functions in `Node.cpp`, keeping this abstract interface free of any engine-specific details.