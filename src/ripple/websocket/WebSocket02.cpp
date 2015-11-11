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

#include <ripple/websocket/WebSocket02.h>
#include <ripple/websocket/Handler.h>
#include <ripple/websocket/Server.h>
#include <ripple/basics/contract.h>
#include <beast/weak_fn.h>

// This file contains websocket::WebSocket02 implementations for the WebSocket
// generic functions as well as methods on Server and ConnectionImpl.

namespace ripple {
namespace websocket {

char const* WebSocket02::versionName ()
{
    return "0.2";
}

void WebSocket02::handleDisconnect (Connection& connection)
{
    connection.close (websocketpp_02::close::status::PROTOCOL_ERROR,
                      "overload");
}

void WebSocket02::closeTooSlowClient (
    Connection& connection, unsigned int timeout,
    std::string const& message)
{
    connection.close (
        websocketpp_02::close::status::value (timeout), message);
}

bool WebSocket02::isTextMessage (Message const& message)
{
    return message.get_opcode () == websocketpp_02::frame::opcode::TEXT;
}

using HandlerPtr02 = WebSocket02::HandlerPtr;
using EndpointPtr02 = WebSocket02::EndpointPtr;

HandlerPtr02 WebSocket02::makeHandler (ServerDescription const& desc)
{
    return boost::make_shared <HandlerImpl <WebSocket02>> (desc);
}

EndpointPtr02 WebSocket02::makeEndpoint (HandlerPtr&& handler)
{
    return boost::make_shared <Endpoint > (std::move (handler));
}

boost::asio::io_service::strand& WebSocket02::getStrand (Connection& con)
{
    return con.get_strand();
}

template <>
void ConnectionImpl <WebSocket02>::setPingTimer ()
{
    if (pingFreq_ <= 0)
        return;
    connection_ptr ptr = m_connection.lock ();
    if (ptr)
    {
        this->m_pingTimer.expires_from_now (
            boost::posix_time::seconds (pingFreq_));

        this->m_pingTimer.async_wait (
            ptr->get_strand ().wrap (
                std::bind (
                    beast::weak_fn (&ConnectionImpl <WebSocket02>::pingTimer,
                                    shared_from_this()),
                    beast::asio::placeholders::error)));
    }
}

template <>
void Server <WebSocket02>::listen()
{
    try
    {
        endpoint_->listen (desc_.port.ip, desc_.port.port);
    }
    catch (std::exception const& e)
    {
        // temporary workaround for websocketpp throwing exceptions on
        // access/close races
        for (int i = 0;; ++i)
        {
            // https://github.com/zaphoyd/websocketpp/issues/98
            try
            {
                endpoint_->get_io_service ().run ();
                break;
            }
            catch (std::exception const& e)
            {
                JLOG (j_.warning) << "websocketpp exception: "
                                             << e.what ();
                static const int maxRetries = 10;
                if (maxRetries && i >= maxRetries)
                {
                    JLOG (j_.warning)
                            << "websocketpp exceeded max retries: " << i;
                    break;
                }
            }
        }
        Throw<std::exception> (e);
    }
}

std::unique_ptr<beast::Stoppable> makeServer02 (ServerDescription const& desc)
{
    return std::make_unique <Server <WebSocket02>> (desc);
}

} // websocket
} // ripple
