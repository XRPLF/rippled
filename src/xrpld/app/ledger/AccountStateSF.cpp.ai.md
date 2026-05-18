# `AccountStateSF` — Account State SHAMap Sync Filter

## Role and Purpose

`AccountStateSF` is the concrete `SHAMapSyncFilter` implementation that connects the low-level SHAMap synchronization machinery to the ledger's persistence and peer-data layers for the **account state tree**. When a node is fetching a historical ledger from the network, the SHAMap traversal code needs two capabilities: somewhere to cache nodes it has just received, and somewhere to look for nodes it might already have cached locally from prior peer messages. `AccountStateSF` fills both roles — it writes newly received nodes straight into the `NodeStore::Database`, and it reads speculatively available nodes out of a fetch-pack cache supplied by `AbstractFetchPackContainer`.

The class holds references to both collaborators: `db_` is the persistent node store shared across all ledger trees, and `fp_` is an abstract interface for fetch-pack retrieval, deliberately narrow so that neither `Application` nor `LedgerMaster` need to be visible at construction time.

## The `SHAMapSyncFilter` Contract

The base class `SHAMapSyncFilter` (in `include/xrpl/shamap/SHAMapSyncFilter.h`) defines only two pure virtual methods and is non-copyable:

- `gotNode()` — called by the SHAMap engine after a tree node has been obtained (from the network, a peer response, or a disk fetch). The implementation is responsible for persisting or caching the node so it survives beyond the current sync pass.
- `getNode()` — called before the SHAMap engine goes to the network for a missing node. If the filter can supply the data from a local cache, the network round-trip is avoided.

The `bool fromFilter` first parameter to `gotNode()` distinguishes whether the node came from `getNode()` itself (i.e., was already in the local fetch pack) versus arrived fresh from the network. `AccountStateSF` ignores this flag — it unconditionally stores every node into the database. This is intentional: the persistent node store is the canonical destination, and writing the same content-addressed blob twice is harmless (the store will deduplicate by `uint256` hash). There is no scenario where the account state filter would want to silently discard a node it just learned about.

## `gotNode` — Persisting Received Nodes

```cpp
void AccountStateSF::gotNode(bool, SHAMapHash const& nodeHash,
    std::uint32_t ledgerSeq, Blob&& nodeData, SHAMapNodeType) const
{
    db_.store(hotACCOUNT_NODE, std::move(nodeData), nodeHash.as_uint256(), ledgerSeq);
}
```

The two ignored parameters (`bool` and `SHAMapNodeType`) narrow the implementation's concern: `AccountStateSF` only ever handles account state nodes, so the type tag from the SHAMap layer is redundant — the `hotACCOUNT_NODE` tag is hardcoded when writing to the database. This tag ends up serialized into the stored blob's encoding and distinguishes account state nodes from ledger-header blobs (`hotLEDGER`) and transaction nodes (`hotTRANSACTION_NODE`) in the node store. The `nodeData` blob is moved rather than copied, avoiding an allocation for what could be a substantial serialized SHAMap inner or leaf node.

## `getNode` — Looking Up Cached Nodes

```cpp
std::optional<Blob> AccountStateSF::getNode(SHAMapHash const& nodeHash) const
{
    return fp_.getFetchPack(nodeHash.as_uint256());
}
```

The fetch pack is a short-lived cache of partial ledger data collected from peers during the ledger-acquisition protocol. Consulting it before requesting nodes from the network avoids redundant peer messages when multiple missing nodes from the same ledger are being fetched in parallel. `AbstractFetchPackContainer` returns `std::nullopt` when the hash is not present, which signals to the SHAMap engine to proceed with a network request.

## Relationship to `ConsensusTransSetSF`

The sibling class `ConsensusTransSetSF` serves the same role for transaction sets built during consensus. The architectural difference is deliberate: `ConsensusTransSetSF` uses an in-memory `TaggedCache<SHAMapHash, Blob>` because consensus transaction sets are ephemeral — they only need to live long enough for consensus to complete. `AccountStateSF` routes everything through the persistent `NodeStore::Database` because account state nodes must survive process restarts and be available for future ledger queries. The choice of backing store is the entire semantic difference between the two filters.

## Usage in `InboundLedger`

`InboundLedger` constructs `AccountStateSF` instances at multiple points during the ledger-fetch lifecycle — when checking for a locally-cached state root, when computing missing node lists, and when processing incoming peer responses containing account state subtrees. The filter is always constructed on the stack with `mLedger->stateMap().family().db()` and `app_.getLedgerMaster()` (which implements `AbstractFetchPackContainer`), keeping its lifetime scoped to the operation at hand and avoiding any heap allocation or shared ownership.

The `SHAMapSyncFilter` base class deletes its copy constructor and copy-assignment operator. `AccountStateSF` inherits this non-copyability, reinforcing that it is a thin, stack-scoped adapter rather than an ownable resource.