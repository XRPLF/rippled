# `include/xrpl/protocol_autogen/ledger_entries/Offer.h`

## Purpose and Context

This file is part of a code-generation pipeline that produces type-safe wrappers for every ledger entry type in the XRP Ledger protocol. The `Offer` ledger entry (type code `ltOFFER`, `0x006f`) represents a single live order in the XRPL's on-ledger decentralized exchange (DEX). Whenever an account places an `OfferCreate` transaction that isn't immediately filled, a persistent `Offer` ledger object is created and indexed in the order books — this file defines the strongly-typed C++ interface used to read and construct those objects throughout the rippled codebase.

The file lives in `include/xrpl/protocol_autogen/`, a directory of ~30 similarly structured headers, one per ledger entry type. The `// This file is auto-generated. Do not edit.` header comment is authoritative — the entire directory is produced by a code generator and should not be modified directly.

## Class Architecture: Immutable Wrapper + Builder

The file defines two cooperating classes following a strict read/write separation.

**`Offer`** extends `LedgerEntryBase` and holds a `std::shared_ptr<SLE const>` — the const qualifier is crucial. The underlying `SLE` (Serialized Ledger Entry) is the live ledger object; wrapping it as `const` means this class can never mutate ledger state. All getters are `[[nodiscard]]` `const` methods, making the immutability contract explicit at the type level. The constructor validates that the incoming `SLE` really is an offer via `sle_->getType() != entryType`, throwing `std::runtime_error` on a mismatch rather than silently wrapping the wrong object type.

**`OfferBuilder`** uses CRTP, inheriting from `LedgerEntryBuilderBase<OfferBuilder>`. The CRTP ensures common setters defined on the base (`setFlags`, `setLedgerIndex`) return `OfferBuilder&` rather than the base-class type, preserving the fluent method-chaining interface without virtual dispatch. The internal state is a plain `STObject object_`, initialized with `ltOFFER` and default flags. The base class deliberately avoids calling `object_.set(soTemplate)` at construction to prevent `applyTemplate()` from creating `soeDEFAULT` placeholder fields that would later cause the SLE constructor to throw "may not be explicitly set to default".

The `build()` method terminates the chain: it moves the accumulated `STObject` into a freshly constructed `SLE` bound to the caller-supplied 256-bit ledger index, then wraps that in an `Offer` wrapper — closing the loop between builder and reader.

## Required Fields and Their XRPL Semantics

The nine required fields capture the full state of a DEX order:

- **`sfAccount`** — the `AccountID` of the offer owner.
- **`sfSequence`** — the sequence number of the `OfferCreate` transaction that created this object. Together with `sfAccount` it uniquely identifies the offer and is used to compute the ledger key.
- **`sfTakerPays` / `sfTakerGets`** — both are `SF_AMOUNT`, meaning they can represent either XRP (in drops) or IOU token amounts from any issuer. `TakerPays` is what a party consuming the offer must deliver; `TakerGets` is what they receive. The ratio defines the exchange rate.
- **`sfBookDirectory`** — a `uint256` hash pointing to the `DirectoryNode` ledger entry that indexes this offer within its order book. Order books in XRPL are implemented as sorted linked lists of `DirectoryNode` pages; this field links the individual offer into that structure.
- **`sfBookNode`** — a `uint64` node index within the `DirectoryNode` page chain, used for O(1) deletion without a full scan.
- **`sfOwnerNode`** — a `uint64` index into the offer owner's account directory (another `DirectoryNode`). When an offer is consumed or cancelled, the ledger uses this to efficiently remove the entry from the owner's object list.
- **`sfPreviousTxnID` / `sfPreviousTxnLgrSeq`** — standard provenance fields present on all mutable ledger objects, recording the last transaction that touched this entry.

## Optional Fields and the `Optional<T>` Type Alias

Three optional fields are surfaced with paired `has*()`/`get*()` methods:

- **`sfExpiration`** (`SF_UINT32`) — a Ripple Epoch timestamp after which the offer is treated as expired. If absent the offer has no time limit.
- **`sfDomainID`** (`SF_UINT256`) — associates the offer with a permissioned domain, part of newer DEX access-control features.
- **`sfAdditionalBooks`** (`STArray`) — an array field enabling multi-leg or alternative order book associations, another recent protocol extension.

Optional scalar fields return `protocol_autogen::Optional<T>`, a type alias defined in `Utils.h` using `std::conditional_t`. If `T` is a reference type the alias resolves to `std::optional<std::reference_wrapper<T>>`, allowing reference semantics inside an optional; if `T` is a value type it resolves to `std::optional<T>`. This is necessary because some `SField` accessors return const references into the SLE's internal storage. `sfAdditionalBooks` bypasses this alias entirely and returns `std::optional<std::reference_wrapper<STArray const>>` directly, since `STArray` is always accessed by reference.

## Round-Trip Mutation via the SLE Copy Constructor

`OfferBuilder` offers a second constructor that takes `std::shared_ptr<SLE const>` and initializes `object_` by dereferencing the SLE: `object_ = *sle`. This enables a round-trip pattern: read an existing offer from the ledger as an immutable `Offer`, copy it into an `OfferBuilder`, modify optional fields (e.g., update remaining `TakerPays`/`TakerGets` after a partial fill), then call `build()` to produce a new `SLE` that can be written back. The type guard (`sle->at(sfLedgerEntryType) != ltOFFER`) applies here too, maintaining the invariant that `OfferBuilder` only ever holds offer-shaped data.

## Relationship to Sibling Files

Every file in the `ledger_entries/` directory follows this identical dual-class pattern. `DirectoryNode.h`, `AccountRoot.h`, `RippleState.h`, and the rest are structurally identical — the fields differ but the wrapper/builder split, CRTP inheritance, and `Optional<T>` usage are uniform across all of them. This consistency is a direct consequence of the code-generation approach: correctness is enforced by generating from a single template rather than by convention across hand-written files.

One minor discrepancy in the generated documentation: the `OfferBuilder` class comment states it "Uses Json::Value internally for flexible ledger entry construction," but the implementation uses `STObject` directly. This appears to be a stale comment artifact in the autogen template.