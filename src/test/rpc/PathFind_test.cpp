#include <test/jtx.h>
#include <test/jtx/WSClient.h>

#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>

namespace xrpl {
namespace test {

class PathFind_test : public beast::unit_test::suite
{
    void
    testCreatePreservesSessionOnError()
    {
        // Regression test for issue #6789:
        // A failed path_find create (invalid parameters) must NOT destroy
        // the client's existing path_find session.  Before the fix,
        // clearRequest() was called unconditionally before makePathRequest(),
        // so a validation failure left the client with no session.
        testcase("path_find create preserves session on invalid input");
        using namespace jtx;

        Env env{*this};
        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];

        env.fund(XRP(10000), alice, bob, gw);
        env.close();
        env.trust(USD(1000), alice);
        env.trust(USD(1000), bob);
        env(pay(gw, alice, USD(100)));
        env(pay(gw, bob, USD(100)));
        env.close();

        auto wsc = makeWSClient(env.app().config());

        // Step 1 – establish a valid path_find session
        {
            Json::Value req;
            req[jss::subcommand] = "create";
            req[jss::source_account] = alice.human();
            req[jss::destination_account] = bob.human();
            req[jss::destination_amount] = bob["USD"](10).value().getJson(JsonOptions::none);
            auto const jr = wsc->invoke("path_find", req)[jss::result];
            BEAST_EXPECT(!RPC::contains_error(jr));
            BEAST_EXPECT(jr.isMember(jss::alternatives));
        }

        // Step 2 – send a malformed create (bad source_account)
        {
            Json::Value req;
            req[jss::subcommand] = "create";
            req[jss::source_account] = "not_a_valid_address";
            req[jss::destination_account] = bob.human();
            req[jss::destination_amount] = bob["USD"](10).value().getJson(JsonOptions::none);
            auto const jr = wsc->invoke("path_find", req)[jss::result];
            BEAST_EXPECT(RPC::contains_error(jr));
        }

        // Step 3 – original session must still be alive
        // Before the fix this would return rpcNO_PF_REQUEST (error 33)
        // because clearRequest() had already destroyed the old session.
        {
            Json::Value req;
            req[jss::subcommand] = "status";
            auto const jr = wsc->invoke("path_find", req)[jss::result];
            BEAST_EXPECT(!RPC::contains_error(jr));
        }
    }

    void
    run() override
    {
        testCreatePreservesSessionOnError();
    }
};

BEAST_DEFINE_TESTSUITE(PathFind, rpc, xrpl);

}  // namespace test
}  // namespace xrpl
