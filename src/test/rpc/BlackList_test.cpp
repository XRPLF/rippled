#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/jss.h>

#include <boost/container/static_vector.hpp>

namespace xrpl {

class BlackList_test : public beast::unit_test::Suite
{
    void
    testBlackList()
    {
        testcase("Blacklist");
        using namespace test::jtx;
        Env env{*this};

        // Omitting the threshold uses the Resource manager's warning
        // threshold. A freshly started Env tracks no loaded consumers, so the
        // result is an empty object rather than an error.
        auto const result = env.client().invoke("blacklist")[jss::result];
        BEAST_EXPECT(!result.isMember(jss::error));
        BEAST_EXPECT(result[jss::status] == "success");
    }

    void
    testValidThreshold()
    {
        testcase("Valid threshold");
        using namespace test::jtx;
        Env env{*this};

        // The threshold is a signed load-balance cutoff, not a count or an
        // index, so negative and zero values are meaningful: they select every
        // tracked consumer.
        boost::container::static_vector<json::Value, 5> const goodThresholds{
            json::Value{json::Value::kMinInt},
            json::Value{-1},
            json::Value{0},
            json::Value{5000},  // the default, Resource::kWarningThreshold
            json::Value{json::Value::kMaxInt},
        };

        for (auto const& goodThreshold : goodThresholds)
        {
            json::Value params{json::ValueType::Object};
            params[jss::threshold] = goodThreshold;
            auto const result = env.client().invoke("blacklist", params)[jss::result];
            BEAST_EXPECT(!result.isMember(jss::error));
            BEAST_EXPECT(result[jss::status] == "success");
        }
    }

    void
    testBadThreshold()
    {
        testcase("Invalid threshold");
        using namespace test::jtx;
        Env env{*this};

        json::Value objThreshold{json::ValueType::Object};
        objThreshold["nope"] = 0;
        json::Value arrThreshold{json::ValueType::Array};
        arrThreshold.append(0);

        // Note that a whole number beyond the range of a uint, e.g.
        // 4294967296, is rejected by the json parser itself, so it never
        // reaches the handler and is not covered here.
        boost::container::static_vector<json::Value, 9> const badThresholds{
            json::Value{"abc"},                   // not a number
            json::Value{"5"},                     // numeric, but a string
            json::Value{1.5},                     // not a whole number
            json::Value{1e30},                    // beyond the range of an int
            json::Value{true},                    // bool
            json::Value{},                        // null
            json::Value{json::UInt{3000000000}},  // beyond the range of an int
            objThreshold,
            arrThreshold,
        };

        for (auto const& badThreshold : badThresholds)
        {
            json::Value params{json::ValueType::Object};
            params[jss::threshold] = badThreshold;
            auto const result = env.client().invoke("blacklist", params)[jss::result];
            BEAST_EXPECT(result[jss::error] == "invalidParams");
            BEAST_EXPECT(result[jss::status] == "error");
        }
    }

public:
    void
    run() override
    {
        testBlackList();
        testValidThreshold();
        testBadThreshold();
    }
};

BEAST_DEFINE_TESTSUITE(BlackList, rpc, xrpl);

}  // namespace xrpl
