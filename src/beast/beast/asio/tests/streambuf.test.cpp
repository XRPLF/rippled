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

#include <beast/asio/streambuf.h>
#include <beast/unit_test/suite.h>

namespace beast {
namespace asio {

class streambuf_test : public unit_test::suite
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
        char buf[12];
        std::string const s = "Hello, world";
        expect(s.size() == sizeof(buf));
        for(std::size_t i = 1; i < 12; ++i) {
        for(std::size_t x = 1; x < 4; ++x) {
        for(std::size_t y = 1; y < 4; ++y) {
        for(std::size_t t = 1; t < 4; ++ t) {
        for(std::size_t u = 1; u < 4; ++ u) {
        std::size_t z = sizeof(buf) - (x + y);
        std::size_t v = sizeof(buf) - (t + u);
        {
            std::memset(buf, 0, sizeof(buf));
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

    void run()
    {
        testStreambuf();
    }
};

BEAST_DEFINE_TESTSUITE(streambuf,asio,beast);

}
}
