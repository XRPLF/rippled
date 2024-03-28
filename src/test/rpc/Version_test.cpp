//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012-2017 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#include <ripple/protocol/jss.h>
#include <ripple/rpc/impl/RPCHelpers.h>
#include <test/jtx.h>

namespace ripple {

class Version_test : public beast::unit_test::suite
{
    void
    testCorrectVersionNumber()
    {
        testcase("right api_version: explicitly specified or filled by parser");

        using namespace test::jtx;
        Env env{*this};

        auto isCorrectReply = [](Json::Value const& re) -> bool {
            if (re.isMember(jss::error))
                return false;
            return re.isMember(jss::version);
        };

        auto jrr = env.rpc(
            "json",
            "version",
            "{\"api_version\": " +
                std::to_string(RPC::apiMaximumSupportedVersion) +
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

        auto badVersion = [](Json::Value const& re) -> bool {
            if (re.isMember("error_what"))
                if (re["error_what"].isString())
                {
                    return re["error_what"].asString().find(
                               jss::invalid_API_version.c_str()) == 0;
                }
            return false;
        };

        auto re = env.rpc(
            "json",
            "version",
            "{\"api_version\": " +
                std::to_string(RPC::apiMinimumSupportedVersion - 1) + "}");
        BEAST_EXPECT(badVersion(re));

        BEAST_EXPECT(env.app().config().BETA_RPC_API);
        re = env.rpc(
            "json",
            "version",
            "{\"api_version\": " +
                std::to_string(
                    std::max(
                        RPC::apiMaximumSupportedVersion.value,
                        RPC::apiBetaVersion.value) +
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

        unsigned int versionIfUnspecified =
            RPC::apiVersionIfUnspecified < RPC::apiMinimumSupportedVersion
            ? RPC::apiInvalidVersion
            : RPC::apiVersionIfUnspecified;

        Json::Value j_array = Json::Value(Json::arrayValue);
        Json::Value j_null = Json::Value(Json::nullValue);
        BEAST_EXPECT(
            RPC::getAPIVersionNumber(j_array, false) == versionIfUnspecified);
        BEAST_EXPECT(
            RPC::getAPIVersionNumber(j_null, false) == versionIfUnspecified);

        Json::Value j_object = Json::Value(Json::objectValue);
        BEAST_EXPECT(
            RPC::getAPIVersionNumber(j_object, false) == versionIfUnspecified);
        j_object[jss::api_version] = RPC::apiVersionIfUnspecified.value;
        BEAST_EXPECT(
            RPC::getAPIVersionNumber(j_object, false) == versionIfUnspecified);

        j_object[jss::api_version] = RPC::apiMinimumSupportedVersion.value;
        BEAST_EXPECT(
            RPC::getAPIVersionNumber(j_object, false) ==
            RPC::apiMinimumSupportedVersion);
        j_object[jss::api_version] = RPC::apiMaximumSupportedVersion.value;
        BEAST_EXPECT(
            RPC::getAPIVersionNumber(j_object, false) ==
            RPC::apiMaximumSupportedVersion);

        j_object[jss::api_version] = RPC::apiMinimumSupportedVersion - 1;
        BEAST_EXPECT(
            RPC::getAPIVersionNumber(j_object, false) ==
            RPC::apiInvalidVersion);
        j_object[jss::api_version] = RPC::apiMaximumSupportedVersion + 1;
        BEAST_EXPECT(
            RPC::getAPIVersionNumber(j_object, false) ==
            RPC::apiInvalidVersion);
        j_object[jss::api_version] = RPC::apiBetaVersion.value;
        BEAST_EXPECT(
            RPC::getAPIVersionNumber(j_object, true) == RPC::apiBetaVersion);
        j_object[jss::api_version] = RPC::apiBetaVersion + 1;
        BEAST_EXPECT(
            RPC::getAPIVersionNumber(j_object, true) == RPC::apiInvalidVersion);

        j_object[jss::api_version] = RPC::apiInvalidVersion.value;
        BEAST_EXPECT(
            RPC::getAPIVersionNumber(j_object, false) ==
            RPC::apiInvalidVersion);
        j_object[jss::api_version] = "a";
        BEAST_EXPECT(
            RPC::getAPIVersionNumber(j_object, false) ==
            RPC::apiInvalidVersion);
    }

    void
    testBatch()
    {
        testcase("batch, all good request");

        using namespace test::jtx;
        Env env{*this};

        auto const without_api_verion = std::string("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 5, "
            "\"method\": \"version\", "
            "\"params\": {}}";
        auto const with_api_verion = std::string("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 6, "
            "\"method\": \"version\", "
            "\"params\": { "
            "\"api_version\": " +
            std::to_string(RPC::apiMaximumSupportedVersion) + "}}";
        auto re = env.rpc(
            "json2", '[' + without_api_verion + ", " + with_api_verion + ']');

        if (!BEAST_EXPECT(re.isArray()))
            return;
        if (!BEAST_EXPECT(re.size() == 2))
            return;
        BEAST_EXPECT(
            re[0u].isMember(jss::result) &&
            re[0u][jss::result].isMember(jss::version));
        BEAST_EXPECT(
            re[1u].isMember(jss::result) &&
            re[1u][jss::result].isMember(jss::version));
    }

    void
    testBatchFail()
    {
        testcase("batch, with a bad request");

        using namespace test::jtx;
        Env env{*this};

        BEAST_EXPECT(env.app().config().BETA_RPC_API);
        auto const without_api_verion = std::string("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 5, "
            "\"method\": \"version\", "
            "\"params\": {}}";
        auto const with_wrong_api_verion = std::string("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 6, "
            "\"method\": \"version\", "
            "\"params\": { "
            "\"api_version\": " +
            std::to_string(std::max(
                               RPC::apiMaximumSupportedVersion.value,
                               RPC::apiBetaVersion.value) +
                           1) +
            "}}";
        auto re = env.rpc(
            "json2",
            '[' + without_api_verion + ", " + with_wrong_api_verion + ']');

        if (!BEAST_EXPECT(re.isArray()))
            return;
        if (!BEAST_EXPECT(re.size() == 2))
            return;
        BEAST_EXPECT(
            re[0u].isMember(jss::result) &&
            re[0u][jss::result].isMember(jss::version));
        BEAST_EXPECT(re[1u].isMember(jss::error));
    }

    void
    testConfig()
    {
        testcase("config test");
        {
            Config c;
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
            "{\"api_version\": " + std::to_string(RPC::apiBetaVersion) +
                "}")[jss::result];

        if (!BEAST_EXPECT(jrr.isMember(jss::version)))
            return;
        if (!BEAST_EXPECT(jrr[jss::version].isMember(jss::first)) &&
            jrr[jss::version].isMember(jss::last))
            return;
        BEAST_EXPECT(
            jrr[jss::version][jss::first] ==
            RPC::apiMinimumSupportedVersion.value);
        BEAST_EXPECT(jrr[jss::version][jss::last] == RPC::apiBetaVersion.value);
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

BEAST_DEFINE_TESTSUITE(Version, rpc, ripple);

}  // namespace ripple
