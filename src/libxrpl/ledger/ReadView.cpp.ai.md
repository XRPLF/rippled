# `src/libxrpl/ledger/ReadView.cpp`

## Role in the System

This file provides the concrete implementations for two concerns that live at the boundary of the `ReadView` abstraction: the range-protocol adapters that let callers iterate over ledger state entries and transactions using standard C++ range syntax, and the `makeRulesGivenLedger` factory functions that bootstrap a `Rules` object from the live state of the ledger. The file is deliberately thin — the real iteration mechanics and storage are pushed into the virtual interface — making this file purely about wiring.

## Range Adapters: `sles_type` and `txs_type`

`ReadView` exposes two public member variables, `sles` and `txs`, that provide range access over serialized ledger entries (SLEs) and transactions respectively. Both are nested `struct` types that inherit from `detail::ReadViewFwdRange<T>`, a template that supplies a type-erased forward iterator wrapping a `std::unique_ptr<iter_base>`. The range itself holds only a raw `ReadView const*` pointer.

The implementations here do nothing more than forward each range operation to corresponding virtual methods on the owning view:

- `sles_type::begin()` → `view_->slesBegin()`
- `sles_type::end()` → `view_->slesEnd()`
- `sles_type::upper_bound(key)` → `view_->slesUpperBound(key)`
- `txs_type::begin()` / `end()` → `view_->txsBegin()` / `view_->txsEnd()`
- `txs_type::empty()` → `begin() == end()` (a convenience shortcut)

This delegation pattern is what makes `ReadView` a true interface: concrete subclasses (`Ledger`, `OpenView`, `CachedView`, etc.) override the `slesBegin` family of methods to return their storage-specific iterators, while all callers interact uniformly through the `sles` and `txs` ranges.

One subtle copy-safety property follows from how `ReadView` initializes these members. Looking at the header, all three `ReadView` constructors — default, copy, and move — initialize `sles(*this)` and `txs(*this)`. This means the range objects always point to their *containing* `ReadView` instance, not the source of a copy or move. Without this rebinding, copying a `ReadView` subclass would leave the `sles` and `txs` ranges dangling or pointing into the wrong object.

## `makeRulesGivenLedger`: Bootstrapping Amendment Rules from Ledger State

`Rules` governs which XRPL amendments (protocol features) are active during transaction processing. It is cheap to pass by value (backed by a `shared_ptr<Impl const>`) but must accurately reflect the set of enabled amendments stored on the ledger. `makeRulesGivenLedger` is the sole authorized path to construct a `Rules` with a live amendment set — the three-argument `Rules` constructor that accepts a digest and `STVector256` of amendment hashes is private, with `makeRulesGivenLedger` declared as a `friend` in `Rules`. This controlled construction pattern prevents callers from accidentally constructing a `Rules` object that diverges from actual ledger state.

The function requires a `DigestAwareReadView` rather than the base `ReadView`. This is significant: `DigestAwareReadView` adds a `digest(key)` method that returns the hash of the SLE at that key without necessarily deserializing it. The implementation uses this in a deliberate two-step:

```cpp
std::optional const digest = ledger.digest(k.key);
if (digest)
{
    auto const sle = ledger.read(k);
    if (sle)
        return Rules(presets, digest, sle->getFieldV256(sfAmendments));
}
return Rules(presets);
```

The outer guard on `digest` is not just defensive null-checking — it carries caching semantics. The `Rules` implementation stores the digest internally so that callers can detect whether rules have changed between ledger closes without re-reading the full SLE. If the digest matches what was seen before, the `Rules` object is still valid. The inner guard on the SLE handles the genesis-ledger case: the amendments object (`ltAMENDMENTS`, addressed via `keylet::amendments()`) simply doesn't exist on the very first ledger, so a plain `Rules(presets)` — with no active amendments — is returned as the correct baseline.

The two-overload design is a convenience split. The first overload accepts a `Rules const& current` (used by `Ledger.cpp` when refreshing its own rules on each new ledger build) and simply delegates to the second after extracting `current.presets()`. The second overload takes an `unordered_set<uint256>` of preset amendments directly — used by `NetworkOPs` and `RCLConsensus` at consensus time, passing `app_.config().features` as the forced-enabled set that operators configure locally.

## Failure Modes and Defensive Patterns

Neither `makeRulesGivenLedger` overload throws. Both double-check the `std::optional` results before use. If the amendments ledger object is absent (no entry at the amendments keylet, or a ledger with no amendments history), the function silently returns a `Rules` constructed only from presets — an intentional graceful degradation rather than an error, because pre-amendment ledgers are a valid and common case during chain replay.

There are no raw pointers owned by this file. The `sles_type` and `txs_type` range types store a `ReadView const*` borrowed from the enclosing object, and the lifetime of that pointer is guaranteed by the `ReadView` member layout — the ranges are destroyed before the view that owns them.