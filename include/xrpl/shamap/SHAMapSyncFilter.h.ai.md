# `SHAMapSyncFilter` — Callback Interface for SHAMap Node Synchronization

`SHAMapSyncFilter` is a two-method abstract interface that sits at the boundary between the low-level SHAMap tree-traversal engine and the higher-level infrastructure that manages node persistence, caching, and fetch packs. It exists because the SHAMap code needs to be decoupled from knowledge about *where* nodes come from or *where* they should go when received — those concerns belong to the application layer.

## The Synchronization Protocol

When a rippled node is downloading a ledger from peers, the SHAMap it is constructing begins sparsely populated. As it walks the tree looking for missing nodes (`getMissingNodes`), it may encounter hashes for which the node data is neither in its in-memory cache nor in its backing database. The filter provides a third source: transient data obtained from peer fetch packs, in-flight consensus caches, or similar ephemeral stores.

The interface captures this with exactly two pure virtual methods:

**`getNode(nodeHash)`** — Called when the SHAMap needs a node that couldn't be resolved locally. If the filter has the raw serialized data for this hash, it returns it as an `std::optional<Blob>`. Returning `std::nullopt` means the filter cannot help; the node is genuinely missing.

**`gotNode(fromFilter, nodeHash, ledgerSeq, nodeData, type)`** — Called after a node has been successfully obtained, regardless of source. The `fromFilter` flag distinguishes the two cases: `true` when the data originated from this filter's own `getNode()` call, `false` when it arrived from the network (i.e., via `addRootNode` or `addKnownNode`). This distinction matters because a `false` call is the signal for the filter to persist the node somewhere durable; a `true` call would be redundant to re-store.

The internal `SHAMap::checkFilter()` function makes the two-step contract explicit: it calls `getNode()` to retrieve, deserializes and validates the data, and then immediately calls `gotNode(true, ...)` to notify the filter that the node was consumed. When `addRootNode` or `addKnownNode` receives a fresh node from a peer, they call `gotNode(false, ...)` directly without going through `getNode()`.

The `nodeData` parameter to `gotNode()` is passed by rvalue reference (`Blob&&`), and the comment "nodeData is overwritten by this call" reflects that the implementation is free to move or destroy the buffer — callers must not rely on the contents afterward.

## Concrete Implementations

Three concrete subclasses cover the ledger sync scenarios:

- **`AccountStateSF`** — used when syncing an account-state SHAMap. It holds references to a `NodeStore::Database` (for durable storage) and an `AbstractFetchPackContainer` (for the transient fetch pack received from a peer). On `gotNode(false, ...)`, it writes to both. On `getNode()`, it checks the fetch pack.

- **`TransactionStateSF`** — structurally identical to `AccountStateSF` but applied to the transaction tree of a ledger. Both classes are described as "only needed on add functions", meaning they only participate in the write path (`addRootNode` / `addKnownNode`), not in the read path (`getMissingNodes`).

- **`ConsensusTransSetSF`** — used during consensus to build transaction sets. Unlike the ledger sync filters, this one is "needed on both add and check functions" because the underlying data source is a transient in-memory `TaggedCache<SHAMapHash, Blob>` rather than a persistent store. It provides nodes from that cache during `getMissingNodes` and writes newly learned nodes back into it.

## Design Rationale

The interface is non-copyable (deleted copy constructor and assignment operator) because its implementations hold non-owning references to databases and caches that have independent lifetimes. Copying would create dangling-reference hazards with no benefit.

The two-method split (pull vs. notify) avoids requiring filters to implement any tree logic: they operate purely in terms of flat `Blob` data keyed by `SHAMapHash`. The SHAMap engine handles all deserialization, canonicalization, and tree structure; the filter only sees opaque byte buffers. This keeps the filter implementations small and testable in isolation.

The `ledgerSeq` argument passed to `gotNode()` allows filter implementations to associate stored nodes with a specific ledger sequence number, enabling them to make intelligent decisions about expiry or prioritization without requiring the filter to maintain its own sequence tracking.