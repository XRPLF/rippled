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

/**
   This code is called from both the HTTP RPC handler and Websockets.

   The form of the Json returned is somewhat different between the two services.

   HTML:
     Success:
        {
           "result" : {
              "ledger" : {
                 "accepted" : false,
                 "transaction_hash" : "..."
              },
              "ledger_index" : 10300865,
              "validated" : false,
              "status" : "success"  # Status is inside the result.
           }
        }

     Failure:
        {
           "result" : {
              // api_version == 1
              "error" : "noNetwork",
              "error_code" : 17,
              "error_message" : "Not synced to the network.",

              // api_version == 2
              "error" : "notSynced",
              "error_code" : 18,
              "error_message" : "Not synced to the network.",

              "request" : {
                 "command" : "ledger",
                 "ledger_index" : 10300865
              },
              "status" : "error"
           }
        }

   Websocket:
     Success:
        {
           "result" : {
              "ledger" : {
                 "accepted" : false,
                 "transaction_hash" : "..."
              },
              "ledger_index" : 10300865,
              "validated" : false
           }
           "type": "response",
           "status": "success",   # Status is OUTside the result!
           "id": "client's ID",   # Optional
           "warning": 3.14        # Optional
        }

     Failure:
        {
          // api_version == 1
          "error" : "noNetwork",
          "error_code" : 17,
          "error_message" : "Not synced to the network.",

          // api_version == 2
          "error" : "notSynced",
          "error_code" : 18,
          "error_message" : "Not synced to the network.",

          "request" : {
             "command" : "ledger",
             "ledger_index" : 10300865
          },
          "type": "response",
          "status" : "error",
          "id": "client's ID"   # Optional
        }

 */

ErrorCodeI
fillHandler(JsonContext& context, Handler const*& result)
{
    if (!isUnlimited(context.role))
    {
        // Count all jobs at jtCLIENT priority or higher.
        int const jobCount = context.app.getJobQueue().getJobCountGE(JtClient);
        if (jobCount > Tuning::kMaxJobQueueClients)
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

template <class Object, class Method>
Status
callMethod(JsonContext& context, Method method, std::string const& name, Object& result)
{
    static std::atomic<std::uint64_t> kRequestId{0};
    auto& perfLog = context.app.getPerfLog();
    std::uint64_t const curId = ++kRequestId;
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

        if (context.loadType == Resource::kFeeReferenceRpc)
            context.loadType = Resource::kFeeExceptionRpc;

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
