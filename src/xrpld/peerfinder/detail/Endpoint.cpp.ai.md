# `PeerFinder::Endpoint` Constructor — `Endpoint.cpp`

## Role in the System

`Endpoint.cpp` provides the single non-trivial constructor for `PeerFinder::Endpoint`, the fundamental data type that PeerFinder uses to gossip peer addresses across the XRPL network. An `Endpoint` pairs a `beast::IP::Endpoint` (IP address and port) with a hop count: an unsigned integer representing how many relay hops away the peer was when it last advertised itself.

The file is intentionally minimal — the struct is declared in `PeerfinderManager.h`, the default constructor is defaulted there, and only this parameterized constructor needs a separate definition because it references `Tuning::maxHops` from `Tuning.h`.

## The Hop-Count Cap

The only logic in the constructor is:

```cpp
: hops(std::min(hops_, Tuning::maxHops + 1)), address(ep)
```

`Tuning::maxHops` is 6. So every `Endpoint` stored in the system has a `hops` value in the range `[0, 7]`. The `+1` is deliberate: it creates a sentinel value meaning "beyond the horizon" rather than truncating to exactly `maxHops`.

Why clamp in the constructor rather than validate at the call site? Because `Endpoint` objects are built directly from data received over the network — an adversarial or buggy peer could send a hop count near `UINT32_MAX`. Clamping at construction ensures that no matter what raw value arrives, the stored field stays within a known domain. The constructor does not throw; it silently clamps. Silent clamping is preferable to rejection here because the value will be further filtered by `Logic::preprocess` before it ever reaches the livecache.

## Relationship to `Logic::preprocess`

The clamped value interacts with the gossip pipeline in `Logic.h`. When a peer sends a `TMEndpoints` message, `Logic::on_endpoints` calls `preprocess`, which:

1. **Drops** any endpoint where `ep.hops > Tuning::maxHops` (i.e., `hops > 6`). An incoming value of 6 survives; anything ≥ 7 is erased.
2. **Increments** surviving hop counts by one (`++ep.hops`) before storing them in the livecache. This models the additional relay hop added by re-broadcasting.

So an endpoint arriving with `hops == 6` enters the livecache as `hops == 7`. If that endpoint were then forwarded again, the `Endpoint` constructor would clamp it back to 7, and `preprocess` would immediately drop it because `7 > maxHops`. The cap is therefore the natural terminus of gossip propagation: no address travels more than six hops from its source.

The sentinel value of 7 also never appears in addresses sent outward — `Handouts.h` skips endpoints with `hops == 0` (which are self-advertisements, only meaningful to the receiving peer), and the livecache discards anything that has propagated past `maxHops`.

## Design Notes

The choice of `maxHops + 1` as the cap rather than `maxHops` is careful: if the cap were exactly `maxHops`, a constructor-clamped value of 6 would pass `preprocess`'s `> 6` check and then increment to 7, surviving into the livecache with a hop count that would cause it to be dropped next time it is forwarded. The current cap gives the same outcome — an excessively large incoming `hops_` is stored as 7, immediately discarded by `preprocess`'s filter — without any ambiguity about the boundary condition. It is a one-line defensive contract that makes the gossip-propagation invariant self-enforcing at the data layer.