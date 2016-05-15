//
// Copyright (c) 2013-2016 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

// Test that header file is self-contained.
#include <beast/http/message_v1.hpp>

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
            expect(! is_upgrade(m));

            prepare(m, connection::upgrade);
            expect(is_upgrade(m));
            expect(m.headers["Connection"] == "upgrade");

            m.version = 10;
            expect(! is_upgrade(m));
        }
    }

    void testPrepare()
    {
        request_v1<empty_body> m;
        m.version = 10;
        expect(! is_upgrade(m));
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
        expect(! is_keep_alive(m));
    }

    void run() override
    {
        testFreeFunctions();
        testPrepare();
    }
};

BEAST_DEFINE_TESTSUITE(message_v1,http,beast);

} // http
} // beast
