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

#ifndef RIPPLE_NET_RPC_RPCCALL_H_INCLUDED
#define RIPPLE_NET_RPC_RPCCALL_H_INCLUDED

namespace ripple {

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
        std::function<void (const Json::Value& jvInput)> callbackFuncP = std::function<void (const Json::Value& jvInput)> ());
};

} // ripple

#endif
