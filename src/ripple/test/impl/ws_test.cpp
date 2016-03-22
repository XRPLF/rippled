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

#include <BeastConfig.h>
#include <beast/asio/placeholders.h>
#include <beast/wsproto.h>
#include <beast/unit_test/suite.h>
#include <beast/unit_test/thread.h>
#include <boost/asio.hpp>
#include <functional>
#include <memory>
#include <thread>

namespace beast {
namespace test {

template<class Buffers>
static
std::string
buffers_to_string(Buffers const& bs)
{
    using namespace boost::asio;
    std::string s;
    s.reserve(buffer_size(bs));
    for(auto const& b : bs)
        s.append(buffer_cast<char const*>(b),
            buffer_size(b));
    return s;
}

//------------------------------------------------------------------------------

// Asynchronous WebSocket echo server
//
class WSAsyncEchoServer
{
public:
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

private:
    class Peer
    {
        struct data
        {
            int state = 0;
            wsproto::socket<socket_type> ws;
            unit_test::suite& suite;
            wsproto::opcode::value op;
            beast::asio::streambuf sb;

            data(socket_type&& sock_,
                    unit_test::suite& suite_)
                : ws(std::move(sock_))
                , suite(suite_)
            {
            }
        };

        std::shared_ptr<data> d_;

    public:
        template<class... Args>
        Peer(Args&&... args)
            : d_(std::make_shared<data>(
                std::forward<Args>(args)...))
        {
        }

        void operator()()
        {
            auto& d = *d_;
            d.ws.async_accept(std::move(*this));
        }

        void operator()(error_code ec)
        {
            auto& d = *d_;
            switch(d_->state)
            {
            // did accept
            case 0:
                if(ec)
                    return fail(ec, "accept");

            // start
            case 1:
                if(ec)
                    return fail(ec, "write_msg");
                d.sb.consume(d.sb.size());
                d.state = 2;
                // read message
                wsproto::async_read_msg(
                    d.ws, d.op, d.sb, std::move(*this));
                return;

            // got message
            case 2:
                if(ec == boost::asio::error::eof)
                    return;
                if(ec)
                    return fail(ec, "read_msg");
                d.state = 1;
                // write message
                wsproto::async_write_msg(
                    d.ws, d.op, d.sb.data(),
                        std::move(*this));
                return;
            }
        }

    private:
        void
        fail(error_code ec, std::string what)
        {
            d_->suite.log <<
                "server " << what << ": " << ec.message();
        }
    };

    unit_test::suite& suite_;
    boost::asio::io_service ios_;
    socket_type sock_;
    boost::asio::ip::tcp::acceptor acceptor_;
    unit_test::thread thread_;

public:
    WSAsyncEchoServer(endpoint_type ep,
            unit_test::suite& suite)
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
            std::bind(&WSAsyncEchoServer::on_accept, this,
                beast::asio::placeholders::error));
        thread_ = unit_test::thread(suite_,
            [&]
            {
                ios_.run();
            });
    }

    ~WSAsyncEchoServer()
    {
        error_code ec;
        acceptor_.close(ec);
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
        Peer{std::move(sock_), suite_}();
        acceptor_.async_accept(sock_,
            std::bind(&WSAsyncEchoServer::on_accept, this,
                beast::asio::placeholders::error));
    }
};

//------------------------------------------------------------------------------

// Synchronous WebSocket echo server
//
class WSEchoServer
{
public:
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

public:
    WSEchoServer(endpoint_type ep, unit_test::suite& suite)
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
            std::bind(&WSEchoServer::on_accept, this,
                beast::asio::placeholders::error));
        thread_ = unit_test::thread(suite_,
            [&]
            {
                ios_.run();
            });
    }

    ~WSEchoServer()
    {
        error_code ec;
        acceptor_.close(ec);
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
        using namespace boost::asio;
        if(ec == error::operation_aborted)
            return;
        maybe_throw(ec, "accept");
        std::thread{
            [
                this,
                sock = std::move(sock_),
                work = io_service::work{ios_}
            ]() mutable
            {
                do_peer(std::move(sock));
            }}.detach();
        acceptor_.async_accept(sock_,
            std::bind(&WSEchoServer::on_accept, this,
                beast::asio::placeholders::error));
    }

    void
    do_peer(socket_type&& sock)
    {
        wsproto::socket<socket_type> ws(std::move(sock));
        error_code ec;
        ws.accept(ec);
        if(ec)
        {
            fail(ec, "accept");
            return;
        }
        for(;;)
        {
            wsproto::opcode::value op;
            beast::asio::streambuf sb;
            wsproto::read_msg(ws, op, sb, ec);
            if(ec)
                break;
            wsproto::write_msg(ws, op, sb.data(), ec);
            if(ec)
                break;
        }
    }
};

//------------------------------------------------------------------------------

class ws_test : public unit_test::suite
{
public:
    using error_code = boost::system::error_code;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

    void
    maybe_fail(error_code const& ec, std::string const& what)
    {
        expect(! ec, what + ": " + ec.message());
    }

    void
    syncEchoClient(endpoint_type ep)
    {
        using namespace boost::asio;
        error_code ec;
        io_service ios;
        wsproto::socket<socket_type> ws(ios);
        ws.next_layer().connect(ep, ec);
        maybe_fail(ec, "connect");
        ws.handshake(ep.address().to_string(), "/", ec);
        maybe_fail(ec, "upgrade");
        std::string const s = "Hello, world!";
        ws.write(wsproto::opcode::text, true, buffer(s), ec);
        maybe_fail(ec, "write");
        streambuf sb;
        wsproto::opcode::value op;
        read_msg(ws, op, sb, ec);
        maybe_fail(ec, "read");
        expect(op == wsproto::opcode::text);
        expect(buffers_to_string(sb.data()) == s);
        ws.close(0, "", ec);
        maybe_fail(ec, "close");
    }

    void
    run() override
    {
        endpoint_type ep{
            address_type::from_string("127.0.0.1"), 6000};
        {
            WSAsyncEchoServer s(ep, *this);
            syncEchoClient(ep);
        }

        {
            WSEchoServer s(ep, *this);
            syncEchoClient(ep);
        }
    }
};

BEAST_DEFINE_TESTSUITE(ws,asio,beast);

//------------------------------------------------------------------------------

class ws_server_test : public unit_test::suite
{
public:
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;

    void
    run() override
    {
        WSAsyncEchoServer s1(endpoint_type{
            address_type::from_string("127.0.0.1"),
                6000 }, *this);

        WSEchoServer s2(endpoint_type{
            address_type::from_string("127.0.0.1"),
                6001 }, *this);

        for(;;)
        {
        }
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(ws_server, asio, beast);

} // test
} // beast

