
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/protocol/ApiVersion.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <memory>
#include <string>

namespace xrpl {

class Version_test : public beast::unit_test::Suite
{
    void
    testCorrectVersionNumber()
    {
        testcase("right api_version: explicitly specified or filled by parser");

        using namespace test::jtx;
        Env env{*this};

        auto isCorrectReply = [](json::Value const& re) -> bool {
            if (re.isMember(jss::error))
                return false;
            return re.isMember(jss::version);
        };

        auto jrr = env.rpc(
            "json",
            "version",
            "{\"api_version\": " + std::to_string(RPC::kApiMaximumSupportedVersion) +
                "}")[jss::result];
        BEAST_EXPECT(isCorrectReply(jrr));

        jrr = env.rpc("version")[jss::result];
        BEAST_EXPECT(isCorrectReply(jrr));
    }

    void
    testWrongVersionNumber()
    {
        testcase("wrong api_version: too low, too high, or wrong format");

        using namespace test::jtx;
        Env env{*this};

        auto badVersion = [](json::Value const& re) -> bool {
            if (re.isMember("error_what"))
            {
                if (re["error_what"].isString())
                {
                    return re["error_what"].asString().starts_with(jss::invalid_API_version.cStr());
                }
            }
            return false;
        };

        auto re = env.rpc(
            "json",
            "version",
            "{\"api_version\": " + std::to_string(RPC::kApiMinimumSupportedVersion - 1) + "}");
        BEAST_EXPECT(badVersion(re));

        BEAST_EXPECT(env.app().config().BETA_RPC_API);
        re = env.rpc(
            "json",
            "version",
            "{\"api_version\": " +
                std::to_string(
                    std::max(RPC::kApiMaximumSupportedVersion.value, RPC::kApiBetaVersion.value) +
                    1) +
                "}");
        BEAST_EXPECT(badVersion(re));

        re = env.rpc("json", "version", "{\"api_version\": \"a\"}");
        BEAST_EXPECT(badVersion(re));
    }

    void
    testGetAPIVersionNumber()
    {
        testcase("test getAPIVersionNumber function");

        unsigned int const versionIfUnspecified =
            RPC::kApiVersionIfUnspecified < RPC::kApiMinimumSupportedVersion
            ? RPC::kApiInvalidVersion
            : RPC::kApiVersionIfUnspecified;

        json::Value const jArray = json::Value(json::ValueType::Array);
        json::Value const jNull = json::Value(json::ValueType::Null);
        BEAST_EXPECT(RPC::getAPIVersionNumber(jArray, false) == versionIfUnspecified);
        BEAST_EXPECT(RPC::getAPIVersionNumber(jNull, false) == versionIfUnspecified);

        json::Value jObject = json::Value(json::ValueType::Object);
        BEAST_EXPECT(RPC::getAPIVersionNumber(jObject, false) == versionIfUnspecified);
        jObject[jss::api_version] = RPC::kApiVersionIfUnspecified.value;
        BEAST_EXPECT(RPC::getAPIVersionNumber(jObject, false) == versionIfUnspecified);

        jObject[jss::api_version] = RPC::kApiMinimumSupportedVersion.value;
        BEAST_EXPECT(RPC::getAPIVersionNumber(jObject, false) == RPC::kApiMinimumSupportedVersion);
        jObject[jss::api_version] = RPC::kApiMaximumSupportedVersion.value;
        BEAST_EXPECT(RPC::getAPIVersionNumber(jObject, false) == RPC::kApiMaximumSupportedVersion);

        jObject[jss::api_version] = RPC::kApiMinimumSupportedVersion - 1;
        BEAST_EXPECT(RPC::getAPIVersionNumber(jObject, false) == RPC::kApiInvalidVersion);
        jObject[jss::api_version] = RPC::kApiMaximumSupportedVersion + 1;
        BEAST_EXPECT(RPC::getAPIVersionNumber(jObject, false) == RPC::kApiInvalidVersion);
        jObject[jss::api_version] = RPC::kApiBetaVersion.value;
        BEAST_EXPECT(RPC::getAPIVersionNumber(jObject, true) == RPC::kApiBetaVersion);
        jObject[jss::api_version] = RPC::kApiBetaVersion + 1;
        BEAST_EXPECT(RPC::getAPIVersionNumber(jObject, true) == RPC::kApiInvalidVersion);

        jObject[jss::api_version] = RPC::kApiInvalidVersion.value;
        BEAST_EXPECT(RPC::getAPIVersionNumber(jObject, false) == RPC::kApiInvalidVersion);
        jObject[jss::api_version] = "a";
        BEAST_EXPECT(RPC::getAPIVersionNumber(jObject, false) == RPC::kApiInvalidVersion);
    }

