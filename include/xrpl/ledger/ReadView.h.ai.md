# `include/xrpl/ledger/ReadView.h`

## Role in the System

`ReadView.h` defines the foundational read-only interface for accessing XRPL ledger state. It sits at the base of the entire ledger view hierarchy: every concrete ledger representation — whether a finalized `Ledger`, an in-progress `OpenView`, an apply-time `Sandbox`, or a payment-path `PaymentSandbox` — exposes its state to the rest of the engine through this interface. Nothing that only reads ledger data needs to know which concrete type it's working with, which is the key benefit of the abstraction.

The design also includes `DigestAwareReadView`, a thin extension that adds per-entry cryptographic digests, and two `makeRulesGivenLedger` factory functions that derive the active amendment `Rules` from a ledger object.

## `ReadView`: Pure Abstract Interface

`ReadView` exposes two conceptually distinct maps: the **state map** (SLEs — Serialized Ledger Entries) and the **transaction map** (committed transactions plus their metadata).

### Core Pure-Virtual Contract

The state-map side requires four pure virtual methods:

- `exists(Keylet const& k)`: checks whether a state entry of the given type and key is present. The `Keylet` structure bundles a raw `uint256` key with its `LedgerEntryType`, giving `exists` a chance to reject type mismatches without deserializing. This makes it more efficient than `read()` for pure presence checks.
- `succ(key_type, optional<key_type> last)`: returns the smallest key strictly greater than the argument, optionally bounded by `last`. This enables range scans of the SHAMap without deserializing every entry.
- `read(Keylet const& k)`: returns `std::shared_ptr<SLE const>` — ownership of a non-modifiable SLE — or `nullptr` when the key is absent or when the ledger entry type doesn't match the keylet. The `const` qualifier on the SLE is the caller's view; the underlying object can still be mutated through `ApplyView` in a different code path.

The transaction-map side provides `txExists()` and `txRead()`, which return a `tx_type` pair: `std::pair<std::shared_ptr<STTx const>, std::shared_ptr<STObject const>>`. For open ledgers the metadata `STObject` is empty, since metadata is only finalized at close time.

Convenience methods like `seq()`, `parentCloseTime()` are non-virtual inline wrappers that delegate to `header()`, keeping the virtual surface small.

### Copy and Move Semantics — A Subtle Invariant

`ReadView` holds two public member objects — `sles` and `txs` — of nested types `sles_type` and `txs_type`. Both are subclasses of `detail::ReadViewFwdRange`, which stores a raw pointer to the owning `ReadView`. This creates a well-known C++ trap: if a copy or move constructor defaulted to memberwise initialization, `sles.view_` and `txs.view_` would point at the *source* object, not the newly constructed one.

The header prevents this by explicitly re-initializing both members with `*this` in every constructor:

```cpp
ReadView(ReadView const& other) : sles(*this), txs(*this) {}
ReadView(ReadView&& other)      : sles(*this), txs(*this) {}
```

Both assignment operators are deleted to prevent the same aliasing from arising post-construction. Derived classes must respect this pattern.

### Iterable Ranges via Type Erasure

`sles_type` and `txs_type` expose STL-style `begin()` / `end()` iterators, enabling range-based for loops over all state entries or transactions. The actual iteration is implemented through the abstract `ReadViewFwdIter<ValueType>` class in `detail/ReadViewFwdRange.h`, which virtualizes `copy()`, `equal()`, `increment()`, and `dereference()`. Concrete views implement the virtual factories `slesBegin()`, `slesEnd()`, `slesUpperBound()`, `txsBegin()`, and `txsEnd()` to return heap-allocated `unique_ptr<iter_base>` objects — one allocation per iterator, not per element.

The `iterator` class in `ReadViewFwdRange` wraps that pointer and adds a mutable `optional<value_type> cache_` to materialize the dereferenced value on demand. This type-erasure approach lets `sles_type` and `txs_type` present a uniform STL iterator interface regardless of whether the view is backed by a SHAMap, a flat delta list, or a layered sandbox.

The `sles_type` also provides `upper_bound(key)`, which maps directly to `slesUpperBound()` — useful for iterating a sub-range of state entries without paying the cost of a full scan.

### Balance and Owner-Count Hooks

`ReadView` declares four virtual methods with default pass-through implementations: `balanceHookIOU`, `balanceHookMPT`, `balanceHookSelfIssueMPT`, and `ownerCountHook`. These are extension points, not core query methods.

The XRPL payment engine executes paths in **reverse order** (destination side first). This means an intermediate account may be credited before it has actually redeemed the corresponding asset, temporarily inflating its balance. The rule is that accounts in a payment may not use assets acquired *during* that same payment — each step must see only the pre-payment balance.

`PaymentSandbox` enforces this by overriding these hooks. When `balanceHookIOU` or `balanceHookMPT` is called during a payment, the sandbox subtracts deferred credits recorded in its `DeferredCredits` table. `ownerCountHook` returns the maximum owner count seen so far rather than the current count, preventing reserve-bypass exploits where a payment temporarily frees reserves that are logically still committed. The default implementations in `ReadView` simply return the arguments unchanged, making the hooks zero-cost for views that do not participate in payment processing.

A complementary set of credit/debit hooks lives in `ApplyView` (`creditHookIOU`, `creditHookMPT`, `adjustOwnerCountHook`, `issuerSelfDebitHookMPT`). Those are called on the write path to *record* the adjustments, while the `ReadView` hooks are called on the read path to *apply* them.

## `DigestAwareReadView`

`DigestAwareReadView` adds a single pure virtual method: `digest(key_type const& key) -> optional<uint256>`. This returns the cryptographic hash of a state entry's serialized content, without necessarily deserializing the entry itself. The `Ledger` class, which stores state in a SHAMap, can answer this question cheaply by inspecting the trie node without loading the leaf. Sandboxes and delta views may not implement this concept at all, which is why the capability is a separate subclass rather than part of `ReadView`.

## `makeRulesGivenLedger`

The two free functions construct a `Rules` object from a ledger. Both are `friend` of `Rules` and call its private three-argument constructor. The implementation in `ReadView.cpp` is instructive:

```cpp
Keylet const k = keylet::amendments();
std::optional const digest = ledger.digest(k.key);
if (digest) {
    auto const sle = ledger.read(k);
    if (sle)
        return Rules(presets, digest, sle->getFieldV256(sfAmendments));
}
return Rules(presets);
```

The digest is passed along with the amendment vector so that `Rules` can cache it and quickly detect when the amendments object has not changed between ledger closes, avoiding repeated re-parsing. This is why the function requires `DigestAwareReadView` rather than plain `ReadView`: the optimization depends on the ability to query a state entry's hash directly.

The two overloads differ only in how they obtain the preset set. The first takes a `Rules const& current` and extracts its internal presets (used when updating rules at ledger close), while the second takes the preset set directly (used during initialization). Both paths feed into the same internal factory logic.