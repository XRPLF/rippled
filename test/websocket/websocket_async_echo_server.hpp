//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_ASYNC_ECHO_PEER_H_INCLUDED
#define BEAST_WEBSOCKET_ASYNC_ECHO_PEER_H_INCLUDED

#include <beast/core/placeholders.hpp>
#include <beast/core/streambuf.hpp>
#include <beast/websocket.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

#include <ostream>

namespace beast {
namespace websocket {

// Asynchronous WebSocket echo client/server
//
class async_echo_server
{
public:
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

private:
    std::ostream* log_;
    boost::asio::io_service ios_;
    socket_type sock_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::vector<std::thread> thread_;
    boost::optional<boost::asio::io_service::work> work_;

public:
    async_echo_server(async_echo_server const&) = delete;
    async_echo_server& operator=(async_echo_server const&) = delete;

    async_echo_server(std::ostream* log,
            std::size_t threads)
        : log_(log)
        , sock_(ios_)
        , acceptor_(ios_)
        , work_(ios_)
    {
        thread_.reserve(threads);
        for(std::size_t i = 0; i < threads; ++i)
            thread_.emplace_back(
                [&]{ ios_.run(); });
    }

    ~async_echo_server()
    {
        work_ = boost::none;
        error_code ec;
        ios_.dispatch(
            [&]{ acceptor_.close(ec); });
        for(auto& t : thread_)
            t.join();
    }

    void
    open(bool server,
        endpoint_type const& ep, error_code& ec)
    {
        if(server)
        {
            acceptor_.open(ep.protocol(), ec);
            if(ec)
            {
                if(log_)
                    (*log_) << "open: " << ec.message() << std::endl;
                return;
            }
            acceptor_.set_option(
                boost::asio::socket_base::reuse_address{true});
            acceptor_.bind(ep, ec);
            if(ec)
            {
                if(log_)
                    (*log_) << "bind: " << ec.message() << std::endl;
                return;
            }
            acceptor_.listen(
                boost::asio::socket_base::max_connections, ec);
            if(ec)
            {
                if(log_)
                    (*log_) << "listen: " << ec.message() << std::endl;
                return;
            }
            acceptor_.async_accept(sock_,
                std::bind(&async_echo_server::on_accept, this,
                    beast::asio::placeholders::error));
        }
        else
        {
            Peer{*this, std::move(sock_), ep};
        }
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
            async_echo_server& server;
            int state = 0;
            boost::optional<endpoint_type> ep;
            stream<socket_type> ws;
            boost::asio::io_service::strand strand;
            opcode op;
            beast::streambuf db;
            int id;

            data(async_echo_server& server_,
                    socket_type&& sock_)
                : server(server_)
                , ws(std::move(sock_))
                , strand(ws.get_io_service())
                , id([]
                    {
                        static int n = 0;
                        return ++n;
                    }())
            {
            }

            data(async_echo_server& server_,
                    socket_type&& sock_, endpoint_type const& ep_)
                : server(server_)
                , ep(ep_)
                , ws(std::move(sock_))
                , strand(ws.get_io_service())
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
            template<class Body, class Fields>
            void
            operator()(http::message<true, Body, Fields>& req)
            {
                req.fields.replace("User-Agent", "async_echo_client");
            }

            template<class Body, class Fields>
            void
            operator()(http::message<false, Body, Fields>& resp)
            {
                resp.fields.replace("Server", "async_echo_server");
            }
        };

        template<class... Args>
        explicit
        Peer(async_echo_server& server,
                socket_type&& sock, Args&&... args)
            : d_(std::make_shared<data>(server,
                std::forward<socket_type>(sock),
                    std::forward<Args>(args)...))
        {
            auto& d = *d_;
            d.ws.set_option(decorate(identity{}));
            d.ws.set_option(read_message_max(64 * 1024 * 1024));
            d.ws.set_option(auto_fragment{false});
            //d.ws.set_option(write_buffer_size{64 * 1024});
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

        template<class DynamicBuffer, std::size_t N>
        static
        bool
        match(DynamicBuffer& db, char const(&s)[N])
        {
            using boost::asio::buffer;
            using boost::asio::buffer_copy;
            if(db.size() < N-1)
                return false;
            static_string<N-1> t;
            t.resize(N-1);
            buffer_copy(buffer(t.data(), t.size()),
                db.data());
            if(t != s)
                return false;
            db.consume(N-1);
            return true;
        }

        void operator()(error_code ec, std::size_t)
        {
            (*this)(ec);
        }

        void operator()(error_code ec)
        {
            using boost::asio::buffer;
            using boost::asio::buffer_copy;
            auto& d = *d_;
            switch(d.state)
            {
            // did accept
            case 0:
                if(ec)
                    return fail(ec, "async_accept");

            // start
            case 1:
                if(ec)
                    return fail(ec, "async_handshake");
                d.db.consume(d.db.size());
                // read message
                d.state = 2;
                d.ws.async_read(d.op, d.db,
                    d.strand.wrap(std::move(*this)));
                return;

            // got message
            case 2:
                if(ec == error::closed)
                    return;
                if(ec)
                    return fail(ec, "async_read");
                if(match(d.db, "RAW"))
                {
                    d.state = 1;
                    boost::asio::async_write(d.ws.next_layer(),
                        d.db.data(), d.strand.wrap(std::move(*this)));
                    return;
                }
                else if(match(d.db, "TEXT"))
                {
                    d.state = 1;
                    d.ws.set_option(message_type{opcode::text});
                    d.ws.async_write(
                        d.db.data(), d.strand.wrap(std::move(*this)));
                    return;
                }
                else if(match(d.db, "PING"))
                {
                    ping_data payload;
                    d.db.consume(buffer_copy(
                        buffer(payload.data(), payload.size()),
                            d.db.data()));
                    d.state = 1;
                    d.ws.async_ping(payload,
                        d.strand.wrap(std::move(*this)));
                    return;
                }
                else if(match(d.db, "CLOSE"))
                {
                    d.state = 1;
                    d.ws.async_close({},
                        d.strand.wrap(std::move(*this)));
                    return;
                }
                // write message
                d.state = 1;
                d.ws.set_option(message_type(d.op));
                d.ws.async_write(d.db.data(),
                    d.strand.wrap(std::move(*this)));
                return;

            // connected
            case 4:
                if(ec)
                    return fail(ec, "async_connect");
                d.state = 1;
                d.ws.async_handshake(
                    d.ep->address().to_string() + ":" +
                        boost::lexical_cast<std::string>(d.ep->port()),
                            "/", d.strand.wrap(std::move(*this)));
                return;
            }
        }

    private:
        void
        fail(error_code ec, std::string what)
        {
            auto& d = *d_;
            if(d.server.log_)
            {
                if(ec != error::closed)
                    (*d.server.log_) << "#" << d.id << " " <<
                        what << ": " << ec.message() << std::endl;
            }
        }
    };

    void
    fail(error_code ec, std::string what)
    {
        if(log_)
            (*log_) << what << ": " <<
                ec.message() << std::endl;
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
        if(ec == boost::asio::error::operation_aborted)
            return;
        maybe_throw(ec, "accept");
        socket_type sock(std::move(sock_));
        acceptor_.async_accept(sock_,
            std::bind(&async_echo_server::on_accept, this,
                beast::asio::placeholders::error));
        Peer{*this, std::move(sock)};
    }
};

} // websocket
} // beast

#endif
