# `GRPCHandlers.h` — gRPC Ledger Handler Declarations

This header is the public interface for XRPL's gRPC API surface. It declares four handler functions that together expose the node's ledger state over Protocol Buffers/gRPC, forming a structured alternative to the traditional JSON-RPC endpoints. Where JSON-over-HTTP serves browser clients and scripts, the gRPC path is intended for high-throughput machine clients — sidechains, data indexers, and sync services — that benefit from typed schemas and binary serialization.

## The Handler Contract

Every function follows an identical signature pattern:

```cpp
std::pair<ResponseType, grpc::Status>
doXxxGrpc(RPC::GRPCContext<RequestType>& context);
```

The return value bundles the protobuf response with a `grpc::Status`. This is a deliberate design choice spelled out in the file's comment: **if the status is not `OK`, only the status is forwarded to the client and the response object is discarded**. This means implementations can construct a partial response without risk — they simply return early with an error status and a default-constructed response, as seen in `doLedgerDiffGrpc` where each failure path does exactly `return {response, errorStatus}`.

## `GRPCContext<T>` — The Context Template

The handlers accept `RPC::GRPCContext<T>`, defined in `Context.h`:

```cpp
template <class RequestType>
struct GRPCContext : public Context
{
    RequestType params;
};
```

`GRPCContext<T>` extends the base `Context` struct — which carries the `Application` reference, `LedgerMaster`, `NetworkOPs`, resource consumer/charge, `Role`, coroutine handle, and `beast::Journal` — by adding a `params` member of the concrete protobuf request type. This mirrors `JsonContext`, which extends the same `Context` base but holds `Json::Value params` instead. Both JSON and gRPC handlers therefore share the same infrastructure for role checking, load accounting, and coroutine dispatch, differing only in how their parameters arrive.

## The Four Handlers

**`doLedgerGrpc`** returns full ledger header information for a specified ledger sequence or hash. This is the gRPC equivalent of the `ledger` JSON-RPC command.

**`doLedgerEntryGrpc`** fetches a single ledger state object by its key. Clients use this to retrieve account root objects, offers, trust lines, and other `SLE` types without pulling the entire state map.

**`doLedgerDataGrpc`** returns a paginated slice of the ledger's state map — the raw SHAMap leaves in serialized form. This is the primary mechanism for external services to perform full ledger replication, one page at a time.

**`doLedgerDiffGrpc`** computes the difference between two ledgers by comparing their state SHAMaps. The implementation in `LedgerDiff.cpp` calls `baseLedger->stateMap().compare(desiredLedger->stateMap(), differences, maxDifferences)` and encodes each changed, added, or deleted object into the response. This is purpose-built for light clients and clio-style indexers that need to track incremental ledger mutations without replaying the full transaction set.

## Wiring into the Server

The declarations here are consumed by `GRPCServer.h` and `GRPCServer.cpp`, which implement the asynchronous gRPC completion-queue event loop. `GRPCServerImpl::CallData<Request, Response>` is a template that stores a `Handler<Request, Response>` — a `std::function` pointing to one of these four declarations. When a request arrives on the completion queue, `CallData::process()` posts a coroutine job to the `JobQueue`, constructs a `GRPCContext<Request>` by populating all `Context` fields from the server's `Application`, and invokes the stored handler. The result's `grpc::Status` drives whether `responder_.Finish(result, status, ...)` or `responder_.FinishWithError(status, ...)` is called, enforcing the error-suppresses-response contract at the transport layer rather than in each handler.

Resource exhaustion, role checks, and condition preconditions (e.g., synced-node requirements) are evaluated in `CallData::process()` before the handler is ever called, so the handlers themselves can assume the request is already authorized and the node is in a valid state to serve it.