# `CheckCancel.h` — Auto-Generated CheckCancel Transaction Wrapper

## Role and Context

This file lives in `include/xrpl/protocol_autogen/transactions/` and is one of a family of auto-generated headers, each encapsulating a single XRPL transaction type. The `// This file is auto-generated. Do not edit.` header makes the generation provenance explicit — the code is produced from a schema description of the protocol rather than written by hand, ensuring consistency across all transaction types.

`CheckCancel` is part of the XRPL Checks feature (enabled without a named amendment, as indicated by `Amendment: uint256{}`). The feature introduces a three-transaction lifecycle: `CheckCreate` (type 16) establishes a deferred payment authorization on the ledger; `CheckCash` (type 17) lets the designated recipient claim it; and `CheckCancel` (type 18) removes the Check object from the ledger, recovering the owner's reserve. Either the original sender or the destination may submit `CheckCancel`, which is why the field set is minimal — just the ID of the check to delete.

## Class Structure

The file defines two classes that cleanly separate read from write concerns.

### `CheckCancel` — Immutable Read Wrapper

`CheckCancel` inherits `TransactionBase` and wraps a `std::shared_ptr<STTx const>`. The `const` qualifier on `STTx` propagates immutability through the entire accessor layer. The constructor enforces a hard invariant at construction time:

```cpp
if (tx_->getTxnType() != txType)
    throw std::runtime_error("Invalid transaction type for CheckCancel");
```

This type check happens even though the `STTx` was already typed — the guard ensures that a `CheckCancel` object can never silently wrap the wrong transaction kind. The `[[nodiscard]]` attribute on all getters prevents callers from accidentally discarding results.

The only transaction-specific accessor is `getCheckID()`, returning the `SF_UINT256` value stored at `sfCheckID`. This field is marked `soeREQUIRED` in the protocol schema, so the field access via `tx_->at(sfCheckID)` is safe without an existence check — `STTx::at()` will throw if the field is absent, but a well-formed ledger transaction will always have it. All common fields — account, sequence, fee, flags, memos, signers, and the optional `sfDelegate` introduced for delegated transactions — are handled by `TransactionBase`.

### `CheckCancelBuilder` — Fluent Construction via CRTP

`CheckCancelBuilder` extends `TransactionBuilderBase<CheckCancelBuilder>`, which uses the Curiously Recurring Template Pattern so that every setter in the base returns `Derived&` (the concrete builder type) rather than the abstract base. This makes method chaining work without casts at call sites:

```cpp
builder.setLastLedgerSequence(n).setCheckID(id).build(pubKey, secKey);
```

The base class holds a mutable `STObject object_{sfTransaction}` rather than an `STTx`. The reason is subtle: `STTx`'s constructor calls `applyTemplate()`, which would reject any fields set to their default value. Keeping the intermediate state as a free `STObject` sidesteps this — `applyTemplate()` only runs once, inside the `build()` call when the final `STTx` is constructed.

There are two builder construction paths. The primary one takes `account` and `checkID` as required arguments (with optional `sequence` and `fee`), immediately populating the `STObject`. The secondary constructor accepts an existing `std::shared_ptr<STTx const>` and copies its contents into `object_`, which enables a round-trip edit pattern: wrap an existing transaction, modify fields, then rebuild and re-sign. Both constructors validate the transaction type and throw `std::runtime_error` on mismatch.

`build()` calls `sign()` from the base class, which serializes the object with `HashPrefix::txSign` prepended (following RFC-style prefix signing), then attaches the resulting ECDSA/Ed25519 signature and `sfSigningPubKey` before wrapping the finalized `STTx` in a new `CheckCancel`.

## Design Decisions

The separation into immutable wrapper plus mutable builder is a deliberate API contract: once a transaction is signed and submitted to the network, nothing should be able to alter it. The `shared_ptr<STTx const>` ownership model also lets ledger processing code share the same transaction object across multiple subsystems without copying.

The `sfCheckID` field uses `std::decay_t<typename SF_UINT256::type::value_type>` as the parameter type for `setCheckID()`. `std::decay_t` strips references and cv-qualifiers, so the setter always takes the field's plain value type regardless of how the field descriptor expresses it — a defensive pattern against type aliasing differences across field descriptors in the protocol layer.

Because this is an auto-generated file, its structure is strictly parallel to `CheckCreate.h` and `CheckCash.h`. Any divergence in pattern (e.g., optional vs. required field handling, CRTP usage, signing approach) would signal a regression in the code generator.