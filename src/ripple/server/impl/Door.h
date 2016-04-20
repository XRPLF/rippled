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

#ifndef RIPPLE_SERVER_DOOR_H_INCLUDED
#define RIPPLE_SERVER_DOOR_H_INCLUDED

#include <ripple/server/impl/io_list.h>
#include <ripple/server/impl/ServerImpl.h>
#include <beast/streambuf.hpp>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/container/flat_map.hpp>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace ripple {

/** A listening socket. */
class Door
    : public io_list::work
    , public std::enable_shared_from_this<Door>
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

    // Detects SSL on a socket
    class Detector
        : public io_list::work
        , public std::enable_shared_from_this<Detector>
    {
    private:
        Port const& port_;
        Handler& handler_;
        socket_type socket_;
        timer_type timer_;
        endpoint_type remote_address_;
        boost::asio::io_service::strand strand_;
        beast::Journal j_;

    public:
        Detector (Port const& port, Handler& handler,
            socket_type&& socket, endpoint_type remote_address,
                beast::Journal j);
        void run();
        void close() override;

    private:
        void do_timer (yield_context yield);
        void do_detect (yield_context yield);
    };

    beast::Journal j_;
    Port const& port_;
    Handler& handler_;
    acceptor_type acceptor_;
    boost::asio::io_service::strand strand_;
    bool ssl_;
    bool plain_;

public:
    Door(Handler& handler, boost::asio::io_service& io_service,
        Port const& port, beast::Journal j);

    // Work-around because we can't call shared_from_this in ctor
    void run();

    /** Close the Door listening socket and connections.
        The listening socket is closed, and all open connections
        belonging to the Door are closed.
        Thread Safety:
            May be called concurrently
    */
    void close();

private:
    template <class ConstBufferSequence>
    void create (bool ssl, ConstBufferSequence const& buffers,
        socket_type&& socket, endpoint_type remote_address);

    void do_accept (yield_context yield);
};

} // ripple

#endif
