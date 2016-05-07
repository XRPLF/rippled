//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/prepare_buffers.hpp>

#include <beast/core/consuming_buffers.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio/buffer.hpp>
#include <string>

namespace beast {

class prepare_buffers_test : public beast::unit_test::suite
{
public:
    template<class ConstBufferSequence>
    static
    std::string
    to_string(ConstBufferSequence const& bs)
    {
        using boost::asio::buffer_cast;
        using boost::asio::buffer_size;
        std::string s;
        s.reserve(buffer_size(bs));
        for(auto const& b : bs)
            s.append(buffer_cast<char const*>(b),
                buffer_size(b));
        return s;
    }

    void testBuffers()
    {
        using boost::asio::buffer_size;
        using boost::asio::const_buffer;
        std::string const s = "Hello, world";
        expect(s.size() == 12);
        for(std::size_t x = 1; x < 4; ++x) {
        for(std::size_t y = 1; y < 4; ++y) {
        std::size_t z = s.size() - (x + y);
        {
            std::array<const_buffer, 3> bs{{
                const_buffer{&s[0], x},
                const_buffer{&s[x], y},
                const_buffer{&s[x+y], z}}};
            for(std::size_t i = 0; i <= s.size() + 1; ++i)
            {
                auto pb = prepare_buffers(i, bs);
                expect(to_string(pb) == s.substr(0, i));
                auto pb2 = pb;
                expect(to_string(pb2) == to_string(pb));
                pb = prepare_buffers(0, bs);
                pb2 = pb;
                expect(buffer_size(pb2) == 0);
                pb2 = prepare_buffers(i, bs);
                expect(to_string(pb2) == s.substr(0, i));
            }
        }
        }}
    }

    void testNullBuffers()
    {
        using boost::asio::buffer_copy;
        using boost::asio::buffer_size;
        using boost::asio::null_buffers;
        auto pb0 = prepare_buffers(0, null_buffers{});
        expect(buffer_size(pb0) == 0);
        auto pb1 = prepare_buffers(1, null_buffers{});
        expect(buffer_size(pb1) == 0);
        expect(buffer_copy(pb0, pb1) == 0);

        using pb_type = decltype(pb0);
        consuming_buffers<pb_type> cb(pb0);
        expect(buffer_size(cb) == 0);
        expect(buffer_copy(cb, pb1) == 0);
        cb.consume(1);
        expect(buffer_size(cb) == 0);
        expect(buffer_copy(cb, pb1) == 0);

        auto pbc = prepare_buffers(2, cb);
        expect(buffer_size(pbc) == 0);
        expect(buffer_copy(pbc, cb) == 0);
    }

    void testIterator()
    {
        using boost::asio::const_buffer;
        char b[3];
        std::array<const_buffer, 3> bs{{
            const_buffer{&b[0], 1},
            const_buffer{&b[1], 1},
            const_buffer{&b[2], 1}}};
        auto pb = prepare_buffers(2, bs);
        std::size_t n = 0;
        for(auto it = pb.end(); it != pb.begin(); --it)
            ++n;
        expect(n == 2);
    }

    void run() override
    {
        testBuffers();
        testNullBuffers();
        testIterator();
    }
};

BEAST_DEFINE_TESTSUITE(prepare_buffers,core,beast);

} // beast
