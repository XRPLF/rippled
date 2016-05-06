//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/websocket/stream.hpp>

#include "websocket_async_echo_peer.hpp"
#include "websocket_sync_echo_peer.hpp"

#include <beast/bind_handler.hpp>
#include <beast/streambuf.hpp>
#include <beast/detail/unit_test/suite.hpp>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/optional.hpp>
#include <condition_variable>
#include <functional>
#include <mutex>
#include <thread>

#include <beast/http/parser_v1.hpp>

namespace beast {
namespace websocket {

class stream_test : public beast::detail::unit_test::suite
{
    boost::asio::io_service ios_;
    boost::optional<boost::asio::io_service::work> work_;
    std::thread thread_;
    std::mutex m_;
    std::condition_variable cv_;
    bool running_ = false;;

public:
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

    // meets the requirements of AsyncStream, SyncStream
    class string_Stream
    {
        std::string s_;
        boost::asio::io_service& ios_;

    public:
        string_Stream(boost::asio::io_service& ios,
                std::string s)
            : s_(s)
            , ios_(ios)
        {
        }

        boost::asio::io_service&
        get_io_service()
        {
            return ios_;
        }

        template<class MutableBufferSequence>
        std::size_t
        read_some(MutableBufferSequence const& buffers)
        {
            error_code ec;
            auto const n = read_some(buffers, ec);
            if(ec)
                throw boost::system::system_error{ec};
            return n;
        }

        template<class MutableBufferSequence>
        std::size_t
        read_some(MutableBufferSequence const& buffers,
            error_code& ec)
        {
            auto const n = boost::asio::buffer_copy(
                buffers, boost::asio::buffer(s_));
            s_.erase(0, n);
            return n;
        }

        template<class MutableBufferSequence, class ReadHandler>
        typename async_completion<ReadHandler,
            void(error_code, std::size_t)>::result_type
        async_read_some(MutableBufferSequence const& buffers,
            ReadHandler&& handler)
        {
            auto const n = boost::asio::buffer_copy(
                buffers, boost::asio::buffer(s_));
            s_.erase(0, n);
            async_completion<ReadHandler,
                void(error_code, std::size_t)> completion(handler);
            ios_.post(bind_handler(
                completion.handler, error_code{}, n));
            return completion.result.get();
        }

        template<class ConstBufferSequence>
        std::size_t
        write_some(ConstBufferSequence const& buffers)
        {
            error_code ec;
            auto const n = write_some(buffers, ec);
            if(ec)
                throw boost::system::system_error{ec};
            return n;
        }

        template<class ConstBufferSequence>
        std::size_t
        write_some(ConstBufferSequence const& buffers,
            error_code&)
        {
            return boost::asio::buffer_size(buffers);
        }

        template<class ConstBuffeSequence, class WriteHandler>
        typename async_completion<WriteHandler,
            void(error_code, std::size_t)>::result_type
        async_write_some(ConstBuffeSequence const& buffers,
            WriteHandler&& handler)
        {
            async_completion<WriteHandler,
                void(error_code, std::size_t)> completion(handler);
            ios_.post(bind_handler(completion.handler,
                error_code{}, boost::asio::buffer_size(buffers)));
            return completion.result.get();
        }
    };

    stream_test()
        : work_(ios_)
        , thread_([&]{ ios_.run(); })
    {
    }

    ~stream_test()
    {
        work_ = boost::none;
        thread_.join();
    }

    template<class Function>
    void exec(Function&& f)
    {
        {
            std::lock_guard<std::mutex> lock(m_);
            running_ = true;
        }
        boost::asio::spawn(ios_,
            [&](boost::asio::yield_context do_yield)
            {
                f(do_yield);
                std::lock_guard<std::mutex> lock(m_);
                running_ = false;
                cv_.notify_all();
            }
            , boost::coroutines::attributes(2 * 1024 * 1024));

        std::unique_lock<std::mutex> lock(m_);
        cv_.wait(lock, [&]{ return ! running_; });
    }

    void testSpecialMembers()
    {
        stream<socket_type> ws(ios_);
        {
            stream<socket_type> ws2(std::move(ws));
        }
        {
            stream<socket_type> ws2(ios_);
            ws = std::move(ws2);
        }
        pass();
    }

    void testOptions()
    {
        stream<socket_type> ws(ios_);
        ws.set_option(message_type(opcode::binary));
        ws.set_option(read_buffer_size(8192));
        ws.set_option(read_message_max(1 * 1024 * 1024));
        ws.set_option(write_buffer_size(2048));
        pass();
    }

    template<std::size_t N>
    static
    boost::asio::const_buffers_1
    strbuf(const char (&s)[N])
    {
        return boost::asio::const_buffers_1(&s[0], N-1);
    }

