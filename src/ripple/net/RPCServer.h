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

#ifndef RIPPLE_NET_BASICS_RPCSERVER_H_INCLUDED
#define RIPPLE_NET_BASICS_RPCSERVER_H_INCLUDED

namespace ripple {

/** Provides RPC services to a client.
    Each client has a separate instance of this object.
*/
class RPCServer
{
public:
    /** Handles a RPC client request.
    */
    class Handler
    {
    public:
        virtual ~Handler () { }

        /** Construct a HTTP response string.
        */
        virtual std::string createResponse (int statusCode, std::string const& description) = 0;

        /** Determine if the connection is authorized.
        */
        virtual bool isAuthorized (std::map <std::string, std::string> const& headers) = 0;

        /** Produce a response for a given request.

            @param  request The RPC request string.
            @return         The server's response.
        */
        virtual std::string processRequest (std::string const& request,
                                            beast::IP::Endpoint const& remoteIPAddress) = 0;
    };

    virtual ~RPCServer () { }

    /** Called when the connection is established.
    */
    virtual void connected () = 0;

    // VFALCO TODO Remove these since they expose boost
    virtual boost::asio::ip::tcp::socket& getRawSocket () = 0;
    virtual boost::asio::ip::tcp::socket::endpoint_type& getRemoteEndpoint () = 0;
};

} // ripple

#endif
