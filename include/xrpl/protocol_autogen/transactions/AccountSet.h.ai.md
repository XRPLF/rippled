# `AccountSet.h` — Auto-Generated AccountSet Transaction Wrapper

## Role and Context

This file is part of the `protocol_autogen` layer — a code-generated tier of the XRPL codebase that sits above the raw `STTx` serialization infrastructure and exposes each transaction type through a typed C++ API. The file lives alongside ~70 other transaction wrappers in `include/xrpl/protocol_autogen/transactions/`, all following the same two-class pattern: an immutable reader and a fluent builder.

`AccountSet` is one of the oldest and most flexible XRPL transaction types (`ttACCOUNT_SET = 3`). It allows an account holder to modify a collection of account-level properties in a single on-ledger operation: display metadata like `sfDomain` and `sfEmailHash`, behavioral settings like `sfTransferRate` and `sfTickSize`, cryptographic fields like `sfMessageKey`, and account flags controlled through `sfSetFlag`/`sfClearFlag`. No field is required beyond what `TransactionBase` mandates — every AccountSet-specific field is `soeOPTIONAL`.

## Class `AccountSet` — Immutable Type-Safe Wrapper

`AccountSet` inherits from `TransactionBase`, which itself wraps a `std::shared_ptr<STTx const>` and provides getters for universal fields like `sfAccount`, `sfSequence`, `sfFee`, `sfMemos`, and `sfDelegate`. The `AccountSet` subclass adds only the transaction-specific optional fields.

The constructor accepts a `shared_ptr<STTx const>` and immediately validates the transaction type against `ttACCOUNT_SET`, throwing `std::runtime_error` on mismatch. This fail-fast guard prevents a caller from accidentally wrapping the wrong transaction type and getting silent field-access failures later.

Every AccountSet-specific field is exposed through a paired `get`/`has` interface. The `hasXxx()` method calls `isFieldPresent()` on the underlying `STTx`, and `getXxx()` returns `protocol_autogen::Optional<T>` — a type alias defined in `Utils.h`. That alias resolves to `std::optional<std::reference_wrapper<std::remove_reference_t<T>>>` when `T` is a reference type, and `std::optional<T>` otherwise. This indirection handles the case where the field type is itself a reference (e.g., blob types returned by reference from the serialized object) without forcing a copy. All getters are marked `[[nodiscard]]` to prevent callers from silently ignoring returned values.

The ten AccountSet-specific fields span several wire types: `sfEmailHash` (UINT128), `sfWalletLocator` (UINT256), `sfWalletSize` and `sfTransferRate` and `sfSetFlag` and `sfClearFlag` (UINT32), `sfTickSize` (UINT8), `sfMessageKey` and `sfDomain` (variable-length blobs, `SF_VL`), and `sfNFTokenMinter` (ACCOUNT). The variety of types highlights why the autogen layer is valuable — callers get correctly typed values rather than manually casting raw field data out of `STObject`.

## Class `AccountSetBuilder` — Fluent CRTP Builder

`AccountSetBuilder` extends `TransactionBuilderBase<AccountSetBuilder>`, a CRTP template. The curiously recurring template pattern is the key design choice here: `TransactionBuilderBase` holds all the common field setters (`setAccount`, `setFee`, `setSequence`, `setFlags`, `setLastLedgerSequence`, etc.) and each returns `Derived&` — which resolves at compile time to `AccountSetBuilder&`. This means callers can chain AccountSet-specific setters with base-class setters interchangeably without any virtual dispatch or awkward down-casting.

The builder maintains an `STObject object_{sfTransaction}` (declared in the base class) as its mutable scratch object. Importantly, `TransactionBuilderBase`'s constructor deliberately does not call `object_.set(soTemplate)`. As the comment in that constructor explains, calling `applyTemplate()` on an `STObject` before the `STTx` constructor runs creates `STBase` placeholder entries for `soeDEFAULT` fields, which then causes the `STTx` constructor to throw "may not be explicitly set to default." By keeping `object_` as a free (template-less) STObject and letting the `STTx` constructor call `applyTemplate()` itself, the builder avoids this trap.

The builder offers two construction paths. The primary path takes an `AccountID`, and optional `sequence` and `fee`, setting up the required fields immediately. The second path accepts an existing `std::shared_ptr<STTx const>` and copies the underlying `STObject` content — useful for modifying a partially-constructed or externally-parsed transaction. This second path also validates the transaction type, throwing on mismatch.

Setter methods use `std::decay_t<typename SF_UINT32::type::value_type> const&` as parameter types. The `std::decay_t` strips any reference-ness from the SField's native value type, ensuring the setter always takes a value or const reference cleanly regardless of how the SField typedef is defined internally.

The `build()` method calls the protected `sign()` helper from `TransactionBuilderBase`, which sets `sfSigningPubKey`, serializes the object (excluding signing fields) with `HashPrefix::txSign` prepended, signs the resulting buffer, stores the signature in `sfTxnSignature`, then constructs and returns an `AccountSet` wrapper around the finalized `STTx`.

## Design Notes

`AccountSet` is marked `Delegation::notDelegable`. Despite `TransactionBase` exposing `getDelegate()` (inherited from the universal field layer), this transaction cannot be submitted through the delegation mechanism — the ledger will reject it. This is enforced at the protocol/validation layer, not at the C++ type level, so the wrapper does not prevent setting `sfDelegate` through the builder; the ledger will reject the transaction at apply-time.

The file header states "This file is auto-generated. Do not edit." The autogen layer is driven from a transaction schema definition that encodes field names, wire types, and optionality. Because all AccountSet-specific fields happen to be optional, the generated `AccountSet` class contains no required-field accessors — a non-obvious consequence of the schema. Validation of which fields actually need to be present for a semantically meaningful AccountSet operation (e.g., providing at least one of `SetFlag`/`ClearFlag`/`Domain`) is handled by the ledger's `doApply` logic, not by this wrapper.