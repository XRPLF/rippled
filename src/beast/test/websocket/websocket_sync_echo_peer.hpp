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

#ifndef BEAST_WEBSOCKET_SYNC_ECHO_PEER_H_INCLUDED
#define BEAST_WEBSOCKET_SYNC_ECHO_PEER_H_INCLUDED

#include <beast/streambuf.hpp>
#include <beast/websocket.hpp>
#include <boost/optional.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

namespace beast {
namespace websocket {

// Synchronous WebSocket echo client/server
//
class sync_echo_peer
{
public:
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

private:
    boost::asio::io_service ios_;
    socket_type sock_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::thread thread_;

public:
    sync_echo_peer(bool server, endpoint_type ep)
        : sock_(ios_)
        , acceptor_(ios_)
    {
        error_code ec;
        acceptor_.open(ep.protocol(), ec);
        maybe_throw(ec, "open");
        acceptor_.set_option(
            boost::asio::socket_base::reuse_address{true});
        acceptor_.bind(ep, ec);
        maybe_throw(ec, "bind");
        acceptor_.listen(
            boost::asio::socket_base::max_connections, ec);
        maybe_throw(ec, "listen");
        acceptor_.async_accept(sock_,
            std::bind(&sync_echo_peer::on_accept, this,
                beast::asio::placeholders::error));
        thread_ = std::thread{[&]{ ios_.run(); }};
    }

    ~sync_echo_peer()
    {
        error_code ec;
        ios_.dispatch(
            [&]{ acceptor_.close(ec); });
        thread_.join();
    }

    endpoint_type
    local_endpoint() const
    {
        return acceptor_.local_endpoint();
    }

private:
    static
    void
    fail(error_code ec, std::string what)
    {
        std::cerr <<
            what << ": " << ec.message() << std::endl;
    }

    static
    void
    fail(int id, error_code ec, std::string what)
    {
        std::cerr << "#" << std::to_string(id) << " " <<
            what << ": " << ec.message() << std::endl;
    }

    static
    void
    maybe_throw(error_code ec, std::string what)
    {
        if(ec)
        {
            fail(ec, what);
            throw ec;
        }
    }

    struct lambda
    {
        int id;
        sync_echo_peer& self;
        socket_type sock;
        boost::asio::io_service::work work;

        lambda(int id_, sync_echo_peer& self_,
                socket_type&& sock_)
            : id(id_)
            , self(self_)
            , sock(std::move(sock_))
            , work(sock.get_io_service())
        {
        }

        void operator()()
        {
            self.do_peer(id, std::move(sock));
        }
    };

    void
    on_accept(error_code ec)
    {
        if(ec == boost::asio::error::operation_aborted)
            return;
        maybe_throw(ec, "accept");
        static int id_ = 0;
        std::thread{lambda{++id_, *this, std::move(sock_)}}.detach();
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
    do_peer(int id, socket_type&& sock)
    {
        websocket::stream<socket_type> ws(std::move(sock));
        ws.set_option(decorate(identity{}));
        ws.set_option(read_message_max(64 * 1024 * 1024));
        error_code ec;
        ws.accept(ec);
        if(ec)
        {
            fail(id, ec, "accept");
            return;
        }
        for(;;)
        {
            websocket::opcode op;
            beast::streambuf sb;
            ws.read(op, sb, ec);
            if(ec)
                break;
            ws.set_option(websocket::message_type(op));
            ws.write(sb.data(), ec);
            if(ec)
                break;
        }
        if(ec && ec != websocket::error::closed)
        {
            fail(id, ec, "read");
        }
    }
};

} // websocket
} // beast

#endif
