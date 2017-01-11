//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/streambuf_body.hpp>

#include <beast/core/to_string.hpp>
#include <beast/http/fields.hpp>
#include <beast/http/parser_v1.hpp>
#include <beast/http/read.hpp>
#include <beast/http/write.hpp>
#include <beast/test/string_stream.hpp>
#include <beast/unit_test/suite.hpp>
#include <boost/lexical_cast.hpp>

namespace beast {
namespace http {

class streambuf_body_test : public beast::unit_test::suite
{
    boost::asio::io_service ios_;

public:
    void run() override
    {
        std::string const s =
            "HTTP/1.1 200 OK\r\n"
            "Server: test\r\n"
            "Content-Length: 3\r\n"
            "\r\n"
            "xyz";
        test::string_stream ss(ios_, s);
        parser_v1<false, streambuf_body, fields> p;
        streambuf sb;
        parse(ss, sb, p);
        BEAST_EXPECT(to_string(p.get().body.data()) == "xyz");
        BEAST_EXPECT(boost::lexical_cast<std::string>(p.get()) == s);
    }
};

BEAST_DEFINE_TESTSUITE(streambuf_body,http,beast);

} // http
} // beast
