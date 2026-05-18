# `ServerState.cpp` — `server_state` RPC Handler

## Role in the System

`ServerState.cpp` implements `doServerState`, the handler for the `server_state` JSON-RPC command. This is one of two symmetric server-introspection endpoints in the XRPL protocol — its twin `ServerInfo.cpp` implements `server_info`. Both handlers delegate almost entirely to `NetworkOPs::getServerInfo`, but differ in one critical flag that controls how numeric values are formatted in the response.

## The Single Distinguishing Decision: `human = false`

The entire behavioral difference between `server_state` and `server_info` lives in the first argument to `getServerInfo`:

```cpp
// ServerState.cpp
context.netOps.getServerInfo(false, isAdmin, hasCounters);

// ServerInfo.cpp
context.netOps.getServerInfo(true, isAdmin, hasCounters);
```

The `bool human` flag instructs `NetworkOPs` to return values in machine-readable form (`false`) versus human-readable form (`true`). For `server_state`, XRP amounts are expressed as raw drops (integer strings), fees as integer basis points, and timestamps as Unix epoch integers — formats suited for programmatic consumption by clients, trading bots, and monitoring tools. `server_info` formats the same data with units spelled out (e.g., `"XRP"` suffixes, floating-point amounts), targeting human operators. The two commands expose the same underlying data through different lenses; there is no separate code path in `NetworkOPs` for each.

## Role-Gated Admin Fields

```cpp
context.role == Role::ADMIN
```

The `role` field in `RPC::Context` is resolved upstream by the RPC framework before the handler is ever called, based on the connection's authentication credentials (IP whitelist, admin password). Passing the boolean result directly to `getServerInfo` means the implementation here makes no trust decisions itself — it simply forwards the already-resolved authorization token. Admin mode unlocks additional fields in the response such as peer counts, internal queue depths, and load factor details that could be sensitive or useful for operator diagnostics but are inappropriate to expose to arbitrary public callers.

## Optional Counters Parameter

```cpp
context.params.isMember(jss::counters) && context.params[jss::counters].asBool()
```

The `counters` request parameter is an opt-in flag. The guard first checks presence with `isMember` before calling `asBool`, avoiding a `Json::LogicError` on missing fields. If present and truthy, `getServerInfo` includes internal performance counters — RPC call counts, ledger validation statistics, and similar instrumentation data. These counters are relatively expensive to serialize and rarely needed outside of debugging sessions, so they are omitted from the default response. The absence of any input-sanitization error path here is intentional: an unrecognized or invalid `counters` value silently evaluates to `false` via the short-circuit `&&`, treating malformed input as opt-out rather than an error.

## Response Shape

The handler wraps the `NetworkOPs` result under the `jss::state` key:

```cpp
ret[jss::state] = context.netOps.getServerInfo(...);
```

This nesting is the externally documented response contract for `server_state`. The parallel `doServerInfo` wraps its result under `jss::info`. Both keys are compile-time string constants from the `jss` namespace, which prevents typo-class bugs through the type system rather than runtime checks.

## Relationship to Sibling Handlers

Within the `server_info/` module, `ServerState.cpp` is the lightest file — a four-line function body with no local logic. The real complexity lives in `NetworkOPs::getServerInfo`, which aggregates ledger state, fee schedules, peer topology, consensus status, and job queue metrics. `ServerState.cpp`'s role is purely to translate the RPC calling convention (extracting role and params from `JsonContext`) into the `NetworkOPs` interface, and to label the result correctly. This separation keeps the RPC layer thin and keeps business logic centralized in `NetworkOPs` rather than scattered across per-handler files.