# `RPCHandler.h` — RPC Command Dispatch Interface

This header is the public face of the XRPL RPC command dispatch layer. It exposes exactly two free functions that together cover the entire lifecycle of an RPC request: executing the command and pre-checking what permission level it requires.

## What This File Does

`RPCHandler.h` sits at the boundary between the transport layer (HTTP and WebSocket servers) and the per-command handler implementations scattered throughout `xrpld/rpc/handlers/`. Its two declarations are the only entry points needed by the rest of the system to drive the full RPC pipeline.

```cpp
Status doCommand(RPC::JsonContext&, Json::Value&);
Role   roleRequired(unsigned int version, bool betaEnabled, std::string const& method);
```

The corresponding implementation lives in `detail/RPCHandler.cpp`, which is deliberately hidden from callers — `Handler.h`, `Tuning.h`, and the command table are all detail-layer internals that nothing outside the RPC subsystem needs to see.

## `doCommand` — The Dispatch Loop

`doCommand` accepts a fully-populated `RPC::JsonContext` and an output `Json::Value`, resolves the command name from the incoming parameters, enforces every gate in sequence, and calls the matched handler. The function encapsulates several distinct responsibilities that would otherwise be scattered across each handler:

**Load shedding.** Before doing anything else, the internal `fillHandler` helper counts jobs at `jtCLIENT` priority or higher. If the queue exceeds `Tuning::maxJobQueueClients`, it returns `rpcTOO_BUSY` immediately. Clients with `ADMIN` or `IDENTIFIED` roles bypass this check via `isUnlimited()`, ensuring operators retain access under load.

**Command resolution.** The function extracts the command name from either the `command` or `method` JSON field (both forms are accepted for compatibility). If both fields are present with differing values, the request is rejected as `rpcUNKNOWN_COMMAND`. The name is passed to `getHandler(version, betaEnabled, name)` which consults a static table of `Handler` structs.

**Role enforcement.** If the matched `Handler` carries `Role::ADMIN` and the caller's `context.role` is anything less, `fillHandler` returns `rpcNO_PERMISSION`. This check deliberately happens before network condition checks — no reason to inspect ledger state for a caller that cannot execute the command regardless.

**Network condition gates.** The `conditionMet()` call in `Handler.h` checks amendment-blocked status, UNL-blocked status, the node's `OperatingMode`, and validated-ledger age. Commands requiring `NEEDS_NETWORK_CONNECTION`, `NEEDS_CURRENT_LEDGER`, or `NEEDS_CLOSED_LEDGER` will fail with appropriate error codes if the node is not ready. Importantly, the error codes themselves are version-sensitive: API v1 gets legacy names like `rpcNO_NETWORK` and `rpcNO_CURRENT`, while API v2 normalises these to `rpcNOT_SYNCED`.

**Instrumented dispatch.** The internal `callMethod` helper calls the handler's `valueMethod_` function inside a `try`/`catch`. It records start/finish/error events in the `PerfLog`, creates a `JobQueue` load event for timing, and logs elapsed time at debug level. Unhandled exceptions are caught and translated to `rpcINTERNAL`, and if the request was priced at `feeReferenceRPC` the load charge is escalated to `feeExceptionRPC`.

The response shape differs between HTTP and WebSocket transports — HTTP wraps `status` inside the `result` object, WebSocket places it outside — but `doCommand` is transport-agnostic: it only fills the `result` `Json::Value`, leaving the envelope construction to the server layers above it.

## `roleRequired` — Pre-Authorisation Probe

`roleRequired` is a pure lookup: given a method name, API version, and the `betaEnabled` flag, it returns the `Role` the caller must hold to execute that command. If the method does not exist in the handler table, it returns `Role::FORBID`.

The function is called by the server layer *before* the request is queued or dispatched, allowing transport-level code to reject unauthorised requests immediately — without allocating any RPC context or touching the job queue. This is the correct place for that check: `doCommand` also re-validates the role internally, but having the server layer do an early reject avoids unnecessary work.

## The `Role` Model

The `Role` enum (`GUEST`, `USER`, `IDENTIFIED`, `ADMIN`, `PROXY`, `FORBID`) is defined in `Role.h`. `ADMIN` is the only role that unlocks administrative RPC commands; `IDENTIFIED` and `ADMIN` are both "unlimited" for load-shedding purposes. The `FORBID` sentinel returned by `roleRequired` for unknown methods signals that the connection should be closed rather than queued.

## Design Philosophy

The header's minimal surface — two free functions, no classes — reflects a deliberate separation between the stable external contract and the volatile implementation details. The `Handler` table, tuning constants, and condition-checking machinery are all confined to the `detail/` subdirectory. Any file that needs to dispatch or pre-check an RPC call includes only this header and `Context.h`; it never needs to know how the handler table is structured or how conditions are evaluated.