
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <thread>

namespace xrpl {

class GetCounts_test : public beast::unit_test::Suite
{
    void
    testGetCounts()
    {
        using namespace test::jtx;
        Env env(*this);

        json::Value result;
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
            BEAST_EXPECT(result.isMember(jss::uptime) && !result[jss::uptime].asString().empty());
            BEAST_EXPECT(result.isMember(jss::dbKBTotal) && result[jss::dbKBTotal].asInt() > 0);
        }

        // create some transactions
        env.close();
        Account const alice{"alice"};
        Account const bob{"bob"};
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

            // compare with values reported by the registry
            auto const& counters = result[jss::counters];

            for (auto const& c : gCountedObjects)
            {
                if (auto const count = c.count(); count >= 10)
                {
                    BEAST_EXPECTS(counters.isMember(c.name()), c.name());
                    BEAST_EXPECTS(counters[c.name()][jss::current].asUInt() == count, c.name());
                }
            }

            BEAST_EXPECT(!result.isMember(jss::local_txs));
        }
        {
            // make request with min threshold 100 and verify
            // that only counters at or above the threshold are reported
            result = env.rpc("get_counts", "100")[jss::result];
            BEAST_EXPECT(result[jss::status] == "success");

            auto const& counters = result[jss::counters];

            // every registry counter at/above threshold must be reported, with
            // a matching current value
            for (auto const& c : gCountedObjects)
            {
                if (auto const count = c.count(); count >= 100)
                {
                    BEAST_EXPECTS(counters.isMember(c.name()), c.name());
                    BEAST_EXPECTS(counters[c.name()][jss::current].asUInt() == count, c.name());
                }
            }

            // conversely, every reported entry must be a known counter that met
            // the threshold, and its maximum must be consistent
            for (auto const& name : counters.getMemberNames())
            {
                auto const it = std::ranges::find_if(
                    gCountedObjects, [&](auto const& c) { return c.name() == name; });
                BEAST_EXPECTS(it != gCountedObjects.end(), name);

                auto const& entry = counters[name];
                BEAST_EXPECTS(entry[jss::current].asUInt() >= 100, name);
                BEAST_EXPECTS(entry[jss::maximum].asUInt() >= entry[jss::current].asUInt(), name);
            }

            BEAST_EXPECT(!counters.isMember("xrpl::Transaction"));
            BEAST_EXPECT(!counters.isMember("xrpl::STTx"));
            BEAST_EXPECT(!counters.isMember("xrpl::STArray"));
            BEAST_EXPECT(!counters.isMember("xrpl::HashRouterEntry"));
            BEAST_EXPECT(!counters.isMember("xrpl::STLedgerEntry"));
        }
        {
            // local_txs field will exist when there are open Txs
            env(pay(alice, bob, alice["USD"](5)));
            result = env.rpc("get_counts")[jss::result];
            // deliberately don't call close so we have open Tx
            BEAST_EXPECT(result.isMember(jss::local_txs) && result[jss::local_txs].asInt() > 0);
        }
    }

public:
    void
    run() override
    {
        testGetCounts();
    }
};

BEAST_DEFINE_TESTSUITE(GetCounts, rpc, xrpl);

}  // namespace xrpl
