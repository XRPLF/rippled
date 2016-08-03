//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/message_v1.hpp>

#include <beast/http/headers.hpp>
#include <beast/http/string_body.hpp>
#include <beast/unit_test/suite.hpp>
#include <beast/http/empty_body.hpp>

namespace beast {
namespace http {

class message_v1_test : public beast::unit_test::suite
{
public:
    void testFreeFunctions()
    {
        {
            request_v1<empty_body> m;
            m.method = "GET";
            m.url = "/";
            m.version = 11;
            m.headers.insert("Upgrade", "test");
            BEAST_EXPECT(! is_upgrade(m));

            prepare(m, connection::upgrade);
            BEAST_EXPECT(is_upgrade(m));
            BEAST_EXPECT(m.headers["Connection"] == "upgrade");

            m.version = 10;
            BEAST_EXPECT(! is_upgrade(m));
        }
    }

    void testPrepare()
    {
        request_v1<empty_body> m;
        m.version = 10;
        BEAST_EXPECT(! is_upgrade(m));
        m.headers.insert("Transfer-Encoding", "chunked");
        try
        {
            prepare(m);
            fail();
        }
        catch(std::exception const&)
        {
        }
        m.headers.erase("Transfer-Encoding");
        m.headers.insert("Content-Length", "0");
        try
        {
            prepare(m);
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }
        m.headers.erase("Content-Length");
        m.headers.insert("Connection", "keep-alive");
        try
        {
            prepare(m);
            fail();
        }
        catch(std::exception const&)
        {
            pass();
        }
        m.version = 11;
        m.headers.erase("Connection");
        m.headers.insert("Connection", "close");
        BEAST_EXPECT(! is_keep_alive(m));
    }

    void testSwap()
    {
        message_v1<false, string_body, headers> m1;
        message_v1<false, string_body, headers> m2;
        m1.status = 200;
        m1.version = 10;
        m1.body = "1";
        m1.headers.insert("h", "v");
        m2.status = 404;
        m2.reason = "OK";
        m2.body = "2";
        m2.version = 11;
        swap(m1, m2);
        BEAST_EXPECT(m1.status == 404);
        BEAST_EXPECT(m2.status == 200);
        BEAST_EXPECT(m1.reason == "OK");
        BEAST_EXPECT(m2.reason.empty());
        BEAST_EXPECT(m1.version == 11);
        BEAST_EXPECT(m2.version == 10);
        BEAST_EXPECT(m1.body == "2");
        BEAST_EXPECT(m2.body == "1");
        BEAST_EXPECT(! m1.headers.exists("h"));
        BEAST_EXPECT(m2.headers.exists("h"));
    }

    void run() override
    {
        testFreeFunctions();
        testPrepare();
        testSwap();
    }
};

BEAST_DEFINE_TESTSUITE(message_v1,http,beast);

} // http
} // beast
