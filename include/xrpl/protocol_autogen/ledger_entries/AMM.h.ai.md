# `AMM.h` — Auto-generated AMM Ledger Entry Wrapper

## Role and Context

This file is part of `xrpl/protocol_autogen/ledger_entries/`, a directory of auto-generated wrappers for every ledger entry type in the XRPL. `AMM.h` specifically encapsulates the **Automated Market Maker** ledger entry (`ltAMM`, type code `0x0079`), which represents a constant-product liquidity pool on the ledger. The file must not be edited by hand — it is generated from a schema that describes the AMM entry's field set, cardinalities, and types, and regenerated whenever that schema changes.

The file lives in the `xrpl::ledger_entries` namespace and defines two classes: `AMM` (an immutable read-side view) and `AMMBuilder` (a write-side fluent constructor). This split enforces a clean read/write boundary: application code that only reads ledger state gets an `AMM`; code that creates or modifies entries uses `AMMBuilder`.

## The `AMM` Wrapper Class

`AMM` inherits from `LedgerEntryBase`, which holds a `std::shared_ptr<SLE const>` (a `const`-qualified Serialized Ledger Entry). All field access delegates to `sle_->at(sfField)` or `sle_->isFieldPresent(sfField)`, so the wrapper adds zero runtime overhead beyond the shared-pointer indirection already present in the underlying storage.

The constructor accepts a `std::shared_ptr<SLE const>` and immediately validates that `sle_->getType() == ltAMM`, throwing `std::runtime_error` on mismatch. This guards against accidentally wrapping the wrong entry type — a risk that exists whenever raw `SLE` objects are passed around by the core ledger engine.

### Field Schema and Getter Strategy

The AMM entry's fields fall into three cardinality classes, and the getter design mirrors them exactly:

**Required fields** (`soeREQUIRED`) — `sfAccount`, `sfLPTokenBalance`, `sfAsset`, `sfAsset2`, `sfOwnerNode` — are returned directly by value with no `std::optional`. Accessing them on a well-formed entry always succeeds; the type system reflects that guarantee.

**Default fields** (`soeDEFAULT`) — `sfTradingFee` — may be absent from the serialized form when equal to their default value (zero in this case). `getTradingFee()` returns `protocol_autogen::Optional<uint16_t>` and is guarded by `hasTradingFee()`. The `Optional<T>` alias from `Utils.h` is a compile-time conditional: for reference types it becomes `std::optional<std::reference_wrapper<T>>` to avoid dangling references; for value types like `uint16_t` it collapses to plain `std::optional<T>`.

**Optional fields** (`soeOPTIONAL`) — `sfVoteSlots`, `sfAuctionSlot`, `sfPreviousTxnID`, `sfPreviousTxnLgrSeq` — follow the same `has*()`/`get*()` pattern. Notably, `getVoteSlots()` returns `std::optional<std::reference_wrapper<STArray const>>` rather than a copy of the array. This avoids copying potentially large vote-slot arrays, while keeping the return type nullable for the absent case. `getAuctionSlot()` returns `std::optional<STObject>` — a by-value copy — because `STObject` does not have stable lifetime tied to the SLE and must be returned as an independent snapshot.

### AMM Domain Fields

- `sfAccount`: the special-purpose AMM account that holds the pool's asset reserves. This is a separate account from any user; it cannot sign transactions and exists solely to carry the pool balances.
- `sfAsset` / `sfAsset2`: the two sides of the pool, typed as `SF_ISSUE::type::value_type` (i.e., an `Issue` struct identifying currency and issuer). Together they uniquely identify the trading pair.
- `sfLPTokenBalance`: total outstanding LP token supply as an `STAmount`. LP tokens represent proportional ownership of the pool and are minted/burned by deposit/withdrawal transactions.
- `sfTradingFee`: a `uint16_t` fee in basis points (0–1000 representing 0–1%), charged on each swap and distributed to liquidity providers.
- `sfVoteSlots`: an `STArray` of vote entries, one per top LP token holder, used by the AMM governance mechanism to collectively adjust the trading fee.
- `sfAuctionSlot`: an optional `STObject` representing the currently active auction slot, which grants the slot holder discounted swap fees for a time window.
- `sfOwnerNode`, `sfPreviousTxnID`, `sfPreviousTxnLgrSeq`: bookkeeping fields common to many ledger entry types — the owner directory back-pointer and audit-trail pair tracking the last modifying transaction.

## The `AMMBuilder` Class

`AMMBuilder` inherits from `LedgerEntryBuilderBase<AMMBuilder>`, a CRTP base that holds the mutable `STObject object_{sfLedgerEntry}` being assembled. The CRTP pattern enables `setLedgerIndex()` and `setFlags()` (defined on the base) to return `AMMBuilder&` rather than `LedgerEntryBuilderBase&`, preserving the fluent chaining API without virtual dispatch.

The constructor enforces required-field discipline: `account`, `lPTokenBalance`, `asset`, `asset2`, and `ownerNode` are all mandatory parameters. Optional and default fields are set via chained setter calls after construction. A second constructor takes an existing `std::shared_ptr<SLE const>`, copying the SLE's fields into `object_` via `object_ = *sle` — enabling round-trip editing patterns where existing ledger state is read, modified through builder setters, and rebuilt.

One subtle but important design note lives in `LedgerEntryBuilderBase`'s constructor comment: it deliberately avoids calling `object_.set(soTemplate)`. That call would create `STBase` placeholder objects for every `soeDEFAULT` field, which causes `applyTemplate()` (called inside the `SLE` constructor during `build()`) to throw "may not be explicitly set to default." By leaving those slots absent in the free `STObject`, the SLE constructor's own template application handles them correctly.

The `setAsset()` and `setAsset2()` setters explicitly wrap their `Issue` argument as `STIssue(sfAsset, value)` before storing it. This is necessary because `SF_ISSUE::type::value_type` resolves to the plain `Issue` struct, but `STObject` requires the fully-typed serialized wrapper `STIssue` (which pairs the field descriptor with the value) to be stored under the correct field key.

`build(uint256 const& index)` finalizes construction by moving `object_` into a new `SLE` at the given ledger key, then wrapping it in an `AMM` view. After `build()` the builder's `object_` is moved-from and should not be reused.

## Relationship to Surrounding Infrastructure

Every file in `ledger_entries/` follows the identical `Foo` + `FooBuilder` pattern. The uniformity is deliberate — auto-generation guarantees that all entry types get the same correctness properties (type-checked construction, mandatory field enforcement, `has*()`/`get*()` pairing for nullable fields) without any per-entry hand-written code. The `LedgerEntryBase::validate()` method, shared by all entry types, cross-checks the assembled `STObject` against the live `LedgerFormats` schema template, providing a runtime safety net in test and debug builds.