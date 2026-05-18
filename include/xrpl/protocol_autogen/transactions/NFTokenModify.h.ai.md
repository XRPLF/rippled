# `NFTokenModify.h` — Auto-Generated Wrapper and Builder for the NFTokenModify Transaction

This file is machine-generated from the XRPL transaction schema and lives inside the `protocol_autogen/transactions/` layer. Its purpose is to expose the `ttNFTOKEN_MODIFY` transaction (type 61) through two complementary C++ abstractions: an **immutable read-only wrapper** (`NFTokenModify`) and a **fluent mutable builder** (`NFTokenModifyBuilder`). Neither class should be hand-edited; both are regenerated whenever the transaction schema changes.

## Ledger Context

`NFTokenModify` is the transaction introduced by the `featureDynamicNFT` amendment to allow the `sfURI` field of an existing NFT to be changed after the token has been minted. The original XRPL NFT design treated all fields as immutable once minted; the `DynamicNFT` amendment lifts that constraint for URI metadata only. The canonical schema, confirmed in `transactions.macro`, is:

```
TRANSACTION(ttNFTOKEN_MODIFY, 61, NFTokenModify,
    Delegation::delegable, featureDynamicNFT, noPriv,
    ({ sfNFTokenID soeREQUIRED }, { sfOwner soeOPTIONAL }, { sfURI soeOPTIONAL }))
```

The transaction is marked `Delegation::delegable`, meaning a delegate account can submit it on behalf of the actual token owner without holding the owner's keys directly.

## `NFTokenModify` — The Immutable Wrapper

`NFTokenModify` inherits from `TransactionBase`, which wraps a `std::shared_ptr<STTx const>`. The `const` propagates throughout: callers can read fields but cannot alter the underlying serialized transaction. This is the representation used after a transaction has been deserialized or signed.

The constructor takes a `shared_ptr<STTx const>` and immediately asserts that `getTxnType() == ttNFTOKEN_MODIFY`, throwing `std::runtime_error` on mismatch. This fail-fast check prevents type confusion when code routes arbitrary `STTx` objects to the wrong wrapper class.

The three transaction-specific fields map directly to the schema:

- **`getNFTokenID()`** — required; returns `SF_UINT256::type::value_type` directly (a `uint256`), no optionality.
- **`getOwner()` / `hasOwner()`** — optional; returns `protocol_autogen::Optional<SF_ACCOUNT::type::value_type>`. The `Optional<T>` alias from `Utils.h` resolves to `std::optional<T>` for non-reference types and `std::optional<std::reference_wrapper<T>>` for reference types, keeping the interface uniform without requiring callers to reason about reference lifetime.
- **`getURI()` / `hasURI()`** — optional; returns `protocol_autogen::Optional<SF_VL::type::value_type>` (a variable-length blob). The URI being optional is intentional: you can submit an `NFTokenModify` without a URI to clear it, or omit it entirely to leave it unchanged.

All getters are marked `[[nodiscard]]`. The `has*` / `get*` pairing for optional fields mirrors the pattern used throughout `TransactionBase` — call `has*` first, then `get*` to avoid a `std::nullopt` return — though `get*` safely returns `std::nullopt` internally when the field is absent.

`TransactionBase` also provides accessors for the entire common transaction field set: `getAccount()`, `getSequence()`, `getFee()`, `getFlags()`, `getMemos()`, `getSigners()`, `getDelegate()`, `getLastLedgerSequence()`, and more. `NFTokenModify` inherits all of these unchanged.

## `NFTokenModifyBuilder` — The Fluent Builder

`NFTokenModifyBuilder` extends `TransactionBuilderBase<NFTokenModifyBuilder>`. The base class is a CRTP template; all common setters return `Derived&` so that chains like `builder.setFlags(x).setLastLedgerSequence(y).setURI(uri)` resolve to the concrete builder type without casts.

Internally, the builder holds a mutable `STObject object_{sfTransaction}` declared in the base class. This is deliberately kept as a free object — `object_.set(soTemplate)` is never called — because doing so would create `STBase` placeholders for `soeDEFAULT` fields, which would then cause `applyTemplate()` to throw "may not be explicitly set to default" when the `STTx` constructor is invoked. The `STTx` constructor calls `applyTemplate()` itself and handles missing fields correctly.

The primary constructor requires `account` and `nFTokenID` (the only `soeREQUIRED` field beyond the common transaction fields), with `sequence` and `fee` as optional parameters. Required fields at the protocol level are surfaced as constructor parameters to make it impossible to call `build()` without them.

A secondary constructor accepts an existing `std::shared_ptr<STTx const>` and copies its content into `object_` via `object_ = *tx`. This round-trip path — deserialize an existing transaction, mutate it, re-sign — is useful in testing and tooling contexts.

The `build()` method calls the protected `sign()` from `TransactionBuilderBase`, which prefixes the serialized object with `HashPrefix::txSign`, calls `addWithoutSigningFields()` to exclude signing-related fields from the hash, signs the result with the provided keys, and stores the signature in `sfTxnSignature`. The builder then wraps the finalized `STObject` in a freshly constructed `STTx` and hands it to the `NFTokenModify` constructor, producing the immutable read side.

## Design Philosophy

The clean split between `NFTokenModify` (const, shared ownership, read-only) and `NFTokenModifyBuilder` (mutable, value-oriented, write-then-discard) ensures that a signed transaction can never be accidentally mutated after the fact. The autogeneration strategy means the field list, optionality, and types are always in sync with the canonical schema in `transactions.macro` without any manual synchronization. The cost is that the file cannot be customized — any protocol-level change to `NFTokenModify`'s field set automatically forces regeneration of this header.