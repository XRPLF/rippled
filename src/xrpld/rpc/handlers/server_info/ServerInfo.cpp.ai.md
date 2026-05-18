# `ServerInfo.cpp` — `server_info` RPC Handler

## Role in the System

`ServerInfo.cpp` implements the `doServerInfo` function, which is the handler for the `server_info` JSON-RPC command — one of the most commonly called RPC endpoints on an XRPL node. The command returns a comprehensive snapshot of a node's operating state: build version, server health, ledger positions, peer counts, amendment warnings, and optional performance counters. Operators, monitoring systems, and client libraries call this endpoint to determine whether a node is healthy and synchronized with the network.

## Architecture: Thin Handler, Deep Delegation

The entire handler body is three lines of logic. `doServerInfo` constructs an empty JSON object, calls `context.netOps.getServerInfo(...)`, and wraps the result under the `jss::info` key. This reflects the RPC framework's philosophy: handlers are routing glue, not logic containers. All substantive assembly of server state lives in `NetworkOPsImp::getServerInfo()` inside `NetworkOPs.cpp`, which spans hundreds of lines gathering warnings, ledger data, peer statistics, consensus state, and optional counters.

The decision to push complexity down to `NetworkOPs` rather than implement it in the handler is deliberate. `NetworkOPs` already holds references to all the subsystems (consensus, validators, ledger master, fee tracking) needed to produce the response. A handler that reached directly into those subsystems would create a web of coupling; instead, `NetworkOPs` provides a single aggregation interface.

## The `human` Flag: `server_info` vs. `server_state`

The first argument to `getServerInfo` is the boolean `human`, and this is the key architectural distinction between `ServerInfo.cpp` and its sibling `ServerState.cpp`. `doServerInfo` passes `true`, `doServerState` passes `false`. When `human` is `true`, `getServerInfo` includes fields like `hostid` (a human-readable machine identifier) and formats data for readability. When `false`, it emits numeric, machine-friendly representations suitable for programmatic parsing. Both handlers share identical logic for the `role` and `counters` arguments — the only difference is this single flag and the JSON key used for the response (`jss::info` vs. `jss::state`).

## Role-Based Information Gating

The second argument, `context.role == Role::ADMIN`, controls a significant branch inside `getServerInfo`. Non-admin callers receive a subset of information — notably, amendment-majority warnings (useful for operators who need to upgrade before being blocked) and node configuration details like `node_size` are withheld from unprivileged callers. The role is established upstream by the RPC framework via `requestRole()` before the handler is ever invoked; the handler itself simply reads and propagates the already-resolved value. This means `doServerInfo` never performs authentication — it only makes a boolean authorization decision on already-authenticated context.

## Optional Counters Parameter

The third argument enables an optional `counters` sub-object containing performance metrics (job queue depths, RPC call counts, etc.). The handler checks `context.params.isMember(jss::counters)` before calling `asBool()` on it, which is the correct defensive pattern: calling `asBool()` on a missing field would be undefined behavior in this JSON implementation. If the field is present but non-boolean, `Json::Value::asBool()` raises a `Json::LogicError`. This is an acceptable exception path since such a request is malformed.

## Relationship to Sibling Handlers

The `server_info/` directory groups a cluster of closely related read-only node-introspection handlers: `Feature.cpp` (amendment status), `Fee.cpp` (current fee schedule), `Manifest.cpp` (validator manifests), `ServerDefinitions.cpp` (protocol field definitions), `ServerInfo.cpp`, `ServerState.cpp`, and `Version.h`. These handlers are intentionally thin to keep the RPC surface uniform — each wraps one or two calls into subsystems that do the real work. `ServerInfo.cpp` and `ServerState.cpp` in particular are near-duplicates differing only in the `human` flag and response key, reflecting that `server_info` and `server_state` exist as two views over the same underlying data source.