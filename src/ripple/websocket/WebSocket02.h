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

#ifndef RIPPLED_RIPPLE_WEBSOCKET_WEBSOCKET02_H
#define RIPPLED_RIPPLE_WEBSOCKET_WEBSOCKET02_H

#include <ripple/websocket/WebSocket.h>

// LexicalCast must be included before websocketpp_02.
#include <beast/module/core/text/LexicalCast.h>

#include <websocketpp_02/src/sockets/socket_base.hpp>
#include <websocketpp_02/src/websocketpp.hpp>
#include <websocketpp_02/src/sockets/autotls.hpp>
#include <websocketpp_02/src/messages/data.hpp>

namespace ripple {
namespace websocket {

struct WebSocket02
{
    using Endpoint = websocketpp_02::server_autotls;
    using Connection = Endpoint::connection_type;
    using ConnectionPtr = boost::shared_ptr<Connection>;
    using ConnectionWeakPtr = boost::weak_ptr<Connection>;
    using EndpointPtr = Endpoint::ptr;
    using ErrorCode = boost::system::error_code;
    using Handler = Endpoint::handler;
    using HandlerPtr = Handler::ptr;
    using Message = websocketpp_02::message::data;
    using MessagePtr = Message::ptr;

    /** The name of this WebSocket version. */
    static
    char const* versionName();

    /** Handle a connection that was cut off from the other side. */
    static
    void handleDisconnect (Connection&);

    /** Close a client that is too slow to respond. */
    static
    void closeTooSlowClient (
        Connection&,
        unsigned int timeout,
        std::string const& message = "Client is too slow.");

    /** Return true if the WebSocket message is a TEXT message. */
    static
    bool isTextMessage (Message const&);

    /** Create a new Handler. */
    static
    HandlerPtr makeHandler (ServerDescription const&);

    /** Make a connection endpoint from a handler. */
    static
    EndpointPtr makeEndpoint (HandlerPtr&&);

    /** Get the ASIO strand that this connection lives on. */
    static
    boost::asio::io_service::strand& getStrand (Connection&);
};

} // websocket
} // ripple

#endif
