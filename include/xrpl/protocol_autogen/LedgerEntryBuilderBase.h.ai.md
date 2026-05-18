# `LedgerEntryBuilderBase.h` — CRTP Base for Auto-Generated Ledger Entry Builders

## Role in the System

`LedgerEntryBuilderBase<Derived>` is the CRTP (Curiously Recurring Template Pattern) base class shared by every auto-generated ledger entry builder in the `xrpl::ledger_entries` namespace. It lives in the `protocol_autogen` module alongside its read-only counterpart, `LedgerEntryBase`, and is instantiated by code generated from the Mako template at `scripts/templates/LedgerEntry.h.mako`. For every concrete ledger entry type — `AccountRoot`, `Offer`, `Escrow`, and so on — the code generator emits a `{Name}Builder` that extends this base.

The class solves a narrow but important problem: how to build a well-formed `STObject` that can be promoted into an `SLE` (Serialized Ledger Entry) without running into the template-enforcement machinery before all required fields are present.

## The `soeDEFAULT` Pitfall and the "Free Object" Strategy

The most critical design decision is captured in the constructor comment: `object_.set(soTemplate)` is **deliberately not called**. In the XRPL serialization layer, calling `set(soTemplate)` on an `STObject` populates placeholder `STBase` entries for every field marked `soeDEFAULT`. When the `SLE` constructor later calls `applyTemplate()` on the incoming object and finds those placeholders already in place, it throws a `"may not be explicitly set to default"` exception — even though the placeholders were inserted by the framework, not by user code.

By keeping `object_` as a "free object" (no template applied), the builder accumulates only the fields the caller explicitly sets. The `SLE` constructor — called from the derived builder's `build(index)` method as `std::make_shared<SLE>(std::move(object_), index)` — then calls `applyTemplate()` once, cleanly, on a clean object with no spurious placeholders. The `TransactionBuilderBase` class in the same module carries an identical comment and uses the same strategy, confirming this is an intentional pattern for all auto-generated builders.

## CRTP Method Chaining

The template parameter `Derived` enables compile-time fluent APIs without virtual dispatch. Every setter in the base class returns `static_cast<Derived&>(*this)`, so callers can chain calls on the concrete type:

```cpp
AccountRootBuilder(account, seq, balance, ownerCount, prevTxnID, prevTxnLgrSeq)
    .setRegularKey(key)
    .setFlags(lsfDefaultRipple)
    .build(index);
```

The cast is always safe because `Derived` is constrained by the CRTP contract — only `AccountRootBuilder` can instantiate `LedgerEntryBuilderBase<AccountRootBuilder>`. The alternative of returning `LedgerEntryBuilderBase&` would force callers to downcast after each base-class setter.

## What the Base Class Handles

The base provides exactly the fields that are universal to all ledger entries — the two fields that every `SOTemplate` shares regardless of entry type: `sfLedgerIndex` (optional, via `setLedgerIndex`) and `sfFlags` (required, pre-initialized to zero in the constructor via `setFlags`). The constructor also writes `sfLedgerEntryType` into the free object immediately, using the discriminant passed by the derived class, so the object is always self-describing from the first line.

The `protected` member `object_` is an `STObject` initialized with the `sfLedgerEntry` field descriptor. Being `protected` rather than private lets generated derived classes write `object_[sfSomeField] = value;` directly without routing through the base class interface — this is intentional for performance and code-generation simplicity.

## Validation

`validate()` performs two checks. First, it confirms `sfLedgerEntryType` is present (the `LCOV_EXCL_LINE` annotation on the failure path signals that this branch is unreachable in practice, since the constructor always sets it). Second, it resolves the entry type to an `SOTemplate` by consulting the `LedgerFormats` singleton and delegates to `protocol_autogen::validateSTObject()`, the inline function in `STObjectValidation.h`. That helper iterates the template and confirms all `soeREQUIRED` fields are present, and also enforces MPT (Multi-Purpose Token) compatibility restrictions — fields marked `soeMPTNotSupported` must not hold an `MPTIssue` variant in their amount or issue types.

## Relationship to `LedgerEntryBase`

`LedgerEntryBase` is the immutable read side of the same abstraction: it wraps a `shared_ptr<SLE const>` and exposes typed getters. `LedgerEntryBuilderBase` is the mutable write side: it accumulates state in a plain `STObject` and materializes an `SLE` only when `build(index)` is called on the derived class. The two classes mirror each other's `validate()` implementation exactly — both call `validateSTObject` against the same `LedgerFormats`-sourced template.

The separation means the type-safe read wrappers (`AccountRoot`, `Offer`, etc.) are always backed by an immutable, fully validated `SLE`, while the builders are transient construction aids that are consumed and discarded at the `build()` call site.