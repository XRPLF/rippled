
#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/pay.h>

#include <xrpl/basics/CountedObject.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <boost/container/static_vector.hpp>

#include <thread>

namespace xrpl {

class GetCounts_test : public beast::unit_test::Suite
{
    void
    testGetCounts()
    {
        testcase("Get counts");
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
            // compare with values reported by CountedObjects
            auto const& objectCounts = CountedObjects::getInstance().getCounts(10);
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
            auto const& objectCounts = CountedObjects::getInstance().getCounts(100);
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
            BEAST_EXPECT(result.isMember(jss::local_txs) && result[jss::local_txs].asInt() > 0);
        }
    }

    void
    testBadMinCount()
    {
        testcase("Invalid min_count");
        using namespace test::jtx;
        Env env{*this};

        json::Value objMinCount{json::ValueType::Object};
        objMinCount["nope"] = 0;
        json::Value arrMinCount{json::ValueType::Array};
        arrMinCount.append(0);

        // Note that a whole number beyond the range of a uint, e.g.
        // 4294967296, is rejected by the json parser itself, so it never
        // reaches the handler and is not covered here.
        boost::container::static_vector<json::Value, 9> const badMinCounts{
            json::Value{-1},     // negative
            json::Value{"abc"},  // not a number
            json::Value{"5"},    // numeric, but a string
            json::Value{1.5},    // not a whole number
            json::Value{1e30},   // beyond the range of a uint
            json::Value{true},   // bool
            json::Value{},       // null
            objMinCount,
            arrMinCount,
        };

        for (auto const& badMinCount : badMinCounts)
        {
            json::Value params{json::ValueType::Object};
            params[jss::min_count] = badMinCount;
            auto const result = env.client().invoke("get_counts", params)[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }
    }

public:
    void
    run() override
    {
        testGetCounts();
        testBadMinCount();
    }
};

BEAST_DEFINE_TESTSUITE(GetCounts, rpc, xrpl);

}  // namespace xrpl
