//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

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
#include <ripple/rpc/ServerHandler.h>
#include <ripple/test/jtx.h>
#include <beast/core/to_string.hpp>
#include <beast/http.hpp>
#include <beast/websocket/detail/mask.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>

namespace ripple {
namespace test {

class ServerStatus_test : public beast::unit_test::suite
{
public:
    void
    testUnauthorizedRequest()
    {
        using namespace jtx;
        Env env(*this, []()
            {
                auto p = std::make_unique<Config>();
                setupConfigForUnitTests(*p);
                p->section("port_ws").set("protocol", "http,https");
                return p;
            }());
        auto const port = env.app().config()["port_ws"].
            get<std::uint16_t>("port");
        if(! expect(port))
            return;

        using namespace boost::asio;
        using namespace beast::http;
        io_service ios;
        ip::tcp::resolver r{ios};
        beast::streambuf sb;
        response_v1<string_body> resp;
        boost::system::error_code ec;

        beast::websocket::detail::maskgen maskgen;
        request_v1<empty_body> req;
        req.url = "/";
        req.version = 11;
        req.method = "GET";
        req.headers.insert("Host", "127.0.0.1:" + to_string(*port));
        req.headers.insert("Upgrade", "websocket");
        std::string key = beast::websocket::detail::make_sec_ws_key(maskgen);
        req.headers.insert("Sec-WebSocket-Key", key);
        req.headers.insert("Sec-WebSocket-Version", "13");
        prepare(req, connection::upgrade);

        // non secure socket
        {
            ip::tcp::socket sock{ios};
            connect(sock, r.resolve(
                ip::tcp::resolver::query{"127.0.0.1", to_string(*port)}), ec);
            if(! expect(! ec))
                return;
            write(sock, req, ec);
            if(! expect(! ec))
                return;
            read(sock, sb, resp, ec);
            if(! expect(! ec))
                return;
            expect(resp.status == 401);
        }

        // secure socket
        {
            ssl::context ctx{ssl::context::sslv23};
            ctx.set_verify_mode(ssl::verify_none);
            ssl::stream<ip::tcp::socket> ss{ios, ctx};
            connect(ss.next_layer(), r.resolve(
                ip::tcp::resolver::query{"127.0.0.1", to_string(*port)}));
            ss.handshake(ssl::stream_base::client, ec);
            if(! expect(! ec))
                return;
            write(ss, req, ec);
            if(! expect(! ec))
                return;
            read(ss, sb, resp, ec);
            if(! expect(! ec))
                return;
            expect(resp.status == 401);
        }
    }

    void
    testStatusRequest()
    {
        using namespace jtx;
        Env env(*this, []()
            {
                auto p = std::make_unique<Config>();
                setupConfigForUnitTests(*p);
                p->section("port_ws").set("protocol", "ws2,wss2");
                return p;
            }());
        auto const port = env.app().config()["port_ws"].
            get<std::uint16_t>("port");
        if(! expect(port))
            return;

        using namespace boost::asio;
        using namespace beast::http;
        io_service ios;
        ip::tcp::resolver r{ios};
        beast::streambuf sb;
        response_v1<string_body> resp;
        boost::system::error_code ec;

        request_v1<empty_body> req;
        req.url = "/";
        req.version = 11;
        req.method = "GET";
        req.headers.insert("Host", "127.0.0.1:" + to_string(*port));
        req.headers.insert("User-Agent", "test");
        prepare(req);

        // Request the status page on a non secure socket
        {
            ip::tcp::socket sock{ios};
            connect(sock, r.resolve(
                ip::tcp::resolver::query{"127.0.0.1", to_string(*port)}), ec);
            if(! expect(! ec))
                return;
            write(sock, req, ec);
            if(! expect(! ec))
                return;
            read(sock, sb, resp, ec);
            if(! expect(! ec))
                return;
            expect(resp.status == 200);
        }

        // Request the status page on a secure socket
        {
            ssl::context ctx{ssl::context::sslv23};
            ctx.set_verify_mode(ssl::verify_none);
            ssl::stream<ip::tcp::socket> ss{ios, ctx};
            connect(ss.next_layer(), r.resolve(
                ip::tcp::resolver::query{"127.0.0.1", to_string(*port)}));
            ss.handshake(ssl::stream_base::client, ec);
            if(! expect(! ec))
                return;
            write(ss, req, ec);
            if(! expect(! ec))
                return;
            read(ss, sb, resp, ec);
            if(! expect(! ec))
                return;
            expect(resp.status == 200);
        }
    };

    void
    run()
    {
        testUnauthorizedRequest();
        testStatusRequest();
    };
};

BEAST_DEFINE_TESTSUITE(ServerStatus, server, ripple);

} // test
} // ripple

