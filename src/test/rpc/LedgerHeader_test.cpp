//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2023 Ripple Labs Inc.

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
#include <test/jtx/Env.h>
#include <test/jtx/envconfig.h>

namespace ripple {

class LedgerHeader_test : public beast::unit_test::suite
{
    void
    testSimpleCurrent()
    {
        testcase("Current ledger");
        using namespace test::jtx;
        Env env{*this, envconfig(no_admin)};

        Json::Value params{Json::objectValue};
        params[jss::api_version] = 1;
        params[jss::ledger_index] = "current";
        auto const result =
            env.client().invoke("ledger_header", params)[jss::result];
        BEAST_EXPECT(result[jss::status] == "success");
        BEAST_EXPECT(result.isMember("ledger"));
        BEAST_EXPECT(result[jss::ledger][jss::closed] == false);
        BEAST_EXPECT(result[jss::validated] == false);
    }

    void
    testSimpleValidated()
    {
        testcase("Validated ledger");
        using namespace test::jtx;
        Env env{*this, envconfig(no_admin)};

        Json::Value params{Json::objectValue};
        params[jss::api_version] = 1;
        params[jss::ledger_index] = "validated";
        auto const result =
            env.client().invoke("ledger_header", params)[jss::result];
        BEAST_EXPECT(result[jss::status] == "success");
        BEAST_EXPECT(result.isMember("ledger"));
        BEAST_EXPECT(result[jss::ledger][jss::closed] == true);
        BEAST_EXPECT(result[jss::validated] == true);
    }

    void
    testCommandRetired()
    {
        testcase("Command retired from API v2");
        using namespace test::jtx;
        Env env{*this, envconfig(no_admin)};

        Json::Value params{Json::objectValue};
        params[jss::api_version] = 2;
        auto const result =
            env.client().invoke("ledger_header", params)[jss::result];
        BEAST_EXPECT(result[jss::error] == "unknownCmd");
        BEAST_EXPECT(result[jss::status] == "error");
    }

public:
    void
    run() override
    {
        testSimpleCurrent();
        testSimpleValidated();
        testCommandRetired();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerHeader, rpc, ripple);

}  // namespace ripple
