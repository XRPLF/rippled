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

#ifndef BEAST_WEBSOCKET_ASYNC_ECHO_PEER_H_INCLUDED
#define BEAST_WEBSOCKET_ASYNC_ECHO_PEER_H_INCLUDED

#include <beast/placeholders.hpp>
#include <beast/streambuf.hpp>
#include <beast/websocket.hpp>
#include <boost/optional.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

namespace beast {
namespace websocket {

// Asynchronous WebSocket echo client/server
//
class async_echo_peer
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
    std::vector<std::thread> thread_;

public:
    async_echo_peer(bool server,
            endpoint_type const& ep, std::size_t threads)
        : sock_(ios_)
        , acceptor_(ios_)
    {
        if(server)
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
                std::bind(&async_echo_peer::on_accept, this,
                    beast::asio::placeholders::error));
        }
        else
        {
            Peer{std::move(sock_), ep};
        }
        thread_.reserve(threads);
        for(std::size_t i = 0; i < threads; ++i)
            thread_.emplace_back(
                [&]{ ios_.run(); });
    }

    ~async_echo_peer()
    {
        error_code ec;
        ios_.dispatch(
            [&]{ acceptor_.close(ec); });
        for(auto& t : thread_)
            t.join();
    }

    endpoint_type
    local_endpoint() const
    {
        return acceptor_.local_endpoint();
    }

private:
    class Peer
    {
        struct data
        {
            int state = 0;
            boost::optional<endpoint_type> ep;
            websocket::stream<socket_type> ws;
            websocket::opcode op;
            beast::streambuf sb;
            int id;

            data(socket_type&& sock_)
                : ws(std::move(sock_))
                , id([]
                    {
                        static int n = 0;
                        return ++n;
                    }())
            {
            }

            data(socket_type&& sock_,
                    endpoint_type const& ep_)
                : ep(ep_)
                , ws(std::move(sock_))
                , id([]
                    {
                        static int n = 0;
                        return ++n;
                    }())
            {
            }
        };

        std::shared_ptr<data> d_;

    public:
        Peer(Peer&&) = default;
        Peer(Peer const&) = default;
        Peer& operator=(Peer&&) = delete;
        Peer& operator=(Peer const&) = delete;

        struct identity
        {
            template<class Body, class Headers>
            void
            operator()(http::message<true, Body, Headers>& req)
            {
                req.headers.replace("User-Agent", "async_echo_client");
            }

            template<class Body, class Headers>
            void
            operator()(http::message<false, Body, Headers>& resp)
            {
                resp.headers.replace("Server", "async_echo_server");
            }
        };

        template<class... Args>
        explicit
        Peer(socket_type&& sock, Args&&... args)
            : d_(std::make_shared<data>(
                std::forward<socket_type>(sock),
                    std::forward<Args>(args)...))
        {
            auto& d = *d_;
            d.ws.set_option(decorate(identity{}));
            d.ws.set_option(read_message_max(64 * 1024 * 1024));
            run();
        }

        void run()
        {
            auto& d = *d_;
            if(! d.ep)
            {
                d.ws.async_accept(std::move(*this));
            }
            else
            {
                d.state = 4;
                d.ws.next_layer().async_connect(
                    *d.ep, std::move(*this));
            }
        }

        void operator()(error_code ec)
        {
            auto& d = *d_;
            switch(d_->state)
            {
            // did accept
            case 0:
                if(ec)
                    return fail(ec, "async_accept");

            // start
            case 1:
                if(ec)
                    return fail(ec, "async_handshake");
                d.sb.consume(d.sb.size());
                // read message
                d.state = 2;
                d.ws.async_read(d.op, d.sb, std::move(*this));
                return;

            // got message
            case 2:
                if(ec == websocket::error::closed)
                    return;
                if(ec)
                    return fail(ec, "async_read");
                // write message
                d.state = 1;
                d.ws.set_option(websocket::message_type(d.op));
                d.ws.async_write(d.sb.data(), std::move(*this));
                return;

            // connected
            case 4:
                if(ec)
                    return fail(ec, "async_connect");
                d.state = 1;
                d.ws.async_handshake(
                    d.ep->address().to_string() + ":" +
                        std::to_string(d.ep->port()),
                            "/", std::move(*this));
                return;
            }
        }

    private:
        void
        fail(error_code ec, std::string what)
        {
            if(ec != websocket::error::closed)
                std::cerr << "#" << d_->id << " " <<
                    what << ": " << ec.message() << std::endl;
        }
    };

    void
    fail(error_code ec, std::string what)
    {
        std::cerr <<
            what << ": " << ec.message() << std::endl;
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
        if(! acceptor_.is_open())
            return;
        maybe_throw(ec, "accept");
        socket_type sock(std::move(sock_));
        acceptor_.async_accept(sock_,
            std::bind(&async_echo_peer::on_accept, this,
                beast::asio::placeholders::error));
        Peer{std::move(sock)};
    }
};

} // websocket
} // beast

#endif
