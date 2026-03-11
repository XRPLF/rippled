#include <test/jtx.h>

#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/ErrorCodes.h>
#include <xrpl/protocol/jss.h>
#include <xrpl/server/NetworkOPs.h>

namespace xrpl {

class FeeVoteRPC_test : public beast::unit_test::suite
{
public:
    void
    testReadAndUpdate()
    {
        testcase("Read and Update");
        using namespace test::jtx;

        Env env{*this};

        Json::Value initial{Json::objectValue};
        {
            initial = env.rpc("fee_vote")[jss::result];
            BEAST_EXPECT(!RPC::contains_error(initial));
            BEAST_EXPECT(initial.isMember(jss::reference_fee));
            BEAST_EXPECT(initial.isMember(jss::account_reserve));
            BEAST_EXPECT(initial.isMember(jss::owner_reserve));
        }

        Json::Value updated{Json::objectValue};
        {
            updated = env.rpc("fee_vote", "12", "11000000", "2100000")[jss::result];
            BEAST_EXPECT(!RPC::contains_error(updated));
        }

        auto const [referenceFee, accountReserve, ownerReserve] = env.app().getOPs().getFeeVote();
        BEAST_EXPECT(updated[jss::reference_fee] == referenceFee.jsonClipped());
        BEAST_EXPECT(updated[jss::account_reserve] == accountReserve.jsonClipped());
        BEAST_EXPECT(updated[jss::owner_reserve] == ownerReserve.jsonClipped());

        BEAST_EXPECT(updated[jss::reference_fee] != initial[jss::reference_fee]);
        BEAST_EXPECT(updated[jss::account_reserve] != initial[jss::account_reserve]);
        BEAST_EXPECT(updated[jss::owner_reserve] != initial[jss::owner_reserve]);
    }

    void
    testNonAdmin()
    {
        testcase("Non Admin");
        using namespace test::jtx;

        Env env{*this, envconfig([](std::unique_ptr<Config> cfg) {
                    (*cfg)["port_rpc"].set("admin", "");
                    (*cfg)["port_ws"].set("admin", "");
                    return cfg;
                })};

        {
            auto const result = env.rpc("fee_vote")[jss::result];
            BEAST_EXPECT(!RPC::contains_error(result));
        }

        {
            auto const result = env.rpc("fee_vote", "11")[jss::result];
            BEAST_EXPECT(result[jss::error] == "noPermission");
            BEAST_EXPECT(
                result[jss::error_message] == "You don't have permission for this command.");
        }
    }

    void
    testInvalidParams()
    {
        testcase("Invalid Params");
        using namespace test::jtx;

        Env env{*this};

        {
            auto const result = env.rpc("fee_vote", "abc")[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
        }

        {
            auto const result = env.rpc("fee_vote", "10", "4294967296")[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
        }
    }

    void
    run() override
    {
        testReadAndUpdate();
        testNonAdmin();
        testInvalidParams();
    }
};

BEAST_DEFINE_TESTSUITE(FeeVoteRPC, rpc, xrpl);

}  // namespace xrpl
