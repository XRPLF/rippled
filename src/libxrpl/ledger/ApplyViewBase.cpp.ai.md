# `src/libxrpl/ledger/ApplyViewBase.cpp`

## Role and Purpose

`ApplyViewBase` is the foundational implementation class for mutable ledger views in XRPL. It lives in `namespace xrpl::detail`, signaling that it is an infrastructure component rather than a public interface. The class is the shared concrete base for two important consumers: `ApplyViewImpl` (the per-transaction apply context used by the transaction engine) and `Sandbox` (a discardable scratch-pad view used for exploratory state changes).

The problem it solves is staging ledger mutations. XRPL processes each transaction against a copy of the current ledger state rather than the ledger itself — changes are buffered, then either committed atomically or discarded depending on whether the transaction succeeds. `ApplyViewBase` provides that buffer, together with the full read interface over the combined "base + pending changes" state.

## Class Hierarchy and Interface Obligations

`ApplyViewBase` inherits from two abstract interfaces: `ApplyView` and `RawView`. `ApplyView` extends `ReadView` with validated, "checkout" style mutation methods (`peek`, `insert`, `update`, `erase`) and a `flags()` accessor that carries per-transaction policy flags. `RawView` provides a lower-level mutation surface (`rawInsert`, `rawErase`, `rawReplace`, `rawDestroyXRP`) used when building transaction metadata and committing state into an `OpenView`. `ApplyViewBase` implements every virtual method in both hierarchies, leaving subclasses only to add lifecycle logic (`apply()` in `ApplyViewImpl`, `apply(RawView&)` in `Sandbox`).

## Two-Member Design: `base_` and `items_`

The class holds exactly two non-trivial members, and the architecture is built around their division of responsibility:

`ReadView const* base_` is the immutable snapshot of the ledger at the start of transaction processing. Every read-only query that doesn't need awareness of pending changes — `open()`, `header()`, `fees()`, `rules()`, and all transaction-map iterators — is forwarded directly to `base_`. The SLE iterators (`slesBegin`, `slesEnd`, `slesUpperBound`) are also delegated straight to `base_` rather than going through the change buffer. This is a deliberate design choice: the apply phase does not need to iterate over its own pending writes, and bypassing the buffer keeps iteration consistent with the ledger snapshot.

`detail::ApplyStateTable items_` is the mutable change buffer. It maintains an internal `std::map` of `uint256 → (Action, SLE)` entries, where `Action` is one of `cache`, `erase`, `insert`, or `modify`. Every mutation routes through `items_`, which records the intended change without touching `base_`. Read operations that are change-aware — `exists()`, `succ()`, `read()`, and `peek()` — pass `*base_` to `items_` so the table can merge pending changes with the base state before answering.

## Validated vs. Raw Mutation

The implementation exposes two tiers of mutation, and their distinction matters:

The *validated* tier (`insert`, `erase`, `update`) goes through the consistency-checking paths in `ApplyStateTable`. For example, `erase` asserts that the SLE was previously obtained via `peek()` on this very view, enforcing the API contract that prohibits sharing SLEs across view instances. `insert` requires that the key not already exist.

The *raw* tier (`rawErase`, `rawInsert`, `rawReplace`, `rawDestroyXRP`) is lower-level and skips those invariant checks. It exists to serve the `RawView` interface, which is the mechanism through which a finalized `ApplyStateTable` commits its changes upstream — for instance, when `ApplyViewImpl::apply()` pumps the buffered changes into an `OpenView`. Notably, `rawInsert` calls the same `items_.insert()` as the high-level `insert()`, but `rawErase` routes to the distinct `items_.rawErase()` that bypasses the ownership check.

## The `peek()` / `update()` / `erase()` Contract

`peek()` returns a mutable `shared_ptr<SLE>` checked out from the table. The caller may modify the SLE in place but must then signal the view via `update()` to mark the entry as modified, or `erase()` to mark it for deletion. Passing the peeked SLE to any other `ApplyView` instance violates the invariant enforced at the `ApplyStateTable` layer. `read()`, by contrast, returns a `shared_ptr<SLE const>` that carries no ownership obligation and is safe to inspect without any follow-up call.

## `flags_` and Transaction Policy

The `ApplyFlags` bitmask stored in `flags_` is set at construction and exposed read-only via `flags()`. Consumers use it to distinguish scenarios like retry mode (`tapRETRY`), privileged transaction sources (`tapUNLIMITED`), batch context (`tapBATCH`), or dry-run simulation (`tapDRY_RUN`). The base class merely carries and vends this value; policy logic is the concern of the callers.

## Non-Copyability

The class deletes the copy constructor and both assignment operators, permitting only move construction. This is appropriate because `ApplyStateTable` holds shared ownership of `SLE` objects and a raw `ReadView*` back-pointer: allowing copies would create aliasing hazards where two views simultaneously believe they own the same pending state.