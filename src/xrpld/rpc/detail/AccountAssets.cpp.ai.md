# `AccountAssets.cpp` — Asset Eligibility for Path-Finding

This file implements two free functions, `accountSourceAssets` and `accountDestAssets`, that classify which assets an account is capable of sending or receiving. Both are utility helpers for the XRPL path-finding subsystem, called directly from `PathRequest.cpp` when constructing or updating a payment path request. Their output — a `hash_set<PathAsset>` — feeds into the `Pathfinder` to narrow the search space before the expensive graph traversal begins.

## Role in the Path-Finding Pipeline

`PathRequest` uses `accountSourceAssets` when the caller has not specified a `SendMax` asset, to enumerate every possible source currency from the sender's holdings. `accountDestAssets` is called to populate the `destination_currencies` field in the path-find response (both the streaming `path_find` API and the legacy `ripple_path_find` API). By computing these sets eagerly from a shared `AssetCache`, the path finder avoids redundant ledger reads and can prune entire asset classes before graph traversal.

The `AssetCache` parameter is a ledger-snapshot cache shared within a single pathfinding session. It holds pre-fetched trust lines keyed by account and `LineDirection`, and a separate map of `PathFindMPT` entries per account. Both functions pass `LineDirection::outgoing`, which returns all trust lines for the queried account rather than filtering out those with rippling disabled — appropriate for the source/destination endpoint itself as opposed to an intermediate hop.

## `accountSourceAssets` — What Can an Account Send?

The function builds the eligible set in three stages:

**XRP**: Optionally inserted unconditionally if `includeXRP` is true. The comment `YYY Only bother if they are above reserve` notes a known limitation — no reserve check is performed. The XRP native currency is included as a `PathAsset` via `xrpCurrency()`.

**IOU trust lines**: Each outgoing `PathFindTrustLine` is evaluated against two conditions that mirror the XRPL's on-ledger credit semantics. A currency is sendable if either the account's balance is positive (it holds issued currency it can push outward) or the account has a negative balance that is less negative than the peer's trust limit (the peer has extended enough credit that there is still room to draw from). The second condition uses `(-saBalance) < getLimitPeer()` — negating the balance to express "amount currently owed" and comparing to the peer's ceiling. This captures the case where an account is a borrower with available headroom.

**MPTs (Multi-Protocol Tokens)**: An MPT issuance is included as a source asset if the account holds a non-zero balance (`!isZeroBalance()`) and the issuance has not been fully saturated (`!isMaxedOut()`). The `maxedOut_` flag reflects whether the issuer's total outstanding amount has reached its configured maximum, which would prevent any further transfers.

After both IOU and MPT passes, `assets.erase(badCurrency())` removes the reserved sentinel currency code, a defensive step that prevents a malformed or zero-currency entry from propagating into path search.

## `accountDestAssets` — What Can an Account Receive?

The structure parallels `accountSourceAssets` but applies the opposite criterion. An IOU currency is receivable if the account's current balance is below its self-imposed trust limit (`saBalance < getLimit()`), meaning there is room to absorb more. Crucially, this check is against the account's own limit, not the peer's, because the destination controls how much of a given IOU it is willing to hold.

The MPT filter is inverted relative to the source check: `isZeroBalance() && !isMaxedOut()`. A zero balance here indicates the account holds none of the MPT yet (fresh capacity), while `!isMaxedOut()` confirms the issuer can still satisfy new transfers. This asymmetry between source and destination is intentional — a source needs tokens on hand to push, while a destination is only a meaningful landing spot when it has no existing holding and the supply ceiling permits issuance.

The comment "Even if account doesn't exist" on the XRP insertion reflects a deliberate policy: XRP is always a valid destination currency regardless of whether the destination account is funded, because account creation itself requires receiving XRP above the base reserve.

## Design Observations

Both functions deliberately avoid throwing exceptions. The `if (auto const lines = ...)` and `if (auto const mpts = ...)` guards use optional-like conditional binding, returning early or simply skipping that asset class if the cache has no entry for the account. This matters for accounts that have never opened a trust line or issued an MPT — no crash, no error propagation, just an empty or XRP-only result.

The use of `hash_set<PathAsset>` is appropriate because `PathAsset` is a `std::variant<Currency, MPTID>` and deduplication across the trust line scan is important; an account could have multiple lines denominated in the same currency with different peers, but the path finder only needs one entry per currency in its initial asset set.

The shared `AssetCache` parameter is the performance critical piece. Since a single path request can probe many intermediate accounts, the cache amortizes ledger reads across all `accountSourceAssets`/`accountDestAssets` calls within a session, avoiding redundant SLE lookups on a read-only ledger snapshot.