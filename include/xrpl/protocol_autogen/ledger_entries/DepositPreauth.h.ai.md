# `DepositPreauth.h` — Auto-generated Ledger Entry Wrapper

## Purpose and Context

This file is part of the `protocol_autogen` layer — a collection of auto-generated C++ headers that provide typed, ergonomic access to every XRPL ledger object. It defines two classes, `DepositPreauth` and `DepositPreauthBuilder`, that together wrap the raw `SLE` (Serialized Ledger Entry) for the `ltDEPOSIT_PREAUTH` (type code `0x0070`) ledger object.

On the XRPL, a `DepositPreauth` object is created when an account with Deposit Authorization enabled pre-approves a specific counterparty — or a set of credential types — to send it payments without going through the normal deposit authorization gate. The ledger entry records that approval and is keyed by the owning account and the authorized party. Importantly, the entry supports two distinct authorization mechanisms: a simple account-to-account grant via `sfAuthorize`, and a more powerful credential-set grant via `sfAuthorizeCredentials` (introduced under the Credentials amendment). Only one of these two optional fields is expected to be present in any given entry.

## Class Design: Immutable Wrapper + Fluent Builder

The file follows the same architectural pattern used throughout `protocol_autogen`: a read-only view class paired with a separate builder class that handles construction. This separation enforces at the type level that an already-stored ledger entry cannot be mutated through its wrapper — callers cannot accidentally write through a `DepositPreauth` reference they received from the ledger state.

`DepositPreauth` extends `LedgerEntryBase`, which holds a `std::shared_ptr<SLE const>` (note the `const`). All field getters on the wrapper delegate directly to the underlying `SLE` via `sle_->at(sfField)` or `sle_->isFieldPresent(sfField)`. The constructor validates the entry type immediately: if the caller wraps an `SLE` whose `getType()` doesn't return `ltDEPOSIT_PREAUTH`, a `std::runtime_error` is thrown. This fail-fast guard prevents silent type confusion when reading ledger state.

`DepositPreauthBuilder` uses the CRTP pattern via `LedgerEntryBuilderBase<DepositPreauthBuilder>`: the base class templatizes on the derived type so that common setters like `setFlags()` and `setLedgerIndex()` return `Derived&` instead of `LedgerEntryBuilderBase&`, enabling uninterrupted method chaining across both base and derived setters. The builder stores its working state in an `STObject object_{sfLedgerEntry}`, deliberately avoiding applying the SOTemplate at construction time (see the comment in `LedgerEntryBuilderBase`) to prevent the SLE constructor's `applyTemplate()` from rejecting default-valued fields.

## Field Inventory

Four fields are required and must be supplied to the primary constructor:

- **`sfAccount`** — the owning account that granted the preauthorization.
- **`sfOwnerNode`** — the page index within the owner's directory that holds this entry, used for efficient deletion.
- **`sfPreviousTxnID`** — the 256-bit hash of the transaction that last modified this entry, forming an audit chain.
- **`sfPreviousTxnLgrSeq`** — the ledger sequence number of that last-modifying transaction.

Two fields are optional and mutually exclusive in practice:

- **`sfAuthorize`** — the single `AccountID` that has been pre-approved. Wrapped with `protocol_autogen::Optional<SF_ACCOUNT::type::value_type>`, which resolves to `std::optional<AccountID>` for non-reference value types.
- **`sfAuthorizeCredentials`** — an `STArray` of credential type descriptors granting access to any account holding matching credentials. Because `STArray` is a non-copyable object internally stored by reference in the SLE, `getAuthorizeCredentials()` returns `std::optional<std::reference_wrapper<STArray const>>` rather than a value copy. This is the one field that deviates from the uniform getter signature; the comment in the source marks it as an "untyped field (unknown)", meaning the code generator could not resolve a strongly-typed accessor for it.

Companion `has*()` predicates (`hasAuthorize()`, `hasAuthorizeCredentials()`) allow presence checks without materializing the optional wrapper.

## Builder Construction Paths

The builder offers two entry points. The primary constructor takes the four required fields and immediately calls their corresponding setters, establishing a minimal valid in-progress object. Optional fields (`setAuthorize`, `setAuthorizeCredentials`) are available for chaining afterward.

The second constructor accepts an existing `std::shared_ptr<SLE const>` and copies its state into the builder's internal `STObject`, enabling a read-modify-write pattern: load an entry from the ledger, wrap it in a builder, call setters to patch specific fields, then call `build()` to produce a new `DepositPreauth` wrapper containing a fresh `SLE`. The same type-guard check (`sfLedgerEntryType != ltDEPOSIT_PREAUTH`) is enforced here, throwing `std::runtime_error` on mismatch.

The terminal `build(uint256 const& index)` method moves the internal `STObject` into a new `SLE` keyed by `index`, then wraps it in a `DepositPreauth`. This move means a builder is single-use: after `build()` the internal object is in a valid-but-unspecified state and should not be reused.

## Relationship to the Transaction Layer

The ledger entry wrapper in this file is purely a read/construct facility. The business logic that creates, validates, and destroys `DepositPreauth` objects lives in `src/libxrpl/tx/transactors/payment/DepositPreauth.cpp`, which operates directly on raw `SLE` pointers via `View`. The `sfAuthorizeCredentials` field in the ledger entry mirrors the credentials-amendment path in the transactor, where `preflight` enforces that exactly one of `sfAuthorize`, `sfUnauthorize`, `sfAuthorizeCredentials`, or `sfUnauthorizeCredentials` is present in the transaction — the dual optional structure of this wrapper reflects that protocol-level exclusivity.