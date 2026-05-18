# `RPCLedgerHelpers.h` — RPC Ledger Resolution Interface

This header declares the full set of ledger-lookup utilities used by XRPL's RPC layer. Its central job is translating the many ways a caller can describe a ledger — by hash, by sequence number, by shortcut string, or via a gRPC `LedgerSpecifier` — into a concrete `shared_ptr<ReadView const>` that downstream RPC handlers can interrogate. Every public RPC command that operates on ledger state ultimately routes through one of these declarations.

## Why a Separate File

Ledger resolution is subtle enough to justify its own module. The same resolution logic is needed by dozens of handlers (account lookups, transaction queries, ledger data dumps, etc.), for both JSON-over-HTTP and gRPC transports, and for both a fast path that serves from in-memory caches and a slower path that can *acquire* missing ledgers over the peer network. Centralizing this logic prevents drift between the two transports and makes it easy to enforce invariants like staleness checks in one place.

## The `getLedger` Overloads

Three overloads cover every identification strategy:

```cpp
template <class T>
Status getLedger(T& ledger, uint256 const& ledgerHash, Context const&);

template <class T>
Status getLedger(T& ledger, uint32_t ledgerIndex, Context const&);

template <class T>
Status getLedger(T& ledger, LedgerShortcut shortcut, Context const&);
```

The template parameter `T` is almost always `std::shared_ptr<ReadView const>`, but the template keeps the interface open without virtual dispatch. All three return a `Status` whose `operator bool()` is truthy on failure, enabling the `if (auto status = ...)` guard pattern seen throughout the codebase.

The implementation reveals important defensive behaviours. The hash-based overload is the simplest — it delegates directly to `LedgerMaster::getLedgerByHash()` and returns `rpcLGR_NOT_FOUND` if the ledger isn't cached. The sequence-based overload adds a fallback: if `getLedgerBySeq()` misses, it checks whether the requested sequence is the open (current) ledger, because the open ledger by definition has not yet been persisted to the sequence index. After finding a ledger by sequence, it additionally guards against network staleness: if the ledger's sequence exceeds the validated index *and* the last validated ledger is more than two minutes old (`Tuning::maxValidatedLedgerAge`), the ledger is rejected and `rpcNOT_SYNCED` is returned. This prevents a drifted node from confidently serving ledger data it cannot vouch for.

The shortcut overload (`LedgerShortcut::Current`, `Closed`, or `Validated`) checks staleness *first* before fetching anything. For the `Validated` case it calls `LedgerMaster::getValidatedLedger()`. For `Current` and `Closed` it enforces an additional guard: if the returned ledger's sequence is more than 10 behind the validated index, it treats the node as out of sync. This `minSequenceGap` of 10 prevents situations where a node is technically running but deeply behind the chain. Assertions also verify the open/closed state invariants — `Current` must be open, `Validated` and `Closed` must not be.

Error codes are API-version–aware throughout: API v1 callers receive the legacy `rpcNO_NETWORK` / `"InsufficientNetworkMode"` response; API v2+ callers receive `rpcNOT_SYNCED` / `"notSynced"`. This preserves backward compatibility while improving terminology for newer clients.

## The `lookupLedger` Pair

Two overloads serve JSON-RPC handlers:

```cpp
Json::Value lookupLedger(std::shared_ptr<ReadView const>&, JsonContext const&);

Status lookupLedger(std::shared_ptr<ReadView const>&, JsonContext const&, Json::Value& result);
```

The `Status`-returning variant is the canonical form: it parses the request params for `ledger_hash`, `ledger_index`, or the deprecated `ledger` field, resolves whichever was supplied, and populates `result` with `ledger_hash` and `ledger_index` (for closed ledgers) or `ledger_current_index` (for the open ledger), plus a `validated` boolean. The `Json::Value`-returning overload simply wraps this — it creates a local result, calls the canonical form, injects any error via `Status::inject()`, and returns. This two-layer design lets callers that already have a `result` object avoid an extra copy, while callers that just need a fresh response object get a one-liner.

The deprecated `ledger` field is still supported but deliberately kept quiet in error messages — if a client mistakenly supplies both `ledger` and `ledger_hash`, the error message omits `ledger` from the "exactly one of" text to avoid encouraging its use.

## gRPC Path: `ledgerFromRequest` and `ledgerFromSpecifier`

```cpp
template <class T, class R>
Status ledgerFromRequest(T& ledger, GRPCContext<R> const& context);

template <class T>
Status ledgerFromSpecifier(T& ledger,
    org::xrpl::rpc::v1::LedgerSpecifier const&, Context const&);
```

The gRPC transport uses protobuf's `LedgerSpecifier` oneof, which can carry a hash bytes field, a sequence number, or one of the shortcut enum values. `ledgerFromSpecifier` switches on `ledger_case()` and fans out to the appropriate `getLedger` overload. The explicit template instantiations in the `.cpp` file cover `GetLedgerEntryRequest`, `GetLedgerDataRequest`, and `GetLedgerRequest`, keeping link-time instantiation tightly controlled.

## `getOrAcquireLedger`: The Acquire Path

```cpp
Expected<std::shared_ptr<Ledger const>, Json::Value>
getOrAcquireLedger(RPC::JsonContext const& context);
```

This function stands apart from the rest. It is designed for use cases (such as transaction proof queries) where the caller can tolerate waiting for a ledger to be fetched from the network rather than only serving from cache. It is also stricter: it accepts exactly one of `ledger_hash` or `ledger_index` with no shortcut strings and no legacy `ledger` field. The return type uses `Expected<T, E>` (analogous to `std::expected`) rather than the output-parameter plus `Status` idiom, reflecting a newer API design philosophy in the codebase.

For index-based lookup, the function cannot directly store an index→hash mapping without walking the ledger history. It calls `hashOfSeq()` on the validated ledger. If the hash cannot be found there (e.g., the sequence is very old), it falls back to `getCandidateLedger()` to find an intermediate ledger that does carry the needed hash in its skip list, then attempts to acquire that intermediate ledger via `InboundLedgers`. The function reports intermediate acquisition state in the error `Json::Value` (with an `acquiring` field), allowing callers to poll and retry. In standalone mode, where there is no peer network, it falls back to `LedgerMaster::getLedgerByHash()` directly, since the inbound ledger system is non-functional without peers.