# `setup_HashRouter.h` — Configuration Bridge for `HashRouter`

This header declares the single factory function `setup_HashRouter()`, which translates XRPL node configuration into a `HashRouter::Setup` value object. It lives at the boundary between the application's configuration layer (`xrpld`) and the lower-level protocol library (`xrpl/core`), following the pattern used throughout the codebase of pairing a `setup_*` function with each configurable component.

## Role in the System

`HashRouter` is the peer-to-peer suppression table: it tracks which network peers have already seen a given hash (transaction, validation, etc.) and uses that to avoid redundant relaying. Its `Setup` struct holds two timing parameters — `holdTime` (how long an entry stays in the map, defaulting to 300 seconds) and `relayTime` (the minimum interval before the same item is relayed again, defaulting to 30 seconds). Rather than having `HashRouter` know about the config format directly, `setup_HashRouter()` owns that translation in the `xrpld` layer and returns a fully-constructed `Setup` by value.

## Implementation Constraints

The implementation in `detail/setup_HashRouter.cpp` reads the `[hashrouter]` section of the config file and enforces hard lower bounds with documented rationale:

- `hold_time` must be at least **12 seconds** — described as "the approximate validation time for three ledgers." Shorter hold times would allow duplicate messages to flood the routing table between ledger closes.
- `relay_time` must be at least **8 seconds** — "the approximate validation time for two ledgers."
- `relay_time` must not exceed `hold_time`, ensuring a relayed entry is never evicted before its relay suppression window expires.

Violations throw `std::runtime_error` at startup, making misconfiguration a hard failure rather than a silent behavioral change. This is intentional: the comment in `HashRouter.h` explicitly warns that these parameters are undocumented and require network-wide coordination to change, so local misconfiguration is treated as a programming error.

## Design Note

The forward declaration of `Config` in the header (rather than including `xrpld/core/Config.h`) keeps this header lightweight — callers that only need the declaration don't pull in the full config machinery. The implementation file includes `Config.h` directly where the field lookups actually happen.