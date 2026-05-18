# `ApplyViewImpl` — Transaction Apply View

## Role in the System

`ApplyViewImpl` is the concrete, per-transaction scratch-pad that the XRPL ledger engine uses whenever it needs to execute a single transaction against a view of ledger state. It sits at the top of a three-layer inheritance chain: `ReadView` → `ApplyView` (adds mutation semantics) → `detail::ApplyViewBase` (adds buffered state storage) → `ApplyViewImpl` (adds metadata construction and final commit). The design separates *reading* ledger state from *mutating* it, and separates *mutating* from *committing* — giving transaction processing the ability to speculatively apply changes and then either commit them or discard the entire view without touching the parent `OpenView`.

## Class Hierarchy

`ApplyViewImpl` inherits from `detail::ApplyViewBase`, which itself inherits from both `ApplyView` and `RawView`. `ApplyViewBase` holds the three key protected members that `ApplyViewImpl` operates through: `flags_` (the `ApplyFlags` bitmask), `base_` (a non-owning `const` pointer to the underlying `ReadView`), and `items_` (an `ApplyStateTable` that buffers every insert, modify, erase, and XRP destruction operation as a map of `key → (Action, SLE)`). `ApplyViewImpl` adds only one data member of its own: `deliver_`, an `optional<STAmount>` for tracking payment delivery amounts.

## `apply()` — The Point of No Return

The central method is `apply(OpenView& to, STTx const& tx, TER ter, std::optional<uint256> parentBatchId, bool isDryRun, beast::Journal j)`. The comment in the header is deliberate and important: after `apply()` is called, the only valid operation on the object is destruction. This is not enforced by the type system, but the intent is clear — `apply()` hands ownership of the buffered mutations down to `items_.apply(...)` in `ApplyStateTable`, which drains them into the target `OpenView` while simultaneously constructing and returning a `TxMeta` object capturing before/after state for every modified SLE. The `TxMeta` is returned as `std::optional<TxMeta>` to handle `isDryRun` scenarios where metadata is computed but the ledger changes are not actually committed.

The `parentBatchId` parameter reflects support for batch transactions: when a transaction is part of a batch, the metadata carries a reference to the parent batch transaction ID, linking individual results back to their containing batch context via `tapBATCH`.

## `deliver()` — Payment Metadata Annotation

`deliver(STAmount const& amount)` is a one-shot setter that stores the delivered currency amount for use when building transaction metadata. In XRPL payment transactions, the amount actually delivered to the destination can differ from the amount sent (due to pathfinding, exchange rates, or partial payments). The `DeliveredAmount` metadata field communicates this to clients. Callers set this *before* calling `apply()`; if it is never set, `deliver_` remains `std::nullopt` and the `DeliveredAmount` field is omitted from the resulting metadata. The actual threading of this value into `TxMeta` happens inside `ApplyStateTable::apply()`.

## Copy/Move Semantics

`ApplyViewImpl` carefully disables copy construction, copy assignment, and move assignment, while allowing move construction. This is consistent with the ownership model: the object exclusively owns its buffered mutation state, and only one view should ever be in a position to commit to the parent `OpenView`. Allowing moves but not copies prevents accidental duplication of a pending transaction's state table while still permitting the object to be constructed and returned from factory functions without heap allocation.

## `size()` and `visit()`

`size()` delegates to `items_.size()`, returning the count of modified entries in the state table. `visit()` delegates to `items_.visit(to, func)`, iterating every modified SLE and invoking a callback with the key, a deletion flag, and `shared_ptr`s to the before and after states. These are used during batch processing — callers can inspect the changes accumulated for one transaction before deciding whether to apply them to the shared `OpenView`, enabling partial-commit semantics for batch transactions.

## Relationship to `OpenView`

Both `apply()` and `visit()` take `OpenView&` as their target. `OpenView` is the writable accumulator for an entire ledger's worth of transactions; `ApplyViewImpl` provides the per-transaction isolation layer. This mirrors a standard copy-on-write pattern: the base `ReadView` is shared and immutable, all mutations are buffered in `items_`, and only `apply()` flushes them forward. Because `ApplyViewBase` holds `base_` as a raw `const*` rather than a shared pointer, the caller is responsible for ensuring the underlying view outlives the `ApplyViewImpl` — a deliberate performance choice that avoids atomic reference count traffic on the hot path.

## `ApplyFlags` and Contextual Behavior

The `ApplyFlags` passed at construction time flow through `flags_` and are accessible via `ApplyViewBase::flags()`. Flags such as `tapRETRY`, `tapFAIL_HARD`, `tapUNLIMITED`, `tapBATCH`, and `tapDRY_RUN` allow the transaction engine to signal its execution context to the view. For example, `tapDRY_RUN` causes the engine to compute outcomes without finalizing state, which is reflected in the `isDryRun` parameter ultimately passed to `ApplyStateTable`. The flag `tapBATCH` signals that the current view is operating within a batch context, which affects how `parentBatchId` is recorded in the resulting `TxMeta`.