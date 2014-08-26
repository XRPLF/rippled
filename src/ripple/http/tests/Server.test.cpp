//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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

#include <ripple/common/RippleSSLContext.h>
#include <ripple/http/Session.h>
#include <ripple/http/Server.h>
#include <beast/unit_test/suite.h>
#include <boost/asio/ip/tcp.hpp>
#include <chrono>
#include <stdexcept>
#include <thread>

namespace ripple {
namespace HTTP {

class Server_test : public beast::unit_test::suite
{
public:
    enum
    {
        testPort = 1001
    };

    class TestSink : public beast::Journal::Sink
    {
        beast::unit_test::suite& suite_;

    public:
        TestSink (beast::unit_test::suite& suite)
            : suite_ (suite)
        {
        }

        void
        write (beast::Journal::Severity level,
            std::string const& text) override
        {
            suite_.log << text;
        }
    };

    struct TestHandler : Handler
    {
        void
        onAccept (Session& session) override
        {
        }

        void
        onRequest (Session& session) override
        {
            session << "Hello, world!\n";
            if (session.message().keep_alive())
                session.complete();
            else
                session.close (true);
        }

        void
        onClose (Session& session,
            boost::system::error_code const&) override
        {
        }

        void
        onStopped (Server& server)
        {
        }
    };

    // Connect to an address
    template <class Socket>
    bool
    connect (Socket& s, std::string const& addr, int port)
    {
        try
        {
            typename Socket::endpoint_type ep (
                boost::asio::ip::address::from_string (addr), port);
            s.connect (ep);
            pass();
            return true;
        }
        catch (std::exception const& e)
        {
            fail (e.what());
        }

        return false;
    }

    // Write a string to the stream
    template <class SyncWriteStream>
    bool
    write (SyncWriteStream& s, std::string const& text)
    {
        try
        {
            boost::asio::write (s, boost::asio::buffer (text));
            pass();
            return true;
        }
        catch (std::exception const& e)
        {
            fail (e.what());
        }
        return false;
    }

    // Expect that reading the stream produces a matching string
    template <class SyncReadStream>
    bool
    expect_read (SyncReadStream& s, std::string const& match)
    {
        boost::asio::streambuf b (1000); // limit on read
        try
        {
            auto const n = boost::asio::read_until (s, b, '\n');
            if (expect (n == match.size()))
            {
                std::string got;
                got.resize (n);
                boost::asio::buffer_copy (boost::asio::buffer (
                    &got[0], n), b.data());
                return expect (got == match);
            }
        }
        catch (std::length_error const& e)
        {
            fail(e.what());
        }
        return false;
    }

    void
    test_request()
    {
        testcase("request");

        boost::asio::io_service ios;
        typedef boost::asio::ip::tcp::socket socket;
        socket s (ios);

        if (! connect (s, "127.0.0.1", testPort))
            return;

        if (! write (s,
            "GET / HTTP/1.1\r\n"
            "Connection: close\r\n"
            "\r\n"))
            return;

        if (! expect_read (s, "Hello, world!\n"))
            return ;

        try
        {
            s.shutdown (socket::shutdown_both);
            pass();
        }
        catch (std::exception const& e)
        {
            fail (e.what());
        }

        std::this_thread::sleep_for (std::chrono::seconds (1));
    }

    void
    test_keepalive()
    {
        testcase("keepalive");

        boost::asio::io_service ios;
        typedef boost::asio::ip::tcp::socket socket;
        socket s (ios);

        if (! connect (s, "127.0.0.1", testPort))
            return;

        if (! write (s,
            "GET / HTTP/1.1\r\n"
            "Connection: Keep-Alive\r\n"
            "\r\n"))
            return;

        if (! expect_read (s, "Hello, world!\n"))
            return ;

        if (! write (s,
            "GET / HTTP/1.1\r\n"
            "Connection: close\r\n"
            "\r\n"))
            return;

        if (! expect_read (s, "Hello, world!\n"))
            return ;

        try
        {
            s.shutdown (socket::shutdown_both);
            pass();
        }
        catch (std::exception const& e)
        {
            fail (e.what());
        }
    }

    void
    run()
    {
        TestSink sink {*this};
        sink.severity (beast::Journal::Severity::kAll);
        beast::Journal journal {sink};
        TestHandler handler;
        Server s (handler, journal);
        Ports ports;
        std::unique_ptr <RippleSSLContext> c (
            RippleSSLContext::createBare ());
        ports.emplace_back (testPort, beast::IP::Endpoint (
            beast::IP::AddressV4 (127, 0, 0, 1), 0),
                 Port::Security::no_ssl, c.get());
        s.setPorts (ports);

        test_request();
        //test_keepalive();

        s.stop();

        pass();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(Server,http,ripple);

}
}
