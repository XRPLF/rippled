#include <test/jtx/Env.h>
#include <test/jtx/JSONRPCClient.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/envconfig.h>

#include <xrpld/app/ledger/LedgerMaster.h>

#include <xrpl/basics/base64.h>
#include <xrpl/beast/test/yield_to.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/LoadFeeTrack.h>
#include <xrpl/server/NetworkOPs.h>

#include <boost/algorithm/string/predicate.hpp>
#include <boost/asio/buffer.hpp>
#include <boost/asio/connect.hpp>
#include <boost/asio/io_context.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/spawn.hpp>
#include <boost/asio/ssl/context.hpp>
#include <boost/asio/ssl/stream.hpp>
#include <boost/asio/ssl/stream_base.hpp>
#include <boost/asio/ssl/verify_mode.hpp>
#include <boost/asio/write.hpp>
#include <boost/beast/core/make_printable.hpp>
#include <boost/beast/core/multi_buffer.hpp>
#include <boost/beast/http/field.hpp>
#include <boost/beast/http/status.hpp>
#include <boost/beast/http/verb.hpp>
#include <boost/beast/websocket/stream.hpp>
#include <boost/lexical_cast.hpp>

#include <array>
#include <cstdint>
#include <memory>
#include <random>
#include <regex>
#include <string>
#include <utility>
#include <vector>

namespace xrpl::test {

class ServerStatus_test : public beast::unit_test::Suite, public beast::test::EnableYieldTo
{
    class MyFields : public boost::beast::http::fields
    {
    };

    static auto
    makeConfig(std::string const& proto, bool admin = true, bool credentials = false)
    {
        auto const sectionName =
            boost::starts_with(proto, "h") ? Sections::kPortRpc : Sections::kPortWs;
        auto p = jtx::envconfig();

        p->overwrite(sectionName, Keys::kProtocol, proto);
        if (!admin)
            p->overwrite(sectionName, Keys::kAdmin, "");

        if (credentials)
        {
            (*p)[sectionName].set(Keys::kAdminPassword, "p");
            (*p)[sectionName].set(Keys::kAdminUser, "u");
        }

        p->overwrite(
            boost::starts_with(proto, "h") ? Sections::kPortWs : Sections::kPortRpc,
            Keys::kProtocol,
            boost::starts_with(proto, "h") ? "ws" : "http");

        if (proto == "https")
        {
            // this port is here to allow the env to create its internal client,
            // which requires an http endpoint to talk to. In the connection
            // failure test, this endpoint should never be used
            (*p)[Sections::kServer].append("port_alt");
            (*p)["port_alt"].set(Keys::kIp, getEnvLocalhostAddr());
            (*p)["port_alt"].set(Keys::kPort, "7099");
            (*p)["port_alt"].set(Keys::kProtocol, "http");
            (*p)["port_alt"].set(Keys::kAdmin, getEnvLocalhostAddr());
        }

        return p;
    }

    static auto
    makeWSUpgrade(std::string const& host, uint16_t port)
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
        {
            // not secure, but OK for a testing
            std::random_device rd;
            std::mt19937 e{rd()};
            std::uniform_int_distribution<> d(0, 255);
            std::array<std::uint8_t, 16> key{};
            for (auto& v : key)
                v = d(e);
            req.insert("Sec-WebSocket-Key", base64Encode(key.data(), key.size()));
        };
        req.insert("Sec-WebSocket-Version", "13");
        req.insert(boost::beast::http::field::connection, "upgrade");
        return req;
    }

