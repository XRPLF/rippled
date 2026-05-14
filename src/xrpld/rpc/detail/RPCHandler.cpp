/** @file
 *  Central dispatch engine for XRPL RPC commands.
 *
 *  Implements the three-stage pipeline shared by the HTTP and WebSocket
 *  transports: `fillHandler` (validation and gating), `callMethod`
 *  (instrumented, exception-safe invocation), and the public `doCommand`
 *  entry point that threads them together.
 *
 *  @note HTTP and WebSocket responses differ structurally: for HTTP the
 *      `status` field lives *inside* `result`; for WebSocket it lives at the
 *      top level. This file populates only the inner `result` object — the
 *      transport layers assemble the envelope.
 *
 *  HTTP success envelope:
 *  @code{.json}
 *  { "result": { ..., "status": "success" } }
 *  @endcode
 *
 *  WebSocket success envelope:
 *  @code{.json}
 *  { "result": { ... }, "type": "response", "status": "success", "id": "..." }
 *  @endcode
 *
 *  Error codes are API-version-sensitive: API v1 emits `rpcNO_NETWORK`,
 *  `rpcNO_CURRENT`, and `rpcNO_CLOSED`; API v2+ collapses all three to
 *  `rpcNOT_SYNCED`.
 */

#include <xrpld/rpc/RPCHandler.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/core/Config.h>
#include <xrpld/rpc/Context.h>
#include <xrpld/rpc/Role.h>
#include <xrpld/rpc/Status.h>
#include <xrpld/rpc/detail/Handler.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/Log.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/core/PerfLog.h>
#include <xrpl/json/to_string.h>  // IWYU pragma: keep
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Fees.h>

#include <atomic>
#include <chrono>
#include <cstdint>
#include <exception>
#include <string>

