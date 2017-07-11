//
// Copyright (c) 2013-2017 Vinnie Falco (vinnie dot falco at gmail dot com)
//
// Distributed under the Boost Software License, Version 1.0. (See accompanying
// file LICENSE_1_0.txt or copy at http://www.boost.org/LICENSE_1_0.txt)
//

#include "example/common/detect_ssl.hpp"

#include <beast/core/flat_buffer.hpp>
#include <beast/core/ostream.hpp>
#include <beast/test/pipe_stream.hpp>
#include <beast/test/yield_to.hpp>
#include <beast/unit_test/suite.hpp>

namespace beast {
namespace http {

class doc_core_samples_test
    : public beast::unit_test::suite
    , public beast::test::enable_yield_to
{
public:
    void
    testDetect()
    {
        char buf[4];
        buf[0] = 0x16;
        buf[1] = 0;
        buf[2] = 0;
        buf[3] = 0;
        BEAST_EXPECT(boost::indeterminate(is_ssl_handshake(
            boost::asio::buffer(buf, 0))));
        BEAST_EXPECT(boost::indeterminate(is_ssl_handshake(
            boost::asio::buffer(buf, 1))));
        BEAST_EXPECT(boost::indeterminate(is_ssl_handshake(
            boost::asio::buffer(buf, 2))));
        BEAST_EXPECT(boost::indeterminate(is_ssl_handshake(
            boost::asio::buffer(buf, 3))));
        BEAST_EXPECT(is_ssl_handshake(
            boost::asio::buffer(buf, 4)));
        buf[0] = 0;
        BEAST_EXPECT(! is_ssl_handshake(
            boost::asio::buffer(buf, 1)));
    }

    void
    testRead()
    {
        {
            test::pipe p{ios_};
            ostream(p.server.buffer) <<
                "\x16***";
            error_code ec;
            flat_buffer b;
            auto const result = detect_ssl(p.server, b, ec);
            BEAST_EXPECTS(! ec, ec.message());
            BEAST_EXPECT(result);
        }
        yield_to(
            [&](yield_context yield)
            {
                test::pipe p{ios_};
                ostream(p.server.buffer) <<
                    "\x16***";
                error_code ec;
                flat_buffer b;
                auto const result = async_detect_ssl(p.server, b, yield[ec]);
                BEAST_EXPECTS(! ec, ec.message());
                BEAST_EXPECT(result);
            });
    }

    void
    run()
    {
        testDetect();
        testRead();
    }
};

BEAST_DEFINE_TESTSUITE(doc_core_samples,core,beast);

} // http
} // beast
