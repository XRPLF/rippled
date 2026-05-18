# `include/xrpl/ledger/OpenView.h`

## Role in the System

`OpenView` is the primary mutable ledger surface used during transaction processing in the XRP Ledger. It models a ledger that has not yet been closed and validated — transactions are being applied to it, state objects are being modified, and the result is still in flux. Once processing completes, the accumulated changes are flushed to a target view via `apply()`.

The design is a delta-accumulation pattern: `OpenView` holds a reference to an immutable base `ReadView` (typically the most recent closed ledger) and records all modifications as a pending diff on top of it. Nothing is written through to the base until `apply()` is called. This makes it safe to discard changes on failure, or to evaluate what changes a transaction would cause before committing.

## Inheritance and Interface Exposure

`OpenView` inherits from both `ReadView` and `TxsRawView`. The read interface lets transaction logic and validation code query the current apparent state (base + pending changes) without needing to know whether the ledger is settled. The `TxsRawView` write interface lets transaction processors insert SLE mutations (`rawErase`, `rawInsert`, `rawReplace`) and register applied transactions (`rawTxInsert`).

Callers holding a `ReadView const*` see a coherent read-only snapshot that transparently merges base state and pending modifications. Callers with a `TxsRawView*` or `OpenView*` can write into it. This asymmetry is deliberate: much of the ledger traversal and validation code is written against the read-only interface, and `OpenView` plugs directly into that without modification.

## Construction Modes

Three construction paths reflect distinct lifecycle stages:

**`open_ledger_t` tag**: Builds a fresh open ledger on top of a base. The header sequence is bumped by one, `parentCloseTime` and `parentHash` are set from the base, and `validated`/`accepted` flags are cleared. The `Rules` object is supplied explicitly because open ledger rules may differ from what the base recorded. An optional `hold` shared pointer keeps the underlying base object alive for the view's lifetime.

**`ReadView const*` without tag**: Used to construct a last-closed-ledger view. It copies the base header and rules directly, and inherits the `open_` flag — so if the base was a closed ledger, this view will also report itself as closed.

**`batch_view_t` tag**: Used during batch transaction processing. The new view wraps an existing `OpenView` rather than a base `ReadView`, and records the current transaction count of the parent (`baseTxCount_`). This ensures `txCount()`, which drives the apply-ordinal used in metadata, remains correct even as the batch stack grows.

## State Change Buffering: `RawStateTable` and `txs_map`

State object changes are buffered in `items_`, a `detail::RawStateTable`. `RawStateTable` maintains a `std::map` from ledger key to a tagged action (`erase`, `insert`, or `replace`) alongside the modified `SLE`. All `ReadView` queries — `exists()`, `read()`, `succ()`, iteration — go through `RawStateTable` passing the base view as a fallback, so the merged view is always consistent with both the base and pending mutations.

Transaction records are held in `txs_`, a `std::map<key_type, txData>` where `txData` pairs a serialized transaction (`txn`) with optional serialized metadata (`meta`). Open ledgers omit metadata; closed ledger representations include it. The `open_` flag drives this distinction: `txsBegin()` and `txsEnd()` pass `!open()` to the `txs_iter_impl`, which controls whether `dereference()` deserializes the metadata field.

The `rawTxInsert()` implementation calls `LogicError` on a duplicate key — duplicate transaction IDs are a hard invariant violation, not a recoverable error.

## Memory: Monotonic PMR Allocation

Both `OpenView` and `RawStateTable` allocate their internal maps using `boost::container::pmr::monotonic_buffer_resource` with a 256 KB initial buffer. The pool starts with a pre-allocated block and grows linearly, making allocation O(1) amortized with no per-element heap overhead. This is important because many small SLE-keyed map entries are inserted during block processing, and the arena strategy avoids the fragmentation and lock contention of the default allocator.

The `monotonic_resource_` member is a `std::unique_ptr` rather than a value, for two reasons: the map's `polymorphic_allocator` holds a raw pointer to the resource and would break if the resource moved, and `unique_ptr` allows `OpenView` to be move-constructed while still maintaining stable resource addressing. The copy constructor allocates a fresh 256 KB arena and copies the map's contents into it using the new allocator. The comment notes this 256 KB size comes from the legacy `qalloc` allocator it replaced.

## The `apply()` Commit Path

`void apply(TxsRawView& to)` is the commit operation. It calls `items_.apply(to)` to replay all SLE-level mutations onto the target, then iterates `txs_` and calls `to.rawTxInsert()` for each transaction. The typical call site is `ApplyViewImpl::apply()`, which applies a per-transaction sandbox into the enclosing `OpenView`. Later, the `OpenView` itself is applied into the final ledger object. The two-phase structure means each transaction can be independently discarded without affecting the accumulator.

## `txCount()` and Ordinal Tracking

`txCount()` returns `baseTxCount_ + txs_.size()`. The apply ordinal embedded in transaction metadata must be globally unique and monotonically increasing within a ledger. In batch mode where views are stacked, `baseTxCount_` captures how many transactions had already been applied to the parent before this child view was constructed, preserving correct ordinals even when sub-views are committed incrementally.

## Key Invariants

- `rawTxInsert` rejects duplicate transaction IDs with a hard logic error.
- `items_` is always queried with a reference to `*base_`, ensuring that any key absent from the diff falls through to the authoritative base state.
- The `monotonic_resource_` is always constructed before `txs_` and `items_`, and outlives them — enforced by declaration order and the fact that both maps hold a raw pointer to the resource.
- Move assignment and copy assignment are deleted; only move construction and copy construction are available, ensuring the resource/map lifetime coupling cannot be inadvertently broken.