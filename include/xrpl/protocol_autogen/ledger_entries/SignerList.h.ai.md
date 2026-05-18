# `SignerList.h` — Auto-Generated Ledger Entry Wrapper

## Role in the System

This file is part of the `protocol_autogen` subsystem, which generates type-safe C++ wrappers for every XRPL ledger entry type. `SignerList.h` represents the `ltSIGNER_LIST` (0x0053) ledger entry — the on-ledger record that enables multi-signature authorization for an XRPL account. When an account activates multi-signing via a `SignerListSet` transaction, a `SignerList` object is written to the ledger containing the set of authorized co-signers and the quorum weight required to approve subsequent transactions.

The file lives in `include/xrpl/protocol_autogen/ledger_entries/` alongside approximately thirty other generated entry wrappers (e.g., `AccountRoot.h`, `Offer.h`, `Escrow.h`). It is auto-generated — the comment at line 1 is literal — meaning the source-of-truth is an upstream schema definition, and manual edits would be overwritten.

## The Two-Class Pattern

Every autogen entry header defines exactly two classes: a read-only wrapper named after the entry type (`SignerList`) and a companion builder (`SignerListBuilder`). This strict separation between read and write paths is a deliberate architectural choice. The ledger state is immutable once written; wrapping it in a `const`-correct, read-only view prevents accidental mutation through the accessor layer.

### `SignerList` — Immutable Wrapper

`SignerList` inherits from `LedgerEntryBase`, which itself wraps a `std::shared_ptr<SLE const>` (SLE = Serialized Ledger Entry, the XRPL canonical on-wire object). The constructor takes shared ownership of an existing SLE and immediately validates that its type tag is `ltSIGNER_LIST`, throwing `std::runtime_error` on mismatch. This upfront check means any subsequent field access on a `SignerList` instance can assume the correct entry type without repeated defensive checks.

The class exposes six domain-specific getters:

- `getOwnerNode()` returns an `SF_UINT64` index hint into the owner directory tree, used for efficient ledger traversal when an account holds many objects.
- `getSignerQuorum()` returns the minimum total signer weight required to authorise a transaction — the core threshold of the multi-sig scheme.
- `getSignerEntries()` returns a `const STArray&` — an array of `SignerEntry` inner objects, each carrying an account ID and its weight (`sfAccount`, `sfSignerWeight`). This is the only field returned as an `STArray` rather than a scalar value type; the comment marks it as "untyped (unknown)", reflecting that the code generator has no first-class typed array accessor for inner objects.
- `getSignerListID()` returns a `uint32_t` identifier — currently always `0` per protocol. The field was introduced to reserve space for a potential future feature allowing multiple signer lists per account, though that extension was never deployed.
- `getPreviousTxnID()` and `getPreviousTxnLgrSeq()` are standard bookkeeping fields present on nearly every mutable ledger entry, recording which transaction last touched this object.

The `sfOwner` field is optional (`soeOPTIONAL`), so `getOwner()` returns `protocol_autogen::Optional<SF_ACCOUNT::type::value_type>`. This type alias, defined in `Utils.h`, resolves to `std::optional<T>` for value types and `std::optional<std::reference_wrapper<T>>` for reference types — ensuring that both reference and value semantics for optional fields work correctly without separate specialisations. A `hasOwner()` predicate mirrors the pattern used by `LedgerEntryBase` for common optional fields like `sfLedgerIndex`.

### `SignerListBuilder` — Fluent Builder

`SignerListBuilder` inherits from the CRTP base `LedgerEntryBuilderBase<SignerListBuilder>`. The base holds an `STObject object_{sfLedgerEntry}` as mutable storage and provides common setters for `sfFlags` and `sfLedgerIndex`. The CRTP pattern lets each `setXxx()` return `SignerListBuilder&` rather than a `LedgerEntryBuilderBase&`, enabling unbroken method chaining without virtual dispatch or casts at call sites.

The constructor enforces the required/optional split directly: all six required fields (`ownerNode`, `signerQuorum`, `signerEntries`, `signerListID`, `previousTxnID`, `previousTxnLgrSeq`) must be provided at construction time. The optional `sfOwner` has no corresponding constructor parameter and must be set explicitly via `setOwner()` afterward. This makes it impossible to accidentally omit a required field and only discover the problem at validation time.

There is a secondary constructor accepting an existing `SLE const` — it copies the SLE's field values into the internal `STObject`, enabling a pattern where callers load an existing ledger entry, wrap it in a builder, mutate specific fields, and re-build a new entry. This is useful in amendment-driven upgrade logic or test helpers.

A critical subtlety in `LedgerEntryBuilderBase`: the constructor deliberately does not call `object_.set(soTemplate)`. Doing so would pre-populate all `soeDEFAULT` fields with placeholder `STBase` values, which would then cause the `SLE` constructor's `applyTemplate()` call to throw "may not be explicitly set to default". Leaving those fields absent from the `STObject` lets the `SLE` constructor set them properly on finalisation.

The `build(uint256 const& index)` method finalises construction by moving the internal `STObject` into an `SLE` keyed by the provided index, then wrapping it in the `SignerList` read-only type, completing the read/write lifecycle.

## Design Tradeoffs

The strict immutability of `SignerList` means callers cannot patch individual fields on an existing wrapper — they must go through the builder, which creates a new SLE. This adds a copy but eliminates entire classes of accidental state corruption in ledger processing code. The autogen approach also ensures that every entry type has a uniform interface contract: every entry always has `getType()`, `getKey()`, `getFlags()`, `validate()`, and `getSle()` inherited from `LedgerEntryBase`, while entry-specific fields are consistently exposed through named accessors with `[[nodiscard]]` annotations to discourage silently ignored return values.