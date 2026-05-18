# `include/xrpl/protocol_autogen/ledger_entries/Amendments.h`

This file is an auto-generated type-safe wrapper for the XRPL `Amendments` ledger entry (`ltAMENDMENTS`, type `0x0066`). It belongs to the `xrpl::ledger_entries` namespace alongside roughly thirty similar wrappers — one per ledger object kind — all generated from a common schema. The file should never be edited by hand; changes belong in the generator.

## Domain context

The Amendments ledger entry is a singleton on the XRP Ledger that records the state of the amendment process. `sfAmendments` holds a vector of 256-bit hashes identifying every protocol amendment that has been fully enabled on the network. `sfMajorities` is a structured array tracking amendments that have crossed the validator supermajority threshold but have not yet been enabled — each element carries an amendment ID and the ledger timestamp at which majority support was first observed. Together these two fields power the two-week lockout that prevents feature activation until sufficient validators have upgraded. `sfPreviousTxnID` and `sfPreviousTxnLgrSeq` are the standard auditing fields that trace any ledger entry back to the `Change` pseudo-transaction that last mutated it.

## Two-class pattern: wrapper + builder

The file defines exactly two classes. `Amendments` is an **immutable read-only wrapper** that owns a `std::shared_ptr<SLE const>` (the serialized ledger entry). `AmendmentsBuilder` is the **mutable construction side** that accumulates field values and materializes them into an `SLE` only when `build()` is called. This strict separation prevents accidental mutation of a live SLE that is shared across consensus, validation, and view layers.

`Amendments` inherits from `LedgerEntryBase`, which provides `getKey()`, `getType()`, `getFlags()`, `getLedgerIndex()`, `getSle()`, and `validate()` — the common denominator for all ledger entry types. `AmendmentsBuilder` inherits from `LedgerEntryBuilderBase<AmendmentsBuilder>`, which uses CRTP so that the inherited `setLedgerIndex()` and `setFlags()` methods return `AmendmentsBuilder&` and support uninterrupted method chaining without any casting at the call site.

## Construction and type-safety guard

Both constructors that accept an existing `SLE` enforce a type match at runtime. `Amendments(std::shared_ptr<SLE const> sle)` calls `sle_->getType()` and throws `std::runtime_error` if the result is not `ltAMENDMENTS`. `AmendmentsBuilder(std::shared_ptr<SLE const> sle)` does the same via `sle->at(sfLedgerEntryType)`. This deliberate eagerness means a wrong-type mistake surfaces immediately at construction rather than silently returning garbage data from a getter, as the unit tests explicitly verify.

The default `AmendmentsBuilder()` constructor pre-populates only `sfLedgerEntryType` and `sfFlags` on a free `STObject`. It intentionally does **not** call `object_.set(soTemplate)`, which would install `soeDEFAULT` placeholder fields. Calling `applyTemplate()` later (inside the `SLE` constructor) would reject any field set to its default value with an "may not be explicitly set to default" error. Leaving the builder object as a schema-free container avoids this trap.

## Field accessors and the Optional alias

Every public field getter follows a paired pattern: a `has*()` predicate and a `get*()` that returns `std::nullopt` when the field is absent. This is consistent with the `soeOPTIONAL` nature of every field on the Amendments entry — even `sfAmendments` itself may be absent on a brand-new ledger before any amendments are enabled.

The return type of scalar getters like `getAmendments()` and `getPreviousTxnID()` uses the `protocol_autogen::Optional<T>` alias defined in `Utils.h`. That alias resolves to either `std::optional<T>` or `std::optional<std::reference_wrapper<std::remove_reference_t<T>>>` depending on whether `T` is a reference type. This ensures that large field values accessed through `SLE::at()` — which can return const references into the SLE's internal storage — do not get silently copied when wrapped in an optional.

`getMajorities()` diverges from this pattern. Because `STArray` is a variable-length structured type not covered by the `SF_*` accessor templates, it uses `sle_->getFieldArray(sfMajorities)` and returns `std::optional<std::reference_wrapper<STArray const>>` directly, allowing callers to iterate the majority records by reference without copying the array.

## Builder `build()` materialization

`AmendmentsBuilder::build(uint256 const& index)` finalizes the entry by moving the accumulated `STObject` into an `SLE` constructor along with the caller-supplied 256-bit ledger index. It then wraps the resulting `SLE` in a `shared_ptr<SLE const>` and passes it to the `Amendments` constructor. From this point forward the entry is immutable, and the builder is consumed (its `object_` has been moved out). Clients that need to re-read the entry after construction must go through the returned `Amendments` wrapper.