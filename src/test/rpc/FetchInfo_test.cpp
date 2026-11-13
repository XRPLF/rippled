#include <test/jtx/Env.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/json/to_string.h>
#include <xrpl/protocol/jss.h>

namespace xrpl::test {

class FetchInfo_test : public beast::unit_test::Suite
{
    void
    testClearParameter()
    {
        testcase("fetch_info clear parameter");

        using namespace test::jtx;

        Env env(*this);

        auto const callFetchInfo = [&](json::Value const& params) {
            return env.rpc("json", "fetch_info", json::to_string(params))[jss::result];
        };

        json::Value trueParams(json::ValueType::Object);
        trueParams[jss::clear] = true;
        auto const trueResult = callFetchInfo(trueParams);
        BEAST_EXPECT(
            trueResult[jss::clear].isBool() && trueResult[jss::clear].asBool());

        json::Value falseParams(json::ValueType::Object);
        falseParams[jss::clear] = false;
        auto const falseResult = callFetchInfo(falseParams);
        BEAST_EXPECT(!falseResult.isMember(jss::error));
        BEAST_EXPECT(!falseResult.isMember(jss::clear));

        auto testInvalidClear = [&](json::Value const& clear) {
            json::Value params(json::ValueType::Object);
            params[jss::clear] = clear;

            auto const result = callFetchInfo(params);
            BEAST_EXPECT(result[jss::error] == "invalidParams");
        };

        testInvalidClear(json::Value{"false"});

        json::Value array(json::ValueType::Array);
        array.append(1);
        testInvalidClear(array);

        json::Value object(json::ValueType::Object);
        object["value"] = true;
        testInvalidClear(object);
    }

    void
    run() override
    {
        testClearParameter();
    }
};

BEAST_DEFINE_TESTSUITE(FetchInfo, rpc, xrpl);

}  // namespace xrpl::test