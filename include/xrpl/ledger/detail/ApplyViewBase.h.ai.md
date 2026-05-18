# `ApplyViewBase.h` — Buffered Ledger State Foundation for Transaction Application

`ApplyViewBase` is the core abstract-base implementation in the XRPL ledger view hierarchy, living in the `xrpl::detail` namespace to signal that it is internal infrastructure rather than public API. Its purpose is to buffer all ledger state mutations produced during transaction processing — insulating the underlying committed ledger from any partial or failed write — and to present that buffer as a fully-functional `ReadView` to the transaction logic running on top of it.

## Architectural Position

The XRPL ledger view system is layered deliberately. `ReadView` is read-only access to the committed state. `ApplyView` extends it with a peek/insert/update/erase API for transactional mutations. `RawView` provides an unconditional write interface (rawInsert/rawReplace/rawErase/rawDestroyXRP) used when applying a committed sandbox down to a lower layer. `ApplyViewBase` inherits from **both** `ApplyView` and `RawView`, making it the first class in the hierarchy that can function simultaneously as a mutable scratch-pad and as a raw sink for another view above it.

The two raw write interfaces exist for a reason. `ApplyView::insert/update/erase` carry semantic constraints (you must `peek` before `erase`, the key must not pre-exist before `insert`, etc.), and `ApplyStateTable` enforces those invariants with assertions. `RawView::rawInsert/rawReplace/rawErase` bypass those constraints and are intended for use by `Sandbox::apply()` and similar commit-path code that already knows the state is consistent. `ApplyViewBase` satisfies both contracts through the same `items_` buffer.

## The Three Protected Members

All state is held in three protected members:

- `base_` (`ReadView const*`) — a borrowed, non-owning pointer to the underlying committed ledger view. Every read-only query that doesn't involve pending mutations is forwarded here directly.
- `flags_` (`ApplyFlags`) — the set of processing flags for the current transaction (e.g., `tapRETRY`, `tapFAIL_HARD`, `tapUNLIMITED`, `tapDRY_RUN`). These affect how transaction logic interprets failures and are propagated by `flags()`.
- `items_` (`detail::ApplyStateTable`) — the write buffer. Every `insert`, `update`, `erase`, `peek`, `exists`, `succ`, and `read` call is delegated here (along with `*base_` so cache misses fall through to the committed state). `items_` tracks per-key actions as one of `{cache, erase, insert, modify}` and also accumulates the `dropsDestroyed_` counter for fee accounting.

## Read Method Routing — Two Distinct Paths

The `.cpp` implementation reveals an important split. Metadata and iteration methods — `header()`, `fees()`, `rules()`, `open()`, `slesBegin()`, `slesEnd()`, `slesUpperBound()`, `txsBegin()`, `txsEnd()`, `txExists()`, `txRead()` — all pass straight through to `base_`. They never consult `items_` because the pending transaction cannot change the ledger's header, fee schedule, amendment rules, or already-applied transaction set.

State lookup methods — `exists()`, `succ()`, `read()` — all route through `items_`, passing `*base_` as a fallback for cache misses. This means transaction logic transparently sees its own in-flight changes when it queries state, which is essential for correctness: an insert followed by a read of the same key must return the just-inserted value.

The `read()` vs `peek()` distinction is similarly intentional. `read()` returns `shared_ptr<SLE const>` — a snapshot, no mutation tracking, no ownership transfer. `peek()` returns `shared_ptr<SLE>` and notifies `items_` that this key has been checked out for potential modification; the caller must subsequently call `update()` or `erase()`, or the state table will catch the violation.

## `rawDestroyXRP` and Fee Accounting

`rawDestroyXRP()` is not a state mutation in the usual sense — it doesn't modify any `SLE`. Instead it increments a separate `dropsDestroyed_` counter inside `ApplyStateTable`. When the buffer is later applied to a parent `RawView`, `destroyXRP` is replayed first, before any state entries, because the total XRP supply reduction must be recorded before the modified accounts are written.

## Object Lifetime and Move Semantics

Copy construction and all assignment operators are deleted. Move construction is retained (`= default`). The rationale: a view is a unique owner of its in-flight mutation set during a transaction; copying it would duplicate the change journal and risk double-applying mutations. The move constructor allows subclasses to be constructed with move semantics (e.g., returned from factory functions) without enabling accidental duplication.

The constructor `ApplyViewBase(ReadView const* base, ApplyFlags flags)` is the only way to construct the object. `base` must outlive the `ApplyViewBase`; the class makes no attempt to extend its lifetime, relying on the invariant that the underlying ledger view always outlives any transaction-level view built on top of it.

## Concrete Subclasses

Three concrete subclasses build on `ApplyViewBase`:

**`ApplyViewImpl`** is the commit path for a full transaction. It adds a `deliver()` setter for payment amount metadata and an `apply(OpenView&, STTx, TER, ...)` method that delegates to `items_.apply(OpenView&, ...)` — the overload that produces `TxMeta` and threads the transaction through affected account entries. This is the only subclass that generates ledger metadata.

**`Sandbox`** is the discard-or-commit path for speculative or nested mutations. It adds only `apply(RawView& to)`, which calls `items_.apply(to)` — the overload that replays raw writes into any `RawView` target without generating metadata. Transaction logic uses `Sandbox` when it needs to attempt a multi-step operation atomically and roll back on failure.

**`PaymentSandbox`** overrides the hook methods declared in `ApplyView` (`creditHookIOU`, `creditHookMPT`, `issuerSelfDebitHookMPT`, `adjustOwnerCountHook`) to record deferred credit information during payment path processing. This is the only subclass that exploits the hook extension points rather than accepting their default no-op implementations.

The placement of `ApplyViewBase` in `xrpl::detail` is a deliberate encapsulation boundary: transaction processing code works with `ApplyView` or `ApplyViewImpl` references; only the three concrete subclasses themselves, and `ApplyStateTable`, need to reach into this layer.