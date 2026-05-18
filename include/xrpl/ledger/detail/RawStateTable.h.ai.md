# `include/xrpl/ledger/detail/RawStateTable.h`

## Role in the System

`RawStateTable` is the low-level write buffer sitting underneath every mutable ledger view in XRPL. It solves a specific problem: when a transaction — or a batch of transactions during consensus — modifies the ledger state, those changes must be accumulated in memory and only flushed to the underlying store after the transaction succeeds. This class is that accumulator. It lives in the `xrpl::detail` namespace because it is an implementation detail of `OpenView` and similar view classes, not a public-facing API.

The class maintains an ordered map from `uint256` ledger keys to `sleAction` records. Each record pairs an `Action` tag (`erase`, `insert`, or `replace`) with a `shared_ptr<SLE>` — the serialized ledger entry being acted on. All read operations against this table overlay these pending changes on top of a `ReadView const& base`, presenting a coherent merged picture of the current state without touching the base ledger until `apply()` is called.

## Memory Strategy: Monotonic Allocation

The internal `items_` map uses a `boost::container::pmr::polymorphic_allocator` backed by a `boost::container::pmr::monotonic_buffer_resource`. The rationale is explicit in the source comment: this replaces an earlier `qalloc` scheme. A monotonic resource simply bumps a pointer for each allocation — it never frees individual nodes — making map insertions extremely cheap and avoiding heap fragmentation during the burst of operations that constitute a single transaction round. The initial 256 KB buffer (`initialBufferSize`) was inherited from `qalloc` and covers the typical working set without triggering growth.

Because `monotonic_buffer_resource` cannot be shared or assigned, the copy constructor allocates a **fresh** resource and then copy-constructs `items_` from the source. The `unique_ptr` wrapper on the resource is specifically noted in the code as allowing the type to be moveable while keeping the address of the resource stable (since `items_` stores a raw pointer to it). Both assignment operators are deleted, preventing accidental copies through a different path. Move construction transfers the `unique_ptr` directly, leaving the source empty.

## Mutation Operations and State Transitions

`erase()`, `insert()`, and `replace()` all follow the same pattern: attempt an `emplace` into `items_`. If the emplace succeeds (no prior entry), the operation is recorded directly. If the key already exists, the code applies a state-machine transition:

- `insert` after `erase`: converts the record to `replace`. An item that was previously erased and is now re-inserted effectively becomes a replacement from the perspective of the base view.
- `erase` after `insert`: removes the entry entirely from the map. The two operations cancel each other out — the base never needs to know.
- `erase` after `replace`: updates the action to `erase` and updates the stored SLE.
- `insert` after `insert` or `replace`, and `erase` after `erase`, call `LogicError` — these represent invariant violations that indicate a bug upstream.
- `replace` after `insert` or `replace`: simply updates the stored SLE pointer, since the action type is already correct.

This state-machine collapse is important because it keeps the map minimal. A sequence like "insert, then replace twice, then erase" leaves no entry rather than three to flush.

## Read Operations Against the Overlay

`read()` checks `items_` first. If the key is absent, it falls through to `base.read(k)`. If the key is present but the action is `erase`, it returns `nullptr` — the entry has been logically deleted. Otherwise it validates the type tag via `k.check(*sle)` before returning, ensuring that a key collision of different SLE types (which should never happen but is a defensive check) returns `nullptr` rather than the wrong object.

`exists()` follows the same priority: check the overlay, consult the base only on a miss, and return `false` for erased entries.

`succ()` is more involved. It finds the next key after a given one by running parallel searches on both the base and the overlay. The base successor is stepped forward, skipping any base keys that appear in `items_` with `Action::erase`. The overlay is scanned forward from `upper_bound(key)` skipping erased entries. The method then returns the lower of the two candidates. This merging logic ensures the successor function reflects the fully overlaid state.

## Merged Iteration via `sles_iter_impl`

Iteration over all SLEs requires merging the base view's sorted SLE sequence with the overlay's sorted `items_` map. The private `sles_iter_impl` class (defined entirely in the `.cpp`) implements `ReadView::sles_type::iter_base` using a two-pointer merge. It holds `(iter0, end0)` into the base SLE sequence and `(iter1, end1)` into `items_`. On `dereference()` it returns whichever current SLE has the smaller key, with the overlay winning ties. On `increment()` it advances the pointer whose current key was just yielded; when both point to the same key, both advance together.

A `skip()` helper handles erased entries: if `iter1` points to an `Action::erase` record whose key matches `sle0_`'s key, both pointers advance in tandem and the entry is suppressed from the output. This correctly handles the case where a base entry has been locally deleted. The `slesBegin()`, `slesEnd()`, and `slesUpperBound()` factory methods on `RawStateTable` construct `sles_iter_impl` instances with the appropriate start positions.

## Fee Destruction

`destroyXRP()` accumulates drops into `dropsDestroyed_` rather than recording a map entry. This tracks the total XRP burned by fees during the buffered transaction set. On `apply()`, this accumulated amount is passed to `to.rawDestroyXRP()` as a single call before the per-entry loop, keeping fee accounting separate from state-entry accounting.

## Relationship to `OpenView` and `ApplyStateTable`

`OpenView` embeds a `RawStateTable items_` directly. Its `rawErase`, `rawInsert`, `rawReplace`, and `rawDestroyXRP` method overrides delegate straight into the table. When `OpenView::apply()` is called to commit to a parent, it calls `items_.apply(to)`, flushing the accumulated operations through the `RawView` interface.

`ApplyStateTable` is a sibling class (also in `xrpl::detail`) that handles a higher-level view of mutations — including a `cache` action for read-only entries and support for transaction metadata threading. `RawStateTable` handles only the raw, unconditional layer; it knows nothing about metadata or caching, which keeps it lean and focused.