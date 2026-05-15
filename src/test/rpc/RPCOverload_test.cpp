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
#include <xrpl/protocol/Seed.h>
#include <xrpl/protocol/jss.h>

#include <memory>
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

    void
    run() override
    {
        testOverload(false /* http */);
        testOverload(true /* ws */);
    }
};

BEAST_DEFINE_TESTSUITE(RPCOverload, rpc, xrpl);

}  // namespace xrpl::test
