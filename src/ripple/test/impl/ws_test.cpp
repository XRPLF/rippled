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
#include <boost/asio/spawn.hpp>
#include <boost/optional.hpp>
#include <functional>
#include <condition_variable>
#include <memory>
#include <mutex>
#include <thread>

namespace beast {
namespace wsproto {

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
class WSAsyncEchoPeer
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
            unit_test::suite& suite;
            boost::optional<endpoint_type> ep;
            wsproto::socket<socket_type> ws;
            wsproto::opcode::value op;
            beast::asio::streambuf sb;
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

        template<class... Args>
        explicit
        Peer(Args&&... args)
            : d_(std::make_shared<data>(
                std::forward<Args>(args)...))
        {
            run();
        }

        void run()
        {
            auto& d = *d_;
            d.ws.set_option(beast::wsproto::decorator(
                [&](auto& m)
                {
                    if(d.ep)
                        m.headers.append("Server", "AsyncEchoClient");
                    else
                        m.headers.append("Server", "AsyncEchoServer");
                }));
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
            using namespace boost::asio;
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
                wsproto::async_read(
                    d.ws, d.op, d.sb, std::move(*this));
                return;

            // got message
            case 2:
                if(ec == wsproto::error::closed)
                    return;
                if(ec)
                    return fail(ec, "async_read");
                // write message
                d.state = 1;
                wsproto::async_write_msg(
                    d.ws, d.op, d.sb.data(),
                        std::move(*this));
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

    unit_test::suite& suite_;
    boost::asio::io_service ios_;
    socket_type sock_;
    boost::asio::ip::tcp::acceptor acceptor_;
    std::vector<unit_test::thread> thread_;

public:
    WSAsyncEchoPeer(bool server,
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
                std::bind(&WSAsyncEchoPeer::on_accept, this,
                    beast::asio::placeholders::error));
        }
        else
        {
            Peer{std::move(sock_), ep, suite_};
        }
#if 0
        auto const n = std::min<std::size_t>(
                std::thread::hardware_concurrency(), 12);
#else
        auto const n = 1;
#endif
        thread_.reserve(n);
        for(int i = 0; i < n; ++i)
            thread_.emplace_back(suite_,
                [&] { ios_.run(); });
    }

