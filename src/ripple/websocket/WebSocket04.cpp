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

#include <ripple/websocket/WebSocket04.h>
#include <ripple/websocket/Handler.h>
#include <ripple/websocket/Server.h>

#include <boost/make_shared.hpp>
#include <beast/weak_fn.h>

namespace ripple {
namespace websocket {

char const* WebSocket04::versionName()
{
    return "websocketpp 0.40";
}

void WebSocket04::handleDisconnect (Connection& connection)
{
    connection.close (websocketpp::close::status::protocol_error,
                      "overload");
}

void WebSocket04::closeTooSlowClient (
    Connection& connection,
    unsigned int timeout,
    std::string const& message)
{
    connection.close (
        websocketpp::close::status::value (timeout), message);
}

bool WebSocket04::isTextMessage (Message const& message)
{
    return message.get_opcode () == websocketpp::frame::opcode::text;
}

using HandlerPtr04 = WebSocket04::HandlerPtr;
using EndpointPtr04 = WebSocket04::EndpointPtr;

HandlerPtr04 WebSocket04::makeHandler (ServerDescription const& desc)
{
    return std::make_shared <HandlerImpl <WebSocket04>> (desc);
}

EndpointPtr04  WebSocket04::makeEndpoint (HandlerPtr&& handler)
{
    auto endpoint = std::make_shared <Endpoint> (std::move (handler));

    endpoint->set_open_handler (
        [endpoint] (websocketpp::connection_hdl hdl) {
            if (auto conn = endpoint->get_con_from_hdl(hdl))
                endpoint->handler()->on_open (conn);
        });

    endpoint->set_close_handler (
        [endpoint] (websocketpp::connection_hdl hdl) {
            if (auto conn = endpoint->get_con_from_hdl(hdl))
                endpoint->handler()->on_close (conn);
        });

    endpoint->set_fail_handler (
        [endpoint] (websocketpp::connection_hdl hdl) {
            if (auto conn = endpoint->get_con_from_hdl(hdl))
                endpoint->handler()->on_fail (conn);
        });

    endpoint->set_pong_handler (
        [endpoint] (websocketpp::connection_hdl hdl, std::string data) {
            if (auto conn = endpoint->get_con_from_hdl(hdl))
                endpoint->handler()->on_pong (conn, data);
        });

    endpoint->set_http_handler (
        [endpoint] (websocketpp::connection_hdl hdl) {
            if (auto conn = endpoint->get_con_from_hdl(hdl))
                endpoint->handler()->http (conn);
        });

    endpoint->set_message_handler (
        [endpoint] (websocketpp::connection_hdl hdl,
                    MessagePtr msg) {
            if (auto conn = endpoint->get_con_from_hdl(hdl))
                endpoint->handler()->on_message (conn, msg);
        });

    endpoint->set_send_empty_handler (
        [endpoint] (websocketpp::connection_hdl hdl) {
            if (auto conn = endpoint->get_con_from_hdl(hdl))
                endpoint->handler()->on_send_empty (conn);
        });

    endpoint->init_asio();

    return endpoint;
}

template <>
void ConnectionImpl <WebSocket04>::setPingTimer ()
{
    if (pingFreq_ <= 0)
        return;
    if (auto con = m_connection.lock ())
    {
        auto t = boost::posix_time::seconds (pingFreq_);
        auto ms = t.total_milliseconds();
        con->set_timer (
            ms,
            std::bind (
                beast::weak_fn (&ConnectionImpl <WebSocket04>::pingTimer,
                                shared_from_this()),
                beast::asio::placeholders::error));
    }
}

boost::asio::io_service::strand& WebSocket04::getStrand (Connection& con)
{
    return *con.get_strand();
}

template <>
void Server <WebSocket04>::listen()
{
    endpoint_->listen (desc_.port.ip, desc_.port.port);
    endpoint_->start_accept();
    auto c = endpoint_->get_io_service ().run ();
    JLOG (j_.warning)
            << "Server run with: '" << c;

}

std::unique_ptr<beast::Stoppable> makeServer04 (ServerDescription const& desc)
{
    return std::make_unique <Server <WebSocket04>> (desc);
}

} // websocket
} // ripple
