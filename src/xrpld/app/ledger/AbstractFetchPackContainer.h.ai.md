# `AbstractFetchPackContainer.h`

## Role

`AbstractFetchPackContainer` is a narrow dependency-inversion interface that exposes a single capability: looking up cached fetch-pack blobs by their `uint256` hash. Its stated purpose — "without an application or ledgermaster object" — explains exactly why it exists. Without this interface, `AccountStateSF` and `TransactionStateSF` would need to hold a reference to the heavyweight `LedgerMaster` (or the full `Application`) just to perform one cache lookup during ledger sync. Instead, they depend only on this minimal abstract type.

## What Fetch Packs Are

During historical ledger acquisition, a catching-up node sends peers a `TMGetObjectByHash` request of type `otFETCH_PACK`. Peers respond with batches of SHAMap nodes needed to reconstruct the missing ledger. `LedgerMaster` stores these blobs in a tagged cache (`fetch_packs_`) keyed by the node's `uint256` content hash. The single method `getFetchPack()` is the only externally visible access point to that cache.

## Interface Design

The interface contains one pure virtual method:

```cpp
virtual std::optional<Blob> getFetchPack(uint256 const& nodeHash) = 0;
```

It returns `std::nullopt` on a cache miss and the raw node data on a hit. The implementation in `LedgerMaster::getFetchPack()` does more than a plain cache lookup — it removes the entry after retrieval (fetch packs are consumed, not reread) and validates data integrity by recomputing `sha512Half` over the blob, discarding and returning `nullopt` if the hash does not match.

## Consumers

Both `AccountStateSF` and `TransactionStateSF` — the `SHAMapSyncFilter` implementations for account-state and transaction trees respectively — accept an `AbstractFetchPackContainer&` at construction and delegate their `getNode()` callbacks directly to it. `SHAMapSyncFilter::getNode()` is called by the SHAMap sync machinery whenever it needs a node that is not already in memory; checking the fetch-pack cache first avoids an unnecessary network round-trip since peers may have pre-populated it. `InboundLedger` bypasses this interface and calls `getLedgerMaster().getFetchPack()` directly, because it already holds a reference to the full application context.

## Why This Abstraction

The separation keeps sync-filter classes testable in isolation: a test double implementing `AbstractFetchPackContainer` can inject arbitrary node data without constructing a live `LedgerMaster`. It also enforces a clear ownership boundary — sync filters are concerned only with *reading* cached blobs, never with how those blobs were fetched or stored.