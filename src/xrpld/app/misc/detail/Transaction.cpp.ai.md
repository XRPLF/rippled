# `Transaction.cpp` — Application-Layer Transaction Wrapper

## Role and Context

`Transaction.cpp` implements the `Transaction` class — the application-level shell that wraps the protocol-level `STTx` and tracks everything the node needs to know about a transaction's lifecycle: where it sits in the ledger, what status it carries, and how to serialize it for RPC clients. While `STTx` in `xrpl/protocol` is a pure serialization type that knows nothing about ledgers or databases, `Transaction` layers on ledger placement (`mLedgerIndex`), position within that ledger (`mTxnSeq`), submission bookkeeping (`SubmitResult`, `mApplying`), and the plumbing that connects all of that to the relational database store.

## Construction and Failure Signaling

The constructor is declared `noexcept` yet contains a `try/catch` block — an intentional design where failure is communicated via the `reason` output parameter rather than through exception propagation. If `getTransactionID()` throws (because the `STTx` is malformed or incomplete), the reason string is populated and the object is left in a partially constructed state with `mStatus` unset (it never reaches `NEW`). Callers must check whether the resulting `shared_ptr<Transaction>` is usable before proceeding. This is preferable to throwing in a constructor of a reference-counted type because it avoids the complexity of exception-safe resource cleanup in every call site.

## Status Lifecycle and SQL Mapping

The `TransStatus` enum in the header defines nine states: `NEW`, `INVALID`, `INCLUDED`, `CONFLICTED`, `COMMITTED`, `HELD`, `REMOVED`, `OBSOLETE`, and `INCOMPLETE`. These represent the full lifecycle from first receipt through final ledger inclusion or discard.

When transactions are persisted to and retrieved from the relational database, their status is stored as a single character string (the `TxnSql` encoding). `sqlTransactionStatus()` handles the reverse mapping, using `safe_cast<TxnSql>` on the first character of the status string. It bridges an intentional impedance mismatch: `boost::optional<std::string>` is used — not `std::optional` — because SOCI, the SQL library used for database access, requires `boost::optional` in its result binding interface. The `XRPL_ASSERT` on the `txnSqlUnknown` default ensures that any unrecognized database value will be caught in non-production builds.

## Deserialization from the Database: `transactionFromSQL`

`transactionFromSQL()` is the canonical factory for reconstructing a `Transaction` from rows returned by the SQL layer. It performs three sequential validation steps, each targeting a different type of corruption:

1. `rangeCheckedCast<std::uint32_t>(ledgerSeq.value_or(0))` — ensures the `uint64_t` ledger sequence from the database actually fits in the `uint32_t` used everywhere else in the protocol. A missing ledger sequence is treated as zero, consistent with the "not yet in a ledger" state.
2. `std::make_shared<STTx const>(SerialIter it)` — deserializes the raw blob via `STTx`'s serialized-form constructor, which validates structure and field types.
3. `sqlTransactionStatus(status)` — converts the stored status character to the in-memory enum as described above.

This layered approach means a corrupted database row will fail at the earliest point where the specific corruption is detectable, with each failure mode producing a different kind of diagnostic.

## The `load()` Overload Chain

Three public/private `load()` static methods form a small dispatch chain. The two public overloads — one without a range, one with a `ClosedInterval<uint32_t>` — both normalize to `std::optional<ClosedInterval<uint32_t>>` and call the private third overload, which delegates directly to `RelationalDatabase::getTransaction()`. This design keeps the public API ergonomic (callers don't need to wrap their range in an optional) while centralizing the actual database call. The return type — `std::variant<std::pair<Transaction, TxMeta>, TxSearched>` — distinguishes between finding a transaction, not finding it but having searched all ledgers in the range (`TxSearched::All`), or not finding it with only a partial search (`TxSearched::Some` or `TxSearched::Unknown`). This gives RPC handlers enough information to give meaningful responses about whether the transaction definitely does not exist versus whether the answer is simply unknown.

## JSON Output and the CTID

`getJson()` is the most complex method in the file. It builds on `STTx::getJson()` but explicitly strips the `include_date` flag before forwarding to the inner call, then re-adds the date at the outer level via `LedgerMaster::getCloseTimeBySeq()`. This separation exists because `STTx` has no ledger awareness; close-time lookup requires application context that `STTx` cannot access.

The method also handles API versioning: the deprecated `inLedger` field is emitted only when the `disable_API_prior_V2` option is absent, ensuring older clients still receive the field while newer ones see only `ledger_index`. A commented TODO notes a planned `disable_API_prior_V3` that would also suppress both `date` and `ledger_index`.

The CTID computation (XLS-15d, Concise Transaction Identifier) encodes ledger sequence, transaction position within that ledger, and network ID into a 16-hex-digit string. The priority logic is deliberate: if the `STTx` itself carries an `sfNetworkID` field, that value overrides `mNetworkID` stored on the `Transaction` object. Transactions on networks that set `sfNetworkID` are therefore self-identifying regardless of how the node's local network configuration is set, which matters in multi-network deployments and replay scenarios. `RPC::encodeCTID()` returns `std::nullopt` if any component exceeds its bit-field limit (ledger sequence above 28 bits, indices or network ID above 16 bits), so the CTID is only emitted when all components are representable.

## Concurrency Note

The `mApplying` flag and `SubmitResult` fields in the header (not implemented here, but part of the same class) are documented to be accessed exclusively under `NetworkOPsImp`'s own lock rather than a lock dedicated to `Transaction`. The comment in the header explicitly accepts weak consistency: a race on `mApplying` at worst causes a transaction to be attempted twice, which the engine handles gracefully with `tefALREADY`. This is a conscious performance tradeoff — avoiding a per-transaction lock when the consequences of the race are benign.