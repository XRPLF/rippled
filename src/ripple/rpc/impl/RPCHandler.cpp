//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/app/ledger/LedgerMaster.h>
#include <ripple/app/main/Application.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/basics/Log.h>
#include <ripple/basics/PerfLog.h>
#include <ripple/basics/contract.h>
#include <ripple/core/Config.h>
#include <ripple/core/JobQueue.h>
#include <ripple/json/Object.h>
#include <ripple/json/to_string.h>
#include <ripple/net/InfoSub.h>
#include <ripple/net/RPCErr.h>
#include <ripple/protocol/jss.h>
#include <ripple/resource/Fees.h>
#include <ripple/rpc/RPCHandler.h>
#include <ripple/rpc/Role.h>
#include <ripple/rpc/impl/Handler.h>
#include <ripple/rpc/impl/Tuning.h>
#include <atomic>
#include <chrono>

namespace ripple {
namespace RPC {

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

error_code_i
fillHandler(JsonContext& context, Handler const*& result)
{
    if (!isUnlimited(context.role))
    {
        // VFALCO NOTE Should we also add up the jtRPC jobs?
        //
        int jc = context.app.getJobQueue().getJobCountGE(jtCLIENT);
        if (jc > Tuning::maxJobQueueClients)
        {
            JLOG(context.j.debug()) << "Too busy for command: " << jc;
            return rpcTOO_BUSY;
        }
    }

    if (!context.params.isMember(jss::command) &&
        !context.params.isMember(jss::method))
        return rpcCOMMAND_MISSING;
    if (context.params.isMember(jss::command) &&
        context.params.isMember(jss::method))
    {
        if (context.params[jss::command].asString() !=
            context.params[jss::method].asString())
            return rpcUNKNOWN_COMMAND;
    }

    std::string strCommand = context.params.isMember(jss::command)
        ? context.params[jss::command].asString()
        : context.params[jss::method].asString();

    JLOG(context.j.trace()) << "COMMAND:" << strCommand;
    JLOG(context.j.trace()) << "REQUEST:" << context.params;
    auto handler = getHandler(context.apiVersion, strCommand);

    if (!handler)
        return rpcUNKNOWN_COMMAND;

    if (handler->role_ == Role::ADMIN && context.role != Role::ADMIN)
        return rpcNO_PERMISSION;

    error_code_i res = conditionMet(handler->condition_, context);
    if (res != rpcSUCCESS)
    {
        return res;
    }

    result = handler;
    return rpcSUCCESS;
}

template <class Object, class Method>
Status
callMethod(
    JsonContext& context,
    Method method,
    std::string const& name,
    Object& result)
{
    static std::atomic<std::uint64_t> requestId{0};
    auto& perfLog = context.app.getPerfLog();
    std::uint64_t const curId = ++requestId;
    try
    {
        perfLog.rpcStart(name, curId);
        auto v =
            context.app.getJobQueue().makeLoadEvent(jtGENERIC, "cmd:" + name);

        auto ret = method(context, result);
        perfLog.rpcFinish(name, curId);
        return ret;
    }
    catch (std::exception& e)
    {
        perfLog.rpcError(name, curId);
        JLOG(context.j.info()) << "Caught throw: " << e.what();

        if (context.loadType == Resource::feeReferenceRPC)
            context.loadType = Resource::feeExceptionRPC;

        inject_error(rpcINTERNAL, result);
        return rpcINTERNAL;
    }
}

}  // namespace

Status
doCommand(RPC::JsonContext& context, Json::Value& result)
{
    Handler const* handler = nullptr;
    if (auto error = fillHandler(context, handler))
    {
        inject_error(error, result);
        return error;
    }

    if (auto method = handler->valueMethod_)
    {
        if (!context.headers.user.empty() ||
            !context.headers.forwardedFor.empty())
        {
            JLOG(context.j.debug())
                << "start command: " << handler->name_
                << ", user: " << context.headers.user
                << ", forwarded for: " << context.headers.forwardedFor;

            auto ret = callMethod(context, method, handler->name_, result);

            JLOG(context.j.debug())
                << "finish command: " << handler->name_
                << ", user: " << context.headers.user
                << ", forwarded for: " << context.headers.forwardedFor;

            return ret;
        }
        else
        {
            return callMethod(context, method, handler->name_, result);
        }
    }

    return rpcUNKNOWN_COMMAND;
}

Role
roleRequired(unsigned int version, std::string const& method)
{
    auto handler = RPC::getHandler(version, method);

    if (!handler)
        return Role::FORBID;

    return handler->role_;
}

}  // namespace RPC
}  // namespace ripple
