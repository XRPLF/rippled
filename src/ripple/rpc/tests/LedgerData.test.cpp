//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2016 Ripple Labs Inc.

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

#include <ripple/protocol/JsonFields.h>
#include <ripple/test/jtx.h>

namespace ripple {

class LedgerData_test : public beast::unit_test::suite
{
public:

    static
    std::unique_ptr<Config>
    makeConfig(bool setup_admin)
    {
        auto p = std::make_unique<Config>();
        test::setupConfigForUnitTests(*p);
        // the default config has admin active
        // ...we remove them if setup_admin is false
        if (! setup_admin)
        {
            (*p)["port_rpc"].set("admin","");
            (*p)["port_ws"].set("admin","");
        }
        return p;
    }

    // test helper
    static bool checkArraySize(Json::Value const& val, unsigned int size)
    {
        return val.isArray() &&
               val.size() == size;
    }

    // test helper
    static bool checkMarker(Json::Value const& val)
    {
        return val.isMember(jss::marker) &&
               val[jss::marker].isString() &&
               val[jss::marker].asString().size() > 0;
    }

    void testCurrentLedgerToLimits(bool as_admin)
    {
        using namespace test::jtx;
        Env env(*this, makeConfig(as_admin));
        Account const gw ("gateway");
        auto const USD  = gw["USD"];
        env.fund(XRP(100000), gw);

        const int max_limit = 256; //would be 2048 for binary requests, no need to test that here

        for (auto i = 0; i < max_limit + 10; i++)
        {
            Account const bob (std::string("bob") + std::to_string(i));
            env.fund(XRP(1000), bob);
        }
        env.close();

        // no limit specified
        // with no limit specified, we get the max_limit if the total number of
        // accounts is greater than max, which it is here
        Json::Value jvParams;
        jvParams[jss::ledger_index] = "current";
        jvParams[jss::binary] = false;
        auto const jrr = env.rpc ("json", "ledger_data", jvParams.toStyledString())[jss::result];
        BEAST_EXPECT(jrr[jss::ledger_current_index].isIntegral() && jrr[jss::ledger_current_index].asInt() > 0);
        BEAST_EXPECT(checkMarker(jrr));
        BEAST_EXPECT(checkArraySize(jrr[jss::state], max_limit));

        // check limits values around the max_limit (+/- 1)
        for (auto delta = -1; delta <= 1; delta++)
        {
            jvParams[jss::limit] = max_limit + delta;
            auto const jrr = env.rpc ("json", "ledger_data", jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(checkArraySize(jrr[jss::state], (delta > 0 && !as_admin) ? max_limit : max_limit + delta ));
        }
    }

    void testCurrentLedgerBinary()
    {
        using namespace test::jtx;
        Env env(*this, makeConfig(false));
        Account const gw ("gateway");
        auto const USD  = gw["USD"];
        env.fund(XRP(100000), gw);

        const int num_accounts = 10;

        for (auto i = 0; i < num_accounts; i++)
        {
            Account const bob (std::string("bob") + std::to_string(i));
            env.fund(XRP(1000), bob);
        }
        env.close();

        // no limit specified
        // with no limit specified, we should get all of our fund entries
        // plus three more related to the gateway setup
        Json::Value jvParams;
        jvParams[jss::ledger_index] = "current";
        jvParams[jss::binary] = true;
        auto const jrr = env.rpc ("json", "ledger_data", jvParams.toStyledString())[jss::result];
        BEAST_EXPECT(jrr[jss::ledger_current_index].isIntegral() && jrr[jss::ledger_current_index].asInt() > 0);
        BEAST_EXPECT(! jrr.isMember(jss::marker));
        BEAST_EXPECT(checkArraySize(jrr[jss::state], num_accounts + 3));
    }

    void testBadInput()
    {
        using namespace test::jtx;
        Env env(*this);
        Account const gw ("gateway");
        auto const USD  = gw["USD"];
        Account const bob ("bob");

        env.fund(XRP(10000), gw, bob);
        env.trust(USD(1000), bob);

        {
            // bad limit
            Json::Value jvParams;
            jvParams[jss::limit]   = "0"; // NOT an integer
            auto const jrr = env.rpc ("json", "ledger_data", jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::status]        == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'limit', not integer.");
        }

        {
            // invalid marker
            Json::Value jvParams;
            jvParams[jss::marker]   = "NOT_A_MARKER";
            auto const jrr = env.rpc ("json", "ledger_data", jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::status]        == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'marker', not valid.");
        }

        {
            // invalid marker - not a string
            Json::Value jvParams;
            jvParams[jss::marker]   = 1;
            auto const jrr = env.rpc ("json", "ledger_data", jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "invalidParams");
            BEAST_EXPECT(jrr[jss::status]        == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "Invalid field 'marker', not valid.");
        }

        {
            // ask for a bad ledger index
            Json::Value jvParams;
            jvParams[jss::ledger_index]   = 10u;
            auto const jrr = env.rpc ("json", "ledger_data", jvParams.toStyledString())[jss::result];
            BEAST_EXPECT(jrr[jss::error]         == "lgrNotFound");
            BEAST_EXPECT(jrr[jss::status]        == "error");
            BEAST_EXPECT(jrr[jss::error_message] == "ledgerNotFound");
        }

    }

    void run()
    {
        testCurrentLedgerToLimits(true);
        testCurrentLedgerToLimits(false);
        testCurrentLedgerBinary();
        testBadInput();
    }

};

BEAST_DEFINE_TESTSUITE(LedgerData,app,ripple);

}

