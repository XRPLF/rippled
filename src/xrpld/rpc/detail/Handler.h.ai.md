# `src/xrpld/rpc/detail/Handler.h` — RPC Handler Registry and Dispatch Primitives

This header is the definitional core of the XRPL RPC subsystem. It declares the `Handler` struct that represents a single dispatchable RPC method, the `Condition` enumeration that encodes the node-state requirements for calling that method, the gating predicate `conditionMet()` that enforces those requirements at call time, and the lookup functions that connect incoming requests to the right handler. Together with its companion implementation file `Handler.cpp`, it forms the registry that maps every JSON-RPC method name to executable code.

## The `Handler` Struct

`Handler` is a plain aggregate that records everything the dispatch layer needs to know about one RPC endpoint:

```cpp
struct Handler
{
    char const* name_;
    Method<Json::Value> valueMethod_;
    Role role_;
    RPC::Condition condition_;
    unsigned minApiVer_ = apiMinimumSupportedVersion;
    unsigned maxApiVer_ = apiMaximumValidVersion;
};
```

`Method<JsonValue>` is a type alias for `std::function<Status(JsonContext&, JsonValue&)>`. The output is written into the reference parameter rather than returned by value, which avoids an extra copy of the (potentially large) JSON result and lets the function signal success/failure through `Status` independently of the output value.

The `minApiVer_` / `maxApiVer_` range fields are what allow multiple entries with the same `name_` to coexist in the registry for different protocol generations. For example, `ledger_header` is registered only for API version 1 (`{..., 1, 1}`), whereas most handlers span versions 1 through the maximum valid version. When a new handler implementation must behave differently across API versions, two `Handler` entries with non-overlapping ranges can be inserted for the same method name — the `HandlerTable` in `Handler.cpp` asserts at startup that no two entries for the same name have an overlapping version range.

## The `Condition` Enum

```cpp
enum Condition {
    NO_CONDITION           = 0,
    NEEDS_NETWORK_CONNECTION = 1,
    NEEDS_CURRENT_LEDGER   = 1 << 1,
    NEEDS_CLOSED_LEDGER    = 1 << 2,
};
```

This bitmask encodes the minimum node state an RPC endpoint requires. Methods like `fee`, `path_find`, and `submit` need a current ledger; `tx` requires a network connection; `ledger_closed` needs a closed ledger. Methods that operate on historical data or perform local crypto (`channel_authorize`, `sign`, `random`) carry `NO_CONDITION` and are always executable, even on a completely isolated node.

## `conditionMet()` — the Gating Predicate

This function template is the single enforcement point for the `Condition` contract. It checks four independent conditions in order:

1. **Amendment block** — if the node has been amendment-blocked (a supermajority of validators have enabled an amendment this node does not support), any non-trivial RPC call is rejected with `rpcAMENDMENT_BLOCKED`. Allowing reads from an amendment-blocked node would surface ledger state that diverges from the rest of the network.

2. **UNL block** — if the node's validator list has expired, non-trivial calls return `rpcEXPIRED_VALIDATOR_LIST`. The node cannot safely assess which ledger is authoritative without a current UNL.

3. **Network operating mode** — the node must be at least `SYNCING` (as reported by `NetworkOPs::getOperatingMode()`). If it is only `DISCONNECTED` or `CONNECTED`, the call returns `rpcNO_NETWORK` (API v1) or `rpcNOT_SYNCED` (API v2+). The API-version split is intentional: v2 consolidates several legacy error codes into the single `rpcNOT_SYNCED` for a cleaner client experience, while v1 keeps the legacy codes for backward compatibility.

4. **Ledger freshness** — in networked mode (not standalone), the last validated ledger must not be older than `Tuning::maxValidatedLedgerAge` (2 minutes), and the current ledger index must not be more than 10 behind the validated index. If either condition fails, the call returns `rpcNO_CURRENT` (v1) or `rpcNOT_SYNCED` (v2+). The 10-ledger tolerance avoids transient false positives during normal operation.

5. **Closed ledger availability** — if `NEEDS_CLOSED_LEDGER` is set, `LedgerMaster::getClosedLedger()` must return a valid pointer. If not, the response is `rpcNO_CLOSED` / `rpcNOT_SYNCED`.

The function is templated on `T` (the context type) rather than hard-coded to `JsonContext` so that it can be reused with gRPC contexts that share the same `Context` base. All required fields — `app`, `netOps`, `ledgerMaster`, `j`, `apiVersion` — come from the `Context` struct declared in `Context.h`.

## Handler Lookup — `getHandler()` and the `HandlerTable`

`getHandler(version, betaEnabled, name)` is the public entry point for dispatch. Its implementation in `Handler.cpp` delegates to the `HandlerTable` singleton, a `std::multimap<std::string, Handler>` built once at first use. The map is keyed by method name, with `equal_range` used to iterate all entries for that name, and the correct entry selected by version range membership. Passing a version outside `[apiMinimumSupportedVersion, apiMaximumSupportedVersion]` (or `apiBetaVersion` if beta is enabled) immediately returns `nullptr` — the caller interprets that as an unknown or unsupported method.

At construction time, the table loads `handlerArray` (the ~70 statically declared handlers) and then calls `addHandler<LedgerHandler>()` and `addHandler<VersionHandler>()` for the newer class-based handler style. The class-based variant (`handlerFrom<HandlerImpl>()`) reads `name`, `role`, `condition`, `minApiVer`, and `maxApiVer` as static class members and wraps a two-phase `check()` / `writeResult()` dispatch into a `Method<Json::Value>` lambda — providing a more structured pattern for handlers that need clean validation separated from result generation.

The older functional handlers are adapted via the private `byRef()` helper, which wraps a function that returns `Json::Value` into the `Method<Json::Value>` signature that writes to a reference. An `UNREACHABLE` guard asserts that the returned value is always a JSON object, since the RPC protocol only permits JSON objects at the top level.

## `makeObjectValue()` Utility

A small convenience template that constructs a `Json::Value` of object type with a single key-value pair, defaulting the key to `jss::message`. It is used in `byRef()` as a defensive fallback when a legacy handler accidentally returns a non-object JSON value.

## Design Rationale

The split between the header and the implementation file is deliberate. `Handler.h` exposes only the types needed to check and dispatch — `Handler`, `Condition`, `conditionMet()`, and the lookup interface — while the actual handler table (with its 70+ entries and their concrete implementations pulled from `handlers/Handlers.h`) lives entirely in the `.cpp` translation unit. This keeps compilation dependencies tight: code that only needs to *call* `getHandler()` or check conditions does not transitively include every RPC handler's header.