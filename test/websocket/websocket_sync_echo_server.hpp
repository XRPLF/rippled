//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#ifndef BEAST_WEBSOCKET_SYNC_ECHO_PEER_H_INCLUDED
#define BEAST_WEBSOCKET_SYNC_ECHO_PEER_H_INCLUDED

#include <beast/core/placeholders.hpp>
#include <beast/core/streambuf.hpp>
#include <beast/websocket.hpp>
#include <boost/lexical_cast.hpp>
#include <boost/optional.hpp>
#include <functional>
#include <iostream>
#include <memory>
#include <thread>

namespace beast {
namespace websocket {

// Synchronous WebSocket echo client/server
//
class sync_echo_server
{
public:
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

private:
    bool log_ = false;
    boost::asio::io_service ios_;
    socket_type sock_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::thread thread_;

public:
    sync_echo_server(bool /*server*/, endpoint_type ep)
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
            std::bind(&sync_echo_server::on_accept, this,
                beast::asio::placeholders::error));
        thread_ = std::thread{[&]{ ios_.run(); }};
    }

    ~sync_echo_server()
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
    void
    fail(error_code ec, std::string what)
    {
        if(log_)
            std::cerr <<
                what << ": " << ec.message() << std::endl;
    }

    void
    fail(int id, error_code ec, std::string what)
    {
        if(log_)
            std::cerr << "#" << boost::lexical_cast<std::string>(id) << " " <<
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

    struct lambda
    {
        int id;
        sync_echo_server& self;
        boost::asio::io_service::work work;
        // Must be destroyed before work otherwise the
        // io_service could be destroyed before the socket.
        socket_type sock;

        lambda(int id_, sync_echo_server& self_,
                socket_type&& sock_)
            : id(id_)
            , self(self_)
            , work(sock_.get_io_service())
            , sock(std::move(sock_))
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
            std::bind(&sync_echo_server::on_accept, this,
                beast::asio::placeholders::error));
    }

    struct identity
    {
        template<class Body, class Fields>
        void
        operator()(http::message<true, Body, Fields>& req)
        {
            req.fields.replace("User-Agent", "sync_echo_client");
        }

        template<class Body, class Fields>
        void
        operator()(http::message<false, Body, Fields>& resp)
        {
            resp.fields.replace("Server", "sync_echo_server");
        }
    };

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

    void
    do_peer(int id, socket_type&& sock)
    {
        using boost::asio::buffer;
        using boost::asio::buffer_copy;
        stream<socket_type> ws(std::move(sock));
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
            opcode op;
            beast::streambuf sb;
            ws.read(op, sb, ec);
            if(ec)
            {
                auto const s = ec.message();
                break;
            }
            ws.set_option(message_type(op));
            if(match(sb, "RAW"))
            {
                boost::asio::write(
                    ws.next_layer(), sb.data(), ec);
            }
            else if(match(sb, "TEXT"))
            {
                ws.set_option(message_type{opcode::text});
                ws.write(sb.data(), ec);
            }
            else if(match(sb, "PING"))
            {
                ping_data payload;
                sb.consume(buffer_copy(
                    buffer(payload.data(), payload.size()),
                        sb.data()));
                ws.ping(payload, ec);
            }
            else if(match(sb, "CLOSE"))
            {
                ws.close({}, ec);
            }
            else
            {
                ws.write(sb.data(), ec);
            }
            if(ec)
                break;
        }
        if(ec && ec != error::closed)
        {
            fail(id, ec, "read");
        }
    }
};

} // websocket
} // beast

#endif
