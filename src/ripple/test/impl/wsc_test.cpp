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
#include <ripple/json/json_reader.h>
#include <ripple/test/jtx.h>
#include <beast/asio/streambuf.h>
#include <beast/http/parser.h>
#include <beast/unit_test/suite.h>
#include <boost/asio.hpp>
#include <boost/optional.hpp>
#include <array>
#include <chrono>
#include <memory>
#include <sstream>
#include <thread>
#include <type_traits>

namespace ripple {
namespace test {

#if 0
//------------------------------------------------------------------------------

class wsc_test : public beast::unit_test::suite
{
public:
    template <class ConstBufferSequence>
    std::string
    buffer_string (ConstBufferSequence const& b)
    {
        using namespace boost::asio;
        std::string s;
        s.resize(buffer_size(b));
        buffer_copy(buffer(&s[0], s.size()), b);
        return s;
    }

    void run() override
    {
        using namespace jtx;
        auto cfg = std::make_unique<Config>();
        setupConfigForUnitTests(*cfg);
        Env env(*this,
            []
            {
                auto cfg = std::make_unique<Config>();
                setupConfigForUnitTests(*cfg);
                (*cfg)["server"].append("port_ws");
                (*cfg)["port_ws"].set("port", "6007");
                (*cfg)["port_ws"].set("ip", "127.0.0.1");
                (*cfg)["port_ws"].set("protocol", "ws");
                return cfg;
            }());

        using namespace boost::asio;
        io_service ios;
        boost::optional<io_service::work> work(ios);
        std::thread t([&]{ ios.run(); });
        {
            ip::tcp::socket sock(ios);
            wsock<ip::tcp::socket&> ws(sock);
            sock.connect({
                ip::address::from_string("127.0.0.1"), 6007});
            write(sock, buffer(
                "GET / HTTP/1.1\r\n"
                "Host: server.example.com\r\n"
                "Upgrade: websocket\r\n"
                "Connection: Upgrade\r\n"
                "Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==\r\n"
                "Origin: http://example.com\r\n"
                "Sec-WebSocket-Protocol: chat, superchat\r\n"
                "Sec-WebSocket-Version: 13\r\n"
                "\r\n"
                ));
            streambuf sb;
            boost::asio::read_until(sock, sb, "\r\n\r\n");
            using namespace beast;
            http::body b;
            http::message m;
            http::parser p(m, b, false);
            auto const result = p.write(sb.data());
            if (result.first || ! p.complete())
                throw std::runtime_error(result.first.message());
            sb.consume(result.second);

            ws.write(buffer(
                R"( { "id" : 1, "command" : "server_info" }\n)"));
            ws.read(sb);
            Json::Reader jr;
            Json::Value jv;
            jr.parse(buffer_string(sb.data()), jv);
            sb.consume(sb.size());
            //log << pretty(jv);

            auto const gw = Account("gateway");
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const USD = gw["USD"];
            env.fund(XRP(10000), "alice", bob, gw);
            env.trust(USD(10000), "alice", bob);
            env(pay(gw, alice, USD(1000)));
            env(offer(alice, USD(10), XRP(1000)));

            Json::Value jp;
            jp["id"] = 2;
            jp["command"] = "ripple_path_find";
            jp["source_account"] = toBase58(bob.id());
            jp["source_currencies"][0u]["currency"] = "XRP";
            jp["destination_account"] = toBase58(bob.id());
            jp["destination_amount"] = USD(1).value().getJson(0);
            ws.write(buffer(to_string(jp)));
            ws.read(sb);
            jr.parse(buffer_string(sb.data()), jv);
            log << pretty(jv);
        }
        work = boost::none;
        t.join();
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(wsc,test,ripple);
#endif

//------------------------------------------------------------------------------

class wsc2_test : public beast::unit_test::suite
{
public:
    void run() override
    {
        using namespace jtx;
        Env env(*this);
        pass();
    }
};

BEAST_DEFINE_TESTSUITE(wsc2,test,ripple);

} // test
} // ripple
