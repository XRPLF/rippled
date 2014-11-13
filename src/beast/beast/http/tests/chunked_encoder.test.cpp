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
#include <beast/http/chunk_encode.h>
#include <beast/unit_test/suite.h>

namespace beast {
namespace http {

class chunk_encode_test : public unit_test::suite
{
public:
    // Convert CR LF to printables for display
    static
    std::string
    encode (std::string const& s)
    {
        std::string result;
        for(auto const c : s)
        {
            if (c == '\r')
                result += "\\r";
            else if (c== '\n')
                result += "\\n";
            else
                result += c;
        }
        return result;
    }

    // Print the contents of a ConstBufferSequence to the log
    template <class ConstBufferSequence, class Log>
    static
    void
    print (ConstBufferSequence const& buffers, Log log)
    {
        for(auto const& buf : buffers)
            log << encode (std::string(
                boost::asio::buffer_cast<char const*>(buf),
                    boost::asio::buffer_size(buf)));
    } 

    // Convert a ConstBufferSequence to a string
    template <class ConstBufferSequence>
    static
    std::string
    buffer_to_string (ConstBufferSequence const& b)
    {
        std::string s;
        auto const n = boost::asio::buffer_size(b);
        s.resize(n);
        boost::asio::buffer_copy(
            boost::asio::buffer(&s[0], n), b);
        return s;
    }

    // Append a ConstBufferSequence to an existing string
    template <class ConstBufferSequence>
    static
    void
    buffer_append (std::string& s, ConstBufferSequence const& b)
    {
        s += buffer_to_string(b);
    }

    // Convert the input sequence of the stream to a
    // chunked-encoded string. The input sequence is consumed.
    template <class Streambuf>
    static
    std::string
    streambuf_to_string (Streambuf& sb,
        bool final_chunk = false)
    {
        std::string s;
        buffer_append(s, chunk_encode(sb.data(), final_chunk));
        return s;
    }

    // Check an input against the correct chunk encoded version
    void
    check (std::string const& in, std::string const& answer,
        bool final_chunk = true)
    {
        asio::streambuf sb(3);
        sb << in;
        auto const out = streambuf_to_string (sb, final_chunk);
        if (! expect (out == answer))
            log << "expected\n" << encode(answer) <<
                "\ngot\n" << encode(out);
    }

    void testStreambuf()
    {
        asio::streambuf sb(3);
        std::string const s =
            "0123456789012345678901234567890123456789012345678901234567890123456789"
            "0123456789012345678901234567890123456789012345678901234567890123456789"
            "0123456789012345678901234567890123456789012345678901234567890123456789";
        sb << s;
        expect(buffer_to_string(sb.data()) == s);
    }

    void
    testEncoder()
    {
        check("", "0\r\n\r\n");
        check("x", "1\r\nx\r\n0\r\n\r\n");
        check("abcd", "4\r\nabcd\r\n0\r\n\r\n");
        check("x", "1\r\nx\r\n", false);
        check(
            "0123456789012345678901234567890123456789012345678901234567890123456789"
            "0123456789012345678901234567890123456789012345678901234567890123456789"
            "0123456789012345678901234567890123456789012345678901234567890123456789"
            ,
            "d2\r\n"
            "0123456789012345678901234567890123456789012345678901234567890123456789"
            "0123456789012345678901234567890123456789012345678901234567890123456789"
            "0123456789012345678901234567890123456789012345678901234567890123456789"
            "\r\n"
            "0\r\n\r\n");
    }

    void
    run()
    {
        testStreambuf();
        testEncoder();
    }
};

BEAST_DEFINE_TESTSUITE(chunk_encode,http,beast);

}
}
