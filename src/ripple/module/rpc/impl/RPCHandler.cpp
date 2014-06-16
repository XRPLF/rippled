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
#include <ripple/module/rpc/impl/Context.h>
#include <ripple/module/rpc/impl/Handler.h>

namespace ripple {

//
// Carries out the RPC.
//

SETUP_LOG (RPCHandler)

RPCHandler::RPCHandler (NetworkOPs& netOps)
    : mNetOps (&netOps)
    , mRole (Config::FORBID)
{
    assert (mNetOps);
}

RPCHandler::RPCHandler (NetworkOPs& netOps, InfoSub::pointer infoSub)
    : mNetOps (&netOps)
    , mInfoSub (infoSub)
    , mRole (Config::FORBID)
{
    assert (mNetOps);
}

// Provide the JSON-RPC "result" value.
//
// JSON-RPC provides a method and an array of params. JSON-RPC is used as a
// transport for a command and a request object. The command is the method. The
// request object is supplied as the first element of the params.
Json::Value RPCHandler::doRpcCommand (
    const std::string& strMethod, Json::Value const& jvParams, Config::Role iRole,
    Resource::Charge& loadType)
{
    WriteLog (lsTRACE, RPCHandler)
        << "doRpcCommand:" << strMethod << ":" << jvParams;

    if (!jvParams.isArray () || jvParams.size () > 1)
        return logRPCError (rpcError (rpcINVALID_PARAMS));

    Json::Value params   = jvParams.size () ? jvParams[0u]
        : Json::Value (Json::objectValue);

    if (!params.isObject ())
        return logRPCError (rpcError (rpcINVALID_PARAMS));

    // Provide the JSON-RPC method as the field "command" in the request.
    params[jss::command]    = strMethod;

    Json::Value jvResult = doCommand (params, iRole, loadType);

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

// TODO(tom): this should go with the other handlers.
Json::Value doInternal (RPC::Context& context)
{
    // Used for debug or special-purpose RPC commands
    if (!context.params_.isMember ("internal_command"))
        return rpcError (rpcINVALID_PARAMS);

    return RPCInternalHandler::runHandler (
        context.params_["internal_command"].asString (),
        context.params_["params"]);
}

Json::Value RPCHandler::doCommand (
    const Json::Value& params, Config::Role iRole, Resource::Charge& loadType)
{
    if (iRole != Config::ADMIN)
    {
        // VFALCO NOTE Should we also add up the jtRPC jobs?
        //
        int jc = getApp().getJobQueue ().getJobCountGE (jtCLIENT);

        if (jc > 500)
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

    mRole   = iRole;

    const RPC::Handler* handler = RPC::getHandler(strCommand);

    if (!handler)
        return rpcError (rpcUNKNOWN_COMMAND);

    if (handler->role_ == Config::ADMIN && mRole != Config::ADMIN)
        return rpcError (rpcNO_PERMISSION);

    Application::ScopedLockType lock (getApp().getMasterLock ());

    if ((handler->condition_ & RPC::NEEDS_NETWORK_CONNECTION) &&
        (mNetOps->getOperatingMode () < NetworkOPs::omSYNCING))
    {
        WriteLog (lsINFO, RPCHandler)
            << "Insufficient network mode for RPC: "
            << mNetOps->strOperatingMode ();

        return rpcError (rpcNO_NETWORK);
    }

    if (!getConfig ().RUN_STANDALONE
        && (handler->condition_ & RPC::NEEDS_CURRENT_LEDGER)
        && (getApp().getLedgerMaster().getValidatedLedgerAge() > 120))
    {
        return rpcError (rpcNO_CURRENT);
    }

    if ((handler->condition_ & RPC::NEEDS_CLOSED_LEDGER) &&
        !mNetOps->getClosedLedger ())
    {
            return rpcError (rpcNO_CLOSED);
    }

    try
    {
        LoadEvent::autoptr ev = getApp().getJobQueue().getLoadEventAP(
            jtGENERIC, std::string("cmd:") + strCommand);
        RPC::Context context{params, loadType, lock, *mNetOps, mInfoSub, mRole};
        Json::Value jvRaw = handler->method_(context);

        // Regularize result.
        if (jvRaw.isObject ())
        {
            // Got an object.
            return jvRaw;
        }
        else
        {
            // Probably got a string.
            Json::Value jvResult (Json::objectValue);

            jvResult[jss::message] = jvRaw;

            return jvResult;
        }
    }
    catch (std::exception& e)
    {
        WriteLog (lsINFO, RPCHandler) << "Caught throw: " << e.what ();

        if (loadType == Resource::feeReferenceRPC)
            loadType = Resource::feeExceptionRPC;

        return rpcError (rpcINTERNAL);
    }
}

RPCInternalHandler* RPCInternalHandler::sHeadHandler = nullptr;

RPCInternalHandler::RPCInternalHandler (
    const std::string& name, handler_t Handler)
        : mName (name),
          mHandler (Handler)
{
    mNextHandler = sHeadHandler;
    sHeadHandler = this;
}

Json::Value RPCInternalHandler::runHandler (
    const std::string& name, const Json::Value& params)
{
    RPCInternalHandler* h = sHeadHandler;
    while (h != nullptr)
    {
        if (name == h->mName)
        {
            WriteLog (lsWARNING, RPCHandler)
                << "Internal command " << name << ": " << params;
            Json::Value ret = h->mHandler (params);
            WriteLog (lsWARNING, RPCHandler)
                << "Internal command returns: " << ret;
            return ret;
        }

        h = h->mNextHandler;
    }

    return rpcError (rpcBAD_SYNTAX);
}

} // ripple
