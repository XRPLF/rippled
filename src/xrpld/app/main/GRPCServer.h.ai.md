# `GRPCServer.h` — Asynchronous gRPC Server for the XRP Ledger API

This header defines the complete infrastructure for running an asynchronous gRPC server inside `xrpld`. It exposes a binary protobuf/gRPC API (`XRPLedgerAPIService` v1) as an alternative to the existing JSON-RPC interface, primarily targeting high-throughput API servers like Clio that need efficient ledger data access. The file contains three cooperating types: the abstract `Processor` interface, the monolithic `GRPCServerImpl` (which houses the async event loop and the template `CallData` request handler), and the thin `GRPCServer` wrapper that owns the background thread.

## The Async Completion Queue Pattern

Rather than using gRPC's simpler synchronous API, the implementation adopts gRPC's async server pattern built around a `grpc::ServerCompletionQueue`. In this model, a `CallData` object does not block waiting for a request — it registers itself as a listener by calling `bindListener_()` at construction time and then returns. The gRPC runtime delivers a notification (via `cq_->Next()`) with a raw pointer tag identifying which `CallData` was triggered. The event loop in `handleRpcs()` casts this tag back to a `Processor*` and decides what to do.

This design avoids one-thread-per-request overhead and integrates naturally with `xrpld`'s existing single-threaded event dispatch discipline.

## `Processor` — the Request Lifecycle Contract

The `Processor` interface enforces a strict three-method protocol: `process()` performs the work (callable only once per instance), `clone()` produces a fresh listener of the same type ready to accept the next request, and `isFinished()` signals when the object is safe to destroy. `Processor` is non-copyable, making the clone-not-copy idiom explicit.

## `CallData<Request, Response>` — One Object Per In-Flight Request

`CallData` is a private template nested inside `GRPCServerImpl`, parameterized by a protobuf request type and its matching response type. Each instance carries its own `grpc::ServerContext`, a `grpc::ServerAsyncResponseWriter`, and the three injected function objects:

- `BindListener` — a member-function pointer such as `AsyncService::RequestGetLedger`, used to re-arm the completion queue for the next incoming request.
- `Handler` — the actual business logic (e.g., `doLedgerGrpc`), returning `std::pair<Response, grpc::Status>`.
- `Forward` — a stub method pointer for proxying the request to a peer node.

**The clone-before-process invariant** is critical. When `handleRpcs()` sees a new request arrive, it calls `clone()` on the `CallData` first — creating a new listener for subsequent requests of the same type — and only then calls `process()` on the original. Without this ordering, the server would stop accepting new requests of that type while one was being processed.

**The `finished_` flag timing** is equally subtle. `process()` sets `finished_ = true` *before* dispatching work to the job queue. The reason: as soon as `responder_.Finish()` or `responder_.FinishWithError()` is called (inside the coroutine), the completion queue returns this same `CallData*` as a tag again (signaling the send completed). The event loop checks `isFinished()` to decide whether to clone-and-process or destroy. If `finished_` were set after the send, there would be a race window where the tag was returned but `isFinished()` still returned false. The flag is `std::atomic<bool>` as a defensive measure even though currently single-threaded, as the comment explains.

## Job Queue Integration

Rather than processing requests inline, `process()` submits a coroutine to `app_.getJobQueue()` as `jtRPC`. This keeps gRPC request handling inside `xrpld`'s cooperative scheduling fabric, sharing thread pool resources and load-shedding logic with JSON-RPC requests. If the job queue is already shut down, `process()` immediately replies with `INTERNAL` error before the coroutine runs.

Inside the coroutine, `process(coro)` assembles a `RPC::GRPCContext<Request>` — the gRPC analog of `RPC::JsonContext`, reusing the same `Context` base struct with a protobuf `params` field instead of `Json::Value`. This context is passed to the registered `Handler`.

## Resource Management and the Secure Gateway

The resource layer is fully integrated. `getUsage()` registers the client's IP with `app_.getResourceManager()` and returns a `Resource::Consumer`. If the consumer's balance exceeds the disconnect threshold (and the client is not unlimited), the request is refused with `RESOURCE_EXHAUSTED` before touching the handler.

Clients connecting from an IP in `secureGatewayIPs_` (configured via `secure_gateway` in `[port_grpc]`) and providing a non-empty `user` field in their request are granted `Role::IDENTIFIED`, which exempts them from resource limits. This allows a trusted Clio proxy to send requests on behalf of many end users without being rate-limited at the gateway level. The `setIsUnlimited()` method uses protobuf reflection to stamp `is_unlimited = true` into the response if the field exists, letting the caller know their elevated status.

## `handleRpcs()` — The Event Loop

`handleRpcs()` runs on its own dedicated thread (`"xrpld: grpc"`). It calls `setupListeners()` to seed one `CallData` per RPC method, then enters a `cq_->Next()` loop. Each iteration yields either a new request (`ok = true`, `isFinished() = false`), a completed send (`ok = true`, `isFinished() = true`), or a cancelled listener during shutdown (`ok = false`). Cancellations and completed objects are erased from the `requests_` vector; new requests cause a clone to be pushed before calling `process()`. The loop exits when `cq_->Next()` returns false, meaning the completion queue has drained after `cq_->Shutdown()`.

## Registered RPC Methods

`setupListeners()` registers exactly four ledger-query RPCs: `GetLedger`, `GetLedgerData`, `GetLedgerDiff`, and `GetLedgerEntry`. All carry `feeMediumBurdenRPC` load and `NO_CONDITION`, meaning they require no special server state (such as a fully synced ledger) before executing. These are deliberately read-only bulk data operations, matching the workload pattern of state-synchronisation tools.

## `GRPCServer` — Lifecycle Wrapper

`GRPCServer` is a thin facade over `GRPCServerImpl`. It holds the impl as a value member (no heap allocation), owns the `std::thread`, and enforces a clean lifecycle contract: `start()` launches `handleRpcs()` on the thread only if `GRPCServerImpl::start()` successfully binds a port; `stop()` calls `impl_.shutdown()` (which drains the queue) and then `thread_.join()`. The destructor asserts `!running_`, which means callers must invoke `stop()` before destroying the object — a hard invariant rather than a silent cleanup.

`getEndpoint()` exposes the actual bound address and port after startup, supporting port `0` in configuration for OS-assigned port allocation, which is useful in test harnesses that need to avoid port conflicts.