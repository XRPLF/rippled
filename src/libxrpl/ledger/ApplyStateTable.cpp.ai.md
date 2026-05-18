# `ApplyStateTable` — Transaction Ledger Write Buffer

`ApplyStateTable` is the write-staging layer that every XRPL transaction application relies on. It sits inside `xrpl::detail` and is the core member of `ApplyViewBase`, which in turn backs all the `ApplyView` and `ApplyViewImpl` types that transactors receive at execution time. Its purpose is to accumulate all ledger mutations a transaction wishes to make — without touching the real ledger — and then either commit them atomically or discard them if the transaction fails.

## The Central Data Structure

The entire class revolves around one map:

```cpp
using items_t = std::map<key_type, std::pair<Action, std::shared_ptr<SLE>>>;
items_t items_;
```

Every ledger object (`SLE`) that the current transaction touches is represented here, keyed by its `uint256` ledger key. The `Action` enum (private to the class) records the fate of each entry:

- `cache` — read from the base ledger and available for writes, but not yet marked dirty
- `insert` — a new object to be added
- `modify` — an existing object that has been mutated
- `erase` — an object to be deleted

This four-state design lets the system distinguish a clean read (`cache`) from an actual write intent (`modify`). Callers receive a mutable `shared_ptr<SLE>` from `peek()`, which enters the map as `cache`. Only when `update()` is called on it does the action upgrade to `modify`, ensuring no spurious write metadata is generated for objects that were merely inspected. If a key is erased and then re-inserted within the same transaction, `insert()` correctly detects the prior `erase` action and collapses the transition into a `modify` — the net effect for the base ledger is a replacement.

A second field, `dropsDestroyed_`, tracks XRP taken permanently out of circulation by transaction fees within this transaction's scope.

## Two Flavors of `apply()`

The class has two `apply()` overloads with very different roles.

`apply(RawView& to)` is the simple flush. It iterates `items_` and maps each action to a raw write: `rawErase`, `rawInsert`, or `rawReplace`. Cached-only entries are skipped. This is used when applying a sandbox or a nested view back to its parent.

`apply(OpenView& to, STTx const& tx, TER ter, ...)` is the full transaction-commit path. It does everything the raw version does but first generates `TxMeta` — the transaction metadata that gets stored on-ledger and tells downstream clients exactly what changed. The condition `!to.open() || isDryRun` gates metadata generation: metadata is always produced for closed ledgers (where transactions are final), and also when `isDryRun` is true. In dry-run mode the metadata is produced but the state changes themselves are suppressed — this supports pre-flight validation and fee calculation without side effects.

## Metadata Construction

The metadata generation in the `apply(OpenView&...)` overload is the most complex part of the file. For each pending item, it classifies the change as `sfDeletedNode`, `sfCreatedNode`, or `sfModifiedNode` and populates the appropriate metadata fields using `SField` metadata flags baked into the XRPL protocol schema:

- **Deleted nodes** capture `sfPreviousFields` (any field from the original that differs from the final state, controlled by `sMD_ChangeOrig`) and `sfFinalFields` (always-included fields plus delete-final fields, via `sMD_Always | sMD_DeleteFinal`).
- **Modified nodes** capture `sfPreviousFields` the same way and `sfFinalFields` with `sMD_Always | sMD_ChangeNew` to record both the stable identity fields and the newly changed values.
- **Created nodes** capture only `sfNewFields` — all non-default values that carry `sMD_Create | sMD_Always`.

A subtle optimization prevents spurious `sfModifiedNode` entries: `if ((type == &sfModifiedNode) && (*curNode == *origNode)) continue;` — if the buffer holds a modify action but the content is byte-for-byte identical to the original, the node is silently omitted from metadata. This can happen when a transaction reads and re-writes a field with the same value.

After the loop, any `Mods` entries accumulated by threading are written back via `rawReplace` (unless it is a dry run).

## The Threading System

XRPL ledger objects maintain a "thread" — a singly-linked history of the last transaction that touched each account root. The threading helpers implement this.

`threadItem(TxMeta&, SLE&)` calls `sle->thread(txID, lgrSeq, prevTxID, prevLgrID)` which updates the SLE's `sfPreviousTxnID` / `sfPreviousTxnLgrSeq` fields in place. If there was a previous transaction, it adds those old fields to the metadata's `sfPreviousTxnID` / `sfPreviousTxnLgrSeq` entries on the affected node — so the chain of transactions is visible in metadata.

`threadOwners()` figures out which accounts need threading for a given node type:

- `ltACCOUNT_ROOT` objects thread only to themselves (handled by the caller).
- `ltRIPPLE_STATE` (trust lines) threads to both the low-limit and high-limit account — the two parties to the trust line.
- Everything else threads to `sfAccount` if present, and to `sfDestination` if present.

`getForMod()` is the helper that retrieves an SLE for threading modification. It checks the local `Mods` map first (objects already being modified by threading in this same pass), then checks `items_` (objects being modified by the transaction itself), and finally falls back to reading from the base view. Objects found only in `items_` as `cache` (not actually written) are placed in `Mods` because their only modification is the threading metadata — they shouldn't be promoted to `Action::modify` in the primary items table. The function gracefully returns `nullptr` when threading to a deleted or nonexistent account, which is legal (e.g., the destination of an expired Escrow or PayChannel may have been deleted).

## Snapshot Semantics and Invariant Enforcement

The `erase()` and `update()` methods both perform pointer-identity checks (`item.second != sle`): they require the exact same `shared_ptr` that was handed out by `peek()`. This enforces a strict ownership protocol — a transactor cannot erase an SLE it didn't explicitly peek, preventing accidental mutation of a stale copy. Both methods call `LogicError` (a hard abort) on invariant violations like double-erase or erasing an unknown pointer, reflecting that these represent programming errors rather than runtime conditions.

`rawErase()` deliberately skips the identity check — it is the unsafe bypass used when the caller provides its own SLE (e.g., during `rawInsert`/`rawErase` operations from `ApplyViewBase`).

## Successor Navigation

`succ()` merges two sorted key spaces: the base ledger's key space and the local `items_` map. It must find the smallest key strictly greater than the given key that actually exists after applying the pending changes. The algorithm first walks the base view's successor, skipping any keys that are pending deletion in `items_`, then independently walks `items_` for non-erased entries greater than the key, and returns whichever result is smaller. This O(log n) merge is necessary because the view must present a consistent ordered sequence of ledger objects to callers such as directory walkers.

## Relationship to `RawStateTable`

`RawStateTable` (used by `PaymentSandbox`) is a leaner cousin with only three actions (erase, insert, replace — no `cache`) and no metadata generation. `ApplyStateTable` is the richer layer specifically designed for the transactor path, where both the state changes and the metadata record of those changes must be produced together.