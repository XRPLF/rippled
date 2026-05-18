# TrustLine.cpp

This file implements the trust line wrapper hierarchy used by two distinct XRPL subsystems: the payment pathfinder and the `account_lines` RPC command. It solves the fundamental perspective problem inherent in how XRPL stores trust lines and provides typed collections through a shared iteration template.

## The Low/High Perspective Problem

Every trust line in the XRP Ledger is stored as a single `ltRIPPLE_STATE` SLE with two sides — a "low" account and a "high" account — determined by lexicographic ordering of their `AccountID` values. This ledger-centric representation is opaque from any one account's point of view: a caller asking about their own balance needs the sign of that balance flipped if they happen to be the "high" party.

`TrustLineBase` resolves this at construction time. The `mViewLowest` flag is set to `true` when the requesting `viewAccount` matches `mLowLimit.getIssuer()`. If `mViewLowest` is `false` (the caller is the "high" party), the constructor immediately calls `mBalance.negate()`. All subsequent accessors — `getBalance()`, `getLimit()`, `getLimitPeer()`, `getAuth()`, `getNoRipple()`, `getFreeze()`, `getDeepFreeze()`, and their `*Peer` counterparts — use `mViewLowest` to select the correct flag bits or limit field without any further branching. This one-time normalization at construction keeps the rest of the codebase clean.

## The Two Derived Types

`PathFindTrustLine` is the lightweight variant used by the path-finding engine. The header comments explicitly warn that there can be tens of millions of live instances during pathfinding, making memory layout critical. Accordingly, `PathFindTrustLine` adds no fields beyond those in `TrustLineBase`. It inherits constructors directly (`using TrustLineBase::TrustLineBase`) and only exposes the static factory interface.

`RPCTrustLine` extends the base with four quality rate fields: `lowQualityIn_`, `lowQualityOut_`, `highQualityIn_`, `highQualityOut_`. These correspond to the `quality_in` and `quality_out` values surfaced in the `account_lines` JSON response. The `getQualityIn()` and `getQualityOut()` accessors again use `mViewLowest` to select the perspective-correct rate. These fields are not needed by the pathfinder, which is why they are isolated in the heavier `RPCTrustLine` subclass.

Both classes mix in `CountedObject<T>` via CRTP, which tracks live object counts. This is a practical concession to the scale at which `PathFindTrustLine` instances are created — it enables the monitoring infrastructure to report on pathfinder memory pressure.

## Collection Building via the Shared Template

`detail::getTrustLineItems<T>` is a private function template (hidden in the inner `detail` namespace) that both `PathFindTrustLine::getItems` and `RPCTrustLine::getItems` delegate to. It uses `forEachItem` to iterate an account's owner directory, calling `T::makeItem` on every yielded SLE. The owner directory contains every ledger object an account owns — offers, escrows, checks, NFT pages, and more — so `makeItem` must filter aggressively.

Both `makeItem` implementations apply two guards: a null check on the `shared_ptr<SLE const>` and a type check for `ltRIPPLE_STATE`. If either fails, `std::nullopt` is returned and the item is silently skipped. No exception is thrown; the error is contained entirely within the factory. This pattern makes `getTrustLineItems` robust to heterogeneous owner directories without any caller-visible error handling.

After iteration, `shrink_to_fit()` is called on the result vector. This is directly motivated by the pathfinder's usage — the comment notes that the returned list "may be around for a while," so freeing unused capacity from the initial over-allocation is worth the potential reallocation.

## Direction Filtering for the Pathfinder

`PathFindTrustLine::getItems` accepts a `LineDirection` parameter. The filter inside `getTrustLineItems` reads:

```cpp
if (ret && (direction == LineDirection::outgoing || !ret->getNoRipple()))
    items.push_back(std::move(*ret));
```

`LineDirection::outgoing` always includes a trust line (it is the default). `LineDirection::incoming` — used when the pathfinder encounters an account reached via a NoRipple-disabled line — includes only trust lines where the `NoRipple` flag is not set on the viewing account's side. This precisely implements the XRPL pathfinding rule that an incoming account cannot further propagate payments through its own NoRipple-flagged lines.

`RPCTrustLine::getItems` omits the direction parameter entirely; the `account_lines` command always returns all trust lines regardless of rippling configuration.

## Relationship to AccountLines.cpp

`AccountLines.cpp`'s `doAccountLines` handler does not call `RPCTrustLine::getItems` directly. Instead it uses `forEachItemAfter` for paginated iteration and calls `RPCTrustLine::makeItem` individually per matching SLE, then passes each `RPCTrustLine` to `addLine()`, which converts it to the full JSON representation including balance, limits, quality rates, and all flag fields. The `getJson(int)` method on `TrustLineBase` — which outputs only `low_id` and `high_id` — is therefore a minimal debug/diagnostic helper rather than a production JSON serializer.