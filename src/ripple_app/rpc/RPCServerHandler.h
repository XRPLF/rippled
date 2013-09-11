//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_RPCSERVERHANDLER_H_INCLUDED
#define RIPPLE_RPCSERVERHANDLER_H_INCLUDED

class NetworkOPs;

/** Handles RPC requests.
*/
class RPCServerHandler : public RPCServer::Handler
{
public:
    explicit RPCServerHandler (NetworkOPs& networkOPs);

    std::string createResponse (int statusCode, std::string const& description);

    bool isAuthorized (std::map <std::string, std::string> const& headers);

    std::string processRequest (std::string const& request, std::string const& remoteAddress);

private:
    NetworkOPs& m_networkOPs;
};

#endif
