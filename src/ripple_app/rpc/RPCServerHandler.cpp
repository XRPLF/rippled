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

std::string RPCServerHandler::processRequest (std::string const& request, std::string const& remoteAddress)
{
    Json::Value jvRequest;
    {
        Json::Reader reader;

        if (! reader.parse (request, jvRequest) ||
            jvRequest.isNull () ||
            ! jvRequest.isObject ())
        {
            return createResponse (400, "Unable to parse request");
        }
    }

    Config::Role const role (getConfig ().getAdminRole (jvRequest, remoteAddress));

    Resource::Consumer usage;

    if (role == Config::ADMIN)
        usage = m_resourceManager.newAdminEndpoint (remoteAddress);
    else
        usage = m_resourceManager.newInboundEndpoint (IPAddress::from_string (remoteAddress));

    if (usage.disconnect ())
        return createResponse (503, "Server is overloaded");

    // Parse id now so errors from here on will have the id
    //
    // VFALCO NOTE Except that "id" isn't included in the following errors...
    //
    Json::Value const id = jvRequest ["id"];

    Json::Value const method = jvRequest ["method"];

    if (method.isNull ())
    {
        return createResponse (400, "Null method");
    }
    else if (! method.isString ())
    {
        return createResponse (400, "method is not string");
    }

    std::string strMethod = method.asString ();

    // Parse params
    Json::Value params = jvRequest ["params"];

    if (params.isNull ())
    {
        params = Json::Value (Json::arrayValue);
    }
    else if (!params.isArray ())
    {
        return HTTPReply (400, "params unparseable");
    }

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

    RPCHandler rpcHandler (&m_networkOPs);

    Resource::Charge loadType = Resource::feeReferenceRPC;

    Json::Value const result = rpcHandler.doRpcCommand (strMethod, params, role, loadType);

    usage.charge (loadType);

    WriteLog (lsDEBUG, RPCServer) << "Reply: " << result;

    response = JSONRPCReply (result, Json::Value (), id);

    return createResponse (200, response);
}
