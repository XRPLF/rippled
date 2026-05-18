# `Check.h` — Auto-generated Check Ledger Entry Wrapper

## Role in the System

`Check.h` is a machine-generated file under `include/xrpl/protocol_autogen/ledger_entries/` that provides two classes — `Check` and `CheckBuilder` — for working with the XRPL Check ledger entry type (`ltCHECK`, wire value `0x0043`). A Check on the XRPL represents a deferred, pre-authorized payment instruction: the sender (account) commits to letting a specific destination cash up to `sfSendMax` at any time before an optional expiry. No funds are reserved when the Check is created; the payment only moves when the recipient submits a `CheckCash` transaction.

The file sits alongside ~30 peer headers covering every first-class ledger entry type (Offer, Escrow, PayChannel, AccountRoot, etc.). All follow an identical structural pattern, which is the entire motivation for code generation: rather than hand-maintaining fragile ad-hoc accessors for each entry type, a single schema drives the generation of consistent, type-checked wrappers.

## Class `Check` — Immutable Read Facade

`Check` extends `LedgerEntryBase` and owns a `std::shared_ptr<SLE const>` (Serialized Ledger Entry), inherited as `sle_`. The `const`-qualified pointer is the key design choice: it enforces immutability at the type system level. All callers who receive a `Check` know that the ledger data underneath cannot change through this handle — a desirable property when the same `SLE` object may be referenced by multiple readers during transaction processing.

The constructor accepts `std::shared_ptr<SLE const>` and immediately validates `sle_->getType() != entryType`, throwing `std::runtime_error` if the entry is not actually a Check. This is an eager type-guard: rather than silently accessing wrong fields on a mistyped entry, the mismatch is surfaced at construction time, keeping downstream getters unconditionally safe.

### Required vs. Optional Field Access

The accessor design distinguishes between `soeREQUIRED` and `soeOPTIONAL` fields in a first-class way:

- **Required fields** (`sfAccount`, `sfDestination`, `sfSendMax`, `sfSequence`, `sfOwnerNode`, `sfDestinationNode`, `sfPreviousTxnID`, `sfPreviousTxnLgrSeq`) return their value type directly. Calling `sle_->at(sfXxx)` on a required field is safe by ledger invariant — if the field were absent the entry would be invalid and would not have passed ledger validation.
- **Optional fields** (`sfExpiration`, `sfInvoiceID`, `sfSourceTag`, `sfDestinationTag`) return `protocol_autogen::Optional<T>`. This alias, defined in `Utils.h`, is a `std::conditional_t` that resolves to either `std::optional<std::reference_wrapper<U>>` (when `T` is a reference type) or plain `std::optional<T>` (when it is a value). The extra indirection for reference types prevents dangling references when wrapping fields accessed by reference from the underlying `SLE`. Each optional getter is paired with a `hasXxx()` predicate that calls `sle_->isFieldPresent(sfXxx)`, and the getter's body gates the `sle_->at(...)` call through that predicate before returning.

All getters are `[[nodiscard]]` and `const`, which reinforces the read-only contract and warns callers who accidentally discard return values.

## Class `CheckBuilder` — Fluent Construction

`CheckBuilder` extends `LedgerEntryBuilderBase<CheckBuilder>`, a CRTP template whose `object_` member is an `STObject{sfLedgerEntry}`. The base class deliberately avoids calling `object_.set(soTemplate)` on construction (noted in a comment): doing so would pre-populate `soeDEFAULT` placeholder fields, which the `SLE` constructor's internal `applyTemplate()` call would then reject as "may not be explicitly set to default." By leaving `object_` as a free `STObject`, the builder accumulates only the fields that are explicitly set, and the `SLE` constructor fills gaps according to the template on finalization.

`CheckBuilder` takes all eight required fields in its primary constructor, calling the corresponding setters immediately. This guarantees that a `CheckBuilder` can never produce an incomplete `Check` lacking required data — the compiler enforces it. The secondary constructor takes `std::shared_ptr<SLE const>` to clone an existing entry into the builder for editing; it performs the same type check (`sfLedgerEntryType != ltCHECK`) before copying with `object_ = *sle`.

Each setter returns `CheckBuilder&`, enabling method chaining: `builder.setExpiration(exp).setSourceTag(tag).build(index)`. The CRTP base's common setters (`setFlags`, `setLedgerIndex`) also return `Derived&` via `static_cast<Derived&>(*this)`, so they slot seamlessly into the same chain.

The `build(uint256 const& index)` method finalizes construction by calling `std::make_shared<SLE>(std::move(object_), index)` and wrapping the result in a `Check`. Moving `object_` into the `SLE` avoids a copy and signals that the builder is consumed — use-after-build would operate on an empty `STObject`.

## Relationship to Validation

Both `Check` (via `LedgerEntryBase::validate()`) and `CheckBuilder` (via `LedgerEntryBuilderBase::validate()`) delegate validation to `protocol_autogen::validateSTObject()`, which consults `LedgerFormats::getInstance()` to retrieve the canonical `SOTemplate` for `ltCHECK` and checks required/optional field presence against it. This shared path means builder and wrapper agree on exactly the same validity criteria.

## Domain Semantics Encoded in the Schema

The field set reflects the Check's place in the XRPL owner-directory graph. `sfOwnerNode` is a back-pointer into the sender's owner directory (a `DirectoryNode` ledger entry), and `sfDestinationNode` is a back-pointer into the recipient's owner directory — both required so the ledger can efficiently remove the Check object and update both directories when the check is cashed or cancelled. `sfPreviousTxnID` and `sfPreviousTxnLgrSeq` are standard audit fields present on all mutable ledger objects, allowing nodes to trace which transaction last touched this entry.