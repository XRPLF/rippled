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

#include <beast/streambuf.hpp>
#include <beast/to_string.hpp>
#include <beast/test/fail_stream.hpp>
#include <beast/test/string_stream.hpp>
#include <beast/test/yield_to.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/optional.hpp>

namespace beast {
namespace websocket {

class stream_test
    : public beast::unit_test::suite
    , public test::enable_yield_to
{
public:
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

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
        expect(&ws.get_io_service() == &ios_);
        pass();
    }

    void testOptions()
    {
        stream<socket_type> ws(ios_);
        ws.set_option(message_type(opcode::binary));
        ws.set_option(read_buffer_size(8192));
        ws.set_option(read_message_max(1 * 1024 * 1024));
        ws.set_option(write_buffer_size(2048));
        try
        {
            ws.set_option(message_type(opcode::close));
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }
    }

    template<std::size_t N>
    static
    boost::asio::const_buffers_1
    strbuf(const char (&s)[N])
    {
        return boost::asio::const_buffers_1(&s[0], N-1);
    }

    void testAccept(yield_context do_yield)
    {
        {
            stream<test::string_stream> ws(ios_,
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
            stream<test::string_stream> ws(ios_,
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
            stream<test::string_stream> ws(ios_,
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
            stream<test::string_stream> ws(ios_,
                "GET / HTTP/1.1\r\n"
                "\r\n");
            error_code ec;
            ws.accept(ec);
            expect(ec);
        }
        {
            stream<test::string_stream> ws(ios_,
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
            stream<test::string_stream> ws(ios_,
                "GET / HTTP/1.1\r\n"
                "\r\n");
            error_code ec;
            ws.async_accept(do_yield[ec]);
            expect(ec);
        }
        {
            stream<test::string_stream> ws(ios_,
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
            stream<test::string_stream> ws(ios_,
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
            stream<test::string_stream> ws(ios_,
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
            stream<test::string_stream> ws(ios_,
                "GET / HTTP/1.1\r\n"
                "\r\n");
            error_code ec;
            ws.accept(ec);
            expect(ec);
        }
        {
            stream<test::string_stream> ws(ios_,
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
            stream<test::string_stream> ws(ios_,
                "\r\n");
            error_code ec;
            ws.async_accept(strbuf(
                "GET / HTTP/1.1\r\n"), do_yield[ec]);
            expect(ec);
        }
    }

    void testHandshake(endpoint_type const& ep,
        yield_context do_yield)
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

    void testErrorHandling(endpoint_type const& ep,
        yield_context do_yield)
    {
        static std::size_t constexpr limit = 100;
        std::size_t n;

        // synchronous, exceptions
        for(n = 1; n < limit; ++n)
        {
            error_code ec;
            socket_type sock(ios_);
            sock.connect(ep, ec);
            if(! expect(! ec, ec.message()))
                break;
            stream<test::fail_stream<socket_type&>> ws(n, sock);
            try
            {
                ws.handshake("localhost", "/");
                ws.write(boost::asio::const_buffers_1(
                    "Hello", 5));
                opcode op;
                streambuf sb;
                ws.read(op, sb);
                expect(op == opcode::text);
                expect(to_string(sb.data()) == "Hello");
                ws.close({});
                try
                {
                    ws.read(op, sb);
                }
                catch(boost::system::system_error const& se)
                {
                    if(se.code() == error::closed)
                        break;
                    throw;
                }
                fail();
                break;
            }
            catch(boost::system::system_error const&)
            {
            }
        }
        expect(n < limit);

        // synchronous, error codes
        for(n = 1; n < limit; ++n)
        {
            error_code ec;
            socket_type sock(ios_);
            sock.connect(ep, ec);
            if(! expect(! ec, ec.message()))
                break;
            stream<test::fail_stream<socket_type&>> ws(n, sock);
            ws.handshake("localhost", "/", ec);
            if(ec)
                continue;
            ws.write(boost::asio::const_buffers_1(
                "Hello", 5), ec);
            if(ec)
                continue;
            opcode op;
            streambuf sb;
            ws.read(op, sb, ec);
            if(ec)
                continue;
            expect(op == opcode::text);
            expect(to_string(sb.data()) == "Hello");
            ws.close({}, ec);
            if(ec)
                continue;
            ws.read(op, sb, ec);
            if(ec == error::closed)
            {
                pass();
                break;
            }
        }
        expect(n < limit);

        // asynchronous
        for(n = 1; n < limit; ++n)
        {
            error_code ec;
            socket_type sock(ios_);
            sock.connect(ep, ec);
            if(! expect(! ec, ec.message()))
                break;
            stream<test::fail_stream<socket_type&>> ws(n, sock);
            ws.async_handshake("localhost", "/", do_yield[ec]);
            if(ec)
                break;
            ws.async_write(boost::asio::const_buffers_1(
                "Hello", 5), do_yield[ec]);
            if(ec)
                continue;
            opcode op;
            streambuf sb;
            ws.async_read(op, sb, do_yield[ec]);
            if(ec)
                continue;
            expect(op == opcode::text);
            expect(to_string(sb.data()) == "Hello");
            ws.async_close({}, do_yield[ec]);
            if(ec)
                continue;
            ws.async_read(op, sb, do_yield[ec]);
            if(ec == error::closed)
            {
                pass();
                break;
            }
        }
        expect(n < limit);
    }

    void testMask(endpoint_type const& ep,
        yield_context do_yield)
    {
        {
            std::vector<char> v;
            for(char n = 0; n < 20; ++n)
            {
                error_code ec;
                socket_type sock(ios_);
                sock.connect(ep, ec);
                if(! expect(! ec, ec.message()))
                    break;
                stream<socket_type&> ws(sock);
                ws.handshake("localhost", "/", ec);
                if(! expect(! ec, ec.message()))
                    break;
                ws.write(boost::asio::buffer(v), ec);
                if(! expect(! ec, ec.message()))
                    break;
                opcode op;
                streambuf sb;
                ws.read(op, sb, ec);
                if(! expect(! ec, ec.message()))
                    break;
                expect(to_string(sb.data()) ==
                    std::string{v.data(), v.size()});
                v.push_back(n+1);
            }
        }
        {
            std::vector<char> v;
            for(char n = 0; n < 20; ++n)
            {
                error_code ec;
                socket_type sock(ios_);
                sock.connect(ep, ec);
                if(! expect(! ec, ec.message()))
                    break;
                stream<socket_type&> ws(sock);
                ws.handshake("localhost", "/", ec);
                if(! expect(! ec, ec.message()))
                    break;
                ws.async_write(boost::asio::buffer(v), do_yield[ec]);
                if(! expect(! ec, ec.message()))
                    break;
                opcode op;
                streambuf sb;
                ws.async_read(op, sb, do_yield[ec]);
                if(! expect(! ec, ec.message()))
                    break;
                expect(to_string(sb.data()) ==
                    std::string{v.data(), v.size()});
                v.push_back(n+1);
            }
        }
    }

    void run() override
    {
        testSpecialMembers();

        testOptions();

        yield_to(std::bind(&stream_test::testAccept,
            this, std::placeholders::_1));

        auto const any = endpoint_type{
            address_type::from_string("127.0.0.1"), 0};
        {
            sync_echo_peer server(true, any);

            yield_to(std::bind(&stream_test::testHandshake,
                this, server.local_endpoint(),
                    std::placeholders::_1));

            yield_to(std::bind(&stream_test::testErrorHandling,
                this, server.local_endpoint(),
                    std::placeholders::_1));

            yield_to(std::bind(&stream_test::testMask,
                this, server.local_endpoint(),
                    std::placeholders::_1));
        }
        {
            async_echo_peer server(true, any, 1);

            yield_to(std::bind(&stream_test::testHandshake,
                this, server.local_endpoint(),
                    std::placeholders::_1));

            yield_to(std::bind(&stream_test::testErrorHandling,
                this, server.local_endpoint(),
                    std::placeholders::_1));

            yield_to(std::bind(&stream_test::testMask,
                this, server.local_endpoint(),
                    std::placeholders::_1));
        }

        pass();
    }
};

BEAST_DEFINE_TESTSUITE(stream,websocket,beast);

} // websocket
} // beast
