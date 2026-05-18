# `AccountStateSF` — Account State SHAMap Sync Filter

`AccountStateSF` is a concrete implementation of `SHAMapSyncFilter` dedicated to the **account state tree** during ledger synchronization. Its entire purpose is to answer two questions that the generic SHAMap sync machinery cannot: "what do I do when I receive a new node?" and "where do I look for a node I don't have yet?" The class lives at the seam between the SHAMap layer (which is deliberately kept storage-agnostic) and the application-level infrastructure that owns persistent node storage and ephemeral peer-supplied data.

## Role in Ledger Acquisition

When an XRPL node is catching up to the network, it reconstructs missing ledgers by fetching their constituent SHAMap nodes from peers. The `InboundLedger` subsystem drives this process and constructs `AccountStateSF` instances on the stack — for example, when calling `fetchRoot()` or `neededStateHashes()` on the account state map. The sync filter is short-lived: it exists only for the duration of a single acquisition step and is discarded once the call returns. This usage pattern explains why the constructor takes references rather than shared ownership: the caller — `InboundLedger` — is responsible for ensuring that both the `NodeStore::Database` and the `AbstractFetchPackContainer` outlive the filter.

## The Two Filter Callbacks

The `SHAMapSyncFilter` base class defines exactly two pure virtual methods, and `AccountStateSF` provides the account-state-specific semantics for each.

`gotNode()` is called by SHAMap sync code when it has successfully received a node from a peer. `AccountStateSF`'s implementation unconditionally forwards it to `db_.store()`, tagging it as `hotACCOUNT_NODE`. This tag is significant: it tells the node store that this blob is an account state trie node, enabling type-specific storage and eviction policies. The `fromFilter` boolean (indicating whether the node came from the filter's own cache rather than a remote peer) and the `SHAMapNodeType` are both ignored — the implementation stores regardless of origin, since durability is always the right behavior here. The node data is moved in, reflecting a zero-copy handoff to the database.

`getNode()` is called when sync code needs a specific node that isn't yet in the local store. Rather than attempting a database read (which would be slow and is unlikely to help during an in-progress acquisition), it consults the **fetch pack** via `fp_.getFetchPack()`. Fetch packs are short-lived, peer-assembled blobs of ledger data: a peer bundles up the nodes needed to complete a ledger acquisition and sends them ahead of explicit requests. The fetch pack container holds these in memory temporarily, making them available through this call. Returning `std::nullopt` signals that the node isn't available locally and must be explicitly requested from peers.

## Dependency Design

The constructor takes `AbstractFetchPackContainer&` rather than a direct reference to `LedgerMaster`. This is a deliberate narrowing of the dependency. `AccountStateSF` only needs one thing from `LedgerMaster`: the ability to look up fetch pack blobs by hash. Binding it to the full `LedgerMaster` would introduce a broad coupling between the sync filter and the application's central ledger management object. The `AbstractFetchPackContainer` interface — a single-method pure virtual — expresses the minimum required contract. `LedgerMaster` happens to implement it, so the usage in `InboundLedger.cpp` passes `app_.getLedgerMaster()` directly, but the filter itself knows nothing of that.

## Relationship to Sibling Filters

`AccountStateSF` has a structural twin: `TransactionStateSF`, which fulfills the same role for the **transaction tree** of a ledger. Together they cover the two SHAMap trees that compose a complete XRPL ledger. Both implement `gotNode()` and `getNode()` with identical strategies but different `hotType` storage tags. A third related class, `ConsensusTransSetSF`, handles transaction sets during active consensus — a distinct use case where nodes go into a transient cache rather than the persistent node store. The existence of these three classes reflects the principle that storage policy is per-tree and per-phase, and the sync filter interface provides the customization point without burdening the generic SHAMap code with those distinctions.