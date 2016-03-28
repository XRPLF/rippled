//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

#include <beast/asio/append_buffers.h>
#include <beast/asio/buffers_adapter.h>
#include <beast/asio/prepare_buffers.h>
#include <beast/asio/consuming_buffers.h>
#include <beast/asio/streambuf.h>
#include <beast/asio/static_streambuf.h>
#include <beast/unit_test/suite.h>
#include <boost/asio/buffer.hpp>
#include <array>
#include <list>
#include <string>

namespace beast {
namespace asio {

class buffers_test : public unit_test::suite
{
public:
    template<class ConstBufferSequence>
    static
    std::string
    to_string(ConstBufferSequence const& bs)
    {
        using namespace boost::asio;
        std::string s;
        s.reserve(buffer_size(bs));
        for(auto const& b : bs)
            s.append(buffer_cast<char const*>(b),
                buffer_size(b));
        return s;
    }

    void testStreambuf()
    {
        using namespace boost::asio;
        std::string const s = "Hello, world";
        expect(s.size() == 12);
        for(std::size_t i = 1; i < 12; ++i) {
        for(std::size_t x = 1; x < 4; ++x) {
        for(std::size_t y = 1; y < 4; ++y) {
        for(std::size_t t = 1; t < 4; ++ t) {
        for(std::size_t u = 1; u < 4; ++ u) {
        std::size_t z = s.size() - (x + y);
        std::size_t v = s.size() - (t + u);
        {
            streambuf ba(i);
            decltype(ba)::mutable_buffers_type d;
            d = ba.prepare(z); expect(buffer_size(d) == z);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
            d = ba.prepare(y); expect(buffer_size(d) == y);
            d = ba.prepare(x); expect(buffer_size(d) == x);
            ba.commit(buffer_copy(d, buffer(s.data(), x)));
            expect(ba.size() == x);
            expect(buffer_size(ba.data()) == ba.size());
            d = ba.prepare(x); expect(buffer_size(d) == x);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
            d = ba.prepare(z); expect(buffer_size(d) == z);
            d = ba.prepare(y); expect(buffer_size(d) == y);
            ba.commit(buffer_copy(d, buffer(s.data()+x, y)));
            ba.commit(1);
            expect(ba.size() == x + y);
            expect(buffer_size(ba.data()) == ba.size());
            d = ba.prepare(x); expect(buffer_size(d) == x);
            d = ba.prepare(y); expect(buffer_size(d) == y);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
            d = ba.prepare(z); expect(buffer_size(d) == z);
            ba.commit(buffer_copy(d, buffer(s.data()+x+y, z)));
            ba.commit(2);
            expect(ba.size() == x + y + z);
            expect(buffer_size(ba.data()) == ba.size());
            expect(to_string(ba.data()) == s);
            ba.consume(t);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
            expect(to_string(ba.data()) == s.substr(t, std::string::npos));
            ba.consume(u);
            expect(to_string(ba.data()) == s.substr(t + u, std::string::npos));
            ba.consume(v);
            expect(to_string(ba.data()) == "");
            ba.consume(1);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
        }
        }}}}}
    }

