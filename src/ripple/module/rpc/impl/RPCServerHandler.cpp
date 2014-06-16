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

#include <ripple/module/app/main/RPCHTTPServer.h>
#include <ripple/module/rpc/RPCHandler.h>
#include <ripple/module/rpc/RPCServerHandler.h>

namespace ripple {

RPCServerHandler::RPCServerHandler (NetworkOPs& networkOPs, Resource::Manager& resourceManager)
    : m_networkOPs (networkOPs)
    , m_resourceManager (resourceManager)
{
}

std::string RPCServerHandler::createResponse (
    int statusCode,
    std::string const& description)
{
    return HTTPReply (statusCode, description);
}

bool RPCServerHandler::isAuthorized (
    std::map <std::string, std::string> const& headers)
{
    return HTTPAuthorized (headers);
}

std::string RPCServerHandler::processRequest (std::string const& request,
                                              beast::IP::Endpoint const& remoteIPAddress)
{
    Json::Value jsonRequest;
    {
        Json::Reader reader;

        if ((request.size() > 1000000) ||
            ! reader.parse (request, jsonRequest) ||
            jsonRequest.isNull () ||
            ! jsonRequest.isObject ())
        {
            return createResponse (400, "Unable to parse request");
        }
    }

    Config::Role const role (getConfig ().getAdminRole (jsonRequest, remoteIPAddress));

    Resource::Consumer usage;

    if (role == Config::ADMIN)
        usage = m_resourceManager.newAdminEndpoint (remoteIPAddress.to_string());
    else
        usage = m_resourceManager.newInboundEndpoint (remoteIPAddress);

    if (usage.disconnect ())
        return createResponse (503, "Server is overloaded");

    // Parse id now so errors from here on will have the id
    //
    // VFALCO NOTE Except that "id" isn't included in the following errors...
    //
    Json::Value const& id = jsonRequest ["id"];

    Json::Value const& method = jsonRequest ["method"];

    if (method.isNull ())
    {
        return createResponse (400, "Null method");
    }
    else if (! method.isString ())
    {
        return createResponse (400, "method is not string");
    }

    std::string strMethod = method.asString ();

    if (jsonRequest["params"].isNull())
        jsonRequest["params"] = Json::Value (Json::arrayValue);

    // Parse params
    Json::Value& params = jsonRequest ["params"];

    if (!params.isArray ())
        return HTTPReply (400, "params unparseable");

    // VFALCO TODO Shouldn't we handle this earlier?
    //
    if (role == Config::FORBID)
    {
        // VFALCO TODO Needs implementing
        // FIXME Needs implementing
        // XXX This needs rate limiting to prevent brute forcing password.
        return HTTPReply (403, "Forbidden");
    }

    // This code does all the work on the io_service thread and
    // has no rate-limiting based on source IP or anything.
    // This is a temporary safety
    if ((role != Config::ADMIN) && (getApp().getFeeTrack().isLoadedLocal()))
    {
        return HTTPReply (503, "Unable to service at this time");
    }

    std::string response;

    WriteLog (lsDEBUG, RPCServer) << "Query: " << strMethod << params;

    {
        Json::Value ripple_params (params.size()
            ? params [0u] : Json::Value (Json::objectValue));
        if (!ripple_params.isObject())
            return HTTPReply (400, "params must be an object");

        ripple_params ["command"] = strMethod;
        RPC::Request req (LogPartition::getJournal <RPCServer> (),
            strMethod, ripple_params, getApp ());

        // VFALCO Try processing the command using the new code
        if (getApp().getRPCManager().dispatch (req))
        {
            usage.charge (req.fee);
            WriteLog (lsDEBUG, RPCServer) << "Reply: " << req.result;
            return createResponse (200,
                JSONRPCReply (req.result, Json::Value (), id));
        }
    }

    // legacy dispatcher
    Resource::Charge fee (Resource::feeReferenceRPC);
    RPCHandler rpcHandler (m_networkOPs);
    Json::Value const result = rpcHandler.doRpcCommand (
        strMethod, params, role, fee);

    usage.charge (fee);

    WriteLog (lsDEBUG, RPCServer) << "Reply: " << result;

    response = JSONRPCReply (result, Json::Value (), id);

    return createResponse (200, response);
}

}
