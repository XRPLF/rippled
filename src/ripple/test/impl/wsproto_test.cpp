//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2015 Ripple Labs Inc.

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

#include <BeastConfig.h>
#include <ripple/server/Port.h>
#include <ripple/test/jtx.h>
#include <beast/wsproto/http.h>
#include <beast/asio/placeholders.h>
#include <beast/unit_test/suite.h>
#include <beast/wsproto.h>
#include <stdexcept>
#include <type_traits>

#include <beast/utility/type_name.h>

namespace ripple {
namespace test {

/*
    Operations on http message:

    read from socket, parse

    write
        serialize entire message into a string
        serialize entire message into a streambuf

    calculate Content-Length and Transfer-Coding fields
        Include them in serialization?

    http_message::prepare()
        - Prepares the body and everything else, this
          sets Content-Length and Transfer-Coding
        - Undefined behavior if called twice
        - Needs access to the body through a uniform API,
          or else the body needs to hook the call chain
          to do body-specific preparation


    - A function returns http response with a simple string body
        Response is serialized into a streambuf and sent in one write

    How do we set Content-Length (and Transfer-Coding, Content-Type)?

        - Do it during the write()
            But callers can't inspect it
        - Add a prepare() interface method
            But this allows an invalid state (before prepare)
        - Do it on construction to "fuse" the body to the headers

*/

/* string body, simple return value */
template<class Stream, class Body>
static
beast::wsproto::http_response<beast::wsproto::string_body>
request(Stream& stream,
    beast::wsproto::http_request<Body> const& m)
{
    auto const err =
        [&](auto&& text)
        {
            int const status = 400;
            beast::wsproto::http_headers h;
            return beast::wsproto::prepare_response<beast::wsproto::string_body>(
                status, beast::wsproto::http_reason(status), h, text);
        };
    if(m.version != "1.1")
        return err("Bad HTTP version");
    if(m.method != beast::http::method_t::http_get)
        return err("Bad HTTP method");
    return err("OK");
}

//------------------------------------------------------------------------------

static
boost::asio::ip::tcp::endpoint
getEndpoint(BasicConfig const& cfg,
    std::string const& protocol)
{
    auto& log = std::cerr;
    ParsedPort common;
    parse_Port (common, cfg["server"], log);
    for (auto const& name : cfg.section("server").values())
    {
        if (! cfg.exists(name))
            continue;
        ParsedPort pp;
        parse_Port(pp, cfg[name], log);
        if(pp.protocol.count(protocol) == 0)
            continue;
        using boost::asio::ip::address_v4;
        if(*pp.ip == address_v4{0x00000000})
            *pp.ip = address_v4{0x7f000001};
        return { *pp.ip, *pp.port };
    }
    throw std::runtime_error("protocol not found");
}

template<class Body>
std::string
to_string(beast::wsproto::http_response<Body> const& m)
{
    beast::asio::streambuf b;
    write(b, m);
    return beast::wsproto::detail::buffersToString(b.data());
}

class wsproto_test : public beast::unit_test::suite
{
public:
    void testHandshake(std::string const& ps)
    {
        using namespace jtx;
        using socket_type = boost::asio::ip::tcp::socket;
        Env env(*this);
        boost::asio::io_service ios;
        boost::asio::ip::tcp::socket socket(ios);
        socket.connect(getEndpoint(env.app().config(), ps));

        beast::wsproto::http_request<beast::wsproto::empty_body> m;
        log << to_string(request(socket, m));
    }

    void
    on_write(beast::wsproto::error_code const& ec)
    {
    }

    template<class T>
    struct U
    {
        using ref = T&;
    };

    // Test compilation of various instantiations
    void
    testTypes()
    {
        using namespace boost::asio;
        io_service ios;
        {
            beast::wsproto::stream<ip::tcp::socket> ws(ios);
        }
        {
            ip::tcp::socket sock(ios);
            beast::wsproto::stream<ip::tcp::socket> ws(std::move(sock));
            ws.async_write(beast::wsproto::opcode::text, false, null_buffers{},
                std::bind(&wsproto_test::on_write, this,
                    beast::asio::placeholders::error));
        }
        {
            ip::tcp::socket sock(ios);
            beast::wsproto::stream<ip::tcp::socket&> ws(sock);
            ws.async_write(beast::wsproto::opcode::text, false,
                null_buffers{}, std::bind(&wsproto_test::on_write,
                    this, beast::asio::placeholders::error));
            std::vector<mutable_buffer> v;
            ws.async_read_some(v,
                [](boost::system::error_code, std::size_t)
                {
                });
            beast::asio::streambuf sb;
            auto const b = sb.prepare(64);
            for(auto iter = b.end(); iter != b.begin(); --iter)
            {
                std::prev(iter);
            }
            beast::wsproto::async_read_msg(ws, sb,
                [](boost::system::error_code)
                {
                });
        }
        {
            struct T { };
            log << beast::type_name<U<T>::ref>();
        }
    }

    void run() override
    {
        testHandshake("ws");
        testTypes();
        pass();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(wsproto, test, ripple);

} // test
} // ripple

//------------------------------------------------------------------------------
/*

HTTP message use cases

Read side:
    - Parsing

Write side:
    - Simple send with a small string body


*/
