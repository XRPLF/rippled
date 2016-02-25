//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#ifndef BEAST_WSPROTO_SYNC_ECHO_PEER_H_INCLUDED
#define BEAST_WSPROTO_SYNC_ECHO_PEER_H_INCLUDED

#include <beast/unit_test/suite.h>
#include <beast/asio/streambuf.h>
#include <beast/wsproto.h>
#include <boost/optional.hpp>
#include <functional>
#include <memory>
#include <thread>

namespace beast {
namespace wsproto {
namespace test {

// Synchronous WebSocket echo client/server
//
class sync_echo_peer
{
public:
    static std::size_t constexpr autobahnCycles = 520;

    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

private:
    unit_test::suite& suite_;
    boost::asio::io_service ios_;
    socket_type sock_;
    boost::asio::ip::tcp::acceptor acceptor_;
    unit_test::thread thread_;
    std::size_t n_ = 0;

public:
    sync_echo_peer(bool server,
            endpoint_type ep, unit_test::suite& suite)
        : suite_(suite)
        , sock_(ios_)
        , acceptor_(ios_)
    {
        error_code ec;
        acceptor_.open(ep.protocol(), ec);
        maybe_throw(ec, "open");
        acceptor_.bind(ep, ec);
        maybe_throw(ec, "bind");
        acceptor_.listen(
            boost::asio::socket_base::max_connections, ec);
        maybe_throw(ec, "listen");
        acceptor_.async_accept(sock_,
            std::bind(&sync_echo_peer::on_accept, this,
                beast::asio::placeholders::error));
        thread_ = unit_test::thread(suite_,
            [&]
            {
                ios_.run();
            });
    }

    ~sync_echo_peer()
    {
        error_code ec;
        ios_.dispatch(
            [&]{ acceptor_.close(ec); });
        thread_.join();
    }

private:
    void
    fail(error_code ec, std::string what)
    {
        suite_.log <<
            what << ": " << ec.message();
    }

    void
    maybe_throw(error_code ec, std::string what)
    {
        if(ec)
        {
            fail(ec, what);
            throw ec;
        }
    }

    void
    on_accept(error_code ec)
    {
        if(ec == boost::asio::error::operation_aborted)
            return;
        maybe_throw(ec, "accept");
        ++n_;
        std::thread{
            [
                this,
                sock = std::move(sock_),
                work = boost::asio::io_service::work{ios_}
            ]() mutable
            {
                do_peer(std::move(sock));
            }}.detach();
        if(n_ < autobahnCycles)
            acceptor_.async_accept(sock_,
                std::bind(&sync_echo_peer::on_accept, this,
                    beast::asio::placeholders::error));
    }

    struct identity
    {
        template<class Body, class Headers>
        void
        operator()(http::message<true, Body, Headers>& req)
        {
            req.headers.replace("User-Agent", "sync_echo_client");
        }

        template<class Body, class Headers>
        void
        operator()(http::message<false, Body, Headers>& resp)
        {
            resp.headers.replace("Server", "sync_echo_server");
        }
    };

    void
    do_peer(socket_type&& sock)
    {
        wsproto::socket<socket_type> ws(std::move(sock));
        ws.set_option(decorate(identity{}));
        ws.set_option(read_message_max(64 * 1024 * 1024));
        //ws.set_option(auto_fragment_size(0));
        error_code ec;
        ws.accept(ec);
        if(ec)
        {
            fail(ec, "accept");
            return;
        }
        for(;;)
        {
            wsproto::opcode op;
            beast::streambuf sb;
            ws.read(op, sb, ec);
            if(ec)
                break;
            ws.set_option(wsproto::message_type(op));
            ws.write(sb.data(), ec);
            if(ec)
                break;
        }
        if(ec && ec != wsproto::error::closed)
        {
            fail(ec, "read");
        }
    }
};

} // test
} // wsproto
} // beast

#endif
