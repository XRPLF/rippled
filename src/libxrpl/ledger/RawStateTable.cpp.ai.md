# `RawStateTable.cpp` — Staged Ledger Mutation Buffer

`RawStateTable` lives in `xrpl::detail` and serves as the write-buffer that sits between a transaction's in-flight state changes and the actual ledger storage. Every write-capable ledger view (most visibly `OpenView`) holds a `RawStateTable` internally. Rather than mutating the underlying ledger immediately, all inserts, erases, and field replacements accumulate here. Only when `apply()` is called do those mutations fan out to a real `RawView` target.

## Memory Layout

The `items_` map is the central data structure: a `std::map<uint256, sleAction>` keyed by the SLE's hash key, storing both the pending `Action` enum (`erase`, `insert`, `replace`) and a `shared_ptr<SLE>`. Rather than the standard heap allocator, the map uses a Boost PMR `polymorphic_allocator` backed by a `monotonic_buffer_resource` pre-allocated at 256 KB. This was inherited from an older `qalloc` scheme and exists purely for throughput: transaction processing creates and tears down many small map nodes, and bump-pointer allocation from a single arena is far cheaper than per-node `malloc`. The `monotonic_resource_` is stored as a `unique_ptr` so the object is moveable — the header makes `operator=(RawStateTable&&)` deleted but allows move construction, and the copy constructor allocates a fresh 256 KB arena for the destination.

## Action State Machine in `erase`, `insert`, `replace`

The three mutation methods encode an important state machine. Each SLE key can only ever be in one pending state, and the transitions enforce correctness before `apply()` is called rather than deferring errors.

`insert()` on a key that was previously erased in this same transaction batch upgrades the action to `replace` — this handles the "delete then re-create at the same key" pattern. Inserting into a key that already has a pending insert or replace is a `LogicError`, because from the base view's perspective the object already exists.

`erase()` on a key that was previously inserted (but not yet committed) simply removes the entry from `items_` entirely — the net effect is zero, and there is nothing for `apply()` to propagate. Erasing a `replace`d key downgrades it back to `erase`. Double-erasing is a `LogicError`.

`replace()` on a pending-insert entry just updates the stored SLE pointer, preserving the `insert` action — from the base view's perspective the key is still being created. Replacing an erased key is a `LogicError`.

This design catches misuse at the point of the second conflicting operation, not during `apply()`, which makes debugging significantly easier.

## Merging Reads with the Base View

`read()`, `exists()`, and `succ()` all take a `ReadView const& base` parameter and overlay the pending delta. The pattern for `read()` and `exists()` is a simple two-step: check `items_` first; if found and `action == erase`, return null/false; otherwise return from the local entry. If not found locally, delegate to `base`. The `Keylet` type-check (`k.check(*sle)`) guards against type mismatches — a key might match but the SLE's type might not conform to the requested keylet, so this additional filter prevents wrong-type reads.

`succ()` is more involved: it searches both the base view and the local `items_` independently, skips over base results that are locally erased, and returns whichever candidate key is smaller. The loop that advances the base's successor skips each deleted key one at a time, which is safe because deletions in practice are sparse.

## Merged Iteration via `sles_iter_impl`

The nested class `sles_iter_impl` implements the virtual `ReadView::sles_type::iter_base` interface, providing a sorted merged view over the base ledger's SLEs and the pending changes. It maintains two parallel iterator pairs:

- `iter0_` / `sle0_`: current position in the base view's SLE range (already sorted by key).
- `iter1_` / `sle1_`: current position in `items_` (also sorted, since `std::map` iterates in key order). Only non-null when `iter1_->second.sle` is valid.

`dereference()` returns whichever of `sle0_` and `sle1_` has the smaller key — the local entry always wins on a tie, shadowing the base. `increment()` advances the "winning" iterator. On a key tie (local shadows base), both iterators advance simultaneously so the base entry is consumed.

The `skip()` helper handles erasures: after `iter1_` points to an `Action::erase` entry, `skip()` loops, advancing both iterators in tandem until the local iterator no longer masks the base entry or one of them is exhausted. This ensures erased SLEs are invisible to callers iterating the merged view.

The `equal()` implementation uses `dynamic_cast` to ensure both sides are `sles_iter_impl` instances, then asserts that both end-iterators match (a cross-view comparison would be meaningless). The iterator positions are compared as pairs — both `iter0_` and `iter1_` must agree.

## `apply()` and `destroyXRP()`

`apply()` is straightforward: it dispatches `rawDestroyXRP()` first to account for accumulated fee burning (`dropsDestroyed_`), then iterates `items_` dispatching each action to the target `RawView`. The comment "Base invariants are checked by the base during apply()" signals that the target `RawView` (e.g., a ledger's state map) is responsible for enforcing preconditions like "key must not already exist for rawInsert" — `RawStateTable` only enforces the *transition* invariants within its own pending buffer.

`destroyXRP()` simply accumulates drops into `dropsDestroyed_`, which is replayed as a single `rawDestroyXRP` call at apply time rather than one per fee event.

## Relationship to Sibling Components

`ApplyStateTable` (in the same `detail` namespace) is the higher-level sibling used during transaction application. It adds a `cache` action (read-only peeked SLEs) and handles metadata threading. `RawStateTable` is the lower-level primitive used by `OpenView` directly and by `ApplyStateTable`'s own apply path when writing to a target view without metadata. Both share the same conceptual pattern of a delta-on-top-of-base, but `RawStateTable` handles the raw bytes layer while `ApplyStateTable` handles the semantic layer.