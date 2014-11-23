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
    // Convert a ConstBufferSequence to a string
    template <class ConstBufferSequence>
    static
    std::string
    to_str (ConstBufferSequence const& b)
    {
        std::string s;
        auto const n = boost::asio::buffer_size(b);
        s.resize(n);
        boost::asio::buffer_copy(
            boost::asio::buffer(&s[0], n), b);
        return s;
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

        pass();
    }
};

BEAST_DEFINE_TESTSUITE(streambuf,asio,beast);

}
}
