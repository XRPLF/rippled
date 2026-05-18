# `PermissionedDomainSet.h`

## Role in the System

This auto-generated header defines the C++ interface for the `PermissionedDomainSet` transaction type (`ttPERMISSIONED_DOMAIN_SET`, ordinal 62) on the XRP Ledger. It resides in the `protocol_autogen` layer — a schema-driven abstraction that wraps the low-level `STTx`/`STObject` serialization primitives with typed, named accessors, eliminating direct field-key manipulation from application code. The file is governed by the `featurePermissionedDomains` amendment, which introduces a permissioned access-control mechanism on the ledger. Together with `PermissionedDomainDelete` (ordinal 63), this transaction forms the complete lifecycle for permissioned domain management: `PermissionedDomainSet` handles both creation of new domains and updates to existing ones, while the delete transaction removes them entirely.

Because this file is auto-generated (the header warns *do not edit*), the field cardinality and type assignments here are outputs of a schema specification, not decisions made within the file itself.

## The Two-Class Pattern

The file declares two tightly coupled classes within `xrpl::transactions`:

**`PermissionedDomainSet`** is an immutable, read-only wrapper around a `std::shared_ptr<STTx const>`. It inherits the full suite of common field accessors from `TransactionBase` — account, fee, sequence, flags, memos, signers, network ID, delegate, and more — and adds two domain-specific members. `getDomainID()` returns `protocol_autogen::Optional<SF_UINT256::type::value_type>`, deferring to the `hasDomainID()` presence check before accessing `sfDomainID`; if the field is absent, `std::nullopt` is returned. `getAcceptedCredentials()` returns a `const STArray&` directly (no optional wrapper) because `sfAcceptedCredentials` is declared `soeREQUIRED` for this transaction type.

**`PermissionedDomainSetBuilder`** is the mutable counterpart, implemented as a CRTP class inheriting `TransactionBuilderBase<PermissionedDomainSetBuilder>`. Field assignments accumulate in an `STObject`. The primary constructor requires `account` and `acceptedCredentials` as positional arguments — reflecting their required status — while `sequence` and `fee` are optional. `setDomainID()` exists as a separate optional setter, to be called when targeting an existing domain rather than creating a new one. Calling `build(publicKey, secretKey)` signs the accumulated object, wraps it in an `STTx`, and returns an immutable `PermissionedDomainSet` wrapper. This split enforces that a signed transaction cannot be further mutated.

## Create-or-Update Semantics and the Optional `sfDomainID`

The most semantically significant design point in this file — relative to its sibling `PermissionedDomainDelete` — is that `sfDomainID` is `soeOPTIONAL` here but `soeREQUIRED` there. The reason is the "set" verb: a `PermissionedDomainSet` with no `sfDomainID` creates a new permissioned domain object on the ledger, while one that includes `sfDomainID` updates the `sfAcceptedCredentials` list of an existing domain. This dual-mode behavior is encoded directly in the field optionality, making the create vs. update distinction visible at the C++ type level. A caller who receives a `PermissionedDomainSet` object and observes `getDomainID() == std::nullopt` knows unambiguously that it is a creation, not a modification.

By contrast, `PermissionedDomainDelete`'s `getDomainID()` returns `SF_UINT256::type::value_type` by value with no optional wrapping, because you cannot delete an unspecified target.

## `sfAcceptedCredentials` as a Required Array

`sfAcceptedCredentials` is an `STArray` — an untyped heterogeneous array in XRPL's serialization layer — and is required on every `PermissionedDomainSet` transaction. It defines the set of credential types that an account must hold to be permitted within the domain. Both the getter (`getAcceptedCredentials()`) and the builder setter (`setAcceptedCredentials()`) use `STArray const&` directly; there is no `protocol_autogen::Optional` wrapper and no presence check needed. The builder's constructor enforces this: `acceptedCredentials` is a required positional parameter, so no `PermissionedDomainSetBuilder` can be constructed without providing it.

## Shared Infrastructure and Design Invariants

**Type-guard at construction**: Both classes validate `TxType` at runtime via `getTxnType() != txType`, throwing `std::runtime_error` on mismatch. This cannot be a compile-time check because `STTx` carries its type as a serialized field. The builder's alternate constructor (accepting an existing `std::shared_ptr<STTx const>`) enables round-trip deserialization: a transaction received from the wire or retrieved from the ledger can be loaded into the builder for further field inspection or re-signing.

**No `soTemplate` initialization**: `TransactionBuilderBase` deliberately avoids pre-populating the internal `STObject` with `soeDEFAULT` placeholders. If those were present, the `STTx` constructor's `applyTemplate()` call would throw for any field explicitly set to its default value. Keeping `object_` as a free object sidesteps this, delegating proper default-field handling to the `STTx` constructor itself.

**`std::decay_t` in setter signatures**: `setDomainID` takes `std::decay_t<typename SF_UINT256::type::value_type> const&` rather than the raw typedef. This strips reference qualifiers from the SField's native type, guarding against dangling-reference issues if the typedef resolves to a reference type. The getter returns by value, which is already safe for a `uint256`.

**Delegation support**: The transaction is marked `Delegation::delegable`, meaning the inherited `sfDelegate` field may be populated. This allows a delegate account to create or modify a permissioned domain on behalf of the domain owner, subject to the `featurePermissionedDomains` amendment being active.

## Relationship to Sibling Files

Every file in `protocol_autogen/transactions/` follows this identical two-class pattern. The base types — `TransactionBase` and `TransactionBuilderBase<Derived>` — provide all common functionality (field accessors, signing, validation). Each auto-generated file contributes only the transaction-type constant, the type-guard, and the handful of domain-specific getters and setters. The result is a highly regular, easily diffable family of headers where the structural overhead is zero and the only variation between files is the field schema of each transaction type.