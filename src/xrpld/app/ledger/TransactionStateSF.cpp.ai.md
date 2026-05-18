# `TransactionStateSF.cpp` — Transaction Tree Sync Filter

## Role in the System

`TransactionStateSF` is a thin but semantically important class that bridges the SHAMap synchronization machinery and the node store during ledger acquisition. When a rippled node is catching up and pulling a ledger's transaction tree from peers, the `SHAMap` sync engine needs a two-way hook: a place to *store* nodes that arrive from the network, and a place to *look up* nodes that a peer may have pre-packaged in a fetch pack. `TransactionStateSF` provides both of those hooks, scoped specifically to the transaction tree (`txMap`) as opposed to the account-state tree.

## Relationship to `SHAMapSyncFilter`

The class inherits from `SHAMapSyncFilter`, a non-copyable abstract interface with two pure virtuals: `gotNode()` and `getNode()`. The filter is passed into `SHAMap` add/sync operations so the map can notify the application layer when new nodes are encountered or request cached data before going to the database. `TransactionStateSF` and `AccountStateSF` are the two concrete implementations used during ledger sync; they differ only in the node type tag they write to the database and in one assertion.

## `gotNode()` — Persisting Incoming Nodes

When the SHAMap sync engine receives a tree node from a peer and verifies its hash, it calls `gotNode()` so the application layer can durably store it:

```cpp
db_.store(hotTRANSACTION_NODE, std::move(nodeData), nodeHash.as_uint256(), ledgerSeq);
```

The `hotTRANSACTION_NODE` tag (value `4` in the `NodeObjectType` enum) distinguishes transaction-tree nodes from account-state nodes (`hotACCOUNT_NODE = 3`) and ledger headers (`hotLEDGER = 1`) within the shared node store. This tag is preserved through serialisation and is used by the `DecodedBlob` layer on read-back to reconstitute the correct object type.

Node data is moved — not copied — into `db_.store`, transferring ownership in a single step and avoiding an unnecessary heap allocation.

The `fromFilter` boolean is intentionally ignored (unnamed parameter). It tells the callee whether the data originated from the filter's own `getNode()` call rather than from a peer message, but `TransactionStateSF` doesn't need to distinguish these origins: both paths are equally trustworthy by the time `gotNode()` is invoked.

## The `tnTRANSACTION_NM` Assertion

The single guard in this file asserts that the arriving node's type is *not* `SHAMapNodeType::tnTRANSACTION_NM` (transaction without metadata):

```cpp
XRPL_ASSERT(
    type != SHAMapNodeType::tnTRANSACTION_NM,
    "xrpl::TransactionStateSF::gotNode : valid input");
```

This is not redundant pedantry. In the XRPL data model there are two kinds of transaction leaf nodes: `tnTRANSACTION_NM` (no metadata, used for proposed/standalone transactions) and `tnTRANSACTION_MD` (with metadata, the form committed into closed ledgers). A `TransactionStateSF` instance is only ever constructed for a ledger's *committed* transaction tree. Receiving a `tnTRANSACTION_NM` leaf in that context signals a logic error — likely a misrouted node from the consensus transaction set, which is handled by the separate `ConsensusTransSetSF` filter. The assertion enforces this invariant at the boundary between sync machinery and persistent storage. The companion `AccountStateSF::gotNode()` carries no such assertion because the account-state tree has only one leaf type.

## `getNode()` — Fetch Pack Lookup

```cpp
return fp_.getFetchPack(nodeHash.as_uint256());
```

Before the SHAMap asks the database or the network for a missing node, it calls `getNode()` to check the *fetch pack* — a temporary, peer-supplied cache of ledger nodes for efficient bulk sync. `AbstractFetchPackContainer` is a minimal interface (`getFetchPack(uint256)`) that decouples `TransactionStateSF` from the full `LedgerMaster` object, even though `LedgerMaster` is what actually implements it in production (as seen in `InboundLedger.cpp`). The fetch pack is a speculative optimisation: if the peer predicted which nodes you'd need and packed them in advance, `getNode()` returns them without a round trip. On cache miss it returns `std::nullopt` and the sync engine falls back to requesting the node from the network.

## Usage Context

`TransactionStateSF` is constructed stack-locally in `InboundLedger.cpp` at several call sites, each pairing the transaction map's family database with `app_.getLedgerMaster()` as the fetch pack container:

```cpp
TransactionStateSF filter(mLedger->txMap().family().db(), app_.getLedgerMaster());
```

This scoping is deliberate: the filter lives only for the duration of a single sync pass, holding non-owning references to the database and ledger master, both of which outlive any individual sync operation.