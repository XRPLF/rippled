# `AccountRoot.h` — Auto-Generated Type-Safe Wrapper for the AccountRoot Ledger Entry

This file is part of the `xrpl/protocol_autogen` layer and provides the C++ interface for the XRPL `AccountRoot` ledger entry (`ltACCOUNT_ROOT`, type code `0x0061`). It is auto-generated — edits are not preserved — and lives alongside parallel files for every other ledger entry type (Offer, RippleState, AMM, Vault, etc.) in the `ledger_entries/` subdirectory. The file defines two classes that together implement a clean separation between reading and constructing ledger state: an immutable wrapper `AccountRoot` and a fluent builder `AccountRootBuilder`.

## Role in the System

The `AccountRoot` ledger entry is the most fundamental object in the XRPL: every account has exactly one, and it is the authoritative record of that account's XRP balance, transaction sequence number, owned-object count, signing configuration, and a growing set of protocol-level IDs linking to AMM pools, vaults, loan brokers, and NFT tracking state. Before this auto-generated layer existed, code would retrieve fields from raw `SLE` objects using untyped `at(sfSomeField)` calls scattered throughout the codebase. This wrapper layer enforces at the type system level that you cannot call `getBalance()` on an entry that is not an `AccountRoot`.

## `AccountRoot` — Immutable Read Wrapper

`AccountRoot` inherits from `LedgerEntryBase`, which holds the single protected member `std::shared_ptr<SLE const> sle_` and exposes common cross-entry getters: `getType()`, `getKey()`, `getFlags()`, `getLedgerIndex()`, and `getSle()`. It also owns the `validate()` method, which dispatches to `validateSTObject` against the format template registered in `LedgerFormats`.

The `AccountRoot` constructor takes a `std::shared_ptr<SLE const>` and immediately checks `sle_->getType() != entryType`, throwing `std::runtime_error` if there is a mismatch. This eager validation is important because callers often extract SLEs from a general-purpose ledger lookup that returns any entry type; the constructor acts as a runtime assertion that the caller selected the right entry. Note the subtle `SLE const` — the const qualifier is intentional and propagates through the entire read interface, making accidental mutation impossible.

Field getters divide into two categories based on field optionality in the protocol schema:

**Required fields** (`soeREQUIRED`) — `getAccount()`, `getSequence()`, `getBalance()`, `getOwnerCount()`, `getPreviousTxnID()`, `getPreviousTxnLgrSeq()` — return their value types directly, with no `Optional` wrapping. The protocol guarantees these fields are always present in a well-formed SLE, so no presence check is needed.

**Optional and default-valued fields** return `protocol_autogen::Optional<T>`. The `Optional<T>` alias, defined in `Utils.h`, resolves to `std::optional<T>` for value types and `std::optional<std::reference_wrapper<T>>` for reference types — a detail that lets the abstraction work correctly whether the underlying field type is a value or a reference. Each such getter delegates to a paired `has*()` method that calls `sle_->isFieldPresent(sfXxx)`, only extracting the field if present. This pattern applies to `sfAccountTxnID`, `sfRegularKey`, `sfEmailHash`, `sfWalletLocator`, `sfWalletSize`, `sfMessageKey`, `sfTransferRate`, `sfDomain`, `sfTickSize`, `sfTicketCount`, `sfNFTokenMinter`, `sfFirstNFTokenSequence`, `sfAMMID`, `sfVaultID`, and `sfLoanBrokerID`.

Two fields — `sfMintedNFTokens` and `sfBurnedNFTokens` — are annotated `soeDEFAULT` in the schema, meaning they have a protocol-defined default (zero) that the serializer may omit when the field is at that default. Despite conceptually always being present for NFT-capable accounts, the getters still return `Optional` and guard via `isFieldPresent()`, which is the correct behavior: a legacy `AccountRoot` that has never interacted with NFTs may not serialize these fields at all.

The richness of the optional field set reflects the evolution of the XRPL protocol. Fields like `sfEmailHash`, `sfWalletLocator`, and `sfWalletSize` are legacy from early XRPL design and rarely populated today. More recent additions — `sfAMMID`, `sfVaultID`, `sfLoanBrokerID` — link the account to specific DeFi protocol objects, allowing a single account to serve as the on-ledger identity for an AMM pool or lending vault without requiring a separate lookup strategy.

## `AccountRootBuilder` — Fluent Construction

`AccountRootBuilder` inherits from `LedgerEntryBuilderBase<AccountRootBuilder>`, which uses the Curiously Recurring Template Pattern (CRTP). The base class exposes `setFlags()` and `setLedgerIndex()` and returns `Derived&` (i.e., `AccountRootBuilder&`), so method chaining on common fields still returns the concrete derived type. The internal state is an `STObject object_{sfLedgerEntry}`.

A critical design choice in `LedgerEntryBuilderBase` is that it does *not* call `object_.set(soTemplate)` during construction. Setting the template would pre-populate `soeDEFAULT` fields with STBase placeholder objects. When the builder later constructs an `SLE` — which calls `applyTemplate()` internally — those placeholders would trigger an exception: "may not be explicitly set to default." By keeping `object_` as a free, template-less `STObject`, only fields that were explicitly assigned appear in it, and `applyTemplate()` can safely insert defaults for unset fields.

The primary constructor accepts all six required fields as parameters, initialises the base with `ltACCOUNT_ROOT` (setting `sfLedgerEntryType` and zeroing `sfFlags`), and immediately calls the corresponding setters. A secondary constructor accepts an existing `std::shared_ptr<SLE const>` and copies the SLE's field data into `object_` via `object_ = *sle`, enabling round-trip editing workflows where an entry is read from the ledger, modified through the builder API, and then rebuilt.

All optional field setters follow the identical signature pattern: accept a `std::decay_t<typename SF_Xxx::type::value_type> const&` and write it into `object_[sfXxx]`, then return `*this`. Using `std::decay_t` strips any reference or cv-qualification from the field's native value type, ensuring the parameter type is always a plain value regardless of how the field type is defined in the protocol type system.

The terminal `build(uint256 const& index)` method constructs a final `AccountRoot` by moving `object_` into an `SLE` keyed at `index`, wrapping it in a `shared_ptr<SLE const>`, and passing it to the `AccountRoot` constructor. After `build()`, the builder's `object_` has been moved-from and should not be reused.

## Relationship to the Broader Auto-Gen Layer

This file follows the exact same structural template as the other ~25 ledger entry files in the directory (`AMM.h`, `Offer.h`, `RippleState.h`, etc.) and is regenerated whenever the protocol schema changes. The separation of concerns is clean: `LedgerEntryBase` and `LedgerEntryBuilderBase` carry all reusable infrastructure, while each generated file provides only field-specific accessors and the mandatory-field constructor. Adding a new protocol field to `AccountRoot` means regenerating this file; no manual changes to the base classes are required.