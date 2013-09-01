//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NET_RPC_RPCCALL_H_INCLUDED
#define RIPPLE_NET_RPC_RPCCALL_H_INCLUDED

//
// This a trusted interface, the user is expected to provide valid input to perform valid requests.
// Error catching and reporting is not a requirement of this command line interface.
//
// Improvements to be more strict and to provide better diagnostics are welcome.
//

/** Processes Ripple RPC calls.
*/
class RPCCall
{
public:

    static int fromCommandLine (const std::vector<std::string>& vCmd);

    static void fromNetwork (
        boost::asio::io_service& io_service,
        const std::string& strIp, const int iPort,
        const std::string& strUsername, const std::string& strPassword,
        const std::string& strPath, const std::string& strMethod,
        const Json::Value& jvParams, const bool bSSL,
        FUNCTION_TYPE<void (const Json::Value& jvInput)> callbackFuncP = FUNCTION_TYPE<void (const Json::Value& jvInput)> ());
};

#endif
