# `ApplyStateTable` — Transaction-Scoped Ledger State Buffer

`ApplyStateTable` is the write-ahead buffer that sits between a single transaction's processing context and the underlying ledger state. It lives in the `xrpl::detail` namespace because it is an implementation detail of `ApplyViewBase`, not intended to be used directly by transaction handlers. Its two primary responsibilities are (1) accumulating state mutations in a way that can be committed or discarded atomically, and (2) producing the `TxMeta` object that XRPL embeds into every closed-ledger transaction — the machine-readable record of exactly what changed.

## Core Data Model

The central data structure is `items_`, a `std::map<key_type, std::pair<Action, std::shared_ptr<SLE>>>`. Every ledger entry (`SLE`) that is touched during transaction processing gets an entry in this map tagged with one of four `Action` values:

- **`cache`** — the SLE was read from the base view and is held as a mutable copy, but has not yet been modified. This is the "read-intent" state produced by `peek()`.
- **`modify`** — the SLE was pre-existing and has been changed.
- **`insert`** — the SLE is newly created and does not exist in the base view.
- **`erase`** — the SLE should be deleted when changes are committed.

The `cache` action is what distinguishes `ApplyStateTable` from its simpler sibling `RawStateTable` (used internally by `OpenView`). `RawStateTable` only has `erase`, `insert`, and `replace` — there is no concept of loading a mutable copy. `ApplyStateTable` adds `cache` to support the `peek()`/`update()` pattern expected by `ApplyView` clients: call `peek()` to get a writable handle, mutate it, then call `update()` to promote it from `cache` to `modify`.

## State Transition Invariants

The action tags obey carefully enforced invariants. Several state transitions are prohibited and trigger `LogicError`:

- Double-erasing the same key.
- Calling `update()` on an SLE that was not previously loaded via `peek()` (i.e., the exact same `shared_ptr` must be present in the map).
- Inserting a key that is already `cache`d or in `modify` state.

Some transitions intentionally collapse to their logical equivalent. If an entry is `insert`ed and then `erase`d before commit, `erase()` removes it from `items_` entirely — the net effect on the ledger is zero, and no commit overhead is incurred. Conversely, `rawErase` followed by `insert` (which is the sub-transaction nested view pattern) merges into a `modify` action because the underlying object existed, was deleted, and then a new version was created in the same batch.

The `erase()` and `rawErase()` methods differ in precondition. `erase()` requires that the SLE was previously obtained via `peek()` — it verifies pointer identity (`item.second != sle` triggers `LogicError`). `rawErase()` is more permissive: it can create a new `erase` entry for an SLE that was never `peek()`d, which is needed when sub-views apply their changes upward via the `RawView` interface.

## Commit Paths

There are two overloads of `apply()`, each serving a different commit scenario.

**`apply(RawView& to)`** is the simple path. It calls `to.rawDestroyXRP(dropsDestroyed_)` first, then walks `items_` and dispatches each entry to `to.rawErase()`, `to.rawInsert()`, or `to.rawReplace()`. Cache-only entries are skipped. This path is used when a nested `ApplyViewBase` (e.g., a sandboxed sub-transaction view) commits its changes up to the parent view.

**`apply(OpenView& to, STTx const& tx, TER ter, ...)`** is the full metadata-building path, triggered when a transaction is applied to a closing ledger (or during a dry run). It constructs a `TxMeta` object and iterates `items_`, categorizing each change as `sfCreatedNode`, `sfModifiedNode`, or `sfDeletedNode`. For each, it computes `sfPreviousFields`, `sfFinalFields`, or `sfNewFields` by comparing the original entry from the base view against the buffered version — but only for fields whose `SField` metadata flags (`sMD_ChangeOrig`, `sMD_Always`, `sMD_DeleteFinal`, `sMD_Create`, `sMD_ChangeNew`) indicate they should appear in the metadata. An optimization skips `modify` entries where the before and after states are byte-equal.

The `isDryRun` flag separates metadata generation from actual commitment. When `isDryRun` is true, the full `TxMeta` is built and returned, but the call to `apply(to)` and `to.rawTxInsert(...)` are skipped. This allows callers to simulate transaction effects and inspect metadata without mutating the ledger.

## Transaction Threading

The private `threadItem()`, `threadTx()`, and `threadOwners()` methods implement XRPL's account threading mechanism. Each `AccountRoot` SLE carries `PreviousTxnID` and `PreviousTxnLgrSeq` fields that form a reverse-linked list through every transaction that touched that account. When a transaction modifies or creates a ledger entry, `threadOwners()` determines which accounts should be threaded based on the entry type:

- `ltACCOUNT_ROOT` entries thread themselves (handled by `threadItem` when the SLE is of threaded type).
- `ltRIPPLE_STATE` trust line entries thread to both the low and high limit account.
- All other entry types thread to the `sfAccount` field if present, and to `sfDestination` if present.

`getForMod()` supports threading by fetching a mutable SLE for accounts that need their threading fields updated but were not otherwise part of the transaction's primary changes. It checks a local `Mods` accumulator first, then `items_`, then falls back to copying from the base view. These incidental modifications are tracked separately and flushed to the view via `rawReplace()` after all metadata is assembled — but only when not in dry-run mode.

## Ownership and Lifecycle

`ApplyStateTable` is move-constructible but copy-construction and all assignment operators are deleted. The `shared_ptr<SLE>` instances returned by `peek()` are owned jointly by the caller and the table: `update()` verifies pointer identity, not value equality, to prevent callers from substituting a different SLE object for the same key. This ensures the mutable SLE handed out by `peek()` is the authoritative version that will be committed.

The `dropsDestroyed_` counter is separate from the `items_` map. Fee destruction is accumulated via `destroyXRP()` and applied unconditionally as the first step of any `apply()` call, regardless of the other mutations.

`ApplyStateTable` is used exclusively inside `ApplyViewBase`, which is the foundation of all per-transaction apply views in the codebase. It is the mechanism that makes XRPL transaction application both atomic and auditable.