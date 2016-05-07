//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/streambuf_readstream.hpp>

#include <beast/core/streambuf.hpp>
#include <beast/test/fail_stream.hpp>
#include <beast/test/string_stream.hpp>
#include <beast/test/yield_to.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio.hpp>

namespace beast {

class streambuf_readstream_test
    : public unit_test::suite
    , public test::enable_yield_to
{
    using self = streambuf_readstream_test;

public:
    void testSpecialMembers()
    {
        using socket_type = boost::asio::ip::tcp::socket;
        boost::asio::io_service ios;
        {
            streambuf_readstream<socket_type, streambuf> srs(ios);
            streambuf_readstream<socket_type, streambuf> srs2(std::move(srs));
            srs = std::move(srs2);
            expect(&srs.get_io_service() == &ios);
            expect(&srs.get_io_service() == &srs2.get_io_service());
        }
        {
            socket_type sock(ios);
            streambuf_readstream<socket_type&, streambuf> srs(sock);
            streambuf_readstream<socket_type&, streambuf> srs2(std::move(srs));
        }
    }

    void testRead(yield_context do_yield)
    {
        using boost::asio::buffer;
        using boost::asio::buffer_copy;
        static std::size_t constexpr limit = 100;
        std::size_t n;
        std::string s;
        s.resize(13);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<
                test::string_stream> fs(n, ios_, ", world!");
            streambuf_readstream<
                decltype(fs)&, streambuf> srs(fs);
            srs.buffer().commit(buffer_copy(
                srs.buffer().prepare(5), buffer("Hello", 5)));
            boost::system::error_code ec;
            boost::asio::read(srs, buffer(&s[0], s.size()), ec);
            if(! ec)
            {
                expect(s == "Hello, world!");
                break;
            }
        }
        expect(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<
                test::string_stream> fs(n, ios_, ", world!");
            streambuf_readstream<
                decltype(fs)&, streambuf> srs(fs);
            srs.capacity(3);
            srs.buffer().commit(buffer_copy(
                srs.buffer().prepare(5), buffer("Hello", 5)));
            boost::system::error_code ec;
            boost::asio::read(srs, buffer(&s[0], s.size()), ec);
            if(! ec)
            {
                expect(s == "Hello, world!");
                break;
            }
        }
        expect(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<
                test::string_stream> fs(n, ios_, ", world!");
            streambuf_readstream<
                decltype(fs)&, streambuf> srs(fs);
            srs.buffer().commit(buffer_copy(
                srs.buffer().prepare(5), buffer("Hello", 5)));
            boost::system::error_code ec;
            boost::asio::async_read(
                srs, buffer(&s[0], s.size()), do_yield[ec]);
            if(! ec)
            {
                expect(s == "Hello, world!");
                break;
            }
        }
        expect(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<
                test::string_stream> fs(n, ios_, ", world!");
            streambuf_readstream<
                decltype(fs)&, streambuf> srs(fs);
            srs.capacity(3);
            srs.buffer().commit(buffer_copy(
                srs.buffer().prepare(5), buffer("Hello", 5)));
            boost::system::error_code ec;
            boost::asio::async_read(
                srs, buffer(&s[0], s.size()), do_yield[ec]);
            if(! ec)
            {
                expect(s == "Hello, world!");
                break;
            }
        }
        expect(n < limit);
    }

    void run() override
    {
        testSpecialMembers();

        yield_to(std::bind(&self::testRead,
            this, std::placeholders::_1));
    }
};

BEAST_DEFINE_TESTSUITE(streambuf_readstream,core,beast);

} // beast

