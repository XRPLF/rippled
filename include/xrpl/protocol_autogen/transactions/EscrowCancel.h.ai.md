# `EscrowCancel.h` — Auto-Generated EscrowCancel Transaction Wrapper and Builder

## Role in the System

This file is part of the `xrpl/protocol_autogen/transactions/` layer — a code-generated set of per-transaction-type headers that give C++ consumers a strongly-typed, compile-time-checked interface over the XRPL `STTx` serialization format. The `EscrowCancel` transaction (`ttESCROW_CANCEL`, type 4) allows any account to reclaim XRP locked in an escrow object after that escrow's expiry condition is met. The two fields it requires beyond the common transaction fields — `sfOwner` and `sfOfferSequence` — uniquely identify the escrow: the account that originally created it and the sequence number of that `EscrowCreate` transaction.

The file is marked `// This file is auto-generated. Do not edit.`, meaning the source of truth is a transaction schema definition file elsewhere in the build system, and this header is regenerated when the schema changes. Manual edits would be overwritten.

## Class Structure: Wrapper/Builder Pair

The file defines two classes that work as a pair. This pattern appears consistently across every transaction type in `protocol_autogen/transactions/` (e.g., `EscrowCreate.h`, `EscrowFinish.h`).

**`EscrowCancel`** is an immutable read-side wrapper. It extends `TransactionBase`, which holds a `std::shared_ptr<STTx const>` as `tx_` and provides accessors for all fields common to every transaction (account, sequence, fee, flags, memos, signers, etc.). `EscrowCancel` adds only the two domain-specific getters:

- `getOwner()` — returns the `AccountID` of the escrow's original creator via `sfOwner`
- `getOfferSequence()` — returns the `uint32_t` sequence number of the `EscrowCreate` via `sfOfferSequence`

Both are annotated `[[nodiscard]]` and declared `const`, which is consistent with the immutability contract: once an `EscrowCancel` is constructed, its state cannot change.

The constructor validates the transaction type immediately, throwing `std::runtime_error` if the wrapped `STTx` is not `ttESCROW_CANCEL`. This is a defensive guard against misuse when the object is constructed from a raw `shared_ptr<STTx const>` obtained from an external source (e.g., deserialization, ledger replay), where the type cannot be verified at compile time.

**`EscrowCancelBuilder`** is the mutable write-side counterpart. It uses CRTP via `TransactionBuilderBase<EscrowCancelBuilder>`, which stores a mutable `STObject object_{sfTransaction}` and provides fluent setters for all common fields (`setSequence()`, `setFee()`, `setFlags()`, `setLastLedgerSequence()`, `setDelegate()`, etc.), all returning `Derived&` so calls can be chained.

The builder requires `owner` and `offerSequence` as mandatory constructor arguments (alongside `account`) because these are `soeREQUIRED` fields in the XRPL transaction schema — the transaction is invalid without them. Sequence and fee are `std::optional`, reflecting the common case where they are auto-filled by a library or server rather than specified by the caller.

A second constructor overload accepts an existing `std::shared_ptr<STTx const>`, allowing a transaction to be loaded from the ledger and then modified (e.g., to re-sign with different keys). It performs the same type guard as the wrapper constructor.

## The `build()` Method and Signing Flow

`build(PublicKey const& publicKey, SecretKey const& secretKey)` finalises the transaction by:

1. Calling the protected `sign()` method from `TransactionBuilderBase`, which sets `sfSigningPubKey`, serializes the object prefixed with `HashPrefix::txSign`, and stores the resulting `sfTxnSignature`.
2. Moving `object_` into a new `STTx`, then wrapping it in `EscrowCancel`. The move into `STTx` triggers `applyTemplate()`, which validates field presence against the registered `TxFormats` schema.

Signing happens in the builder before constructing the `STTx`, specifically because `STTx` is immutable once created — you cannot amend a signature after the fact. The flow enforces that the final `EscrowCancel` wrapper is always in a signed, schema-validated state.

## Key Design Decisions

**Why not use `std::decay_t` in getter return types?** The getters return `SF_ACCOUNT::type::value_type` and `SF_UINT32::type::value_type` directly. These are the underlying primitive types (`AccountID` and `uint32_t` respectively). Returning by value from `tx_->at(...)` is safe because `STTx` is `const` and the accessor performs a copy.

**Why `std::decay_t` in setter parameter types?** The builder's `setOwner` and `setOfferSequence` accept `std::decay_t<typename SF_ACCOUNT::type::value_type> const&`. The `std::decay_t` strips any reference or cv-qualifiers from the field's value type, ensuring the function signature binds to a plain const reference regardless of whether the underlying type is itself a reference type. This is defensive template hygiene for generated code.

**Why separate wrapper and builder?** The split enforces the XRPL immutability contract at the type level: `STTx` objects are never modified after creation, which is critical for ledger integrity and thread safety. Code that reads transaction data is handed an `EscrowCancel`, not a builder; it cannot accidentally mutate state.

## Relationship to Other Files

- `TransactionBase.h` — superclass providing common field accessors and `validate()`, which calls `passesLocalChecks` and schema validation via `TxFormats`.
- `TransactionBuilderBase.h` — CRTP superclass providing the mutable `STObject` store, common setters, and the `sign()` implementation.
- `EscrowCreate.h` and `EscrowFinish.h` — sibling files following the identical pattern for the other two legs of the escrow lifecycle (creation and fulfilment).
- `xrpl/protocol/STTx.h` — the underlying serialized transaction type that both the wrapper and builder ultimately consume.