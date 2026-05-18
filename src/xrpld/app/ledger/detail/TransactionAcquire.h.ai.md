# `TransactionAcquire` — Peer-Driven Transaction Set Fetcher

## Role in the System

During XRPL consensus, every validating node must agree on an identical set of candidate transactions before proposing and finalizing a ledger. When a node has not yet seen a particular transaction set — identified by the `uint256` hash of its `SHAMap` root — it must reconstruct that map by querying connected peers for its nodes. `TransactionAcquire` is the object that drives this retrieval from start to finish.

The class sits inside `xrpld/app/ledger/detail/`, keeping it an implementation detail of the ledger acquisition subsystem. Its lifetime is managed entirely by `InboundTransactions`, which creates one instance per missing transaction set hash and delivers incoming peer data to it.

## Inheritance and Design Shape

`TransactionAcquire` inherits from three bases:

- **`TimeoutCounter`** — provides the asynchronous timer loop, the mutex (`mtx_`), and the terminal state flags (`complete_`, `failed_`, `progress_`). It repeatedly fires `onTimer()` at 250 ms intervals until the object reports `isDone()`.
- **`std::enable_shared_from_this<TransactionAcquire>`** — needed because `pmDowncast()` must hand a `std::weak_ptr<TimeoutCounter>` back to the base without slicing. `TimeoutCounter` cannot call `shared_from_this()` directly; each concrete subclass implements `pmDowncast()` to return its own weak pointer, which `TransactionAcquire::pmDowncast()` satisfies via `shared_from_this()`.
- **`CountedObject<TransactionAcquire>`** — injects diagnostic object-count tracking at no runtime cost.

The `PeerSet` is injected as a `std::unique_ptr` via the constructor, enabling test doubles and clean ownership semantics — `TransactionAcquire` alone owns it.

## SHAMap Retrieval Strategy

The constructor creates an empty `SHAMap` of type `TRANSACTION`, keyed by the target hash, and immediately calls `setUnbacked()`. This is the critical first act: it tells the map that its nodes need not be written to any persistent node store. The map is purely an in-memory reconstruction vehicle; once complete, it is handed off and discarded.

Retrieval proceeds in two phases controlled by the `mHaveRoot` boolean:

1. **Root phase**: Until the root node arrives, `trigger()` sends a `TMGetLedger` message with `liTS_CANDIDATE` type and `querydepth=3`. Requesting depth 3 at the outset is deliberate — the sender will likely include interior nodes proactively, reducing round trips for small transaction sets.

2. **Interior-node phase**: Once `mHaveRoot` is set, `trigger()` calls `mMap->getMissingNodes(256, &sf)` using a `ConsensusTransSetSF` sync filter. The filter mediates between the low-level map sync code and the application's temporary node cache, allowing nodes to be found in cache before hitting the network. Up to 256 missing `SHAMapNodeID`s are batched into a single `TMGetLedger` request.

## Data Ingestion: `takeNodes()`

`takeNodes()` is the sole entry point for peer-delivered data. It acquires the mutex, validates terminal state (returning silently if already complete or failed), then iterates the incoming `(SHAMapNodeID, Slice)` pairs:

- Root nodes go through `addRootNode()`, which validates the hash matches the expected `SHAMapHash{hash_}`.
- Non-root nodes go through `addKnownNode()`, which also runs the sync filter.

A malformed node causes an early `SHAMapAddNode::invalid()` return without poisoning the whole acquisition — except for non-root bad nodes, which do abort with `invalid()`. After integrating the data, `trigger()` is called again on the responding peer, immediately requesting whatever the map still needs. This pipeline of receive→request drives rapid convergence when a peer has the full set.

## Timeout and Peer Escalation

`onTimer()` implements a two-tier failure policy using two constants: `NORM_TIMEOUTS = 4` (1 second) and `MAX_TIMEOUTS = 20` (5 seconds). Before reaching `NORM_TIMEOUTS`, the timer just adds one more peer via `addPeers(1)`. After `NORM_TIMEOUTS`, it also calls `trigger(nullptr)`, which sends a `qtINDIRECT` query — asking the contacted peer to relay the request deeper into the network rather than only checking itself. After `MAX_TIMEOUTS`, `failed_` is set and `done()` is called.

`addPeers()` delegates to `PeerSet::addPeers()` with two lambdas: a selection predicate (`peer->hasTxSet(hash_)`) and an on-add callback (`trigger(peer)`). This ensures only peers advertising the transaction set are contacted, and each newly added peer is immediately triggered.

## Completion and the Lock-Constrained `done()`

When `trigger()` finds no missing nodes and the map is valid, it sets `complete_ = true` and calls `done()`. The comment in `done()` is architecturally important: it is called while holding the `PeerSet` lock, so it cannot safely call arbitrary application code. The solution is to schedule a `jtTXN_DATA` job that calls `app_.getInboundTransactions().giveSet(hash, map, true)`. This deferred handoff integrates the completed `SHAMap` into the consensus state without creating lock-order hazards.

## `stillNeed()` — Preventing Premature Abandonment

`stillNeed()` resets `timeouts_` back to `NORM_TIMEOUTS` and clears `failed_`. It exists because consensus may re-request a transaction set that a previous acquisition attempt failed to complete — for example, when a delayed proposal references the same set ID. Without this escape hatch, a first failed attempt would permanently poison the acquisition object for that hash. By clamping timeouts to the lower bound and clearing failure, the object gets a fresh window of attempts.

## Relationship to `InboundTransactions`

`TransactionAcquire` does not manage its own registration or deregistration. `InboundTransactions` is responsible for creating instances, routing `TMGetLedger` responses to them via `gotData()`, and processing the completed `SHAMap` via `giveSet()`. The rename comment in the header (`// VFALCO TODO rename to PeerTxRequest`) reflects a recognized naming inconsistency: the class acquires a transaction *set*, not a single transaction, so the name borrowed from ledger acquisition is slightly misleading.