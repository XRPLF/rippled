//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/chunk_encode.hpp>

#include <beast/core/to_string.hpp>
#include <beast/unit_test/suite.hpp>

namespace beast {
namespace http {

class chunk_encode_test : public beast::unit_test::suite
{
public:
    struct final_chunk
    {
        std::string s;
        
        final_chunk() = default;

        explicit
        final_chunk(std::string s_)
            : s(std::move(s_))
        {
        }
    };

    static
    void
    encode1(std::string& s, final_chunk const& fc)
    {
        using boost::asio::buffer;
        if(! fc.s.empty())
            s.append(to_string(chunk_encode(
                false, buffer(fc.s.data(), fc.s.size()))));
        s.append(to_string(chunk_encode_final()));
    }

    static
    void
    encode1(std::string& s, std::string const& piece)
    {
        using boost::asio::buffer;
        s.append(to_string(chunk_encode(
            false, buffer(piece.data(), piece.size()))));
    }

    static
    inline
    void
    encode(std::string&)
    {
    }

    template<class Arg, class... Args>
    static
    void
    encode(std::string& s, Arg const& arg, Args const&... args)
    {
        encode1(s, arg);
        encode(s, args...);
    }

    template<class... Args>
    void
    check(std::string const& answer, Args const&... args)
    {
        std::string s;
        encode(s, args...);
        BEAST_EXPECT(s == answer);
    }

    void run() override
    {
        check(
            "0\r\n\r\n"
            "0\r\n\r\n",
            "", final_chunk{});

        check(
            "1\r\n"
            "*\r\n"
            "0\r\n\r\n",
            final_chunk("*"));

        check(
            "2\r\n"
            "**\r\n"
            "0\r\n\r\n",
            final_chunk("**"));

        check(
            "1\r\n"
            "*\r\n"
            "1\r\n"
            "*\r\n"
            "0\r\n\r\n",
            "*", final_chunk("*"));

        check(
            "5\r\n"
            "*****\r\n"
            "7\r\n"
            "*******\r\n"
            "0\r\n\r\n",
            "*****", final_chunk("*******"));

        check(
            "1\r\n"
            "*\r\n"
            "1\r\n"
            "*\r\n"
            "0\r\n\r\n",
            "*", "*", final_chunk{});

        check(
            "4\r\n"
            "****\r\n"
            "0\r\n\r\n",
            "****", final_chunk{});

        BEAST_EXPECT(to_string(chunk_encode(true,
            boost::asio::buffer("****", 4))) ==
                "4\r\n****\r\n0\r\n\r\n");
    }
};

BEAST_DEFINE_TESTSUITE(chunk_encode,http,beast);

} // http
} // beast
