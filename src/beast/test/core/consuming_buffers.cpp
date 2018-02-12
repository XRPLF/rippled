//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/consuming_buffers.hpp>

#include "buffer_test.hpp"

#include <beast/core/buffer_cat.hpp>
#include <beast/core/ostream.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio/buffer.hpp>
#include <string>

namespace beast {

class consuming_buffers_test : public beast::unit_test::suite
{
public:
    template<class BufferSequence>
    static
    consuming_buffers<BufferSequence>
    consumed_buffers(BufferSequence const& bs, std::size_t n)
    {
        consuming_buffers<BufferSequence> cb(bs);
        cb.consume(n);
        return cb;
    }

    template<class Buffers1, class Buffers2>
    static
    bool
    eq(Buffers1 const& lhs, Buffers2 const& rhs)
    {
        using namespace test;
        return to_string(lhs) == to_string(rhs);
    }

    template<class ConstBufferSequence>
    void
    expect_size(std::size_t n, ConstBufferSequence const& buffers)
    {
        BEAST_EXPECT(test::size_pre(buffers) == n);
        BEAST_EXPECT(test::size_post(buffers) == n);
        BEAST_EXPECT(test::size_rev_pre(buffers) == n);
        BEAST_EXPECT(test::size_rev_post(buffers) == n);
    }

    void
    testMembers()
    {
        char buf[12];
        consuming_buffers<
            boost::asio::const_buffers_1> cb1{
                boost::in_place_init, buf, sizeof(buf)};
        consuming_buffers<
            boost::asio::const_buffers_1> cb2{
                boost::in_place_init, nullptr, 0};
        cb2 = cb1;
        cb1 = std::move(cb2);
    }

    void
    testMatrix()
    {
        using namespace test;
        using boost::asio::buffer;
        using boost::asio::const_buffer;
        char buf[12];
        std::string const s = "Hello, world";
        BEAST_EXPECT(s.size() == sizeof(buf));
        buffer_copy(buffer(buf), buffer(s));
        BEAST_EXPECT(to_string(buffer(buf)) == s);
        for(std::size_t i = 1; i < 4; ++i) {
        for(std::size_t j = 1; j < 4; ++j) {
        for(std::size_t x = 1; x < 4; ++x) {
        for(std::size_t y = 1; y < 4; ++y) {
        std::size_t k = sizeof(buf) - (i + j);
        std::size_t z = sizeof(buf) - (x + y);
        {
            std::array<const_buffer, 3> bs{{
                const_buffer{&buf[0], i},
                const_buffer{&buf[i], j},
                const_buffer{&buf[i+j], k}}};
            consuming_buffers<decltype(bs)> cb(bs);
            BEAST_EXPECT(to_string(cb) == s);
            expect_size(s.size(), cb);
            cb.consume(0);
            BEAST_EXPECT(eq(cb, consumed_buffers(bs, 0)));
            BEAST_EXPECT(to_string(cb) == s);
            expect_size(s.size(), cb);
            cb.consume(x);
            BEAST_EXPECT(to_string(cb) == s.substr(x));
            BEAST_EXPECT(eq(cb, consumed_buffers(bs, x)));
            cb.consume(y);
            BEAST_EXPECT(to_string(cb) == s.substr(x+y));
            BEAST_EXPECT(eq(cb, consumed_buffers(bs, x+y)));
            cb.consume(z);
            BEAST_EXPECT(to_string(cb) == "");
            BEAST_EXPECT(eq(cb, consumed_buffers(bs, x+y+z)));
            cb.consume(1);
            BEAST_EXPECT(to_string(cb) == "");
            BEAST_EXPECT(eq(cb, consumed_buffers(bs, x+y+z)));
        }
        }}}}
    }
    
    void
    testDefaultCtor()
    {
        using namespace test;
        class test_buffer : public boost::asio::const_buffers_1
        {
        public:
            test_buffer()
                : boost::asio::const_buffers_1("\r\n", 2)
            {
            }
        };

        consuming_buffers<test_buffer> cb;
        BEAST_EXPECT(to_string(cb) == "\r\n");
    }

    void
    testInPlace()
    {
        using namespace test;
        consuming_buffers<buffer_cat_view<
            boost::asio::const_buffers_1,
            boost::asio::const_buffers_1>> cb(
                boost::in_place_init,
                    boost::asio::const_buffers_1("\r", 1),
                    boost::asio::const_buffers_1("\n", 1));
        BEAST_EXPECT(to_string(cb) == "\r\n");
    }

    void
    testNullBuffers()
    {
        using boost::asio::buffer_copy;
        using boost::asio::buffer_size;
        using boost::asio::null_buffers;
        consuming_buffers<null_buffers> cb(
            null_buffers{});
        BEAST_EXPECT(buffer_size(cb) == 0);
        consuming_buffers<null_buffers> cb2(
            null_buffers{});
        BEAST_EXPECT(buffer_copy(cb2, cb) == 0);
    }

    void
    testIterator()
    {
        using boost::asio::const_buffer;
        std::array<const_buffer, 3> ba;
        consuming_buffers<decltype(ba)> cb(ba);
        std::size_t n = 0;
        for(auto it = cb.end(); it != cb.begin(); --it)
            ++n;
        BEAST_EXPECT(n == 3);
    }

    void run() override
    {
        testMembers();
        testMatrix();
        testDefaultCtor();
        testInPlace();
        testNullBuffers();
        testIterator();
    }
};

BEAST_DEFINE_TESTSUITE(consuming_buffers,core,beast);

} // beast
