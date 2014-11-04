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

#ifndef RIPPLE_NET_RPC_RPCUTIL_H_INCLUDED
#define RIPPLE_NET_RPC_RPCUTIL_H_INCLUDED

namespace ripple {

using HTTPHeaders = std::map<std::string, std::string>;

// VFALCO TODO Wrap these up into a class. It looks like they just do some
//             convenience packaging of JSON data from the pieces. It looks
//             Ripple client protocol-specific.
//
std::string JSONRPCRequest (
    std::string const& method, Json::Value const& param, Json::Value const& id);

std::string JSONRPCReply (
    Json::Value const& result, Json::Value const& error, Json::Value const& id);

std::string HTTPReply (int nStatus, std::string const& strMsg);

// VFALCO TODO Create a HTTPHeaders class with a nice interface.
bool HTTPAuthorized (HTTPHeaders const& mapHeaders);

std::string createHTTPPost (
    std::string const& host,
    std::string const& path,
    std::string const& msg,
    HTTPHeaders const& requestHeaders);

} // ripple

#endif
