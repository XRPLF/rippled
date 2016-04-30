//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/consuming_buffers.hpp>

#include "buffer_test.hpp"
#include <beast/core/to_string.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio/buffer.hpp>
#include <string>

namespace beast {

class consuming_buffers_test : public beast::unit_test::suite
{
public:
    template<class Buffers1, class Buffers2>
    static
    bool
    eq(Buffers1 const& lhs, Buffers2 const& rhs)
    {
        return to_string(lhs) == to_string(rhs);
    }

    template<class ConstBufferSequence>
    void
    expect_size(std::size_t n, ConstBufferSequence const& buffers)
    {
        expect(test::size_pre(buffers) == n);
        expect(test::size_post(buffers) == n);
        expect(test::size_rev_pre(buffers) == n);
        expect(test::size_rev_post(buffers) == n);
    }

    void testMatrix()
    {
        using boost::asio::buffer;
        using boost::asio::const_buffer;
        char buf[12];
        std::string const s = "Hello, world";
        expect(s.size() == sizeof(buf));
        buffer_copy(buffer(buf), buffer(s));
        expect(to_string(buffer(buf)) == s);
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
            expect(to_string(cb) == s);
            expect_size(s.size(), cb);
            cb.consume(0);
            expect(eq(cb, consumed_buffers(bs, 0)));
            expect(to_string(cb) == s);
            expect_size(s.size(), cb);
            cb.consume(x);
            expect(to_string(cb) == s.substr(x));
            expect(eq(cb, consumed_buffers(bs, x)));
            cb.consume(y);
            expect(to_string(cb) == s.substr(x+y));
            expect(eq(cb, consumed_buffers(bs, x+y)));
            cb.consume(z);
            expect(to_string(cb) == "");
            expect(eq(cb, consumed_buffers(bs, x+y+z)));
            cb.consume(1);
            expect(to_string(cb) == "");
            expect(eq(cb, consumed_buffers(bs, x+y+z)));
        }
        }}}}
    }

    void testNullBuffers()
    {
        using boost::asio::buffer_copy;
        using boost::asio::buffer_size;
        using boost::asio::null_buffers;
        consuming_buffers<null_buffers> cb(
            null_buffers{});
        expect(buffer_size(cb) == 0);
        consuming_buffers<null_buffers> cb2(
            null_buffers{});
        expect(buffer_copy(cb2, cb) == 0);
    }

    void testIterator()
    {
        using boost::asio::const_buffer;
        std::array<const_buffer, 3> ba;
        consuming_buffers<decltype(ba)> cb(ba);
        std::size_t n = 0;
        for(auto it = cb.end(); it != cb.begin(); --it)
            ++n;
        expect(n == 3);
    }

    void run() override
    {
        testMatrix();
        testNullBuffers();
        testIterator();
    }
};

BEAST_DEFINE_TESTSUITE(consuming_buffers,core,beast);

} // beast
