# LedgerData.cpp — `ledger_data` RPC Handler

This file implements the `ledger_data` command, which exposes the raw state-node contents of a closed ledger for bulk retrieval. It is one of two mechanisms (alongside `ledger_entry` for single-object lookups) that let external clients download a complete ledger's state. Both a JSON-RPC entry point (`doLedgerData`) and a gRPC entry point (`doLedgerDataGrpc`) are provided in the same file.

## Purpose and Design Context

The XRP Ledger's state database is a sorted map of 256-bit keys to serialized ledger objects (SLEs). The `ledger_data` command exposes a paginated forward scan of this sorted map, returning up to a caller-controlled number of entries per call. The canonical use case is ledger synchronization and archival: a tool can walk the entire state of any available ledger sequentially by following the opaque `marker` token from response to response.

## `doLedgerData` — JSON-RPC Path

After resolving the target ledger via `RPC::lookupLedger`, the handler validates and interprets four optional parameters:

**`marker`** is a hex-encoded 256-bit key that represents the resume position from a previous call. If present, it is decoded directly into a `ReadView::key_type` (a `uint256`). Rejection on malformed input is explicit via `RPC::expected_field_error`.

**`limit`** controls how many entries to return. It defaults to `RPC::Tuning::pageLength(isBinary)`, which is 2048 for binary output and 256 for JSON. Callers without an "unlimited" role (`isUnlimited(context.role)`) have their limit silently clamped to this ceiling; privileged administrative clients may request larger pages.

**`binary`** toggles the output format. In binary mode each entry is serialized via `serializeHex(*sle)`, producing a compact hex blob suitable for efficient transport. In JSON mode each entry is expanded via `sle->getJson(JsonOptions::none)`. The two formats share the same pagination logic but have distinct page-size limits — binary pages are 8× larger than JSON pages, reflecting the much higher cost of JSON serialization.

**`type`** optionally filters the result set to a single `LedgerEntryType` (e.g., only `Offer` objects, only `RippleState` objects). `RPC::chooseLedgerEntryType` maps the string name to the internal enum and returns an error status for unknown types. When type is `ltANY`, no filtering occurs.

### Pagination via Upper-Bound Iteration

The traversal uses `lpLedger->sles.upper_bound(key)`, which returns an iterator to the first state entry with a key strictly greater than the resume marker. On the first call, `key` is the zero hash, so iteration starts from the beginning of the state map.

The "limit exhausted" condition is handled with a deliberate off-by-one: when the counter `limit` reaches zero, the handler sets `marker = sle->key() - 1` and breaks. On the next call, `upper_bound(marker)` lands exactly on `sle->key()` again, so no entry is skipped and no entry is duplicated. This fence-post arithmetic is the only non-obvious invariant in the function.

The entry read inside the loop uses `keylet::unchecked((*i)->key())` — an intentional bypass of keylet validation that is safe here because the key originates from the ledger's own state map, not from untrusted client input.

### First-Page Header Injection

When no marker is present (i.e., this is the first page of a scan), the response includes the full ledger header under the `jss::ledger` key via `getJson(LedgerFill(...))`. Subsequent pages skip this because the caller already has the header and repeating it would add noise to every continuation call.

## `doLedgerDataGrpc` — gRPC Path

The gRPC handler mirrors the JSON path but with several differences reflecting its different calling contract:

**Binary-only output.** Every SLE is serialized with `sle->add(s)` into a `Serializer` buffer, then written raw into the protobuf response. There is no JSON-format option.

**Fixed page limit.** The gRPC handler ignores any limit field in the request and always applies `RPC::Tuning::pageLength(true)` (2048 entries). Role-based limit expansion is absent.

**Bidirectional range.** The gRPC request can include both a `marker` (start key) and an `end_marker` (end key), which sets the iterator endpoint `e` to `ledger->sles.upper_bound(*key)`. This bounds the scan from both ends, enabling the caller to split the keyspace into disjoint ranges and parallelize state downloads — a pattern that has no equivalent in the JSON API.

Error codes map to gRPC status codes: `rpcINVALID_PARAMS` becomes `INVALID_ARGUMENT`; all other failures become `NOT_FOUND`. Marker bytes are written to the response protobuf rather than a JSON string field.

## Resource and Safety Considerations

Both functions hold the ledger alive via `std::shared_ptr<ReadView const>` for the duration of iteration. Because `ReadView` is immutable and the sles map is accessed read-only, no locking is required inside the scan loop. The shared ownership model means the ledger cannot be evicted from memory mid-iteration even if the background ledger manager advances to a new ledger during processing.

The `isUnlimited` role guard on the JSON path protects ordinary clients from requesting arbitrarily large pages that would serialize thousands of SLEs synchronously on the IO thread, while still allowing monitoring tools and internal administrative clients to walk ledgers efficiently.