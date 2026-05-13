/** @file
 *  Defines the RPC context types that bundle execution state for handler dispatch.
 *
 *  Every RPC handler receives a context object — either `JsonContext` or a
 *  specialization of `GRPCContext` — as its primary input for all node
 *  infrastructure concerns. The base `Context` struct holds protocol-neutral
 *  state; the subtypes carry only the transport-specific request payload.
 */

#pragma once

#include <xrpld/rpc/Role.h>

#include <xrpl/beast/utility/Journal.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/server/InfoSub.h>

namespace xrpl {

class Application;
class NetworkOPs;
class LedgerMaster;

namespace RPC {

/** Protocol-neutral execution state passed to every RPC handler.
 *
 *  Bundles all node-level references an RPC handler may need into a single
 *  aggregate, avoiding the need to thread individual subsystem pointers
 *  through every function signature. All members are non-owning references or
 *  lightweight handles; `Context` owns nothing and imposes no lifecycle
 *  obligations on its holders.
 *
 *  @note Using references for `app`, `netOps`, and `ledgerMaster` is a
 *      deliberate invariant: a context cannot be constructed with null
 *      subsystems. All referenced objects are guaranteed live for the
 *      duration of any in-flight RPC call.
 *
 *  @see JsonContext, GRPCContext
 */
struct Context
{
    /** Structured logger for this call; stored by value (journals are cheap handles). */
    beast::Journal const j;

    /** Root of the node's service graph: wallet, config, database, etc. */
    Application& app;

    /** Resource cost the handler should charge for this call.
     *
     *  This is an in-out field: the transport layer initialises it to
     *  `feeReferenceRPC` before dispatch; handlers update it to
     *  `feeMediumBurdenRPC` or `feeHeavyBurdenRPC` to reflect actual work
     *  performed. An uncaught exception automatically escalates the charge
     *  to `feeExceptionRPC`.
     */
    Resource::Charge& loadType;

    /** Network and consensus state; used to check operating mode and submit transactions. */
    NetworkOPs& netOps;

    /** Ledger history, current open ledger, and validated ledger state. */
    LedgerMaster& ledgerMaster;

    /** Per-client resource tracking record used to enforce rate limits. */
    Resource::Consumer& consumer;

    /** Authorization level resolved from port config and request credentials.
     *
     *  Handlers gate admin-only operations on this value. Possible values are
     *  `GUEST`, `USER`, `IDENTIFIED`, `ADMIN`, `PROXY`, and `FORBID`.
     */
    Role role;

    /** Optional coroutine handle for long-running handlers to yield cooperatively.
     *
     *  When set, the handler may call `coro->yield()` to return the thread to
     *  the job queue and resume later, preventing thread starvation. Most
     *  handlers leave this null.
     */
    std::shared_ptr<JobQueue::Coro> coro;

    /** Open WebSocket session for subscription-oriented handlers.
     *
     *  Set only for WebSocket connections. Handlers such as `subscribe` and
     *  `path_find` use this to register or deregister the session for event
     *  feeds. Null for plain HTTP requests.
     */
    InfoSub::pointer infoSub;

    /** Client's requested API version.
     *
     *  Handlers use this to shape response formats and error codes.
     *  For example, `conditionMet()` returns `rpcNO_NETWORK` on v1 but
     *  `rpcNOT_SYNCED` on v2 and later.
     */
    unsigned int apiVersion;
};

/** Extends `Context` for JSON-RPC handlers dispatched over HTTP or WebSocket.
 *
 *  Adds the parsed request parameters and any proxy-supplied identity headers.
 *  Every old-style handler function has signature `Status(JsonContext&, Json::Value&)`.
 */
struct JsonContext : public Context
{
    /** Identity fields sourced from upstream proxy HTTP headers.
     *
     *  Views into the HTTP request buffer that lives for the duration of the
     *  call; no copy is made. Only trusted when the remote IP belongs to a
     *  configured `secure_gateway` network — otherwise these fields are
     *  stripped by `ServerHandler` before the context is constructed.
     */
    struct Headers
    {
        /** Value of the `X-User` header, identifying the authenticated client. */
        std::string_view user;

        /** Value of the `X-Forwarded-For` header, carrying the original client IP. */
        std::string_view forwardedFor;
    };

    /** Parsed JSON request parameters for this call. */
    json::Value params;

    /** Proxy-supplied identity headers; default-initialised to empty views. */
    Headers headers{};
};

/** Extends `Context` for strongly-typed gRPC handlers.
 *
 *  The template parameter `RequestType` is the generated protobuf message
 *  type for the specific gRPC method (e.g.,
 *  `org::xrpl::rpc::v1::GetLedgerRequest`). All node infrastructure is
 *  inherited from the base `Context`.
 *
 *  @tparam RequestType The protobuf request message type for this gRPC method.
 */
template <class RequestType>
struct GRPCContext : public Context
{
    /** Decoded protobuf request for this gRPC call. */
    RequestType params;
};

}  // namespace RPC
}  // namespace xrpl