namespace xrpl::RPC {

namespace {

/** Pre-dispatch gatekeeper: validate, route, and authorize an RPC request.
 *
 *  Executes every check required before a handler is invoked, in order:
 *
 *  1. **Load shedding** — for non-unlimited callers, counts all jobs at
 *     `jtCLIENT` priority or above. Rejects with `rpcTOO_BUSY` if the count
 *     exceeds `Tuning::kMAX_JOB_QUEUE_CLIENTS`. Admin/unlimited callers bypass
 *     this gate so operators retain access under saturation.
 *  2. **Command field resolution** — accepts `"command"` (canonical JSON-RPC)
 *     or `"method"` (legacy WebSocket alias). If both are present with
 *     *different* values, returns `rpcUNKNOWN_COMMAND` rather than silently
 *     picking one.
 *  3. **Handler lookup** — calls `getHandler(version, betaEnabled, name)`.
 *     Unknown commands or out-of-range API versions return `rpcUNKNOWN_COMMAND`.
 *  4. **Role enforcement** — rejects non-admin callers for `Role::ADMIN`
 *     handlers with `rpcNO_PERMISSION`.
 *  5. **Node condition check** — delegates to `conditionMet()`, which enforces
 *     amendment-blocked, UNL-blocked, operating-mode, and ledger-freshness
 *     requirements. Error codes are version-sensitive (v1 vs v2+).
 *
 *  On success, `result` is set to the resolved `Handler` and `RpcSuccess` is
 *  returned. On any failure, `result` is left unchanged and the appropriate
 *  `ErrorCodeI` is returned.
 *
 *  @param context  The RPC dispatch context, supplying params, role, API
 *      version, and references to node subsystems.
 *  @param result   Output parameter; set to the matched handler on success.
 *  @return `RpcSuccess` if all gates pass, otherwise the first failing error
 *      code.
 */
ErrorCodeI
fillHandler(JsonContext& context, Handler const*& result)
{
    if (!isUnlimited(context.role))
    {
        int const jobCount = context.app.getJobQueue().getJobCountGE(JtClient);
        if (jobCount > Tuning::kMAX_JOB_QUEUE_CLIENTS)
        {
            JLOG(context.j.debug()) << "Too busy for command: " << jobCount;
            return RpcTooBusy;
        }
    }

    if (!context.params.isMember(jss::command) && !context.params.isMember(jss::method))
        return RpcCommandMissing;
    if (context.params.isMember(jss::command) && context.params.isMember(jss::method))
    {
        if (context.params[jss::command].asString() != context.params[jss::method].asString())
            return RpcUnknownCommand;
    }

    std::string const strCommand = context.params.isMember(jss::command)
        ? context.params[jss::command].asString()
        : context.params[jss::method].asString();

    JLOG(context.j.trace()) << "COMMAND:" << strCommand;
    JLOG(context.j.trace()) << "REQUEST:" << context.params;
    auto handler = getHandler(context.apiVersion, context.app.config().BETA_RPC_API, strCommand);

    if (handler == nullptr)
        return RpcUnknownCommand;

    if (handler->role == Role::ADMIN && context.role != Role::ADMIN)
        return RpcNoPermission;

    ErrorCodeI const res = conditionMet(handler->condition, context);
    if (res != RpcSuccess)
    {
        return res;
    }

    result = handler;
    return RpcSuccess;
}

/** Invoke an RPC handler with performance instrumentation and exception safety.
 *
 *  Brackets the handler call with `PerfLog::rpcStart` / `rpcFinish` (or
 *  `rpcError` on failure) and registers a `JobQueue` load event named
 *  `"cmd:<name>"` for wall-clock accounting. A monotonically increasing
 *  `requestId` (shared `static std::atomic<uint64_t>`) correlates start and
 *  finish log entries across concurrent calls without requiring a mutex.
 *
 *  If any `std::exception` escapes the handler and the request was charged at
 *  `feeReferenceRPC`, this function escalates `context.loadType` to
 *  `feeExceptionRPC` before returning. The escalation causes the resource
 *  layer to levy a higher fee for the call that crashed the handler — an
 *  automatic deterrent against server-side abort-inducing requests.
 *
 *  No exception ever propagates out of this function; callers receive either
 *  the handler's `Status` or `RpcInternal`.
 *
 *  @tparam Object  The JSON output type (typically `json::Value`).
 *  @tparam Method  A callable matching `Status(JsonContext&, Object&)`.
 *  @param context  The RPC dispatch context for the current request.
 *  @param method   The handler callable to invoke.
 *  @param name     The RPC method name; used as the `PerfLog` and load-event key.
 *  @param result   Output object the handler populates with its response.
 *  @return The handler's `Status`, or `RpcInternal` if an exception was caught.
 */
template <class Object, class Method>
Status
callMethod(JsonContext& context, Method method, std::string const& name, Object& result)
{
    static std::atomic<std::uint64_t> kREQUEST_ID{0};
    auto& perfLog = context.app.getPerfLog();
    std::uint64_t const curId = ++kREQUEST_ID;
    try
    {
        perfLog.rpcStart(name, curId);
        auto v = context.app.getJobQueue().makeLoadEvent(JtGeneric, "cmd:" + name);

        auto start = std::chrono::system_clock::now();
        auto ret = method(context, result);
        auto end = std::chrono::system_clock::now();

        JLOG(context.j.debug()) << "RPC call " << name << " completed in "
                                << ((end - start).count() / 1000000000.0) << "seconds";
        perfLog.rpcFinish(name, curId);
        return ret;
    }
    catch (std::exception& e)
    {
        perfLog.rpcError(name, curId);
        JLOG(context.j.info()) << "Caught throw: " << e.what();

        if (context.loadType == Resource::kFEE_REFERENCE_RPC)
            context.loadType = Resource::kFEE_EXCEPTION_RPC;

        injectError(RpcInternal, result);
        return RpcInternal;
    }
}

}  // namespace

Status
doCommand(RPC::JsonContext& context, json::Value& result)
{
    Handler const* handler = nullptr;
    if (auto error = fillHandler(context, handler))
    {
        injectError(error, result);
        return error;
    }

    if (auto method = handler->valueMethod)
    {
        if (!context.headers.user.empty() || !context.headers.forwardedFor.empty())
        {
            JLOG(context.j.debug())
                << "start command: " << handler->name << ", user: " << context.headers.user
                << ", forwarded for: " << context.headers.forwardedFor;

            auto ret = callMethod(context, method, handler->name, result);

            JLOG(context.j.debug())
                << "finish command: " << handler->name << ", user: " << context.headers.user
                << ", forwarded for: " << context.headers.forwardedFor;

            return ret;
        }

        auto ret = callMethod(context, method, handler->name, result);
        return ret;
    }

    return RpcUnknownCommand;
}

Role
roleRequired(unsigned int version, bool betaEnabled, std::string const& method)
{
    auto handler = RPC::getHandler(version, betaEnabled, method);

    if (handler == nullptr)
        return Role::FORBID;

    return handler->role;
}

}  // namespace xrpl::RPC