    void testBuffersAdapter()
    {
        using namespace boost::asio;
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
            decltype(ba)::mutable_buffers_type d;
            d = ba.prepare(z); expect(buffer_size(d) == z);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
            d = ba.prepare(y); expect(buffer_size(d) == y);
            d = ba.prepare(x); expect(buffer_size(d) == x);
            ba.commit(buffer_copy(d, buffer(s.data(), x)));
            expect(ba.size() == x);
            expect(ba.max_size() == sizeof(buf) - x);
            expect(buffer_size(ba.data()) == ba.size());
            d = ba.prepare(x); expect(buffer_size(d) == x);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
            d = ba.prepare(z); expect(buffer_size(d) == z);
            d = ba.prepare(y); expect(buffer_size(d) == y);
            ba.commit(buffer_copy(d, buffer(s.data()+x, y)));
            ba.commit(1);
            expect(ba.size() == x + y);
            expect(ba.max_size() == sizeof(buf) - (x + y));
            expect(buffer_size(ba.data()) == ba.size());
            d = ba.prepare(x); expect(buffer_size(d) == x);
            d = ba.prepare(y); expect(buffer_size(d) == y);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
            d = ba.prepare(z); expect(buffer_size(d) == z);
            ba.commit(buffer_copy(d, buffer(s.data()+x+y, z)));
            ba.commit(2);
            expect(ba.size() == x + y + z);
            expect(ba.max_size() == 0);
            expect(buffer_size(ba.data()) == ba.size());
            expect(to_string(ba.data()) == s);
            ba.consume(t);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
            expect(to_string(ba.data()) == s.substr(t, std::string::npos));
            ba.consume(u);
            expect(to_string(ba.data()) == s.substr(t + u, std::string::npos));
            ba.consume(v);
            expect(to_string(ba.data()) == "");
            ba.consume(1);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
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

    void testConsuming()
    {
        using namespace boost::asio;
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
            cb.consume(0);
            expect(to_string(cb) == s);
            cb.consume(x);
            expect(to_string(cb) == s.substr(x));
            cb.consume(y);
            expect(to_string(cb) == s.substr(x+y));
            cb.consume(z);
            expect(to_string(cb) == "");
            cb.consume(1);
            expect(to_string(cb) == "");
        }
        }}}}
    }

    void testStaticBuffers()
    {
        using namespace boost::asio;
        char buf[12];
        std::string const s = "Hello, world";
        expect(s.size() == sizeof(buf));
        for(std::size_t i = 1; i < 4; ++i) {
        for(std::size_t j = 1; j < 4; ++j) {
        for(std::size_t x = 1; x < 4; ++x) {
        for(std::size_t y = 1; y < 4; ++y) {
        for(std::size_t t = 1; t < 4; ++ t) {
        for(std::size_t u = 1; u < 4; ++ u) {
        std::size_t z = sizeof(buf) - (x + y);
        std::size_t v = sizeof(buf) - (t + u);
        {
            std::memset(buf, 0, sizeof(buf));
            static_streambuf_n<sizeof(buf)> ba;
            decltype(ba)::mutable_buffers_type d;
            d = ba.prepare(z); expect(buffer_size(d) == z);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
            d = ba.prepare(y); expect(buffer_size(d) == y);
            d = ba.prepare(x); expect(buffer_size(d) == x);
            ba.commit(buffer_copy(d, buffer(s.data(), x)));
            expect(ba.size() == x);
            expect(buffer_size(ba.data()) == ba.size());
            d = ba.prepare(x); expect(buffer_size(d) == x);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
            d = ba.prepare(z); expect(buffer_size(d) == z);
            d = ba.prepare(y); expect(buffer_size(d) == y);
            ba.commit(buffer_copy(d, buffer(s.data()+x, y)));
            ba.commit(1);
            expect(ba.size() == x + y);
            expect(buffer_size(ba.data()) == ba.size());
            d = ba.prepare(x); expect(buffer_size(d) == x);
            d = ba.prepare(y); expect(buffer_size(d) == y);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
            d = ba.prepare(z); expect(buffer_size(d) == z);
            ba.commit(buffer_copy(d, buffer(s.data()+x+y, z)));
            ba.commit(2);
            expect(ba.size() == x + y + z);
            expect(buffer_size(ba.data()) == ba.size());
            expect(to_string(ba.data()) == s);
            ba.consume(t);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
            expect(to_string(ba.data()) == s.substr(t, std::string::npos));
            ba.consume(u);
            expect(to_string(ba.data()) == s.substr(t + u, std::string::npos));
            ba.consume(v);
            expect(to_string(ba.data()) == "");
            ba.consume(1);
            d = ba.prepare(0); expect(buffer_size(d) == 0);
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

    void testAppendBuffers()
    {
        using namespace boost::asio;
        char buf[10];
        std::list<const_buffer> b1;
        std::vector<const_buffer> b2{
            const_buffer{buf+0, 1},
            const_buffer{buf+1, 2}};
        std::list<const_buffer> b3;
        std::array<const_buffer, 3> b4{{
            const_buffer{buf+3, 1},
            const_buffer{buf+4, 2},
            const_buffer{buf+6, 3}}};
        std::list<const_buffer> b5{
            const_buffer{buf+9, 1}};
        std::list<const_buffer> b6;
        auto bs = append_buffers(
            b1, b2, b3, b4, b5, b6);
        expect(buffer_size(bs) == 10);
        std::vector<const_buffer> v;
        for(auto iter = std::make_reverse_iterator(bs.end());
                iter != std::make_reverse_iterator(bs.begin()); ++iter)
            v.emplace_back(*iter);
        expect(buffer_size(bs) == 10);
        decltype(bs) bs2(bs);
        auto bs3(std::move(bs));
        bs = bs2;
        bs3 = std::move(bs2);
        {
            streambuf sb1, sb2;
            expect(buffer_size(append_buffers(
                sb1.prepare(5), sb2.prepare(7))) == 12);
            sb1.commit(5);
            sb2.commit(7);
            expect(buffer_size(append_buffers(
                sb1.data(), sb2.data())) == 12);
        }
    }

    void testClipBuffers()
    {
        using namespace boost::asio;
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
            for(int i = 0; i <= s.size() + 1; ++i)
                expect(to_string(prepare_buffers(i, bs)) ==
                    s.substr(0, i));
        }
        }}
    }

    void run() override
    {
        testStreambuf();
        testBuffersAdapter();
        testConsuming();
        testStaticBuffers();

        testAppendBuffers();
        testClipBuffers();
    }
};

BEAST_DEFINE_TESTSUITE(buffers,asio,beast);

}
}
