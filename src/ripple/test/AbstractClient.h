//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#ifndef RIPPLE_TEST_ABSTRACTCLIENT_H_INCLUDED
#define RIPPLE_TEST_ABSTRACTCLIENT_H_INCLUDED

#include <ripple/json/json_value.h>

namespace ripple {
namespace test {

/* Abstract Ripple Client interface.

   This abstracts the transport layer, allowing
   commands to be submitted to a rippled server.
*/
class AbstractClient
{
public:
    virtual ~AbstractClient() = default;
    AbstractClient() = default;
    AbstractClient(AbstractClient const&) = delete;
    AbstractClient& operator=(AbstractClient const&) = delete;

    /** Submit a command synchronously.

        The arguments to the function and the returned JSON
        are in a normalized format, the same whether the client
        is using the JSON-RPC over HTTP/S or WebSocket transport.

        @param cmd The command to execute
        @param params Json::Value of null or object type
                      with zero or more key/value pairs.
        @return The server response in normalized format.
    */
    virtual
    Json::Value
    invoke(std::string const& cmd,
        Json::Value const& params) = 0;

    virtual
    Json::Value
    invoke(Json::Value const& cmd) = 0;

    /// Get RPC 1.0 or RPC 2.0
    virtual unsigned version() const = 0;
};

} // test
} // ripple

#endif
