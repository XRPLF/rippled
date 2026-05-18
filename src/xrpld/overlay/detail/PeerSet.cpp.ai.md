# `src/xrpld/overlay/detail/PeerSet.cpp`

## Role in the System

This file provides the concrete implementations backing the `PeerSet` and `PeerSetBuilder` abstract interfaces declared in `PeerSet.h`. A `PeerSet` is the mechanism by which data-acquisition subsystems — `InboundLedger`, `TransactionAcquire`, `LedgerDeltaAcquire`, and similar — select a working subset of overlay peers to query for missing data (ledgers, transaction sets, skip lists). The file defines three distinct classes: `PeerSetImpl` (the live implementation), `PeerSetBuilderImpl` (a factory), and `DummyPeerSet` (a null-object for offline loading).

## `PeerSetImpl` — Scored Peer Selection and Broadcast

`PeerSetImpl` holds a `std::set<Peer::id_t>` rather than a set of `shared_ptr<Peer>`. This is a deliberate design choice: overlay peers can disconnect at any moment, and storing numeric IDs avoids holding long-lived `shared_ptr` references that would keep disconnected peer objects alive. Peer pointers are only resolved transiently via `overlay.findPeerByShortID()` at the moment a message needs to be sent.

### `addPeers()`

The algorithm for adding peers is more sophisticated than a simple random selection. It asks every connected peer for a score via `peer->getScore(hasItem(peer))`, where `hasItem` is a caller-supplied predicate that returns `true` if the peer already possesses the target data. By passing this boolean into `getScore`, the scoring function can rank peers that are known to have the item higher, making it likely that requests reach useful peers first. The resulting `(score, peer)` pairs are sorted descending, and up to `limit` peers that are not already in `peers_` are accepted. Uniqueness is enforced implicitly via the `std::set::insert` return value — a `false` second element means the ID is already tracked and the peer is skipped without calling `onPeerAdded`.

The two callback parameters (`hasItem` and `onPeerAdded`) keep this class completely domain-agnostic. `PeerSet` never needs to know whether the data being retrieved is a ledger, a transaction set, or anything else — callers inject their own logic. `InboundLedger`, for instance, passes `peer->hasLedger(hash_, mSeq)` as `hasItem` and a lambda that sends an initial fetch request as `onPeerAdded`.

### `sendRequest()`

`sendRequest()` handles both unicast and broadcast in a single method. When the `peer` parameter is non-null, the message is sent only to that peer. When it is null, the method iterates over all tracked `Peer::id_t` values, resolves each to a live `Peer` via `findPeerByShortID`, and calls `send()` only if the peer is still connected (a null return from `findPeerByShortID` is silently skipped). The `Message` object wrapping the protobuf payload is allocated once as a `shared_ptr` and shared across all send calls in the broadcast path, avoiding redundant serialization.

The public `sendRequest<MessageType>()` template in the header derives the `protocol::MessageType` enum value via `protocolMessageType(message)` and delegates to this virtual method, so callers never have to specify the type tag manually.

## `PeerSetBuilderImpl` — Factory Pattern for Testability

Rather than constructing `PeerSetImpl` directly, consumers receive a `PeerSetBuilder` whose `build()` method produces a fresh `PeerSet`. This indirection allows test code to substitute a mock builder that returns instrumented or no-op `PeerSet` instances without touching production wiring. The `make_PeerSetBuilder(Application&)` free function is the entry point used by real application code to obtain a `PeerSetBuilderImpl`.

## `DummyPeerSet` — Null-Object for Offline Ledger Loading

`DummyPeerSet` exists for one specific scenario documented in `PeerSet.h`: `ApplicationImp::loadOldLedger()`. When the node is replaying or loading a historical ledger at startup from local storage, an `InboundLedger` object is still constructed (to reuse its ledger-assembly logic), but no peer communication should occur. Rather than adding special-case branches throughout `InboundLedger`, a `DummyPeerSet` is injected. All three interface methods log an error if called — they represent programming errors if triggered in this context, not expected no-ops — and `getPeerIds()` returns a reference to a `static` empty set to satisfy the return type contract without undefined behavior.

This is a clean application of the null-object pattern: the shape of the interface is preserved, the surrounding code needs no conditional guards, and any accidental peer interaction during offline loading surfaces immediately as an error log rather than silently misbehaving.

## Invariants and Failure Modes

The `peers_` set provides an automatic duplicate-prevention invariant: a peer that has already contributed to a retrieval attempt cannot be re-added by a subsequent `addPeers()` call. This matters because `addPeers()` is called repeatedly as a retrieval times out and retries — the growing `peers_` set acts as an exclusion list ensuring each peer is tried at most once per acquisition. If all overlay peers have been exhausted (i.e., every scored peer is already in `peers_`), `addPeers()` simply adds zero new peers without error, leaving the higher-level `TimeoutCounter` to decide whether to declare the acquisition failed.