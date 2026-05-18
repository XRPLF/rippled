# `OpenView.cpp` — Mutable Ledger Scratchpad for Transaction Processing

`OpenView` is the writable in-memory layer that sits above a read-only committed ledger during transaction processing on the XRPL. Where `ReadView` provides a snapshot of settled ledger state, `OpenView` accumulates pending mutations — state-object changes and new transactions — without touching the underlying base. When all desired transactions have been applied, the caller calls `apply()` to flush those changes into the next committed view.

## Architecture: Two Independent Delta Layers

`OpenView` maintains two separate deltas over its `base_` (`ReadView const*`):

- **State delta** (`items_`, a `detail::RawStateTable`): tracks inserts, erases, and replacements of `SLE` (serialized ledger entries). Lookups in `exists()`, `read()`, `succ()`, and the SLE iterators all route through `RawStateTable`, which merges local mutations with the base view transparently.
- **Transaction delta** (`txs_`, a `txs_map`): an ordered map from transaction hash (`uint256`) to a `txData` struct holding serialized blobs for the transaction body and its metadata.

This split is intentional. State entries and transaction entries have different lifecycles and access patterns: state is frequently read back during the same transaction processing pass, while transactions are write-once and only iterated at commit time.

## Constructors and Ledger Header Setup

There are three constructors covering distinct use cases:

The `open_ledger_t`-tagged constructor creates a fresh view for building the *next* ledger on top of a closed parent. It explicitly mutates the copied header: `seq` is incremented by one, `parentCloseTime` and `parentHash` are set from the base header, and `validated`/`accepted` are cleared to false. This enforces the invariant that an open (in-flight) ledger is never mistakenly treated as final.

The bare `ReadView const*` constructor copies the header and rules verbatim, inheriting `open_` from the base. This is used when building a last-closed ledger view where header mutation has already been done externally.

The copy constructor makes a shallow structural copy — the `SLE` objects referenced by `items_` are shared (they are immutable, so sharing is safe) but the map itself is duplicated. This is used to take snapshots before speculative execution. Note that move assignment is deleted while move construction is allowed, which prevents accidental overwrites of existing views.

The `batch_view_t` constructor wraps another `OpenView` as a stacking layer for batch transaction processing. It captures the current `txCount()` as `baseTxCount_`, which ensures that `txCount()` returns the total transaction count including any previously applied transactions. This ordinal is used when computing transaction metadata apply-order during ledger close.

## Memory Management Strategy

Both `items_` and `txs_` use `boost::container::pmr::monotonic_buffer_resource` for their allocators. This is a bump-pointer arena: a 256 KB block is pre-allocated at construction, and map nodes are carved out of it without individual heap allocations. When the arena fills, it falls back to heap. The design trades deallocation granularity (individual map entries cannot be freed) for throughput — transaction processing is latency-sensitive and map churn is high.

The `monotonic_resource_` member is a `unique_ptr` declared before both `txs_` and `items_`, ensuring it is destroyed *after* them (C++ member destruction is reverse-declaration-order). This is the only lifetime guarantee the arena has, and the header comment makes this dependency explicit.

The `hold_` member is a `shared_ptr<void const>` — an intentionally opaque ownership anchor. The caller can pass any reference-counted object (e.g., the base ledger itself, a cache entry) and it will be kept alive for as long as the `OpenView` exists, preventing the base pointer from dangling.

## Transaction Iteration: `txs_iter_impl`

The private `txs_iter_impl` class adapts the flat `txs_map` iterator into the polymorphic `txs_type::iter_base` interface used by `ReadView`. It carries two pieces of state: the underlying `txs_map::const_iterator` and a `metadata_` flag.

The `metadata_` flag is set to `!open()`. Open ledgers do not produce transaction metadata (metadata is only computed at close time), so when iterating transactions on an open view the metadata blob is simply omitted. For closed-ledger views the metadata is deserialized alongside the transaction body.

Deserialization in `dereference()` is on-demand: the raw `Serializer` blobs are decoded into `STTx` and `STObject` only when the iterator is dereferenced. This avoids paying parsing costs for transactions that are never inspected.

The `equal()` method guards against iterator cross-type comparison by using `dynamic_cast`. If the passed `base_type` is not a `txs_iter_impl`, it returns false rather than undefined behavior.

## `txRead()` and the Fallback Pattern

`txRead()` demonstrates the two-layer lookup directly: it first checks the local `txs_` map. On a miss it falls back to `base_->txRead()`. This means a view can see transactions that came from the committed base ledger as well as newly inserted ones in the same interface. The metadata nullability check is explicit — during open-ledger processing, `meta` is a null `shared_ptr` by convention, and the caller receives `nullptr` for the second element of the returned pair.

## `apply()` and Commit Flow

`apply(TxsRawView& to)` is the one-way commit operation. It first delegates `items_.apply(to)` to flush all state mutations, then iterates `txs_` and calls `to.rawTxInsert()` for each transaction. This ordering means state changes always precede transaction insertion when composing into the target view, which is the expected ledger close sequence.

## Error Handling and Open Issues

`rawTxInsert()` enforces uniqueness: inserting a duplicate transaction hash triggers `LogicError`, which throws and is treated as a programming bug rather than a recoverable runtime condition. Duplicate transactions in a single ledger would violate fundamental XRPL consensus invariants.

`rawDestroyXRP()` delegates fee burning to `items_.destroyXRP(fee)` but carries a `VFALCO`-attributed comment questioning whether `header_.totalDrops` should also be decremented and how child views should propagate that change. This unresolved design question suggests that XRP supply accounting across stacked views may not be fully consistent in all code paths.