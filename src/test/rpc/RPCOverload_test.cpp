#include <test/jtx/AbstractClient.h>
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/JSONRPCClient.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/pay.h>

#include <xrpld/core/Config.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/config/Constants.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/jss.h>

#include <memory>
#include <optional>
#include <string>
#include <utility>

namespace xrpl::test {

class RPCOverload_test : public beast::unit_test::Suite
{
public:
    void
    testOverload(bool useWS)
    {
        testcase << "Overload " << (useWS ? "WS" : "HTTP") << " RPC client";
        using namespace jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->loadFromString(std::string("[") + Sections::kSigningSupport + "]\ntrue");
                    return noAdmin(std::move(cfg));
                })};

        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, bob);

        std::unique_ptr<AbstractClient> client =
            useWS ? makeWSClient(env.app().config()) : makeJSONRPCClient(env.app().config());

        json::Value tx = json::ValueType::Object;
        tx[jss::tx_json] = pay(alice, bob, XRP(1));
        tx[jss::secret] = toBase58(generateSeed("alice"));

        // Ask the server to repeatedly sign this transaction
        // Signing is a resource heavy transaction, so we want the server
        // to warn and eventually boot us.
        bool warned = false, booted = false;
        for (int i = 0; i < 500 && !booted; ++i)
        {
            auto jv = client->invoke("sign", tx);
            if (!useWS)
                jv = jv[jss::result];
            // When booted, we just get a null json response
            if (jv.isNull())
            {
                booted = true;
            }
            else if (!(jv.isMember(jss::status) && (jv[jss::status] == "success")))
            {
                // Don't use BEAST_EXPECT above b/c it will be called a
                // non-deterministic number of times and the number of tests run
                // should be deterministic
                fail("", __FILE__, __LINE__);
            }

            if (jv.isMember(jss::warning))
                warned = jv[jss::warning] == jss::load;
        }
        BEAST_EXPECT(warned && booted);
    }

    /**
     * The load warning reaches the client on both transports at every
     * version, at the place that version puts it. From API version 3 that is
     * the top level of the specification envelope, which reserves no place
     * for a notice and so carries it beside its own members. Below version 3
     * it is beside `result` on the WebSocket transport and inside `result`
     * on the JSON-RPC one.
     *
     * @param useWS True to test the WebSocket transport, false for JSON-RPC.
     * @param apiVersion The API version to request the warning under.
     */
    void
    testWarningLocation(bool useWS, unsigned apiVersion)
    {
        testcase << "Load warning on " << (useWS ? "WS" : "HTTP") << " at API version "
                 << apiVersion;
        using namespace jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    cfg->loadFromString(std::string("[") + Sections::kSigningSupport + "]\ntrue");
                    return noAdmin(std::move(cfg));
                })};

        Account const alice{"alice"};
        Account const bob{"bob"};
        env.fund(XRP(10000), alice, bob);

        // invokeRaw is needed on the WebSocket transport: invoke rewrites a version 3 reply into
        // the legacy envelope, which is the very difference under test.
        auto const ws = useWS ? makeWSClient(env.app().config(), true, 2, {}, apiVersion) : nullptr;
        auto const http = useWS ? nullptr : makeJSONRPCClient(env.app().config());

        json::Value tx = json::ValueType::Object;
        tx[jss::tx_json] = pay(alice, bob, XRP(1));
        tx[jss::secret] = toBase58(generateSeed("alice"));
        // A WebSocket request carries api_version in the same flat object, which makeWSClient adds.
        if (!useWS)
            tx[jss::api_version] = apiVersion;

        auto const send = [&] -> std::optional<json::Value> {
            if (useWS)
                return ws->invokeRaw("sign", tx);
            auto jv = http->invoke("sign", tx);
            if (jv.isNull())
                return std::nullopt;
            return jv;
        };

        // Signing is charged heavily enough that a limited consumer is warned within a few calls.
        // Stop at the first reply that carries a warning anywhere: that reply is the one under
        // test, and continuing would eventually get the client booted instead.
        std::optional<json::Value> warned;
        for (int i = 0; i < 500 && !warned; ++i)
        {
            auto const reply = send();
            // A booted client gets nothing back, which means no reply ever carried the warning.
            if (!reply)
                break;
            if (reply->isMember(jss::warning) || (*reply)[jss::result].isMember(jss::warning))
                warned = reply;
        }

        if (!BEAST_EXPECT(warned.has_value()))
            return;

        // NOLINTNEXTLINE(bugprone-unchecked-optional-access) checked above
        json::Value const& reply = *warned;
        if (apiVersion >= rpc::kApiMinimumSpecVersion || useWS)
        {
            BEAST_EXPECT(reply[jss::warning] == jss::load);
            BEAST_EXPECT(!reply[jss::result].isMember(jss::warning));
        }
        else
        {
            BEAST_EXPECT(reply[jss::result][jss::warning] == jss::load);
            BEAST_EXPECT(!reply.isMember(jss::warning));
        }
    }

    void
    run() override
    {
        testOverload(false /* http */);
        testOverload(true /* ws */);

        for (auto const useWS : {false, true})
        {
            for (auto const apiVersion : {1u, 2u, 3u})
                testWarningLocation(useWS, apiVersion);
        }
    }
};

BEAST_DEFINE_TESTSUITE(RPCOverload, rpc, xrpl);

}  // namespace xrpl::test
