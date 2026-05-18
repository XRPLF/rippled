# GRPCServer.cpp

## Role and Context

`GRPCServer.cpp` implements the asynchronous gRPC server that runs alongside the existing JSON/HTTP RPC interface in `xrpld`. It exposes a small set of ledger-data RPCs — `GetLedger`, `GetLedgerData`, `GetLedgerDiff`, and `GetLedgerEntry` — over Google's Protocol Buffer-based transport. The server is optional and only activates when a `[port_grpc]` section is present in the node's configuration file, meaning existing deployments are unaffected.

## Architecture: Three Layers

The implementation is split across three collaborating types.

**`GRPCServer`** is the public RAII wrapper. It owns a `std::thread` and a `GRPCServerImpl`, and presents a clean `start()` / `stop()` interface. The destructor asserts `running_ == false`, making it a programming error to destroy the server without first stopping it. The thread is named `xrpld: grpc` via `beast::setCurrentThreadName`.

**`GRPCServerImpl`** handles everything at the gRPC infrastructure level: parsing configuration, building the gRPC server, owning the completion queue (`cq_`), and running the `handleRpcs()` event loop. The constructor reads `ip`, `port`, and the optional `secure_gateway` list from the `[port_grpc]` config section. The `secure_gateway` field accepts a comma-separated list of IP addresses; these IPs receive privileged access (see below). If either `ip` or `port` is absent, `serverAddress_` stays empty and `start()` returns false without binding.

**`CallData<Request, Response>`** is a private inner template class of `GRPCServerImpl` that captures the state needed for one in-flight or pending RPC. It inherits from both `Processor` (the polymorphic interface used by the event loop) and `std::enable_shared_from_this` (so it can capture itself in the coroutine lambda safely). Each instantiation is bound to a concrete protobuf Request/Response pair and carries three function objects: `bindListener_` (how to register with the async service), `handler_` (how to populate a response), and `forward_` (stub method for forwarding, though forwarding logic is declared in the header but not exercised in the current `.cpp` paths).

## Completion Queue Event Loop

The gRPC async server pattern used here is the canonical "reactor on a completion queue" approach, but implemented manually. The flow during normal operation:

1. `setupListeners()` creates one `CallData` per RPC type and calls `bindListener_` on each, registering them with the async service and completion queue. Each idle `CallData` acts as a listener token.
2. `handleRpcs()` loops on `cq_->Next(&tag, &ok)`. The `tag` is the raw pointer of whichever `CallData` has an event.
3. When a request arrives (`ok == true`, `!ptr->isFinished()`), the loop calls `ptr->clone()` first to create a replacement listener, then `ptr->process()` to begin handling the request. This ensures the server never goes deaf while a request is in flight.
4. When a response has been sent (`ok == true`, `ptr->isFinished()`), the loop erases the pointer from the live set, destroying the `CallData`.
5. When the server is shut down, idle listeners are cancelled and returned with `ok == false`, causing the loop to erase them immediately. Active requests must complete (send a response) before `cq_->Next()` finally returns false and the loop exits.

The comment in `handleRpcs()` is explicit about why the requests collection is a `vector` rather than an `unordered_set`: deletion requires a `shared_ptr`, but `cq_->Next()` hands back a raw pointer as tag, and the erase lambda does a linear scan and swap-with-back to avoid an O(n) shift.

## The `finished_` Flag Timing Invariant

The two-phase `process()` design contains a subtle but critical ordering constraint. The public `process()` (called from the event loop) sets `finished_ = true` *before* dispatching work to the job queue. This is necessary because `responder_.Finish()` inside the worker coroutine posts a completion event back to `cq_`, and by the time `handleRpcs()` dequeues that event, it must see `isFinished() == true` to know the object should be destroyed rather than treated as a new request. If `finished_` were set after dispatch, a race would exist where the response completion arrives before the flag is visible. The field itself is declared `std::atomic_bool` in the header as a forward-looking safety measure, even though the current single-thread design on the completion queue side does not strictly require atomicity today.

## Request Processing and Resource Enforcement

The actual per-request work runs inside a `JobQueue::Coro` posted with type `jtRPC`. If the job queue has already been stopped, `process()` immediately calls `responder_.FinishWithError` with `INTERNAL`. This prevents new gRPC requests from being accepted during node shutdown.

Inside the coroutine (`process(coro)`), the server:

1. Resolves the `Resource::Consumer` for this client via `getUsage()`, which calls `app_.getResourceManager().newInboundEndpoint(...)` using the decoded client endpoint. If the endpoint cannot be decoded, it throws.
2. Checks whether the client is "unlimited" by verifying that the request contains a non-empty `user` field and that the client IP appears in the `secureGatewayIPs_` list. This two-part check prevents IP-spoofed escalation.
3. If not unlimited, calls `usage.disconnect()`. If the usage balance exceeds threshold, the server responds with `RESOURCE_EXHAUSTED` rather than processing the request.
4. Charges the request's `loadType` (uniformly `feeMediumBurdenRPC` for all four current RPCs) and resolves the `Role`: `IDENTIFIED` for unlimited clients, `USER` otherwise.
5. Checks `RPC::conditionMet()` — the `NO_CONDITION` value used for all current RPCs means this always passes, but the hook is in place for future RPCs that require specific server states (e.g., full history).
6. Calls the handler, receives a `(Response, grpc::Status)` pair, stamps `is_unlimited` on the response if applicable (using protobuf reflection to check whether the response type even has that field), and sends it via `responder_.Finish()`.

## Endpoint Parsing

The file-local `getEndpoint()` helper deals with the variety of formats gRPC uses to represent peer addresses, which may include a scheme prefix such as `ipv4:` or `ipv6:`. The logic finds the first and last `:` in the string: if they differ, there is either an IPv6 address or a scheme prefix, and the helper strips everything up to and including the first colon. The cleaned string is then handed to `beast::IP::Endpoint::from_string_checked`, which returns an empty optional rather than throwing on malformed input. All exceptions at this layer are silently swallowed, and an empty `std::optional` is propagated up to the callers that use it for logging and rate-limiting — a reasonable defensive choice since a peer address that cannot be decoded should not crash the server.

## Shutdown Sequence

`GRPCServerImpl::shutdown()` follows the ordering required by gRPC: `server_->Shutdown()` first, then `cq_->Shutdown()`. Reversing these would cause a hang, because active requests need the server alive to send their responses before the completion queue can drain. The comments in the source spell this out explicitly, as it is a non-obvious gRPC API constraint.

## Configuration Summary

The `[port_grpc]` section supports:
- `ip` — bind address (required)
- `port` — TCP port (required; port 0 is accepted and the actually-bound port is stored in `serverPort_` after `BuildAndStart`)
- `secure_gateway` — comma-separated list of trusted IP addresses whose requests include a `user` field and are exempt from rate limiting

No TLS support is wired in; the server always uses `grpc::InsecureServerCredentials()`. Security for production deployments is expected to be handled at the network or proxy layer.