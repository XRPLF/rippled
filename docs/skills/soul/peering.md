# Overlay Peering

P2P network using persistent TCP/IP connections. Messages serialized via Protocol Buffers. `OverlayImpl` manages connections; `PeerImp` handles per-peer logic.

## Key Invariants

- Connection preference order: Fixed Peers -> Livecache -> Bootcache
- Cluster connections do NOT count toward connection limits (unlimited)
- Protobuf message changes MUST maintain wire compatibility or risk network partitioning
- Squelching: after enough peers relay a validator's messages, a subset is "Selected" and the rest are temporarily muted to reduce bandwidth
- Handshake binds TLS session to node identity via `signDigest` of the session fingerprint

## Common Bug Patterns

- PeerFinder slot exhaustion: if `maxPeers` is reached, new outbound connections silently fail; check slot availability before connecting
- `HashRouter::shouldRelay` prevents duplicate relay; bypassing it causes message storms
- `ConnectAttempt::processResponse` on HTTP 503 parses "peer-ips" for alternatives; malformed responses here can crash with bad IP parsing
- `PeerImp::close` must run on the strand; calling from wrong thread causes race conditions on socket and timer state
- Destructor chain: `~PeerImp` -> `deletePeer` -> `onPeerDeactivate` -> `on_closed` -> `remove`; interrupting this chain leaks slots

## Connection Lifecycle

1. `OverlayImpl::connect` -> check resource limits -> allocate PeerFinder slot -> create `ConnectAttempt`
2. Async TCP connect -> TLS handshake -> HTTP upgrade with identity headers
3. `processResponse` -> verify handshake -> create `PeerImp` -> `add_active` -> `run()`
4. `doProtocolStart` -> start async message receive loop -> exchange validator lists and manifests

## Review Checklist

- Verify resource manager checks on both inbound and outbound connections
- New protocol messages: update protobuf definitions AND verify wire compatibility
- Squelch changes: test with high peer counts; incorrect squelch logic can silence validators

## Key Patterns

### Strand Execution
```cpp
// REQUIRED: socket operations must run on the strand
if (!strand_.running_in_this_thread())
    return post(strand_, std::bind(
        &PeerImp::close, shared_from_this()));
// Calling socket ops from wrong thread causes races on state
```

### Duplicate Relay Prevention
```cpp
// REQUIRED: check HashRouter before relaying
if (!hashRouter_.shouldRelay(hash))
    return;  // already relayed — suppress duplicate
overlay_.relay(message, hash);
// Bypassing this causes message storms across the network
```

## Key Files

- `src/xrpld/overlay/detail/OverlayImpl.cpp` - main overlay manager
- `src/xrpld/overlay/detail/PeerImp.cpp` - per-peer logic
- `src/xrpld/overlay/detail/ConnectAttempt.cpp` - outbound connection
- `src/xrpld/overlay/Slot.h` - squelch state machine
- `src/xrpld/overlay/detail/Handshake.cpp` - handshake crypto
