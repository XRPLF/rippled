//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/websocket/stream.hpp>

#include "websocket_async_echo_server.hpp"
#include "websocket_sync_echo_server.hpp"

#include <beast/core/streambuf.hpp>
#include <beast/core/to_string.hpp>
#include <beast/test/fail_stream.hpp>
#include <beast/test/string_stream.hpp>
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

    template<class NextLayer, class DynamicBuffer>
    static
    void
    read(stream<NextLayer>& ws, opcode& op, DynamicBuffer& db)
    {
        frame_info fi;
        for(;;)
        {
            ws.read_frame(fi, db);
            op = fi.op;
            if(fi.fin)
                break;
        }
    }

    typedef void(self::*pmf_t)(endpoint_type const&, yield_context);

    void yield_to_mf(endpoint_type const& ep, pmf_t mf)
    {
        yield_to(std::bind(mf, this, ep, std::placeholders::_1));
    }

    struct identity
    {
        template<class Body, class Fields>
        void
        operator()(http::message<true, Body, Fields>&)
        {
        }

        template<class Body, class Fields>
        void
        operator()(http::message<false, Body, Fields>&)
        {
        }
    };

    void testOptions()
    {
        stream<socket_type> ws(ios_);
        ws.set_option(auto_fragment{true});
        ws.set_option(decorate(identity{}));
        ws.set_option(keep_alive{false});
        ws.set_option(write_buffer_size{2048});
        ws.set_option(message_type{opcode::text});
        ws.set_option(read_buffer_size{8192});
        ws.set_option(read_message_max{1 * 1024 * 1024});
        try
        {
            ws.set_option(write_buffer_size{7});
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }
        try
        {
            message_type{opcode::close};
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }
    }

    void testAccept()
    {
        {
            static std::size_t constexpr limit = 100;
            std::size_t n;
            for(n = 0; n < limit; ++n)
            {
                // valid
                http::request<http::empty_body> req;
                req.method = "GET";
                req.url = "/";
                req.version = 11;
                req.fields.insert("Host", "localhost");
                req.fields.insert("Upgrade", "websocket");
                req.fields.insert("Connection", "upgrade");
                req.fields.insert("Sec-WebSocket-Key", "dGhlIHNhbXBsZSBub25jZQ==");
                req.fields.insert("Sec-WebSocket-Version", "13");
                stream<test::fail_stream<
                    test::string_stream>> ws(n, ios_, "");
                try
                {
                    ws.accept(req);
                    break;
                }
                catch(system_error const&)
                {
                }
            }
            BEAST_EXPECT(n < limit);
        }
        {
            // valid
            stream<test::string_stream> ws(ios_,
                "GET / HTTP/1.1\r\n"
                "Host: localhost:80\r\n"
                "Upgrade: WebSocket\r\n"
                "Connection: upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "\r\n"
            );
            try
            {
                ws.accept();
                pass();
            }
            catch(system_error const&)
            {
                fail();
            }
        }
        {
            // invalid
            stream<test::string_stream> ws(ios_,
                "GET / HTTP/1.0\r\n"
                "\r\n"
            );
            try
            {
                ws.accept();
                fail();
            }
            catch(system_error const&)
            {
                pass();
            }
        }
    }

    void testBadHandshakes()
    {
        auto const check =
            [&](error_code const& ev, std::string const& s)
            {
                for(std::size_t i = 0; i < s.size(); ++i)
                {
                    stream<test::string_stream> ws(ios_,
                        s.substr(i, s.size() - i));
                    ws.set_option(keep_alive{true});
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
        check(error::handshake_failed,
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
                stream<test::string_stream> ws(ios_, s);
                try
                {
                    ws.handshake("localhost:80", "/");
                    fail();
                }
                catch(system_error const& se)
                {
                    BEAST_EXPECT(se.code() == error::response_failed);
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
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    break;
                stream<socket_type&> ws(sock);
                ws.handshake("localhost", "/", ec);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    break;
                ws.write(boost::asio::buffer(v), ec);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    break;
                opcode op;
                streambuf db;
                ws.read(op, db, ec);
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
                error_code ec;
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
                opcode op;
                streambuf db;
                ws.async_read(op, db, do_yield[ec]);
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
    void testInvokable1(endpoint_type const& ep)
    {
        boost::asio::io_service ios;
        stream<socket_type> ws(ios);
        ws.next_layer().connect(ep);
        ws.handshake("localhost", "/");

        // Make remote send a ping frame
        ws.set_option(message_type(opcode::text));
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
        opcode op;
        streambuf db;
        ++count;
        ws.async_read(op, db,
            [&](error_code ec)
            {
                --count;
            });
        // Run until the read_op writes a close frame.
        while(! ws.wr_block_)
            ios.run_one();
        // Write a text message, leaving
        // the write_op suspended as invokable.
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

    void testInvokable2(endpoint_type const& ep)
    {
        boost::asio::io_service ios;
        stream<socket_type> ws(ios);
        ws.next_layer().connect(ep);
        ws.handshake("localhost", "/");

        // Make remote send a text message with bad utf8.
        ws.set_option(message_type(opcode::binary));
        ws.write(buffer_cat(sbuf("TEXT"),
            cbuf(0x03, 0xea, 0xf0, 0x28, 0x8c, 0xbc)));
        opcode op;
        streambuf db;
        std::size_t count = 0;
        // Read text message with bad utf8.
        // Causes a close to be sent, blocking writes.
        ws.async_read(op, db,
            [&](error_code ec)
            {
                // Read should fail with protocol error
                ++count;
                BEAST_EXPECTS(
                    ec == error::failed, ec.message());
                // Reads after failure are aborted
                ws.async_read(op, db,
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
        // the write_op suspended as invokable.
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

    void testInvokable3(endpoint_type const& ep)
    {
        boost::asio::io_service ios;
        stream<socket_type> ws(ios);
        ws.next_layer().connect(ep);
        ws.handshake("localhost", "/");

        // Cause close to be received
        ws.set_option(message_type(opcode::binary));
        ws.write(sbuf("CLOSE"));
        opcode op;
        streambuf db;
        std::size_t count = 0;
        // Read a close frame.
        // Sends a close frame, blocking writes.
        ws.async_read(op, db,
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

    void testInvokable4(endpoint_type const& ep)
    {
        boost::asio::io_service ios;
        stream<socket_type> ws(ios);
        ws.next_layer().connect(ep);
        ws.handshake("localhost", "/");

        // Cause close to be received
        ws.set_option(message_type(opcode::binary));
        ws.write(sbuf("CLOSE"));
        opcode op;
        streambuf db;
        std::size_t count = 0;
        ws.async_read(op, db,
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
    void testInvokable5(endpoint_type const& ep)
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
        opcode op;
        streambuf db;
        ws.async_read(op, db,
            [&](error_code ec)
            {
                BEAST_EXPECTS(ec == error::closed, ec.message());
            });
        if(! BEAST_EXPECT(run_until(ios, 100,
                [&]{ return ios.stopped(); })))
            return;
    }
#endif

    void testSyncClient(endpoint_type const& ep)
    {
        using boost::asio::buffer;
        static std::size_t constexpr limit = 200;
        std::size_t n;
        for(n = 0; n < limit; ++n)
        {
            stream<test::fail_stream<socket_type>> ws(n, ios_);
            auto const restart =
                [&](error_code ev)
                {
                    try
                    {
                        opcode op;
                        streambuf db;
                        ws.read(op, db);
                        fail();
                        return false;
                    }
                    catch(system_error const& se)
                    {
                        if(se.code() != ev)
                            throw;
                    }
                    error_code ec;
                    ws.lowest_layer().connect(ep, ec);
                    if(! BEAST_EXPECTS(! ec, ec.message()))
                        return false;
                    ws.handshake("localhost", "/");
                    return true;
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
                ws.handshake("localhost", "/");

                // send message
                ws.set_option(auto_fragment{false});
                ws.set_option(message_type(opcode::text));
                ws.write(sbuf("Hello"));
                {
                    // receive echoed message
                    opcode op;
                    streambuf db;
                    read(ws, op, db);
                    BEAST_EXPECT(op == opcode::text);
                    BEAST_EXPECT(to_string(db.data()) == "Hello");
                }

                // close, no payload
                ws.close({});
                if(! restart(error::closed))
                    return;

                // close with code
                ws.close(close_code::going_away);
                if(! restart(error::closed))
                    return;

                // close with code and reason string
                ws.close({close_code::going_away, "Going away"});
                if(! restart(error::closed))
                    return;

                // send ping and message
                bool pong = false;
                ws.set_option(pong_callback{
                    [&](ping_data const& payload)
                    {
                        BEAST_EXPECT(! pong);
                        pong = true;
                        BEAST_EXPECT(payload == "");
                    }});
                ws.ping("");
                ws.set_option(message_type(opcode::binary));
                ws.write(sbuf("Hello"));
                {
                    // receive echoed message
                    opcode op;
                    streambuf db;
                    ws.read(op, db);
                    BEAST_EXPECT(pong == 1);
                    BEAST_EXPECT(op == opcode::binary);
                    BEAST_EXPECT(to_string(db.data()) == "Hello");
                }
                ws.set_option(pong_callback{});

                // send ping and fragmented message
                ws.set_option(pong_callback{
                    [&](ping_data const& payload)
                    {
                        BEAST_EXPECT(payload == "payload");
                    }});
                ws.ping("payload");
                ws.write_frame(false, sbuf("Hello, "));
                ws.write_frame(false, sbuf(""));
                ws.write_frame(true, sbuf("World!"));
                {
                    // receive echoed message
                    opcode op;
                    streambuf db;
                    ws.read(op, db);
                    BEAST_EXPECT(pong == 1);
                    BEAST_EXPECT(to_string(db.data()) == "Hello, World!");
                }
                ws.set_option(pong_callback{});

                // send pong
                ws.pong("");

                // send auto fragmented message
                ws.set_option(auto_fragment{true});
                ws.set_option(write_buffer_size{8});
                ws.write(sbuf("Now is the time for all good men"));
                {
                    // receive echoed message
                    opcode op;
                    streambuf sb;
                    ws.read(op, sb);
                    BEAST_EXPECT(to_string(sb.data()) == "Now is the time for all good men");
                }
                ws.set_option(auto_fragment{false});
                ws.set_option(write_buffer_size{4096});

                // send message with write buffer limit
                {
                    std::string s(2000, '*');
                    ws.set_option(write_buffer_size(1200));
                    ws.write(buffer(s.data(), s.size()));
                    {
                        // receive echoed message
                        opcode op;
                        streambuf db;
                        ws.read(op, db);
                        BEAST_EXPECT(to_string(db.data()) == s);
                    }
                }

                // cause ping
                ws.set_option(message_type(opcode::binary));
                ws.write(sbuf("PING"));
                ws.set_option(message_type(opcode::text));
                ws.write(sbuf("Hello"));
                {
                    // receive echoed message
                    opcode op;
                    streambuf db;
                    ws.read(op, db);
                    BEAST_EXPECT(op == opcode::text);
                    BEAST_EXPECT(to_string(db.data()) == "Hello");
                }

                // cause close
                ws.set_option(message_type(opcode::binary));
                ws.write(sbuf("CLOSE"));
                if(! restart(error::closed))
                    return;

                // send bad utf8
                ws.set_option(message_type(opcode::binary));
                ws.write(buffer_cat(sbuf("TEXT"),
                    cbuf(0x03, 0xea, 0xf0, 0x28, 0x8c, 0xbc)));
                if(! restart(error::failed))
                    return;

                // cause bad utf8
                ws.set_option(message_type(opcode::binary));
                ws.write(buffer_cat(sbuf("TEXT"),
                    cbuf(0x03, 0xea, 0xf0, 0x28, 0x8c, 0xbc)));
                ws.write(sbuf("Hello"));
                if(! restart(error::failed))
                    return;

                // cause bad close
                ws.set_option(message_type(opcode::binary));
                ws.write(buffer_cat(sbuf("RAW"),
                    cbuf(0x88, 0x02, 0x03, 0xed)));
                if(! restart(error::failed))
                    return;

                // unexpected cont
                boost::asio::write(ws.next_layer(),
                    cbuf(0x80, 0x80, 0xff, 0xff, 0xff, 0xff));
                if(! restart(error::closed))
                    return;

                // expected cont
                ws.write_frame(false, boost::asio::null_buffers{});
                boost::asio::write(ws.next_layer(),
                    cbuf(0x81, 0x80, 0xff, 0xff, 0xff, 0xff));
                if(! restart(error::closed))
                    return;

                // message size above 2^64
                ws.write_frame(false, cbuf(0x00));
                boost::asio::write(ws.next_layer(),
                    cbuf(0x80, 0xff, 0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff));
                if(! restart(error::closed))
                    return;

                // message size exceeds max
                ws.set_option(read_message_max{1});
                ws.write(cbuf(0x00, 0x00));
                if(! restart(error::failed))
                    return;
                ws.set_option(read_message_max{16*1024*1024});

                // invalid fixed frame header
                boost::asio::write(ws.next_layer(),
                    cbuf(0x8f, 0x80, 0xff, 0xff, 0xff, 0xff));
                if(! restart(error::closed))
                    return;

                // cause non-canonical extended size
                ws.write(buffer_cat(sbuf("RAW"),
                    cbuf(0x82, 0x7e, 0x00, 0x01, 0x00)));
                if(! restart(error::failed))
                    return;
            }
            catch(system_error const&)
            {
                continue;
            }
            break;
        }
        BEAST_EXPECT(n < limit);
    }

    void testAsyncClient(
        endpoint_type const& ep, yield_context do_yield)
    {
        using boost::asio::buffer;
        static std::size_t constexpr limit = 200;
        std::size_t n;
        for(n = 0; n < limit; ++n)
        {
            stream<test::fail_stream<socket_type>> ws(n, ios_);
            auto const restart =
                [&](error_code ev)
                {
                    opcode op;
                    streambuf db;
                    error_code ec;
                    ws.async_read(op, db, do_yield[ec]);
                    if(! ec)
                    {
                        fail();
                        return false;
                    }
                    if(ec != ev)
                    {
                        auto const s = ec.message();
                        throw system_error{ec};
                    }
                    ec = {};
                    ws.lowest_layer().close(ec);
                    ec = {};
                    ws.lowest_layer().connect(ep, ec);
                    if(! BEAST_EXPECTS(! ec, ec.message()))
                        return false;
                    ws.async_handshake("localhost", "/", do_yield[ec]);
                    if(ec)
                        throw system_error{ec};
                    return true;
                };
            try
            {
                error_code ec;

                // connect
                ws.lowest_layer().connect(ep, ec);
                if(! BEAST_EXPECTS(! ec, ec.message()))
                    return;
                ws.async_handshake("localhost", "/", do_yield[ec]);
                if(ec)
                    throw system_error{ec};

                // send message
                ws.set_option(auto_fragment{false});
                ws.set_option(message_type(opcode::text));
                ws.async_write(sbuf("Hello"), do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                {
                    // receive echoed message
                    opcode op;
                    streambuf db;
                    ws.async_read(op, db, do_yield[ec]);
                    if(ec)
                        throw system_error{ec};
                    BEAST_EXPECT(op == opcode::text);
                    BEAST_EXPECT(to_string(db.data()) == "Hello");
                }

                // close, no payload
                ws.async_close({}, do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                if(! restart(error::closed))
                    return;

                // close with code
                ws.async_close(close_code::going_away, do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                if(! restart(error::closed))
                    return;

                // close with code and reason string
                ws.async_close({close_code::going_away, "Going away"}, do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                if(! restart(error::closed))
                    return;

                // send ping and message
                bool pong = false;
                {
                    ws.set_option(pong_callback{
                        [&](ping_data const& payload)
                        {
                            BEAST_EXPECT(! pong);
                            pong = true;
                            BEAST_EXPECT(payload == "");
                        }});
                    ws.async_ping("", do_yield[ec]);
                    if(ec)
                        throw system_error{ec};
                    ws.set_option(message_type(opcode::binary));
                    ws.async_write(sbuf("Hello"), do_yield[ec]);
                    if(ec)
                        throw system_error{ec};
                    // receive echoed message
                    opcode op;
                    streambuf db;
                    ws.async_read(op, db, do_yield[ec]);
                    if(ec)
                        throw system_error{ec};
                    BEAST_EXPECT(op == opcode::binary);
                    BEAST_EXPECT(to_string(db.data()) == "Hello");
                    ws.set_option(pong_callback{});
                }

                // send ping and fragmented message
                {
                    ws.set_option(pong_callback{
                        [&](ping_data const& payload)
                        {
                            BEAST_EXPECT(payload == "payload");
                        }});
                    ws.async_ping("payload", do_yield[ec]);
                    if(! ec)
                        ws.async_write_frame(false, sbuf("Hello, "), do_yield[ec]);
                    if(! ec)
                        ws.async_write_frame(false, sbuf(""), do_yield[ec]);
                    if(! ec)
                        ws.async_write_frame(true, sbuf("World!"), do_yield[ec]);
                    if(ec)
                        throw system_error{ec};
                    {
                        // receive echoed message
                        opcode op;
                        streambuf db;
                        ws.async_read(op, db, do_yield[ec]);
                        if(ec)
                            throw system_error{ec};
                        BEAST_EXPECT(to_string(db.data()) == "Hello, World!");
                    }
                    ws.set_option(pong_callback{});
                }

                // send pong
                ws.async_pong("", do_yield[ec]);

                // send auto fragmented message
                ws.set_option(auto_fragment{true});
                ws.set_option(write_buffer_size{8});
                ws.async_write(sbuf("Now is the time for all good men"), do_yield[ec]);
                {
                    // receive echoed message
                    opcode op;
                    streambuf db;
                    ws.async_read(op, db, do_yield[ec]);
                    if(ec)
                        throw system_error{ec};
                    BEAST_EXPECT(to_string(db.data()) == "Now is the time for all good men");
                }
                ws.set_option(auto_fragment{false});
                ws.set_option(write_buffer_size{4096});

                // send message with mask buffer limit
                {
                    std::string s(2000, '*');
                    ws.set_option(write_buffer_size(1200));
                    ws.async_write(buffer(s.data(), s.size()), do_yield[ec]);
                    if(ec)
                        throw system_error{ec};
                    {
                        // receive echoed message
                        opcode op;
                        streambuf db;
                        ws.async_read(op, db, do_yield[ec]);
                        if(ec)
                            throw system_error{ec};
                        BEAST_EXPECT(to_string(db.data()) == s);
                    }
                }

                // cause ping
                ws.set_option(message_type(opcode::binary));
                ws.async_write(sbuf("PING"), do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                ws.set_option(message_type(opcode::text));
                ws.async_write(sbuf("Hello"), do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                {
                    // receive echoed message
                    opcode op;
                    streambuf db;
                    ws.async_read(op, db, do_yield[ec]);
                    if(ec)
                        throw system_error{ec};
                    BEAST_EXPECT(op == opcode::text);
                    BEAST_EXPECT(to_string(db.data()) == "Hello");
                }

                // cause close
                ws.set_option(message_type(opcode::binary));
                ws.async_write(sbuf("CLOSE"), do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                if(! restart(error::closed))
                    return;

                // send bad utf8
                ws.set_option(message_type(opcode::binary));
                ws.async_write(buffer_cat(sbuf("TEXT"),
                    cbuf(0x03, 0xea, 0xf0, 0x28, 0x8c, 0xbc)), do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                if(! restart(error::failed))
                    return;

                // cause bad utf8
                ws.set_option(message_type(opcode::binary));
                ws.async_write(buffer_cat(sbuf("TEXT"),
                    cbuf(0x03, 0xea, 0xf0, 0x28, 0x8c, 0xbc)), do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                ws.async_write(sbuf("Hello"), do_yield[ec]);
                if(! restart(error::failed))
                    return;

                // cause bad close
                ws.set_option(message_type(opcode::binary));
                ws.async_write(buffer_cat(sbuf("RAW"),
                    cbuf(0x88, 0x02, 0x03, 0xed)), do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                if(! restart(error::failed))
                    return;

                // unexpected cont
                boost::asio::async_write(ws.next_layer(),
                    cbuf(0x80, 0x80, 0xff, 0xff, 0xff, 0xff),
                        do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                if(! restart(error::closed))
                    return;

                // expected cont
                ws.async_write_frame(false,
                    boost::asio::null_buffers{}, do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                boost::asio::async_write(ws.next_layer(),
                    cbuf(0x81, 0x80, 0xff, 0xff, 0xff, 0xff),
                        do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                if(! restart(error::closed))
                    return;

                // message size above 2^64
                ws.async_write_frame(false, cbuf(0x00), do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                boost::asio::async_write(ws.next_layer(),
                    cbuf(0x80, 0xff, 0xff, 0xff, 0xff, 0xff,
                        0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff, 0xff),
                            do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                if(! restart(error::closed))
                    return;

                // message size exceeds max
                ws.set_option(read_message_max{1});
                ws.async_write(cbuf(0x00, 0x00), do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                if(! restart(error::failed))
                    return;

                // invalid fixed frame header
                boost::asio::async_write(ws.next_layer(),
                    cbuf(0x8f, 0x80, 0xff, 0xff, 0xff, 0xff),
                        do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                if(! restart(error::closed))
                    return;

                // cause non-canonical extended size
                ws.async_write(buffer_cat(sbuf("RAW"),
                    cbuf(0x82, 0x7e, 0x00, 0x01, 0x00)),
                        do_yield[ec]);
                if(ec)
                    throw system_error{ec};
                if(! restart(error::failed))
                    return;
            }
            catch(system_error const&)
            {
                continue;
            }
            break;
        }
        BEAST_EXPECT(n < limit);
    }

    void testAsyncWriteFrame(endpoint_type const& ep)
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

    void run() override
    {
        static_assert(std::is_constructible<
            stream<socket_type>, boost::asio::io_service&>::value, "");

        static_assert(std::is_move_constructible<
            stream<socket_type>>::value, "");

        static_assert(std::is_move_assignable<
            stream<socket_type>>::value, "");

        static_assert(std::is_constructible<
            stream<socket_type&>, socket_type&>::value, "");

        static_assert(std::is_move_constructible<
            stream<socket_type&>>::value, "");

        static_assert(! std::is_move_assignable<
            stream<socket_type&>>::value, "");

        log << "sizeof(websocket::stream) == " <<
            sizeof(websocket::stream<boost::asio::ip::tcp::socket&>) << std::endl;

        auto const any = endpoint_type{
            address_type::from_string("127.0.0.1"), 0};

        for(std::size_t n = 0; n < 1; ++n)
        {
            testOptions();
            testAccept();
            testBadHandshakes();
            testBadResponses();
            {
                sync_echo_server server(true, any);
                auto const ep = server.local_endpoint();

                //testInvokable1(ep);
                testInvokable2(ep);
                testInvokable3(ep);
                testInvokable4(ep);
                //testInvokable5(ep);

                testSyncClient(ep);
                testAsyncWriteFrame(ep);
                yield_to_mf(ep, &stream_test::testAsyncClient);
            }
            {
                error_code ec;
                async_echo_server server{nullptr, 4};
                server.open(true, any, ec);
                BEAST_EXPECTS(! ec, ec.message());
                auto const ep = server.local_endpoint();
                testSyncClient(ep);
                testAsyncWriteFrame(ep);
                yield_to_mf(ep, &stream_test::testAsyncClient);
            }
        }
    }
};

BEAST_DEFINE_TESTSUITE(stream,websocket,beast);

} // websocket
} // beast