    static auto
    makeHTTPRequest(
        std::string const& host,
        uint16_t port,
        std::string const& body,
        MyFields const& fields)
    {
        using namespace boost::asio;
        using namespace boost::beast::http;
        request<string_body> req;

        req.target("/");
        req.version(11);
        for (auto const& f : fields)
            req.insert(f.name(), f.value());
        req.insert("Host", host + ":" + std::to_string(port));
        req.insert("User-Agent", "test");
        if (body.empty())
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
        boost::beast::http::request<boost::beast::http::string_body> const& req,
        std::string const& host,
        uint16_t port,
        bool secure,
        boost::beast::http::response<boost::beast::http::string_body>& resp,
        boost::system::error_code& ec)
    {
        using namespace boost::asio;
        using namespace boost::beast::http;
        io_context& ios = getIoContext();
        ip::tcp::resolver r{ios};
        boost::beast::multi_buffer sb;

        auto it = r.async_resolve(host, std::to_string(port), yield[ec]);
        if (ec)
            return;

        resp.body().clear();
        if (secure)
        {
            ssl::context ctx{ssl::context::sslv23};
            ctx.set_verify_mode(ssl::verify_none);
            ssl::stream<ip::tcp::socket> ss{ios, ctx};
            async_connect(ss.next_layer(), it, yield[ec]);
            if (ec)
                return;
            ss.async_handshake(ssl::stream_base::client, yield[ec]);
            if (ec)
                return;
            boost::beast::http::async_write(ss, req, yield[ec]);
            if (ec)
                return;
            async_read(ss, sb, resp, yield[ec]);
            if (ec)
                return;
        }
        else
        {
            ip::tcp::socket sock{ios};
            async_connect(sock, it, yield[ec]);
            if (ec)
                return;
            boost::beast::http::async_write(sock, req, yield[ec]);
            if (ec)
                return;
            async_read(sock, sb, resp, yield[ec]);
            if (ec)
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
        auto const port = env.app().config()[Sections::kPortWs].get<std::uint16_t>(Keys::kPort);
        auto ip = env.app().config()[Sections::kPortWs].get<std::string>(Keys::kIp);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        doRequest(yield, makeWSUpgrade(*ip, *port), *ip, *port, secure, resp, ec);
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
        MyFields const& fields = {})
    {
        auto const port = env.app().config()[Sections::kPortRpc].get<std::uint16_t>(Keys::kPort);
        auto const ip = env.app().config()[Sections::kPortRpc].get<std::string>(Keys::kIp);
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        doRequest(yield, makeHTTPRequest(*ip, *port, body, fields), *ip, *port, secure, resp, ec);
        return;
    }

    static auto
    makeAdminRequest(
        jtx::Env& env,
        std::string const& proto,
        std::string const& user,
        std::string const& password,
        bool subobject = false)
    {
        json::Value jrr;

        json::Value jp = json::ValueType::Object;
        if (!user.empty())
        {
            jp["admin_user"] = user;
            if (subobject)
            {
                // special case of bad password..passed as object
                json::Value jpi = json::ValueType::Object;
                jpi["admin_password"] = password;
                jp["admin_password"] = jpi;
            }
            else
            {
                jp["admin_password"] = password;
            }
        }

        if (boost::starts_with(proto, "h"))
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
        testcase << "Admin request over " << proto << ", config "
                 << (admin ? "enabled" : "disabled") << ", credentials "
                 << (credentials ? "" : "not ") << "set";
        using namespace jtx;
        Env env{*this, makeConfig(proto, admin, credentials)};

        json::Value jrr;
        auto const protoWs = boost::starts_with(proto, "w");

        // the set of checks we do are different depending
        // on how the admin config options are set

        if (admin && credentials)
        {
            auto const user = env.app()
                                  .config()[protoWs ? Sections::kPortWs : Sections::kPortRpc]
                                  .get<std::string>(Keys::kAdminUser);

            auto const password = env.app()
                                      .config()[protoWs ? Sections::kPortWs : Sections::kPortRpc]
                                      .get<std::string>(Keys::kAdminPassword);

            // 1 - FAILS with wrong pass
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            jrr = makeAdminRequest(env, proto, *user, *password + "_")[jss::result];
            BEAST_EXPECT(jrr["error"] == protoWs ? "forbidden" : "noPermission");
            BEAST_EXPECT(
                jrr["error_message"] == protoWs ? "Bad credentials."
                                                : "You don't have permission for this command.");

            // 2 - FAILS with password in an object
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            jrr = makeAdminRequest(env, proto, *user, *password, true)[jss::result];
            BEAST_EXPECT(jrr["error"] == protoWs ? "forbidden" : "noPermission");
            BEAST_EXPECT(
                jrr["error_message"] == protoWs ? "Bad credentials."
                                                : "You don't have permission for this command.");

            // 3 - FAILS with wrong user
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            jrr = makeAdminRequest(env, proto, *user + "_", *password)[jss::result];
            BEAST_EXPECT(jrr["error"] == protoWs ? "forbidden" : "noPermission");
            BEAST_EXPECT(
                jrr["error_message"] == protoWs ? "Bad credentials."
                                                : "You don't have permission for this command.");

            // 4 - FAILS no credentials
            jrr = makeAdminRequest(env, proto, "", "")[jss::result];
            BEAST_EXPECT(jrr["error"] == protoWs ? "forbidden" : "noPermission");
            BEAST_EXPECT(
                jrr["error_message"] == protoWs ? "Bad credentials."
                                                : "You don't have permission for this command.");

            // 5 - SUCCEEDS with proper credentials
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            jrr = makeAdminRequest(env, proto, *user, *password)[jss::result];
            BEAST_EXPECT(jrr["status"] == "success");
        }
        else if (admin)
        {
            // 1 - SUCCEEDS with proper credentials
            jrr = makeAdminRequest(env, proto, "u", "p")[jss::result];
            BEAST_EXPECT(jrr["status"] == "success");

            // 2 - SUCCEEDS without proper credentials
            jrr = makeAdminRequest(env, proto, "", "")[jss::result];
            BEAST_EXPECT(jrr["status"] == "success");
        }
        else
        {
            // 1 - FAILS - admin disabled
            jrr = makeAdminRequest(env, proto, "", "")[jss::result];
            BEAST_EXPECT(jrr["error"] == protoWs ? "forbidden" : "noPermission");
            BEAST_EXPECT(
                jrr["error_message"] == protoWs ? "Bad credentials."
                                                : "You don't have permission for this command.");
        }
    }

    void
    testWSClientToHttpServer(boost::asio::yield_context& yield)
    {
        testcase("WS client to http server fails");
        using namespace jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->section(Sections::kPortWs).set(Keys::kProtocol, "http,https");
                    return cfg;
                })};

        // non-secure request
        {
            boost::system::error_code ec;
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doWSRequest(env, yield, false, resp, ec);
            if (!BEAST_EXPECTS(!ec, ec.message()))
                return;
            BEAST_EXPECT(resp.result() == boost::beast::http::status::unauthorized);
        }

        // secure request
        {
            boost::system::error_code ec;
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doWSRequest(env, yield, true, resp, ec);
            if (!BEAST_EXPECTS(!ec, ec.message()))
                return;
            BEAST_EXPECT(resp.result() == boost::beast::http::status::unauthorized);
        }
    }

    void
    testStatusRequest(boost::asio::yield_context& yield)
    {
        testcase("Status request");
        using namespace jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->section(Sections::kPortRpc).set(Keys::kProtocol, "ws2,wss2");
                    cfg->section(Sections::kPortWs).set(Keys::kProtocol, "http");
                    return cfg;
                })};

        // non-secure request
        {
            boost::system::error_code ec;
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doHTTPRequest(env, yield, false, resp, ec);
            if (!BEAST_EXPECTS(!ec, ec.message()))
                return;
            BEAST_EXPECT(resp.result() == boost::beast::http::status::ok);
        }

        // secure request
        {
            boost::system::error_code ec;
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doHTTPRequest(env, yield, true, resp, ec);
            if (!BEAST_EXPECTS(!ec, ec.message()))
                return;
            BEAST_EXPECT(resp.result() == boost::beast::http::status::ok);
        }
    }

    void
    testTruncatedWSUpgrade(boost::asio::yield_context& yield)
    {
        testcase("Partial WS upgrade request");
        using namespace jtx;
        using namespace boost::asio;
        using namespace boost::beast::http;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->section(Sections::kPortWs).set(Keys::kProtocol, "ws2");
                    return cfg;
                })};

        auto const port = env.app().config()[Sections::kPortWs].get<std::uint16_t>(Keys::kPort);
        auto const ip = env.app().config()[Sections::kPortWs].get<std::string>(Keys::kIp);

        boost::system::error_code ec;
        response<string_body> resp;
        auto req = makeWSUpgrade(*ip, *port);  // NOLINT(bugprone-unchecked-optional-access)

        // truncate the request message to near the value of the version header
        auto reqString = boost::lexical_cast<std::string>(req);
        reqString.erase(reqString.find_last_of("13"), std::string::npos);

        io_context& ios = getIoContext();
        ip::tcp::resolver r{ios};
        boost::beast::multi_buffer sb;

        auto it = r.async_resolve(
            *ip, std::to_string(*port), yield[ec]);  // NOLINT(bugprone-unchecked-optional-access)
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;

        ip::tcp::socket sock{ios};
        async_connect(sock, it, yield[ec]);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        async_write(sock, boost::asio::buffer(reqString), yield[ec]);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        // since we've sent an incomplete request, the server will
        // keep trying to read until it gives up (by timeout)
        async_read(sock, sb, resp, yield[ec]);
        BEAST_EXPECT(ec);
    }

    void
    testCantConnect(
        std::string const& clientProtocol,
        std::string const& serverProtocol,
        boost::asio::yield_context& yield)
    {
        // The essence of this test is to have a client and server configured
        // out-of-phase with respect to ssl (secure client and insecure server
        // or vice-versa)
        testcase << "Connect fails: " << clientProtocol << " client to " << serverProtocol
                 << " server";
        using namespace jtx;
        Env env{*this, makeConfig(serverProtocol)};

        boost::beast::http::response<boost::beast::http::string_body> resp;
        boost::system::error_code ec;
        if (boost::starts_with(clientProtocol, "h"))
        {
            doHTTPRequest(env, yield, clientProtocol == "https", resp, ec);
            BEAST_EXPECT(ec);
        }
        else
        {
            doWSRequest(env, yield, clientProtocol == "wss" || clientProtocol == "wss2", resp, ec);
            BEAST_EXPECT(ec);
        }
    }

    void
    testAuth(bool secure, boost::asio::yield_context& yield)
    {
        testcase << "Server with authorization, " << (secure ? "secure" : "non-secure");

        using namespace test::jtx;
        Env env{*this, envconfig([secure](std::unique_ptr<Config> cfg) {
                    (*cfg)[Sections::kPortRpc].set(Keys::kUser, "me");
                    (*cfg)[Sections::kPortRpc].set(Keys::kPassword, "secret");
                    (*cfg)[Sections::kPortRpc].set(Keys::kProtocol, secure ? "https" : "http");
                    if (secure)
                        (*cfg)[Sections::kPortWs].set(Keys::kProtocol, "http,ws");
                    return cfg;
                })};

        json::Value jr;
        jr[jss::method] = "server_info";
        boost::beast::http::response<boost::beast::http::string_body> resp;
        boost::system::error_code ec;
        doHTTPRequest(env, yield, secure, resp, ec, to_string(jr));
        BEAST_EXPECT(resp.result() == boost::beast::http::status::forbidden);

        MyFields auth;
        auth.insert("Authorization", "");
        doHTTPRequest(env, yield, secure, resp, ec, to_string(jr), auth);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::forbidden);

        auth.set("Authorization", "Basic NOT-VALID");
        doHTTPRequest(env, yield, secure, resp, ec, to_string(jr), auth);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::forbidden);

        auth.set("Authorization", "Basic " + base64Encode("me:badpass"));
        doHTTPRequest(env, yield, secure, resp, ec, to_string(jr), auth);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::forbidden);

        auto const section = env.app().config().section(Sections::kPortRpc);
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        auto const user = section.get<std::string>(Keys::kUser).value();
        auto const pass = section.get<std::string>(Keys::kPassword).value();
        // NOLINTEND(bugprone-unchecked-optional-access)

        // try with the correct user/pass, but not encoded
        auth.set("Authorization", "Basic " + user + ":" + pass);
        doHTTPRequest(env, yield, secure, resp, ec, to_string(jr), auth);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::forbidden);

        // finally if we use the correct user/pass encoded, we should get a 200
        auth.set("Authorization", "Basic " + base64Encode(user + ":" + pass));
        doHTTPRequest(env, yield, secure, resp, ec, to_string(jr), auth);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::ok);
        BEAST_EXPECT(!resp.body().empty());
    }

    void
    testLimit(boost::asio::yield_context& yield, int limit)
    {
        testcase << "Server with connection limit of " << limit;

        using namespace test::jtx;
        using namespace boost::asio;
        using namespace boost::beast::http;
        Env env{*this, envconfig([&](std::unique_ptr<Config> cfg) {
                    (*cfg)[Sections::kPortRpc].set(Keys::kLimit, std::to_string(limit));
                    return cfg;
                })};

        auto const section = env.app().config().section(Sections::kPortRpc);
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        auto const port = section.get<std::uint16_t>(Keys::kPort).value();
        auto const ip = section.get<std::string>(Keys::kIp).value();
        // NOLINTEND(bugprone-unchecked-optional-access)

        boost::system::error_code ec;
        io_context& ios = getIoContext();
        ip::tcp::resolver r{ios};

        json::Value jr;
        jr[jss::method] = "server_info";

        auto it = r.async_resolve(ip, std::to_string(port), yield[ec]);
        BEAST_EXPECT(!ec);

        std::vector<std::pair<ip::tcp::socket, boost::beast::multi_buffer>> clients;
        int connectionCount{1};  // starts at 1 because the Env already has one
                                 // for JSONRPCCLient

        // for nonzero limits, go one past the limit, although failures happen
        // at the limit, so this really leads to the last two clients failing.
        // for zero limit, pick an arbitrary nonzero number of clients - all
        // should connect fine.

        int const testTo = (limit == 0) ? 50 : limit + 1;
        while (connectionCount < testTo)
        {
            clients.emplace_back(ip::tcp::socket{ios}, boost::beast::multi_buffer{});
            async_connect(clients.back().first, it, yield[ec]);
            BEAST_EXPECT(!ec);
            auto req = makeHTTPRequest(ip, port, to_string(jr), {});
            async_write(clients.back().first, req, yield[ec]);
            BEAST_EXPECT(!ec);
            ++connectionCount;
        }

        int readCount = 0;
        for (auto& [soc, buf] : clients)
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            async_read(soc, buf, resp, yield[ec]);
            ++readCount;
            // expect the reads to fail for the clients that connected at or
            // above the limit. If limit is 0, all reads should succeed
            BEAST_EXPECT((limit == 0 || readCount < limit - 1) ? (!ec) : bool(ec));
        }
    }

    void
    testWSHandoff(boost::asio::yield_context& yield)
    {
        testcase("Connection with WS handoff");

        using namespace test::jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg)[Sections::kPortWs].set(Keys::kProtocol, "wss");
                    return cfg;
                })};

        auto const section = env.app().config().section(Sections::kPortWs);
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        auto const port = section.get<std::uint16_t>(Keys::kPort).value();
        auto const ip = section.get<std::string>(Keys::kIp).value();
        // NOLINTEND(bugprone-unchecked-optional-access)
        boost::beast::http::response<boost::beast::http::string_body> resp;
        boost::system::error_code ec;
        doRequest(yield, makeWSUpgrade(ip, port), ip, port, true, resp, ec);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::switching_protocols);
        BEAST_EXPECT(resp.contains("Upgrade") && resp["Upgrade"] == "websocket");
        BEAST_EXPECT(resp.contains("Connection") && boost::iequals(resp["Connection"], "upgrade"));
    }

    void
    testNoRPC(boost::asio::yield_context& yield)
    {
        testcase("Connection to port with no RPC enabled");

        using namespace test::jtx;
        Env env{*this};

        auto const section = env.app().config().section(Sections::kPortWs);
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        auto const port = section.get<std::uint16_t>(Keys::kPort).value();
        auto const ip = section.get<std::string>(Keys::kIp).value();
        // NOLINTEND(bugprone-unchecked-optional-access)
        boost::beast::http::response<boost::beast::http::string_body> resp;
        boost::system::error_code ec;
        // body content is required here to avoid being
        // detected as a status request
        doRequest(yield, makeHTTPRequest(ip, port, "foo", {}), ip, port, false, resp, ec);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::forbidden);
        BEAST_EXPECT(resp.body() == "Forbidden\r\n");
    }

    void
    testWSRequests(boost::asio::yield_context& yield)
    {
        testcase("WS client sends assorted input");

        using namespace test::jtx;
        using namespace boost::asio;
        using namespace boost::beast::http;
        Env env{*this};

        auto const section = env.app().config().section(Sections::kPortWs);
        // NOLINTBEGIN(bugprone-unchecked-optional-access)
        auto const port = section.get<std::uint16_t>(Keys::kPort).value();
        auto const ip = section.get<std::string>(Keys::kIp).value();
        // NOLINTEND(bugprone-unchecked-optional-access)
        boost::system::error_code ec;

        io_context& ios = getIoContext();
        ip::tcp::resolver r{ios};

        auto it = r.async_resolve(ip, std::to_string(port), yield[ec]);
        if (!BEAST_EXPECT(!ec))
            return;

        ip::tcp::socket sock{ios};
        async_connect(sock, it, yield[ec]);
        if (!BEAST_EXPECT(!ec))
            return;

        boost::beast::websocket::stream<boost::asio::ip::tcp::socket&> ws{sock};
        ws.handshake(ip + ":" + std::to_string(port), "/");

        // helper lambda, used below
        auto sendAndParse = [&](std::string const& req) -> json::Value {
            ws.async_write_some(true, buffer(req), yield[ec]);
            if (!BEAST_EXPECT(!ec))
                return json::ValueType::Object;

            boost::beast::multi_buffer sb;
            ws.async_read(sb, yield[ec]);
            if (!BEAST_EXPECT(!ec))
                return json::ValueType::Object;

            json::Value resp;
            json::Reader jr;
            if (!BEAST_EXPECT(jr.parse(
                    boost::lexical_cast<std::string>(boost::beast::make_printable(sb.data())),
                    resp)))
                return json::ValueType::Object;
            sb.consume(sb.size());
            return resp;
        };

        {  // send invalid json
            auto resp = sendAndParse("NOT JSON");
            BEAST_EXPECT(resp.isMember(jss::error) && resp[jss::error] == "jsonInvalid");
            BEAST_EXPECT(!resp.isMember(jss::status));
        }

        {  // send incorrect json (method and command fields differ)
            json::Value jv;
            jv[jss::command] = "foo";
            jv[jss::method] = "bar";
            auto resp = sendAndParse(to_string(jv));
            BEAST_EXPECT(resp.isMember(jss::error) && resp[jss::error] == "missingCommand");
            BEAST_EXPECT(resp.isMember(jss::status) && resp[jss::status] == "error");
        }

        {  // send a ping (not an error)
            json::Value jv;
            jv[jss::command] = "ping";
            auto resp = sendAndParse(to_string(jv));
            BEAST_EXPECT(resp.isMember(jss::status) && resp[jss::status] == "success");
            BEAST_EXPECT(
                resp.isMember(jss::result) && resp[jss::result].isMember(jss::role) &&
                resp[jss::result][jss::role] == "admin");
        }
    }

    void
    testAmendmentWarning(boost::asio::yield_context& yield)
    {
        testcase("Status request over WS and RPC with/without Amendment Warning");
        using namespace jtx;
        using namespace boost::asio;
        using namespace boost::beast::http;
        Env env{
            *this,
            validator(
                envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->section(Sections::kPortRpc).set(Keys::kProtocol, "http");
                    return cfg;
                }),
                "")};

        env.close();

        // advance the ledger so that server status
        // sees a published ledger -- without this, we get a status
        // failure message about no published ledgers
        env.app().getLedgerMaster().tryAdvance();

        // make an RPC server info request and look for
        // amendment warning status
        auto si = env.rpc("server_info")[jss::result];
        BEAST_EXPECT(si.isMember(jss::info));
        BEAST_EXPECT(!si[jss::info].isMember(jss::amendment_blocked));
        BEAST_EXPECT(env.app().getOPs().getConsensusInfo()["validating"] == true);
        BEAST_EXPECT(!si.isMember(jss::warnings));

        // make an RPC server state request and look for
        // amendment warning status
        si = env.rpc("server_state")[jss::result];
        BEAST_EXPECT(si.isMember(jss::state));
        BEAST_EXPECT(!si[jss::state].isMember(jss::amendment_blocked));
        BEAST_EXPECT(env.app().getOPs().getConsensusInfo()["validating"] == true);
        BEAST_EXPECT(!si[jss::state].isMember(jss::warnings));

        auto const portWs = env.app().config()[Sections::kPortWs].get<std::uint16_t>(Keys::kPort);
        auto const ipWs = env.app().config()[Sections::kPortWs].get<std::string>(Keys::kIp);

        boost::system::error_code ec;
        response<string_body> resp;

        doRequest(
            yield,
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            makeHTTPRequest(*ipWs, *portWs, "", {}),
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            *ipWs,
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            *portWs,
            false,
            resp,
            ec);

        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        BEAST_EXPECT(resp.result() == boost::beast::http::status::ok);
        BEAST_EXPECT(resp.body().contains("connectivity is working."));

        // mark the Network as having an Amendment Warning, but won't fail
        env.app().getOPs().setAmendmentWarned();
        env.app().getOPs().beginConsensus(env.closed()->header().hash, {});

        // consensus doesn't change
        BEAST_EXPECT(env.app().getOPs().getConsensusInfo()["validating"] == true);

        // RPC request server_info again, now unsupported majority should be
        // returned
        si = env.rpc("server_info")[jss::result];
        BEAST_EXPECT(si.isMember(jss::info));
        BEAST_EXPECT(!si[jss::info].isMember(jss::amendment_blocked));
        BEAST_EXPECT(
            si[jss::info].isMember(jss::warnings) && si[jss::info][jss::warnings].isArray() &&
            si[jss::info][jss::warnings].size() == 1 &&
            si[jss::info][jss::warnings][0u][jss::id].asInt() == WarnRpcUnsupportedMajority);

        // RPC request server_state again, now unsupported majority should be
        // returned
        si = env.rpc("server_state")[jss::result];
        BEAST_EXPECT(si.isMember(jss::state));
        BEAST_EXPECT(!si[jss::state].isMember(jss::amendment_blocked));
        BEAST_EXPECT(
            si[jss::state].isMember(jss::warnings) && si[jss::state][jss::warnings].isArray() &&
            si[jss::state][jss::warnings].size() == 1 &&
            si[jss::state][jss::warnings][0u][jss::id].asInt() == WarnRpcUnsupportedMajority);

        // but status does not indicate a problem
        doRequest(
            yield,
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            makeHTTPRequest(*ipWs, *portWs, "", {}),
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            *ipWs,
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            *portWs,
            false,
            resp,
            ec);

        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        BEAST_EXPECT(resp.result() == boost::beast::http::status::ok);
        BEAST_EXPECT(resp.body().contains("connectivity is working."));

        // with ELB_SUPPORT, status still does not indicate a problem
        env.app().config().elbSupport = true;

        doRequest(
            yield,
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            makeHTTPRequest(*ipWs, *portWs, "", {}),
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            *ipWs,
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            *portWs,
            false,
            resp,
            ec);

        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        BEAST_EXPECT(resp.result() == boost::beast::http::status::ok);
        BEAST_EXPECT(resp.body().contains("connectivity is working."));
    }

    void
    testAmendmentBlock(boost::asio::yield_context& yield)
    {
        testcase("Status request over WS and RPC with/without Amendment Block");
        using namespace jtx;
        using namespace boost::asio;
        using namespace boost::beast::http;
        Env env{
            *this,
            validator(
                envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->section(Sections::kPortRpc).set(Keys::kProtocol, "http");
                    return cfg;
                }),
                "")};

        env.close();

        // advance the ledger so that server status
        // sees a published ledger -- without this, we get a status
        // failure message about no published ledgers
        env.app().getLedgerMaster().tryAdvance();

        // make an RPC server info request and look for
        // amendment_blocked status
        auto si = env.rpc("server_info")[jss::result];
        BEAST_EXPECT(si.isMember(jss::info));
        BEAST_EXPECT(!si[jss::info].isMember(jss::amendment_blocked));
        BEAST_EXPECT(env.app().getOPs().getConsensusInfo()["validating"] == true);
        BEAST_EXPECT(!si.isMember(jss::warnings));

        // make an RPC server state request and look for
        // amendment_blocked status
        si = env.rpc("server_state")[jss::result];
        BEAST_EXPECT(si.isMember(jss::state));
        BEAST_EXPECT(!si[jss::state].isMember(jss::amendment_blocked));
        BEAST_EXPECT(env.app().getOPs().getConsensusInfo()["validating"] == true);
        BEAST_EXPECT(!si[jss::state].isMember(jss::warnings));

        auto const portWs = env.app().config()[Sections::kPortWs].get<std::uint16_t>(Keys::kPort);
        auto const ipWs = env.app().config()[Sections::kPortWs].get<std::string>(Keys::kIp);

        boost::system::error_code ec;
        response<string_body> resp;

        doRequest(
            yield,
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            makeHTTPRequest(*ipWs, *portWs, "", {}),
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            *ipWs,
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            *portWs,
            false,
            resp,
            ec);

        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        BEAST_EXPECT(resp.result() == boost::beast::http::status::ok);
        BEAST_EXPECT(resp.body().contains("connectivity is working."));

        // mark the Network as Amendment Blocked, but still won't fail until
        // ELB is enabled (next step)
        env.app().getOPs().setAmendmentBlocked();
        env.app().getOPs().beginConsensus(env.closed()->header().hash, {});

        // consensus now sees validation disabled
        BEAST_EXPECT(env.app().getOPs().getConsensusInfo()["validating"] == false);

        // RPC request server_info again, now AB should be returned
        si = env.rpc("server_info")[jss::result];
        BEAST_EXPECT(si.isMember(jss::info));
        BEAST_EXPECT(
            si[jss::info].isMember(jss::amendment_blocked) &&
            si[jss::info][jss::amendment_blocked] == true);
        BEAST_EXPECT(
            si[jss::info].isMember(jss::warnings) && si[jss::info][jss::warnings].isArray() &&
            si[jss::info][jss::warnings].size() == 1 &&
            si[jss::info][jss::warnings][0u][jss::id].asInt() == WarnRpcAmendmentBlocked);

        // RPC request server_state again, now AB should be returned
        si = env.rpc("server_state")[jss::result];
        BEAST_EXPECT(
            si[jss::state].isMember(jss::amendment_blocked) &&
            si[jss::state][jss::amendment_blocked] == true);
        BEAST_EXPECT(
            si[jss::state].isMember(jss::warnings) && si[jss::state][jss::warnings].isArray() &&
            si[jss::state][jss::warnings].size() == 1 &&
            si[jss::state][jss::warnings][0u][jss::id].asInt() == WarnRpcAmendmentBlocked);

        // but status does not indicate because it still relies on ELB
        // being enabled
        doRequest(
            yield,
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            makeHTTPRequest(*ipWs, *portWs, "", {}),
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            *ipWs,
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            *portWs,
            false,
            resp,
            ec);

        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        BEAST_EXPECT(resp.result() == boost::beast::http::status::ok);
        BEAST_EXPECT(resp.body().contains("connectivity is working."));

        env.app().config().elbSupport = true;

        doRequest(
            yield,
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            makeHTTPRequest(*ipWs, *portWs, "", {}),
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            *ipWs,
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            *portWs,
            false,
            resp,
            ec);

        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;
        BEAST_EXPECT(resp.result() == boost::beast::http::status::internal_server_error);
        BEAST_EXPECT(resp.body().contains("cannot accept clients:"));
        BEAST_EXPECT(resp.body().contains("Server version too old"));
    }

    void
    testRPCRequests(boost::asio::yield_context& yield)
    {
        testcase("RPC client sends assorted input");

        using namespace test::jtx;
        Env env{*this};

        boost::system::error_code ec;
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doHTTPRequest(env, yield, false, resp, ec, "{}");
            BEAST_EXPECT(resp.result() == boost::beast::http::status::bad_request);
            BEAST_EXPECT(resp.body() == "Unable to parse request: \r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            json::Value jv;
            jv["invalid"] = 1;
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == boost::beast::http::status::bad_request);
            BEAST_EXPECT(resp.body() == "Null method\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            json::Value jv(json::ValueType::Array);
            jv.append("invalid");
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == boost::beast::http::status::bad_request);
            BEAST_EXPECT(resp.body() == "Unable to parse request: \r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            json::Value jv(json::ValueType::Array);
            json::Value j;
            j["invalid"] = 1;
            jv.append(j);
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == boost::beast::http::status::bad_request);
            BEAST_EXPECT(resp.body() == "Unable to parse request: \r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            json::Value jv;
            jv[jss::method] = "batch";
            jv[jss::params] = 2;
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == boost::beast::http::status::bad_request);
            BEAST_EXPECT(resp.body() == "Malformed batch request\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            json::Value jv;
            jv[jss::method] = "batch";
            jv[jss::params] = json::ValueType::Object;
            jv[jss::params]["invalid"] = 3;
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == boost::beast::http::status::bad_request);
            BEAST_EXPECT(resp.body() == "Malformed batch request\r\n");
        }

        json::Value jv;
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            jv[jss::method] = json::ValueType::Null;
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
            BEAST_EXPECT(resp.body() == "params unparsable\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = "not an object";
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == boost::beast::http::status::bad_request);
            BEAST_EXPECT(resp.body() == "params unparsable\r\n");
        }
    }

    void
    testStatusNotOkay(boost::asio::yield_context& yield)
    {
        testcase("Server status not okay");

        using namespace test::jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->elbSupport = true;
                    return cfg;
                })};

        // raise the fee so that the server is considered overloaded
        env.app().getFeeTrack().raiseLocalFee();

        boost::beast::http::response<boost::beast::http::string_body> resp;
        boost::system::error_code ec;
        doHTTPRequest(env, yield, false, resp, ec);
        BEAST_EXPECT(resp.result() == boost::beast::http::status::internal_server_error);
        std::regex const body{"Server cannot accept clients"};
        BEAST_EXPECT(std::regex_search(resp.body(), body));
    }

