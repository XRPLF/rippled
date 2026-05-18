# `RPCHandler.cpp` — RPC Command Dispatch Engine

## Overview

`RPCHandler.cpp` is the central execution hub for all XRPL RPC commands, shared by both the HTTP and WebSocket transports. When a client sends a JSON request — whether over a REST-style HTTP call or a persistent WebSocket connection — it eventually arrives here for validation, authorization, and dispatch to the appropriate command handler. The file is deliberately thin: it owns the dispatch pipeline but not the business logic of any individual command.

The large block comment at the top of the file captures an important protocol quirk: HTTP and WebSocket responses have structurally different JSON shapes. For HTTP, `status` lives *inside* the `result` object; for WebSocket, it lives *outside* at the top level. `RPCHandler.cpp` doesn't normalize this — that divergence is handled by the transport layers. Its job is to populate the `result` object itself.

## `fillHandler()` — The Pre-Dispatch Gatekeeper

`fillHandler()` is an anonymous-namespace function that performs every validation required before a command handler is invoked. Its responsibilities, in order, are:

**Job queue backpressure.** For non-privileged callers (`isUnlimited` returns false), the function counts all jobs at `jtCLIENT` priority or above and rejects the request with `rpcTOO_BUSY` if the count exceeds `Tuning::maxJobQueueClients` (500). This is the node's primary defense against client-induced job queue saturation. Admin/unlimited clients bypass this gate intentionally — operators need access even under load.

**Command field resolution.** The XRPL protocol accepts the command name under either `"command"` (the canonical JSON-RPC field) or `"method"` (the legacy WebSocket field). `fillHandler()` accepts either, but if a request provides *both* with *different* values it returns `rpcUNKNOWN_COMMAND`. This prevents ambiguous dispatch rather than silently picking one field over the other.

**Handler lookup.** `getHandler(version, betaEnabled, strCommand)` maps the command string to a `Handler` struct, taking the API version and a beta-API flag from the application config. Unknown commands return `nullptr`, producing `rpcUNKNOWN_COMMAND`. The version parameter matters here: handlers declare `minApiVer_` and `maxApiVer_` ranges, so the same command name can map to different implementations across API versions.

**Role-based access control.** If the resolved handler requires `Role::ADMIN`, the caller's `context.role` must match. This is the enforcement point for administrative RPC commands (such as `stop` or `peers`); the role itself was set by the HTTP or WebSocket layer based on the origin IP and authentication credentials.

**Node condition checks.** `conditionMet()` (defined in `Handler.h`) evaluates whether the node is in a suitable state for commands that carry a non-`NO_CONDITION` flag. It guards against amendment-blocked nodes, expired validator lists, insufficient network synchronization, and stale validated ledgers. Importantly, `conditionMet()` is API-version-aware: a v1 caller receives the legacy `rpcNO_NETWORK` error code while a v2 caller receives the more precise `rpcNOT_SYNCED`. This backward-compatibility branching is baked directly into the condition-check layer rather than scattered across individual handlers.

## `callMethod()` — Instrumented Invocation

`callMethod()` is a file-local template function that wraps the actual handler call with observability and exception safety. It generates a monotonically increasing `requestId` via a `static std::atomic<uint64_t>` — a simple but thread-safe counter that produces unique IDs for the performance log without needing a mutex.

The function brackets handler execution with `perfLog.rpcStart()` and `perfLog.rpcFinish()` calls, and also registers a `makeLoadEvent` with the job queue under the name `"cmd:<name>"`. Wall-clock duration is measured with `std::chrono::system_clock` and logged at debug level on completion, providing a lightweight per-call trace in the node's log.

Exception handling is where a subtle resource policy lives: if any `std::exception` escapes the handler, and the request was classified as `feeReferenceRPC` (the standard cost tier), `callMethod()` escalates `context.loadType` to `feeExceptionRPC`. This elevation causes the resource management layer to charge the client a higher fee for the call that caused a server-side crash — an automatic deterrent against requests that abuse the server while appearing within the normal API surface.

After escalation, `inject_error(rpcINTERNAL, result)` writes the generic internal-error JSON into the result object, and the function returns `rpcINTERNAL`. No exception ever propagates out of `callMethod()`, giving the HTTP and WebSocket layers a clean contract.

## `doCommand()` — The Public Entry Point

`doCommand()` is the sole public function in this file (along with `roleRequired()`). It calls `fillHandler()`, injects any gating error into the result, and then invokes `callMethod()` via the handler's `valueMethod_` function pointer. The only branching here is an optional logging path: when HTTP headers carry non-empty `user` or `X-Forwarded-For` values, start/finish log lines bracket the call with that identity. This supports audit logging for API gateways that forward client IP addresses, without burdening the common code path with string construction when headers are empty.

If `handler->valueMethod_` is null (which should not occur for any registered handler but is defensively checked), the function returns `rpcUNKNOWN_COMMAND`. This is belt-and-suspenders protection against a handler registration that omits the method function.

## `roleRequired()` — Out-of-Band Role Query

`roleRequired()` is a utility exposed to callers that need to know a command's access level *before* constructing a full `JsonContext` — for example, to pre-filter WebSocket subscriptions or validate access during connection setup. It delegates to `getHandler()` and returns `Role::FORBID` for unknown commands, making it safe to call for any arbitrary method string.

## Design Tradeoffs

The three-function pipeline — `fillHandler` → `callMethod` → handler — cleanly separates concerns: gating/routing, instrumentation/safety, and business logic. The cost is indirection: every RPC call passes through two layers before touching any domain code. For a latency-sensitive path this matters, but the `makeLoadEvent` and `perfLog` calls indicate that throughput accounting is already the dominant overhead, so the extra function calls are noise.

The shared `static std::atomic<uint64_t> requestId` inside `callMethod()` deserves mention: it is reset to zero only at process start, never wraps in practice for a single node's lifetime, and is incremented unconditionally across all concurrent callers — making it a correct lightweight correlation token for performance logs without requiring a centralized ID service.