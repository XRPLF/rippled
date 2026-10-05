#include <test/jtx/Account.h>
#include <test/jtx/CaptureLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/JSONRPCClient.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/pay.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/rpc/detail/MaskSecrets.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/basics/base64.h>
#include <xrpl/beast/net/IPEndpoint.h>
#include <xrpl/beast/test/yield_to.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_reader.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/JsonRpc.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/resource/Charge.h>
#include <xrpl/resource/Consumer.h>
#include <xrpl/resource/Fees.h>
#include <xrpl/resource/ResourceManager.h>
#include <xrpl/resource/detail/Tuning.h>
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
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <random>
#include <regex>
#include <string>
#include <string_view>
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
        auto const sectionName = proto.starts_with('h') ? Sections::kPortRpc : Sections::kPortWs;
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
            proto.starts_with('h') ? Sections::kPortWs : Sections::kPortRpc,
            Keys::kProtocol,
            proto.starts_with('h') ? "ws" : "http");

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

    /**
     * Build an HTTP/1.1 request for the server's root. It is a GET when `body`
     * is empty and otherwise a JSON POST carrying it.
     *
     * @param host The host name or address the `Host` header names.
     * @param port The port the `Host` header names.
     * @param body The request body, empty for a GET.
     * @param fields Headers inserted by name, so a name Beast has no
     *        enumerator for, such as `X-User`, is carried too.
     * @return The request, with its payload prepared.
     */
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
        // By name: `name()` answers `field::unknown` for a header Beast has no enumerator for,
        // such as `X-User`, and inserting that asserts.
        for (auto const& f : fields)
            req.insert(f.name_string(), f.value());
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
    }

    using Response = boost::beast::http::response<boost::beast::http::string_body>;

    static constexpr auto kOk = boost::beast::http::status::ok;
    static constexpr auto kBadRequest = boost::beast::http::status::bad_request;

    /**
     * Posts @p body to the RPC port and returns the reply, parsed.
     *
     * @param env The environment naming the port.
     * @param yield The coroutine the request runs on.
     * @param resp Receives the whole HTTP response, for a caller asserting on
     *         its status.
     * @param ec Receives a connection error.
     * @param body The request to post.
     * @param label Identifies which case failed, for a caller iterating over
     *         several. An empty one reports what BEAST_EXPECT would.
     * @param fields Extra headers to send with it.
     * @return The reply, parsed. Empty when the body is not JSON, which this
     *          asserts against.
     */
    json::Value
    postAndParse(
        test::jtx::Env& env,
        boost::asio::yield_context& yield,
        Response& resp,
        boost::system::error_code& ec,
        std::string const& body,
        std::string_view label = {},
        MyFields const& fields = {})
    {
        doHTTPRequest(env, yield, false, resp, ec, body, fields);

        json::Value reply;
        BEAST_EXPECTS(json::Reader{}.parse(resp.body(), reply), label);
        return reply;
    }

    /**
     * Charges the inbound resource entry of @p ip past the drop threshold.
     *
     * The balance sums charges over the decay window, so one large charge
     * stands in for thousands of requests.
     *
     * @param env The environment whose resource manager holds the entry.
     * @param ip The address the entry is keyed by.
     */
    static void
    overloadEndpoint(test::jtx::Env& env, std::string const& ip)
    {
        auto usage =
            env.app().getResourceManager().newInboundEndpoint(beast::ip::Endpoint::fromString(ip));
        usage.charge(
            resource::Charge{
                2 * resource::kDropThreshold * resource::kDecayWindowSeconds, "test overload"});
    }

    /**
     * Posts @p request to the RPC port and returns the reply, asserting that it
     * was answered.
     *
     * For a case whose subject is what a reply says, not the status it carries.
     *
     * @param env The environment naming the port.
     * @param yield The coroutine the request runs on.
     * @param ec Receives a connection error.
     * @param request The request to post.
     * @return The reply, parsed.
     */
    json::Value
    answer(
        test::jtx::Env& env,
        boost::asio::yield_context& yield,
        boost::system::error_code& ec,
        json::Value const& request)
    {
        Response resp;
        auto const reply = postAndParse(env, yield, resp, ec, to_string(request));
        BEAST_EXPECT(resp.result() == kOk);
        return reply;
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
    }

    /**
     * Sends @p body verbatim over a fresh WebSocket session and returns the
     * first reply, parsed, so a message naming no command can be exercised.
     *
     * @param yield The coroutine the request runs on.
     * @param ip The server address.
     * @param port The WebSocket port.
     * @param body The frame to send.
     * @return The reply, parsed. Empty when the connection fails, which this
     *         asserts against.
     */
    json::Value
    doWSMessage(
        boost::asio::yield_context& yield,
        std::string const& ip,
        std::uint16_t port,
        std::string const& body)
    {
        using namespace boost::asio;
        using namespace boost::beast;

        io_context& ios = getIoContext();
        websocket::stream<ip::tcp::socket> ws{ios};
        boost::system::error_code ec;

        ip::tcp::resolver r{ios};
        auto const it = r.async_resolve(ip, std::to_string(port), yield[ec]);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return {};
        async_connect(ws.next_layer(), it, yield[ec]);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return {};
        ws.async_handshake(ip + ":" + std::to_string(port), "/", yield[ec]);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return {};

        ws.async_write(buffer(body), yield[ec]);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return {};

        multi_buffer sb;
        ws.async_read(sb, yield[ec]);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return {};

        std::string text;
        text.resize(buffer_size(sb.data()));
        buffer_copy(buffer(text.data(), text.size()), sb.data());

        json::Value reply;
        BEAST_EXPECT(json::Reader{}.parse(text, reply));
        return reply;
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

        if (proto.starts_with('h'))
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
        auto const protoWs = proto.starts_with('w');

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
            BEAST_EXPECT(resp.result() == kOk);
        }

        // secure request
        {
            boost::system::error_code ec;
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doHTTPRequest(env, yield, true, resp, ec);
            if (!BEAST_EXPECTS(!ec, ec.message()))
                return;
            BEAST_EXPECT(resp.result() == kOk);
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
        if (clientProtocol.starts_with('h'))
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
        BEAST_EXPECT(resp.result() == kOk);
        BEAST_EXPECT(!resp.body().empty());
    }

    void
    testLimit(boost::asio::yield_context& yield, int limit)
    {
        testcase << "Server with connection limit of " << limit;

        using namespace test::jtx;
        using namespace boost::asio;
        using namespace boost::beast::http;
        // Run the server with a single io thread so disconnectClient() below
        // can deterministically drain the server's io_context (see its docs).
        Env env{*this, singleThreadIo(envconfig([&](std::unique_ptr<Config> cfg) {
                    (*cfg)[Sections::kPortRpc].set(Keys::kLimit, std::to_string(limit));
                    return cfg;
                }))};

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

        // Env owns a persistent JSON-RPC HTTP client connection to port_rpc as
        // part of startup, which counts against this port's connection limit.
        // This test wants a known starting occupancy of zero, so for nonzero
        // limits it deterministically drops that hidden client and waits for
        // the server to register the disconnect before opening its own clients.
        //
        // Starting from zero is important because the port limit rejects once
        // the incremented connection count reaches the configured limit. With a
        // zero baseline and N = limit + 1 test-owned clients, exactly the last
        // two requests should be rejected.
        if (limit != 0)
            BEAST_EXPECT(env.disconnectClient());

        // For nonzero limits, go one past the limit. The port rejects at the
        // limit, not only above it, so this yields the last two clients
        // failing. For zero limit, pick an arbitrary nonzero number of clients
        // and expect them all to succeed.

        int const testTo = (limit == 0) ? 50 : limit + 1;
        while (static_cast<int>(clients.size()) < testTo)
        {
            clients.emplace_back(ip::tcp::socket{ios}, boost::beast::multi_buffer{});
            async_connect(clients.back().first, it, yield[ec]);
            BEAST_EXPECT(!ec);
            auto req = makeHTTPRequest(ip, port, to_string(jr), {});
            async_write(clients.back().first, req, yield[ec]);
            BEAST_EXPECT(!ec);
        }

        int successfulReads = 0;
        for (auto& [soc, buf] : clients)
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            async_read(soc, buf, resp, yield[ec]);
            if (!ec)
                ++successfulReads;
        }

        // This test cares about the exact number of accepted requests, not which
        // specific client observed the rejection. With a zero baseline (the
        // hidden Env client dropped above), the server accepts until the
        // connection count reaches the limit: all clients for limit 0, else
        // limit - 1 of the limit + 1 clients (the last two are rejected).
        int const expectedReads = (limit == 0) ? static_cast<int>(clients.size()) : limit - 1;
        BEAST_EXPECT(successfulReads == expectedReads);
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
            // The body is reported by size, not echoed.
            BEAST_EXPECT(resp.isMember(jss::size) && resp[jss::size] == 8);
            BEAST_EXPECT(!resp.isMember(jss::value));
        }

        {  // an unparsable frame carrying a credential does not echo it
            auto resp =
                sendAndParse(R"({"command":"submit","secret":"snoPBrXtMeMyMHUVTgbuqAfg1SUTb",})");
            BEAST_EXPECT(resp.isMember(jss::error) && resp[jss::error] == "jsonInvalid");
            BEAST_EXPECT(!to_string(resp).contains("snoPBrXtMeMyMHUVTgbuqAfg1SUTb"));
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

    /**
     * A WebSocket frame the server cannot read is charged against the sender's
     * resource entry, and a sender over the drop threshold is disconnected
     * instead of answered.
     *
     * @param yield The coroutine the frames are sent on.
     */
    void
    testWSUnparsableFrames(boost::asio::yield_context& yield)
    {
        testcase("Unparsable WS frames are charged for");

        using namespace test::jtx;
        using namespace boost::asio;

        // Without admin privilege: a privileged connection is neither charged nor dropped.
        Env env{*this, envconfig(noAdmin)};

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

        auto const sendAndParse = [&](std::string const& req) -> json::Value {
            ws.async_write_some(true, buffer(req), yield[ec]);
            if (!BEAST_EXPECT(!ec))
                return json::ValueType::Object;

            boost::beast::multi_buffer sb;
            ws.async_read(sb, yield[ec]);
            if (!BEAST_EXPECT(!ec))
                return json::ValueType::Object;

            json::Value resp;
            BEAST_EXPECT(
                json::Reader{}.parse(
                    boost::lexical_cast<std::string>(boost::beast::make_printable(sb.data())),
                    resp));
            return resp;
        };

        // The entry is keyed by address, so a frame from this client is charged against it.
        auto usage =
            env.app().getResourceManager().newInboundEndpoint(beast::ip::Endpoint::fromString(ip));
        auto const before = usage.balance();

        // An unreadable frame is answered and charged here; it never reaches the session where
        // every other request is charged.
        for (int i = 0; i < 10; ++i)
        {
            auto const resp = sendAndParse("NOT JSON");
            BEAST_EXPECT(resp[jss::error] == "jsonInvalid");
        }
        BEAST_EXPECT(usage.balance() > before);

        // Over the threshold the frame is not answered at all: the connection is closed.
        overloadEndpoint(env, ip);

        ws.async_write_some(true, buffer(std::string_view{"NOT JSON"}), yield[ec]);
        if (BEAST_EXPECT(!ec))
        {
            boost::beast::multi_buffer sb;
            ws.async_read(sb, yield[ec]);
            BEAST_EXPECT(ec);
        }
    }

    /**
     * A privileged WebSocket connection is answered for every unreadable frame,
     * its resource entry being exempt from the charge and the drop.
     *
     * @param yield The coroutine the frames are sent on.
     */
    void
    testPrivilegedWSFramesAreExempt(boost::asio::yield_context& yield)
    {
        testcase("A privileged WS connection is exempt from the frame charge");

        using namespace test::jtx;
        using namespace boost::asio;

        // With admin privilege, which the default configuration grants a local connection. Its
        // resource entry is exempt from both the charge and the drop.
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
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;

        ip::tcp::socket sock{ios};
        async_connect(sock, it, yield[ec]);
        if (!BEAST_EXPECTS(!ec, ec.message()))
            return;

        boost::beast::websocket::stream<boost::asio::ip::tcp::socket&> ws{sock};
        ws.handshake(ip + ":" + std::to_string(port), "/");

        // An unprivileged connection from this address is closed instead; see
        // testWSUnparsableFrames.
        overloadEndpoint(env, ip);

        // The privileged connection is answered every time, holding an exempt entry.
        for (int i = 0; i < 3; ++i)
        {
            ws.async_write_some(true, buffer(std::string_view{"NOT JSON"}), yield[ec]);
            if (!BEAST_EXPECTS(!ec, ec.message()))
                return;

            boost::beast::multi_buffer sb;
            ws.async_read(sb, yield[ec]);
            if (!BEAST_EXPECTS(!ec, ec.message()))
                return;

            json::Value resp;
            BEAST_EXPECT(
                json::Reader{}.parse(
                    boost::lexical_cast<std::string>(boost::beast::make_printable(sb.data())),
                    resp));
            BEAST_EXPECT(resp[jss::error] == "jsonInvalid");
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
        BEAST_EXPECT(resp.result() == kOk);
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
        BEAST_EXPECT(resp.result() == kOk);
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
        BEAST_EXPECT(resp.result() == kOk);
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
        BEAST_EXPECT(resp.result() == kOk);
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
        BEAST_EXPECT(resp.result() == kOk);
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

        // Only a body the reader could not parse has a reason to append after the colon.
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doHTTPRequest(env, yield, false, resp, ec, "{]");
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body().starts_with("Unable to parse request: "));
            BEAST_EXPECT(
                resp.body().size() > std::string_view{"Unable to parse request: \r\n"}.size());
        }

        // The size limit is checked before the parse, so an oversized body is never read.
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doHTTPRequest(
                env, yield, false, resp, ec, std::string(rpc::tuning::kMaxRequestSize + 1, 'x'));
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "Request is too large\r\n");
        }

        // A whitespace-only body is a parse failure, not an empty document: the reader records a
        // reason for it, so there is text after the colon. An empty body is not sent here: this
        // client sends a body-less request as a GET, which the status page answers instead.
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doHTTPRequest(env, yield, false, resp, ec, "  \n");
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body().starts_with("Unable to parse request: "));
            BEAST_EXPECT(
                resp.body().size() > std::string_view{"Unable to parse request: \r\n"}.size());
        }

        // `null` parses to a document that carries nothing, like the empty object below.
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doHTTPRequest(env, yield, false, resp, ec, "null");
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "Request is empty\r\n");
        }

        // An empty object parses, so it is not a parse failure; it simply carries no request.
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            doHTTPRequest(env, yield, false, resp, ec, "{}");
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "Request is empty\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            json::Value jv;
            jv["invalid"] = 1;
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "Null method\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            json::Value jv(json::ValueType::Array);
            jv.append("invalid");
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "Request is not a JSON object\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            json::Value jv(json::ValueType::Array);
            json::Value j;
            j["invalid"] = 1;
            jv.append(j);
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "Request is not a JSON object\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            json::Value jv;
            jv[jss::method] = "batch";
            jv[jss::params] = 2;
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "Malformed batch request\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            json::Value jv;
            jv[jss::method] = "batch";
            jv[jss::params] = json::ValueType::Object;
            jv[jss::params]["invalid"] = 3;
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "Malformed batch request\r\n");
        }

        json::Value jv;
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            jv[jss::method] = json::ValueType::Null;
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "Null method\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            jv[jss::method] = 1;
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "method is not string\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            jv[jss::method] = "";
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "method is empty\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            jv[jss::method] = "some_method";
            jv[jss::params] = "params";
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "params unparsable\r\n");
        }

        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = "not an object";
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "params unparsable\r\n");
        }
    }

    /**
     * Builds a request carrying the `ripplerpc` parameter, which selects the
     * reply envelope.
     *
     * @param method The method the request names.
     * @param version The `ripplerpc` value. Empty omits the field entirely.
     * @return The request, serialized.
     */
    static std::string
    makeRippleRpcRequest(std::string_view method, std::string_view version)
    {
        json::Value jv;
        jv[jss::method] = method;
        jv[jss::params] = json::ValueType::Array;
        json::Value params(json::ValueType::Object);
        if (!version.empty())
            params[jss::ripplerpc] = version;
        jv[jss::params][0u] = params;
        return to_string(jv);
    }

    /**
     * An overloaded endpoint sheds a request unless the connection holds a
     * privileged role, whose resource entry is exempt.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testPrivilegedRequestIsNotShed(boost::asio::yield_context& yield)
    {
        testcase("A privileged request is not shed from an overloaded endpoint");

        using namespace test::jtx;

        boost::system::error_code ec;

        json::Value ping;
        ping[jss::method] = "ping";

        // Without privilege the request is shed, which is what the case below contrasts.
        {
            Env env{*this, envconfig(noAdmin)};
            auto const ip = env.app().config()[Sections::kPortRpc].get<std::string>(Keys::kIp);
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            overloadEndpoint(env, *ip);

            Response resp;
            doHTTPRequest(env, yield, false, resp, ec, to_string(ping));
            BEAST_EXPECT(resp.result() == boost::beast::http::status::service_unavailable);
            BEAST_EXPECT(resp.body() == "Server is overloaded\r\n");
        }

        // With privilege it is answered, the connection holding an exempt resource entry.
        {
            Env env{*this};
            auto const ip = env.app().config()[Sections::kPortRpc].get<std::string>(Keys::kIp);
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            overloadEndpoint(env, *ip);

            Response resp;
            auto const reply = postAndParse(env, yield, resp, ec, to_string(ping));
            BEAST_EXPECT(resp.result() == kOk);
            BEAST_EXPECT(reply[jss::result][jss::status] == jss::success);
        }
    }

    /**
     * `ripplerpc` is refused when it is not a string and echoed back at every
     * version the server serves. An entry naming an `api_version` the server
     * cannot serve is returned under `request`.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testRipplerpcVersions(boost::asio::yield_context& yield)
    {
        testcase("RPC client sends assorted ripplerpc versions");

        using namespace test::jtx;
        Env env{*this};

        boost::system::error_code ec;

        // A value naming no supported version is rejected rather than coerced.
        for (auto const* version : {
                 "0.0",                            // unsupported major
                 "4.0",                            //
                 "10.0",                           // would sort below "3.0" lexicographically
                 "02.0",                           // leading zero
                 "2.00",                           // non-canonical minor
                 "2",                              // missing minor
                 "2.",                             //
                 ".0",                             // missing major
                 "2.x",                            // non-numeric minor
                 "2x",                             // trailing junk
                 "x2",                             //
                 "abc",                            //
                 "-1.0",                           // signs are not part of a version
                 "+2.0",                           //
                 " 2.0",                           // surrounding whitespace
                 "2.0 ",                           //
                 "2.0.0",                          // patch component is not accepted
                 "2e0",                            //
                 "2.7",                            // no version defines a nonzero minor
                 "2.99",                           //
                 "3.99",                           //
                 "2.01",                           // leading zero in the minor
                 "1.4294967295",                   //
                 "999999999999999999999999999.0",  // too long to name a version
                 "1.999999999999999999999999999",  // too long to name a version
             })
        {
            Response resp;
            doHTTPRequest(env, yield, false, resp, ec, makeRippleRpcRequest("ping", version));
            BEAST_EXPECTS(resp.result() == kBadRequest, version);
            BEAST_EXPECTS(resp.body() == "ripplerpc is not a supported version\r\n", version);
        }

        // A non-string is reported separately, before any version parsing.
        {
            Response resp;
            json::Value jv;
            jv[jss::method] = "ping";
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = json::ValueType::Object;
            jv[jss::params][0u][jss::ripplerpc] = 2;
            doHTTPRequest(env, yield, false, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kBadRequest);
            BEAST_EXPECT(resp.body() == "ripplerpc is not a string\r\n");
        }

        // Each supported version is accepted and echoed back.
        for (auto const version :
             {rpc::kRippleRpcVersion1, rpc::kRippleRpcVersion2, rpc::kRippleRpcVersion3})
        {
            Response resp;
            auto const reply =
                postAndParse(env, yield, resp, ec, makeRippleRpcRequest("ping", version), version);
            BEAST_EXPECTS(resp.result() == kOk, version);
            BEAST_EXPECTS(reply[jss::ripplerpc] == version, version);
            BEAST_EXPECTS(reply[jss::result][jss::status] == jss::success, version);
        }

        // Every version reports success the same way, so only the error envelope
        // distinguishes them. `account_info` without an account fails in the handler,
        // carrying an `error_code` that version 3 maps onto an HTTP status.
        {
            Response resp;
            doHTTPRequest(
                env,
                yield,
                false,
                resp,
                ec,
                makeRippleRpcRequest("account_info", rpc::kRippleRpcVersion1));
            BEAST_EXPECT(resp.result() == kOk);

            json::Value reply;
            BEAST_EXPECT(json::Reader{}.parse(resp.body(), reply));
            BEAST_EXPECT(reply[jss::result][jss::status] == jss::error);
            BEAST_EXPECT(reply[jss::result].isMember(jss::error_message));
            BEAST_EXPECT(reply[jss::result].isMember(jss::request));
            BEAST_EXPECT(!reply.isMember(jss::error));
        }

        // Versions 2 and 3 share an error envelope, so one loop covers both.
        for (auto const version : {rpc::kRippleRpcVersion2, rpc::kRippleRpcVersion3})
        {
            Response resp;
            auto const reply = postAndParse(
                env, yield, resp, ec, makeRippleRpcRequest("account_info", version), version);
            BEAST_EXPECTS(reply[jss::error][jss::status] == jss::error, version);
            BEAST_EXPECTS(reply[jss::error].isMember(jss::message), version);
            BEAST_EXPECTS(!reply[jss::error].isMember(jss::error_message), version);
            BEAST_EXPECTS(!reply[jss::error].isMember(jss::request), version);
            BEAST_EXPECTS(!reply.isMember(jss::request), version);

            auto const expected = version == rpc::kRippleRpcVersion3 ? kBadRequest : kOk;
            BEAST_EXPECTS(resp.result() == expected, version);
        }

        // A batch entry rejected before dispatch is echoed back, so it is masked too.
        {
            Response resp;
            json::Value entry(json::ValueType::Object);
            entry[jss::method] = "ping";
            entry[jss::ripplerpc] = "10.0";
            for (auto const field : rpc::kCredentialFields)
                entry[std::string{field}] = "sensitive";

            json::Value jv;
            jv[jss::method] = "batch";
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = entry;
            auto const reply = postAndParse(env, yield, resp, ec, to_string(jv));
            BEAST_EXPECT(reply.isArray() && reply.size() == 1);
            for (auto const field : rpc::kCredentialFields)
                BEAST_EXPECTS(reply[0u][std::string{field}] == "<masked>", std::string{field});
        }

        // A rejected entry does not decide the version for the rest, and the batch returns 200.
        {
            Response resp;
            json::Value jv;
            jv[jss::method] = "batch";
            jv[jss::params] = json::ValueType::Array;
            for (auto const& [index, version] :
                 {std::pair{0u, "2.0"}, std::pair{1u, "abc"}, std::pair{2u, "1.0"}})
            {
                json::Value entry(json::ValueType::Object);
                entry[jss::method] = "ping";
                entry[jss::ripplerpc] = version;
                entry[jss::id] = index;
                jv[jss::params][index] = entry;
            }
            auto const reply = postAndParse(env, yield, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kOk);
            BEAST_EXPECT(reply.isArray() && reply.size() == 3);
            BEAST_EXPECT(reply[0u][jss::result][jss::status] == jss::success);
            BEAST_EXPECT(reply[2u][jss::result][jss::status] == jss::success);
            // A version the server cannot honor is a bad parameter, not a missing method.
            BEAST_EXPECT(
                reply[1u][jss::error][jss::error][jss::message] ==
                "ripplerpc is not a supported version");
            BEAST_EXPECT(reply[1u][jss::error][jss::error][jss::code] == -32602);
            BEAST_EXPECT(reply[1u][jss::ripplerpc] == "abc");
        }

        // An entry naming an `api_version` the server cannot serve is returned under `request`,
        // rather than spliced beside the error the way other rejections are.
        {
            Response resp;
            json::Value entry(json::ValueType::Object);
            entry[jss::method] = "ping";
            entry[jss::api_version] = 99u;
            entry[jss::secret] = "sensitive";

            json::Value jv;
            jv[jss::method] = "batch";
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = entry;
            auto const reply = postAndParse(env, yield, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kOk);
            BEAST_EXPECT(reply.isArray() && reply.size() == 1);

            auto const& echoed = reply[0u][jss::request];
            BEAST_EXPECT(echoed[jss::method] == "ping");
            BEAST_EXPECT(echoed[jss::api_version] == 99u);
            BEAST_EXPECT(echoed[jss::secret] == "<masked>");
            BEAST_EXPECT(!reply[0u].isMember(jss::method));

            auto const& error = reply[0u][jss::error][jss::error];
            BEAST_EXPECT(error[jss::code] == rpc::kJsonRpcWrongVersion);
            BEAST_EXPECT(error[jss::message] == jss::invalid_API_version);
        }
    }

    /**
     * Two `method: "batch"` entry rejections answer the shape shipped versions
     * report. A null `method` reports `-32601`, which the specification would
     * call an invalid request rather than a method it could not find; a lone
     * request is answered with text, so only an entry carries the code at all.
     * An entry that is not an object is echoed under `request` with the same
     * code and `Method not found`.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testLegacyBatchEntryRejections(boost::asio::yield_context& yield)
    {
        testcase("A rejected batch entry reports the code shipped versions report");

        using namespace test::jtx;
        Env env{*this};
        boost::system::error_code ec;

        // A null `method`: the entry is echoed with the error beside its own members.
        {
            json::Value entry(json::ValueType::Object);
            entry[jss::method] = json::ValueType::Null;

            json::Value jv;
            jv[jss::method] = "batch";
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = entry;

            Response resp;
            auto const reply = postAndParse(env, yield, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kOk);
            BEAST_EXPECT(reply.isArray() && reply.size() == 1);

            auto const& error = reply[0u][jss::error][jss::error];
            BEAST_EXPECT(error[jss::code] == rpc::kJsonRpcMethodNotFound);
            BEAST_EXPECT(error[jss::code] == -32601);
            BEAST_EXPECT(error[jss::message] == "Null method");
        }

        // An entry that is not an object: echoed under `request`, having no members of its own.
        {
            json::Value jv;
            jv[jss::method] = "batch";
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = 7;

            Response resp;
            auto const reply = postAndParse(env, yield, resp, ec, to_string(jv));
            BEAST_EXPECT(resp.result() == kOk);
            BEAST_EXPECT(reply.isArray() && reply.size() == 1);
            BEAST_EXPECT(reply[0u][jss::request] == 7);

            auto const& error = reply[0u][jss::error][jss::error];
            BEAST_EXPECT(error[jss::code] == rpc::kJsonRpcMethodNotFound);
            BEAST_EXPECT(error[jss::message] == "Method not found");
        }
    }

    /**
     * What a client receives cannot depend on how the server logs.
     *
     * Reading an absent member off a non-const value inserts it as an explicit
     * null, so reading `error_message` after the rename to `message` puts
     * `"error_message": null` back into the reply. The Env below logs at debug,
     * which is what arms these assertions.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testAnErrorReplyDoesNotFollowTheLogLevel(boost::asio::yield_context& yield)
    {
        testcase("An error reply is the same at every log level");

        using namespace test::jtx;
        Env env{*this, envconfig(), nullptr, beast::Severity::Debug};

        boost::system::error_code ec;

        // `account_info` naming no account fails in the handler, where both members come from.
        for (auto const version : {rpc::kRippleRpcVersion2, rpc::kRippleRpcVersion3})
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            auto const reply = postAndParse(
                env, yield, resp, ec, makeRippleRpcRequest("account_info", version), version);
            BEAST_EXPECTS(reply[jss::error].isMember(jss::message), version);
            BEAST_EXPECTS(!reply[jss::error].isMember(jss::error_message), version);
        }
    }

    /**
     * A batch charges for every entry and stops once the connection cannot take
     * another. The `method: "batch"` form is uncapped, so without both one body
     * buys around 333,000 entries' worth of work and reply.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testBatchOverload(boost::asio::yield_context& yield)
    {
        testcase("A batch charges every entry and stops when overloaded");

        using namespace test::jtx;

        // Without admin privilege, since a privileged connection is never charged and never
        // dropped.
        Env env{*this, envconfig(noAdmin)};

        boost::system::error_code ec;

        // Posts `count` copies of `entry` as one `method: "batch"` body and returns the answers.
        auto const answers = [&](json::Value const& entry, unsigned count) -> json::Value {
            json::Value batch;
            batch[jss::method] = "batch";
            batch[jss::params] = json::ValueType::Array;
            for (unsigned i = 0; i < count; ++i)
                batch[jss::params][i] = entry;

            Response resp;
            auto const reply = postAndParse(env, yield, resp, ec, to_string(batch));
            BEAST_EXPECT(resp.result() == kOk);
            BEAST_EXPECT(reply.isArray());
            return reply;
        };

        // One shape per rejection that answers an entry without dispatching it.
        json::Value const nullMethod(json::ValueType::Object);
        json::Value const notAnObject{7};
        json::Value badVersion(json::ValueType::Object);
        badVersion[jss::api_version] = 99u;
        json::Value adminOnly(json::ValueType::Object);
        adminOnly[jss::method] = "ledger_accept";

        auto const shapes = {nullMethod, notAnObject, badVersion, adminOnly};
        unsigned const kEntries = 10;

        // The entry is keyed by address, so an entry from this client is charged against it.
        auto usage = env.app().getResourceManager().newInboundEndpoint(
            beast::ip::Endpoint::fromString(getEnvLocalhostAddr()));

        for (auto const& entry : shapes)
        {
            auto const before = usage.balance();
            auto const reply = answers(entry, kEntries);

            BEAST_EXPECT(reply.size() == kEntries);
            BEAST_EXPECT(usage.balance() > before);
        }

        // Over the threshold the batch stops at the entry it cannot serve, so the reply is shorter
        // than the request.
        for (auto const& entry : shapes)
        {
            overloadEndpoint(env, getEnvLocalhostAddr());

            auto const reply = answers(entry, kEntries);
            BEAST_EXPECT(reply.size() >= 1);
            BEAST_EXPECT(reply.size() < kEntries);
        }

        // An entry rejected after its consumer exists reaches the drop check, so it is answered
        // "Server is overloaded" and nothing follows.
        {
            overloadEndpoint(env, getEnvLocalhostAddr());

            auto const reply = answers(adminOnly, kEntries);
            BEAST_EXPECT(reply.size() == 1);
            BEAST_EXPECT(reply[0u][jss::error][jss::error][jss::message] == "Server is overloaded");
        }
    }

    /**
     * A body the server rejects before it reads a request out of it costs the
     * sender what a malformed request costs.
     *
     * An oversized body costs a megabyte of transfer, is refused before it is
     * parsed and answers in a few bytes, so a client sending nothing else
     * exhausts its allowance.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testUnreadBodiesAreCharged(boost::asio::yield_context& yield)
    {
        testcase("A body rejected before it is read is charged for");

        using namespace test::jtx;

        // Without admin privilege, since a privileged connection is never charged.
        Env env{*this, envconfig(noAdmin)};

        boost::system::error_code ec;

        // The connection's resource entry, keyed by address, which is the one a body from this
        // client is charged against.
        auto usage = env.app().getResourceManager().newInboundEndpoint(
            beast::ip::Endpoint::fromString(getEnvLocalhostAddr()));

        // The five conditions that reject a body before any of it is read as a request. A scalar is
        // not among them: the reader admits only null, an array or an object at the root.
        json::Value malformedBatch(json::ValueType::Object);
        malformedBatch[jss::method] = "batch";

        struct Case
        {
            char const* label;
            std::string body;
        };

        auto const cases = {
            Case{.label = "too large", .body = std::string(rpc::tuning::kMaxRequestSize + 1, 'x')},
            Case{.label = "unparsable", .body = "{"},
            Case{.label = "empty document", .body = "{}"},
            Case{.label = "not an object", .body = "[1,2,3]"},
            Case{.label = "malformed batch", .body = to_string(malformedBatch)},
        };

        // Answers each body and returns what it cost the connection.
        auto const costOf = [&](char const* label, std::string const& body) {
            auto const before = usage.balance();

            Response resp;
            doHTTPRequest(env, yield, false, resp, ec, body);
            BEAST_EXPECTS(resp.result() == kBadRequest, label);
            return usage.balance() - before;
        };

        for (auto const& [label, body] : cases)
            BEAST_EXPECTS(costOf(label, body) > 0, label);

        // What one such body costs is the malformed-request charge and nothing else, at any load.
        // Asking whether the connection is over the drop threshold is itself a charge, so a body
        // answered before the entry loop must not ask, and one body cannot cost what a drop costs.
        overloadEndpoint(env, getEnvLocalhostAddr());

        for (auto const& [label, body] : cases)
            BEAST_EXPECTS(costOf(label, body) <= resource::kFeeMalformedRpc.cost(), label);

        // A lone request naming an `api_version` the server cannot serve is charged inside the
        // entry loop, which asks about the drop threshold so a batch can stop at the entry that
        // crossed it. A lone request has no entry after it, so asking would cost it a drop charge
        // for nothing.
        {
            json::Value jv;
            jv[jss::method] = "ping";
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = json::ValueType::Object;
            jv[jss::params][0u][jss::api_version] = 99u;

            BEAST_EXPECT(
                costOf("lone invalid api_version", to_string(jv)) <=
                resource::kFeeMalformedRpc.cost());
        }
    }

    /**
     * A body a privileged connection sends is not charged for.
     *
     * The charge is keyed on the resource entry a request's own credentials
     * select, so a connection the port grants admin is charged against an
     * exempt entry.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testPrivilegedBodiesAreNotCharged(boost::asio::yield_context& yield)
    {
        testcase("A body a privileged connection sends is not charged for");

        using namespace test::jtx;

        // With admin privilege, which the default configuration grants to a local connection.
        Env env{*this};

        boost::system::error_code ec;

        // The address entry, which is the one an unprivileged body is charged against. A privileged
        // connection is charged against an exempt entry instead, so this balance must not move.
        auto usage = env.app().getResourceManager().newInboundEndpoint(
            beast::ip::Endpoint::fromString(getEnvLocalhostAddr()));
        auto const before = usage.balance();

        for (auto const* body : {"{", "{}", "7"})
        {
            Response resp;
            doHTTPRequest(env, yield, false, resp, ec, body);
            BEAST_EXPECTS(resp.result() == kBadRequest, body);
        }

        BEAST_EXPECT(usage.balance() == before);
    }

    /**
     * Header-assigned values belong to the connection, not to one entry of a
     * batch, so an entry whose own role is neither identified nor proxied
     * must not clear them for the entries after it.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testBatchIdentity(boost::asio::yield_context& yield)
    {
        testcase("A batch entry keeps the connection's identity");

        using namespace test::jtx;

        // Both an admin net and a secure gateway, so one entry can be admin while the next is
        // identified by the header.
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg)[Sections::kPortRpc].set(Keys::kAdminUser, "u");
                    (*cfg)[Sections::kPortRpc].set(Keys::kAdminPassword, "p");
                    (*cfg)[Sections::kPortRpc].set(Keys::kSecureGateway, getEnvLocalhostAddr());
                    return cfg;
                })};

        boost::system::error_code ec;

        MyFields fields;
        fields.insert("X-User", "xrposhi");
        fields.insert("X-Forwarded-For", "203.0.113.9");

        // The first entry is admin, which is the role the clearing keys off; the second presents
        // no credentials and keeps the connection's `X-User` and forwarded-for address.
        json::Value credentials(json::ValueType::Object);
        credentials["admin_user"] = "u";
        credentials["admin_password"] = "p";

        json::Value batch;
        batch[jss::method] = "batch";
        batch[jss::params] = json::ValueType::Array;
        batch[jss::params][0u][jss::method] = "ping";
        batch[jss::params][0u][jss::params] = json::ValueType::Array;
        batch[jss::params][0u][jss::params][0u] = credentials;
        batch[jss::params][1u][jss::method] = "ping";

        Response resp;
        auto const reply = postAndParse(env, yield, resp, ec, to_string(batch), {}, fields);
        BEAST_EXPECT(resp.result() == kOk);
        BEAST_EXPECT(reply.isArray() && reply.size() == 2);

        BEAST_EXPECT(reply[0u][jss::result][jss::role] == "admin");

        // The second entry reports what it reports as a request of its own.
        auto const& ping = reply[1u][jss::result];
        BEAST_EXPECT(ping[jss::role] == "identified");
        BEAST_EXPECT(ping["username"] == "xrposhi");
        BEAST_EXPECT(ping[jss::ip] == "203.0.113.9");
    }

    /**
     * A request may present its parameters as the object a handler reads or as
     * an array holding that object. Both forms reach every API version, and
     * the version is read from the object either way.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testRequestForms(boost::asio::yield_context& yield)
    {
        testcase("A request may name its parameters either way");

        using namespace test::jtx;
        Env env{*this};

        boost::system::error_code ec;

        // The by-name form reaches the handler at every version: `actMalformed` shows
        // `account_info` was given the account, where absent parameters report one missing.
        {
            json::Value jv;
            jv[jss::method] = "account_info";
            jv[jss::params] = json::ValueType::Object;
            jv[jss::params][jss::account] = "bogus";

            BEAST_EXPECT(answer(env, yield, ec, jv)[jss::result][jss::error] == "actMalformed");
        }

        // The version is read from the by-name object too. `tx_history` is absent from version 2,
        // so it answers `unknownCmd` there; `start` is required, and `index` is what the handler
        // writes when it runs.
        auto const txHistory = [](unsigned apiVersion) {
            json::Value jv;
            jv[jss::method] = "tx_history";
            jv[jss::params] = json::ValueType::Object;
            jv[jss::params][jss::start] = 0u;
            jv[jss::params][jss::api_version] = apiVersion;
            return jv;
        };
        auto const ran = [](json::Value const& reply) {
            return reply[jss::result].isMember(jss::index) &&
                !reply[jss::result].isMember(jss::error);
        };

        BEAST_EXPECT(ran(answer(env, yield, ec, txHistory(1))));
        BEAST_EXPECT(answer(env, yield, ec, txHistory(2))[jss::result][jss::error] == "unknownCmd");

        // A `method: "batch"` entry carrying a by-name object is read for its version from that
        // object as well. The entry is itself the object a handler reads, so `start` sits at its
        // top level. Each version gets a body of its own, both entries naming it.
        {
            auto const entry = [](unsigned apiVersion) {
                json::Value jv;
                jv[jss::method] = "tx_history";
                jv[jss::start] = 0u;
                jv[jss::params] = json::ValueType::Object;
                jv[jss::params][jss::api_version] = apiVersion;
                return jv;
            };

            for (auto const apiVersion : {1u, 2u})
            {
                json::Value batch;
                batch[jss::method] = "batch";
                batch[jss::params] = json::ValueType::Array;
                batch[jss::params][0u] = entry(apiVersion);
                batch[jss::params][1u] = entry(apiVersion);

                auto const reply = answer(env, yield, ec, batch);
                BEAST_EXPECT(reply.isArray() && reply.size() == 2);
                for (auto const i : {0u, 1u})
                {
                    BEAST_EXPECTS(
                        apiVersion == 1 ? ran(reply[i])
                                        : reply[i][jss::result][jss::error] == "unknownCmd",
                        to_string(reply));
                }
            }
        }

        // Credentials are read from the by-name object too, for a lone request and for a batch
        // entry. The port names an admin user and password, so the role `ping` reports says whether
        // they were read: the right pair is admin, the wrong one is a guest, which reports no role.
        {
            Env admin{*this, envconfig([](std::unique_ptr<Config> cfg) {
                          (*cfg)[Sections::kPortRpc].set(Keys::kAdminUser, "u");
                          (*cfg)[Sections::kPortRpc].set(Keys::kAdminPassword, "p");
                          return cfg;
                      })};

            auto const ping = [](char const* password) {
                json::Value jv;
                jv[jss::method] = "ping";
                jv[jss::params] = json::ValueType::Object;
                jv[jss::params]["admin_user"] = "u";
                jv[jss::params]["admin_password"] = password;
                return jv;
            };

            BEAST_EXPECT(answer(admin, yield, ec, ping("p"))[jss::result][jss::role] == "admin");
            BEAST_EXPECT(!answer(admin, yield, ec, ping("x"))[jss::result].isMember(jss::role));

            json::Value batch;
            batch[jss::method] = "batch";
            batch[jss::params] = json::ValueType::Array;
            batch[jss::params][0u] = ping("p");
            batch[jss::params][1u] = ping("x");

            auto const reply = answer(admin, yield, ec, batch);
            BEAST_EXPECT(reply.isArray() && reply.size() == 2);
            BEAST_EXPECT(reply[0u][jss::result][jss::role] == "admin");
            BEAST_EXPECT(!reply[1u][jss::result].isMember(jss::role));
        }
    }

    /**
     * A request may name its API version beside its method, as an XRPL
     * extension placed alongside the members the specification defines, as
     * well as inside its parameters. A value beside the method is honored from
     * version 3 up, and the value with the parameters decides when both are
     * present.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testTopLevelVersion(boost::asio::yield_context& yield)
    {
        testcase("A request may name its version beside its method");

        using namespace test::jtx;
        Env env{*this};

        boost::system::error_code ec;

        // The top level is read as well as the parameters, but only a specification version is
        // honored there. `tx_history` is absent from version 2, so it answers `unknownCmd` there.
        auto const txHistory = [](std::optional<unsigned> atTopLevel,
                                  std::optional<unsigned> inParams) {
            json::Value jv;
            jv[jss::method] = "tx_history";
            if (atTopLevel)
                jv[jss::api_version] = *atTopLevel;

            // `start` is required, so without it the reply says nothing about the handler.
            json::Value params(json::ValueType::Object);
            params[jss::start] = 0u;
            if (inParams)
                params[jss::api_version] = *inParams;
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = params;
            return jv;
        };

        // An absent `unknownCmd` proves nothing, a reply carrying no `result` lacking it too.
        // `index` is what the handler writes; `txs` is null when there is nothing to report.
        auto const ran = [](json::Value const& reply) {
            return reply[jss::result].isMember(jss::index) &&
                !reply[jss::result].isMember(jss::error);
        };

        BEAST_EXPECT(
            answer(env, yield, ec, txHistory(std::nullopt, 2))[jss::result][jss::error] ==
            "unknownCmd");
        BEAST_EXPECT(ran(answer(env, yield, ec, txHistory(2, std::nullopt))));

        // A version the server cannot serve is ignored there too, rather than rejecting a request
        // that is otherwise answerable.
        BEAST_EXPECT(ran(answer(env, yield, ec, txHistory(99, std::nullopt))));

        // Version 3 is honored there. `tx_history` is absent from it as well, so the handler that
        // ran for the version 1 answers above is not reached.
        BEAST_EXPECT(
            answer(env, yield, ec, txHistory(3, std::nullopt))[jss::result][jss::error] ==
            "unknownCmd");

        // Naming a version in both places, the one with the parameters decides. A `method: "batch"`
        // entry is itself the object a handler reads, so `start` sits at its top level and the
        // version in its `params`.
        {
            json::Value entry;
            entry[jss::method] = "tx_history";
            entry[jss::api_version] = 2u;
            entry[jss::start] = 0u;
            entry[jss::params] = json::ValueType::Array;
            entry[jss::params][0u] = json::ValueType::Object;
            entry[jss::params][0u][jss::api_version] = 1u;

            json::Value batch;
            batch[jss::method] = "batch";
            batch[jss::params] = json::ValueType::Array;
            batch[jss::params][0u] = entry;

            auto const reply = answer(env, yield, ec, batch);
            BEAST_EXPECT(reply.isArray() && reply.size() == 1);
            BEAST_EXPECT(ran(reply[0u]));
        }
    }

    /**
     * The five handlers that report a bare token carry a code and message with
     * it.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testHandlerErrorsCarryCodes(boost::asio::yield_context& yield)
    {
        testcase("Handler errors carry a code and message");

        using namespace test::jtx;
        Env env{*this};

        boost::system::error_code ec;

        // Both `ripplerpc` envelopes need all three present: version 3 derives the HTTP status
        // from `error_code`, and version 2 copies the pair into `code` and `message`, so a missing
        // field leaves the reply claiming success or carrying an explicit null. Expected values are
        // literals, so a row changing under a client turns this red.
        struct Case
        {
            std::string_view method;
            std::string_view token;
            ErrorCodeI code;
            std::string_view message;
            unsigned status;
        };
        for (auto const& [method, token, code, message, httpStatus] : {
                 Case{
                     .method = "transaction_entry",
                     .token = "fieldNotFoundTransaction",
                     .code = RpcFieldNotFoundTransaction,
                     .message = "Missing field 'tx_hash'.",
                     .status = 400},
                 Case{
                     .method = "vault_info",
                     .token = "invalidParams",
                     .code = RpcInvalidParams,
                     .message = "Must specify either 'vault_id' or both 'owner' and 'seq'.",
                     .status = 400},
                 // Reached only below API version 2; see the api_version case.
                 Case{
                     .method = "ledger_entry",
                     .token = "unknownOption",
                     .code = RpcUnknownOption,
                     .message = "Unknown option.",
                     .status = 400},
             })
        {
            auto const status = static_cast<boost::beast::http::status>(httpStatus);
            auto const label = std::string{method};

            // Version 2 must report a real code and message, never null.
            {
                Response resp;
                auto const reply = postAndParse(
                    env,
                    yield,
                    resp,
                    ec,
                    makeRippleRpcRequest(method, rpc::kRippleRpcVersion2),
                    label);
                auto const& error = reply[jss::error];
                BEAST_EXPECTS(error[jss::error] == token, label);
                BEAST_EXPECTS(error[jss::error_code] == code, label);
                BEAST_EXPECTS(error[jss::code] == code, label);
                BEAST_EXPECTS(error[jss::message] == message, label);
                // Version 2 always answers 200, whatever the error code.
                BEAST_EXPECTS(resp.result() == kOk, label);
            }

            // Version 3 maps that code onto the HTTP status.
            {
                Response resp;
                doHTTPRequest(
                    env,
                    yield,
                    false,
                    resp,
                    ec,
                    makeRippleRpcRequest(method, rpc::kRippleRpcVersion3));
                BEAST_EXPECTS(resp.result() == status, label);
            }

            // Version 1 keeps the token and gains a message.
            {
                Response resp;
                auto const reply = postAndParse(
                    env,
                    yield,
                    resp,
                    ec,
                    makeRippleRpcRequest(method, rpc::kRippleRpcVersion1),
                    label);
                auto const& result = reply[jss::result];
                BEAST_EXPECTS(result[jss::error] == token, label);
                BEAST_EXPECTS(result[jss::error_code] == code, label);
                BEAST_EXPECTS(result[jss::error_message] == message, label);
            }
        }

        // `ledger_entry` reports `unknownOption` below API version 2 and `invalidParams` from
        // version 2 onwards. Both carry a code, so both select an HTTP status under ripplerpc 3.
        for (auto const apiVersion : {1u, 2u})
        {
            Response resp;
            json::Value jv;
            jv[jss::method] = "ledger_entry";
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = json::ValueType::Object;
            jv[jss::params][0u][jss::ripplerpc] = rpc::kRippleRpcVersion3;
            jv[jss::params][0u][jss::api_version] = apiVersion;

            // Literals again, for the reason the case table above gives.
            auto const expected = apiVersion < 2 ? RpcUnknownOption : RpcInvalidParams;
            auto const token = apiVersion < 2 ? "unknownOption" : "invalidParams";

            auto const label = std::to_string(apiVersion);
            auto const reply = postAndParse(env, yield, resp, ec, to_string(jv), label);
            BEAST_EXPECTS(reply[jss::error][jss::error] == token, label);
            BEAST_EXPECTS(reply[jss::error][jss::error_code] == expected, label);
            BEAST_EXPECTS(resp.result() == kBadRequest, label);
        }

        // `submit` reports `invalidTransaction` with a coded error, and adds `error_exception`
        // carrying the underlying failure detail.
        {
            Response resp;
            json::Value jv;
            jv[jss::method] = "submit";
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = json::ValueType::Object;
            jv[jss::params][0u][jss::tx_blob] = "DEADBEEF";
            jv[jss::params][0u][jss::ripplerpc] = rpc::kRippleRpcVersion3;
            auto const reply = postAndParse(env, yield, resp, ec, to_string(jv));
            auto const& error = reply[jss::error];
            BEAST_EXPECT(error[jss::error] == "invalidTransaction");
            BEAST_EXPECT(error[jss::error_code] == RpcInvalidTransaction);
            BEAST_EXPECT(error[jss::message] == "Transaction is invalid.");
            BEAST_EXPECT(!error[jss::error_exception].asString().empty());
            BEAST_EXPECT(resp.result() == kBadRequest);
        }
    }

    /**
     * The `ripplerpc: "3.0"` envelope reports 200 for the codes below.
     *
     * `account_info` on an account the ledger does not hold is a routine call,
     * and a 4xx there turns a working reply into a failure for anything that
     * fails over on one. The status each code names in the table is pinned in
     * the `ErrorCodes` gtest.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testGainedStatusesStayOffLegacyEnvelope(boost::asio::yield_context& yield)
    {
        testcase("A code that gained an HTTP status keeps 200 on the legacy envelope");

        using namespace test::jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->loadFromString(std::string("[") + Sections::kSigningSupport + "]\ntrue");
                    return cfg;
                })};

        Account const alice{"alice"};
        // Never funded, so the ledger holds no entry for it.
        Account const absent{"absent"};
        env.fund(XRP(10000), alice);
        env.close();

        boost::system::error_code ec;

        // Accepted by the signing checks, so the request reaches the two conditions below.
        // `sign_for` fills nothing in, so the fee and sequence are named here.
        auto const accountSet = [&env, &alice] {
            json::Value tx;
            tx[jss::Account] = alice.human();
            tx[jss::TransactionType] = jss::AccountSet;
            tx[jss::SigningPubKey] = "";
            tx[jss::Fee] = (8 * env.current()->fees().base).jsonClipped();
            tx[jss::Sequence] = env.seq(alice);
            return tx;
        };

        auto const account = [](std::string_view ident) {
            json::Value params(json::ValueType::Object);
            params[jss::account] = ident;
            return params;
        };

        // `sign` single-signs, so a transaction already carrying `Signers` is multisigned.
        json::Value multisigned(json::ValueType::Object);
        multisigned[jss::secret] = toBase58(generateSeed("alice"));
        multisigned[jss::tx_json] = accountSet();
        multisigned[jss::tx_json][jss::Signers] = json::ValueType::Array;

        // `sign_for` multisigns, so a transaction already carrying `TxnSignature` is single-signed.
        json::Value singleSigned(json::ValueType::Object);
        singleSigned[jss::account] = alice.human();
        singleSigned[jss::secret] = toBase58(generateSeed("alice"));
        singleSigned[jss::tx_json] = accountSet();
        singleSigned[jss::tx_json][jss::TxnSignature] = "DEADBEEF";

        struct Case
        {
            char const* method;
            json::Value params;
            ErrorCodeI code;
        };

        for (auto& [method, params, code] : {
                 Case{
                     .method = "account_info", .params = account("bogus"), .code = RpcActMalformed},
                 Case{
                     .method = "account_info",
                     .params = account(absent.human()),
                     .code = RpcActNotFound},
                 Case{.method = "sign", .params = multisigned, .code = RpcAlreadyMultisig},
                 Case{.method = "sign_for", .params = singleSigned, .code = RpcAlreadySingleSig},
             })
        {
            json::Value jv;
            jv[jss::method] = method;
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = params;
            jv[jss::params][0u][jss::ripplerpc] = rpc::kRippleRpcVersion3;

            Response resp;
            auto const& info = rpc::getErrorInfo(code);
            auto const label = std::string{info.token.cStr()};

            auto const reply = postAndParse(env, yield, resp, ec, to_string(jv), label);
            BEAST_EXPECTS(reply[jss::error][jss::error] == info.token.cStr(), label);
            BEAST_EXPECTS(reply[jss::error][jss::error_code] == code, label);
            // The code names a status the envelope declines to report, so the two disagree here
            // by design.
            BEAST_EXPECTS(info.httpStatus != 200, label);
            BEAST_EXPECTS(resp.result() == kOk, label);
        }
    }

    /**
     * Every reply begins with a status line, including one whose status the
     * status-line switch does not spell out. `highFee` reports 402, one of
     * the two statuses the switch names no case for, and a reply that begins
     * with a header instead is not an HTTP response at all.
     *
     * @param yield The coroutine the request runs on.
     */
    void
    testUncommonHttpStatus(boost::asio::yield_context& yield)
    {
        testcase("A reply names an HTTP status the status-line switch does not spell out");

        using namespace test::jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->loadFromString(std::string("[") + Sections::kSigningSupport + "]\ntrue");
                    return cfg;
                })};

        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.close();

        // A fee ceiling of zero covers no fee at all, which is what `highFee` reports. The
        // `ripplerpc: "3.0"` envelope derives the HTTP status from the code.
        json::Value params(json::ValueType::Object);
        params[jss::ripplerpc] = rpc::kRippleRpcVersion3;
        params[jss::secret] = toBase58(generateSeed("alice"));
        params[jss::fee_mult_max] = 0;
        params[jss::tx_json] = pay(alice, bob, XRP(1));

        json::Value jv;
        jv[jss::method] = "sign";
        jv[jss::params] = json::ValueType::Array;
        jv[jss::params][0u] = params;

        Response resp;
        boost::system::error_code ec;
        auto const reply = postAndParse(env, yield, resp, ec, to_string(jv));

        // The reply parsed as a response, which is what a missing status line breaks.
        BEAST_EXPECT(!ec);
        BEAST_EXPECT(resp.result_int() == rpc::errorCodeHttpStatus(RpcHighFee));
        BEAST_EXPECT(resp.result() == boost::beast::http::status::payment_required);
        BEAST_EXPECT(reply[jss::error][jss::error] == "highFee");
    }

    /**
     * A credential the server echoes back is masked, on every path that echoes.
     *
     * Driven from `kCredentialFields` itself, so a field added to the list is
     * covered here.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testMaskedCredentials(boost::asio::yield_context& yield)
    {
        testcase("Echoed requests have their credentials masked");

        using namespace test::jtx;
        Env env{*this};

        boost::system::error_code ec;

        // Version 1 echoes the request back on an error, so its credentials must be masked.
        {
            boost::beast::http::response<boost::beast::http::string_body> resp;
            json::Value jv;
            jv[jss::method] = "sign";
            jv[jss::params] = json::ValueType::Array;
            json::Value params(json::ValueType::Object);
            params[jss::ripplerpc] = rpc::kRippleRpcVersion1;
            for (auto const field : rpc::kCredentialFields)
                params[std::string{field}] = "sensitive";
            jv[jss::params][0u] = params;
            auto const reply = postAndParse(env, yield, resp, ec, to_string(jv));
            auto const& echoed = reply[jss::result][jss::request];
            for (auto const field : rpc::kCredentialFields)
                BEAST_EXPECTS(echoed[std::string{field}] == "<masked>", std::string{field});
        }

        // A request the session rejects before dispatch is echoed back, so it must be masked.
        // Written directly, since the client always names a command.
        {
            auto const port = env.app().config()[Sections::kPortWs].get<std::uint16_t>(Keys::kPort);
            auto const ip = env.app().config()[Sections::kPortWs].get<std::string>(Keys::kIp);

            json::Value jv;
            for (auto const field : rpc::kCredentialFields)
                jv[std::string{field}] = "sensitive";

            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            auto const reply = doWSMessage(yield, *ip, *port, to_string(jv));
            auto const& echoed = reply[jss::request];
            for (auto const field : rpc::kCredentialFields)
                BEAST_EXPECTS(echoed[std::string{field}] == "<masked>", std::string{field});
        }

        // A rejection echoing the whole request must mask the credentials inside
        // `params`, where the JSON-RPC transport carries them.
        {
            json::Value params(json::ValueType::Object);
            params[jss::account] = "rSomeAccount";
            for (auto const field : rpc::kCredentialFields)
                params[std::string{field}] = "sensitive";

            json::Value entry;
            entry[jss::id] = 2;
            entry[jss::params] = json::ValueType::Array;
            entry[jss::params][0u] = params;

            // A batch: a lone request naming no method is answered with a bare message. The
            // `"method": "batch"` form nests entries under `params` on every version.
            json::Value batch;
            batch[jss::method] = "batch";
            batch[jss::params] = json::ValueType::Array;
            batch[jss::params][0u] = entry;

            Response resp;
            auto const reply = postAndParse(env, yield, resp, ec, to_string(batch));
            BEAST_EXPECT(reply.isArray() && reply.size() == 1);

            auto const& echoed = reply[0u][jss::params][0u];
            for (auto const field : rpc::kCredentialFields)
                BEAST_EXPECTS(echoed[std::string{field}] == "<masked>", std::string{field});

            // The rest of the request survives masking; only the credentials are replaced.
            BEAST_EXPECT(echoed[jss::account] == "rSomeAccount");
            BEAST_EXPECT(reply[0u][jss::id] == 2);
        }
    }

    /**
     * The request written to the log is masked, and capped in length.
     *
     * Both are asserted on one rendering: without the mask a seed reaches the
     * log in the clear, and without the cap a client chooses how much it writes
     * there. The marker past the cap is expected nowhere, every site rendering
     * through `rpc::loggable`.
     *
     * `CaptureLogs` assigns its text in the destructor, so the `Env` is scoped
     * and the string read after it.
     *
     * @param yield The coroutine the request runs on.
     */
    void
    testTheLoggedRequestIsMaskedAndCapped(boost::asio::yield_context& yield)
    {
        testcase("The logged request is masked and capped");

        using namespace test::jtx;

        // Sorts after `secret` and before the tail marker, so the cap falls between them.
        static constexpr std::size_t kFillerSize = 12000;
        static constexpr char const* kSensitiveSeed = "snoPBrXtMeMyMHUVTgbuqAfg1SUTb";
        static constexpr char const* kTailMarker = "PastTheCapMarker";

        std::string logs;
        {
            Env env{
                *this, envconfig(), std::make_unique<CaptureLogs>(&logs), beast::Severity::Debug};

            boost::system::error_code ec;
            Response resp;

            json::Value params(json::ValueType::Object);
            params[jss::secret] = kSensitiveSeed;
            params["zfiller"] = std::string(kFillerSize, 'z');
            params["zzztail"] = kTailMarker;

            json::Value jv;
            jv[jss::method] = "ping";
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = params;

            auto const reply = postAndParse(env, yield, resp, ec, to_string(jv));
            BEAST_EXPECT(reply[jss::result][jss::status] == jss::success);
        }

        auto const occurrences = [&logs](std::string_view needle) {
            std::size_t count = 0;
            for (auto at = logs.find(needle); at != std::string::npos;
                 at = logs.find(needle, at + 1))
            {
                ++count;
            }
            return count;
        };

        // The seed reaches no line at all, and the duration line wrote the mask in its place.
        BEAST_EXPECT(occurrences(kSensitiveSeed) == 0);
        BEAST_EXPECT(logs.contains("RPC request processing duration = "));
        BEAST_EXPECT(occurrences("<masked>") >= 2);

        // No line reached the tail marker, because every site renders through `rpc::loggable`.
        BEAST_EXPECT(occurrences(kTailMarker) == 0);
    }

    /**
     * The reply written to the log is masked only when it carries a credential.
     *
     * A `wallet_propose` reply carries the keys it generated, and its `Reply:`
     * line holds `<masked>` in their place. A `server_info` reply carries none
     * and is logged as the string the client received.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testTheLoggedReplyIsMaskedOnlyWhenItCarriesACredential(boost::asio::yield_context& yield)
    {
        testcase("The logged reply is masked only when it carries a credential");

        using namespace test::jtx;

        std::string logs;
        json::Value wallet;
        json::Value info;
        {
            Env env{
                *this, envconfig(), std::make_unique<CaptureLogs>(&logs), beast::Severity::Debug};

            boost::system::error_code ec;
            Response resp;

            json::Value jv;
            jv[jss::method] = "wallet_propose";
            wallet = postAndParse(env, yield, resp, ec, to_string(jv))[jss::result];
            BEAST_EXPECT(wallet[jss::status] == jss::success);

            jv[jss::method] = "server_info";
            info = postAndParse(env, yield, resp, ec, to_string(jv))[jss::result];
            BEAST_EXPECT(info[jss::status] == jss::success);
        }

        BEAST_EXPECT(logs.contains("Reply: "));

        // The keys the client received reach no line, and the mask stands in their place.
        for (auto const field : {jss::master_seed, jss::master_key})
        {
            auto const key = wallet[field].asString();
            BEAST_EXPECT(!key.empty());
            BEAST_EXPECTS(!logs.contains(key), field.cStr());
        }
        BEAST_EXPECT(logs.contains("<masked>"));

        // A reply with no credential is logged as built: one line carries this member as the
        // client read it.
        auto const version = info[jss::info][jss::build_version].asString();
        BEAST_EXPECT(!version.empty());
        BEAST_EXPECT(logs.contains("\"build_version\":\"" + version + "\""));
    }

    /**
     * A handler that throws is reported as `internal`, on both transports.
     *
     * `RPCHandler` catches the throw and injects `internal`, so the `catch`
     * arms in `processSession` and `processRequest` are not what answers this.
     * What this pins is the reply: `ledger_entry` is `Role::USER`, so an
     * anonymous client reaches it.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testInternalErrorIsReportedOnBothTransports(boost::asio::yield_context& yield)
    {
        testcase("An internal error is reported on both transports");

        using namespace test::jtx;

        static constexpr char const* kSensitiveSeed = "snoPBrXtMeMyMHUVTgbuqAfg1SUTb";

        std::string logs;
        {
            Env env{
                *this, envconfig(), std::make_unique<CaptureLogs>(&logs), beast::Severity::Info};
            env.close();

            auto const port = env.app().config()[Sections::kPortWs].get<std::uint16_t>(Keys::kPort);
            auto const ip = env.app().config()[Sections::kPortWs].get<std::string>(Keys::kIp);

            json::Value params(json::ValueType::Object);
            params[jss::ledger_index] = jss::validated;
            params[jss::hashes] = -1;
            params[jss::secret] = kSensitiveSeed;

            json::Value jv;
            jv[jss::method] = "ledger_entry";
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = params;

            boost::system::error_code ec;
            Response resp;
            auto const reply = postAndParse(env, yield, resp, ec, to_string(jv));
            BEAST_EXPECTS(reply[jss::result][jss::error] == "internal", to_string(reply));

            json::Value frame(params);
            frame[jss::command] = "ledger_entry";
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            auto const wsReply = doWSMessage(yield, *ip, *port, to_string(frame));
            BEAST_EXPECTS(wsReply[jss::error] == "internal", to_string(wsReply));
        }

        // The capture is not empty, so the absence assertion below is not vacuous.
        BEAST_EXPECT(logs.contains("Caught throw: "));

        // The line reports what was thrown and never the request.
        BEAST_EXPECT(!logs.contains(kSensitiveSeed));
    }

    /**
     * The credentials the command line client adds are masked in what it
     * prints.
     *
     * The client copies `admin_user` and `admin_password` out of `[port_rpc]`
     * into the request it builds, and prints to stdout rather than to a log.
     *
     * The account is asserted unmasked beside them, so that masking everything
     * does not satisfy this case.
     *
     * `request_sent` is a second echo of the same request and is not exercised
     * here: `fromCommandLine` writes it only on the transport-error path, which
     * no jtx harness drives.
     */
    void
    testCommandLineCredentialsAreMasked()
    {
        testcase("The command line client masks the credentials it adds");

        using namespace test::jtx;

        static constexpr char const* kConfiguredPassword = "correct-horse-battery-staple";

        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg)[Sections::kPortRpc].set(Keys::kAdminUser, "operator");
                    (*cfg)[Sections::kPortRpc].set(Keys::kAdminPassword, kConfiguredPassword);
                    return cfg;
                })};

        // `Env::doRpc` retries an internal error, repeating the request for no gain here.
        env.setRetries(0);

        // Well formed, so the parser accepts it and the server answers `actNotFound`. A malformed
        // account fails in the parser, before there is a sent request to echo.
        auto const output = env.rpc("account_info", Account{"never-funded"}.human());

        // The call reached the server, so the echo below is the request the client built.
        auto const& result = output[jss::result];
        BEAST_EXPECTS(result[jss::error] == "actNotFound", to_string(output));

        // The credentials from the config are masked; the account the operator typed is not.
        BEAST_EXPECT(result[jss::request]["admin_password"] == "<masked>");
        BEAST_EXPECT(result[jss::request]["admin_user"] == "<masked>");
        BEAST_EXPECT(result[jss::request][jss::account] == Account{"never-funded"}.human());

        // The subject of the fix: the configured password reaches no member of the output.
        BEAST_EXPECT(!to_string(output).contains(kConfiguredPassword));
    }

    /**
     * A credential in an `[rpc_startup]` command is masked in the startup
     * log, and so is the result.
     *
     * Both lines are written at fatal, which the capture is taken at, and
     * only by a server that is not quiet. The passphrase is the marker for the
     * command line; a `wallet_propose` for a passphrase is deterministic, so
     * the `master_seed` the same command returns through the client is the
     * marker for the result line.
     */
    void
    testStartupCommandsAreMasked()
    {
        testcase("Startup commands and their results are masked in the log");

        using namespace test::jtx;

        static constexpr char const* kPassphrase = "startup-passphrase-marker";

        std::string logs;
        std::string seed;
        {
            Env env{
                *this,
                envconfig([](std::unique_ptr<Config> cfg) {
                    // The test config is quiet, and a quiet server writes neither line.
                    cfg->setupControl(false, false, true);
                    json::Value command(json::ValueType::Object);
                    command[jss::command] = "wallet_propose";
                    command[jss::passphrase] = kPassphrase;
                    cfg->section(Sections::kRpcStartup).append(to_string(command));
                    return cfg;
                }),
                std::make_unique<CaptureLogs>(&logs),
                beast::Severity::Fatal};

            json::Value params(json::ValueType::Object);
            params[jss::passphrase] = kPassphrase;
            auto const reply = env.rpc("json", "wallet_propose", to_string(params));
            seed = reply[jss::result][jss::master_seed].asString();
        }

        // Both lines were written, so the absence assertions below are not vacuous.
        BEAST_EXPECT(logs.contains("Startup RPC: "));
        BEAST_EXPECT(logs.contains("Result: "));
        BEAST_EXPECT(logs.contains("<masked>"));

        BEAST_EXPECT(!logs.contains(kPassphrase));
        BEAST_EXPECT(!seed.empty());
        BEAST_EXPECT(!logs.contains(seed));
    }

    /**
     * No credential reaches the log at trace, on any transport.
     *
     * Several render sites write only at `trace`, and no other test raises
     * the threshold that far. A `wallet_propose` reply is the control for the
     * `HTTP Reply` line: it carries the keys it generated, and that line
     * carries the status only.
     *
     * @param yield The coroutine the requests run on.
     */
    void
    testNoCredentialReachesTheLogAtTrace(boost::asio::yield_context& yield)
    {
        testcase("No credential reaches the log at trace");

        using namespace test::jtx;

        static constexpr char const* kSensitive = "sensitive-trace-value";

        std::string logs;
        std::string seed;
        {
            Env env{
                *this, envconfig(), std::make_unique<CaptureLogs>(&logs), beast::Severity::Trace};

            auto const port = env.app().config()[Sections::kPortWs].get<std::uint16_t>(Keys::kPort);
            auto const ip = env.app().config()[Sections::kPortWs].get<std::string>(Keys::kIp);

            json::Value params(json::ValueType::Object);
            for (auto const field : rpc::kCredentialFields)
                params[std::string{field}] = kSensitive;

            json::Value jv;
            jv[jss::method] = "ping";
            jv[jss::params] = json::ValueType::Array;
            jv[jss::params][0u] = params;

            boost::system::error_code ec;
            Response resp;
            auto const reply = postAndParse(env, yield, resp, ec, to_string(jv));
            BEAST_EXPECT(reply[jss::result][jss::status] == jss::success);

            json::Value frame(params);
            frame[jss::command] = "ping";
            // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
            doWSMessage(yield, *ip, *port, to_string(frame));

            json::Value wallet;
            wallet[jss::method] = "wallet_propose";
            auto const proposed = postAndParse(env, yield, resp, ec, to_string(wallet));
            seed = proposed[jss::result][jss::master_seed].asString();
        }

        // Both transports logged, so an empty capture cannot pass this.
        BEAST_EXPECT(logs.contains("Websocket received '"));
        BEAST_EXPECT(logs.contains("doRpcCommand:"));
        BEAST_EXPECT(logs.contains("HTTP Reply "));

        BEAST_EXPECT(!logs.contains(kSensitive));
        BEAST_EXPECT(logs.contains("<masked>"));

        // The seed the client received reaches no line, the status-only reply line included.
        BEAST_EXPECT(!seed.empty());
        BEAST_EXPECT(!logs.contains(seed));
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
            testWSUnparsableFrames(yield);
            testPrivilegedWSFramesAreExempt(yield);
            testRPCRequests(yield);
            testMaskedCredentials(yield);
            testTheLoggedRequestIsMaskedAndCapped(yield);
            testTheLoggedReplyIsMaskedOnlyWhenItCarriesACredential(yield);
            testInternalErrorIsReportedOnBothTransports(yield);
            testNoCredentialReachesTheLogAtTrace(yield);
            testRipplerpcVersions(yield);
            testPrivilegedRequestIsNotShed(yield);
            testLegacyBatchEntryRejections(yield);
            testAnErrorReplyDoesNotFollowTheLogLevel(yield);
            testBatchOverload(yield);
            testUnreadBodiesAreCharged(yield);
            testPrivilegedBodiesAreNotCharged(yield);
            testBatchIdentity(yield);
            testRequestForms(yield);
            testTopLevelVersion(yield);
            testHandlerErrorsCarryCodes(yield);
            testGainedStatusesStayOffLegacyEnvelope(yield);
            testUncommonHttpStatus(yield);
            testStatusNotOkay(yield);
        });

        testCommandLineCredentialsAreMasked();
        testStartupCommandsAreMasked();
    }
};

BEAST_DEFINE_TESTSUITE(ServerStatus, server, xrpl);

}  // namespace xrpl::test
