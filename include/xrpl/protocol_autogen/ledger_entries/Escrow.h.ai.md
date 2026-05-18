# `Escrow.h` — Auto-Generated Ledger Entry Wrapper for XRPL Escrow

## Role in the System

This file is part of the `protocol_autogen` subsystem under `xrpl::ledger_entries`, which provides statically-typed C++ wrappers for every ledger entry type stored in the XRP Ledger. `Escrow.h` specifically encapsulates the `ltESCROW` (type code `0x0075`) ledger object — the on-chain record created when an XRP amount is held in a cryptographically or time-conditioned escrow. The file is auto-generated and must not be hand-edited; its source of truth is a schema definition that drives code generation for all ledger entry types in this directory.

The file defines two classes: `Escrow`, an immutable read-only view, and `EscrowBuilder`, a fluent construction interface. This two-class pattern appears across every sibling file in the directory (e.g., `Check.h`, `PayChannel.h`, `Offer.h`) and is mandated by the base classes in `LedgerEntryBase.h` and `LedgerEntryBuilderBase.h`.

## `Escrow` — The Immutable Wrapper

`Escrow` inherits from `LedgerEntryBase`, which holds a `std::shared_ptr<SLE const>` — a shared, const-qualified pointer to a Serialized Ledger Entry. The `const` qualifier is load-bearing: it ensures that once an `Escrow` object is constructed from a live ledger state, the underlying serialized object cannot be mutated through this interface. The base class exposes common fields (`sfFlags`, `sfLedgerEntryType`, `sfLedgerIndex`, `getKey()`) and a `validate()` method that checks the SLE against the registered `SOTemplate` for `ltESCROW`.

The constructor performs a runtime type guard: it verifies `sle_->getType() == ltESCROW` and throws `std::runtime_error` on mismatch. This is the only safety mechanism — there is no compile-time enforcement that an arbitrary `shared_ptr<SLE const>` contains the right entry type — so the check is essential to prevent silent field misreads when the wrong SLE is wrapped.

### Field Access Pattern

Fields divide into two categories based on their serialized optionality:

**Required fields** (`soeREQUIRED`) — `sfAccount`, `sfDestination`, `sfAmount`, `sfOwnerNode`, `sfPreviousTxnID`, `sfPreviousTxnLgrSeq` — are accessed via a single `getX()` method that reads directly from the SLE with `sle_->at(sfField)`. These are guaranteed present by the ledger's own schema enforcement, so no null-check is needed.

**Optional fields** (`soeOPTIONAL`) — `sfSequence`, `sfCondition`, `sfCancelAfter`, `sfFinishAfter`, `sfSourceTag`, `sfDestinationTag`, `sfDestinationNode`, `sfTransferRate`, `sfIssuerNode` — are surfaced as a pair: `hasX()` checks `sle_->isFieldPresent()`, and `getX()` returns `protocol_autogen::Optional<T>`. That alias, defined in `Utils.h`, resolves to `std::optional<std::reference_wrapper<T>>` when `T` is a reference type, or plain `std::optional<T>` when it is a value type. This distinction matters for large or non-copyable field types where a reference wrapper avoids an unnecessary copy.

### Escrow-Specific Field Semantics

- **`sfCondition`** (`SF_VL`, variable-length blob): The binary PREIMAGE-SHA-256 crypto-condition defined in RFC 3230. When present, the escrow can only be finished by a transaction that supplies the matching fulfillment (`sfFulfillment`). When absent, the escrow is purely time-gated.
- **`sfCancelAfter` / `sfFinishAfter`**: Both are `uint32` values representing ripple epoch timestamps (seconds since January 1, 2000). The escrow becomes cancellable after `sfCancelAfter` passes, and becomes executable (finishable) after `sfFinishAfter` passes. Either or both may be present; a valid escrow must have at least one of these or a condition.
- **`sfSequence`**: The transaction sequence number of the originating `EscrowCreate` transaction. Together with `sfAccount`, it is used to compute the escrow's canonical ledger key, so it serves a structural identity role rather than just bookkeeping.
- **`sfOwnerNode` / `sfDestinationNode`**: 64-bit directory page indices. These are internal skip-list pointers used to efficiently locate the escrow's entries in the owner directory (from the creator's perspective) and the destination directory. `sfDestinationNode` is optional because early escrow entries pre-dating the destination directory feature do not have it.
- **`sfTransferRate` / `sfIssuerNode`**: These fields appear in the schema for escrowed IOU amounts where an issuer's transfer rate must be applied at settlement. For pure XRP escrows they will be absent.

## `EscrowBuilder` — The Fluent Construction Interface

`EscrowBuilder` inherits from `LedgerEntryBuilderBase<EscrowBuilder>`, a CRTP template whose `Derived` parameter ensures that the common setters (`setFlags()`, `setLedgerIndex()`) return `EscrowBuilder&` rather than a `LedgerEntryBuilderBase&`, preserving method-chain fluency without any virtual dispatch overhead.

The base class initializes an `STObject object_{sfLedgerEntry}` — deliberately *not* bound to a schema template. This is a subtle but important design decision explained in the base class comments: calling `object_.set(soTemplate)` would insert `STBase` placeholder instances for `soeDEFAULT` fields, which causes the `SLE` constructor's `applyTemplate()` to throw "may not be explicitly set to default". By keeping the object "free" and only populating fields that are actually set, the builder lets `applyTemplate()` handle defaults correctly when `build()` finally constructs the `SLE`.

The primary constructor accepts all six required fields and immediately delegates to the corresponding setters, preventing callers from forgetting mandatory state. The secondary constructor accepts an existing `std::shared_ptr<SLE const>` and copies its content into `object_`, enabling a read-modify-write workflow where an existing escrow entry is cloned into a builder, optional fields are adjusted, and a new `SLE` is produced.

`build(uint256 const& index)` is the terminal operation. It constructs a `std::shared_ptr<SLE>` by moving `object_` into the `SLE` constructor along with the caller-supplied ledger index, then wraps the result in an `Escrow`. The index is intentionally not accumulated inside the builder — it must be computed externally (typically as a function of `sfAccount` and `sfSequence`) and supplied at finalization, keeping the builder independent of key-derivation logic.

## Relationship to the Wider `protocol_autogen` Layer

Every file in `include/xrpl/protocol_autogen/ledger_entries/` follows this exact two-class pattern, generated from a common schema. The `LedgerEntryBase` and `LedgerEntryBuilderBase` base classes encapsulate all behavior that is truly shared — immutability, SLE ownership, flag access, validation against the live `LedgerFormats` registry — while each generated file contributes only the field-specific getters and setters. The result is a consistent, auditable API surface across all ~30 ledger entry types, with type safety enforced at the boundary where an untyped `SLE` is first wrapped into a concrete subclass.