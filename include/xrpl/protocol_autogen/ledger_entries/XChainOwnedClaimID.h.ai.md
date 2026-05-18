# `XChainOwnedClaimID.h` — Cross-Chain Claim ID Ledger Entry Wrapper

## Purpose and Context

This file is part of the `protocol_autogen` subsystem — a layer of auto-generated C++ that wraps the XRPL ledger's raw serialized objects (`SLE`) with type-safe, field-specific accessors. The `// This file is auto-generated. Do not edit.` pragma signals that it is produced by a code generator from a schema definition and must not be modified by hand.

`XChainOwnedClaimID` (ledger type `ltXCHAIN_OWNED_CLAIM_ID`, tag `0x0071`) is a ledger entry created when an account initiates a cross-chain value transfer via the XRPL XChain Bridge feature. Conceptually, a user locks funds on a source chain and submits an `XChainCreateClaimID` transaction. This creates an `XChainOwnedClaimID` entry on the destination chain's ledger. That entry then accumulates signed attestations from the bridge's trusted witness servers until a quorum is reached, at which point the corresponding `XChainClaim` transaction can release the equivalent funds to the recipient.

The entry therefore acts as an accumulation point for cross-chain evidence. It is distinct from `XChainOwnedCreateAccountClaimID` (type `0x0074`), which handles the more specialized case of creating a brand-new account on the destination chain rather than crediting an existing one.

## Key Fields

All nine fields exposed by `XChainOwnedClaimID` are marked `soeREQUIRED`:

- **`sfAccount`** — the XRPL account that owns this claim ID on the destination chain. This account pays the reserve and will ultimately receive the transferred value.
- **`sfXChainBridge`** — a composite field (`SF_XCHAIN_BRIDGE`) identifying the specific bridge: it encodes the issuing account, locking chain door, and issuing chain door.
- **`sfXChainClaimID`** — a monotonically increasing `uint64` sequence number that uniquely identifies this claim within the bridge. It prevents replay attacks by ensuring each transfer has a unique handle.
- **`sfOtherChainSource`** — the account on the source chain that originated the transfer. Witnesses use this to correlate on-chain events.
- **`sfXChainClaimAttestations`** — an `STArray` of attestation objects collected from bridge witnesses. Each element encodes a witness's signature confirming the source-chain transaction. Because the field type is not a simple scalar, it is accessed via the `getFieldArray` / `setFieldArray` SLE methods rather than the generic `at()` operator; the comment labels it "untyped (unknown)" reflecting the code generator's handling of array fields that lack a precise static type.
- **`sfSignatureReward`** — an `STAmount` paid out to the witnesses in exchange for their attestations. This is the economic incentive that keeps the bridge's witness network functioning.
- **`sfOwnerNode`** — a `uint64` back-pointer into the owner directory that tracks which directory page holds this entry. Standard XRPL bookkeeping for owner reserve accounting.
- **`sfPreviousTxnID`** and **`sfPreviousTxnLgrSeq`** — the canonical transaction audit trail carried by all mutable ledger objects: the hash and ledger sequence of the last transaction that modified this entry.

## Class Design: Wrapper and Builder

The file defines two classes following the same pattern used across all ~30 auto-generated ledger entry types in this directory.

### `XChainOwnedClaimID` (read-only wrapper)

`XChainOwnedClaimID` inherits from `LedgerEntryBase`, which holds a `std::shared_ptr<SLE const>` and exposes common fields (`getType()`, `getKey()`, `getFlags()`, `validate()`). The `const` on the SLE pointer is deliberate: the wrapper is intentionally immutable. All field getters are marked `[[nodiscard]]` to prevent silent discard of returned values. The constructor immediately validates the SLE's type code against the class's `static constexpr entryType`, throwing `std::runtime_error` on mismatch. This is a hard fail rather than a silent no-op — consuming code that obtains the wrong SLE from the ledger will crash loudly rather than silently reading garbage field values.

### `XChainOwnedClaimIDBuilder` (mutable construction)

`XChainOwnedClaimIDBuilder` inherits from `LedgerEntryBuilderBase<XChainOwnedClaimIDBuilder>`, which uses CRTP (Curiously Recurring Template Pattern) so that `setLedgerIndex()` and `setFlags()` defined in the base return a reference to the concrete derived type rather than to the base. This enables uninterrupted method chaining even when mixing base-class and derived-class setters.

The builder stores its in-progress state as an `STObject object_{sfLedgerEntry}` (a "free" object without a template applied). A deliberate design note in `LedgerEntryBuilderBase` explains why the SO template is *not* applied eagerly: setting `soeDEFAULT` placeholder values on the `STObject` before constructing the `SLE` would cause `applyTemplate()` to throw a "may not be explicitly set to default" error. By keeping the builder's `STObject` template-free and deferring template application to the `SLE` constructor, the builder avoids this hazard.

The builder offers two construction paths: a full-argument constructor that enforces every required field at construction time, and a second constructor that ingests an existing `std::shared_ptr<SLE const>` by copying the `SLE`'s raw `STObject` into `object_`. The copy-from-SLE path enables round-trip mutation: read a ledger entry, wrap it in a builder, update specific fields, then call `build()` to produce a fresh immutable `XChainOwnedClaimID`. The `build()` method finalizes the entry by constructing an `SLE` from the accumulated `STObject` and the caller-supplied `uint256` index, then wrapping it in a new `XChainOwnedClaimID`.

Field setter parameters use `std::decay_t<typename SF_XXX::type::value_type>` to strip const/reference qualifiers that the SField type machinery may carry, producing clean by-value parameter types suitable for assignment into the `STObject`.

## Relationship to Sibling Files

Within `include/xrpl/protocol_autogen/ledger_entries/`, every other ledger entry type — `Bridge.h`, `AccountRoot.h`, `Offer.h`, and so on — follows an identical structural pattern: a read-only `XxxEntry` class backed by `LedgerEntryBase` and a `XxxEntryBuilder` backed by `LedgerEntryBuilderBase<XxxEntryBuilder>`. The `XChainOwnedClaimID` and `XChainOwnedCreateAccountClaimIDBuilder` files together cover the two flavors of XChain claim ID: regular value transfers and account-creation transfers respectively. The companion `Bridge.h` entry represents the bridge configuration object that both claim ID types reference via `sfXChainBridge`.