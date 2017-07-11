//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/websocket/stream.hpp>

#include "websocket_async_echo_server.hpp"
#include "websocket_sync_echo_server.hpp"

#include <beast/core/ostream.hpp>
#include <beast/core/multi_buffer.hpp>
#include <beast/test/fail_stream.hpp>
#include <beast/test/string_istream.hpp>
#include <beast/test/string_iostream.hpp>
#include <beast/test/string_ostream.hpp>
#include <beast/test/yield_to.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/optional.hpp>
#include <mutex>
#include <condition_variable>

namespace beast {
namespace websocket {

class stream_test
    : public beast::unit_test::suite
    , public test::enable_yield_to
{
public:
    using self = stream_test;
    using endpoint_type = boost::asio::ip::tcp::endpoint;
    using address_type = boost::asio::ip::address;
    using socket_type = boost::asio::ip::tcp::socket;

    template<class ConstBufferSequence>
    static
    std::string
    to_string(ConstBufferSequence const& bs)
    {
        return boost::lexical_cast<
            std::string>(buffers(bs));
    }

    struct con
    {
        stream<socket_type> ws;

        con(endpoint_type const& ep, boost::asio::io_service& ios)
            : ws(ios)
        {
            ws.next_layer().connect(ep);
            ws.handshake("localhost", "/");
        }
    };

    template<std::size_t N>
    class cbuf_helper
    {
        std::array<std::uint8_t, N> v_;
        boost::asio::const_buffer cb_;

    public:
        using value_type = decltype(cb_);
        using const_iterator = value_type const*;

        template<class... Vn>
        explicit
        cbuf_helper(Vn... vn)
            : v_({{ static_cast<std::uint8_t>(vn)... }})
            , cb_(v_.data(), v_.size())
        {
        }

        const_iterator
        begin() const
        {
            return &cb_;
        }

        const_iterator
        end() const
        {
            return begin()+1;
        }
    };

    template<class... Vn>
    cbuf_helper<sizeof...(Vn)>
    cbuf(Vn... vn)
    {
        return cbuf_helper<sizeof...(Vn)>(vn...);
    }

    template<std::size_t N>
    static
    boost::asio::const_buffers_1
    sbuf(const char (&s)[N])
    {
        return boost::asio::const_buffers_1(&s[0], N-1);
    }

    template<class Pred>
    static
    bool
    run_until(boost::asio::io_service& ios,
        std::size_t limit, Pred&& pred)
    {
        for(std::size_t i = 0; i < limit; ++i)
        {
            if(pred())
                return true;
            ios.run_one();
        }
        return false;
    }

    struct SyncClient
    {
        template<class NextLayer>
        void
        accept(stream<NextLayer>& ws) const
        {
            ws.accept();
        }

        template<class NextLayer, class Buffers>
        typename std::enable_if<
            ! http::detail::is_header<Buffers>::value>::type
        accept(stream<NextLayer>& ws,
            Buffers const& buffers) const
        {
            ws.accept(buffers);
        }

        template<class NextLayer, class Fields>
        void
        accept(stream<NextLayer>& ws,
            http::header<true, Fields> const& req) const
        {
            ws.accept(req);
        }

        template<class NextLayer,
            class Fields, class Buffers>
        void
        accept(stream<NextLayer>& ws,
            http::header<true, Fields> const& req,
                Buffers const& buffers) const
        {
            ws.accept(req, buffers);
        }

        template<class NextLayer, class Decorator>
        void
        accept_ex(stream<NextLayer>& ws,
            Decorator const& d) const
        {
            ws.accept_ex(d);
        }

        template<class NextLayer,
            class Buffers, class Decorator>
        typename std::enable_if<
            ! http::detail::is_header<Buffers>::value>::type
        accept_ex(stream<NextLayer>& ws,
            Buffers const& buffers,
                Decorator const& d) const
        {
            ws.accept_ex(buffers, d);
        }

        template<class NextLayer,
            class Fields, class Decorator>
        void
        accept_ex(stream<NextLayer>& ws,
            http::header<true, Fields> const& req,
                Decorator const& d) const
        {
            ws.accept_ex(req, d);
        }

        template<class NextLayer,
            class Fields, class Buffers,
                class Decorator>
        void
        accept_ex(stream<NextLayer>& ws,
            http::header<true, Fields> const& req,
                Buffers const& buffers,
                    Decorator const& d) const
        {
            ws.accept_ex(req, buffers, d);
        }

        template<class NextLayer>
        void
        handshake(stream<NextLayer>& ws,
            string_view uri,
                string_view path) const
        {
            ws.handshake(uri, path);
        }

        template<class NextLayer>
        void
        handshake(stream<NextLayer>& ws,
            response_type& res,
                string_view uri,
                    string_view path) const
        {
            ws.handshake(res, uri, path);
        }

        template<class NextLayer, class Decorator>
        void
        handshake_ex(stream<NextLayer>& ws,
            string_view uri,
                string_view path,
                    Decorator const& d) const
        {
            ws.handshake_ex(uri, path, d);
        }

        template<class NextLayer, class Decorator>
        void
        handshake_ex(stream<NextLayer>& ws,
            response_type& res,
                string_view uri,
                    string_view path,
                        Decorator const& d) const
        {
            ws.handshake_ex(res, uri, path, d);
        }

        template<class NextLayer>
        void
        ping(stream<NextLayer>& ws,
            ping_data const& payload) const
        {
            ws.ping(payload);
        }

        template<class NextLayer>
        void
        pong(stream<NextLayer>& ws,
            ping_data const& payload) const
        {
            ws.pong(payload);
        }

        template<class NextLayer>
        void
        close(stream<NextLayer>& ws,
            close_reason const& cr) const
        {
            ws.close(cr);
        }

        template<
            class NextLayer, class DynamicBuffer>
        void
        read(stream<NextLayer>& ws,
            DynamicBuffer& buffer) const
        {
            ws.read(buffer);
        }

        template<
            class NextLayer, class ConstBufferSequence>
        void
        write(stream<NextLayer>& ws,
            ConstBufferSequence const& buffers) const
        {
            ws.write(buffers);
        }

        template<
            class NextLayer, class ConstBufferSequence>
        void
        write_frame(stream<NextLayer>& ws, bool fin,
            ConstBufferSequence const& buffers) const
        {
            ws.write_frame(fin, buffers);
        }

        template<
            class NextLayer, class ConstBufferSequence>
        void
        write_raw(stream<NextLayer>& ws,
            ConstBufferSequence const& buffers) const
        {
            boost::asio::write(
                ws.next_layer(), buffers);
        }
    };

    class AsyncClient
    {
        yield_context& yield_;

    public:
        explicit
        AsyncClient(yield_context& yield)
            : yield_(yield)
        {
        }

        template<class NextLayer>
        void
        accept(stream<NextLayer>& ws) const
        {
            error_code ec;
            ws.async_accept(yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer, class Buffers>
        typename std::enable_if<
            ! http::detail::is_header<Buffers>::value>::type
        accept(stream<NextLayer>& ws,
            Buffers const& buffers) const
        {
            error_code ec;
            ws.async_accept(buffers, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer, class Fields>
        void
        accept(stream<NextLayer>& ws,
            http::header<true, Fields> const& req) const
        {
            error_code ec;
            ws.async_accept(req, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer,
            class Fields, class Buffers>
        void
        accept(stream<NextLayer>& ws,
            http::header<true, Fields> const& req,
                Buffers const& buffers) const
        {
            error_code ec;
            ws.async_accept(req, buffers, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer,
            class Decorator>
        void
        accept_ex(stream<NextLayer>& ws,
            Decorator const& d) const
        {
            error_code ec;
            ws.async_accept_ex(d, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer,
            class Buffers, class Decorator>
        typename std::enable_if<
            ! http::detail::is_header<Buffers>::value>::type
        accept_ex(stream<NextLayer>& ws,
            Buffers const& buffers,
                Decorator const& d) const
        {
            error_code ec;
            ws.async_accept_ex(buffers, d, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer,
            class Fields, class Decorator>
        void
        accept_ex(stream<NextLayer>& ws,
            http::header<true, Fields> const& req,
                Decorator const& d) const
        {
            error_code ec;
            ws.async_accept_ex(req, d, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer, class Fields,
            class Buffers, class Decorator>
        void
        accept_ex(stream<NextLayer>& ws,
            http::header<true, Fields> const& req,
                Buffers const& buffers,
                    Decorator const& d) const
        {
            error_code ec;
            ws.async_accept_ex(
                req, buffers, d, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer>
        void
        handshake(stream<NextLayer>& ws,
            string_view uri,
                string_view path) const
        {
            error_code ec;
            ws.async_handshake(
                uri, path, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer>
        void
        handshake(stream<NextLayer>& ws,
            response_type& res,
                string_view uri,
                    string_view path) const
        {
            error_code ec;
            ws.async_handshake(
                res, uri, path, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer, class Decorator>
        void
        handshake_ex(stream<NextLayer>& ws,
            string_view uri,
                string_view path,
                    Decorator const &d) const
        {
            error_code ec;
            ws.async_handshake_ex(
                uri, path, d, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer, class Decorator>
        void
        handshake_ex(stream<NextLayer>& ws,
            response_type& res,
                string_view uri,
                    string_view path,
                        Decorator const &d) const
        {
            error_code ec;
            ws.async_handshake_ex(
                res, uri, path, d, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer>
        void
        ping(stream<NextLayer>& ws,
            ping_data const& payload) const
        {
            error_code ec;
            ws.async_ping(payload, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer>
        void
        pong(stream<NextLayer>& ws,
            ping_data const& payload) const
        {
            error_code ec;
            ws.async_pong(payload, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<class NextLayer>
        void
        close(stream<NextLayer>& ws,
            close_reason const& cr) const
        {
            error_code ec;
            ws.async_close(cr, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<
            class NextLayer, class DynamicBuffer>
        void
        read(stream<NextLayer>& ws,
            DynamicBuffer& buffer) const
        {
            error_code ec;
            ws.async_read(buffer, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<
            class NextLayer, class ConstBufferSequence>
        void
        write(stream<NextLayer>& ws,
            ConstBufferSequence const& buffers) const
        {
            error_code ec;
            ws.async_write(buffers, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<
            class NextLayer, class ConstBufferSequence>
        void
        write_frame(stream<NextLayer>& ws, bool fin,
            ConstBufferSequence const& buffers) const
        {
            error_code ec;
            ws.async_write_frame(fin, buffers, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }

        template<
            class NextLayer, class ConstBufferSequence>
        void
        write_raw(stream<NextLayer>& ws,
            ConstBufferSequence const& buffers) const
        {
            error_code ec;
            boost::asio::async_write(
                ws.next_layer(), buffers, yield_[ec]);
            if(ec)
                throw system_error{ec};
        }
    };

    void
    testOptions()
    {
        stream<socket_type> ws(ios_);
        ws.auto_fragment(true);
        ws.write_buffer_size(2048);
        ws.binary(false);
        ws.read_buffer_size(8192);
        ws.read_message_max(1 * 1024 * 1024);
        try
        {
            ws.write_buffer_size(7);
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }
    }

    //--------------------------------------------------------------------------

    class res_decorator
    {
        bool& b_;

    public:
        res_decorator(res_decorator const&) = default;

        explicit
        res_decorator(bool& b)
            : b_(b)
        {
        }

        void
        operator()(response_type&) const
        {
            b_ = true;
        }
    };

    template<class Client>
    void
    testAccept(Client const& c)
    {
        static std::size_t constexpr limit = 200;
        std::size_t n;
        for(n = 0; n < limit; ++n)
        {
            test::fail_counter fc{n};
            try
            {
                // request in stream
                {
                    stream<test::fail_stream<
                        test::string_iostream>> ws{fc, ios_,
                        "GET / HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: upgrade\r\n"
                        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                        "Sec-WebSocket-Version: 13\r\n"
                        "\r\n"
                        , 20};
                    c.accept(ws);
                    // VFALCO validate contents of ws.next_layer().str?
                }
                {
                    stream<test::fail_stream<
                        test::string_iostream>> ws{fc, ios_,
                        "GET / HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: upgrade\r\n"
                        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                        "Sec-WebSocket-Version: 13\r\n"
                        "\r\n"
                        , 20};
                    bool called = false;
                    c.accept_ex(ws, res_decorator{called});
                    BEAST_EXPECT(called);
                }
                // request in buffers
                {
                    stream<test::fail_stream<
                        test::string_ostream>> ws{fc, ios_};
                    c.accept(ws, sbuf(
                        "GET / HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: upgrade\r\n"
                        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                        "Sec-WebSocket-Version: 13\r\n"
                        "\r\n"
                    ));
                }
                {
                    stream<test::fail_stream<
                        test::string_ostream>> ws{fc, ios_};
                    bool called = false;
                    c.accept_ex(ws, sbuf(
                        "GET / HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: upgrade\r\n"
                        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                        "Sec-WebSocket-Version: 13\r\n"
                        "\r\n"),
                        res_decorator{called});
                    BEAST_EXPECT(called);
                }
                // request in buffers and stream
                {
                    stream<test::fail_stream<
                        test::string_iostream>> ws{fc, ios_,
                        "Connection: upgrade\r\n"
                        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                        "Sec-WebSocket-Version: 13\r\n"
                        "\r\n"
                        , 16};
                    c.accept(ws, sbuf(
                        "GET / HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Upgrade: websocket\r\n"
                    ));
                }
                {
                    stream<test::fail_stream<
                        test::string_iostream>> ws{fc, ios_,
                        "Connection: upgrade\r\n"
                        "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                        "Sec-WebSocket-Version: 13\r\n"
                        "\r\n"
                        , 16};
                    bool called = false;
                    c.accept_ex(ws, sbuf(
                        "GET / HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Upgrade: websocket\r\n"),
                        res_decorator{called});
                    BEAST_EXPECT(called);
                }
                // request in message
                {
                    request_type req;
                    req.method(http::verb::get);
                    req.target("/");
                    req.version = 11;
                    req.insert("Host", "localhost");
                    req.insert("Upgrade", "websocket");
                    req.insert("Connection", "upgrade");
                    req.insert("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
                    req.insert("Sec-WebSocket-Version", "13");
                    stream<test::fail_stream<
                        test::string_ostream>> ws{fc, ios_};
                    c.accept(ws, req);
                }
                {
                    request_type req;
                    req.method(http::verb::get);
                    req.target("/");
                    req.version = 11;
                    req.insert("Host", "localhost");
                    req.insert("Upgrade", "websocket");
                    req.insert("Connection", "upgrade");
                    req.insert("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
                    req.insert("Sec-WebSocket-Version", "13");
                    stream<test::fail_stream<
                        test::string_ostream>> ws{fc, ios_};
                    bool called = false;
                    c.accept_ex(ws, req,
                        res_decorator{called});
                    BEAST_EXPECT(called);
                }
                // request in message, close frame in buffers
                {
                    request_type req;
                    req.method(http::verb::get);
                    req.target("/");
                    req.version = 11;
                    req.insert("Host", "localhost");
                    req.insert("Upgrade", "websocket");
                    req.insert("Connection", "upgrade");
                    req.insert("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
                    req.insert("Sec-WebSocket-Version", "13");
                    stream<test::fail_stream<
                        test::string_ostream>> ws{fc, ios_};
                    c.accept(ws, req,
                        cbuf(0x88, 0x82, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x17));
                    try
                    {
                        multi_buffer b;
                        c.read(ws, b);
                        fail("success", __FILE__, __LINE__);
                    }
                    catch(system_error const& e)
                    {
                        if(e.code() != websocket::error::closed)
                            throw;
                    }
                }
                {
                    request_type req;
                    req.method(http::verb::get);
                    req.target("/");
                    req.version = 11;
                    req.insert("Host", "localhost");
                    req.insert("Upgrade", "websocket");
                    req.insert("Connection", "upgrade");
                    req.insert("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
                    req.insert("Sec-WebSocket-Version", "13");
                    stream<test::fail_stream<
                        test::string_ostream>> ws{fc, ios_};
                    bool called = false;
                    c.accept_ex(ws, req,
                        cbuf(0x88, 0x82, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x17),
                        res_decorator{called});
                    BEAST_EXPECT(called);
                    try
                    {
                        multi_buffer b;
                        c.read(ws, b);
                        fail("success", __FILE__, __LINE__);
                    }
                    catch(system_error const& e)
                    {
                        if(e.code() != websocket::error::closed)
                            throw;
                    }
                }
                // request in message, close frame in stream
                {
                    request_type req;
                    req.method(http::verb::get);
                    req.target("/");
                    req.version = 11;
                    req.insert("Host", "localhost");
                    req.insert("Upgrade", "websocket");
                    req.insert("Connection", "upgrade");
                    req.insert("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
                    req.insert("Sec-WebSocket-Version", "13");
                    stream<test::fail_stream<
                        test::string_iostream>> ws{fc, ios_,
                        "\x88\x82\xff\xff\xff\xff\xfc\x17"};
                    c.accept(ws, req);
                    try
                    {
                        multi_buffer b;
                        c.read(ws, b);
                        fail("success", __FILE__, __LINE__);
                    }
                    catch(system_error const& e)
                    {
                        if(e.code() != websocket::error::closed)
                            throw;
                    }
                }
                // request in message, close frame in stream and buffers
                {
                    request_type req;
                    req.method(http::verb::get);
                    req.target("/");
                    req.version = 11;
                    req.insert("Host", "localhost");
                    req.insert("Upgrade", "websocket");
                    req.insert("Connection", "upgrade");
                    req.insert("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
                    req.insert("Sec-WebSocket-Version", "13");
                    stream<test::fail_stream<
                        test::string_iostream>> ws{fc, ios_,
                        "xff\xff\xfc\x17"};
                    c.accept(ws, req,
                        cbuf(0x88, 0x82, 0xff, 0xff));
                    try
                    {
                        multi_buffer b;
                        c.read(ws, b);
                        fail("success", __FILE__, __LINE__);
                    }
                    catch(system_error const& e)
                    {
                        if(e.code() != websocket::error::closed)
                            throw;
                    }
                }
                // failed handshake (missing Sec-WebSocket-Key)
                {
                    stream<test::fail_stream<
                        test::string_iostream>> ws{fc, ios_,
                        "GET / HTTP/1.1\r\n"
                        "Host: localhost\r\n"
                        "Upgrade: websocket\r\n"
                        "Connection: upgrade\r\n"
                        "Sec-WebSocket-Version: 13\r\n"
                        "\r\n"
                        , 20};
                    try
                    {
                        c.accept(ws);
                        fail("success", __FILE__, __LINE__);
                    }
                    catch(system_error const& e)
                    {
                        if( e.code() !=
                                websocket::error::handshake_failed &&
                            e.code() !=
                                boost::asio::error::eof)
                            throw;
                    }
                }
            }
            catch(system_error const&)
            {
                continue;
            }
            break;
        }
        BEAST_EXPECT(n < limit);
    }

    void
    testAccept()
    {
        testAccept(SyncClient{});
        yield_to(
            [&](yield_context yield)
            {
                testAccept(AsyncClient{yield});
            });
    }

    //--------------------------------------------------------------------------

    class req_decorator
    {
        bool& b_;

    public:
        req_decorator(req_decorator const&) = default;

        explicit
        req_decorator(bool& b)
            : b_(b)
        {
        }

        void
        operator()(request_type&) const
        {
            b_ = true;
        }
    };

    template<class Client>
    void
    testHandshake(endpoint_type const& ep, Client const& c)
    {
        static std::size_t constexpr limit = 200;
        std::size_t n;
        for(n = 199; n < limit; ++n)
        {
            test::fail_counter fc{n};
            try
            {
                // handshake
                {
                    stream<test::fail_stream<
                        boost::asio::ip::tcp::socket>> ws{fc, ios_};
                    ws.next_layer().next_layer().connect(ep);
                    c.handshake(ws, "localhost", "/");
                }
                // handshake, response
                {
                    stream<test::fail_stream<
                        boost::asio::ip::tcp::socket>> ws{fc, ios_};
                    ws.next_layer().next_layer().connect(ep);
                    response_type res;
                    c.handshake(ws, res, "localhost", "/");
                    // VFALCO validate res?
                }
                // handshake_ex
                {
                    stream<test::fail_stream<
                        boost::asio::ip::tcp::socket>> ws{fc, ios_};
                    ws.next_layer().next_layer().connect(ep);
                    bool called = false;
                    c.handshake_ex(ws, "localhost", "/",
                        req_decorator{called});
                    BEAST_EXPECT(called);
                }
                // handshake_ex, response
                {
                    stream<test::fail_stream<
                        boost::asio::ip::tcp::socket>> ws{fc, ios_};
                    ws.next_layer().next_layer().connect(ep);
                    bool called = false;
                    response_type res;
                    c.handshake_ex(ws, res, "localhost", "/",
                        req_decorator{called});
                    // VFALCO validate res?
                    BEAST_EXPECT(called);
                }
            }
            catch(system_error const&)
            {
                continue;
            }
            break;
        }
        BEAST_EXPECT(n < limit);
    }

    void
    testHandshake()
    {
        error_code ec = test::error::fail_error;
        ::websocket::async_echo_server server{nullptr, 1};
        auto const any = endpoint_type{
            address_type::from_string("127.0.0.1"), 0};
        server.open(any, ec);
        BEAST_EXPECTS(! ec, ec.message());
        auto const ep = server.local_endpoint();
        testHandshake(ep, SyncClient{});
        yield_to(
            [&](yield_context yield)
            {
                testHandshake(ep, AsyncClient{yield});
            });
    }

    //--------------------------------------------------------------------------

    void testBadHandshakes()
    {
        auto const check =
            [&](error_code const& ev, std::string const& s)
            {
                for(std::size_t i = 0; i < s.size(); ++i)
                {
                    stream<test::string_istream> ws(ios_,
                        s.substr(i, s.size() - i));
                    try
                    {
                        ws.accept(boost::asio::buffer(
                            s.substr(0, i), i));
                        BEAST_EXPECTS(! ev, ev.message());
                    }
                    catch(system_error const& se)
                    {
                        BEAST_EXPECTS(se.code() == ev, se.what());
                    }
                }
            };
        // wrong version
        check(http::error::end_of_stream,
            "GET / HTTP/1.0\r\n"
            "Host: localhost:80\r\n"
            "Upgrade: WebSocket\r\n"
            "Connection: keep-alive,upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        );
        // wrong method
        check(error::handshake_failed,
            "POST / HTTP/1.1\r\n"
            "Host: localhost:80\r\n"
            "Upgrade: WebSocket\r\n"
            "Connection: keep-alive,upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        );
        // missing Host
        check(error::handshake_failed,
            "GET / HTTP/1.1\r\n"
            "Upgrade: WebSocket\r\n"
            "Connection: keep-alive,upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        );
        // missing Sec-WebSocket-Key
        check(error::handshake_failed,
            "GET / HTTP/1.1\r\n"
            "Host: localhost:80\r\n"
            "Upgrade: WebSocket\r\n"
            "Connection: keep-alive,upgrade\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        );
        // missing Sec-WebSocket-Version
        check(error::handshake_failed,
            "GET / HTTP/1.1\r\n"
            "Host: localhost:80\r\n"
            "Upgrade: WebSocket\r\n"
            "Connection: keep-alive,upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "\r\n"
        );
        // wrong Sec-WebSocket-Version
        check(error::handshake_failed,
            "GET / HTTP/1.1\r\n"
            "Host: localhost:80\r\n"
            "Upgrade: WebSocket\r\n"
            "Connection: keep-alive,upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 1\r\n"
            "\r\n"
        );
        // missing upgrade token
        check(error::handshake_failed,
            "GET / HTTP/1.1\r\n"
            "Host: localhost:80\r\n"
            "Upgrade: HTTP/2\r\n"
            "Connection: upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        );
        // missing connection token
        check(error::handshake_failed,
            "GET / HTTP/1.1\r\n"
            "Host: localhost:80\r\n"
            "Upgrade: WebSocket\r\n"
            "Connection: keep-alive\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        );
        // valid request
        check({},
            "GET / HTTP/1.1\r\n"
            "Host: localhost:80\r\n"
            "Upgrade: WebSocket\r\n"
            "Connection: upgrade\r\n"
            "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        );
    }

    void testBadResponses()
    {
        auto const check =
            [&](std::string const& s)
            {
                stream<test::string_istream> ws(ios_, s);
                try
                {
                    ws.handshake("localhost:80", "/");
                    fail();
                }
                catch(system_error const& se)
                {
                    BEAST_EXPECT(se.code() == error::handshake_failed);
                }
            };
        // wrong HTTP version
        check(
            "HTTP/1.0 101 Switching Protocols\r\n"
            "Server: beast\r\n"
            "Upgrade: WebSocket\r\n"
            "Connection: upgrade\r\n"
            "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        );
        // wrong status
        check(
            "HTTP/1.1 200 OK\r\n"
            "Server: beast\r\n"
            "Upgrade: WebSocket\r\n"
            "Connection: upgrade\r\n"
            "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        );
        // missing upgrade token
        check(
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Server: beast\r\n"
            "Upgrade: HTTP/2\r\n"
            "Connection: upgrade\r\n"
            "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        );
        // missing connection token
        check(
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Server: beast\r\n"
            "Upgrade: WebSocket\r\n"
            "Connection: keep-alive\r\n"
            "Sec-WebSocket-Accept: s3pPLMBiTxaQ9kYGzzhZRbK+xOo=\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        );
        // missing accept key
        check(
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Server: beast\r\n"
            "Upgrade: WebSocket\r\n"
            "Connection: upgrade\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        );
        // wrong accept key
        check(
            "HTTP/1.1 101 Switching Protocols\r\n"
            "Server: beast\r\n"
            "Upgrade: WebSocket\r\n"
            "Connection: upgrade\r\n"
            "Sec-WebSocket-Accept: *\r\n"
            "Sec-WebSocket-Version: 13\r\n"
            "\r\n"
        );
    }

    void
    testMask(endpoint_type const& ep,
        yield_context do_yield)
    {
        {
            std::vector<char> v;
            for(char n = 0; n < 20; ++n)
            {
                error_code ec = test::error::fail_error;
                socket_type sock(ios_);
                sock.connect(ep, ec);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    break;
                stream<socket_type&> ws(sock);
                ws.handshake("localhost", "/", ec);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    break;
                ws.write(boost::asio::buffer(v), ec);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    break;
                multi_buffer db;
                ws.read(db, ec);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    break;
                BEAST_EXPECT(to_string(db.data()) ==
                    std::string(v.data(), v.size()));
                v.push_back(n+1);
            }
        }
        {
            std::vector<char> v;
            for(char n = 0; n < 20; ++n)
            {
                error_code ec = test::error::fail_error;
                socket_type sock(ios_);
                sock.connect(ep, ec);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    break;
                stream<socket_type&> ws(sock);
                ws.handshake("localhost", "/", ec);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    break;
                ws.async_write(boost::asio::buffer(v), do_yield[ec]);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    break;
                multi_buffer db;
                ws.async_read(db, do_yield[ec]);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    break;
                BEAST_EXPECT(to_string(db.data()) ==
                    std::string(v.data(), v.size()));
                v.push_back(n+1);
            }
        }
    }

    void testClose(endpoint_type const& ep, yield_context)
    {
        {
            // payload length 1
            con c(ep, ios_);
            boost::asio::write(c.ws.next_layer(),
                cbuf(0x88, 0x81, 0xff, 0xff, 0xff, 0xff, 0x00));
        }
        {
            // invalid close code 1005
            con c(ep, ios_);
            boost::asio::write(c.ws.next_layer(),
                cbuf(0x88, 0x82, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x12));
        }
        {
            // invalid utf8
            con c(ep, ios_);
            boost::asio::write(c.ws.next_layer(),
                cbuf(0x88, 0x86, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x15,
                    0x0f, 0xd7, 0x73, 0x43));
        }
        {
            // good utf8
            con c(ep, ios_);
            boost::asio::write(c.ws.next_layer(),
                cbuf(0x88, 0x86, 0xff, 0xff, 0xff, 0xff, 0xfc, 0x15,
                    'u', 't', 'f', '8'));
        }
    }

#if 0
    void testPausation1(endpoint_type const& ep)
    {
        boost::asio::io_service ios;
        stream<socket_type> ws(ios);
        ws.next_layer().connect(ep);
        ws.handshake("localhost", "/");

        // Make remote send a ping frame
        ws.binary(false);
        ws.write(buffer_cat(sbuf("PING"), sbuf("ping")));

        std::size_t count = 0;

        // Write a text message
        ++count;
        ws.async_write(sbuf("Hello"),
            [&](error_code ec)
            {
                --count;
            });

        // Read
        multi_buffer db;
        ++count;
        ws.async_read(db,
            [&](error_code ec)
            {
                --count;
            });
        // Run until the read_op writes a close frame.
        while(! ws.wr_block_)
            ios.run_one();
        // Write a text message, leaving
        // the write_op suspended as pausation.
        ws.async_write(sbuf("Hello"),
            [&](error_code ec)
            {
                ++count;
                // Send is canceled because close received.
                BEAST_EXPECT(ec == boost::asio::
                    error::operation_aborted,
                        ec.message());
                // Writes after close are aborted.
                ws.async_write(sbuf("World"),
                    [&](error_code ec)
                    {
                        ++count;
                        BEAST_EXPECT(ec == boost::asio::
                            error::operation_aborted,
                                ec.message());
                    });
            });
        // Run until all completions are delivered.
        static std::size_t constexpr limit = 100;
        std::size_t n;
        for(n = 0; n < limit; ++n)
        {
            if(count >= 4)
                break;
            ios.run_one();
        }
        BEAST_EXPECT(n < limit);
        ios.run();
    }
#endif

    void testPausation2(endpoint_type const& ep)
    {
        boost::asio::io_service ios;
        stream<socket_type> ws(ios);
        ws.next_layer().connect(ep);
        ws.handshake("localhost", "/");

        // Make remote send a text message with bad utf8.
        ws.binary(true);
        ws.write(buffer_cat(sbuf("TEXT"),
            cbuf(0x03, 0xea, 0xf0, 0x28, 0x8c, 0xbc)));
        multi_buffer db;
        std::size_t count = 0;
        // Read text message with bad utf8.
        // Causes a close to be sent, blocking writes.
        ws.async_read(db,
            [&](error_code ec)
            {
                // Read should fail with protocol error
                ++count;
                BEAST_EXPECTS(
                    ec == error::failed, ec.message());
                // Reads after failure are aborted
                ws.async_read(db,
                    [&](error_code ec)
                    {
                        ++count;
                        BEAST_EXPECTS(ec == boost::asio::
                            error::operation_aborted,
                                ec.message());
                    });
            });
        // Run until the read_op writes a close frame.
        while(! ws.wr_block_)
            ios.run_one();
        // Write a text message, leaving
        // the write_op suspended as a pausation.
        ws.async_write(sbuf("Hello"),
            [&](error_code ec)
            {
                ++count;
                // Send is canceled because close received.
                BEAST_EXPECTS(ec == boost::asio::
                    error::operation_aborted,
                        ec.message());
                // Writes after close are aborted.
                ws.async_write(sbuf("World"),
                    [&](error_code ec)
                    {
                        ++count;
                        BEAST_EXPECTS(ec == boost::asio::
                            error::operation_aborted,
                                ec.message());
                    });
            });
        // Run until all completions are delivered.
        static std::size_t constexpr limit = 100;
        std::size_t n;
        for(n = 0; n < limit; ++n)
        {
            if(count >= 4)
                break;
            ios.run_one();
        }
        BEAST_EXPECT(n < limit);
        ios.run();
    }

    void testPausation3(endpoint_type const& ep)
    {
        boost::asio::io_service ios;
        stream<socket_type> ws(ios);
        ws.next_layer().connect(ep);
        ws.handshake("localhost", "/");

        // Cause close to be received
        ws.binary(true);
        ws.write(sbuf("CLOSE"));
        multi_buffer db;
        std::size_t count = 0;
        // Read a close frame.
        // Sends a close frame, blocking writes.
        ws.async_read(db,
            [&](error_code ec)
            {
                // Read should complete with error::closed
                ++count;
                BEAST_EXPECTS(ec == error::closed,
                    ec.message());
                // Pings after a close are aborted
                ws.async_ping("",
                    [&](error_code ec)
                    {
                        ++count;
                        BEAST_EXPECTS(ec == boost::asio::
                            error::operation_aborted,
                                ec.message());
                    });
            });
        if(! BEAST_EXPECT(run_until(ios, 100,
                [&]{ return ws.wr_close_; })))
            return;
        // Try to ping
        ws.async_ping("payload",
            [&](error_code ec)
            {
                // Pings after a close are aborted
                ++count;
                BEAST_EXPECTS(ec == boost::asio::
                    error::operation_aborted,
                        ec.message());
                // Subsequent calls to close are aborted
                ws.async_close({},
                    [&](error_code ec)
                    {
                        ++count;
                        BEAST_EXPECTS(ec == boost::asio::
                            error::operation_aborted,
                                ec.message());
                    });
            });
        static std::size_t constexpr limit = 100;
        std::size_t n;
        for(n = 0; n < limit; ++n)
        {
            if(count >= 4)
                break;
            ios.run_one();
        }
        BEAST_EXPECT(n < limit);
        ios.run();
    }

    void testPausation4(endpoint_type const& ep)
    {
        boost::asio::io_service ios;
        stream<socket_type> ws(ios);
        ws.next_layer().connect(ep);
        ws.handshake("localhost", "/");

        // Cause close to be received
        ws.binary(true);
        ws.write(sbuf("CLOSE"));
        multi_buffer db;
        std::size_t count = 0;
        ws.async_read(db,
            [&](error_code ec)
            {
                ++count;
                BEAST_EXPECTS(ec == error::closed,
                    ec.message());
            });
        while(! ws.wr_block_)
            ios.run_one();
        // try to close
        ws.async_close("payload",
            [&](error_code ec)
            {
                ++count;
                BEAST_EXPECTS(ec == boost::asio::
                    error::operation_aborted,
                        ec.message());
            });
        static std::size_t constexpr limit = 100;
        std::size_t n;
        for(n = 0; n < limit; ++n)
        {
            if(count >= 2)
                break;
            ios.run_one();
        }
        BEAST_EXPECT(n < limit);
        ios.run();
    }

#if 0
    void testPausation5(endpoint_type const& ep)
    {
        boost::asio::io_service ios;
        stream<socket_type> ws(ios);
        ws.next_layer().connect(ep);
        ws.handshake("localhost", "/");

        ws.async_write(sbuf("CLOSE"),
            [&](error_code ec)
            {
                BEAST_EXPECT(! ec);
                ws.async_write(sbuf("PING"),
                    [&](error_code ec)
                    {
                        BEAST_EXPECT(! ec);
                    });
            });
        multi_buffer db;
        ws.async_read(db,
            [&](error_code ec)
            {
                BEAST_EXPECTS(ec == error::closed, ec.message());
            });
        if(! BEAST_EXPECT(run_until(ios, 100,
                [&]{ return ios.stopped(); })))
            return;
    }
#endif

    /*
        https://github.com/vinniefalco/Beast/issues/300

        Write a message as two individual frames
    */
    void
    testWriteFrames(endpoint_type const& ep)
    {
        error_code ec;
        socket_type sock{ios_};
        sock.connect(ep, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        stream<socket_type&> ws{sock};
        ws.handshake("localhost", "/", ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        ws.write_frame(false, sbuf("u"));
        ws.write_frame(true, sbuf("v"));
        multi_buffer b;
        ws.read(b, ec);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
    }

    void
    testAsyncWriteFrame(endpoint_type const& ep)
    {
        for(;;)
        {
            boost::asio::io_service ios;
            error_code ec;
            socket_type sock(ios);
            sock.connect(ep, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                break;
            stream<socket_type&> ws(sock);
            ws.handshake("localhost", "/", ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                break;
            ws.async_write_frame(false,
                boost::asio::null_buffers{},
                [&](error_code)
                {
                    fail();
                });
            ws.next_layer().cancel(ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                break;
            //
            // Destruction of the io_service will cause destruction
            // of the write_frame_op without invoking the final handler.
            //
            break;
        }
    }

    struct abort_test
    {
    };

    template<class Client>
    void
    testEndpoint(Client const& c,
        endpoint_type const& ep, permessage_deflate const& pmd)
    {
        using boost::asio::buffer;
        static std::size_t constexpr limit = 200;
        std::size_t n;
        for(n = 0; n <= limit; ++n)
        {
            stream<test::fail_stream<socket_type>> ws{n, ios_};
            ws.set_option(pmd);
            auto const restart =
                [&](error_code ev)
                {
                    try
                    {
                        multi_buffer db;
                        c.read(ws, db);
                        fail();
                        throw abort_test{};
                    }
                    catch(system_error const& se)
                    {
                        if(se.code() != ev)
                            throw;
                    }
                    error_code ec;
                    ws.lowest_layer().connect(ep, ec);
                    if(! BEAST_EXPECTS(! ec, ec.message()))
                        throw abort_test{};
                    c.handshake(ws, "localhost", "/");
                };
            try
            {
                {
                    // connect
                    error_code ec;
                    ws.lowest_layer().connect(ep, ec);
                    if(! BEAST_EXPECTS(! ec, ec.message()))
                        return;
                }
                c.handshake(ws, "localhost", "/");

                // send message
                ws.auto_fragment(false);
                ws.binary(false);
                c.write(ws, sbuf("Hello"));
                {
                    // receive echoed message
                    multi_buffer db;
                    c.read(ws, db);
                    BEAST_EXPECT(ws.got_text());
                    BEAST_EXPECT(to_string(db.data()) == "Hello");
                }

                // close, no payload
                c.close(ws, {});
                restart(error::closed);

                // close with code
                c.close(ws, close_code::going_away);
                restart(error::closed);

                // close with code and reason string
                c.close(ws, {close_code::going_away, "Going away"});
                restart(error::closed);

                bool once;

                // send ping and message
                once = false;
                ws.control_callback(
                    [&](frame_type kind, string_view s)
                    {
                        BEAST_EXPECT(kind == frame_type::pong);
                        BEAST_EXPECT(! once);
                        once = true;
                        BEAST_EXPECT(s == "");
                    });
                c.ping(ws, "");
                ws.binary(true);
                c.write(ws, sbuf("Hello"));
                {
                    // receive echoed message
                    multi_buffer db;
                    c.read(ws, db);
                    BEAST_EXPECT(once);
                    BEAST_EXPECT(ws.got_binary());
                    BEAST_EXPECT(to_string(db.data()) == "Hello");
                }
                ws.control_callback({});

                // send ping and fragmented message
                once = false;
                ws.control_callback(
                    [&](frame_type kind, string_view s)
                    {
                        BEAST_EXPECT(kind == frame_type::pong);
                        BEAST_EXPECT(! once);
                        once = true;
                        BEAST_EXPECT(s == "payload");
                    });
                ws.ping("payload");
                c.write_frame(ws, false, sbuf("Hello, "));
                c.write_frame(ws, false, sbuf(""));
                c.write_frame(ws, true, sbuf("World!"));
                {
                    // receive echoed message
                    multi_buffer db;
                    c.read(ws, db);
                    BEAST_EXPECT(once);
                    BEAST_EXPECT(to_string(db.data()) == "Hello, World!");
                }
                ws.control_callback({});

                // send pong
                c.pong(ws, "");

                // send auto fragmented message
                ws.auto_fragment(true);
                ws.write_buffer_size(8);
                c.write(ws, sbuf("Now is the time for all good men"));
                {
                    // receive echoed message
                    multi_buffer b;
                    c.read(ws, b);
                    BEAST_EXPECT(to_string(b.data()) == "Now is the time for all good men");
                }
                ws.auto_fragment(false);
                ws.write_buffer_size(4096);

                // send message with write buffer limit
                {
                    std::string s(2000, '*');
                    ws.write_buffer_size(1200);
                    c.write(ws, buffer(s.data(), s.size()));
                    {
                        // receive echoed message
                        multi_buffer db;
                        c.read(ws, db);
                        BEAST_EXPECT(to_string(db.data()) == s);
                    }
                }

                // cause ping
                ws.binary(true);
                c.write(ws, sbuf("PING"));
                ws.binary(false);
                c.write(ws, sbuf("Hello"));
                {
                    // receive echoed message
                    multi_buffer db;
                    c.read(ws, db);
                    BEAST_EXPECT(ws.got_text());
                    BEAST_EXPECT(to_string(db.data()) == "Hello");
                }

                // cause close
                ws.binary(true);
                c.write(ws, sbuf("CLOSE"));
                restart(error::closed);

                // send bad utf8
                ws.binary(true);
                c.write(ws, buffer_cat(sbuf("TEXT"),
                    cbuf(0x03, 0xea, 0xf0, 0x28, 0x8c, 0xbc)));
                restart(error::failed);

                // cause bad utf8
                ws.binary(true);
                c.write(ws, buffer_cat(sbuf("TEXT"),
                    cbuf(0x03, 0xea, 0xf0, 0x28, 0x8c, 0xbc)));
                c.write(ws, sbuf("Hello"));
                restart(error::failed);

                // cause bad close
                ws.binary(true);
                c.write(ws, buffer_cat(sbuf("RAW"),
                    cbuf(0x88, 0x02, 0x03, 0xed)));
                restart(error::failed);

                // unexpected cont
                c.write_raw(ws,
                    cbuf(0x80, 0x80, 0xff, 0xff, 0xff, 0xff));
                restart(error::closed);

                // invalid fixed frame header
                c.write_raw(ws,
                    cbuf(0x8f, 0x80, 0xff, 0xff, 0xff, 0xff));
                restart(error::closed);

                // cause non-canonical extended size
                c.write(ws, buffer_cat(sbuf("RAW"),
                    cbuf(0x82, 0x7e, 0x00, 0x01, 0x00)));
                restart(error::failed);

                if(! pmd.client_enable)
                {
                    // expected cont
                    c.write_frame(ws, false, boost::asio::null_buffers{});
                    c.write_raw(ws,
                        cbuf(0x81, 0x80, 0xff, 0xff, 0xff, 0xff));
                    restart(error::closed);

                    // message size above 2^64
                    c.write_frame(ws, false, cbuf(0x00));
                    c.write_raw(ws,
                        cbuf(0x80, 0xff, 0xff, 0xff, 0xff, 0xff,
                            0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff));
                    restart(error::closed);

                    // message size exceeds max
                    ws.read_message_max(1);
                    c.write(ws, cbuf(0x00, 0x00));
                    restart(error::failed);
                    ws.read_message_max(16*1024*1024);
                }
            }
            catch(system_error const&)
            {
                continue;
            }
            break;
        }
        BEAST_EXPECT(n < limit);
    }

    void
    run() override
    {
        BOOST_STATIC_ASSERT(std::is_constructible<
            stream<socket_type>, boost::asio::io_service&>::value);

        BOOST_STATIC_ASSERT(std::is_move_constructible<
            stream<socket_type>>::value);

        BOOST_STATIC_ASSERT(std::is_move_assignable<
            stream<socket_type>>::value);

        BOOST_STATIC_ASSERT(std::is_constructible<
            stream<socket_type&>, socket_type&>::value);

        BOOST_STATIC_ASSERT(std::is_move_constructible<
            stream<socket_type&>>::value);

        BOOST_STATIC_ASSERT(! std::is_move_assignable<
            stream<socket_type&>>::value);

        log << "sizeof(websocket::stream) == " <<
            sizeof(websocket::stream<boost::asio::ip::tcp::socket&>) << std::endl;

        auto const any = endpoint_type{
            address_type::from_string("127.0.0.1"), 0};

        testOptions();
        testAccept();
        testHandshake();
        testBadHandshakes();
        testBadResponses();

        permessage_deflate pmd;
        pmd.client_enable = false;
        pmd.server_enable = false;

        {
            error_code ec;
            ::websocket::sync_echo_server server{nullptr};
            server.set_option(pmd);
            server.open(any, ec);
            BEAST_EXPECTS(! ec, ec.message());
            auto const ep = server.local_endpoint();
            //testPausation1(ep);
            testPausation2(ep);
            testPausation3(ep);
            testPausation4(ep);
            //testPausation5(ep);
            testWriteFrames(ep);
            testAsyncWriteFrame(ep);
        }

        {
            error_code ec;
            ::websocket::async_echo_server server{nullptr, 4};
            server.open(any, ec);
            BEAST_EXPECTS(! ec, ec.message());
            auto const ep = server.local_endpoint();
            testAsyncWriteFrame(ep);
        }

        auto const doClientTests =
            [this, any](permessage_deflate const& pmd)
            {
                {
                    error_code ec;
                    ::websocket::sync_echo_server server{nullptr};
                    server.set_option(pmd);
                    server.open(any, ec);
                    BEAST_EXPECTS(! ec, ec.message());
                    auto const ep = server.local_endpoint();
                    testEndpoint(SyncClient{}, ep, pmd);
                    yield_to(
                        [&](yield_context yield)
                        {
                            testEndpoint(
                                AsyncClient{yield}, ep, pmd);
                        });
                }
                {
                    error_code ec;
                    ::websocket::async_echo_server server{nullptr, 4};
                    server.set_option(pmd);
                    server.open(any, ec);
                    BEAST_EXPECTS(! ec, ec.message());
                    auto const ep = server.local_endpoint();
                    testEndpoint(SyncClient{}, ep, pmd);
                    yield_to(
                        [&](yield_context yield)
                        {
                            testEndpoint(
                                AsyncClient{yield}, ep, pmd);
                        });
                }
            };

        pmd.client_enable = false;
        pmd.server_enable = false;
        doClientTests(pmd);

    #if ! BEAST_NO_SLOW_TESTS
        pmd.client_enable = true;
        pmd.server_enable = true;
        pmd.client_max_window_bits = 10;
        pmd.client_no_context_takeover = false;
        doClientTests(pmd);

        pmd.client_enable = true;
        pmd.server_enable = true;
        pmd.client_max_window_bits = 10;
        pmd.client_no_context_takeover = true;
        doClientTests(pmd);
    #endif
    }
};

BEAST_DEFINE_TESTSUITE(stream,websocket,beast);

} // websocket
} // beast
