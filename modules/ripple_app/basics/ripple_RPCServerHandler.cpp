//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

RPCServerHandler::RPCServerHandler (NetworkOPs& networkOPs)
    : m_networkOPs (networkOPs)
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

    int role = iAdminGet (jvRequest, remoteAddress);

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
    if (role == RPCHandler::FORBID)
    {
        // VFALCO TODO Needs implementing
        // FIXME Needs implementing
        // XXX This needs rate limiting to prevent brute forcing password.
        return HTTPReply (403, "Forbidden");
    }

    std::string response;

    WriteLog (lsINFO, RPCServer) << params;

    RPCHandler rpcHandler (&m_networkOPs);

    LoadType loadType = LT_RPCReference;

    Json::Value const result = rpcHandler.doRpcCommand (strMethod, params, role, &loadType);
    // VFALCO NOTE We discard loadType since there is no endpoint to punish

    WriteLog (lsINFO, RPCServer) << result;

    response = JSONRPCReply (result, Json::Value (), id);

    return createResponse (200, response);
}
