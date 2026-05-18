# `PermissionedDomainDelete.h`

## Role in the System

This auto-generated header defines the C++ interface for the `PermissionedDomainDelete` transaction type (`ttPERMISSIONED_DOMAIN_DELETE`, ordinal 63) on the XRP Ledger. It lives in the `protocol_autogen` layer — a code-generated abstraction that wraps the low-level `STTx`/`STObject` serialization primitives with typed, named accessors. The `PermissionedDomains` amendment introduces a permissioned access-control mechanism on the ledger; this transaction removes a domain object previously created or configured via `PermissionedDomainSet` (type 62). Together the two transactions form the full lifecycle for permissioned domain management.

Because this file is auto-generated (the header warns *do not edit*), it should be viewed as schema-driven output: the definitive source of truth for field cardinality and types lives in the amendment's specification, not in this file directly.

## The Two-Class Pattern

The file declares two closely related classes within `xrpl::transactions`:

**`PermissionedDomainDelete`** is an immutable, read-only wrapper around a `std::shared_ptr<STTx const>`. It inherits all common-field accessors from `TransactionBase` (account, fee, sequence, flags, memos, signers, network ID, delegate, etc.) and adds exactly one domain-specific getter: `getDomainID()`. This getter returns `sfDomainID` as a `uint256` value directly — not as `std::optional` — because the field is declared `soeREQUIRED` for this transaction type. There is no null path.

**`PermissionedDomainDeleteBuilder`** is the mutable counterpart, implementing CRTP via `TransactionBuilderBase<PermissionedDomainDeleteBuilder>`. It accumulates field assignments into an `STObject` and provides a fluent `setDomainID()` setter. Calling `build(publicKey, secretKey)` signs the accumulated object, wraps it in an `STTx`, and returns an immutable `PermissionedDomainDelete` wrapper. The split between wrapper and builder enforces that a transaction object, once constructed and signed, cannot be mutated — a critical correctness invariant for transaction handling.

## Design Decisions Worth Noting

**Required vs. optional `sfDomainID`**: Comparing with `PermissionedDomainSet`, where `getDomainID()` returns `protocol_autogen::Optional<...>` because a new domain is created when no ID is specified, the delete transaction requires the ID unconditionally. You cannot delete an unknown or unspecified domain. This asymmetry in optionality is intentional and reflects the semantics of each operation: set = create-or-update, delete = must-target-existing.

**`std::decay_t` in the setter signature**: `setDomainID` takes `std::decay_t<typename SF_UINT256::type::value_type> const&` rather than the raw typedef. `std::decay_t` strips reference qualifiers from the SField's native C++ type, preventing accidental reference-to-temporary issues if the SField type resolves to a reference type. The getter, by contrast, returns by value using the typedef directly, which is already a value type for `uint256`.

**Type-check at construction, not at compile time**: Both `PermissionedDomainDelete` and `PermissionedDomainDeleteBuilder` validate the `TxType` at runtime in their constructors, throwing `std::runtime_error` on mismatch. This is necessary because `STTx` is a runtime-typed container — it carries its type as a serialized field — so a compile-time check is not available. The builder's alternate constructor (taking an existing `STTx`) enables round-trip deserialization: parse a raw transaction from the wire or ledger, then wrap it in the typed builder for further manipulation.

**No `soTemplate` initialization in the builder**: `TransactionBuilderBase`'s constructor deliberately avoids calling `object_.set(soTemplate)`. Doing so would pre-populate the `STObject` with default-value placeholders for `soeDEFAULT` fields. When the builder later calls `std::make_shared<STTx>(std::move(object_))`, the `STTx` constructor invokes `applyTemplate()`, which throws if it encounters a field explicitly set to its default value. Keeping `object_` as a "free object" sidesteps this and lets `applyTemplate()` handle defaults correctly.

**Delegation support**: The transaction is marked `Delegation::delegable`, meaning the `sfDelegate` field (inherited from `TransactionBase`) may be set on it. This allows a delegate account to authorize a permissioned domain deletion on behalf of the domain owner. The field is gated behind the `featurePermissionedDomains` amendment along with the transaction type itself, so neither can appear on the ledger until the amendment is voted in.

## Relationship to Sibling Files

Within `protocol_autogen/transactions/`, each transaction type follows this exact two-class structure. The base types are not transaction-specific: `TransactionBase` provides the immutable field reader foundation, and `TransactionBuilderBase<Derived>` provides the mutable CRTP builder foundation with signing. The auto-generated transaction files contribute only the transaction-type constant, the type-guard logic, and the domain-specific fields — keeping the architecture DRY and making the auto-generation schema straightforward.