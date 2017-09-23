//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/buffered_read_stream.hpp>

#include <beast/core/multi_buffer.hpp>
#include <beast/test/fail_stream.hpp>
#include <beast/test/string_istream.hpp>
#include <beast/test/yield_to.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio.hpp>

namespace beast {

class buffered_read_stream_test
    : public unit_test::suite
    , public test::enable_yield_to
{
    using self = buffered_read_stream_test;

public:
    void testSpecialMembers()
    {
        using socket_type = boost::asio::ip::tcp::socket;
        boost::asio::io_service ios;
        {
            buffered_read_stream<socket_type, multi_buffer> srs(ios);
            buffered_read_stream<socket_type, multi_buffer> srs2(std::move(srs));
            srs = std::move(srs2);
            BEAST_EXPECT(&srs.get_io_service() == &ios);
            BEAST_EXPECT(&srs.get_io_service() == &srs2.get_io_service());
        }
        {
            socket_type sock(ios);
            buffered_read_stream<socket_type&, multi_buffer> srs(sock);
            buffered_read_stream<socket_type&, multi_buffer> srs2(std::move(srs));
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
                test::string_istream> fs(n, ios_, ", world!");
            buffered_read_stream<
                decltype(fs)&, multi_buffer> srs(fs);
            srs.buffer().commit(buffer_copy(
                srs.buffer().prepare(5), buffer("Hello", 5)));
            error_code ec = test::error::fail_error;
            boost::asio::read(srs, buffer(&s[0], s.size()), ec);
            if(! ec)
            {
                BEAST_EXPECT(s == "Hello, world!");
                break;
            }
        }
        BEAST_EXPECT(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<
                test::string_istream> fs(n, ios_, ", world!");
            buffered_read_stream<
                decltype(fs)&, multi_buffer> srs(fs);
            srs.capacity(3);
            srs.buffer().commit(buffer_copy(
                srs.buffer().prepare(5), buffer("Hello", 5)));
            error_code ec = test::error::fail_error;
            boost::asio::read(srs, buffer(&s[0], s.size()), ec);
            if(! ec)
            {
                BEAST_EXPECT(s == "Hello, world!");
                break;
            }
        }
        BEAST_EXPECT(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<
                test::string_istream> fs(n, ios_, ", world!");
            buffered_read_stream<
                decltype(fs)&, multi_buffer> srs(fs);
            srs.buffer().commit(buffer_copy(
                srs.buffer().prepare(5), buffer("Hello", 5)));
            error_code ec = test::error::fail_error;
            boost::asio::async_read(
                srs, buffer(&s[0], s.size()), do_yield[ec]);
            if(! ec)
            {
                BEAST_EXPECT(s == "Hello, world!");
                break;
            }
        }
        BEAST_EXPECT(n < limit);

        for(n = 0; n < limit; ++n)
        {
            test::fail_stream<
                test::string_istream> fs(n, ios_, ", world!");
            buffered_read_stream<
                decltype(fs)&, multi_buffer> srs(fs);
            srs.capacity(3);
            srs.buffer().commit(buffer_copy(
                srs.buffer().prepare(5), buffer("Hello", 5)));
            error_code ec = test::error::fail_error;
            boost::asio::async_read(
                srs, buffer(&s[0], s.size()), do_yield[ec]);
            if(! ec)
            {
                BEAST_EXPECT(s == "Hello, world!");
                break;
            }
        }
        BEAST_EXPECT(n < limit);
    }

    void run() override
    {
        testSpecialMembers();

        yield_to([&](yield_context yield){
            testRead(yield);});
    }
};

BEAST_DEFINE_TESTSUITE(buffered_read_stream,core,beast);

} // beast

