# `PermissionedDomain.h` — Auto-Generated Ledger Entry Wrapper

This file is part of the `protocol_autogen/ledger_entries/` family — a collection of auto-generated, type-safe C++ wrappers for every ledger entry type recognized by the XRPL. It represents the `PermissionedDomain` ledger entry (type code `ltPERMISSIONED_DOMAIN`, `0x0082`), a feature that lets account owners define an access-controlled domain by specifying a set of accepted credential types. Only accounts presenting a credential from that set may participate in features gated behind the domain — such as permissioned AMMs or lending pools. The `// This file is auto-generated. Do not edit.` header comment means all changes belong in the upstream code-generation templates, not in this file directly.

## `PermissionedDomain` — Immutable Read Wrapper

`PermissionedDomain` inherits from `LedgerEntryBase` and holds a `std::shared_ptr<SLE const>` — a shared, immutable reference to a live ledger entry in node state. The `const` on the pointee is load-bearing: it prevents any code path from mutating a ledger entry through this wrapper, enforcing the read-only contract at the type system level.

The constructor performs a single upfront type check:

```cpp
if (sle_->getType() != entryType)
    throw std::runtime_error("Invalid ledger entry type for PermissionedDomain");
```

This fail-fast validation moves the invariant assertion to the wrapping point rather than scattering it across every call site. Because all six fields are declared `soeREQUIRED` in the ledger format definition, once the type check passes, all getters can call `sle_->at(...)` unconditionally without returning `std::optional` — if the `SLE` was accepted by consensus it is structurally complete.

`getAcceptedCredentials()` is the one getter that returns `STArray const&` rather than a value type. `STArray` is a variable-length array of `STObject` elements — each element represents a credential issuer/credential-type pair — and returning it by reference avoids a potentially expensive copy. The `const` reference preserves the immutability contract while keeping access zero-cost.

The common fields inherited from `LedgerEntryBase` — `getType()`, `getKey()`, `getFlags()`, `getLedgerEntryType()` — are shared across all ledger entry wrappers in the directory and are defined once in the base class.

## `PermissionedDomainBuilder` — Fluent Construction Interface

`PermissionedDomainBuilder` uses the CRTP pattern via `LedgerEntryBuilderBase<PermissionedDomainBuilder>`. The base class holds a mutable `STObject object_{sfLedgerEntry}` and provides common setters (`setFlags`, `setLedgerIndex`) that return a `Derived&`, enabling uniform method chaining across all builder types. The entry-specific setters defined here extend that chain with `setOwner`, `setSequence`, `setAcceptedCredentials`, `setOwnerNode`, `setPreviousTxnID`, and `setPreviousTxnLgrSeq`.

The primary constructor requires all six required fields up front. This is stricter than a default-then-mutate approach, and it mirrors the XRPL protocol's own requirement: a `PermissionedDomain` SLE that is missing any required field would fail consensus validation anyway, so the builder makes a partial object impossible to construct rather than letting invalid state accumulate silently.

The base class constructor deliberately avoids calling `object_.set(soTemplate)` before populating fields. That matters because setting a template would create `STBase` placeholder objects for `soeDEFAULT` fields, which causes `applyTemplate()` to throw "may not be explicitly set to default" during the `SLE` constructor. Keeping `object_` as a free (template-less) object and letting the `SLE` constructor handle `applyTemplate()` itself is the correct sequencing.

A second constructor accepts `std::shared_ptr<SLE const>`, enabling a copy-and-modify workflow: deserialize an existing `PermissionedDomain` from the ledger, wrap it in a builder, update individual fields (such as replacing `sfAcceptedCredentials` after a `PermissionedDomainSet` transaction), and materialize a new SLE. The same type guard is applied here to maintain consistency with the read wrapper.

Field setters use `std::decay_t<typename SF_ACCOUNT::type::value_type> const&` — stripping cv-qualifiers and reference from the field's canonical C++ type before accepting it as a `const&`. This pattern appears uniformly across all builders in the directory and prevents accidental reference-lifetime issues when callers pass temporaries.

`setAcceptedCredentials` calls `object_.setFieldArray(sfAcceptedCredentials, value)` rather than `object_[sfAcceptedCredentials] = value`, consistent with how `STArray` fields are handled throughout the XRPL codebase — the array accessor has a distinct method rather than going through the subscript operator.

`build(uint256 const& index)` finalizes construction by moving `object_` into a new `SLE` keyed by the given ledger index, then wrapping that `SLE` in a `PermissionedDomain`. The `std::move` ensures no unnecessary copy of the accumulated field data.

## Relationship to Sibling Files and Broader System

Every file in `protocol_autogen/ledger_entries/` — `AccountRoot.h`, `Credential.h`, `Offer.h`, and the rest — follows an identical structural pattern: an immutable wrapper that validates on construction and a CRTP fluent builder. The uniformity is a direct consequence of code generation: the template knows the field list and optionality for each entry type and emits the same skeleton.

`PermissionedDomain` has a meaningful semantic relationship to `Credential.h`: the `sfAcceptedCredentials` array references credential types that must match `Credential` ledger entries owned by other accounts. This cross-entry referential integrity is not enforced at the wrapper layer; it is validated during transaction processing by the `PermissionedDomainSet` transactor, which calls `credentials::checkArray()` in preflight and verifies issuer account existence in preclaim. The wrapper layer is intentionally kept thin — it only guarantees type-correct field access, not protocol-level business rules.