    ~WSAsyncEchoPeer()
    {
        error_code ec;
        acceptor_.close(ec);
        for(auto& t : thread_)
            t.join();
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
        if(! acceptor_.is_open())
            return;
        maybe_throw(ec, "accept");
        socket_type sock(std::move(sock_));
        acceptor_.async_accept(sock_,
            std::bind(&WSAsyncEchoPeer::on_accept, this,
                beast::asio::placeholders::error));
        Peer{std::move(sock), suite_};
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
        if(ec == boost::asio::error::operation_aborted)
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
        ws.set_option(beast::wsproto::decorator(
            [&](auto& m)
            {
                m.headers.append("Server", "WSEchoServer");
            }));
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
            wsproto::read(ws, op, sb, ec);
            if(ec)
                break;
            wsproto::write_msg(ws, op, sb.data(), ec);
            if(ec)
                break;
        }
        if(ec == wsproto::error::closed)
        {
            ws.next_layer().shutdown(
                boost::asio::ip::tcp::socket::shutdown_send, ec);
            while(! ec)
            {
                char buf[65536];
                auto const n = ws.next_layer().read_some(
                    boost::asio::buffer(buf), ec);
                if(! n)
                    break;
            }
            ws.next_layer().close(ec);
        }
        else if(ec && ec != boost::asio::error::eof)
        {
            fail(ec, "read");
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

    endpoint_type ep_;

    void
    maybe_fail(error_code const& ec, std::string const& what)
    {
        expect(! ec, what + ": " + ec.message());
    }

    void
    maybe_throw(error_code ec, std::string what)
    {
        if(ec)
        {
            maybe_fail(ec, what);
            throw ec;
        }
    }

    int
    request(std::string const& s)
    {
        using namespace boost::asio;
        io_service ios;
        ip::tcp::socket sock(ios);
        sock.connect(ep_);
        write(sock, asio::append_buffers(
            buffer(s), buffer("\r\n")));
        http::body b;
        http::message m;
        http::parser p(m, b, false);
        streambuf sb;
        read_until(sock, sb, "\r\n\r\n");
        auto const result = p.write(sb.data());
        sock.shutdown(socket_type::shutdown_both);
        sock.close();
        if(! p.complete() || result.first)
            return -1;
        return m.status();
    }

    void
    check(int status, std::string const& s)
    {
        expect(request(s) == status);
    }

    void
    testInvokable()
    {
        using error_code = boost::system::error_code;
        using endpoint_type = boost::asio::ip::tcp::endpoint;
        using address_type = boost::asio::ip::address;
        using socket_type = boost::asio::ip::tcp::socket;
    
        endpoint_type const ep{
            address_type::from_string("127.0.0.1"), 6000};
        boost::asio::io_service ios1;
        boost::asio::spawn(ios1,
            [&](auto yield)
            {
                error_code ec;
                boost::asio::ip::tcp::acceptor acceptor(ios1);
                acceptor.open(ep.protocol(), ec);
                maybe_throw(ec, "open");
                acceptor.bind(ep, ec);
                maybe_throw(ec, "bind");
                acceptor.listen(
                    boost::asio::socket_base::max_connections, ec);
                maybe_throw(ec, "listen");
                socket_type sock(ios1);
                acceptor.async_accept(sock, yield[ec]);
                maybe_throw(ec, "accept");
                socket<socket_type&> ws(sock);
                ws.async_accept(yield[ec]);
                log << "accepted";
            });

        boost::asio::io_service ios2;
        boost::asio::spawn(ios2,
            [&](boost::asio::yield_context yield)
            {
                error_code ec;
                endpoint_type const ep{
                    address_type::from_string(
                        "127.0.0.1"), 6000};
                socket_type sock(ios2);
                sock.async_connect(ep, yield[ec]);
                maybe_throw(ec, "connect");
                socket<socket_type&> ws(sock);
                ws.async_handshake(ep.address().to_string() + ":" +
                    std::to_string(ep.port()), "/", yield[ec]);
                log << "handshaked";
            });

        ios1.run_one(); // async_accept
        ios2.run_one(); //                      async_connect
        ios1.run_one(); // async_accept(ws)
        ios2.run_one(); //                      async_handshake
        ios1.run_one(); //...
    }

    void
    testHandshake(endpoint_type ep)
    {
        ep_ = ep;

        check(40, "GET / HTTP/1.0\r\n");
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
        read(ws, op, sb, ec);
        expect(op == wsproto::opcode::text);
        expect(buffers_to_string(sb.data()) == s);
        maybe_fail(ec, "read");
        ws.close(0, "", ec);
        maybe_fail(ec, "close");
    }

    void
    run() override
    {
        testInvokable();

        endpoint_type ep{
            address_type::from_string("127.0.0.1"), 6000};

        {
            testcase("Echo Server");
            WSEchoServer s(ep, *this);
            testHandshake(ep);
            syncEchoClient(ep);
        }

        {
            testcase("Async Echo Server");
            WSAsyncEchoPeer s(true, ep, *this);
            testHandshake(ep);
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
        WSAsyncEchoPeer s1(true, endpoint_type{
            address_type::from_string("127.0.0.1"),
                6000 }, *this);

        WSEchoServer s2(endpoint_type{
            address_type::from_string("127.0.0.1"),
                6001 }, *this);

        boost::asio::io_service ios;
        boost::asio::signal_set signals(
            ios, SIGINT, SIGTERM);
        std::mutex m;
        bool stop = false;
        std::condition_variable cv;
        signals.async_wait(
            [&](boost::system::error_code const& ec,
                int signal_number)
            {
                std::lock_guard<std::mutex> lock(m);
                stop = true;
                cv.notify_one();
            });
        std::unique_lock<std::mutex> lock(m);
        cv.wait(lock, [&]{ return stop; });
    }
};

class ws_client_test : public unit_test::suite
{
public:
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;

    void
    run() override
    {
        pass();
        {
            WSAsyncEchoPeer s1(false, endpoint_type{
                address_type::from_string("127.0.0.1"),
                    9001 }, *this);
        }
#if 0
        {
            WSEchoServer s2(false, endpoint_type{
                address_type::from_string("127.0.0.1"),
                    9001 }, *this);
        }
#endif
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(ws_server, asio, beast);
BEAST_DEFINE_TESTSUITE_MANUAL(ws_client, asio, beast);

} // wsproto
} // beast
