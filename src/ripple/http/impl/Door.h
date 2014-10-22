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

#ifndef RIPPLE_HTTP_DOOR_H_INCLUDED
#define RIPPLE_HTTP_DOOR_H_INCLUDED

#include <ripple/http/impl/ServerImpl.h>
#include <ripple/http/impl/Types.h>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <chrono>
#include <memory>

namespace ripple {
namespace HTTP {

/** A listening socket. */
class Door
    : public beast::List <Door>::Node
    , public std::enable_shared_from_this <Door>
{
private:
    using clock_type = std::chrono::steady_clock;
    using timer_type = boost::asio::basic_waitable_timer<clock_type>;
    using error_code = boost::system::error_code;
    using yield_context = boost::asio::yield_context;
    using protocol_type = boost::asio::ip::tcp;
    using acceptor_type = protocol_type::acceptor;
    using endpoint_type = protocol_type::endpoint;
    using socket_type = protocol_type::socket;

    boost::asio::io_service& io_service_;
    boost::asio::basic_waitable_timer <clock_type> timer_;
    acceptor_type acceptor_;
    Port port_;
    ServerImpl& server_;
    bool ssl_;
    bool plain_;

public:
    Door (boost::asio::io_service& io_service,
        ServerImpl& impl, Port const& port);

    ~Door ();

    Port const&
    port() const
    {
        return port_;
    }

    void listen();
    void cancel();

private:
    class connection
        : public std::enable_shared_from_this <connection>
    {
    private:
        Door& door_;
        socket_type socket_;
        endpoint_type remote_address_;
        boost::asio::io_service::strand strand_;
        timer_type timer_;

    public:
        connection (Door& door, socket_type&& socket,
            endpoint_type remote_address);

        void
        run();

    private:
        void
        do_timer (yield_context yield);

        void
        do_detect (yield_context yield);
    };

    void do_accept (yield_context yield);
};

}
}

#endif
