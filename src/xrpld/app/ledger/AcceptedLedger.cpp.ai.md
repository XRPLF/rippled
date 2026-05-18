# `AcceptedLedger.cpp` — Post-Consensus Transaction Index for a Closed Ledger

## Role in the System

`AcceptedLedger` bridges the raw, immutable ledger state (a `ReadView`) and the downstream consumers that need to iterate transactions in a predictable order. Two subsystems depend on it directly: `NetworkOPs`, which fans out transaction events to WebSocket subscribers after validation, and the relational database backend (`Node.cpp`), which persists transaction records to SQLite. Both follow the same cache-or-construct pattern, checking `app.getAcceptedLedgerCache()` (a `TaggedCache<uint256, AcceptedLedger>` keyed by ledger hash) before constructing a new instance.

## What the Constructor Does

The constructor's job is entirely one-shot transformation: take every transaction from `ledger->txs`, wrap each pair of `(STTx, STObject metadata)` in an `AcceptedLedgerTx`, and sort the resulting vector by transaction sequence. There is no deferred or lazy loading — the entire ledger's transaction set is materialized at construction time and owned for the lifetime of the `AcceptedLedger` object.

The `transactions_.reserve(256)` call appears twice in the source (lines 9 and 18), which is a harmless redundancy — the second call is a no-op because the vector was already reserved. The 256-slot reservation is a heuristic to avoid reallocation for typical ledger sizes.

## `AcceptedLedgerTx` — The Wrapped Transaction

Each element in `transactions_` is an `AcceptedLedgerTx`, defined in `include/xrpl/ledger/AcceptedLedgerTx.h` and implemented in `src/libxrpl/ledger/AcceptedLedgerTx.cpp`. Its constructor eagerly builds several representations from the raw transaction and metadata:

- A `TxMeta` object derived from the `STObject` metadata blob, capturing the transaction result code, affected nodes, and the transaction's index within the ledger.
- A `flat_set<AccountID>` of all accounts touched by the transaction, derived from `TxMeta::getAffectedAccounts()`, used by `InfoSub` to route events to subscribed clients.
- Pre-serialized JSON combining the transaction, metadata, hex-encoded raw metadata, human-readable result string, and — for `ttOFFER_CREATE` transactions that are not self-funded — the owner's current balance of the `TakerGets` asset (`owner_funds`).
- A raw binary serialization of the metadata blob (`mRawMeta`) for database storage, accessed via `getEscMeta()`.

The constructor asserts `!ledger->open()`, enforcing that `AcceptedLedgerTx` objects are only created from closed ledger views. This is a correctness invariant: transaction metadata is only meaningful once the ledger is finalized.

## Sorting by Transaction Sequence

After all transactions are wrapped, the constructor sorts by `getTxnSeq()`, which returns `mMeta.getIndex()` — the transaction's ordinal position within the closed ledger, not the account-level sequence number on the `STTx`. This ordering guarantees that downstream consumers iterate transactions in the same deterministic order they were applied during consensus, which matters for database writes and for publishing events in a consistent sequence to WebSocket clients.

## Caching and Ownership Model

`AcceptedLedger` is owned exclusively via `shared_ptr` and cached by ledger hash. The `AcceptedLedger` itself holds a `shared_ptr<ReadView const>` to keep the underlying ledger view alive for the duration of any reference to its transactions. Each `AcceptedLedgerTx` is owned via `unique_ptr` inside the `transactions_` vector, so the vector has sole ownership of the wrapped objects. Callers iterate with `begin()`/`end()` and receive raw `unique_ptr` references — there is no need to copy individual transactions out of the container.

The `TaggedCache` handles cache eviction automatically based on age and memory pressure, which prevents stale `AcceptedLedger` objects from accumulating for every ledger ever validated.

## Naming Clarification

The header includes a `VFALCO TODO` comment that flags a terminology ambiguity in the XRPL codebase: "closed" means the ledger's close time has passed; "accepted" nominally refers to a ledger that passed the consensus round but has not yet accumulated sufficient validations; and "validated" refers to a ledger with full validation quorum. In practice, both `NetworkOPs` and the database backend construct `AcceptedLedger` objects from ledgers that have already been validated, meaning the class name slightly understates the certainty of the ledger state it represents.