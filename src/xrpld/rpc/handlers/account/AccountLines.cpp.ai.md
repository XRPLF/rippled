# AccountLines.cpp

This file implements the `account_lines` RPC command for the XRP Ledger, exposing an account's trust lines (IOU credit relationships) to external callers. It is one of the most frequently-called read-only RPC handlers because trust lines are the fundamental mechanism by which non-XRP assets circulate on the ledger.

## Role in the System

Trust lines live in the ledger as `ltRIPPLE_STATE` entries stored inside each account's owner directory. The `account_lines` command walks that directory, filters and shapes each entry, and returns a paginated JSON array. Two functions divide the work cleanly: `addLine()` serializes one trust line into JSON, and `doAccountLines()` handles the full request lifecycle.

## `addLine()` — Trust Line Serialization

`addLine()` appends one `RPCTrustLine` to the JSON array, producing the canonical shape of every element in the `lines` response array. The balance field follows XRPL's sign convention: a positive value means the requesting account holds the peer's IOUs (it is owed value), while a negative value means the peer holds the requesting account's IOUs (the account is the issuer side of that balance).

Flag fields — `authorized`, `peer_authorized`, `no_ripple`, `no_ripple_peer`, `freeze`, `freeze_peer`, `deep_freeze`, `deep_freeze_peer` — are emitted only when `true`. This is a deliberate compactness choice: omitting false flags reduces response size substantially when most lines are in a neutral state. A caller that needs to distinguish "not set" from "set to false" can rely on the absence of the key as the false case.

`RPCTrustLine` is the `TrustLineBase` subclass purpose-built for this handler. Unlike its sibling `PathFindTrustLine` (used by the path-finding engine where millions of instances are allocated and memory pressure is critical), `RPCTrustLine` carries four extra `Rate` fields for `quality_in` and `quality_out`. These quality values are only needed by the RPC layer, not by the payment engine, which motivates keeping them out of `PathFindTrustLine` entirely.

The `TrustLineBase` type stores every trust line from the perspective of a nominated "view account" by recording a `mViewLowest` boolean at construction time. Internally, XRPL orders trust line entries by account ID (numerically lower account is the "low" side). `mViewLowest` normalizes all flag and limit accessors so that `getAuth()`, `getFreeze()`, `getLimit()`, etc., always return the value relative to the requesting account regardless of which side of the ledger entry it occupies.

## `doAccountLines()` — Request Handler

### Input Validation

Validation is layered. The function first checks that `account` is present and is a string, then calls `RPC::lookupLedger()` to resolve the requested ledger version. `parseBase58<AccountID>()` decodes the account string and `ledger->exists(keylet::account(accountID))` confirms the account actually exists in that ledger. A missing account returns `rpcACT_NOT_FOUND` rather than an empty lines array, making the distinction explicit.

The optional `peer` parameter follows the same base58 validation path. If the parameter is provided but fails to parse, `rpcACT_MALFORMED` is injected and the request is rejected. This prevents a caller from passing a malformed peer value and receiving silently-empty results.

`readLimitField()` applies the tuning constants from `RPC::Tuning::accountLines` — a minimum of 10, a default of 200, and a maximum of 400. Values outside this range are clamped or rejected with an error.

### `ignore_default` Filtering

The `ignore_default` flag lets callers skip trust lines that are in the default state on the requesting account's side. A line is default on that side when neither `lsfLowReserve` nor `lsfHighReserve` is set (depending on whether the account is the low or high party). A default-state line does not consume an owner reserve slot, making it effectively invisible to most balance-sheet analyses. Callers indexing "active" trust lines can set `ignore_default: true` to avoid iterating through many zero-balance, unfunded lines.

The flag check uses the raw `sfLowLimit` issuer field to determine which side the account is on rather than going through `RPCTrustLine`, because at filter time the handler wants to avoid constructing the full wrapper object for lines that will be discarded anyway.

### Cursor-Based Pagination

Pagination uses `forEachItemAfter()` from `DirectoryHelpers.h`, which traverses the account's owner directory starting just after a given 256-bit key. The handler requests `limit + 1` items from the iterator. If the callback fires exactly `limit + 1` times, there are more items; the marker is set at the limit-th item's key and emitted in the response.

This off-by-one pattern is the canonical XRPL pagination idiom: request one extra item to detect whether a next page exists, but only collect up to `limit` results. The double-check `count == limit + 1 && marker` guards against an edge case where the limit-th item happens to be the last item in the directory — in that case `count` reaches `limit + 1` only if the callback was actually called that many times.

The marker itself is a comma-separated string encoding a hex-encoded key and a 64-bit page hint (`"<hex_key>,<uint64_hint>"`). The hint is used by `forEachItemAfter()` to skip directly to the directory page containing the resume point rather than scanning from the beginning. The hint is derived from `getStartHint()`, which reads the page-index metadata embedded in the owner directory SLE.

### Marker Security

Before resuming a paginated request, the handler reads the SLE at the marker's key with `ledger->read({ltANY, startAfter})` and then calls `RPC::isRelatedToAccount()` to verify that object belongs to the account in the request. Without this check, a malicious caller could supply a valid marker pointing into another account's owner directory and receive entries that should not be accessible through this account's namespace. The check returns `rpcINVALID_PARAMS` on failure, which also covers the case where the marker points to a ledger object that no longer exists (e.g., because the trust line was deleted between pages).

### Resource Metering

The handler sets `context.loadType = Resource::feeMediumBurdenRPC` unconditionally before returning. This signals to the resource management layer that the request warrants moderate throttling. Walking an owner directory and constructing wrapper objects for hundreds of trust lines is meaningfully more expensive than a point lookup, but less than operations requiring cryptographic verification or path finding.