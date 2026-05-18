# `Squelch.h` — Per-Peer Validator Relay Suppression

## Role in the System

`Squelch.h` implements the downstream half of the XRPL reduce-relay protocol. The reduce-relay system was introduced to replace naive gossip flooding of validator messages (validations and proposals) with a smarter, peer-selected relay tree. The core idea — described on the [XRPL blog](https://xrpl.org/blog/2021/message-routing-optimizations-pt-1-proposal-validation-relaying.html) — is that when many peers all forward the same validator's message, most of those forwards are redundant. A coordinating node can select a small subset (typically 5, controlled by `MAX_SELECTED_PEERS`) to continue relaying and instruct the rest to stop.

`Squelch<clock_type>` is the memory of that instruction on the silenced peer. Each `PeerImp` instance owns a `Squelch` object and consults it before forwarding any outgoing message that carries a validator's public key. This makes `Squelch` the enforcement point: it is not involved in deciding *which* peers to suppress — that decision is made upstream in `Slot`/`Slots` (see `Slot.h`) — it only records and enforces the resulting directive locally.

## Architecture: Two Sides of Squelching

The reduce-relay subsystem has two distinct roles that live in different objects:

- **`Slots<clock_type>`** (upstream node, `Slot.h`): counts inbound validator messages per peer, selects a preferred relay set once `MAX_SELECTED_PEERS` peers hit `MAX_MESSAGE_THRESHOLD`, and calls `SquelchHandler::squelch()` for each peer that should stop relaying. This fires a `TMSquelch` protobuf message over the wire.
- **`Squelch<clock_type>`** (receiving peer, `Squelch.h`): processes incoming `TMSquelch` messages and records their effect locally. When this peer's `PeerImp::send()` is called with a validator-keyed message, it calls `expireSquelch()` first. A `false` return short-circuits the send and increments the `squelch_suppressed` traffic counter.

This separation of concerns means the coordinating node's complex selection logic is entirely decoupled from the suppression enforcement on each individual peer.

## Class Design

`Squelch` is a straightforward clock-parameterized template. The only state is `squelched_`, a `hash_map<PublicKey, time_point>` that maps a validator's public key to the absolute time when its squelch expires. The clock parameterization (`clock_type`) follows the same pattern used throughout the overlay codebase: production code uses `UptimeClock`, and unit tests inject a controlled mock clock to advance time deterministically without real delays.

### `addSquelch(validator, squelchDuration)`

Records a new squelch expiry. Before storing, it validates that `squelchDuration` falls within `[MIN_UNSQUELCH_EXPIRE (300 s), MAX_UNSQUELCH_EXPIRE_PEERS (3600 s)]`. These bounds come from `ReduceRelayCommon.h` and mirror the range that `Slot::getSquelchDuration()` uses when computing squelch durations — the upstream node picks a random value in that range so that squelches stagger their expirations and don't all expire simultaneously.

If the duration falls outside these bounds, `addSquelch` does two defensive things: it logs an error and explicitly calls `removeSquelch`, clearing any previously active entry. This ensures a malformed `TMSquelch` cannot be exploited to leave stale squelch state. The caller in `PeerImp::onMessage` then charges the sender a `feeInvalidData` resource fee.

### `expireSquelch(validator)` — Lazy Cleanup

This method is called on every outbound send for a validator-keyed message. It returns `true` when the message should be forwarded (either no squelch exists, or the squelch has expired) and `false` when the suppression is still active.

The expiry is intentionally lazy: there is no background timer or cleanup thread. The squelch map entry is only removed when `expireSquelch` is called *after* the expiry time has passed. This is safe because the check is always performed before sending, so the suppression always takes effect for the correct duration, and the cleanup happens naturally the first time a new message arrives after expiry — which is also when it matters.

On the `Slot` side, `Slot::update()` mirrors this: when a message arrives from a peer whose `PeerState` is `Squelched` but whose `expire` time has passed, it transitions that peer back to `Counting` state and initiates a new selection round.

### `removeSquelch(validator)`

A simple erase from the map. Called when `PeerImp::onMessage(TMSquelch)` receives a message with `squelch == false` (the unsquelch signal), which the upstream `OverlayImpl` sends when a selected peer disconnects or goes idle and squelched peers need to be freed.

## Integration with `PeerImp`

```
// PeerImp::send() — outbound message gate
auto validator = m->getValidatorKey();
if (validator && !squelch_.expireSquelch(*validator))
{
    overlay_.reportOutboundTraffic(TrafficCount::squelch_suppressed, ...);
    return;                            // message dropped
}
```

This single guard at the send path is the only place `Squelch` intervenes in normal message flow. The `TMSquelch` handler populates the map:

```
// PeerImp::onMessage(TMSquelch)
if (!m->squelch())
    squelch_.removeSquelch(key);
else if (!squelch_.addSquelch(key, std::chrono::seconds{duration}))
    fee_.update(Resource::feeInvalidData, "squelch duration");
```

A guard prevents a validator node from squelching its own messages: if the incoming `TMSquelch`'s key matches `app_.getValidationPublicKey()`, the message is silently discarded. This prevents a misbehaving peer from exploiting the protocol to silence a validator at its own node.

## Summary of Invariants

- The squelch map holds only entries within the valid duration window; out-of-range entries are rejected and any stale entry for that key is proactively cleared.
- Expiry is checked lazily at send time; no background cleanup task is required.
- The `Squelch` class enforces suppression but makes no selection decisions — that responsibility belongs entirely to `Slot` and `Slots` in `Slot.h`.
- Clock parameterization keeps the class fully unit-testable without mocking the OS clock.