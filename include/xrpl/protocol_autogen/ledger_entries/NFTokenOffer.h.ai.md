# `NFTokenOffer.h` — Auto-Generated NFT Offer Ledger Entry Wrapper

## Role in the System

This file defines the type-safe C++ interface for the `NFTokenOffer` ledger entry (`ltNFTOKEN_OFFER`, type code `0x0037`), one of the two NFT-related ledger entry types in the XRP Ledger (the other being `NFTokenPage`). An `NFTokenOffer` object is created on-ledger whenever an account submits an `NFTokenCreateOffer` transaction — either to sell an NFT they own or to bid on one owned by another account. The entry persists until the offer is accepted, cancelled, or expires.

The file is auto-generated (the header comment makes this explicit) and lives in the `protocol_autogen/ledger_entries/` directory alongside similarly generated wrappers for every other ledger entry type (`Offer`, `Escrow`, `Check`, `PayChannel`, etc.). The code generation pipeline ensures that schema changes propagate consistently to all consumers without manual maintenance.

## Two-Class Design: Immutable Wrapper + Fluent Builder

The file exports two cooperating classes within the `xrpl::ledger_entries` namespace:

**`NFTokenOffer`** is an immutable read wrapper around a `std::shared_ptr<SLE const>` — the serialized ledger entry. It inherits from `LedgerEntryBase`, which holds the `sle_` pointer and provides common accessors (`getKey()`, `getType()`, `getFlags()`, `validate()`). `NFTokenOffer` adds NFT-specific getters. The choice of `shared_ptr<SLE const>` is deliberate: the ledger state is immutable after being committed, and shared ownership allows many readers to reference the same on-ledger object without copying.

**`NFTokenOfferBuilder`** is a CRTP-based fluent builder inheriting from `LedgerEntryBuilderBase<NFTokenOfferBuilder>`. It accumulates field assignments into an `STObject object_` and then materializes a fully constructed `NFTokenOffer` via `build(uint256 const& index)`. The builder also accepts an existing `SLE const` for copy-based modification. The CRTP base returns `Derived&` from its shared setters (`setFlags()`, `setLedgerIndex()`), enabling unbroken method chains across both base and derived setters.

A key implementation note in `LedgerEntryBuilderBase`: the constructor intentionally does **not** call `object_.set(soTemplate)`. The comment explains why — calling it would create `STBase` placeholder values for `soeDEFAULT` fields, causing the `SLE` constructor's internal `applyTemplate()` to throw `"may not be explicitly set to default"`. By leaving the `STObject` as a free (untemplatized) object, missing optional fields are handled cleanly during SLE construction.

## Field Structure

Required fields — all enforced at construction time in the builder — are:

- `sfOwner` (`SF_ACCOUNT`): the account that created the offer and is responsible for the reserve.
- `sfNFTokenID` (`SF_UINT256`): the 256-bit identifier of the specific NFT being offered.
- `sfAmount` (`SF_AMOUNT`): the price, expressed as XRP drops or an IOU amount.
- `sfOwnerNode` (`SF_UINT64`): a back-pointer into the owner's account directory page, enabling efficient deletion of the entry when the offer is cancelled or accepted without a full directory scan.
- `sfNFTokenOfferNode` (`SF_UINT64`): a back-pointer into the NFToken's dedicated buy/sell offer directory — a linked list structure tracking all active offers for a given NFT.
- `sfPreviousTxnID` / `sfPreviousTxnLgrSeq`: standard provenance fields recording the last transaction that touched this entry, required for all ledger objects.

Optional fields are:

- `sfDestination` (`SF_ACCOUNT`): when present, restricts acceptance of the offer to a single named account. This supports private/targeted sales. The getter returns `protocol_autogen::Optional<SF_ACCOUNT::type::value_type>`, paired with a `hasDestination()` predicate.
- `sfExpiration` (`SF_UINT32`): a Ripple epoch timestamp after which the offer is no longer valid. Like `sfDestination`, the getter returns `protocol_autogen::Optional<...>` and is accompanied by `hasExpiration()`.

The `protocol_autogen::Optional<T>` type alias (defined in `Utils.h`) is a small but important detail: it resolves to `std::optional<std::reference_wrapper<T>>` for reference types and `std::optional<T>` for value types, preventing accidental dangling references when returning values from optional `SLE` fields.

## Type Safety and Failure Modes

The `NFTokenOffer` constructor validates the wrapped SLE's type immediately, reading `sle_->getType()` against the `constexpr entryType = ltNFTOKEN_OFFER`. A mismatch throws `std::runtime_error`. Similarly, `NFTokenOfferBuilder`'s SLE-copy constructor checks `sle->at(sfLedgerEntryType) != ltNFTOKEN_OFFER` before accepting the entry. This eager validation means incorrect entry types are caught at construction rather than silently returning garbage field values — an important invariant for code that processes heterogeneous ledger state.

All getters are marked `[[nodiscard]]` and `const`, reinforcing the immutability contract: nothing in `NFTokenOffer` can mutate the underlying `SLE`. Callers needing to produce a modified entry must go through `NFTokenOfferBuilder`, which constructs a fresh `SLE` on `build()`.

## Relationship to Sibling Files

`NFTokenOffer.h` follows the same structural template as every other entry in `protocol_autogen/ledger_entries/` — the pattern is uniform enough that the entire directory is machine-generated from a ledger schema. `NFTokenPage.h`, the companion file, holds the actual NFT tokens and their metadata; `NFTokenOffer.h` holds the market-side activity. The two work together in the NFT subsystem: `NFTokenPage` records ownership, while `NFTokenOffer` records intent to trade.