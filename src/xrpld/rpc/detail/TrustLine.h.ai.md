# `TrustLine.h` — Trust Line Wrapper for Path Finding and RPC

This header defines the data abstraction layer between raw trust line ledger entries (`ltRIPPLE_STATE` SLEs) and the two major consumers that need to read trust line data: the path finder and the `account_lines` RPC handler. It exists to solve a fundamental asymmetry in how XRPL stores trust lines and to give each consumer a type that carries exactly the fields it needs.

## The Low/High Asymmetry Problem

Every trust line in the XRPL ledger is stored as a single `ltRIPPLE_STATE` SLE shared between two accounts. The ledger canonicalizes the two sides as "low" and "high" based on the lexicographic ordering of the `AccountID` values, and stores the balance from the low account's perspective (positive means the high side owes the low side). Per-side metadata — limits, authorization flags, freeze flags, no-ripple flags — is stored in separate `sfLowLimit`/`sfHighLimit` and `lsfLow*`/`lsfHigh*` fields within the same object.

`TrustLineBase` normalizes this away. Its constructor accepts the `viewAccount` parameter and sets `mViewLowest` to indicate whether the caller is the low or high participant on the line. Every getter then dispatches on this flag:

```cpp
bool getNoRipple() const {
    return mFlags & (mViewLowest ? lsfLowNoRipple : lsfHighNoRipple);
}
```

The constructor also negates `mBalance` when the view account is the high side, so `getBalance()` always returns a value with the correct sign relative to the caller. This normalization is the sole purpose of the class — once constructed, callers never need to think about which "side" of the SLE they're reading.

## Memory Constraints and the `PathFindTrustLine` Split

The class comment is explicit: the path finder can easily create tens of millions of `TrustLineBase` instances. This pressure drove the decision to split the class into two derived types rather than carrying all fields in one.

`PathFindTrustLine` inherits only from `TrustLineBase` and adds no data members beyond those in the base. It omits the quality-in/quality-out transfer fee rates because the path finder does not need them during path enumeration — fees are handled at a later stage. Keeping these four `Rate` fields out of `PathFindTrustLine` measurably reduces the per-instance footprint when tens of millions of instances are live in memory.

`RPCTrustLine` extends `TrustLineBase` with `lowQualityIn_`, `lowQualityOut_`, `highQualityIn_`, `highQualityOut_`, reading them from `sfLowQualityIn` etc. in the SLE. The `getQualityIn()`/`getQualityOut()` accessors again use `mViewLowest` to surface the caller's side. This variant is used exclusively by the `account_lines` handler, which serializes all four quality values into the JSON response.

## `LineDirection` and Path Traversal Filtering

```cpp
enum class LineDirection : bool { incoming = false, outgoing = true };
```

This `bool`-backed enum classifies an account's role in a payment path. An "outgoing" account (no-ripple flag off) can be a transit hop; an "incoming" account (no-ripple flag on) is a dead end for further rippling. `TrustLineBase::getDirection()` and `getDirectionPeer()` derive direction directly from the no-ripple flags.

`PathFindTrustLine::getItems()` takes a `LineDirection` argument and forwards it to the shared template `getTrustLineItems()`. When direction is `outgoing`, all trust lines are returned. When `incoming`, the helper filters out any line where `getNoRipple()` is true — those lines cannot carry value further along the path and would only waste path-finding cycles. `RPCTrustLine::getItems()` omits the direction parameter entirely because `account_lines` always wants all trust lines regardless.

## Factory Pattern and Generic Iteration

Both subclasses expose a static `makeItem(accountID, sle)` factory that returns `std::optional<T>`, yielding empty when the SLE is null or not of type `ltRIPPLE_STATE`. The shared implementation template `getTrustLineItems<T>()` uses `forEachItem()` to walk the account's owner directory — which can contain offers, escrows, and other SLE types — and calls `T::makeItem()` on each entry. Non-trust-line entries silently produce `std::nullopt` and are skipped. This avoids any need for the template to know which SLE types it will encounter.

After building the vector, `shrink_to_fit()` is called explicitly because these vectors may persist for a long time inside `AssetCache`, and trimming excess capacity across millions of instances has meaningful impact.

## Copy and Move Semantics

`TrustLineBase` deletes copy assignment but allows copy construction and move construction. The `const` members `mLowLimit` and `mHighLimit` would make assignment ill-formed regardless, but the explicit `= delete` makes the design intent clear. The derived classes inherit these semantics and add no constructors of their own beyond `PathFindTrustLine`'s inherited constructor and `RPCTrustLine`'s explicit two-argument constructor.

## `CountedObject` Tracking

Both `PathFindTrustLine` and `RPCTrustLine` inherit from `CountedObject<T>`, a lock-free reference-count mixin. This increments a global atomic counter per type on construction and decrements it on destruction, enabling live instance counts to be queried at runtime. Given the path finder's potential to create millions of instances, this is a practical diagnostic: if memory pressure spikes, the counter reveals exactly how many `PathFindTrustLine` objects are alive.