# `SetRegularKey.h` — Auto-generated SetRegularKey Transaction Wrapper

## Role in the System

This file is part of the `protocol_autogen` layer — a family of auto-generated headers, one per transaction type, that impose a type-safe C++ interface on top of XRPL's dynamically-typed `STTx` serialization substrate. `SetRegularKey.h` covers transaction type `ttREGULAR_KEY_SET` (numeric value 5), one of the oldest and most security-sensitive transaction types on the XRP Ledger.

The `SetRegularKey` transaction allows an account holder to assign a secondary *regular key* to their account, or to remove one by omitting the field entirely. Once a regular key is set, it can be used to sign subsequent transactions in place of the account's master key. This decouples routine signing credentials from the master key pair, which is critical for operational security: the master key can be kept in cold storage while the regular key handles day-to-day activity. The optional nature of `sfRegularKey` is therefore load-bearing — an absent field signals intent to *remove* any existing regular key, not merely a missing input.

## Design: Immutable Wrapper + Builder Pair

The file defines two classes in `xrpl::transactions` that together implement the read/write split common to all autogen transaction types.

`SetRegularKey` extends `TransactionBase` and acts as an immutable, read-only view of a signed transaction. It holds a `std::shared_ptr<STTx const>` (inherited as `tx_`), making the wrapper cheap to copy and pass around while guaranteeing the underlying data cannot be mutated post-construction. The only transaction-specific accessor beyond what `TransactionBase` provides is `getRegularKey()`, which returns `protocol_autogen::Optional<AccountID>` — returning `std::nullopt` when `sfRegularKey` is absent. The companion `hasRegularKey()` method allows callers to distinguish between "field absent" and checking the value, following the pattern used for all optional fields across the autogen layer.

`SetRegularKeyBuilder` extends `TransactionBuilderBase<SetRegularKeyBuilder>` via CRTP, which is why `setRegularKey()` returns `SetRegularKeyBuilder&` rather than the base type — the template machinery resolves `static_cast<Derived&>(*this)` to the concrete type at compile time, preserving full method chaining without virtual dispatch. The builder holds a mutable `STObject` internally (initialized as an `sfTransaction` object). Fields are written directly into this object by key (`object_[sfRegularKey] = value`), and the `STTx` is only materialized at `build()` time, which atomically signs the object and constructs the immutable `SetRegularKey` wrapper.

## Type Validation as a Defensive Guard

Both constructors that accept an existing `std::shared_ptr<STTx const>` check `getTxnType()` against `ttREGULAR_KEY_SET` and throw `std::runtime_error` on mismatch. This is necessary because `STTx` is a generic property bag; nothing in the type system prevents a caller from accidentally wrapping a `Payment` with a `SetRegularKey`. The check also appears in `SetRegularKeyBuilder`'s copy-from-existing constructor, which supports a round-trip workflow (deserializing a transaction and re-wrapping it in the builder for modification before re-signing).

## Security Annotation: `notDelegable`

The macro definition in `transactions.macro` marks this transaction type `Delegation::notDelegable`. Unlike most operational transactions (offers, payments, escrow), `SetRegularKey` cannot be executed by a delegate account on behalf of another. This is a hardcoded policy decision: allowing a delegate to change another account's signing key would let a limited-trust party permanently compromise the account. The `notDelegable` annotation is surfaced in the class docstring but enforced elsewhere in the ledger's transaction processing logic, not in this header.

## Relationship to Other Files

- **`TransactionBase.h`** provides the `tx_` member and all common field accessors (`getAccount()`, `getSequence()`, `getFee()`, `getSigners()`, etc.). `SetRegularKey` inherits these without override.
- **`TransactionBuilderBase.h`** provides all common field setters and the `sign()` method (which serializes with `HashPrefix::txSign`, computes the signature, and sets `sfSigningPubKey` and `sfTxnSignature`). The deliberate choice *not* to call `object_.set(soTemplate)` in the base constructor avoids creating placeholder `soeDEFAULT` fields that would cause `applyTemplate()` to reject them when the `STTx` is constructed.
- **`transactions.macro`** is the ground truth for the field schema: `sfRegularKey` is the only transaction-specific field, and it is `soeOPTIONAL`. This matches the design where a `SetRegularKeyBuilder` with no `setRegularKey()` call produces a valid transaction that clears the regular key.
- **`AccountRoot`** in `ledger_entries` is the ledger object mutated when this transaction is applied — its own `sfRegularKey` field (also `soeOPTIONAL`) is written or cleared accordingly.

## Practical Usage Flow

A typical usage constructs the builder with the signing account, chains any optional fields, calls `build(publicKey, secretKey)` to produce a signed `SetRegularKey` object, then passes it (or its underlying `getSTTx()`) to the transaction submission layer. The split means the caller cannot accidentally submit an unsigned transaction, and once `build()` returns, the result is immutable — there is no way to modify a `SetRegularKey` after it has been created without going back through the builder.