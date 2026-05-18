# `src/xrpld/rpc/detail/MPT.h`

## Role in the System

This header defines `PathFindMPT`, a compact value type used exclusively within the XRPL pathfinding subsystem. Its purpose is to carry an `MPTID` (a 192-bit Multi-Party Token identifier) together with two boolean flags that encode economically relevant state about that token — state that needs to be inspected during path construction without re-reading the ledger on every evaluation step.

The type lives in `src/xrpld/rpc/detail/`, alongside `AssetCache.h`, `AssetCache.cpp`, and `Pathfinder.cpp`. It represents the MPT analogue of `PathFindTrustLine`: where trust-line pathfinding stores per-account, per-line trust state in a cache, MPT pathfinding stores per-account issuance state as `std::vector<PathFindMPT>` in `AssetCache`.

## The Two Boolean Flags

`zeroBalance_` records whether the account's MPToken balance is currently zero. The comment makes a critical constraint explicit: this is always `false` for the issuer. When `AssetCache::getMPTs()` processes an `ltMPTOKEN_ISSUANCE` ledger object (i.e., the issuer's side), it constructs `PathFindMPT` with `zeroBalance = false` unconditionally. When it processes an `ltMPTOKEN` (a holder's token object), it reads `sfMPTAmount` and sets the flag accordingly. This distinction matters because a holder with a zero balance can receive but not meaningfully send, which affects whether a path through that account is viable.

`maxedOut_` records whether the issuance's `sfOutstandingAmount` has reached its configured maximum (`maxMPTAmount`). When an issuance is maxed out, no additional tokens can be minted, which means certain payment paths are effectively closed. This flag is computed both for issuers (from their `ltMPTOKEN_ISSUANCE` directly) and for holders (by looking up the associated issuance from the ledger). The holder-side lookup has a subtle fallback: if the issuance ledger entry cannot be found, `maxedOut` defaults to `true` — a conservative choice that treats orphaned or missing issuances as non-viable routes rather than potentially routing through them.

## Why Cache State on the Identifier

Path construction in `Pathfinder.cpp` iterates over many candidate accounts and assets using a templated lambda (`forAssets`) that handles both `std::vector<PathFindTrustLine>` and `std::vector<PathFindMPT>` via `if constexpr` dispatch. By the time the pathfinder calls into this logic, the ledger has already been read once by `AssetCache::getMPTs()` and the results are stored behind a shared pointer. Each subsequent evaluation of a candidate MPT in the path avoids another ledger read and instead checks the pre-populated flags on the `PathFindMPT` object directly.

This is important because path search is combinatorial: the same token may appear repeatedly across many candidate paths for a single `path_find` or `ripple_path_find` RPC call.

## Implicit Conversion and API Design

The `operator MPTID const&()` conversion operator allows a `PathFindMPT` to be passed directly to any function that accepts an `MPTID`, such as `getMPTIssuer()` called in `Pathfinder.cpp`:

```cpp
return getMPTIssuer(asset);  // asset is PathFindMPT; implicit conversion to MPTID
```

This means the pathfinder loop, which is templated over the asset-vector type, can call `getMPTIssuer(asset)` without knowing whether `asset` is an `MPTID` or a `PathFindMPT`. The explicit `getMptID()` accessor mirrors the same accessor on `MPTIssue` and provides a named alternative where implicit conversion would be ambiguous or where `constexpr` evaluation requires it.

## Relationship to MPTIssue

`MPTIssue` (defined in `xrpl/protocol/MPTIssue.h`) is the protocol-layer type that adapts `MPTID` to the `Issue` interface for use in amounts, comparisons, and JSON serialization. `PathFindMPT` is narrower — it exists only for pathfinding and carries no serialization logic. The class is `final`, has all-`const` members, and offers no mutation methods, reinforcing that it is a read-once snapshot of ledger state at the start of a path search session.

## MPT Pathfinding Constraints

Unlike trust lines, MPT does not support rippling. The pathfinder reflects this by always assigning `LineDirection::incoming` for MPT assets, regardless of the account's position in the path, and by always treating the issuer account (extracted via `getMPTIssuer`) as the other party. This is a deliberate asymmetry that `PathFindMPT`'s design accommodates: the two flags are defined relative to the holder/issuer distinction rather than a directional flow, making the type self-consistent with the non-rippling constraint.