    void
    testBatch()
    {
        testcase("batch, all good request");

        using namespace test::jtx;
        Env env{*this};

        auto const withoutApiVerion = std::string("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 5, "
            "\"method\": \"version\", "
            "\"params\": {}}";
        auto const withApiVerion = std::string("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 6, "
            "\"method\": \"version\", "
            "\"params\": { "
            "\"api_version\": " +
            std::to_string(RPC::kApiMaximumSupportedVersion) + "}}";
        auto re = env.rpc("json2", '[' + withoutApiVerion + ", " + withApiVerion + ']');

        if (!BEAST_EXPECT(re.isArray()))
            return;
        if (!BEAST_EXPECT(re.size() == 2))
            return;
        BEAST_EXPECT(re[0u].isMember(jss::result) && re[0u][jss::result].isMember(jss::version));
        BEAST_EXPECT(re[1u].isMember(jss::result) && re[1u][jss::result].isMember(jss::version));
    }

    void
    testBatchFail()
    {
        testcase("batch, with a bad request");

        using namespace test::jtx;
        Env env{*this};

        BEAST_EXPECT(env.app().config().BETA_RPC_API);
        auto const withoutApiVerion = std::string("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 5, "
            "\"method\": \"version\", "
            "\"params\": {}}";
        auto const withWrongApiVerion =
            std::string("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 6, "
            "\"method\": \"version\", "
            "\"params\": { "
            "\"api_version\": " +
            std::to_string(
                std::max(RPC::kApiMaximumSupportedVersion.value, RPC::kApiBetaVersion.value) + 1) +
            "}}";
        auto re = env.rpc("json2", '[' + withoutApiVerion + ", " + withWrongApiVerion + ']');

        if (!BEAST_EXPECT(re.isArray()))
            return;
        if (!BEAST_EXPECT(re.size() == 2))
            return;
        BEAST_EXPECT(re[0u].isMember(jss::result) && re[0u][jss::result].isMember(jss::version));
        BEAST_EXPECT(re[1u].isMember(jss::error));
    }

    void
    testConfig()
    {
        testcase("config test");
        {
            Config const c;
            BEAST_EXPECT(c.BETA_RPC_API == false);
        }

        {
            Config c;
            c.loadFromString("\n[beta_rpc_api]\n1\n");
            BEAST_EXPECT(c.BETA_RPC_API == true);
        }

        {
            Config c;
            c.loadFromString("\n[beta_rpc_api]\n0\n");
            BEAST_EXPECT(c.BETA_RPC_API == false);
        }
    }

    void
    testVersionRPCV2()
    {
        testcase("test version RPC with api_version >= 2");

        using namespace test::jtx;
        Env env{*this, envconfig([](std::unique_ptr<Config> c) {
                    c->loadFromString("\n[beta_rpc_api]\n1\n");
                    return c;
                })};
        if (!BEAST_EXPECT(env.app().config().BETA_RPC_API == true))
            return;

        auto jrr = env.rpc(
            "json",
            "version",
            "{\"api_version\": " + std::to_string(RPC::kApiBetaVersion) + "}")[jss::result];

        if (!BEAST_EXPECT(jrr.isMember(jss::version)))
            return;
        if (!BEAST_EXPECT(jrr[jss::version].isMember(jss::first)) &&
            jrr[jss::version].isMember(jss::last))
            return;
        BEAST_EXPECT(jrr[jss::version][jss::first] == RPC::kApiMinimumSupportedVersion.value);
        BEAST_EXPECT(jrr[jss::version][jss::last] == RPC::kApiBetaVersion.value);
    }

public:
    void
    run() override
    {
        testCorrectVersionNumber();
        testWrongVersionNumber();
        testGetAPIVersionNumber();
        testBatch();
        testBatchFail();
        testConfig();
        testVersionRPCV2();
    }
};

BEAST_DEFINE_TESTSUITE(Version, rpc, xrpl);

}  // namespace xrpl
