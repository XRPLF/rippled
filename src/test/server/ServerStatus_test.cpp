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
#include <ripple/app/misc/LoadFeeTrack.h>
#include <test/jtx.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/JSONRPCClient.h>
#include <ripple/app/misc/NetworkOPs.h>
#include <ripple/app/ledger/LedgerMaster.h>
#include <boost/beast/http.hpp>
#include <beast/test/yield_to.hpp>
#include <boost/beast/websocket/detail/mask.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/asio.hpp>
#include <boost/asio/ssl.hpp>
#include <boost/algorithm/string/predicate.hpp>
#include <regex>

namespace ripple {
namespace test {

class ServerStatus_test :
    public beast::unit_test::suite, public beast::test::enable_yield_to
{
    class myFields : public boost::beast::http::fields {};

    auto makeConfig(
        std::string const& proto,
        bool admin = true,
        bool credentials = false)
    {
        auto const section_name =
            boost::starts_with(proto, "h") ?  "port_rpc" : "port_ws";
        auto p = jtx::envconfig();

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
        using namespace boost::beast::http;
        request<string_body> req;

        req.target("/");
        req.version(11);
        req.insert("Host", host + ":" + std::to_string(port));
        req.insert("User-Agent", "test");
        req.method(boost::beast::http::verb::get);
        req.insert("Upgrade", "websocket");
        boost::beast::websocket::detail::maskgen maskgen;
        boost::beast::websocket::detail::sec_ws_key_type key;
        boost::beast::websocket::detail::make_sec_ws_key(key, maskgen);
        req.insert("Sec-WebSocket-Key", key);
        req.insert("Sec-WebSocket-Version", "13");
        req.insert(boost::beast::http::field::connection, "upgrade");
        return req;
    }

    auto makeHTTPRequest(
        std::string const& host,
        uint16_t port,
        std::string const& body,
        myFields const& fields)
    {
        using namespace boost::asio;
        using namespace boost::beast::http;
        request<string_body> req;

        req.target("/");
        req.version(11);
        for(auto const& f : fields)
            req.insert(f.name(), f.value());
        req.insert("Host", host + ":" + std::to_string(port));
        req.insert("User-Agent", "test");
        if(body.empty())
        {
            req.method(boost::beast::http::verb::get);
        }
        else
        {
            req.method(boost::beast::http::verb::post);
            req.insert("Content-Type", "application/json; charset=UTF-8");
            req.body() = body;
        }
        req.prepare_payload();

        return req;
    }

    void
    doRequest(
        boost::asio::yield_context& yield,
        boost::beast::http::request<boost::beast::http::string_body>&& req,
        std::string const& host,
        uint16_t port,
        bool secure,
        boost::beast::http::response<boost::beast::http::string_body>& resp,
        boost::system::error_code& ec)
    {
        using namespace boost::asio;
        using namespace boost::beast::http;
        io_service& ios = get_io_service();
        ip::tcp::resolver r{ios};
        boost::beast::multi_buffer sb;

        auto it =
            r.async_resolve(
                ip::tcp::resolver::query{host, std::to_string(port)}, yield[ec]);
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
            boost::beast::http::async_write(ss, req, yield[ec]);
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
            boost::beast::http::async_write(sock, req, yield[ec]);
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
        boost::beast::http::response<boost::beast::http::string_body>& resp,
        boost::system::error_code& ec)
    {
        auto const port = env.app().config()["port_ws"].
            get<std::uint16_t>("port");
        auto ip = env.app().config()["port_ws"].
            get<std::string>("ip");
        doRequest(
            yield,
            makeWSUpgrade(*ip, *port),
            *ip,
            *port,
            secure,
            resp,
            ec);
        return;
    }

    void
    doHTTPRequest(
        test::jtx::Env& env,
        boost::asio::yield_context& yield,
        bool secure,
        boost::beast::http::response<boost::beast::http::string_body>& resp,
        boost::system::error_code& ec,
        std::string const& body = "",
        myFields const& fields = {})
    {
        auto const port = env.app().config()["port_rpc"].
            get<std::uint16_t>("port");
        auto const ip = env.app().config()["port_rpc"].
            get<std::string>("ip");
        doRequest(
            yield,
            makeHTTPRequest(*ip, *port, body, fields),
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

        Json::Value jp = Json::objectValue;
        if(! user.empty())
        {
            jp["admin_user"] = user;
            if(subobject)
            {
                //special case of bad password..passed as object
                Json::Value jpi = Json::objectValue;
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
    testAdminRequest(std::string const& proto, bool admin, bool credentials)
    {
        testcase << "Admin request over " << proto <<
            ", config " << (admin ? "enabled" : "disabled") <<
            ", credentials " << (credentials ? "" : "not ") << "set";
        using namespace jtx;
        Env env {*this, makeConfig(proto, admin, credentials)};

        Json::Value jrr;
        auto const proto_ws = boost::starts_with(proto, "w");

        // the set of checks we do are different depending
        // on how the admin config options are set

        if(admin && credentials)
        {
            auto const user = env.app().config()
                [proto_ws ? "port_ws" : "port_rpc"].
                get<std::string>("admin_user");

            auto const password = env.app().config()
                [proto_ws ? "port_ws" : "port_rpc"].
                get<std::string>("admin_password");

            //1 - FAILS with wrong pass
            jrr = makeAdminRequest(env, proto, *user, *password + "_")
                [jss::result];
            BEAST_EXPECT(jrr["error"] == proto_ws ? "forbidden" : "noPermission");
            BEAST_EXPECT(jrr["error_message"] ==
                proto_ws ?
                "Bad credentials." :
                "You don't have permission for this command.");

            //2 - FAILS with password in an object
            jrr = makeAdminRequest(env, proto, *user, *password, true)[jss::result];
            BEAST_EXPECT(jrr["error"] == proto_ws ? "forbidden" : "noPermission");
            BEAST_EXPECT(jrr["error_message"] ==
                proto_ws ?
                "Bad credentials." :
                "You don't have permission for this command.");

            //3 - FAILS with wrong user
            jrr = makeAdminRequest(env, proto, *user + "_", *password)[jss::result];
            BEAST_EXPECT(jrr["error"] == proto_ws ? "forbidden" : "noPermission");
            BEAST_EXPECT(jrr["error_message"] ==
                proto_ws ?
                "Bad credentials." :
                "You don't have permission for this command.");

            //4 - FAILS no credentials
            jrr = makeAdminRequest(env, proto, "", "")[jss::result];
            BEAST_EXPECT(jrr["error"] == proto_ws ? "forbidden" : "noPermission");
            BEAST_EXPECT(jrr["error_message"] ==
                proto_ws ?
                "Bad credentials." :
                "You don't have permission for this command.");

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
            BEAST_EXPECT(jrr["error"] == proto_ws ? "forbidden" : "noPermission");
            BEAST_EXPECT(jrr["error_message"] ==
                proto_ws ?
                "Bad credentials." :
                "You don't have permission for this command.");
        }
    }

    void
    testWSClientToHttpServer(boost::asio::yield_context& yield)
    {
        testcase("WS client to http server fails");
        using namespace jtx;
        Env env {*this, envconfig([](std::unique_ptr<Config> cfg)
            {
                cfg->section("port_ws").set("protocol", "http,https");
                return cfg;
            })};

        //non-secure request
        {
            boost::system::error_code ec;
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doWSRequest(env, yield, false, resp, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            BEAST_EXPECT(resp.result() == boost::beast::http::status::unauthorized);
        }

        //secure request
        {
            boost::system::error_code ec;
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doWSRequest(env, yield, true, resp, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            BEAST_EXPECT(resp.result() == boost::beast::http::status::unauthorized);
        }
    }

    void
    testStatusRequest(boost::asio::yield_context& yield)
    {
        testcase("Status request");
        using namespace jtx;
        Env env {*this, envconfig([](std::unique_ptr<Config> cfg)
            {
                cfg->section("port_rpc").set("protocol", "ws2,wss2");
                cfg->section("port_ws").set("protocol", "http");
                return cfg;
            })};

        //non-secure request
        {
            boost::system::error_code ec;
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doHTTPRequest(env, yield, false, resp, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            BEAST_EXPECT(resp.result() == boost::beast::http::status::ok);
        }

        //secure request
        {
            boost::system::error_code ec;
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doHTTPRequest(env, yield, true, resp, ec);
            if(! BEAST_EXPECTS(! ec, ec.message()))
                return;
            BEAST_EXPECT(resp.result() == boost::beast::http::status::ok);
        }
    };

    void
    testTruncatedWSUpgrade(boost::asio::yield_context& yield)
    {
        testcase("Partial WS upgrade request");
        using namespace jtx;
        using namespace boost::asio;
        using namespace boost::beast::http;
        Env env {*this, envconfig([](std::unique_ptr<Config> cfg)
            {
                cfg->section("port_ws").set("protocol", "ws2");
                return cfg;
            })};

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
        boost::beast::multi_buffer sb;

        auto it =
            r.async_resolve(
                ip::tcp::resolver::query{*ip, std::to_string(*port)}, yield[ec]);
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
        // The essence of this test is to have a client and server configured
        // out-of-phase with respect to ssl (secure client and insecure server
        // or vice-versa)
        testcase << "Connect fails: " << client_protocol << " client to " <<
            server_protocol << " server";
        using namespace jtx;
        Env env {*this, makeConfig(server_protocol)};

        boost::beast::http::response<boost::beast::http::string_body> resp;
        boost::system::error_code ec;
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
    testAuth(bool secure, boost::asio::yield_context& yield)
    {
        testcase << "Server with authorization, " <<
            (secure ? "secure" : "non-secure");

        using namespace test::jtx;
        Env env {*this, envconfig([secure](std::unique_ptr<Config> cfg) {
            (*cfg)["port_rpc"].set("user","me");
            (*cfg)["port_rpc"].set("password","secret");
            (*cfg)["port_rpc"].set("protocol", secure ? "https" : "http");
            if (secure)
                (*cfg)["port_ws"].set("protocol","http,ws");
            return cfg;
        })};

        Json::Value jr;
        jr[jss::method] = "server_info";
        boost::beast::http::response<boost::beast::http::string_body> resp;
        boost::system::error_code ec;
        doHTTPRequest(env, yield, secure, resp, ec, to_string(jr));
        BEAST_EXPECT(resp.result() == boost::beast::http::status::forbidden);

        myFields auth;
        auth.insert("Authorization", "");
        doHTTPRequest(env, yield, secure, resp, ec, to_string(jr), auth);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::forbidden);

        auth.set("Authorization", "Basic NOT-VALID");
        doHTTPRequest(env, yield, secure, resp, ec, to_string(jr), auth);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::forbidden);

        auth.set("Authorization", "Basic " + boost::beast::detail::base64_encode("me:badpass"));
        doHTTPRequest(env, yield, secure, resp, ec, to_string(jr), auth);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::forbidden);

        auto const user = env.app().config().section("port_rpc").
            get<std::string>("user").value();
        auto const pass = env.app().config().section("port_rpc").
            get<std::string>("password").value();

        // try with the correct user/pass, but not encoded
        auth.set("Authorization", "Basic " + user + ":" + pass);
        doHTTPRequest(env, yield, secure, resp, ec, to_string(jr), auth);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::forbidden);

        // finally if we use the correct user/pass encoded, we should get a 200
        auth.set("Authorization", "Basic " +
            boost::beast::detail::base64_encode(user + ":" + pass));
        doHTTPRequest(env, yield, secure, resp, ec, to_string(jr), auth);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::ok);
        BEAST_EXPECT(! resp.body().empty());
    }

    void
    testLimit(boost::asio::yield_context& yield, int limit)
    {
        testcase << "Server with connection limit of " << limit;

        using namespace test::jtx;
        using namespace boost::asio;
        using namespace boost::beast::http;
        Env env {*this, envconfig([&](std::unique_ptr<Config> cfg) {
            (*cfg)["port_rpc"].set("limit", to_string(limit));
            return cfg;
        })};


        auto const port = env.app().config()["port_rpc"].
            get<std::uint16_t>("port").value();
        auto const ip = env.app().config()["port_rpc"].
            get<std::string>("ip").value();

        boost::system::error_code ec;
        io_service& ios = get_io_service();
        ip::tcp::resolver r{ios};

        Json::Value jr;
        jr[jss::method] = "server_info";

        auto it =
            r.async_resolve(
                ip::tcp::resolver::query{ip, to_string(port)}, yield[ec]);
        BEAST_EXPECT(! ec);

        std::vector<std::pair<ip::tcp::socket, boost::beast::multi_buffer>> clients;
        int connectionCount {1}; //starts at 1 because the Env already has one
                                 //for JSONRPCCLient

        // for nonzero limits, go one past the limit, although failures happen
        // at the limit, so this really leads to the last two clients failing.
        // for zero limit, pick an arbitrary nonzero number of clients - all should
        // connect fine.

        int testTo = (limit == 0) ? 50 : limit + 1;
        while (connectionCount < testTo)
        {
            clients.emplace_back(
                std::make_pair(ip::tcp::socket {ios}, boost::beast::multi_buffer{}));
            async_connect(clients.back().first, it, yield[ec]);
            BEAST_EXPECT(! ec);
            auto req = makeHTTPRequest(ip, port, to_string(jr), {});
            async_write(
                clients.back().first,
                req,
                yield[ec]);
            BEAST_EXPECT(! ec);
            ++connectionCount;
        }

        int readCount = 0;
        for (auto& c : clients)
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            async_read(c.first, c.second, resp, yield[ec]);
            ++readCount;
            // expect the reads to fail for the clients that connected at or
            // above the limit. If limit is 0, all reads should succeed
            BEAST_EXPECT((limit == 0 || readCount < limit-1) ? (! ec) : ec);
        }
    }

    void
    testWSHandoff(boost::asio::yield_context& yield)
    {
        testcase ("Connection with WS handoff");

        using namespace test::jtx;
        Env env {*this, envconfig([](std::unique_ptr<Config> cfg) {
            (*cfg)["port_ws"].set("protocol","wss");
            return cfg;
        })};

        auto const port = env.app().config()["port_ws"].
            get<std::uint16_t>("port").value();
        auto const ip = env.app().config()["port_ws"].
            get<std::string>("ip").value();
        boost::beast::http::response<boost::beast::http::string_body> resp;
        boost::system::error_code ec;
        doRequest(
            yield, makeWSUpgrade(ip, port), ip, port, true, resp, ec);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::switching_protocols);
        BEAST_EXPECT(resp.find("Upgrade") != resp.end() &&
               resp["Upgrade"] == "websocket");
        BEAST_EXPECT(resp.find("Connection") != resp.end() &&
               resp["Connection"] == "upgrade");
    }

    void
    testNoRPC(boost::asio::yield_context& yield)
    {
        testcase ("Connection to port with no RPC enabled");

        using namespace test::jtx;
        Env env {*this};

        auto const port = env.app().config()["port_ws"].
            get<std::uint16_t>("port").value();
        auto const ip = env.app().config()["port_ws"].
            get<std::string>("ip").value();
        boost::beast::http::response<boost::beast::http::string_body> resp;
        boost::system::error_code ec;
        // body content is required here to avoid being
        // detected as a status request
        doRequest(yield,
            makeHTTPRequest(ip, port, "foo", {}), ip, port, false, resp, ec);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::forbidden);
        BEAST_EXPECT(resp.body() == "Forbidden\r\n");
    }

    void
    testWSRequests(boost::asio::yield_context& yield)
    {
        testcase ("WS client sends assorted input");

        using namespace test::jtx;
        using namespace boost::asio;
        using namespace boost::beast::http;
        Env env {*this};

        auto const port = env.app().config()["port_ws"].
            get<std::uint16_t>("port").value();
        auto const ip = env.app().config()["port_ws"].
            get<std::string>("ip").value();
        boost::system::error_code ec;

        io_service& ios = get_io_service();
        ip::tcp::resolver r{ios};

        auto it =
            r.async_resolve(
                ip::tcp::resolver::query{ip, to_string(port)}, yield[ec]);
        if(! BEAST_EXPECT(! ec))
            return;

        ip::tcp::socket sock{ios};
        async_connect(sock, it, yield[ec]);
        if(! BEAST_EXPECT(! ec))
            return;

        boost::beast::websocket::stream<boost::asio::ip::tcp::socket&> ws{sock};
        ws.handshake(ip + ":" + to_string(port), "/");

        // helper lambda, used below
        auto sendAndParse = [&](std::string const& req) -> Json::Value
        {
            ws.async_write_some(true, buffer(req), yield[ec]);
            if(! BEAST_EXPECT(! ec))
                return Json::objectValue;

            boost::beast::multi_buffer sb;
            ws.async_read(sb, yield[ec]);
            if(! BEAST_EXPECT(! ec))
                return Json::objectValue;

            Json::Value resp;
            Json::Reader jr;
            if(! BEAST_EXPECT(jr.parse(
                boost::lexical_cast<std::string>(
                    boost::beast::buffers(sb.data())), resp)))
                return Json::objectValue;
            sb.consume(sb.size());
            return resp;
        };

        { // send invalid json
            auto resp = sendAndParse("NOT JSON");
            BEAST_EXPECT(resp.isMember(jss::error) &&
                resp[jss::error] == "jsonInvalid");
            BEAST_EXPECT(! resp.isMember(jss::status));
        }

        { // send incorrect json (method and command fields differ)
            Json::Value jv;
            jv[jss::command] = "foo";
            jv[jss::method] = "bar";
            auto resp = sendAndParse(to_string(jv));
            BEAST_EXPECT(resp.isMember(jss::error) &&
                resp[jss::error] == "missingCommand");
            BEAST_EXPECT(resp.isMember(jss::status) &&
                resp[jss::status] == "error");
        }

        { // send a ping (not an error)
            Json::Value jv;
            jv[jss::command] = "ping";
            auto resp = sendAndParse(to_string(jv));
            BEAST_EXPECT(resp.isMember(jss::status) &&
                resp[jss::status] == "success");
            BEAST_EXPECT(resp.isMember(jss::result) &&
                resp[jss::result].isMember(jss::role) &&
                resp[jss::result][jss::role] == "admin");
        }
    }

    void
    testAmendmentBlock(boost::asio::yield_context& yield)
    {
        testcase("Status request over WS and RPC with/without Amendment Block");
        using namespace jtx;
        using namespace boost::asio;
        using namespace boost::beast::http;
        Env env {*this, validator( envconfig([](std::unique_ptr<Config> cfg)
            {
                cfg->section("port_rpc").set("protocol", "http");
                return cfg;
            }), "")};

        env.close();

        // advance the ledger so that server status
        // sees a published ledger -- without this, we get a status
        // failure message about no published ledgers
        env.app().getLedgerMaster().tryAdvance();

        // make an RPC server info request and look for
        // amendment_blocked status
        auto si = env.rpc("server_info") [jss::result];
        BEAST_EXPECT(! si[jss::info].isMember(jss::amendment_blocked));
        BEAST_EXPECT(
            env.app().getOPs().getConsensusInfo()["validating"] == true);

        auto const port_ws = env.app().config()["port_ws"].
            get<std::uint16_t>("port");
        auto const ip_ws = env.app().config()["port_ws"].
            get<std::string>("ip");


        boost::system::error_code ec;
        response<string_body> resp;

        doRequest(
            yield,
            makeHTTPRequest(*ip_ws, *port_ws, "", {}),
            *ip_ws,
            *port_ws,
            false,
            resp,
            ec);

        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        BEAST_EXPECT(resp.result() == boost::beast::http::status::ok);
        BEAST_EXPECT(
            resp.body().find("connectivity is working.") != std::string::npos);

        // mark the Network as Amendment Blocked, but still won't fail until
        // ELB is enabled (next step)
        env.app().getOPs().setAmendmentBlocked ();
        env.app().getOPs().beginConsensus(env.closed()->info().hash);

        // consensus now sees validation disabled
        BEAST_EXPECT(
            env.app().getOPs().getConsensusInfo()["validating"] == false);

        // RPC request server_info again, now AB should be returned
        si = env.rpc("server_info") [jss::result];
        BEAST_EXPECT(
            si[jss::info].isMember(jss::amendment_blocked) &&
            si[jss::info][jss::amendment_blocked] == true);

        // but status does not indicate because it still relies on ELB
        // being enabled
        doRequest(
            yield,
            makeHTTPRequest(*ip_ws, *port_ws, "", {}),
            *ip_ws,
            *port_ws,
            false,
            resp,
            ec);

        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        BEAST_EXPECT(resp.result() == boost::beast::http::status::ok);
        BEAST_EXPECT(
            resp.body().find("connectivity is working.") != std::string::npos);

        env.app().config().ELB_SUPPORT = true;

        doRequest(
            yield,
            makeHTTPRequest(*ip_ws, *port_ws, "", {}),
            *ip_ws,
            *port_ws,
            false,
            resp,
            ec);

        if(! BEAST_EXPECTS(! ec, ec.message()))
            return;
        BEAST_EXPECT(resp.result() == boost::beast::http::status::internal_server_error);
        BEAST_EXPECT(
            resp.body().find("cannot accept clients:") != std::string::npos);
        BEAST_EXPECT(
            resp.body().find("Server version too old") != std::string::npos);
    }

    void
    testRPCRequests(boost::asio::yield_context& yield)
    {
        testcase ("RPC client sends assorted input");

        using namespace test::jtx;
        Env env {*this};

        boost::system::error_code ec;
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doHTTPRequest(env, yield, false, resp, ec, "{}");
            BEAST_EXPECT(resp.result() == boost::beast::http::status::bad_request);
            BEAST_EXPECT(resp.body() == "Unable to parse request\r\n");
        }

        Json::Value jv;
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            jv[jss::method] = Json::nullValue;
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == boost::beast::http::status::bad_request);
            BEAST_EXPECT(resp.body() == "Null method\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            jv[jss::method] = 1;
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == boost::beast::http::status::bad_request);
            BEAST_EXPECT(resp.body() == "method is not string\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            jv[jss::method] = "";
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == boost::beast::http::status::bad_request);
            BEAST_EXPECT(resp.body() == "method is empty\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            jv[jss::method] = "some_method";
            jv[jss::params] = "params";
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == boost::beast::http::status::bad_request);
            BEAST_EXPECT(resp.body() == "params unparseable\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            jv[jss::params] = Json::arrayValue;
            jv[jss::params][0u] = "not an object";
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == boost::beast::http::status::bad_request);
            BEAST_EXPECT(resp.body() == "params unparseable\r\n");
        }
    }

    void
    testStatusNotOkay(boost::asio::yield_context& yield)
    {
        testcase ("Server status not okay");

        using namespace test::jtx;
        Env env {*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->ELB_SUPPORT = true;
            return cfg;
        })};

        //raise the fee so that the server is considered overloaded
        env.app().getFeeTrack().raiseLocalFee();

        boost::beast::http::response<boost::beast::http::string_body> resp;
        boost::system::error_code ec;
        doHTTPRequest(env, yield, false, resp, ec);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::internal_server_error);
        std::regex body {"Server cannot accept clients"};
        BEAST_EXPECT(std::regex_search(resp.body(), body));
    }

public:
    void
    run()
    {
        for (auto it : {"http", "ws", "ws2"})
        {
            testAdminRequest (it, true, true);
            testAdminRequest (it, true, false);
            testAdminRequest (it, false, false);
        }

        yield_to([&](boost::asio::yield_context& yield)
        {
            testWSClientToHttpServer (yield);
            testStatusRequest (yield);
            testTruncatedWSUpgrade (yield);

            // these are secure/insecure protocol pairs, i.e. for
            // each item, the second value is the secure or insecure equivalent
            testCantConnect ("ws", "wss", yield);
            testCantConnect ("ws2", "wss2", yield);
            testCantConnect ("http", "https", yield);
            testCantConnect ("wss", "ws", yield);
            testCantConnect ("wss2", "ws2", yield);
            testCantConnect ("https", "http", yield);

            testAmendmentBlock(yield);
            testAuth (false, yield);
            testAuth (true, yield);
            testLimit (yield, 5);
            testLimit (yield, 0);
            testWSHandoff (yield);
            testNoRPC (yield);
            testWSRequests (yield);
            testRPCRequests (yield);
            testStatusNotOkay (yield);
        });

    };
};

BEAST_DEFINE_TESTSUITE(ServerStatus, server, ripple);

} // test
} // ripple

