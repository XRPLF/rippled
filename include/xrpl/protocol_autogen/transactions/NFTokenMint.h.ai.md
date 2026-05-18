# `NFTokenMint.h` — Auto-generated NFT Minting Transaction Wrapper

## Role and Context

This file is part of the `protocol_autogen` layer inside `xrpl::transactions`, a collection of ~70 auto-generated transaction wrappers covering every XRPL transaction type. It is not intended to be edited by hand. The file defines two complementary classes for the `ttNFTOKEN_MINT` transaction type (opcode 25): `NFTokenMint`, an immutable read accessor, and `NFTokenMintBuilder`, a fluent construction interface. Together they enforce a strict separation between reading a transaction that already exists on-chain and building a new one to be submitted.

The NFTokenMint transaction creates a Non-Fungible Token on the XRP Ledger. It is marked as `delegable` (meaning another account may be authorized to issue it on behalf of the token creator) and requires the `changeNFTCounts` privilege, reflecting the ledger-state mutations it entails.

## `NFTokenMint` — Immutable Wrapper

`NFTokenMint` inherits from `TransactionBase`, which itself holds a `std::shared_ptr<STTx const>`. Const-ness is threaded all the way through: the shared pointer owns a `const`-qualified `STTx`, so neither the wrapper nor any caller can mutate the underlying serialized object. This is deliberately immutable — any modification requires going through the builder.

The constructor accepts a `std::shared_ptr<STTx const>` and immediately validates the transaction type. This guard exists because `TransactionBase` is a generic wrapper and the type is only known at the derived-class level; the runtime check at construction time is the only opportunity to enforce type safety before the typed accessors are exposed.

The class exposes one required field and six optional fields:

| Field | XRPL type | Optionality | Meaning |
|---|---|---|---|
| `sfNFTokenTaxon` | `uint32` | Required | Groups NFTs by semantic category |
| `sfTransferFee` | `uint16` | Optional | Royalty in units of 1/100,000 per transfer |
| `sfIssuer` | `AccountID` | Optional | Original issuer when minting on behalf of another account |
| `sfURI` | `Blob` | Optional | URI pointing to token metadata |
| `sfAmount` | `STAmount` | Optional | Asking price for an initial offer attached at mint time |
| `sfDestination` | `AccountID` | Optional | Restricts who may purchase the initial offer |
| `sfExpiration` | `uint32` | Optional | Ripple epoch after which the initial offer expires |

Every optional field follows the `has*()`/`get*()` accessor pair pattern rather than a single `get*()` returning `Optional<T>` with a conditional check inside. This is more than style: the `has*()` method calls `isFieldPresent()` on the `STTx` directly and is cheap; callers can branch on presence before paying the cost of field deserialization when they don't need the value.

Return types for optional getters use the `protocol_autogen::Optional<T>` alias defined in `Utils.h`:

```cpp
template <typename ValueType>
using Optional = std::conditional_t<
    std::is_reference_v<ValueType>,
    std::optional<std::reference_wrapper<std::remove_reference_t<ValueType>>>,
    std::optional<ValueType>>;
```

This alias exists because `std::optional` cannot hold a raw reference type. When a field's native C++ representation is a reference (e.g., `STArray const&` for array fields like `sfMemos`), the alias transparently wraps it in `std::reference_wrapper`. For value types — which all NFTokenMint fields are — the alias degenerates to plain `std::optional<T>`. The uniform alias lets the code generator use the same getter template regardless of whether the underlying XRPL field is a value or reference type.

All getter methods are marked `[[nodiscard]]`, preventing callers from silently discarding return values, a subtle class of bug that can occur when reading a field for side-effect (there are none, but the annotation costs nothing and documents intent).

## `NFTokenMintBuilder` — Fluent Construction

`NFTokenMintBuilder` inherits from the CRTP base `TransactionBuilderBase<NFTokenMintBuilder>`. The Curiously Recurring Template Pattern is essential here: the base class exposes setters for common fields (`setAccount`, `setFee`, `setSequence`, `setLastLedgerSequence`, etc.) that return `Derived&` rather than `TransactionBuilderBase&`. This makes method chaining work transparently across the type boundary — a call to `setLastLedgerSequence(n).setNFTokenTaxon(t)` chains correctly even though `setLastLedgerSequence` is defined in the base. Without CRTP, the base would have to return `TransactionBuilderBase&` and the chain would lose access to derived setters.

The builder stores work-in-progress state as an `STObject` named `object_` (initialized with `sfTransaction`), not as an `STTx`. Crucially, the base class constructor notes that it deliberately avoids calling `object_.set(soTemplate)`. The `STTx` constructor internally calls `applyTemplate()`, which enforces field requirements. If `soeDEFAULT` fields were pre-seeded as `STBase` placeholders in the `STObject`, `applyTemplate()` would throw "may not be explicitly set to default". Deferring to `STTx` construction at `build()` time means the template is applied exactly once, correctly, at the point where the object becomes final.

The primary constructor requires `account` and `nFTokenTaxon` (the only mandatory transaction-specific field), while `sequence` and `fee` are `std::optional` to accommodate cases such as auto-filled transactions or delegated signing workflows where these values are provided later or externally. The builder also provides a secondary constructor that accepts an existing `std::shared_ptr<STTx const>`, enabling a round-trip: deserialize a transaction, reconstruct a builder, modify optional fields, and re-sign. This pattern is useful in testing and in relay scenarios.

The `build()` method calls the protected `sign()` from `TransactionBuilderBase`, which serializes the object with `HashPrefix::txSign` prepended (following the XRPL signing convention), computes the signature over that payload, and embeds both the public key and signature into `object_`. The signed `STObject` is then moved into a new `STTx` and wrapped in a `NFTokenMint` value, consuming the builder state. The move into `STTx` avoids a copy of what can be a non-trivial serialized structure.

## Relationship to the Rest of the Module

This file is one of roughly 70 identically structured generated headers in `include/xrpl/protocol_autogen/transactions/`. Each one captures exactly the field schema of one transaction type and no more. The common infrastructure (`TransactionBase`, `TransactionBuilderBase`, `Utils.h`) is written once and parameterized through inheritance and templates, so the generated files remain minimal. The separation also means the hand-maintained base classes can evolve — adding support for new common fields or signing modes — without regenerating every transaction file.