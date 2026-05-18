# `LedgerRequest.cpp` — Admin RPC Handler for Historical Ledger Acquisition

## Role and Context

`LedgerRequest.cpp` implements `doLedgerRequest`, the server-side handler for the `ledger_request` admin RPC command. Its job is narrowly scoped: locate a specific historical ledger by hash or sequence number — acquiring it from the network if it isn't already cached locally — then return its full JSON representation. Because it can initiate peer-to-peer ledger downloads, it lives under `handlers/admin/data/` and is only accessible to admin-credentialed callers.

This handler is deliberately distinct from the public `Ledger` handler in `handlers/ledger/Ledger.cpp`. The public ledger RPC queries the local validated and recent ledger history. `ledger_request` is the operator's tool for explicitly pulling historical ledgers into a node's local store, making it heavier and more appropriate as an admin operation.

## Resource Cost Declaration

The first thing the handler does is declare its expense:

```cpp
context.loadType = Resource::feeHeavyBurdenRPC;
```

`feeHeavyBurdenRPC` carries a cost weight of 3000 — the highest tier in the resource fee schedule, shared with operations like path-finding and multi-signed submit. This is correct: a call may trigger `InboundLedgers::acquire()`, which can initiate a full network ledger fetch. Setting `loadType` early allows the resource manager to rate-limit or deprioritize the caller even before the work begins.

## The `getOrAcquireLedger` Contract

All the substantive logic is delegated to `RPC::getOrAcquireLedger(context)`, defined in `RPCLedgerHelpers.cpp`. Its signature returns `Expected<std::shared_ptr<Ledger const>, Json::Value>` — the monadic result type that either holds the ledger or an error JSON object, without exceptions or output-parameter mutation.

The function enforces a strict input contract: **exactly one** of `ledger_hash` or `ledger_index` must be present. Providing both, or neither, produces a `rpcBAD_PARAM` error. This is tighter than most XRPL RPC handlers, which treat both as optional and fall back to the current ledger — appropriate here because the caller is explicitly requesting a specific historical ledger.

**Hash path:** The hash string is parsed from hex into a `uint256`. If the string is malformed, an `expected_field_error` is returned immediately. The hash is then passed directly to `InboundLedgers::acquire()` which both checks local caches and, if needed, requests the ledger from peers.

**Index path:** The sequence number path is more complex. First, the handler checks that the node's validated ledger isn't stale (beyond `RPC::Tuning::maxValidatedLedgerAge`), since resolving a sequence to a hash requires a live tip. It then walks the skip-list embedded in recent ledgers (`hashOfSeq`) to translate the requested index into a hash. If the required reference ledger isn't locally available, `getOrAcquireLedger` tries to fetch it from the inbound ledger subsystem and returns an intermediate `rpcLGR_NOT_FOUND` response containing an `acquiring` field — signaling the client that the server is in the process of obtaining the ledger and the request should be retried. Standalone mode (used in development and testing) skips network acquisition and falls back to `LedgerMaster::getLedgerByHash`.

## Serialization

On success, the handler constructs the response:

```cpp
jvResult[jss::ledger_index] = ledger->header().seq;
addJson(jvResult, {*ledger, &context, 0});
```

The `LedgerFill` constructor (in `LedgerToJson.h`) takes the `ReadView` and `context`, with `options = 0` — meaning no transaction dump, no state dump, no expansion. The zero-options default produces a compact header-level JSON representation. The `context` pointer is needed so `LedgerFill` can retrieve the close time for the ledger sequence from `LedgerMaster`. The `ledger_index` field is explicitly set before `addJson` so it's present even in partial or error-adjacent paths.

## Design Observations

The handler's body is only nine executable lines, but this brevity is load-bearing rather than superficial. The `Expected`-based return from `getOrAcquireLedger` means the handler needs no explicit error-code checks or conditional branching — the `if (!res.has_value()) return res.error()` idiom propagates the error JSON directly, whether it is a parameter validation failure, a sync error, or an ongoing acquisition response. This design cleanly separates what the handler cares about (declare cost, acquire ledger, serialize result) from the policy of how ledgers are resolved (encapsulated in the helper).

The handler cannot be called with `current`, `closed`, or `validated` shortcut strings — `getOrAcquireLedger` only accepts explicit hash or numeric index, reinforcing that this command targets historical, potentially off-chain ledgers rather than the live chain state.