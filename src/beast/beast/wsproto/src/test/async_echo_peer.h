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

#ifndef BEAST_WSPROTO_ASYNC_ECHO_PEER_H_INCLUDED
#define BEAST_WSPROTO_ASYNC_ECHO_PEER_H_INCLUDED

#include <beast/unit_test/suite.h>
#include <beast/unit_test/thread.h>
#include <beast/asio/placeholders.h>
#include <beast/asio/streambuf.h>
#include <beast/wsproto.h>
#include <boost/optional.hpp>
#include <functional>
#include <memory>
#include <thread>

namespace beast {
namespace wsproto {
namespace test {

// Asynchronous WebSocket echo client/server
//
class async_echo_peer
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
    std::vector<unit_test::thread> thread_;
    std::size_t n_ = 0;

public:
    async_echo_peer(bool server,
            endpoint_type const& ep, unit_test::suite& suite)
        : suite_(suite)
        , sock_(ios_)
        , acceptor_(ios_)
    {
        if(server)
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
                std::bind(&async_echo_peer::on_accept, this,
                    beast::asio::placeholders::error));
        }
        else
        {
            Peer{std::move(sock_), ep, suite_};
        }
        auto const n = 1;
        thread_.reserve(n);
        for(int i = 0; i < n; ++i)
            thread_.emplace_back(suite_,
                [&] { ios_.run(); });
    }

    ~async_echo_peer()
    {
        error_code ec;
        ios_.dispatch(
            [&]{ acceptor_.close(ec); });
        for(auto& t : thread_)
            t.join();
    }

private:
    class Peer
    {
        struct data
        {
            int state = 0;
            unit_test::suite& suite;
            boost::optional<endpoint_type> ep;
            wsproto::socket<socket_type> ws;
            wsproto::opcode op;
            beast::streambuf sb;
            int id;

            data(socket_type&& sock_,
                    unit_test::suite& suite_)
                : suite(suite_)
                , ws(std::move(sock_))
                , id([]
                    {
                        static int n = 0;
                        return ++n;
                    }())
            {
            }

            data(socket_type&& sock_, endpoint_type const& ep_,
                    unit_test::suite& suite_)
                : suite(suite_)
                , ep(ep_)
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
            //d.ws.set_option(auto_fragment_size(0));
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
                if(ec == wsproto::error::closed)
                    return;
                if(ec)
                    return fail(ec, "async_read");
                // write message
                d.state = 1;
                d.ws.set_option(wsproto::message_type(d.op));
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
            if(ec != wsproto::error::closed)
            {
                d_->suite.log <<
                    "#" << std::to_string(d_->id) << " " <<
                    what << ": " << ec.message();
            }
        }
    };

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
        if(! acceptor_.is_open())
            return;
        maybe_throw(ec, "accept");
        socket_type sock(std::move(sock_));
        if(n_ < autobahnCycles)
            acceptor_.async_accept(sock_,
                std::bind(&async_echo_peer::on_accept, this,
                    beast::asio::placeholders::error));
        Peer{std::move(sock), suite_};
    }
};

} // test
} // wsproto
} // beast

#endif
