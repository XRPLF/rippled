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
#include <ripple/json/json_reader.h>
#include <test/jtx.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/JSONRPCClient.h>
#include <ripple/core/DeadlineTimer.h>
#include <beast/core/to_string.hpp>
#include <beast/http.hpp>
#include <beast/test/yield_to.hpp>
#include <beast/websocket/detail/mask.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/algorithm/string/predicate.hpp>

namespace ripple {
namespace test {

class ServerStatus_test :
    public beast::unit_test::suite, public beast::test::enable_yield_to
{
    auto makeConfig(
        std::string const& proto,
        bool admin = true,
        bool credentials = false)
    {
        auto const section_name =
            boost::starts_with(proto, "h") ?  "port_rpc" : "port_ws";
        auto p = std::make_unique<Config>();
        setupConfigForUnitTests(*p);

        p->overwrite(section_name, "protocol", proto);
        if(! admin)
            p->overwrite(section_name, "admin", "");

        if(credentials)
        {
            (*p)[section_name].set("admin_password", "p");
            (*p)[section_name].set("admin_user", "u");
        }

        p->overwrite(
            boost::starts_with(proto, "h") ?  "port_ws" : "port_rpc",
            "protocol",
            boost::starts_with(proto, "h") ?  "ws" : "http");

        if(proto == "https")
        {
            // this port is here to allow the env to create its internal client,
            // which requires an http endpoint to talk to. In the connection
            // failure test, this endpoint should never be used
            (*p)["server"].append("port_alt");
            (*p)["port_alt"].set("ip", "127.0.0.1");
            (*p)["port_alt"].set("port", "8099");
            (*p)["port_alt"].set("protocol", "http");
            (*p)["port_alt"].set("admin", "127.0.0.1");
        }

        return p;
    }

    auto makeWSUpgrade(
        std::string const& host,
        uint16_t port)
    {
        using namespace boost::asio;
        using namespace beast::http;
        request<string_body> req;

        req.url = "/";
        req.version = 11;
        req.fields.insert("Host", host + ":" + to_string(port));
        req.fields.insert("User-Agent", "test");
        req.method = "GET";
        req.fields.insert("Upgrade", "websocket");
        beast::websocket::detail::maskgen maskgen;
        std::string key = beast::websocket::detail::make_sec_ws_key(maskgen);
        req.fields.insert("Sec-WebSocket-Key", key);
        req.fields.insert("Sec-WebSocket-Version", "13");
        prepare(req, connection::upgrade);
        return req;
    }

    auto makeHTTPRequest(
        std::string const& host,
        uint16_t port,
        std::string const& body)
    {
        using namespace boost::asio;
        using namespace beast::http;
        request<string_body> req;

        req.url = "/";
        req.version = 11;
        req.fields.insert("Host", host + ":" + to_string(port));
        req.fields.insert("User-Agent", "test");
        if(body.empty())
        {
            req.method = "GET";
        }
        else
        {
            req.method = "POST";
            req.fields.insert("Content-Type", "application/json; charset=UTF-8");
            req.body = body;
        }
        prepare(req);

        return req;
    }

    void
    doRequest(
        boost::asio::yield_context& yield,
        beast::http::request<beast::http::string_body> const& req,
        std::string const& host,
        uint16_t port,
        bool secure,
        beast::http::response<beast::http::string_body>& resp,
        boost::system::error_code& ec)
    {
        using namespace boost::asio;
        using namespace beast::http;
        io_service& ios = get_io_service();
        ip::tcp::resolver r{ios};
        beast::streambuf sb;

        auto it =
            r.async_resolve(
                ip::tcp::resolver::query{host, to_string(port)}, yield[ec]);
        if(ec)
            return;

        if(secure)
        {
            ssl::context ctx{ssl::context::sslv23};
            ctx.set_verify_mode(ssl::verify_none);
            ssl::stream<ip::tcp::socket> ss{ios, ctx};
            async_connect(ss.next_layer(), it, yield[ec]);
            if(ec)
                return;
            ss.async_handshake(ssl::stream_base::client, yield[ec]);
            if(ec)
                return;
            async_write(ss, req, yield[ec]);
            if(ec)
                return;
            async_read(ss, sb, resp, yield[ec]);
            if(ec)
                return;
        }
        else
        {
            ip::tcp::socket sock{ios};
            async_connect(sock, it, yield[ec]);
            if(ec)
                return;
            async_write(sock, req, yield[ec]);
            if(ec)
                return;
            async_read(sock, sb, resp, yield[ec]);
            if(ec)
                return;
        }

        return;
    }

    void
    doWSRequest(
        test::jtx::Env& env,
        boost::asio::yield_context& yield,
        bool secure,
        beast::http::response<beast::http::string_body>& resp,
        boost::system::error_code& ec)
    {
        auto const port = env.app().config()["port_ws"].
            get<std::uint16_t>("port");
        auto const ip = env.app().config()["port_ws"].
            get<std::string>("ip");
        doRequest(
            yield, makeWSUpgrade(*ip, *port), *ip, *port, secure, resp, ec);
        return;
    }

    void
    doHTTPRequest(
        test::jtx::Env& env,
        boost::asio::yield_context& yield,
        bool secure,
        beast::http::response<beast::http::string_body>& resp,
        boost::system::error_code& ec,
        std::string const& body = "")
    {
        auto const port = env.app().config()["port_rpc"].
            get<std::uint16_t>("port");
        auto const ip = env.app().config()["port_rpc"].
            get<std::string>("ip");
        doRequest(
            yield,
            makeHTTPRequest(*ip, *port, body),
            *ip,
            *port,
            secure,
            resp,
            ec);
        return;
    }

    auto makeAdminRequest(
        jtx::Env & env,
        std::string const& proto,
        std::string const& user,
        std::string const& password,
        bool subobject = false)
    {
        Json::Value jrr;

        Json::Value jp{Json::objectValue};
        if(! user.empty())
        {
            jp["admin_user"] = user;
            if(subobject)
            {
                //special case of bad password..passed as object
                Json::Value jpi{Json::objectValue};
                jpi["admin_password"] = password;
                jp["admin_password"] = jpi;
            }
            else
            {
                jp["admin_password"] = password;
            }
        }

        if(boost::starts_with(proto, "h"))
        {
            auto jrc = makeJSONRPCClient(env.app().config());
            jrr = jrc->invoke("ledger_accept", jp);
        }
        else
        {
            auto wsc = makeWSClient(env.app().config(), proto == "ws2");
            jrr = wsc->invoke("ledger_accept", jp);
        }

        return jrr;
    }

// ------------
//  Test Cases
// ------------
    void
    testWSClientToHttpServer(boost::asio::yield_context& yield)
    {
        testcase("WS client to http server fails");
        using namespace jtx;
        Env env(*this, []()
            {
                auto p = std::make_unique<Config>();
                setupConfigForUnitTests(*p);
                p->section("port_ws").set("protocol", "http,https");
                return p;
            }());

        //non-secure request
        {
            boost::system::error_code ec;
            beast::http::response<beast::http::string_body> resp;
            doWSRequest(env, yield, false, resp, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            BEAST_EXPECT(resp.status == 401);
        }

        //secure request
        {
            boost::system::error_code ec;
            beast::http::response<beast::http::string_body> resp;
            doWSRequest(env, yield, true, resp, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            BEAST_EXPECT(resp.status == 401);
        }
    }

    void
    testStatusRequest(boost::asio::yield_context& yield)
    {
        testcase("Status request");
        using namespace jtx;
        Env env(*this, []()
            {
                auto p = std::make_unique<Config>();
                setupConfigForUnitTests(*p);
                p->section("port_rpc").set("protocol", "ws2,wss2");
                p->section("port_ws").set("protocol", "http");
                return p;
            }());

        //non-secure request
        {
            boost::system::error_code ec;
            beast::http::response<beast::http::string_body> resp;
            doHTTPRequest(env, yield, false, resp, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            BEAST_EXPECT(resp.status == 200);
        }

        //secure request
        {
            boost::system::error_code ec;
            beast::http::response<beast::http::string_body> resp;
            doHTTPRequest(env, yield, true, resp, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            BEAST_EXPECT(resp.status == 200);
        }
    };

    void
    testTruncatedWSUpgrade(boost::asio::yield_context& yield)
    {
        testcase("Partial WS upgrade request");
        using namespace jtx;
        using namespace boost::asio;
        using namespace beast::http;
        Env env(*this, []()
            {
                auto p = std::make_unique<Config>();
                setupConfigForUnitTests(*p);
                p->section("port_ws").set("protocol", "ws2");
                return p;
            }());

        auto const port = env.app().config()["port_ws"].
            get<std::uint16_t>("port");
        auto const ip = env.app().config()["port_ws"].
            get<std::string>("ip");

        boost::system::error_code ec;
        response<string_body> resp;
        auto req = makeWSUpgrade(*ip, *port);

        //truncate the request message to near the value of the version header
        auto req_string = boost::lexical_cast<std::string>(req);
        req_string.erase(req_string.find_last_of("13"), std::string::npos);

        io_service& ios = get_io_service();
        ip::tcp::resolver r{ios};
        beast::streambuf sb;

        auto it =
            r.async_resolve(
                ip::tcp::resolver::query{*ip, to_string(*port)}, yield[ec]);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;

        ip::tcp::socket sock{ios};
        async_connect(sock, it, yield[ec]);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        async_write(sock, boost::asio::buffer(req_string), yield[ec]);
        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        // since we've sent an incomplete request, the server will
        // keep trying to read until it gives up (by timeout)
        async_read(sock, sb, resp, yield[ec]);
        BEAST_EXPECT(ec);
    };

    void
    testCantConnect(
        std::string const& client_protocol,
        std::string const& server_protocol,
        boost::asio::yield_context& yield)
    {
        testcase << "Connect fails: " << client_protocol << " client to " <<
            server_protocol << " server";
        using namespace jtx;
        Env env {*this, makeConfig(server_protocol)};

        beast::http::response<beast::http::string_body> resp;
        boost::system::error_code ec;
        // The essence of this test is to have a client and server configured
        // out-of-phase with respect to ssl (secure client and insecure server
        // or vice-versa) - as such, here is a config to pass to
        // WSClient/JSONRPCClient that configures it for a protocol that
        // doesn't match the actual server
        auto cfg = makeConfig(client_protocol);
        if(boost::starts_with(client_protocol, "h"))
        {
                doHTTPRequest(
                    env,
                    yield,
                    client_protocol == "https",
                    resp,
                    ec);
                BEAST_EXPECT(ec);
        }
        else
        {
                doWSRequest(
                    env,
                    yield,
                    client_protocol == "wss" || client_protocol == "wss2",
                    resp,
                    ec);
                BEAST_EXPECT(ec);
        }
    }

    void
    testAdminRequest(std::string const& proto, bool admin, bool credentials)
    {
        testcase << "Admin request over " << proto <<
            ", config " << (admin ? "enabled" : "disabled") <<
            ", credentials " << (credentials ? "" : "not ") << "set";
        using namespace jtx;
        Env env {*this, makeConfig(proto, admin, credentials)};

        auto const user = env.app().config()
            [boost::starts_with(proto, "h") ? "port_rpc" : "port_ws"].
            get<std::string>("admin_user");

        auto const password = env.app().config()
            [boost::starts_with(proto, "h") ? "port_rpc" : "port_ws"].
            get<std::string>("admin_password");

        Json::Value jrr;

        // the set of checks we do are different depending
        // on how the admin config options are set

        if(admin && credentials)
        {
            //1 - FAILS with wrong pass
            jrr = makeAdminRequest(env, proto, *user, *password + "_")[jss::result];
            BEAST_EXPECT(jrr["error"] ==
                boost::starts_with(proto, "h") ? "noPermission" : "forbidden");
            BEAST_EXPECT(jrr["error_message"] ==
                boost::starts_with(proto, "h") ?
                "You don't have permission for this command." :
                "Bad credentials.");

            //2 - FAILS with password in an object
            jrr = makeAdminRequest(env, proto, *user, *password, true)[jss::result];
            BEAST_EXPECT(jrr["error"] ==
                boost::starts_with(proto, "h") ? "noPermission" : "forbidden");
            BEAST_EXPECT(jrr["error_message"] ==
                boost::starts_with(proto, "h") ?
                "You don't have permission for this command." :
                "Bad credentials.");

            //3 - FAILS with wrong user
            jrr = makeAdminRequest(env, proto, *user + "_", *password)[jss::result];
            BEAST_EXPECT(jrr["error"] ==
                boost::starts_with(proto, "h") ? "noPermission" : "forbidden");
            BEAST_EXPECT(jrr["error_message"] ==
                boost::starts_with(proto, "h") ?
                "You don't have permission for this command." :
                "Bad credentials.");

            //4 - FAILS no credentials
            jrr = makeAdminRequest(env, proto, "", "")[jss::result];
            BEAST_EXPECT(jrr["error"] ==
                boost::starts_with(proto, "h") ? "noPermission" : "forbidden");
            BEAST_EXPECT(jrr["error_message"] ==
                boost::starts_with(proto, "h") ?
                "You don't have permission for this command." :
                "Bad credentials.");

            //5 - SUCCEEDS with proper credentials
            jrr = makeAdminRequest(env, proto, *user, *password)[jss::result];
            BEAST_EXPECT(jrr["status"] == "success");
        }
        else if(admin)
        {
            //1 - SUCCEEDS with proper credentials
            jrr = makeAdminRequest(env, proto, "u", "p")[jss::result];
            BEAST_EXPECT(jrr["status"] == "success");

            //2 - SUCCEEDS without proper credentials
            jrr = makeAdminRequest(env, proto, "", "")[jss::result];
            BEAST_EXPECT(jrr["status"] == "success");
        }
        else
        {
            //1 - FAILS - admin disabled
            jrr = makeAdminRequest(env, proto, "", "")[jss::result];
            BEAST_EXPECT(jrr["error"] ==
                boost::starts_with(proto, "h") ? "noPermission" : "forbidden");
            BEAST_EXPECT(jrr["error_message"] ==
                boost::starts_with(proto, "h") ?
                "You don't have permission for this command." :
                "Bad credentials.");
        }
    }

public:
    void
    run()
    {
        yield_to([&](boost::asio::yield_context& yield)
        {
            testWSClientToHttpServer(yield);
            testStatusRequest(yield);
            testTruncatedWSUpgrade(yield);
            // these are secure/insecure protocol pairs, i.e. for
            // each item, the second value is the secure or insecure equivalent
            testCantConnect("ws", "wss", yield);
            testCantConnect("ws2", "wss2", yield);
            testCantConnect("http", "https", yield);
            //THIS HANGS - testCantConnect("wss", "ws", yield);
            testCantConnect("wss2", "ws2", yield);
            testCantConnect("https", "http", yield);
        });

        for (auto it : {"http", "ws", "ws2"})
        {
            testAdminRequest(it, true, true);
            testAdminRequest(it, true, false);
            testAdminRequest(it, false, false);
        }
    };
};

BEAST_DEFINE_TESTSUITE(ServerStatus, server, ripple);

} // test
} // ripple

