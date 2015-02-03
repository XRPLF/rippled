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

#ifndef RIPPLED_RIPPLE_WEBSOCKET_CONFIG04_H
#define RIPPLED_RIPPLE_WEBSOCKET_CONFIG04_H


#include <ripple/websocket/AutoSocket.h>
#include <ripple/websocket/Logger.h>

#include <websocketpp/config/core.hpp>
#include <websocketpp/server.hpp>
#include <websocketpp/transport/asio/endpoint.hpp>

namespace ripple {
namespace websocket {

/** This is a config traits class, copied from
    websocketpp/websocketpp/config/asio_no_tls.hpp*/

using ConfigBase04 = websocketpp::config::core;

struct Config04 : ConfigBase04 {
    typedef ConfigBase04 base;
    typedef Config04 type;
    typedef base::concurrency_type concurrency_type;

    typedef base::request_type request_type;
    typedef base::response_type response_type;

    typedef base::message_type message_type;
    typedef base::con_msg_manager_type con_msg_manager_type;
    typedef base::endpoint_msg_manager_type endpoint_msg_manager_type;

    typedef Logger <LoggerType::access> alog_type;
    typedef Logger <LoggerType::error> elog_type;

    typedef base::rng_type rng_type;

    struct transport_config : public base::transport_config {
        typedef type::concurrency_type concurrency_type;
        typedef type::alog_type alog_type;
        typedef type::elog_type elog_type;
        typedef type::request_type request_type;
        typedef type::response_type response_type;
        // typedef AutoSocket<con_type, std::error_code>
        typedef websocketpp::transport::asio::basic_socket::endpoint
        socket_type;
    };

    typedef websocketpp::transport::asio::endpoint<transport_config>
        transport_type;
};

} // websocket
} // ripple

#endif
