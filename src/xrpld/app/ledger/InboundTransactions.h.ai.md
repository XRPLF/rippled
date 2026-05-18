# `InboundTransactions.h` — Transaction Set Acquisition Interface

## Role in the System

`InboundTransactions` is the abstract interface that manages the acquisition and lifetime of *transaction sets* — the `SHAMap`-backed collections of transactions that the XRPL consensus algorithm exchanges between validators. During consensus, a validator may reference a transaction set by its hash that it has not yet seen locally. This class is the mechanism by which a node fetches those sets from peers and holds them long enough for the consensus round to complete.

The header defines a non-copyable abstract base class and the `make_InboundTransactions` factory function that constructs the concrete implementation in `detail/InboundTransactions.cpp`. This pattern keeps the compilation boundary clean: callers depend only on the interface, not on `TransactionAcquire`, `PeerSet`, or the internal `hash_map` bookkeeping.

## Interface Design

The three core virtual methods map directly onto the lifecycle events of a transaction set during consensus:

**`getSet(setHash, acquire)`** is the main read path. When `acquire` is true and the set is unknown, the implementation immediately creates a `TransactionAcquire` object and begins asking peers for the missing SHAMap nodes, then returns `nullptr` to the caller. The caller must call `getSet` again (or wait for the `gotSet` callback) rather than blocking. This non-blocking design is deliberate: consensus is event-driven, and blocking here would stall the entire consensus state machine.

**`gotData(setHash, peer, message)`** feeds inbound `TMLedgerData` network messages into the appropriate `TransactionAcquire` session. It validates each SHAMap node in the message and charges the sending peer via `Resource::feeInvalidData` / `feeUselessData` / `feeMalformedRequest` if the data is bad or redundant. This peer-charging logic acts as a lightweight DoS defense at the point where untrusted data enters the acquisition pipeline.

**`giveSet(setHash, set, acquired)`** is the write path, used for both externally acquired sets (routed back by `TransactionAcquire::done()`) and locally constructed ones (e.g., a validator's own proposal). The `acquired` flag is forwarded to the `gotSet` callback so the consensus engine can distinguish whether a set arrived from the network or was built locally. If the map entry already holds a set (duplicate delivery), `giveSet` silently discards the duplicate and does not fire the callback again — deduplication is structural rather than explicit.

**`newRound(seq)`** is the eviction hook. The implementation stores a sequence number alongside each cached set and, on each new consensus round, discards any entry whose sequence is more than `setKeepRounds = 3` rounds away from the current sequence. This sliding window prevents unbounded memory growth while keeping sets available for validators running slightly behind or ahead. The zero-hash set (the canonical empty transaction set) receives special protection: `newRound` always refreshes its sequence to prevent it from expiring, since consensus may reference it legally at any time.

## Internal Architecture (from `detail/InboundTransactions.cpp`)

The concrete class `InboundTransactionsImp` owns a `hash_map<uint256, InboundTransactionSet>` protected by a `std::recursive_mutex`. The `InboundTransactionSet` struct holds three things: the ledger sequence at which this entry was last touched (`mSeq`), a completed `std::shared_ptr<SHAMap>` (`mSet`), and an optional `TransactionAcquire::pointer` (`mAcquire`) that represents a pending network fetch. Once `mSet` is populated, `mAcquire` is reset — the two states are mutually exclusive in practice.

The `gotSet` callback injected at construction time (type `std::function<void(std::shared_ptr<SHAMap> const&, bool)>`) is how `InboundTransactions` notifies the consensus engine (specifically `RCLConsensus::Adaptor`) that a newly-complete set is available. The boolean parameter signals whether the set came from a peer acquisition. In the consensus adapter, `acquireTxSet()` calls `getSet(id, true)` and expects to receive either an immediately-available set or `nullptr` while the async fetch is in progress; the `gotSet` callback re-enters the consensus state machine when the fetch completes.

## Relationship to `RCLConsensus::Adaptor`

The consensus layer interacts with this interface in three places:

- `Adaptor::acquireTxSet()` calls `getSet()` — the primary lookup path.
- `Adaptor::share()` calls `giveSet(txns.id(), txns.map_, false)` — publishing a locally-built proposal to the cache so peers can request it.
- `startRound()` and `acquireLedger()` call `newRound(seq)` — advancing the cache's expiry window when consensus moves to a new ledger sequence.

## Stop Semantics

`stop()` sets an internal `stopping_` flag and clears the entire map under the lock. Once stopped, `getSet(..., true)` will not initiate new acquisitions even if `acquire` is true. This prevents new peer requests from being issued during teardown, when the `Application` and its peer subsystem may be partially destroyed.

## Design Tradeoffs

The use of `std::recursive_mutex` (rather than `std::mutex`) signals that some call paths — particularly through `TransactionAcquire`'s completion callback back into `giveSet` — were anticipated to re-enter the lock from the same thread. The abstract base plus factory pattern allows unit tests to substitute a mock without dragging in the full network acquisition machinery.