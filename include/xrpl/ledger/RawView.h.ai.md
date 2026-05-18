# `include/xrpl/ledger/RawView.h` — Raw Ledger Mutation Interface

`RawView.h` defines two pure abstract interfaces — `RawView` and `TxsRawView` — that represent the lowest-level write surface in the XRPL ledger's view hierarchy. Where `ReadView` describes what can be observed about a ledger and `ApplyView` provides a journaled checkout/update cycle for transaction processing, `RawView` describes what it means to *commit* a mutation unconditionally to some backing store.

## Role in the View Hierarchy

The XRPL ledger uses a layered view architecture. Transaction processing never writes directly to a finalized ledger; instead it works through a sandbox (`ApplyView` / `ApplyViewBase`) that journals changes. When the sandbox is satisfied — either because a transaction succeeded or because a consensus round is closing — its buffered mutations need to be flushed down to the parent store. That flush path is `RawView`.

`detail::RawStateTable::apply(RawView& to)` is the canonical consumer: it iterates its internal map of pending `erase`/`insert`/`replace` actions and dispatches each through the corresponding `raw*` method on whatever backing `RawView` was passed in. This design means the flushing logic is written once against the three-operation contract, and any concrete target — a finalizing `Ledger`, an open-ledger `OpenView`, or another sandbox — implements that contract without exposing higher-level checkout semantics.

## `RawView`

`RawView` exposes exactly four operations, one per fundamental mutation type:

- `rawErase(sle)` — remove an existing state item. The full `SLE` is passed (not just its key) so that implementations can compute metadata like the change in owner count or deleted ledger object type.
- `rawInsert(sle)` — unconditionally insert; the key must not already exist. The key is read from the `SLE` itself rather than passed separately, which prevents key/value mismatches.
- `rawReplace(sle)` — unconditionally overwrite; the key must already exist. Same key-from-SLE convention.
- `rawDestroyXRP(fee)` — permanently remove a quantity of XRP drops from the ledger supply. This is the accounting hook for transaction fees, which are burned in XRPL rather than redistributed. Separating this from `rawErase` makes fee accounting explicit and auditable.

The "raw" prefix is intentional and carries a semantic contract: these methods perform no pre-condition checking, no journaling, and no ownership tracking. They are the implementation side of the commit path, not the API that transaction logic should call directly.

The copy constructor is defaulted while the assignment operator is deleted. For an abstract base, this is a deliberate asymmetry: it signals that subclasses may be copyable (useful when snapshotting a view's state) but that assignment across different concrete types should not silently succeed.

## `TxsRawView`

`TxsRawView` inherits `RawView` and adds one method:

```cpp
virtual void rawTxInsert(
    ReadView::key_type const& key,
    std::shared_ptr<Serializer const> const& txn,
    std::shared_ptr<Serializer const> const& metaData) = 0;
```

This inserts a serialized transaction — and optionally its execution metadata — into the ledger's transaction map. The `metaData` parameter is nullable by convention: open ledgers don't produce transaction metadata (consensus hasn't closed yet), while closed ledgers require it. The comment in the header makes this invariant explicit rather than leaving it to callers to discover.

The split between `RawView` (state-only writes) and `TxsRawView` (state plus transaction map) is architecturally meaningful. `detail::ApplyViewBase` only needs to implement `RawView` — sandboxes accumulate state mutations but don't independently maintain a transaction map. `OpenView`, by contrast, inherits from both `ReadView` and `TxsRawView`: it is the accumulation point for an open ledger round and must track both state changes and the growing set of applied transactions.

## Concrete Implementations

`detail::ApplyViewBase` inherits from both `ApplyView` and `RawView`, realizing the full read-modify-write-and-commit stack. Its `rawErase`/`rawInsert`/`rawReplace` implementations delegate to an internal `detail::ApplyStateTable`, which in turn owns a `RawStateTable`. When an `ApplyViewBase` is applied to its parent, `RawStateTable::apply()` drives all mutations back through the parent's `RawView` interface.

`OpenView` inherits `TxsRawView` and implements `rawTxInsert` to store serialized transaction data in an ordered map, while its state mutation methods delegate to its own `RawStateTable`. This architecture ensures `OpenView` can be used as the sink for closing a consensus round without requiring any knowledge of higher-level transaction logic.

The result is a clean separation: `ApplyView` is for transaction code that needs safe, journaled mutation with ownership semantics; `RawView` is for infrastructure code that flushes committed changes unconditionally. Nothing in the `raw*` path touches checksums, performs existence validation, or participates in the SLE checkout protocol — it is the fast, trust-the-caller commit surface.