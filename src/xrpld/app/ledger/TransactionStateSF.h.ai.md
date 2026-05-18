# `TransactionStateSF` — Transaction Tree Sync Filter

## Role in the System

`TransactionStateSF` is a narrow adapter class that bridges the SHAMap synchronization machinery with the persistent node store and the fetch-pack cache during ledger acquisition. Every validated XRPL ledger contains two Merkle trees: an account-state tree and a transaction tree. When a node is catching up and has to fetch a ledger it does not yet have locally, it must reconstruct both trees node-by-node from peers. `TransactionStateSF` is the callback filter attached to the transaction tree during that process, while its structurally identical sibling `AccountStateSF` covers the account-state tree.

The in-file comment — *"this class is only needed on add functions"* — captures the design intent precisely: the filter is not consulted during read-only traversal of a fully-present map. It only activates when nodes are being inserted into a SHAMap that is being rebuilt, making it a pure synchronization concern.

## Inheritance and Interface

The class inherits from `SHAMapSyncFilter`, a non-copyable abstract callback defined in `include/xrpl/shamap/SHAMapSyncFilter.h`. That interface declares exactly two pure virtual methods:

- `gotNode()` — called by the SHAMap engine after a node has been successfully received and decoded, giving the filter a chance to persist it.
- `getNode()` — called when the SHAMap engine needs a node that it does not have in memory; the filter may return it from a local cache.

The non-copyable constraint on `SHAMapSyncFilter` (deleted copy constructor and assignment) is inherited by `TransactionStateSF`, which is appropriate because both held members are references — copying would silently alias the same resources without updating the reference targets.

## Constructor and Dependencies

The constructor takes two references:

```cpp
TransactionStateSF(NodeStore::Database& db, AbstractFetchPackContainer& fp)
```

`NodeStore::Database` is the persistent key-value store where all ledger objects (ledger headers, account nodes, transaction nodes) ultimately live. `AbstractFetchPackContainer` is a thin interface that decouples the filter from `LedgerMaster` and `Application`; it exposes a single `getFetchPack(uint256)` method that probes a short-lived, peer-sourced cache of raw blob data called a *fetch pack*. This abstraction exists specifically to avoid pulling the heavyweight `Application` object into a context where only fetch-pack access is needed.

In practice, as seen in `InboundLedger.cpp`, callers pass `mLedger->txMap().family().db()` as the database and `app_.getLedgerMaster()` (which implements `AbstractFetchPackContainer`) as the fetch pack container.

## `gotNode()` — Persisting Received Nodes

```cpp
void gotNode(bool, SHAMapHash const& nodeHash, std::uint32_t ledgerSeq,
             Blob&& nodeData, SHAMapNodeType type) const override;
```

The implementation stores the arriving node into the database with the `hotTRANSACTION_NODE` type tag. The `bool fromFilter` parameter is ignored here — it signals whether the node originated from the filter's own `getNode()` call or arrived directly from a peer, but the persistence action is identical either way.

A notable defensive detail is the `XRPL_ASSERT` that rejects `SHAMapNodeType::tnTRANSACTION_NM` (transaction without metadata). After a ledger closes, its transaction tree holds `tnTRANSACTION_MD` entries (transactions with attached metadata); bare non-metadata transaction nodes should not appear in this context. The assertion catches any mismatch at development time without incurring a runtime check in production.

The `nodeData` parameter is taken by rvalue reference and moved directly into `db_.store(...)`, avoiding any unnecessary copy of what may be a large blob.

## `getNode()` — Serving Nodes from the Fetch-Pack Cache

```cpp
std::optional<Blob> getNode(SHAMapHash const& nodeHash) const override;
```

The implementation delegates entirely to `fp_.getFetchPack(nodeHash.as_uint256())`. Fetch packs are peer-provided bundles of ledger object data, distributed by nodes that have the full ledger to nodes that are catching up. If the hash is present in the cache, the blob is returned; otherwise `std::nullopt` signals to the SHAMap engine that it must request the node from a peer directly.

## Relationship to `AccountStateSF`

`AccountStateSF` is structurally identical — same constructor signature, same two-reference layout, same delegation pattern — but its `gotNode()` stores nodes as `hotACCOUNT_NODE` rather than `hotTRANSACTION_NODE`. The two classes exist as separate types so that the SHAMap engine can receive a single typed filter pointer and the correct storage tag is applied automatically, without any runtime branching. The duplication is intentional: each filter is permanently bound to one of the two tree roles a ledger has.

## Usage in `InboundLedger`

`InboundLedger.cpp` creates `TransactionStateSF` instances on the stack in four locations: when fetching the transaction tree root, when finding missing nodes, when adding a received root node, and when enumerating still-needed hashes. Each instance is short-lived and stack-allocated, reflecting that the filter holds no ownership — it is a view over the database and the fetch-pack container that exist elsewhere for the ledger's lifetime.