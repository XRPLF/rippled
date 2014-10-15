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
#include <beast/asio/streambuf.h>
#include <boost/asio/basic_waitable_timer.hpp>
#include <boost/asio/io_service.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/intrusive/list.hpp>
#include <chrono>
#include <condition_variable>
#include <memory>
#include <mutex>

namespace ripple {
namespace HTTP {

/** A listening socket. */
class Door
    : public ServerImpl::Child
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

    // Detects SSL on a socket
    class detector
        : public std::enable_shared_from_this <detector>
        , public ServerImpl::Child
    {
    private:
        Door& door_;
        socket_type socket_;
        timer_type timer_;
        endpoint_type remote_endpoint_;

    public:
        detector (Door& door, socket_type&& socket,
            endpoint_type endpoint);

        ~detector();

        void run();
        void close();

    private:
        void do_timer (yield_context yield);
        void do_detect (yield_context yield);
    };

    using list_type = boost::intrusive::make_list <Child,
        boost::intrusive::constant_time_size <false>>::type;

    Port port_;
    ServerImpl& server_;
    acceptor_type acceptor_;
    boost::asio::io_service::strand strand_;
    std::mutex mutex_;
    std::condition_variable cond_;
    list_type list_;

public:
    Door (boost::asio::io_service& io_service,
        ServerImpl& server, Port const& port);

    /** Destroy the door.
        Blocks until there are no pending I/O completion
        handlers, and all connections have been destroyed.
        close() must be called before the destructor.
    */
    ~Door();

    ServerImpl&
    server()
    {
        return server_;
    }

    Port const&
    port() const
    {
        return port_;
    }

    // Work-around because we can't call shared_from_this in ctor
    void run();

    void add (Child& c);

    void remove (Child& c);

    /** Close the Door listening socket and connections.
        The listening socket is closed, and all open connections
        belonging to the Door are closed.
        Thread Safety:
            May be called concurrently
    */
    void close();

private:
    void create (bool ssl, beast::asio::streambuf&& buf,
        socket_type&& socket, endpoint_type remote_address);

    void do_accept (yield_context yield);
};

}
}

#endif
