#include <test/jtx.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/jss.h>

namespace ripple {

class GetCounts_test : public beast::unit_test::suite
{
    void
    testGetCounts()
    {
        using namespace test::jtx;
        Env env(*this);

        Json::Value result;
        {
            using namespace std::chrono_literals;
            // Add a little delay so the App's "uptime" will have a value.
            std::this_thread::sleep_for(1s);
            // check counts with no transactions posted
            result = env.rpc("get_counts")[jss::result];
            BEAST_EXPECT(result[jss::status] == "success");
            BEAST_EXPECT(!result.isMember("Transaction"));
            BEAST_EXPECT(!result.isMember("STObject"));
            BEAST_EXPECT(!result.isMember("HashRouterEntry"));
            BEAST_EXPECT(
                result.isMember(jss::uptime) &&
                !result[jss::uptime].asString().empty());
            BEAST_EXPECT(
                result.isMember(jss::dbKBTotal) &&
                result[jss::dbKBTotal].asInt() > 0);
        }

        // create some transactions
        env.close();
        Account alice{"alice"};
        Account bob{"bob"};
        env.fund(XRP(10000), alice, bob);
        env.trust(alice["USD"](1000), bob);
        for (auto i = 0; i < 20; ++i)
        {
            env(pay(alice, bob, alice["USD"](5)));
            env.close();
        }

        {
            // check counts, default params
            result = env.rpc("get_counts")[jss::result];
            BEAST_EXPECT(result[jss::status] == "success");
            // compare with values reported by CountedObjects
            auto const& objectCounts =
                CountedObjects::getInstance().getCounts(10);
            for (auto const& it : objectCounts)
            {
                BEAST_EXPECTS(result.isMember(it.first), it.first);
                BEAST_EXPECTS(result[it.first].asInt() == it.second, it.first);
            }
            BEAST_EXPECT(!result.isMember(jss::local_txs));
        }

        {
            // make request with min threshold 100 and verify
            // that only STObject and NodeObject are reported
            result = env.rpc("get_counts", "100")[jss::result];
            BEAST_EXPECT(result[jss::status] == "success");

            // compare with values reported by CountedObjects
            auto const& objectCounts =
                CountedObjects::getInstance().getCounts(100);
            for (auto const& it : objectCounts)
            {
                BEAST_EXPECTS(result.isMember(it.first), it.first);
                BEAST_EXPECTS(result[it.first].asInt() == it.second, it.first);
            }
            BEAST_EXPECT(!result.isMember("Transaction"));
            BEAST_EXPECT(!result.isMember("STTx"));
            BEAST_EXPECT(!result.isMember("STArray"));
            BEAST_EXPECT(!result.isMember("HashRouterEntry"));
            BEAST_EXPECT(!result.isMember("STLedgerEntry"));
        }

        {
            // local_txs field will exist when there are open Txs
            env(pay(alice, bob, alice["USD"](5)));
            result = env.rpc("get_counts")[jss::result];
            // deliberately don't call close so we have open Tx
            BEAST_EXPECT(
                result.isMember(jss::local_txs) &&
                result[jss::local_txs].asInt() > 0);
        }
    }

public:
    void
    run() override
    {
        testGetCounts();
    }
};

BEAST_DEFINE_TESTSUITE(GetCounts, rpc, ripple);

}  // namespace ripple
