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

#include <ripple/common/jsonrpc_fields.h>
#include <ripple/module/app/main/RPCHTTPServer.h>
#include <ripple/module/rpc/RPCHandler.h>
#include <ripple/module/rpc/RPCServerHandler.h>
#include <ripple/module/rpc/Tuning.h>
#include <ripple/module/rpc/impl/Context.h>
#include <ripple/module/rpc/impl/Handler.h>

namespace ripple {

RPCHandler::RPCHandler (NetworkOPs& netOps, InfoSub::pointer infoSub)
    : netOps_ (netOps)
    , infoSub_ (infoSub)
{
}

// Provide the JSON-RPC "result" value.
//
// JSON-RPC provides a method and an array of params. JSON-RPC is used as a
// transport for a command and a request object. The command is the method. The
// request object is supplied as the first element of the params.
Json::Value RPCHandler::doRpcCommand (
    const std::string& strMethod,
    Json::Value const& jvParams,
    Config::Role role,
    Resource::Charge& loadType)
{
    WriteLog (lsTRACE, RPCHandler)
        << "doRpcCommand:" << strMethod << ":" << jvParams;

    if (!jvParams.isArray () || jvParams.size () > 1)
        return logRPCError (rpcError (rpcINVALID_PARAMS));

    Json::Value params = jvParams.size () ? jvParams[0u]
        : Json::Value (Json::objectValue);

    if (!params.isObject ())
        return logRPCError (rpcError (rpcINVALID_PARAMS));

    // Provide the JSON-RPC method as the field "command" in the request.
    params[jss::command] = strMethod;

    Json::Value jvResult = doCommand (params, role, loadType);

    // Always report "status".  On an error report the request as received.
    if (jvResult.isMember ("error"))
    {
        jvResult[jss::status] = jss::error;
        jvResult[jss::request] = params;
    }
    else
    {
        jvResult[jss::status]  = jss::success;
    }

    return logRPCError (jvResult);
}

Json::Value RPCHandler::doCommand (
    const Json::Value& params,
    Config::Role role,
    Resource::Charge& loadType)
{
    if (role != Config::ADMIN)
    {
        // VFALCO NOTE Should we also add up the jtRPC jobs?
        //
        int jc = getApp().getJobQueue ().getJobCountGE (jtCLIENT);
        if (jc > RPC::MAX_JOB_QUEUE_CLIENTS)
        {
            WriteLog (lsDEBUG, RPCHandler) << "Too busy for command: " << jc;
            return rpcError (rpcTOO_BUSY);
        }
    }

    if (!params.isMember ("command"))
        return rpcError (rpcCOMMAND_MISSING);

    std::string strCommand  = params[jss::command].asString ();

    WriteLog (lsTRACE, RPCHandler) << "COMMAND:" << strCommand;
    WriteLog (lsTRACE, RPCHandler) << "REQUEST:" << params;

    role_ = role;

    auto handler = RPC::getHandler(strCommand);

    if (!handler)
        return rpcError (rpcUNKNOWN_COMMAND);

    if (handler->role_ == Config::ADMIN && role_ != Config::ADMIN)
        return rpcError (rpcNO_PERMISSION);

    if ((handler->condition_ & RPC::NEEDS_NETWORK_CONNECTION) &&
        (netOps_.getOperatingMode () < NetworkOPs::omSYNCING))
    {
        WriteLog (lsINFO, RPCHandler)
            << "Insufficient network mode for RPC: "
            << netOps_.strOperatingMode ();

        return rpcError (rpcNO_NETWORK);
    }

    if (!getConfig ().RUN_STANDALONE
        && (handler->condition_ & RPC::NEEDS_CURRENT_LEDGER)
        && (getApp().getLedgerMaster().getValidatedLedgerAge() >
            RPC::MAX_VALIDATED_LEDGER_AGE))
    {
        return rpcError (rpcNO_CURRENT);
    }

    if ((handler->condition_ & RPC::NEEDS_CLOSED_LEDGER) &&
        !netOps_.getClosedLedger ())
    {
        return rpcError (rpcNO_CLOSED);
    }

    try
    {
        LoadEvent::autoptr ev = getApp().getJobQueue().getLoadEventAP(
            jtGENERIC, "cmd:" + strCommand);
        RPC::Context context {params, loadType, netOps_, infoSub_, role_};
        Json::Value jvRaw = handler->method_(context);

        // Regularize result.
        if (jvRaw.isObject ())
            return jvRaw;

        // Probably got a string.
        Json::Value jvResult (Json::objectValue);
        jvResult[jss::message] = jvRaw;

        return jvResult;
    }
    catch (std::exception& e)
    {
        WriteLog (lsINFO, RPCHandler) << "Caught throw: " << e.what ();

        if (loadType == Resource::feeReferenceRPC)
            loadType = Resource::feeExceptionRPC;

        return rpcError (rpcINTERNAL);
    }
}

} // ripple
