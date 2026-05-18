# `PeerSet.h` — Peer Management Interface for Distributed Data Retrieval

## Role in the System

When a rippled node needs data it does not have locally — a historical ledger, a transaction set, a SHAMap subtree — it must fetch it from the overlay network by querying connected peers. `PeerSet` is the abstraction that governs that process: it owns the set of peers being queried, selects new peers to try, and dispatches protobuf messages to them.

The class sits at the boundary between the overlay network layer (`src/xrpld/overlay/`) and the ledger acquisition subsystem (`src/xrpld/app/ledger/`). Callers such as `InboundLedger`, `TransactionAcquire`, and `LedgerReplayer` each hold a `std::unique_ptr<PeerSet>` and drive it as their private transport handle. This ownership model is intentional: each in-flight retrieval task owns its peer set exclusively, so there is no shared-state contention between concurrent acquisitions.

## Interface Design

The interface exposes three pure-virtual operations and one non-virtual template helper.

`addPeers(limit, hasItem, onPeerAdded)` is the peer-selection entry point. The caller passes a `limit` on how many new peers to add, a predicate `hasItem` that returns whether a specific peer likely holds the target data, and a callback `onPeerAdded` to execute immediately when a peer is accepted. The concrete implementation in `PeerSetImpl::addPeers()` scores every known peer via `Peer::getScore(hasItem(peer))`, sorts them in descending score order, and then iterates to add up to `limit` peers that haven't been tracked before. The score-based ranking favors peers that have signaled they hold the item, while still considering connection quality. Deduplication is enforced by a `std::set<Peer::id_t> peers_` that prevents the same peer from being re-added across multiple `addPeers` calls for the same acquisition.

`sendRequest` is overloaded in two forms. The virtual form takes a raw `google::protobuf::Message`, the `protocol::MessageType` enum, and an optional `shared_ptr<Peer>`. When `peer` is non-null the message goes only to that peer; when null it broadcasts to every tracked peer by looking each `Peer::id_t` up in the overlay's `findPeerByShortID`. The non-virtual template wrapper on top deduces the `MessageType` automatically via `protocolMessageType()` (defined in `ProtocolMessage.h` for each concrete protobuf type like `TMGetLedger` or `TMReplayDeltaRequest`). This design solves the templated-virtual-function impossibility: the type-safe API lives in the non-virtual template, while the single virtual dispatch point handles the erased protobuf base class.

`getPeerIds()` exposes the set of numeric peer IDs already added, letting callers check how many peers are engaged or avoid re-requesting data from peers that have already been contacted.

## The Builder and Dummy Patterns

`PeerSetBuilder` is a minimal abstract factory with a single `build()` method. Its purpose is testability and initialization-order safety. Subsystems like `InboundLedgersImp` and `InboundTransactions` receive a `std::unique_ptr<PeerSetBuilder>` at construction and call `build()` each time a new acquisition is started. This means the concrete `PeerSetImpl`—which holds an `Application&` reference—is created on demand rather than upfront, and in tests a mock builder can substitute a controlled implementation without touching production code paths.

`make_DummyPeerSet()` serves a specific, documented niche: `ApplicationImp::loadOldLedger()` constructs `InboundLedger` objects to replay historical state but does not want or need live peer network activity. Rather than introducing an optional nullable pointer that every call site must guard, the codebase injects a `DummyPeerSet` that fulfills the interface contract while logging an error and doing nothing. This "fail loudly but don't crash" design catches accidental calls in development without crashing production nodes during startup replay.

## Lifetime and Concurrency Ownership

Each `PeerSet` instance is heap-allocated and owned by exactly one retrieval task via `std::unique_ptr`. The `InboundLedger` stores it as `std::unique_ptr<PeerSet> mPeerSet`, as does `TransactionAcquire`. Because ownership is exclusive and callers serialize access through their own mutex (`ScopedLockType` in `TimeoutCounter`-derived classes), the `PeerSetImpl` itself needs no internal locking. The `peers_` set is mutated only inside `addPeers` calls, which the caller already holds a lock over.

## Relationship to Other Files

- **`detail/PeerSet.cpp`** — provides `PeerSetImpl`, `PeerSetBuilderImpl`, and `DummyPeerSet`, all hidden behind the factory functions. None of these classes appear in any header.
- **`Peer.h`** — defines `Peer::id_t` (`uint32_t`) and `Peer::getScore(bool)`, which are both central to peer selection logic.
- **`detail/ProtocolMessage.h`** — defines the `protocolMessageType()` overloads that the template `sendRequest` wrapper depends on.
- **`InboundLedger.h`**, **`TransactionAcquire.h`**, **`LedgerReplayer.h`** — all hold `std::unique_ptr<PeerSet>` and are the primary consumers of this interface. The `InboundLedger` additionally uses `PeerSet::getPeerIds()` to count active peers when deciding whether to give up on an acquisition.