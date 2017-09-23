//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/buffer_prefix.hpp>

#include <beast/core/consuming_buffers.hpp>
#include <beast/core/type_traits.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio/buffer.hpp>
#include <string>

namespace beast {

BOOST_STATIC_ASSERT(
    std::is_same<boost::asio::const_buffer, decltype(
        buffer_prefix(0,
            std::declval<boost::asio::const_buffer>()))>::value);

BOOST_STATIC_ASSERT(
    is_const_buffer_sequence<decltype(
        buffer_prefix(0,
            std::declval<boost::asio::const_buffers_1>()))>::value);

BOOST_STATIC_ASSERT(
    std::is_same<boost::asio::mutable_buffer, decltype(
        buffer_prefix(0,
            std::declval<boost::asio::mutable_buffer>()))>::value);
BOOST_STATIC_ASSERT(
    is_mutable_buffer_sequence<decltype(
        buffer_prefix(0,
            std::declval<boost::asio::mutable_buffers_1>()))>::value);

class buffer_prefix_test : public beast::unit_test::suite
{
public:
    template<class ConstBufferSequence>
    static
    std::size_t
    bsize1(ConstBufferSequence const& bs)
    {
        using boost::asio::buffer_size;
        std::size_t n = 0;
        for(auto it = bs.begin(); it != bs.end(); ++it)
            n += buffer_size(*it);
        return n;
    }

    template<class ConstBufferSequence>
    static
    std::size_t
    bsize2(ConstBufferSequence const& bs)
    {
        using boost::asio::buffer_size;
        std::size_t n = 0;
        for(auto it = bs.begin(); it != bs.end(); it++)
            n += buffer_size(*it);
        return n;
    }

    template<class ConstBufferSequence>
    static
    std::size_t
    bsize3(ConstBufferSequence const& bs)
    {
        using boost::asio::buffer_size;
        std::size_t n = 0;
        for(auto it = bs.end(); it != bs.begin();)
            n += buffer_size(*--it);
        return n;
    }

    template<class ConstBufferSequence>
    static
    std::size_t
    bsize4(ConstBufferSequence const& bs)
    {
        using boost::asio::buffer_size;
        std::size_t n = 0;
        for(auto it = bs.end(); it != bs.begin();)
        {
            it--;
            n += buffer_size(*it);
        }
        return n;
    }

    template<class ConstBufferSequence>
    static
    std::string
    to_string(ConstBufferSequence const& bs)
    {
        using boost::asio::buffer_cast;
        using boost::asio::buffer_size;
        std::string s;
        s.reserve(buffer_size(bs));
        for(boost::asio::const_buffer b : bs)
            s.append(buffer_cast<char const*>(b),
                buffer_size(b));
        return s;
    }

    template<class BufferType>
    void testMatrix()
    {
        using boost::asio::buffer_size;
        std::string s = "Hello, world";
        BEAST_EXPECT(s.size() == 12);
        for(std::size_t x = 1; x < 4; ++x) {
        for(std::size_t y = 1; y < 4; ++y) {
        std::size_t z = s.size() - (x + y);
        {
            std::array<BufferType, 3> bs{{
                BufferType{&s[0], x},
                BufferType{&s[x], y},
                BufferType{&s[x+y], z}}};
            for(std::size_t i = 0; i <= s.size() + 1; ++i)
            {
                auto pb = buffer_prefix(i, bs);
                BEAST_EXPECT(to_string(pb) == s.substr(0, i));
                auto pb2 = pb;
                BEAST_EXPECT(to_string(pb2) == to_string(pb));
                pb = buffer_prefix(0, bs);
                pb2 = pb;
                BEAST_EXPECT(buffer_size(pb2) == 0);
                pb2 = buffer_prefix(i, bs);
                BEAST_EXPECT(to_string(pb2) == s.substr(0, i));
            }
        }
        }}
    }

    void testNullBuffers()
    {
        using boost::asio::buffer_copy;
        using boost::asio::buffer_size;
        using boost::asio::null_buffers;
        auto pb0 = buffer_prefix(0, null_buffers{});
        BEAST_EXPECT(buffer_size(pb0) == 0);
        auto pb1 = buffer_prefix(1, null_buffers{});
        BEAST_EXPECT(buffer_size(pb1) == 0);
        BEAST_EXPECT(buffer_copy(pb0, pb1) == 0);

        using pb_type = decltype(pb0);
        consuming_buffers<pb_type> cb(pb0);
        BEAST_EXPECT(buffer_size(cb) == 0);
        BEAST_EXPECT(buffer_copy(cb, pb1) == 0);
        cb.consume(1);
        BEAST_EXPECT(buffer_size(cb) == 0);
        BEAST_EXPECT(buffer_copy(cb, pb1) == 0);

        auto pbc = buffer_prefix(2, cb);
        BEAST_EXPECT(buffer_size(pbc) == 0);
        BEAST_EXPECT(buffer_copy(pbc, cb) == 0);
    }

    void testIterator()
    {
        using boost::asio::buffer_size;
        using boost::asio::const_buffer;
        char b[3];
        std::array<const_buffer, 3> bs{{
            const_buffer{&b[0], 1},
            const_buffer{&b[1], 1},
            const_buffer{&b[2], 1}}};
        auto pb = buffer_prefix(2, bs);
        BEAST_EXPECT(bsize1(pb) == 2);
        BEAST_EXPECT(bsize2(pb) == 2);
        BEAST_EXPECT(bsize3(pb) == 2);
        BEAST_EXPECT(bsize4(pb) == 2);
        std::size_t n = 0;
        for(auto it = pb.end(); it != pb.begin(); --it)
        {
            decltype(pb)::const_iterator it2(std::move(it));
            BEAST_EXPECT(buffer_size(*it2) == 1);
            it = std::move(it2);
            ++n;
        }
        BEAST_EXPECT(n == 2);
    }

    void run() override
    {
        testMatrix<boost::asio::const_buffer>();
        testMatrix<boost::asio::mutable_buffer>();
        testNullBuffers();
        testIterator();
    }
};

BEAST_DEFINE_TESTSUITE(buffer_prefix,core,beast);

} // beast
