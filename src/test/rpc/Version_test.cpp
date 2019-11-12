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

#include <test/jtx.h>
#include <ripple/protocol/jss.h>

namespace ripple {

class Version_test : public beast::unit_test::suite
{
    void
    testCorrectVersionNumber()
    {
        testcase ("right api_version: explicitly specified or filled by parser");

        using namespace test::jtx;
        Env env {*this};

        auto isCorrectReply = [](Json::Value const & re) -> bool
        {
            if(re.isMember(jss::error))
                return false;
            return re.isMember(jss::version);
        };

        auto jrr = env.rpc("json", "version", "{\"api_version\": " +
                           std::to_string(RPC::APIVersionSupportedRangeHigh)
                           + "}") [jss::result];
        BEAST_EXPECT(isCorrectReply(jrr));

        jrr = env.rpc("version") [jss::result];
        BEAST_EXPECT(isCorrectReply(jrr));
    }

    void
    testWrongVersionNumber()
    {
        testcase ("wrong api_version: too low, too high, or wrong format");

        using namespace test::jtx;
        Env env {*this};

        auto get_error_what = [](Json::Value const & re) -> std::string
        {
            if(re.isMember("error_what"))
                if(re["error_what"].isString())
                    return re["error_what"].asString();
            return {};
        };

        auto re = env.rpc("json", "version", "{\"api_version\": " +
                          std::to_string(RPC::APIVersionSupportedRangeLow - 1)
                          + "}");
        BEAST_EXPECT(get_error_what(re).find("invalid version"));

        re = env.rpc("json", "version", "{\"api_version\": " +
                     std::to_string(RPC::APIVersionSupportedRangeHigh + 1)
                     + "}");
        BEAST_EXPECT(get_error_what(re).find("invalid version"));

        re = env.rpc("json", "version", "{\"api_version\": \"deadbeef\"}");
        BEAST_EXPECT(get_error_what(re).find("invalid version"));
    }

    void
    testBatch()
    {
        testcase ("batch, all good request");

        using namespace test::jtx;
        Env env {*this};

        auto const without_api_verion = std::string ("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 5, "
            "\"method\": \"version\", "
            "\"params\": {}}";
        auto const with_api_verion = std::string ("{ ") +
            "\"jsonrpc\": \"2.0\", "
            "\"ripplerpc\": \"2.0\", "
            "\"id\": 6, "
            "\"method\": \"version\", "
            "\"params\": { "
            "\"api_version\": " + std::to_string(RPC::APIVersionSupportedRangeHigh) +
            "}}";
        auto re = env.rpc("json2", '[' + without_api_verion + ", " +
                          with_api_verion + ']');

        if( !BEAST_EXPECT( re.isArray() ))
            return;
        if( !BEAST_EXPECT( re.size() == 2 ))
            return;
        BEAST_EXPECT(re[0u].isMember(jss::result) &&
                     re[0u][jss::result].isMember(jss::version));
        BEAST_EXPECT(re[1u].isMember(jss::result) &&
                     re[1u][jss::result].isMember(jss::version));
    }

    void
    testBatchFail()
    {
        testcase ("batch, with a bad request");

        using namespace test::jtx;
        Env env {*this};

        auto const without_api_verion = std::string ("{ ") +
                                        "\"jsonrpc\": \"2.0\", "
                                        "\"ripplerpc\": \"2.0\", "
                                        "\"id\": 5, "
                                        "\"method\": \"version\", "
                                        "\"params\": {}}";
        auto const with_wrong_api_verion = std::string ("{ ") +
                                     "\"jsonrpc\": \"2.0\", "
                                     "\"ripplerpc\": \"2.0\", "
                                     "\"id\": 6, "
                                     "\"method\": \"version\", "
                                     "\"params\": { "
                                     "\"api_version\": " +
                                     std::to_string(RPC::APIVersionSupportedRangeHigh+1) +
                                     "}}";
        auto re = env.rpc("json2", '[' + without_api_verion + ", " +
                                   with_wrong_api_verion + ']');

        if( !BEAST_EXPECT( re.isArray() ))
            return;
        if( !BEAST_EXPECT( re.size() == 2 ))
            return;
        BEAST_EXPECT(re[0u].isMember(jss::result) &&
                     re[0u][jss::result].isMember(jss::version));
        BEAST_EXPECT(re[1u].isMember(jss::error));
    }

public:

    void run() override
    {
        testCorrectVersionNumber();
        testWrongVersionNumber();
        testBatch();
        testBatchFail();
    }
};

BEAST_DEFINE_TESTSUITE(Version,rpc,ripple);

} // ripple
