# `include/xrpl/protocol/STTx.h` — Serialized Transaction

`STTx` is the central transaction object in the XRPL protocol layer. It inherits from `STObject` (the generic serialized-field container) and adds transaction-specific identity, typing, signing, and persistence semantics. Every transaction that enters the ledger — whether submitted by a client, relayed between peers, or reconstructed from disk — is eventually represented as an `STTx`.

## Class Structure and Inheritance

`STTx` extends `STObject` (itself a polymorphic field container built on a vector of `STVar` tagged fields) and mixes in `CountedObject<STTx>` for diagnostic instance tracking. The `final` qualifier prevents further subclassing, reflecting the design intent that `STTx` is the leaf representation of a transaction, not an extensible base for transaction variants — transaction-type-specific behavior lives in the transactor subsystem, not here.

Two pieces of identity are cached on construction and kept current across mutations: `tid_` (a `uint256` SHA-512 half-hash of the serialized form prefixed with `HashPrefix::transactionID`) and `tx_type_` (a `TxType` enum value extracted from the `sfTransactionType` field). Caching both avoids repeated hash computation and repeated field lookups on hot paths.

## Construction Paths

There are three meaningful construction paths, each serving a distinct use case.

`STTx(SerialIter&)` deserializes a transaction from a byte stream. It validates the wire length against the `txMinSizeBytes`/`txMaxSizeBytes` protocol constants before parsing, then applies the `SOTemplate` for the decoded `TxType`. The rvalue-reference overload (`SerialIter&&`) simply delegates to the lvalue version — the `// NOLINT` comment acknowledges that the rvalue is not moved from, since iterators are consumed by value semantics internally.

`STTx(STObject&&)` promotes a generic `STObject` to a typed transaction. This path is used when a transaction arrives as a raw parsed object (e.g., from JSON deserialization) and must be "graduated" to a fully validated `STTx`. The `applyTemplate()` call may throw if the field layout doesn't match the required schema for the transaction type.

`STTx(TxType, std::function<void(STObject&)>)` is the programmatic construction path used by tests and internal transaction builders. The assembler callback receives the pre-templated object and fills in fields. A `LogicError` fires if the callback mutates `sfTransactionType` — a deliberate trap preventing type confusion bugs.

Copy construction is allowed, but copy assignment is explicitly deleted. This asymmetry is intentional: assignment on a typed transaction would need to re-validate and re-hash, creating opportunities for invariant violations. Copies are safe because all invariants are established at construction time.

## Signing and Signature Verification

The signing model in XRPL supports two modes: **single-sign** (one key pair) and **multi-sign** (1–32 signers from an authorized signer list). `STTx` encodes which mode is in use using the `sfSigningPubKey` field: a non-empty value signals single-sign, an empty value signals multi-sign with signers in the `sfSigners` array.

`checkSign(Rules const&)` dispatches to the appropriate private path based on `sfSigningPubKey`. The public overload also checks for `sfCounterpartySignature`, a second embedded signature object for two-party protocols — its verification failure message is prefixed with `"Counterparty: "` to distinguish it from the primary signature.

For multi-sign, `checkMultiSign()` and the shared `multiSignHelper()` function enforce several invariants: signers must be sorted in ascending `AccountID` order (enabling binary-search rejection of duplicates), no duplicates are allowed, the transaction's own account cannot appear in its own multi-signer list, and the signer count must be in `[minMultiSigners=1, maxMultiSigners=32]`. Each signer's signature covers the serialized transaction body combined with the signer's account ID (via `finishMultiSigningData()`), which prevents signature reuse across accounts.

All signature-checking methods return `Expected<void, std::string>` — a `[[nodiscard]]` type backed by `boost::outcome_v2::result` that carries either success or a human-readable error string. This is a pre-C++23 approximation of `std::expected`, avoiding exceptions on the error path while still forcing callers to handle failure explicitly.

## Batch Transaction Support

`STTx` handles the `ttBATCH` transaction type, which wraps multiple inner transactions in a single outer envelope. `getBatchTransactionIDs()` lazily computes and caches in `mutable batchTxnIds_` the hash of each raw inner transaction from `sfRawTransactions`. The result is cached because hashing inner transactions is non-trivial and the list is immutable after construction.

Batch signing uses a different data commitment than standard signing: `checkBatchSign()` iterates the `sfBatchSigners` array and for each signer verifies a signature over `serializeBatch()` output — the outer transaction's flags and the ordered list of inner transaction IDs (prefixed with `HashPrefix::batch`). This binds a batch signer to the exact set and order of inner transactions, not to the full outer transaction body. Nested `ttBATCH` transactions are explicitly rejected in `passesLocalChecks()`.

## Sequence and Ticket Handling

`getSeqProxy()` returns a `SeqProxy` that abstracts over both the classic `sfSequence` field and the newer `sfTicketSequence` mechanism. If `sfSequence` is non-zero it takes precedence; otherwise the optional `sfTicketSequence` field determines the proxy type. The `SeqProxy` comparison operators guarantee that sequence-based proxies sort before ticket-based ones, ensuring that ticket-creating transactions precede ticket-consuming transactions in processing order — an ordering property the protocol relies on.

## Fee Payer and Delegate Accounts

`getFeePayer()` returns the `sfDelegate` account if present, otherwise falling back to `sfAccount`. This supports a delegation model where one account authorizes another to act and pay fees on its behalf; the cryptographic validity and authorization of the delegate relationship are enforced separately in the transactor layer.

## Local Checks and Sterilization

`passesLocalChecks()` is a free function that validates structural correctness before a transaction is admitted to a node's local queue. It checks: memo field size and character legality (MemoType/MemoFormat must contain only RFC 3986 URL-safe characters, decoded from hex); account fields must not be the zero/default account; pseudo-transaction types (`ttAMENDMENT`, `ttFEE`, `ttUNL_MODIFY`) are rejected from client submission; MPT (Multi-Purpose Token) amounts must only appear in fields that explicitly support them; and batch inner transactions must not themselves be batch transactions.

`sterilize()` performs a serialize-then-deserialize round trip, returning a `shared_ptr<STTx const>`. This canonicalization ensures that any programmatically assembled transaction ends up in the identical binary form it would have after a network round trip — normalizing field ordering, triggering template validation, and recomputing the cached `tid_`. Any code that synthesizes transactions and then submits them to the consensus pipeline should sterilize first.

## SQL Persistence

`TxnSql` is a `char`-backed enum used to tag a transaction's status in the local SQLite `Transactions` table: `N`=new, `C`=conflict, `H`=held, `V`=validated, `I`=included, `U`=unknown. `getMetaSQL()` generates the `INSERT OR REPLACE` value tuple used for persistence, embedding the raw serialized transaction as a SQL blob literal alongside ledger sequence, status, and escaped metadata from the transaction's `STObject` representation.

## Design Notes

The placement-new `copy()` and `move()` overrides exist to support `STVar`, the tagged discriminated union that `STObject` uses as its element type. These allow `STTx` instances to be cloned into pre-allocated buffers without going through the heap, which is important for the hot copy paths in ledger building.

The assignment operator deletion, the `LogicError` in the assembler constructor, and the assertion in `getBatchTransactionIDs()` that the cached list always matches the raw array size are all examples of the defensive programming posture throughout: invariants are asserted immediately at the site where they could break, rather than letting inconsistent state propagate silently.