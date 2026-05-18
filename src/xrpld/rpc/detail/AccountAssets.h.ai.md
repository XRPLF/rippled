# `AccountAssets.h` — Asset Eligibility Interface for the Path-Finder

This header declares two free functions that answer a focused question for the XRPL payment path-finding subsystem: given an account, which assets can it currently send, and which can it receive? The functions expose only what `PathRequest.cpp` needs to seed a path-finding session without coupling callers to the `AssetCache` internals or the raw trust line / MPT representations.

## Context in the Path-Finding Pipeline

`PathRequest` drives the `ripple_path_find` and `path_find` RPC commands. Before invoking the expensive graph traversal in `Pathfinder`, it needs to scope the search to assets that are actually usable. Two call sites in `PathRequest.cpp` consume these functions:

- When building the initial response, `accountDestAssets` is called with the destination account to populate `destination_currencies` — the list the caller uses to understand which assets the destination can accept.
- When auto-detecting source currencies (i.e., no explicit `send_max` was provided), `accountSourceAssets` is called with the sending account to discover its spendable assets, capped at `RPC::Tuning::max_auto_src_cur`.

Both functions receive an `AssetCache` — a short-lived, ledger-snapshot cache that pre-fetches and memoizes trust lines and MPT entries for a single pathfinding session. Sharing the cache across repeated calls for the same account avoids redundant ledger reads and keeps memory proportional to accounts actually visited.

## The Two Functions

`accountSourceAssets` determines what an account can pay with. For IOU trust lines it checks two conditions per line: either the account holds a positive balance, or the peer extends a credit limit that hasn't been fully consumed (i.e., `(-balance) < limitPeer`). This captures both the case where the sender literally has tokens and the case where the sender can effectively borrow up to the peer's credit extension. For Multi-Purpose Tokens (MPTs), a holding is source-eligible only when the balance is non-zero and the MPT is not at its maximum outstanding amount. After processing trust lines, `badCurrency()` is explicitly erased from the result — a defensive measure ensuring the protocol's sentinel "bad currency" value can never propagate as a valid payment asset.

`accountDestAssets` checks what an account can receive. For trust lines the condition is simply `balance < ownLimit`: the account hasn't consumed its self-imposed credit ceiling, so more of the IOU can flow in. For MPTs the logic inverts relative to the source check: the account must have a *zero* balance while not being maxed out. This reflects that an MPT holder relationship is established at zero before funds arrive, so a freshly authorized (or recently emptied) holder is the right receive target. The inline comment "// Even if account doesn't exist" clarifies the XRP case — native XRP can be sent to an unfunded account, so XRP is added to the destination set independently of whether the account has an on-ledger entry.

## The `includeXRP` Flag

Both functions accept a `bool includeXRP` parameter rather than hard-coding XRP inclusion. Callers use this to suppress XRP in scenarios where it is irrelevant — for example, when the destination account has set the `lsfDisallowXRP` flag, `PathRequest` passes `!disallowXRP` to `accountDestAssets`. This keeps the policy decision at the RPC layer where the business rule lives, rather than buried inside the asset-enumeration logic.

## Relationship to `AssetCache` and `PathAsset`

The `AssetCache` dependency (declared in `AssetCache.h`) is the only include this header requires beyond `UintTypes.h`. `AssetCache` internally uses `PathFindTrustLine` and `PathFindMPT` wrappers around ledger SLE data, both designed to minimize per-instance memory for the path-finder's high-volume usage. The return type `hash_set<PathAsset>` is a variant-like type that can hold either a currency code (for IOUs and XRP) or an `MPTID`, allowing source and destination asset sets to uniformly represent both token classes.