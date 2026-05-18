# `STTx.cpp` — XRPL Transaction Core: Construction, Signing, and Validation

`STTx` is the canonical representation of an XRP Ledger transaction in the C++ implementation. It inherits from `STObject` but enforces a set of invariants at construction time that a plain `STObject` does not: the transaction type must be registered in `TxFormats`, the field layout must conform to the type's `SOTemplate`, and the 256-bit transaction ID (`tid_`) is computed once from the content and then cached. This file contains all three constructors, the complete signing and signature-verification machinery, local pre-submission validity checks, and SQL persistence helpers.

## Three Construction Paths, One Set of Invariants

Every `STTx` enters one of three constructors and all three must terminate with `tid_ = getHash(HashPrefix::transactionID)`. The cached hash is not a convenience — transaction IDs are the primary lookup key across the entire node, so computing them eagerly once at construction eliminates repeated hashing.

**Wire deserialization** (`STTx(SerialIter&)`) is the hottest path at runtime: every inbound transaction and every ledger transaction loaded from disk passes through it. Before parsing any fields the constructor checks the raw byte count against `txMinSizeBytes` (32 bytes) and `txMaxSizeBytes` (1 MB). Enforcing these bounds before invoking `set(sit)` prevents pathological input from reaching the field-parsing loop. After parsing, `set(sit)` returning `true` means an object terminator was found inside the byte stream, which is structurally invalid for a top-level transaction, so the constructor throws.

**Object promotion** (`STTx(STObject&&)`) is used when an `STObject` has already been parsed — for example when reconstructing a transaction from JSON. No size check is needed since the object is already in memory; the same `applyTemplate` call enforces field conformance.

**Programmatic construction** (`STTx(TxType, assembler)`) is used by tests and transaction-building code. The constructor installs the `SOTemplate` first so the object has the correct field scaffolding, then calls the caller-supplied `assembler` lambda, and finally reads back `sfTransactionType` to verify it wasn't mutated. If the type changed during assembly, `LogicError` fires rather than `std::runtime_error` — the distinction matters: `LogicError` signals a programming mistake, not a data error.

## Signing Architecture

XRPL supports four distinct signing modes and `STTx` handles all of them.

**Single signing** is detected by a non-empty `sfSigningPubKey`. The signing payload is the transaction content serialized without the signature fields, prefixed with `HashPrefix::txSign`. The static helper `getSigningData()` produces this payload. Crucially, both single-sign and multi-sign must be mutually exclusive on the same object: `singleSignHelper()` rejects a transaction that has both `sfSigningPubKey` populated and `sfSigners` present, preventing a transaction from being simultaneously signed two ways.

**Multi-signing** is detected by an empty `sfSigningPubKey` paired with a non-empty `sfSigners` array. The `multiSignHelper()` function processes each signer entry with three enforcements: the transaction owner may not appear as one of their own multi-signers (the `txnAccountID` parameter carries this check), signers must appear in strictly ascending `AccountID` order (no duplicates), and signers must be within the 1–32 range (`STTx::minMultiSigners` / `STTx::maxMultiSigners`). Each signer's actual verification message is constructed by taking the shared prefix (`startMultiSigningData`) and appending the signer's `AccountID` via `finishMultiSigningData`. This per-signer suffix prevents a valid multi-signature from being replayed by a different account in the same or another transaction.

**Batch signing** (`checkBatchSign()`, `checkBatchSingleSign()`, `checkBatchMultiSign()`) uses a completely different message format. The signed data is produced by `serializeBatch()` — a hash prefix specific to batches, the outer transaction flags, and the IDs of all inner transactions. Batch signers are authorizing a specific set of inner transactions, not the outer envelope fields. This means a batch re-signed with the same inner transactions but different outer flags would not verify against an existing batch signature.

