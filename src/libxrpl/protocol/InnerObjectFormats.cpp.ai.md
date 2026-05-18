# `InnerObjectFormats.cpp` — Inner Object Schema Registry

This file contains the sole definition of `InnerObjectFormats`, a singleton registry that declares the canonical field schemas for every structured sub-object that can appear inside an XRPL serialized object (`STObject`). It is the inner-object counterpart to `TxFormats` and `LedgerFormats` — those registries govern top-level transaction and ledger-entry shapes, while `InnerObjectFormats` governs the nested objects that those top-level structures embed.

## Role in the Serialization System

XRPL's serialized object model is built around `STObject`, a heterogeneous field container. When an `STObject` carries a known "inner object" sub-field (e.g. a `SignerEntry` inside a signer list, or an `NFToken` inside a page), the runtime needs a declared template to enforce which child fields are required, which are optional, and which default. `InnerObjectFormats` is that declaration point.

The class inherits from the CRTP base `KnownFormats<int, InnerObjectFormats>`. That base template manages a `std::forward_list<Item>` — chosen deliberately because `Item` objects must have stable addresses after insertion (they are referred to by pointer from two `boost::container::flat_map` indexes, one keyed by string name and one by integer type code). Each `Item` wraps an immutable `SOTemplate`, the name, and the type code derived from the corresponding global `SField` object via `getCode()`.

## Construction and the `add()` Pattern

The private constructor is the entire registration mechanism. Each call to `add()` supplies:

1. `sField.jsonName` — the human-readable name used in JSON serialization and debug output.
2. `sField.getCode()` — the compact integer that uniquely identifies the field type. Using the field's own code as the key means the same `SField` that names the outer wrapper is also the lookup key; no separate enum is needed.
3. An initializer list of `SOElement` pairs `{sfFieldRef, style}` specifying child fields as `soeREQUIRED`, `soeOPTIONAL`, or `soeDEFAULT`.

The choice of `int` as `KeyType` rather than a dedicated enum is intentional: inner objects don't form their own transaction-type space, so using an integral field code avoids coupling the format registry to a separate enumeration and reuses the existing `SField` identity mechanism.

## `soeDEFAULT` vs `soeOPTIONAL` — A Subtle Distinction

`soeREQUIRED` and `soeOPTIONAL` behave intuitively. `soeDEFAULT` is more subtle: a field marked `soeDEFAULT` *may* be absent in serialized form, but if it *is* present, it must **not** carry its default value. The serializer omits default-valued fields to keep the wire encoding compact. Both `sfTradingFee` in `sfVoteEntry` and `sfDiscountedFee` in `sfAuctionSlot` and `sfScale` in `sfPriceData` use `soeDEFAULT` for exactly this reason — a zero fee or zero scale is semantically the absence of that field, not an explicit zero.

## Registered Inner Objects and Their Feature Context

The constructor registers sixteen inner object types spanning the full breadth of XRPL features:

**Core protocol objects**: `sfSignerEntry` (account + weight + optional locator for signer lists) and `sfSigner` (the cryptographic signature wrapper inside a multi-signed transaction) were among the earliest inner objects. `sfMajority` tracks amendment vote status — which amendment hash reached quorum and at what ledger close time. `sfDisabledValidator` records a validator that has been excluded from the UNL.

**NFT support**: `sfNFToken` carries a token ID and optional URI, appearing in `NFTokenPage` ledger objects.

**AMM (Automated Market Maker)**: `sfVoteEntry` and `sfAuctionSlot` represent AMM governance objects. These two received their templates in the *first* amendment wave (`fixInnerObjTemplate`) before the remaining inner objects were covered by `fixInnerObjTemplate2`. This phased rollout is explicitly called out in `STObject::makeInnerObject()`.

**Cross-chain bridge attestations**: Four objects (`sfXChainClaimAttestationCollectionElement`, `sfXChainCreateAccountAttestationCollectionElement`, `sfXChainClaimProofSig`, `sfXChainCreateAccountProofSig`) encode witness signatures for XRPL's cross-chain bridge protocol. The "collection element" variants include a `sfSignature` field (the raw attestation) while the "proof sig" variants do not — they represent the condensed proof submitted to finalize a claim, where the signature has already been verified.

**Newer additions**: `sfAuthAccount` (a single-account authorization entry used in AMM and other features), `sfPriceData` (an oracle price entry with asset pair, optional price, and scale), `sfCredential` (issuer + credential type for identity attestation), `sfPermission` (a single permission value in a delegated-permission list), `sfBatchSigner` (a signer in a batch transaction with optional multi-sig via nested `sfSigners`), `sfBook` (order-book reference with directory and node), and `sfCounterpartySignature` (a flexible optional-key structure supporting both single and multi-signature counterparty authorization).

## Lookup and Template Application

`getInstance()` returns a Meyer's singleton — the static local is initialized exactly once and is then immutable for the lifetime of the process. This is safe because `InnerObjectFormats` is not copyable (the `KnownFormats` base deletes copy constructor and assignment).

`findSOTemplateBySField()` is the single external query point. It delegates to `findByType(sField.getCode())` and, if an `Item` is found, returns a pointer to its `SOTemplate`. Callers receive `nullptr` for any `SField` not in the registry.

The two callers in `STObject.cpp` illustrate the two usage paths:

- `STObject::makeInnerObject()` uses the template to initialize a freshly-constructed inner object, but only when the appropriate amendment rules (`fixInnerObjTemplate` or `fixInnerObjTemplate2`) are enabled — enforcing that template validation is applied consistently with the network's current rule set.
- `STObject::applyTemplateFromSField()` applies the template after deserialization, ensuring that binary data read from the network or ledger conforms to the declared schema before further processing.

In both paths, the `SOTemplate` drives `STObject`'s field-presence validation: required fields that are absent, or unknown fields that appear, result in errors surfaced at object construction or deserialization time — making `InnerObjectFormats` a key enforcement layer in XRPL's type safety story.