    void testAccept(boost::asio::yield_context do_yield)
    {
        {
            stream<string_Stream> ws(ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost:80\r\n"
                "Upgrade: WebSocket\r\n"
                "Connection: upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "\r\n");
            try
            {
                ws.accept();
                pass();
            }
            catch(...)
            {
                fail();
            }
        }
        {
            stream<string_Stream> ws(ios_,
                "GET / HTTP/1.1\r\n"
                "\r\n");
            try
            {
                ws.accept();
                fail();
            }
            catch(...)
            {
                pass();
            }
        }
        {
            stream<string_Stream> ws(ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost:80\r\n"
                "Upgrade: WebSocket\r\n"
                "Connection: upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "\r\n");
            error_code ec;
            ws.accept(ec);
            expect(! ec, ec.message());
        }
        {
            stream<string_Stream> ws(ios_,
                "GET / HTTP/1.1\r\n"
                "\r\n");
            error_code ec;
            ws.accept(ec);
            expect(ec);
        }
        {
            stream<string_Stream> ws(ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost:80\r\n"
                "Upgrade: WebSocket\r\n"
                "Connection: upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "\r\n");
            error_code ec;
            ws.async_accept(do_yield[ec]);
            expect(! ec, ec.message());
        }
        {
            stream<string_Stream> ws(ios_,
                "GET / HTTP/1.1\r\n"
                "\r\n");
            error_code ec;
            ws.async_accept(do_yield[ec]);
            expect(ec);
        }
        {
            stream<string_Stream> ws(ios_,
                "Host: localhost:80\r\n"
                "Upgrade: WebSocket\r\n"
                "Connection: upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "\r\n");
            try
            {
                ws.accept(strbuf(
                    "GET / HTTP/1.1\r\n"));
                pass();
            }
            catch(...)
            {
                fail();
            }
        }
        {
            stream<string_Stream> ws(ios_,
                "\r\n");
            try
            {
                ws.accept(strbuf(
                    "GET / HTTP/1.1\r\n"));
                fail();
            }
            catch(...)
            {
                pass();
            }
        }
        {
            stream<string_Stream> ws(ios_,
                "Host: localhost:80\r\n"
                "Upgrade: WebSocket\r\n"
                "Connection: upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "\r\n");
            error_code ec;
            ws.accept(strbuf(
                "GET / HTTP/1.1\r\n"), ec);
            expect(! ec, ec.message());
        }
        {
            stream<string_Stream> ws(ios_,
                "GET / HTTP/1.1\r\n"
                "\r\n");
            error_code ec;
            ws.accept(ec);
            expect(ec);
        }
        {
            stream<string_Stream> ws(ios_,
                "Host: localhost:80\r\n"
                "Upgrade: WebSocket\r\n"
                "Connection: upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "\r\n");
            error_code ec;
            ws.async_accept(strbuf(
                "GET / HTTP/1.1\r\n"), do_yield[ec]);
            expect(! ec, ec.message());
        }
        {
            stream<string_Stream> ws(ios_,
                "\r\n");
            error_code ec;
            ws.async_accept(strbuf(
                "GET / HTTP/1.1\r\n"), do_yield[ec]);
            expect(ec);
        }
    }

    void testHandshake(endpoint_type const& ep,
        boost::asio::yield_context do_yield)
    {
        {
            // disconnected socket
            socket_type sock(ios_);
            stream<decltype(sock)&> ws(sock);
            try
            {
                ws.handshake("localhost", "/");
                fail();
            }
            catch(boost::system::system_error const&)
            {
                pass();
            }
            catch(...)
            {
                fail();
            }
            error_code ec;
            ws.handshake("localhost", "/", ec);
            if(! expect(ec))
                return;
            ws.async_handshake("localhost", "/", do_yield[ec]);
            if(! expect(ec))
                return;
        }
        {
            error_code ec;
            socket_type sock(ios_);
            sock.connect(ep, ec);
            if(! expect(! ec, ec.message()))
                return;
            stream<decltype(sock)&> ws(sock);
            ws.handshake("localhost", "/", ec);
            if(! expect(! ec, ec.message()))
                return;
            ws.close({}, ec);
            if(! expect(! ec, ec.message()))
                return;
            streambuf sb;
            opcode op;
            ws.read(op, sb, ec);
            if(! expect(ec == error::closed, ec.message()))
                return;
            expect(ws.reason().code == close_code::normal);
        }
        {
            error_code ec;
            socket_type sock(ios_);
            sock.connect(ep, ec);
            if(! expect(! ec, ec.message()))
                return;
            stream<decltype(sock)&> ws(sock);
            ws.async_handshake("localhost", "/", do_yield[ec]);
            if(! expect(! ec, ec.message()))
                return;
            ws.async_close({}, do_yield[ec]);
            if(! expect(! ec, ec.message()))
                return;
            streambuf sb;
            opcode op;
            ws.async_read(op, sb, do_yield[ec]);
            if(! expect(ec == error::closed, ec.message()))
                return;
            expect(ws.reason().code == close_code::normal);
        }
    }

    void run() override
    {
        testSpecialMembers();

        testOptions();

        exec(std::bind(&stream_test::testAccept,
            this, std::placeholders::_1));

        auto const any = endpoint_type{
            address_type::from_string("127.0.0.1"), 0};
        {
            sync_echo_peer server(true, any);
            exec(std::bind(&stream_test::testHandshake,
                this, server.local_endpoint(),
                    std::placeholders::_1));
        }
        {
            async_echo_peer server(true, any, 1);
            exec(std::bind(&stream_test::testHandshake,
                this, server.local_endpoint(),
                    std::placeholders::_1));
        }

        pass();
    }
};

BEAST_DEFINE_TESTSUITE(stream,websocket,beast);

} // websocket
} // beast
