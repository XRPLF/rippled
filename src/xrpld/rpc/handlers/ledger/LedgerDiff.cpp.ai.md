# `LedgerDiff.cpp` — gRPC Ledger State Diff Handler

This file implements the single gRPC handler `doLedgerDiffGrpc`, which answers the `GetLedgerDiff` RPC call. Its purpose is to expose the difference between the state maps of two arbitrary validated ledgers — essentially allowing clients to walk forward (or backward) through ledger history by seeing exactly which ledger objects were added, changed, or deleted between any two points. This is primarily used by state-synchronization clients, indexers, and validation tools that need efficient incremental updates rather than fetching full ledger snapshots.

## Ledger Resolution and the Two-Phase Cast

The handler accepts a `GetLedgerDiffRequest` carrying two `LedgerSpecifier` fields — `base_ledger` and `desired_ledger`. Resolution happens in two phases, and the distinction between them is architecturally important.

The first phase calls `RPC::ledgerFromSpecifier`, which resolves the specifier (sequence number, hash, or shortcut like `validated`) into a `std::shared_ptr<ReadView const>`. `ReadView` is the ledger abstraction layer that covers both fully validated closed ledgers and the current open ledger being built. Both would satisfy `ledgerFromSpecifier`.

The second phase downcasts each `ReadView` to `std::shared_ptr<Ledger const>` via `std::dynamic_pointer_cast`. This is not incidental — `Ledger` (as opposed to the general `ReadView` interface) is the concrete class that exposes `stateMap()`, providing access to the underlying `SHAMap` of account state objects. The open ledger does not have a finalized SHAMap in the same sense, and any non-`Ledger` `ReadView` (such as a transient view or the currently-building ledger) will yield a null shared pointer from the cast. The handler reports `NOT_FOUND / "base ledger not validated"` or `"desired ledger not validated"` in that case. Using the cast as a validation gate is cleaner than a separate flag on `ReadView` because it leverages C++ type safety rather than a boolean predicate that could drift out of sync.

## SHAMap Diffing

Once both `Ledger const` objects are in hand, the actual diff is computed by calling `baseLedger->stateMap().compare(desiredLedger->stateMap(), differences, maxDifferences)`. 

`SHAMap::Delta` is a `std::map<uint256, DeltaItem>` where each `DeltaItem` is a `std::pair<SHAMapItem*, SHAMapItem*>` — the first element is the item as it exists in the base map, the second is the item in the desired map. A `nullptr` first element means the key was added; a `nullptr` second element means it was deleted; both non-null means the key was modified with a different serialized object.

`maxDifferences` is set to `std::numeric_limits<int>::max()`, effectively no cap. The `compare()` method still returns `false` if the internal walk exhausts `maxCount`, but with `INT_MAX` this only fires in genuinely pathological cases. The `RESOURCE_EXHAUSTED` gRPC status is preserved in that branch as a defensive measure against unexpected divergence between two supposedly nearby ledgers.

## Response Encoding and the `include_blobs` Flag

The loop over `differences` translates each `SHAMap::Delta` entry into a proto `LedgerObject` message. The key is always emitted (the `uint256` raw bytes identify which state object changed). The data blob — the serialized `STObject` payload — is only emitted when `request.include_blobs()` is true. This two-mode behavior lets callers perform cheap presence checks (just the key set) without the bandwidth cost of fetching every modified object's serialized form.

An important asymmetry: only the *desired* side blob (`inDesired`) is ever included, never the *base* side. Clients learn the new state of each changed or added key, but deleted keys carry no data since there is no desired-state blob. This matches the typical use case (syncing forward to a new ledger state) but would require callers to do an additional object fetch if they need the old value of a modified key.

The `XRPL_ASSERT(inDesired->size() > 0, ...)` before writing the blob guards against a zero-length `SHAMapItem`, which would indicate corruption in the SHAMap tree. Because `SHAMapItem` contains serialized ledger objects, zero bytes is never a valid item, and this assert catches any inadvertent construction of an empty item before it propagates to the wire.

## Error Handling Summary

Four distinct `grpc::Status` error codes are possible:
- `NOT_FOUND` for either specifier resolving to nothing
- `NOT_FOUND` for either specifier resolving to a non-`Ledger` `ReadView`
- `RESOURCE_EXHAUSTED` if `SHAMap::compare` returns false (diff exceeded `INT_MAX` entries)

All are returned by value as part of the `std::pair<Response, grpc::Status>` return type — no exceptions are thrown and no global state is mutated, consistent with the stateless gRPC handler convention used throughout the XRPL RPC layer.