**Counterparty signing** (`sfCounterpartySignature`, currently used by `LoanSet`) allows a second party to sign the same transaction. The public `checkSign(Rules const&)` overload checks the primary signature and then, if `sfCounterpartySignature` is present, checks it using the same single/multi-sign dispatch. Errors from the counterparty check are prefixed with `"Counterparty: "` so callers can distinguish which signer failed. The `sign()` method accepts an optional `signatureTarget` reference so it can write the counterparty signature into the sub-object rather than the main `sfTxnSignature` field.

## Fee Delegation

`getFeePayer()` returns `sfDelegate` if that field is present, otherwise `sfAccount`. The comment in the implementation is architecturally important: the *authorization* for a delegate to act on behalf of the account is enforced separately in `Transactor::checkPermission`, while the *cryptographic validity* of the delegate's signature is enforced in `Transactor::checkSign`. `getFeePayer()` itself does no authorization — it only resolves which account's balance pays the fee.

## Sequence and Ticket Unification

`getSeqProxy()` returns a `SeqProxy` that unifies the classic `sfSequence` field with the newer `sfTicketSequence` field. When `sfSequence` is zero and `sfTicketSequence` is present, the transaction uses a ticket. The `SeqProxy` comparison operators guarantee that sequence-type values always sort before ticket-type values, which ensures that transactions that create tickets (sequence-based) sort ahead of transactions that consume them (ticket-based) in processing order.

## Local Pre-Submission Checks

`passesLocalChecks()` is a free function rather than an `STTx` method because it operates on any `STObject` — it runs before the object is necessarily promoted to a full `STTx`. It gates local transaction relay and submission:

- `isMemoOkay()` enforces a 1024-byte total memo size (measured after serialization to catch any encoding overhead) and validates that `MemoType` and `MemoFormat` fields decode from hex and contain only RFC 3986 URL-safe characters. The character whitelist is a `constexpr`-initialized 256-element lookup table, computed once at program start, giving O(1) per-character validation without runtime branching.
- `isAccountFieldOkay()` walks the object's fields looking for any `STAccount` holding the default (zero) value, which would represent an uninitialized account ID.
- `isPseudoTx()` blocks submission of amendment, fee, and UNL-modify transaction types. These are system-generated by the ledger itself and must never arrive from external clients.
- `invalidMPTAmountInTx()` consults the `SOTemplate` for each field's MPT support flag (`soeMPTSupported` vs. `soeMPTNone`). If an `STAmount` or `STIssue` field holds an `MPTIssue` but the template does not declare that field as MPT-capable, the check fails.
- `isRawTransactionOkay()` validates the `sfRawTransactions` array for batch transactions: the array is capped at `maxBatchTxCount` (8), nested `ttBATCH` transactions inside the array are forbidden (no batch-of-batches), and each inner transaction's type must pass `applyTemplate()`.

## Batch Transaction ID Caching

`getBatchTransactionIDs()` uses a `mutable std::vector<uint256> batchTxnIds_` for lazy initialization. The IDs are computed on first call by hashing each entry in `sfRawTransactions` and are never recomputed. An assertion on subsequent calls verifies that the cached vector size still matches the `sfRawTransactions` array size, enforcing the invariant that inner transactions cannot be modified after the IDs have been observed.

## Sterilization

`sterilize()` performs a serialize-then-deserialize round-trip: it serializes the `STTx` to bytes via `add()`, then constructs a new `STTx` from those bytes via `SerialIter`. The result is a canonical-form transaction where all equivalent representations collapse to the same byte sequence. This is used when a transaction arrives in a non-canonical in-memory form (for instance, constructed via JSON) and needs to be stored or compared against wire-format transactions.

## SQL Persistence

`getMetaSQL()` and `getMetaSQLInsertReplaceHeader()` produce a parameterized SQL row for the `Transactions` database table. The row includes the transaction ID, type name, source account, sequence number, ledger sequence, a single-character status code (`TxnSql` enum), the raw serialized transaction blob, and pre-escaped metadata. The comment marking this as a potential free function elsewhere (`// VFALCO This could be a free function elsewhere`) signals it's considered an architectural oddity — persistence concerns sitting directly on the domain object — but it remains here for historical reasons.