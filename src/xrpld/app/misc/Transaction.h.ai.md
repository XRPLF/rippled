# `Transaction.h` — Application-Layer Transaction Wrapper

`Transaction.h` defines the `Transaction` class, which wraps an immutable `STTx` (the serialized on-wire transaction object) with the mutable lifecycle state the application layer needs to track a transaction from initial receipt through final commitment in a validated ledger. The distinction matters: `STTx` is a pure protocol type concerned only with encoding and signature; `Transaction` is the application's working object, understanding where the transaction sits in the submission pipeline and what outcomes have been recorded about it.

## The `TransStatus` State Machine

The `TransStatus` enum documents the nine states a transaction can occupy from the application's perspective. `NEW` means recently received; `INVALID` signals rejection (bad signature, insufficient balance); `INCLUDED` means it entered the current open ledger; `COMMITTED` means it is confirmed in a validated ledger. The transitions between these states are driven by `NetworkOPsImp` as batches are applied and ledgers close. `HELD` covers the case where the transaction is not yet valid — typically because its sequence number is ahead of the account's current sequence — and `OBSOLETE` handles the case where another transaction rendered this one unnecessary.

`isValidated()` encodes the convention that `mLedgerIndex == 0` means the transaction has not yet been committed to any validated ledger. This sentinel value is set in the constructor and cleared only when `setStatus()` records a concrete ledger sequence.

## The `mApplying` Flag and Intentional Lock Reuse

`mApplying` is a raw `bool` used to prevent a transaction from being enqueued into more than one batch simultaneously. Rather than protecting it with its own mutex, the class explicitly delegates locking to `NetworkOPsImp`'s own lock — every callsite in `NetworkOPs.cpp` already holds that lock when calling `setApplying()`, `getApplying()`, or `clearApplying()`.

The comment in the private section documents why this relaxed approach is acceptable: if a race somehow reads a stale `false`, the transaction gets re-submitted and collects `tefALREADY` or `tefPAST_SEQ`, which are harmless. If a stale `true` is read, the transaction is simply skipped for one cycle, which is equally benign. This is a deliberate performance trade-off: avoiding a second mutex in a hot path, with thoroughly documented bounded consequences if the assumption breaks.

## `SubmitResult` — Multi-Dimensional Submission Outcome

`SubmitResult` is a small four-flag struct distinguishing the independent outcomes that can follow a `submit` RPC call. A transaction can simultaneously be `applied` (tentatively applied to the open ledger), `broadcast` (sent to peer nodes), `queued` (placed in the transaction queue), and `kept` (placed in the local transaction set for retry). These outcomes are not mutually exclusive. The `any()` helper reduces the struct to the single `accepted` field the client sees in the JSON response. The submit RPC handler in `Submit.cpp` reads all four flags and emits them individually, giving clients a precise diagnostic of what happened to their transaction.

## `CurrentLedgerState` — Snapshot for Client Diagnostics

`CurrentLedgerState` captures a point-in-time view of the ledger as seen when the transaction was processed: the last validated ledger index, the minimum fee required to enter the open ledger, and two sequence numbers for the submitting account (`accountSeqNext` — the next sequence expected from the account, and `accountSeqAvail` — the next sequence the transaction queue will accept). This snapshot is written by `NetworkOPsImp` during transaction processing and read by the submit RPC handler to populate `account_sequence_next`, `account_sequence_available`, `open_ledger_cost`, and `validated_ledger_index` in the response. The field is `std::optional` because it is only meaningful after processing; a freshly constructed `Transaction` carries none of it.

## `Locator` and the `load()` Family

`Locator` is a type-safe discriminated union over two alternatives using `std::variant`: either a `std::pair<uint256, uint32_t>` (a nodestore hash and ledger sequence identifying an exact match), or a `ClosedInterval<uint32_t>` recording the ledger range that was searched and came up empty. Callers must check `isFound()` before calling either `getNodestoreHash()` / `getLedgerSequence()` or `getLedgerRangeSearched()`; calling the wrong getter throws via `std::get`.

The three public `load()` overloads — one without a range, one with a concrete `ClosedInterval<uint32_t>`, and a private canonical one taking `std::optional<ClosedInterval<uint32_t>>` — funnel into a single implementation that calls through to `RelationalDatabase::getTransaction()`. The range parameter allows RPC callers that already know the ledger range their node has available to pass that constraint in, enabling a meaningful `TxSearched` response (indicating whether all, some, or an unknown portion of history was examined) rather than a bare not-found.

The return type of `load()` is itself a `std::variant` between a `(Transaction, TxMeta)` pair when the transaction is found and a `TxSearched` enum value when it is not. This design avoids nullable pointer gymnastics and communicates the absence reason precisely.

## SQL Compatibility and `transactionFromSQL`

Two static members — `transactionFromSQL()` and `sqlTransactionStatus()` — accept `boost::optional` parameters rather than `std::optional`. This is not an oversight; the SOCI database library used throughout `rippled` requires `boost::optional` for nullable column binding, and changing the parameter types would break the SQL integration layer. `sqlTransactionStatus()` maps a single-character SQL status code to a `TransStatus` enum value, isolating the SQL schema representation from the rest of the class.

## `getJson()` and CTID Output

`getJson()` calls through to `STTx::getJson()` but explicitly strips the `include_date` option before passing it down, then re-applies date lookup using `LedgerMaster::getCloseTimeBySeq()` if the option was requested. This two-step indirection exists because the ledger close time is not stored inside the serialized transaction; it must be fetched from `LedgerMaster` using the `mLedgerIndex`. The method also computes a Concise Transaction Identifier (CTID, from XLS-15d) if both `mTxnSeq` and a network ID are available, encoding the ledger sequence, transaction index within the ledger, and network ID into a compact hex string added under `jss::ctid`. The `inLedger` field is emitted only when the API version predates V2, preserving backward compatibility while `ledger_index` becomes the canonical field going forward.