public:
    void
    run() override
    {
        for (auto it : {"http", "ws", "ws2"})
        {
            testAdminRequest(it, true, true);
            testAdminRequest(it, true, false);
            testAdminRequest(it, false, false);
        }

        yieldTo([&](boost::asio::yield_context& yield) {
            testWSClientToHttpServer(yield);
            testStatusRequest(yield);
            testTruncatedWSUpgrade(yield);

            // these are secure/insecure protocol pairs, i.e. for
            // each item, the second value is the secure or insecure equivalent
            testCantConnect("ws", "wss", yield);
            testCantConnect("ws2", "wss2", yield);
            testCantConnect("http", "https", yield);
            testCantConnect("wss", "ws", yield);
            testCantConnect("wss2", "ws2", yield);
            testCantConnect("https", "http", yield);

            testAmendmentWarning(yield);
            testAmendmentBlock(yield);
            testAuth(false, yield);
            testAuth(true, yield);
            testLimit(yield, 5);
            testLimit(yield, 0);
            testWSHandoff(yield);
            testNoRPC(yield);
            testWSRequests(yield);
            testRPCRequests(yield);
            testStatusNotOkay(yield);
        });
    }
};

BEAST_DEFINE_TESTSUITE(ServerStatus, server, xrpl);

}  // namespace xrpl::test
