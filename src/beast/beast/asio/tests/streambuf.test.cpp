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
    // Convert a buffer sequence to a string
    template <class Buffers>
    static
    std::string
    to_str (Buffers const& b)
    {
        std::string s;
        auto const n = boost::asio::buffer_size(b);
        s.resize(n);
        boost::asio::buffer_copy(
            boost::asio::buffer(&s[0], n), b);
        return s;
    }

    // Fill a buffer sequence with predictable data
    template <class Buffers>
    static
    void
    fill (Buffers const& b)
    {
        char c = 0;
        auto first = boost::asio::buffers_begin(b);
        auto last = boost::asio::buffers_end(b);
        while (first != last)
            *first++ = c++;
    }

    // Check that a buffer sequence has predictable data
    template <class Buffers>
    void
    check (Buffers const& b, char c = 0)
    {
        auto first = boost::asio::buffers_begin(b);
        auto last = boost::asio::buffers_end(b);
        while (first != last)
            expect (*first++ == c++);
    }

    void
    test_prepare()
    {
        testcase << "prepare";
        beast::asio::streambuf b(11);
        for (std::size_t n = 0; n < 97; ++n)
        {
            fill(b.prepare(n));
            b.commit(n);
            check(b.data());
            b.consume(n);
        }
    }

    void
    test_commit()
    {
        testcase << "commit";
        beast::asio::streambuf b(11);
        for (std::size_t n = 0; n < 97; ++n)
        {
            fill(b.prepare(n));
            char c = 0;
            for (int i = 1;; ++i)
            {
                b.commit(i);
                check(b.data(), c);
                b.consume(i);
                if (b.size() < 1)
                    break;
                c += i;
            }
        }
    }

    void
    test_consume()
    {
        testcase << "consume";
        beast::asio::streambuf b(11);
        for (std::size_t n = 0; n < 97; ++n)
        {
            fill(b.prepare(n));
            b.commit(n);
            char c = 0;
            for (int i = 1; b.size() > 0; ++i)
            {
                check(b.data(), c);
                b.consume(i);
                c += i;
            }
        }
    }

    void run()
    {
        {
            beast::asio::streambuf b(10);
            std::string const s = "1234567890";
            b << s;
            expect (to_str(b.data()) == s);
            b.prepare(5);
        }

        {
            beast::asio::streambuf b(10);
            b.prepare(10);
            b.commit(10);
            b.consume(10);
        }

        {
            beast::asio::streambuf b(5);
            boost::asio::buffer_copy(b.prepare(14),
                boost::asio::buffer(std::string("1234567890ABCD")));
            b.commit(4);
            expect(to_str(b.data()) == "1234");
            b.consume(4);
            b.commit(10);
            expect(to_str(b.data()) == "567890ABCD");
        }
        
        test_prepare();
        test_commit();
        test_consume();
    }
};

BEAST_DEFINE_TESTSUITE(streambuf,asio,beast);

}
}
