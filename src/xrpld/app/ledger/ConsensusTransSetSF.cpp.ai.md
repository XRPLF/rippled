# `ConsensusTransSetSF` — Transaction Set Sync Filter for Consensus

## Role in the System

During XRPL consensus, every validator must converge on the same transaction set before building the next ledger. When a node doesn't have the full transaction set proposed by its peers, it acquires it incrementally over the peer-to-peer network using `TransactionAcquire`, which internally drives a `SHAMap` synchronization process. `ConsensusTransSetSF` sits at the boundary between that low-level `SHAMap` sync machinery and the higher-level application layer — acting as the bridge that lets the sync engine consult and populate application caches, and that opportunistically injects discovered transactions into the processing pipeline.

The file implements `SHAMapSyncFilter`, a two-method pure-virtual interface defined in `include/xrpl/shamap/SHAMapSyncFilter.h`. The two methods represent opposite data directions: `getNode` supplies data *to* the sync engine (look here first before fetching from the network), and `gotNode` receives notifications *from* the sync engine (a new node just arrived).

## Design Rationale: Two-Direction Filtering

The `SHAMapSyncFilter` abstraction cleanly separates what the SHAMap knows (tree structure, hashes) from what the application knows (transaction caches, job queues, database). This lets the sync algorithm remain agnostic about higher-level storage. `ConsensusTransSetSF` is explicitly noted in its header as being "needed on both add and check functions," distinguishing it from `AccountStateSF`, which only participates in add operations. This distinction reflects that during consensus transaction set acquisition, the node both needs to contribute locally cached data (to avoid redundant network fetches) and needs to act on newly received data (to pipeline discovered transactions).

## `getNode`: Serving Local Data to the Sync Engine

`getNode(nodeHash)` is called by the `SHAMap` sync code when it needs the raw bytes for a particular hash before deciding to request it from a peer. The lookup proceeds in two stages. First it checks `m_nodeCache`, a `TaggedCache<SHAMapHash, Blob>` for intermediate SHAMap tree nodes. If that misses, it falls back to `app_.getMasterTransaction().fetch_from_cache()`, which queries an in-memory `TaggedCache<uint256, Transaction>` for leaf transaction nodes.

When a transaction is found via `TransactionMaster`, it must be re-serialized into wire format before returning. This serialization prepends `HashPrefix::transactionID` (4 bytes) before the transaction body — the same prefix used when the transaction was originally hashed. An `XRPL_ASSERT` then verifies that `sha512Half` of the resulting blob matches the requested hash. This invariant enforcement ensures no hash/content mismatch can propagate silently through the sync process.

## `gotNode`: Receiving and Forwarding Newly Acquired Nodes

`gotNode` is invoked whenever the sync engine successfully integrates a node into the `SHAMap`. The `fromFilter` flag signals whether the data originated from the filter itself (i.e., was returned by a prior `getNode` call). If true, the node was already known locally and there is nothing to do — the method returns immediately, avoiding redundant cache writes and duplicate transaction submissions.

For genuinely new nodes, the raw bytes are first inserted into `m_nodeCache`. If the node type is `SHAMapNodeType::tnTRANSACTION_NM` (a leaf transaction node without metadata) and the data is longer than 16 bytes (a guard against malformed stubs), the method proceeds to deserialize it. It skips the 4-byte `HashPrefix` prefix and constructs an `STTx` from the remaining bytes via `SerialIter`. An `XRPL_ASSERT` checks that the deserialized transaction's ID matches the node hash, catching any corruption before the transaction enters the application.

The transaction is then submitted asynchronously via `app_.getJobQueue().addJob(jtTRANSACTION, ...)`, which calls `NetworkOPs::submitTransaction`. This is the critical side-effect: transactions discovered while assembling a consensus set are opportunistically forwarded into the node's own transaction processing pipeline, so the node can validate and hold them locally even if it hadn't seen them before. The job queue dispatch avoids blocking the sync callback thread on transaction validation work.

Deserialization failures are caught as `std::exception` and logged as warnings rather than propagating. A malformed transaction in a proposed set is treated as a non-fatal event — the SHAMap node is still cached, but the invalid transaction is not submitted.

## Integration with `TransactionAcquire`

`ConsensusTransSetSF` is instantiated in `TransactionAcquire::trigger()` and `TransactionAcquire::takeNodes()`, always using `app_.getTempNodeCache()` as the shared `NodeCache`. In `trigger()`, it is passed to `SHAMap::getMissingNodes(&sf)`, allowing the sync engine to fill gaps from local caches before computing which hashes to request from peers. In `takeNodes()`, it is passed to `SHAMap::addKnownNode(..., &sf)`, so each newly received peer node flows through `gotNode` for caching and potential transaction injection. The filter object is stack-allocated and short-lived — created per call rather than shared across the lifetime of the acquisition — which is safe because `Application` and `NodeCache` are both externally owned and longer-lived.