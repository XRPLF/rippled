# `ApplyViewImpl.cpp` — Concrete Transaction Apply View

## Role in the System

`ApplyViewImpl` is the concrete, client-facing implementation of the ledger's write-staging layer for a single transaction. It sits at the top of a three-level class hierarchy: `ApplyView` (abstract interface) → `detail::ApplyViewBase` (read/write delegation to `ApplyStateTable`) → `ApplyViewImpl` (commit-and-metadata endpoint). The file is deliberately minimal — only 39 lines — because almost all substantive logic lives in `ApplyStateTable`, which buffers ledger-entry mutations as a keyed map of `(Action, SLE)` pairs. `ApplyViewImpl`'s distinct contribution is (1) the `deliver_` field that tracks payment delivery amounts for metadata generation, and (2) the `apply()` method that finalises the buffered changes by committing them into a live `OpenView` and producing a `TxMeta` record.

## Class Hierarchy and Design Rationale

`detail::ApplyViewBase` already wires up all the `ReadView`, `ApplyView`, and `RawView` virtual methods by routing every call through the `items_` member (`ApplyStateTable`). This means that during transaction execution, transactors read and mutate ledger state through the `ApplyView` interface without ever touching the real ledger; every mutation is buffered locally. `ApplyViewImpl` inherits all of that machinery and adds only what is needed at the commit boundary.

This layering exists to support discard semantics: if transaction processing fails, the caller simply destroys the `ApplyViewImpl` and the buffered changes evaporate without modifying the base ledger. The design is intentional and safe because `ApplyViewBase` stores the base view as a raw `ReadView const*` — it never takes ownership — so destruction of the apply view is always trivially correct.

## The `apply()` Method and its Finality Contract

```cpp
std::optional<TxMeta>
ApplyViewImpl::apply(OpenView& to, STTx const& tx, TER ter,
                     std::optional<uint256> parentBatchId,
                     bool isDryRun, beast::Journal j)
{
    return items_.apply(to, tx, ter, deliver_, parentBatchId, isDryRun, j);
}
```

The header comment attached to this method carries a strict post-condition: *after calling `apply()`, the only valid operation is destruction*. This is a move-semantics-like contract without actually moving. The rationale is that `ApplyStateTable::apply()` transfers ownership of buffered SLEs into the target `OpenView`, generating transaction metadata in the process. Reusing the `ApplyViewImpl` afterwards would risk applying the same mutations twice or producing corrupt metadata.

The method passes `deliver_` — an `std::optional<STAmount>` — directly into `ApplyStateTable::apply()`. When `deliver_` is set (via the `deliver()` setter), the emitted `TxMeta` will include the `DeliveredAmount` field, which is required for cross-currency payments and partial payment reporting. If not set, the field is absent from metadata, which is the correct behaviour for non-payment transactions.

Two parameters added for batch processing — `parentBatchId` (`std::optional<uint256>`) and `isDryRun` (bool) — pass through unchanged to `ApplyStateTable::apply()`. The `tapBATCH` and `tapDRY_RUN` flags in `ApplyFlags` mirror these at the flag level, but the explicit parameters allow finer-grained control at the metadata-generation layer without requiring flag inspection inside `ApplyStateTable`.

## `size()` and `visit()` — Inspection Without Commitment

`size()` and `visit()` both delegate directly to `items_`. They exist on `ApplyViewImpl` rather than being buried in `ApplyViewBase` because their signatures require access to `OpenView` and to the before/after SLE snapshot mechanism that is only meaningful at the moment of commit. `visit()` in particular provides a callback-based iterator over every buffered modification, exposing both the pre-modification and post-modification SLE through `shared_ptr<SLE const>` pairs — useful for audit, debugging, or incremental metadata building in callers that need to observe changes without yet committing them.

## State Ownership and Resource Safety

All mutated ledger entries are held as `std::shared_ptr<SLE>` inside `ApplyStateTable::items_`. The `before` snapshots captured for metadata are also reference-counted. This means:

- No manual memory management is required anywhere in the apply-view stack.
- Move construction of `ApplyViewImpl` is enabled (and `ApplyViewBase` is also move-constructible) to support efficient placement of apply-view objects in containers like the batch-apply orchestration layer.
- Copy and assignment operators are deleted throughout the hierarchy, enforcing that exactly one `ApplyViewImpl` owns the buffered state at any point.

## Relationship to `OpenView` and `TxMeta`

The `OpenView` passed to `apply()` and `visit()` is the live, mutable ledger view (the in-progress open ledger or canonical ledger). `ApplyStateTable::apply()` replays each buffered `Action` (insert, modify, erase, cache) into that `OpenView` and simultaneously threads the transaction through account ownership chains to produce the complete `TxMeta` structure returned as `std::optional<TxMeta>`. The optional is absent only in dry-run mode, where changes are still staged but no metadata is emitted and effects are not committed to the target view.