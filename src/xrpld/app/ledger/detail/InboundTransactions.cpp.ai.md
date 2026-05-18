# `InboundTransactions.cpp` — Transaction Set Acquisition and Lifecycle Manager

This file implements the `InboundTransactions` subsystem, which manages the acquisition, caching, and lifetime of transaction sets (`SHAMap` instances of type `TRANSACTION`) that the consensus engine needs during each ledger-building round. During XRPL consensus, every validator must converge on the same set of transactions; this component is what bridges the gap between "I know the hash of the transaction set I need" and "I actually have its contents."

## Internal Structure

The file defines two private classes and a factory function. `InboundTransactionSet` is a trivial value holder bundling a round sequence number (`mSeq`), an optional in-progress `TransactionAcquire` object, and the final `SHAMap` once acquisition completes. `InboundTransactionsImp` is the concrete implementation, hidden behind the `InboundTransactions` abstract interface declared in the header. Consumers interact only with the interface and the `make_InboundTransactions()` factory, keeping the acquisition machinery fully encapsulated.

The central data structure is `m_map`, a `hash_map<uint256, InboundTransactionSet>` keyed by transaction set hash. A single `std::recursive_mutex` (`mLock`) serializes all access to this map.

## The Zero Set

The constructor immediately seeds `m_map` with a special entry for `uint256()` (the all-zeros hash), holding a pre-built empty, unbacked `SHAMap`. This "zero set" represents the empty transaction set, which is a valid consensus outcome. A reference `m_zeroSet` is bound to this map entry at construction time; this is safe because `hash_map` (an open-addressing or node-based hash map) does not move elements on insert, so the reference remains stable. `newRound()` explicitly bumps `m_zeroSet.mSeq` on every round to prevent it from being swept out by the TTL logic.

## Acquisition Flow (`getSet`)

`getSet(hash, acquire)` is the primary read path. When the requested set is already in `m_map`, it returns the `SHAMap` immediately. If acquisition is in progress and `acquire` is true, it also calls `stillNeed()` on the `TransactionAcquire` object, which resets the peer retry counter back to the normal threshold — a defensive measure that prevents the timeout from giving up on a set the caller just confirmed it still needs.

When the set is unknown and `acquire` is true, a new `TransactionAcquire` is created and inserted into `m_map` under the lock, then `init(startPeers)` is called **outside** the lock. This lock-release-before-init pattern is deliberate: `init()` acquires `TransactionAcquire`'s own internal mutex and immediately contacts peers via `addPeers`. When acquisition later completes, `TransactionAcquire::done()` posts a job to call `giveSet()`, which in turn re-acquires `mLock`. Holding `mLock` across `init()` would create a potential deadlock path. After initiating acquisition, `getSet` returns `nullptr`; callers are expected to retry via the `gotSet` callback rather than blocking.

## Incoming Data Pipeline (`gotData`)

`gotData()` is called from the network layer when a peer delivers a `TMLedgerData` protobuf message. The implementation first fetches the corresponding `TransactionAcquire` under lock — releasing the lock before any further work. If no acquisition is active for the given hash, the peer is immediately charged `Resource::feeUselessData`. Otherwise, each node in the packet is validated for the presence of both `nodeid` and `nodedata` fields, and `deserializeSHAMapNodeID` is called on the raw bytes; any deserialization failure triggers `feeInvalidData`. The validated nodes are then handed to `TransactionAcquire::takeNodes()`, which returns a `SHAMapAddNode` status; a non-useful result triggers a `feeUselessData` charge on the peer. This multi-layered charging is an inbound DoS defense: it differentiates malformed, unsolicited, and redundant data to accurately penalize misbehaving peers.

## Completion and Deduplication (`giveSet`)

Once `TransactionAcquire` assembles all nodes, it marks the `SHAMap` immutable and posts a job to call `giveSet(hash, map, true)`. Locally-constructed sets (built by consensus itself) arrive via `giveSet(hash, map, false)`. Inside `giveSet`, the implementation checks whether the entry already has a complete set; if so, the `m_gotSet` callback is suppressed. This deduplication prevents the consensus layer from processing the same transaction set twice — which could happen if, say, a local set construction races with a peer delivering the same set. In either case the `mAcquire` pointer is cleared, releasing the `TransactionAcquire` object and its peer connections.

## Round-Based TTL (`newRound`)

Each time consensus starts a new round, `newRound(seq)` is called. The implementation keeps only entries whose `mSeq` falls within `[seq - setKeepRounds, seq + setKeepRounds]` where `setKeepRounds = 3`. The forward window (allowing entries newer than the current round) is important because validators may receive transaction sets for the upcoming round before the current one closes. Sets older than three rounds are presumed irrelevant and evicted. The zero set is always pinned to the current `seq` to avoid eviction.

## Shutdown Safety

`stop()` sets a `stopping_` flag under the lock and clears `m_map`, dropping all shared_ptr references to in-flight acquisitions. The `getSet` fast-path checks `stopping_` before creating any new `TransactionAcquire`, ensuring no new acquisitions begin after the application begins shutting down. Since `TransactionAcquire::done()` posts the `giveSet` call as a job rather than calling it directly, jobs queued before `stop()` may still invoke `giveSet` after the map is cleared; `giveSet`'s `m_map[hash]` operator will re-insert a fresh entry, but `m_gotSet` will still fire for it. This is noted as acceptable in the `TransactionAcquire` source: the consensus structures need not be updated during shutdown.