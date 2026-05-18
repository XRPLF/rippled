# `EscrowCreate.h` — Auto-generated Transaction Wrapper and Builder

## Role in the System

This file is part of the `protocol_autogen` layer — a collection of auto-generated headers (one per XRPL transaction type) that sit above the raw `STTx` serialized-transaction machinery. Its purpose is to give consuming code a typed, self-documenting interface to the `EscrowCreate` transaction (`ttESCROW_CREATE`, type ID 1) without requiring callers to know field codes, option codes, or the `STTx` API directly.

Every file in the `include/xrpl/protocol_autogen/transactions/` directory follows the same two-class pattern: an immutable *wrapper* that exposes typed getters, and a *builder* that accumulates fields and emits a signed `STTx`. `EscrowCreate.h` is the concrete instantiation of that pattern for on-ledger escrow creation.

## `EscrowCreate` — The Immutable Wrapper

`EscrowCreate` inherits from `TransactionBase`, which holds a `std::shared_ptr<STTx const>` (the `tx_` member). The `const`-ness of the pointer target is the key design choice: once a transaction exists on the ledger or is submitted to the network, it must never change, and the type system enforces this.

The constructor takes a `std::shared_ptr<STTx const>` and immediately validates `tx_->getTxnType() == ttESCROW_CREATE`, throwing `std::runtime_error` on mismatch. This guard is the defensive boundary between the untyped world of raw `STTx` deserialization (which can receive arbitrary transaction bytes from the network) and the typed wrapper world. Every method after construction can rely on the transaction type being correct.

The five escrow-specific fields are:

- **`sfDestination`** (`soeREQUIRED`) — the recipient `AccountID`. Accessed via `getDestination()`, which directly returns the value from `tx_->at(sfDestination)`.
- **`sfAmount`** (`soeREQUIRED`) — the locked amount. Documented as supporting MPT (Multi-Purpose Token) amounts in addition to the traditional XRP or IOU `STAmount` forms.
- **`sfCondition`** (`soeOPTIONAL`) — a variable-length blob (`SF_VL`) encoding a PREIMAGE-SHA-256 or other Crypto-Conditions fulfillment. Its presence makes the escrow cryptographically gated: `EscrowFinish` must supply the matching `sfFulfillment`.
- **`sfCancelAfter`** (`soeOPTIONAL`) — a 32-bit XRPL ripple-epoch timestamp after which the escrow can be cancelled.
- **`sfFinishAfter`** (`soeOPTIONAL`) — a 32-bit XRPL ripple-epoch timestamp before which the escrow cannot be finished.
- **`sfDestinationTag`** (`soeOPTIONAL`) — a routing hint for the destination account, allowing exchanges and services to route the incoming funds without requiring a distinct account per payer.

Optional fields follow a consistent paired pattern: a `hasX()` method checks `tx_->isFieldPresent(sfX)`, and `getX()` returns `protocol_autogen::Optional<T>` — either `std::nullopt` or the field value. The `Optional<T>` alias from `Utils.h` is a `std::conditional_t` that yields `std::optional<std::reference_wrapper<...>>` when `T` is a reference type, or `std::optional<T>` otherwise. This prevents dangling references when field types are returned by reference from the underlying `STObject`.

`TransactionBase` also supplies getters for all universal transaction fields (`sfAccount`, `sfSequence`, `sfFee`, `sfFlags`, `sfMemos`, `sfSigners`, `sfDelegate`, etc.), a `validate()` method that runs schema validation against `TxFormats::getInstance()` and then `passesLocalChecks()`, and `getSTTx()` as an escape hatch when the typed API is insufficient.

## `EscrowCreateBuilder` — The Fluent Builder

`EscrowCreateBuilder` inherits from `TransactionBuilderBase<EscrowCreateBuilder>`, which uses the Curiously Recurring Template Pattern (CRTP). The base class's common setters (`setAccount`, `setFee`, `setSequence`, `setLastLedgerSequence`, etc.) all return `Derived&` — concretely `EscrowCreateBuilder&` — enabling uniform method chaining without virtual dispatch or casting at the call site.

The internal state is a `STObject object_{sfTransaction}` (declared in `TransactionBuilderBase`), populated by direct field assignment (`object_[sfX] = value`). The builder intentionally avoids calling `object_.set(soTemplate)` on this object. That would pre-populate `soeDEFAULT` fields with placeholder entries, which would then cause the `STTx` constructor's `applyTemplate()` call to reject them with "may not be explicitly set to default." The template is applied correctly by the `STTx` constructor itself; the builder's job is only to provide the fields the caller actually sets.

The primary constructor enforces the same invariant as the wrapper: `sfDestination` and `sfAmount` are required and are set immediately via `setDestination()` and `setAmount()`. Optional fields (`sfCondition`, `sfCancelAfter`, `sfFinishAfter`, `sfDestinationTag`) have individual setters returning `EscrowCreateBuilder&`.

A second constructor accepts an existing `std::shared_ptr<STTx const>` and copies its `STObject` contents into `object_`, allowing round-trip editing of a transaction that was already deserialized. It similarly throws on type mismatch.

`build(PublicKey, SecretKey)` finalizes construction: it calls the protected `sign()` method from `TransactionBuilderBase`, which sets `sfSigningPubKey`, serializes the object with `HashPrefix::txSign` prepended (excluding signing fields), signs the bytes with the provided keys, and stores the resulting signature in `sfTxnSignature`. The signed `STObject` is then moved into a new `STTx`, which is wrapped in an `EscrowCreate` and returned.

## Design Tradeoffs and Conventions

The split between wrapper and builder reflects a deliberate immutability discipline: after `build()`, the transaction cannot be mutated, preventing accidental invalidation of the signature. The `[[nodiscard]]` annotation on every getter enforces that callers handle return values rather than discarding them silently.

The `sfAmount` field's MPT support is a forward-looking design: the XRPL is extending escrow semantics to cover Multi-Purpose Token amounts, not just XRP drops and trust-line IOUs. The autogenerated nature of this file means that when the protocol definition changes, the tooling regenerates the header with the correct field annotations — callers never hand-maintain field codes or optionality rules.