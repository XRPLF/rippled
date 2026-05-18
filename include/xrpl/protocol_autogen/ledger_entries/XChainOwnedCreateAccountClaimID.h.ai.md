# `XChainOwnedCreateAccountClaimID.h`

## Role in the System

This auto-generated header defines the `ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID` ledger entry type (numeric type `0x0074`) — the on-ledger object that tracks the in-progress cross-chain account-creation claim process in XRPL's cross-chain bridge feature. It lives inside `include/xrpl/protocol_autogen/ledger_entries/`, a directory of code-generated wrappers, one per ledger entry type.

The file exists because creating an account on a destination chain via a cross-chain bridge is a multi-step process that requires attestations from a quorum of bridge witnesses before the destination account is actually funded. This ledger entry acts as the accumulation point for those attestations, identified by the owner account and a monotonic sequence counter (`sfXChainAccountCreateCount`). It is the create-account analogue of `XChainOwnedClaimID` (type `0x0071`), which handles ordinary cross-chain value transfers.

## The Immutable Wrapper / Builder Split

The file defines two classes: `XChainOwnedCreateAccountClaimID` (the read-only wrapper) and `XChainOwnedCreateAccountClaimIDBuilder` (the construction vehicle). This split is a deliberate design choice seen across all ledger entry types in this autogen layer.

`XChainOwnedCreateAccountClaimID` inherits from `LedgerEntryBase`, which wraps a `std::shared_ptr<SLE const>` — the `const` qualifier is load-bearing. Any caller who holds a wrapper object is guaranteed to observe a frozen snapshot of the ledger entry; the underlying `SLE` cannot be mutated through it. Type safety is enforced eagerly in the constructor: if the passed `SLE` does not carry `ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID` as its entry type, a `std::runtime_error` is thrown immediately rather than allowing a later field access to silently return garbage.

`XChainOwnedCreateAccountClaimIDBuilder` inherits from `LedgerEntryBuilderBase<XChainOwnedCreateAccountClaimIDBuilder>`, a CRTP base that owns a mutable `STObject object_` field and provides common-field setters like `setFlags()` and `setLedgerIndex()`. The CRTP pattern makes those inherited setters return a reference to the concrete `XChainOwnedCreateAccountClaimIDBuilder&`, enabling uninterrupted method-chaining through mixed base/derived calls. The constructor of the base deliberately avoids calling `object_.set(soTemplate)`, because doing so would pre-populate `soeDEFAULT` placeholder fields which would in turn cause the `SLE` constructor's `applyTemplate()` call to reject them as "may not be explicitly set to default." All required fields are instead set explicitly during construction.

The builder's `build(uint256 const& index)` method materializes the `STObject` into a proper `SLE` by move-constructing it with the given ledger key, then immediately wrapping it in the read-only `XChainOwnedCreateAccountClaimID` class. After `build()` the builder's internal state has been moved from, so it should not be reused.

A second builder constructor accepts an existing `std::shared_ptr<SLE const>` to allow copying/modifying an already-existing ledger entry. The builder assigns the `SLE`'s serialized content into `object_` with `object_ = *sle`, providing a mutable copy to edit before re-building. This path also validates the entry type, throwing on mismatch.

## Fields

All seven fields are marked `soeREQUIRED`, meaning they must be present for the ledger entry to be valid:

- `sfAccount` — the account on the destination chain that submitted the `XChainCreateAccountClaimID` transaction; this entry is tracked in that account's owner directory.
- `sfXChainBridge` — identifies the specific bridge (locking and issuing chain door accounts plus asset pair), scoping this claim to a particular cross-chain pathway.
- `sfXChainAccountCreateCount` — a `uint64` sequence counter that monotonically increments with each cross-chain account-creation submitted on this bridge. It functions as the unique identifier distinguishing one create-account claim accumulation object from another.
- `sfXChainCreateAccountAttestations` — an `STArray` of witness attestations collected so far for this create-account operation. Each element carries a witness public key, signature, and the amount being moved. Once sufficient attestations accumulate (meeting the quorum threshold), the destination account is funded and this ledger object is deleted.
- `sfOwnerNode` — a back-link into the owner's directory node list, required for O(1) deletion of the entry from the owner directory when the claim is fulfilled or cancelled.
- `sfPreviousTxnID` and `sfPreviousTxnLgrSeq` — standard audit fields tracking the last transaction that modified this ledger entry.

The `sfXChainCreateAccountAttestations` getter is notable: because the field type is an `STArray` (a compound/array SField), it cannot be fetched via the generic `sle_->at()` template; instead it calls `sle_->getFieldArray()` directly and returns a `const&` rather than a value copy, avoiding an unnecessary deep copy of a potentially large array.

## Validation and Error Handling

Both the wrapper and the builder expose a `validate()` method (inherited from their respective base classes). In both cases, validation delegates to `protocol_autogen::validateSTObject()`, which checks the `STObject`'s fields against the canonical `SOTemplate` registered for `ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID` in `LedgerFormats`. This means the validation logic is data-driven and not duplicated here. The constructor-level type checks (`getType() != entryType`) are a separate, earlier guard that catches misuse before any field access is attempted, and they are tested explicitly in the generated test suite (`WrapperThrowsOnWrongEntryType`, `BuilderThrowsOnWrongEntryType`).

## Relationship to Other Files

This file is one of roughly 30 files in the `ledger_entries/` autogen directory, all following the same structural template. The cross-chain sibling `XChainOwnedClaimID.h` handles ordinary cross-chain value transfers, while this file specializes in the account-creation flow which requires an additional field (`sfXChainAccountCreateCount`) to sequence the operations. The `AccountObjects` RPC handler and the ledger indexing layer in `Indexes.cpp` reference `ltXCHAIN_OWNED_CREATE_ACCOUNT_CLAIM_ID` to enumerate and key these entries in the ledger state tree.