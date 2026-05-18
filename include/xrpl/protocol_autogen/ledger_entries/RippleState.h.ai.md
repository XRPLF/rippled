# `RippleState.h` — Trust Line Ledger Entry Wrapper

`RippleState` is the on-ledger representation of a trust line between two XRPL accounts. Every time two parties agree to hold an IOU balance in a shared currency, the ledger stores exactly one `RippleState` object keyed by the canonical hash of the two account IDs and the currency code. This file is auto-generated and lives inside the `xrpl::ledger_entries` namespace alongside every other ledger entry type in the `protocol_autogen` layer.

## Role in the System

Raw ledger state in rippled is stored as `SLE` (Serialized Ledger Entry) objects — essentially generic key-value bags whose fields are identified by `SField` descriptors at runtime. Working with bare `SLE` objects everywhere would mean scattering `sle_->at(sfLowLimit)` calls across the codebase with no compile-time assurance that the right entry type is in hand. The `protocol_autogen` layer solves this by generating a thin, immutable wrapper for each entry type. `RippleState` is that wrapper for `ltRIPPLE_STATE` (wire type `0x0072`).

## Two-Class Design: Wrapper and Builder

The file defines two cooperating classes that enforce a clean separation between construction and read-only access.

`RippleState` extends `LedgerEntryBase` and holds a `std::shared_ptr<SLE const>` (the `const` is significant — there is no mutation path through this class). The constructor takes an already-live `SLE`, immediately checks `sle_->getType() != entryType`, and throws `std::runtime_error` on mismatch. This makes it impossible to accidentally wrap the wrong entry type and then call `getBalance()` on, say, an `Offer`. The `static constexpr` member `entryType` doubles as a compile-time marker that other template machinery can interrogate without instantiating the class.

`RippleStateBuilder` extends `LedgerEntryBuilderBase<RippleStateBuilder>` — a CRTP base that holds an `STObject object_{sfLedgerEntry}` and provides `setFlags()`/`setLedgerIndex()`. The CRTP trick means every setter in the base returns `Derived&` (i.e., `RippleStateBuilder&`) so method chains don't lose the concrete type. The builder's five-argument constructor accepts all required fields and immediately writes them into `object_`; the remaining optional fields are set via individual `set*` calls. A second constructor accepts an existing `SLE const` directly, performing the same type guard before copying the `STObject`, which enables in-place modification workflows.

`build(uint256 const& index)` is the sole exit point from the builder. It moves the accumulated `STObject` into a freshly constructed `SLE` (which calls `applyTemplate()` internally, filling in any missing defaulted fields), then wraps the resulting `shared_ptr<SLE>` in a `RippleState` value. The builder intentionally does *not* call `object_.set(soTemplate)` during initialization — a deliberate design choice noted in `LedgerEntryBuilderBase` to avoid creating `STBase` placeholders for `soeDEFAULT` fields, which would cause `applyTemplate()` to throw "may not be explicitly set to default."

## Asymmetric Low/High Field Layout

Every field on a trust line is duplicated: `sfLowLimit`/`sfHighLimit`, `sfLowNode`/`sfHighNode`, `sfLowQualityIn`/`sfLowQualityOut`/`sfHighQualityIn`/`sfHighQualityOut`. This reflects XRPL's design for trust lines as bidirectional relationships. The "low" party is the account whose 160-bit account ID is numerically smaller; the "high" party is the other. The balance in `sfBalance` is always stored from the low party's perspective — a positive balance means the low account is owed that amount, negative means it owes.

The two `Node` fields (`sfLowNode` and `sfHighNode`) are page indices into the respective `DirectoryNode` entries that link this trust line into each account's list of trust lines. They are optional because very old entries on the ledger may predate the directory system. The four quality fields are also optional; when absent the effective quality is `1.0` (unity), meaning payments pass through at face value.

## `Optional<T>` and the Presence Pattern

For every optional field, the class generates a paired `has*()` / `get*()` accessor. The getter returns `protocol_autogen::Optional<T>`, a type alias defined in `Utils.h` that resolves to `std::optional<std::reference_wrapper<T>>` when `T` is a reference type, and plain `std::optional<T>` otherwise. This handles the nuance that some SField value types are returned by reference from `STObject::at()`, while others are returned by value, ensuring the optional never holds a dangling reference.

Required fields — `sfBalance`, `sfLowLimit`, `sfHighLimit`, `sfPreviousTxnID`, `sfPreviousTxnLgrSeq` — return their values directly with no optional wrapping, since the type invariant (enforced at construction) guarantees they are always present in a valid `RippleState`.

## Flags Encoding

The `getFlags()` method is inherited from `LedgerEntryBase` and returns the raw 32-bit flags word. RippleState uses a pair-of-bits convention for every behavioral flag: `lsfLowReserve`/`lsfHighReserve`, `lsfLowAuth`/`lsfHighAuth`, `lsfLowNoRipple`/`lsfHighNoRipple`, `lsfLowFreeze`/`lsfHighFreeze`, and `lsfLowDeepFreeze`/`lsfHighDeepFreeze`. Each bit is independently set by the corresponding account, so the freeze state of a trust line is determined by reading one bit on behalf of each party rather than through a separate field.

## Relationship to Surrounding Code

`RippleState` is one of roughly thirty generated entry types in `include/xrpl/protocol_autogen/ledger_entries/`. All follow the identical structural pattern: an immutable wrapper inheriting `LedgerEntryBase`, a CRTP builder inheriting `LedgerEntryBuilderBase<Builder>`, a five-or-fewer-argument required-field constructor, and a `build(uint256)` finalizer. The consistency comes from code generation, not convention — no human would reliably maintain this structure across all entry types without drift.