# `PeerfinderConfig.cpp` — PeerFinder Connection Configuration

This file implements the method bodies for `PeerFinder::Config`, the value-type struct that governs how an XRPL node discovers and manages peer connections. It sits at the boundary between the coarse server-wide `xrpl::Config` (parsed from `rippled.cfg`) and the internal `PeerFinder::Manager` that executes the connection policy at runtime.

## Responsibilities

`PeerFinder::Config` holds five numerically-related slot counts (`maxPeers`, `outPeers`, `inPeers`), several behavioral flags (`autoConnect`, `wantIncoming`, `peerPrivate`), and the per-IP connection ceiling (`ipLimit`). This file provides three things: the logic for deriving `outPeers` from `maxPeers`, the business rules that sanity-clamp `ipLimit`, and the factory function that translates an operator's `rippled.cfg` into a validated `Config` object.

## Default Construction and `calcOutPeers()`

The constructor initializes `outPeers` immediately via `calcOutPeers()`. This works because `maxPeers` already has its in-class default of `Tuning::defaultMaxPeers` (21) when the constructor runs, giving a coherent default `Config` object even without calling `makeConfig()`. The formula uses rounding-half-up integer arithmetic:

```cpp
std::max((maxPeers * Tuning::outPercent + 50) / 100, std::size_t(Tuning::minOutCount))
```

With `outPercent = 15` and `defaultMaxPeers = 21`, the raw result is 3, which is immediately overridden by `minOutCount = 10`. The hard floor exists deliberately: the comment in `Tuning.h` notes that it is enforced *outside* the Logic layer so unit tests can use small peer counts without hitting edge cases in the connection-management logic.

## `makeConfig()` — the Configuration Bridge

`makeConfig()` is a static factory that maps `xrpl::Config` fields to `PeerFinder::Config` fields. The xrpl::Config comments reveal a migration in progress: `PEERS_MAX` is the legacy combined limit, while `PEERS_OUT_MAX` and `PEERS_IN_MAX` are its intended replacements. The factory handles both modes explicitly:

- **Legacy mode** (`PEERS_OUT_MAX == 0 && PEERS_IN_MAX == 0`): `maxPeers` is set from `PEERS_MAX` (or left at the default), `outPeers` is calculated via `calcOutPeers()`, and `inPeers` is the remainder. If the node can't or won't accept incoming connections, `outPeers` is set equal to `maxPeers` and `inPeers` becomes zero.

- **New mode** (either `PEERS_OUT_MAX` or `PEERS_IN_MAX` is non-zero): the split is set directly from config, and `maxPeers` is deliberately set to `0`. This sentinel value signals that `maxPeers` is not meaningful in this mode — the two individual limits stand on their own.

### Two-tier Validator Privacy

The privacy logic is intentionally ordered. First, `wantIncoming` is derived from `!peerPrivate && port != 0`. Then, if `validationPublicKey` is non-empty (i.e., the node is a validator), `peerPrivate` is forced true *after* `wantIncoming` has already been set. The effect is that validators configured with a key but without an explicit `[peer_private]` setting will still advertise `wantIncoming = true` internally — they can accept inbound connections — but they will request that peers never republish their address via gossip. This is what the code comment calls "soft" peer privacy: it protects validators from being widely advertised while allowing them to build connections organically.

If the operator explicitly sets `[peer_private]` in config, the earlier path sets both `peerPrivate` and `wantIncoming = false` together, producing full privacy where the node refuses all inbound connections.

### Standalone Mode and `autoConnect`

`autoConnect` is disabled when the node runs standalone (`cfg.standalone()`) or has explicit peer privacy. Standalone mode is used for development or testing without a live network; having automatic peer discovery enabled there would be a logic error.

## `applyTuning()` — Defending Incoming Slots

`applyTuning()` enforces one key invariant: no single remote IP address should be able to exhaust all incoming connection slots. If `ipLimit` was left at zero by the caller, a base value of 2 is assigned, then scaled upward proportionally for nodes with unusually large `inPeers` counts (one extra slot per multiple of `defaultMaxPeers`). The final line clamps the result to `[1, inPeers/2]`:

```cpp
ipLimit = std::max(1, std::min(ipLimit, static_cast<int>(inPeers / 2)));
```

The lower bound of 1 means a node with even a single inbound slot can still accept connections. The upper bound of `inPeers / 2` ensures that at minimum two distinct source IPs are needed to fill all inbound slots, providing basic resistance to connection monopolization by a single actor. This is applied at the end of `makeConfig()` as the final "business rule enforcement" step, making the correct call order explicit to readers.

## `onWrite()` — Diagnostics

`onWrite()` serializes the configuration into a `beast::PropertyStream::Map` for the live admin inspection surface. The `Manager` class inherits from `beast::PropertyStream::Source`, and it delegates to this method when a monitoring or admin endpoint queries the peerfinder subsystem's current configuration.

## Relationship to Other Files

- **`Tuning.h`**: defines all the numeric constants (`outPercent`, `minOutCount`, `defaultMaxPeers`) consumed here. Keeping these in a separate header allows them to be shared with both the config logic and the connection-management `Logic` class without circular dependencies.
- **`PeerfinderManager.h`**: declares the `Config` struct, the `Manager` abstract interface, and the `Result` enum. This file only provides the method implementations; the data members and their in-class defaults live in the header.
- **`xrpld/core/Config.h`**: the source of `PEERS_MAX`, `PEERS_OUT_MAX`, `PEERS_IN_MAX`, `PEER_PRIVATE`, and `standalone()`. The comments in that file confirm the legacy/new mode duality that `makeConfig()` must navigate.