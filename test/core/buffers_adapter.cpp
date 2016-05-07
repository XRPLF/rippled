//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/core/buffers_adapter.hpp>

#include <beast/core/streambuf.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/streambuf.hpp>
#include <iterator>

namespace beast {

class buffers_adapter_test : public unit_test::suite
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

    void testBuffersAdapter()
    {
        using boost::asio::buffer;
        using boost::asio::buffer_cast;
        using boost::asio::buffer_size;
        using boost::asio::const_buffer;
        using boost::asio::mutable_buffer;
        char buf[12];
        std::string const s = "Hello, world";
        expect(s.size() == sizeof(buf));
        for(std::size_t i = 1; i < 4; ++i) {
        for(std::size_t j = 1; j < 4; ++j) {
        for(std::size_t x = 1; x < 4; ++x) {
        for(std::size_t y = 1; y < 4; ++y) {
        for(std::size_t t = 1; t < 4; ++ t) {
        for(std::size_t u = 1; u < 4; ++ u) {
        std::size_t k = sizeof(buf) - (i + j);
        std::size_t z = sizeof(buf) - (x + y);
        std::size_t v = sizeof(buf) - (t + u);
        {
            std::memset(buf, 0, sizeof(buf));
            std::array<mutable_buffer, 3> bs{{
                mutable_buffer{&buf[0], i},
                mutable_buffer{&buf[i], j},
                mutable_buffer{&buf[i+j], k}}};
            buffers_adapter<decltype(bs)> ba(std::move(bs));
            expect(ba.max_size() == sizeof(buf));
            {
                auto d = ba.prepare(z);
                expect(buffer_size(d) == z);
            }
            {
                auto d = ba.prepare(0);
                expect(buffer_size(d) == 0);
            }
            {
                auto d = ba.prepare(y);
                expect(buffer_size(d) == y);
            }
            {
                auto d = ba.prepare(x);
                expect(buffer_size(d) == x);
                ba.commit(buffer_copy(d, buffer(s.data(), x)));
            }
            expect(ba.size() == x);
            expect(ba.max_size() == sizeof(buf) - x);
            expect(buffer_size(ba.data()) == ba.size());
            {
                auto d = ba.prepare(x);
                expect(buffer_size(d) == x);
            }
            {
                auto d = ba.prepare(0);
                expect(buffer_size(d) == 0);
            }
            {
                auto d = ba.prepare(z);
                expect(buffer_size(d) == z);
            }
            {
                auto d = ba.prepare(y);
                expect(buffer_size(d) == y);
                ba.commit(buffer_copy(d, buffer(s.data()+x, y)));
            }
            ba.commit(1);
            expect(ba.size() == x + y);
            expect(ba.max_size() == sizeof(buf) - (x + y));
            expect(buffer_size(ba.data()) == ba.size());
            {
                auto d = ba.prepare(x);
                expect(buffer_size(d) == x);
            }
            {
                auto d = ba.prepare(y);
                expect(buffer_size(d) == y);
            }
            {
                auto d = ba.prepare(0);
                expect(buffer_size(d) == 0);
            }
            {
                auto d = ba.prepare(z); expect(buffer_size(d) == z);
                ba.commit(buffer_copy(d, buffer(s.data()+x+y, z)));
            }
            ba.commit(2);
            expect(ba.size() == x + y + z);
            expect(ba.max_size() == 0);
            expect(buffer_size(ba.data()) == ba.size());
            expect(to_string(ba.data()) == s);
            ba.consume(t);
            {
                auto d = ba.prepare(0);
                expect(buffer_size(d) == 0);
            }
            expect(to_string(ba.data()) == s.substr(t, std::string::npos));
            ba.consume(u);
            expect(to_string(ba.data()) == s.substr(t + u, std::string::npos));
            ba.consume(v);
            expect(to_string(ba.data()) == "");
            ba.consume(1);
            {
                auto d = ba.prepare(0);
                expect(buffer_size(d) == 0);
            }
            try
            {
                ba.prepare(1);
                fail();
            }
            catch(...)
            {
                pass();
            }
        }
        }}}}}}
    }
    void testCommit()
    {
        using boost::asio::buffer_size;
        {
            using sb_type = boost::asio::streambuf;
            sb_type sb;
            buffers_adapter<
                sb_type::mutable_buffers_type> ba(sb.prepare(3));
            expect(buffer_size(ba.prepare(3)) == 3);
            ba.commit(2);
            expect(buffer_size(ba.data()) == 2);
        }
        {
            using sb_type = beast::streambuf;
            sb_type sb(2);
            sb.prepare(3);
            buffers_adapter<
                sb_type::mutable_buffers_type> ba(sb.prepare(8));
            expect(buffer_size(ba.prepare(8)) == 8);
            ba.commit(2);
            expect(buffer_size(ba.data()) == 2);
            ba.consume(1);
            ba.commit(6);
            ba.consume(2);
            expect(buffer_size(ba.data()) == 5);
            ba.consume(5);
        }
    }
    void run() override
    {
        testBuffersAdapter();
        testCommit();
    }
};

BEAST_DEFINE_TESTSUITE(buffers_adapter,core,beast);

} // beast
