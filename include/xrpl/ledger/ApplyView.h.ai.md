# `include/xrpl/ledger/ApplyView.h`

## Role in the System

`ApplyView` is the central abstraction for applying a transaction to an XRPL ledger. It extends `ReadView` (the read-only layer) by adding mutating operations, directory management, and a hook mechanism that powers the payment sandbox. Everything that modifies ledger state during transaction processing goes through this interface — offer placement, trust line updates, account creation, fee destruction — all routed through `ApplyView` so changes can be journaled, committed, or discarded atomically.

The class sits at the intersection of three design concerns: mutation, observability (hooks), and protocol-level directory structure. Its header bundles all three, plus the `ApplyFlags` bitmask that configures how each transaction is processed.

---

## `ApplyFlags` — Transaction Processing Context

The `ApplyFlags` enum is a bitmask carried through every transaction application call, shaping the engine's behavior:

- `tapFAIL_HARD` (0x10): the transaction came from a local node with `fail_hard` set. The engine should not retry and should produce a hard failure that claims fees.
- `tapRETRY` (0x20): this is not the final pass; soft failures (insufficient balance, sequence mismatch) are allowed.
- `tapUNLIMITED` (0x400): from a trusted source with elevated privileges; certain limits are relaxed.
- `tapBATCH` (0x800): the transaction is part of a batch transaction.
- `tapDRY_RUN` (0x1000): simulate the transaction only — don't apply changes and skip signature checks.

The bitwise operators (`|`, `&`, `~`, `|=`, `&=`) are all defined as `constexpr` using `safe_cast` from `<xrpl/basics/safe_cast.h>`, which prevents implicit conversion to the underlying integer type. The module verifies both commutativity and correctness with `static_assert` at compile time, which is a useful guard against future flag value collisions.

---

## The `ApplyView` Class: Checkout-Modify-Commit Pattern

`ApplyView` inherits from `ReadView`, which provides read-only access to ledger state, fees, rules, and the transaction map. `ApplyView` adds the write protocol:

**`peek(k)`** — "checks out" a `SLE` (Serialized Ledger Entry) for modification. The caller receives an owning `shared_ptr<SLE>` that may be freely mutated in place. This is distinct from `read()`, which returns `shared_ptr<SLE const>`.

**`update(sle)`** — "checks in" the modified SLE, signaling to the underlying implementation that the entry has changed and the delta must be journaled.

**`insert(sle)`** — introduces a new entry not obtained from `peek()`.

**`erase(sle)`** — removes an entry previously obtained from `peek()`.

The invariant documented in the header is strict: `update` and `erase` may only be called with an SLE obtained from `peek()` on **the same view instance**. Passing an SLE across view boundaries is undefined behavior, because each view journals its own deltas.

This pattern exists to support the layered view architecture: `ApplyViewImpl` wraps an `OpenView`, and `PaymentSandbox` can wrap another `ApplyView`. Each layer journals its own changes, and calling `apply()` flushes those changes to the parent. Discarding the view discards all changes without touching the parent — enabling transactional rollback at every level.

---

## Payment Hooks — The `creditHook` and `adjustOwnerCountHook` Family

`ApplyView` declares four virtual "hooks" that default to no-ops:

```cpp
virtual void creditHookIOU(...);
virtual void creditHookMPT(...);
virtual void issuerSelfDebitHookMPT(...);
virtual void adjustOwnerCountHook(...);
```

These exist exclusively to support `PaymentSandbox`. The XRPL payment engine must prevent accounts from spending assets they received within the same payment path — otherwise a circular path could manufacture liquidity. `PaymentSandbox` overrides all four hooks to record credits and owner-count changes in a `DeferredCredits` table, and it overrides `balanceHookIOU` / `balanceHookMPT` on the read side to subtract those deferred credits from reported balances.

The default no-op implementations carry `XRPL_ASSERT` checks that verify the `STAmount` holds the expected asset type, providing a cheap sanity guard when hooks are not active.

The `issuerSelfDebitHookMPT` hook handles a more subtle MPT (Multi-Purpose Token) edge case: when an issuer holds an offer that sells MPT, executing that offer in reverse (as the payment engine does) would temporarily inflate the `OutstandingAmount` beyond `MaximumAmount`. The hook lets `PaymentSandbox` track the issuer's "self-debits" so that the net available-to-issue calculation remains correct across the entire payment.

---

## Directory Management

The XRPL ledger represents ordered collections — offer books, account-owned objects — as **paged linked-list directories** stored as `ltDIR_NODE` ledger entries. Each page holds up to `dirNodeMaxEntries` `uint256` keys. Pages are linked via `sfIndexNext` / `sfIndexPrevious` fields; page 0 is always the root and serves as the directory anchor.

`ApplyView` provides the public interface for managing these directories. All variants delegate to the private `dirAdd(preserveOrder, ...)`:

- **`dirAppend`** — enforces insertion order (append-to-tail). This is used only for offer book directories (`ltOFFER`), where chronological ordering affects priority during offer matching. The implementation guards against non-offer keys with `UNREACHABLE`, making misuse a compile-detectable logic error.

- **`dirInsert`** — inserts in sorted order within each page. Used for account-owned object directories. Pages may be legacy unsorted pages, so `insertKey()` re-sorts on every touch.

Both return the 0-indexed page number where the entry was stored, or `std::nullopt` if the page counter overflowed. Overflow detection uses deliberate unsigned wraparound arithmetic (`++page; if (page == 0)`) verified by a `static_assert` against signed-integer UB.

- **`dirRemove`** — removes a single key, collapses now-empty non-root pages, and optionally preserves the root even if it becomes empty (`keepRoot = true`). It also contains legacy handling for older ledger data where empty trailing pages could be left behind.

- **`dirDelete`** — bulk-removes all pages of a directory, invoking a callback for each key. Used when an entire account's offer set or similar collection must be cleaned up.

- **`emptyDirDelete`** — removes the root only if the directory is truly empty; contains the same legacy empty-trailing-page cleanup.

---

## The `xrpl::directory` Namespace

The implementation in `ApplyView.cpp` factors out four low-level helpers into `xrpl::directory`:

- `createRoot` — allocates the root page and inserts the first key
- `findPreviousPage` — walks the back-pointer to locate the last-used page
- `insertKey` — appends or binary-inserts a key into a page's `sfIndexes` vector
- `insertPage` — allocates a new trailing page and links it into the chain

These functions are exposed in the header with an explicit warning: *"Don't use them unless you really, really know what you're doing."* They are declared in the header because some specialized callers (tests, tooling) need access to individual steps, but the intent is that transaction processing always goes through `dirAppend` / `dirInsert` / `dirRemove`.

---

## Relationship to Sibling Types

`ApplyViewImpl` is the concrete production implementation: it wraps a `ReadView const*`, stores its own delta map, and provides the `apply()` method that writes the journaled changes to an `OpenView`. `PaymentSandbox` is the other concrete implementation, layerable on top of any `ApplyView` to add the deferred-credit tracking needed for multi-hop payments. Both classes inherit through `detail::ApplyViewBase`, which provides the delta journaling. Neither is exposed directly to transaction processors — they always receive an `ApplyView&`, allowing the same transaction code to run correctly whether or not a payment sandbox is active.