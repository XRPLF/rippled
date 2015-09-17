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
    using base = ConfigBase04;
    using type = Config04;
    using concurrency_type = base::concurrency_type;

    using request_type = base::request_type;
    using response_type = base::response_type;

    using message_type = base::message_type;
    using con_msg_manager_type = base::con_msg_manager_type;
    using endpoint_msg_manager_type = base::endpoint_msg_manager_type;

    using alog_type = Logger <LoggerType::access>;
    using elog_type = Logger <LoggerType::error>;

    using rng_type = base::rng_type;

    struct transport_config : public base::transport_config {
        using concurrency_type = type::concurrency_type;
        using alog_type        = type::alog_type;
        using elog_type        = type::elog_type;
        using request_type     = type::request_type;
        using response_type    = type::response_type;
        using socket_type =
            websocketpp::transport::asio::basic_socket::endpoint;
    };

    using transport_type =
        websocketpp::transport::asio::endpoint<transport_config>;
};

} // websocket
} // ripple

#endif
