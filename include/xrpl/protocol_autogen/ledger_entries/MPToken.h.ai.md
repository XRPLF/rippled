# `MPToken.h` — Auto-Generated MPT Holder Ledger Entry

This file is part of the `protocol_autogen` layer in the XRP Ledger codebase and defines the `MPToken` ledger entry type (`ltMPTOKEN`, type code `0x007f`). It lives in `include/xrpl/protocol_autogen/ledger_entries/` alongside roughly thirty other auto-generated ledger entry headers, all following the same structural contract. The file is machine-generated from a schema and must not be edited by hand.

## The MPT Data Model

The Multi-Purpose Token (MPT) system splits responsibility across two ledger object types that are adjacent in the type code space: `MPTokenIssuance` (`0x007e`) defines the token class itself — issuer, metadata, maximum supply, transfer fees — while `MPToken` (`0x007f`) records a single account's balance within that class. Every account that holds any nonzero amount of a given MPT issuance has exactly one `MPToken` entry on the ledger. `sfMPTokenIssuanceID`, a 192-bit field, binds the holder record back to its issuance: the identifier is derived from the issuer's 160-bit account ID concatenated with the 32-bit sequence number of the `MPTokenIssuance` creation transaction, making it a compact, globally unique key without the full 256-bit overhead of a general ledger index.

## `MPToken`: Immutable Read Wrapper

`MPToken` extends `LedgerEntryBase`, which itself wraps a `std::shared_ptr<SLE const>`. The `const`-qualified smart pointer is the architectural foundation of this whole subsystem: once an `MPToken` object is constructed, it cannot mutate the underlying serialized ledger entry. All getter methods are `[[nodiscard]] const`. This is a deliberate tradeoff — the ledger's canonical view of on-disk state should never be accidentally modified through a wrapper class; mutations are exclusively the domain of the builder half of the pair.

Type safety is enforced eagerly at construction time. The single-argument constructor calls `sle_->getType()` and throws `std::runtime_error` if the underlying SLE is not `ltMPTOKEN`. This prevents the silent misuse that would occur if, say, an `MPTokenIssuance` SLE were passed to `MPToken`.

The field schema exposes three distinct optionality tiers that directly correspond to the XRPL serialized-object template opcodes:

- **`soeREQUIRED`** fields (`sfAccount`, `sfMPTokenIssuanceID`, `sfOwnerNode`, `sfPreviousTxnID`, `sfPreviousTxnLgrSeq`) return their value types directly with no optional wrapping, because their presence is guaranteed by the ledger format rules.

- **`soeDEFAULT`** fields (`sfMPTAmount`) are returned as `protocol_autogen::Optional<T>` with a paired `hasMPTAmount()` predicate. A DEFAULT field is serialized only when it differs from its default value; `sfMPTAmount` defaults to zero. In practice, a holder entry with a zero balance would either not exist or would have `sfMPTAmount` absent from the serialized form. Callers that need the numeric value regardless of presence should fall back to zero when `getMPTAmount()` returns `std::nullopt`.

- **`soeOPTIONAL`** fields (`sfLockedAmount`) are also returned as `protocol_autogen::Optional<T>`. Unlike DEFAULT, an OPTIONAL field carries semantic weight when absent — it signals that no tokens are currently locked under any escrow or clawback mechanism, not merely that the value is zero. A caller must not equate absence of `sfLockedAmount` with a zero lock; it means the lock state is undefined for this entry.

`protocol_autogen::Optional<T>` is a thin type alias (defined in `Utils.h`) that resolves to `std::optional<std::reference_wrapper<T>>` when `T` is a reference type, and to plain `std::optional<T>` otherwise. This matters because several SF field `value_type`s are reference types, and wrapping a reference directly in `std::optional` is ill-formed in C++.

`sfOwnerNode` is a back-index into the account's owner directory `DirectoryNode` chain. It provides O(1) access to the relevant page during ledger deletion, avoiding a linear scan through potentially thousands of directory entries. `sfPreviousTxnID` and `sfPreviousTxnLgrSeq` are standard bookkeeping fields present on nearly every ledger object, recording the last transaction that touched this entry and the ledger sequence at which that happened. They are used by clients for history reconstruction and by the ledger for metadata generation.

## `MPTokenBuilder`: Fluent Construction

`MPTokenBuilder` extends the CRTP base `LedgerEntryBuilderBase<MPTokenBuilder>`. The CRTP pattern lets the base class return `Derived&` from shared setters like `setFlags()` and `setLedgerIndex()`, preserving the concrete derived type through a method-chaining call chain without virtual dispatch overhead.

The primary constructor accepts all five required fields and sets them immediately, ensuring the internal `STObject` is always in a consistent state before any optional fields are added. The alternative constructor copies an existing `SLE` directly into `object_`, supporting the pattern of loading an existing ledger entry and rebuilding it with modifications — importantly, it still validates the entry type before copying.

A subtle but critical detail lives in `LedgerEntryBuilderBase`: the constructor explicitly avoids calling `object_.set(soTemplate)`. Applying the SO template upfront would create `STBase` placeholder objects for every `soeDEFAULT` field in the template, and when the subsequent `SLE` constructor calls `applyTemplate()` internally, it would throw an exception complaining that a DEFAULT field "may not be explicitly set to default." By keeping `object_` as a free-form `STObject` and only writing fields that have actual values, the builder avoids this trap entirely.

The `build(uint256 const& index)` method moves the accumulated `STObject` into a freshly allocated `SLE` keyed by the provided 256-bit ledger index, then wraps the result in an `MPToken` reader. After `build()` returns, the builder's internal state has been moved out and should not be reused.

## Relationship to Adjacent Code

Within the autogen layer, `MPToken.h` is structurally identical to every other ledger entry header in its directory — the same inheritance chain, the same optionality pattern, the same CRTP builder idiom. The only entry-specific content is the field list and the `ltMPTOKEN` type constant. This uniformity is intentional and is what makes the code generation approach tractable: the generator emits one header per ledger entry type, each self-contained and carrying no cross-entry dependencies beyond the two shared base headers.