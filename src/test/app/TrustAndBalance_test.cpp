//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2016 Ripple Labs Inc.

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
#include <ripple/beast/unit_test.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/SField.h>
#include <test/jtx/WSClient.h>

namespace ripple {

class TrustAndBalance_test : public beast::unit_test::suite
{
    static auto
    ledgerEntryState(
        test::jtx::Env & env,
        test::jtx::Account const& acct_a,
        test::jtx::Account const & acct_b,
        std::string const& currency)
    {
        Json::Value jvParams;
        jvParams[jss::ledger_index] = "current";
        jvParams[jss::ripple_state][jss::currency] = currency;
        jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
        jvParams[jss::ripple_state][jss::accounts].append(acct_a.human());
        jvParams[jss::ripple_state][jss::accounts].append(acct_b.human());
        return env.rpc ("json", "ledger_entry", to_string(jvParams))[jss::result];
    }

    void
    testPayNonexistent (FeatureBitset features)
    {
        testcase ("Payment to Nonexistent Account");
        using namespace test::jtx;

        Env env {*this, features};
        env (pay (env.master, "alice", XRP(1)), ter(tecNO_DST_INSUF_XRP));
        env.close();
    }

    void
    testTrustNonexistent ()
    {
        testcase ("Trust Nonexistent Account");
        using namespace test::jtx;

        Env env {*this};
        Account alice {"alice"};

        env (trust (env.master, alice["USD"](100)), ter(tecNO_DST));
    }

    void
    testCreditLimit ()
    {
        testcase ("Credit Limit");
        using namespace test::jtx;

        Env env {*this};
        Account gw {"gateway"};
        Account alice {"alice"};
        Account bob {"bob"};

        env.fund (XRP(10000), gw, alice, bob);
        env.close();

        // credit limit doesn't exist yet - verify ledger_entry
        // reflects this
        auto jrr = ledgerEntryState (env, gw, alice, "USD");
        BEAST_EXPECT(jrr[jss::error] == "entryNotFound");

        // now create a credit limit
        env (trust (alice, gw["USD"](800)));

        jrr = ledgerEntryState (env, gw, alice, "USD");
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName][jss::value] == "0");
        BEAST_EXPECT(
            jrr[jss::node][sfHighLimit.fieldName][jss::value] == "800");
        BEAST_EXPECT(
            jrr[jss::node][sfHighLimit.fieldName][jss::issuer] == alice.human());
        BEAST_EXPECT(
            jrr[jss::node][sfHighLimit.fieldName][jss::currency] == "USD");
        BEAST_EXPECT(
            jrr[jss::node][sfLowLimit.fieldName][jss::value] == "0");
        BEAST_EXPECT(
            jrr[jss::node][sfLowLimit.fieldName][jss::issuer] == gw.human());
        BEAST_EXPECT(
            jrr[jss::node][sfLowLimit.fieldName][jss::currency] == "USD");

        // modify the credit limit
        env (trust (alice, gw["USD"](700)));

        jrr = ledgerEntryState (env, gw, alice, "USD");
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName][jss::value] == "0");
        BEAST_EXPECT(
            jrr[jss::node][sfHighLimit.fieldName][jss::value] == "700");
        BEAST_EXPECT(
            jrr[jss::node][sfHighLimit.fieldName][jss::issuer] == alice.human());
        BEAST_EXPECT(
            jrr[jss::node][sfHighLimit.fieldName][jss::currency] == "USD");
        BEAST_EXPECT(
            jrr[jss::node][sfLowLimit.fieldName][jss::value] == "0");
        BEAST_EXPECT(
            jrr[jss::node][sfLowLimit.fieldName][jss::issuer] == gw.human());
        BEAST_EXPECT(
            jrr[jss::node][sfLowLimit.fieldName][jss::currency] == "USD");

        // set negative limit - expect failure
        env (trust (alice, gw["USD"](-1)), ter(temBAD_LIMIT));

        // set zero limit
        env (trust (alice, gw["USD"](0)));

        //ensure line is deleted
        jrr = ledgerEntryState (env, gw, alice, "USD");
        BEAST_EXPECT(jrr[jss::error] == "entryNotFound");

        // TODO Check in both owner books.

        // set another credit limit
        env (trust (alice, bob["USD"](600)));

        // set limit on other side
        env (trust (bob, alice["USD"](500)));

        // check the ledger state for the trust line
        jrr = ledgerEntryState (env, alice, bob, "USD");
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName][jss::value] == "0");
        BEAST_EXPECT(
            jrr[jss::node][sfHighLimit.fieldName][jss::value] == "500");
        BEAST_EXPECT(
            jrr[jss::node][sfHighLimit.fieldName][jss::issuer] == bob.human());
        BEAST_EXPECT(
            jrr[jss::node][sfHighLimit.fieldName][jss::currency] == "USD");
        BEAST_EXPECT(
            jrr[jss::node][sfLowLimit.fieldName][jss::value] == "600");
        BEAST_EXPECT(
            jrr[jss::node][sfLowLimit.fieldName][jss::issuer] == alice.human());
        BEAST_EXPECT(
            jrr[jss::node][sfLowLimit.fieldName][jss::currency] == "USD");
    }

    void
    testDirectRipple (FeatureBitset features)
    {
        testcase ("Direct Payment, Ripple");
        using namespace test::jtx;

        Env env {*this, features};
        Account alice {"alice"};
        Account bob {"bob"};

        env.fund (XRP(10000), alice, bob);
        env.close();

        env (trust (alice, bob["USD"](600)));
        env (trust (bob, alice["USD"](700)));

        // alice sends bob partial with alice as issuer
        env (pay (alice, bob, alice["USD"](24)));
        env.require (balance (bob, alice["USD"](24)));

        // alice sends bob more with bob as issuer
        env (pay (alice, bob, bob["USD"](33)));
        env.require (balance (bob, alice["USD"](57)));

        // bob sends back more than sent
        env (pay (bob, alice, bob["USD"](90)));
        env.require (balance (bob, alice["USD"](-33)));

        // alice sends to her limit
        env (pay (alice, bob, bob["USD"](733)));
        env.require (balance (bob, alice["USD"](700)));

        // bob sends to his limit
        env (pay (bob, alice, bob["USD"](1300)));
        env.require (balance (bob, alice["USD"](-600)));

        // bob sends past limit
        env (pay (bob, alice, bob["USD"](1)), ter(tecPATH_DRY));
        env.require (balance (bob, alice["USD"](-600)));
    }

    void
    testWithTransferFee (bool subscribe, bool with_rate, FeatureBitset features)
    {
        testcase(std::string("Direct Payment: ") +
                (with_rate ? "With " : "Without ") + " Xfer Fee, " +
                (subscribe ? "With " : "Without ") + " Subscribe");
        using namespace test::jtx;

        Env env {*this, features};
        auto wsc = test::makeWSClient(env.app().config());
        Account gw {"gateway"};
        Account alice {"alice"};
        Account bob {"bob"};

        env.fund (XRP(10000), gw, alice, bob);
        env.close();

        env (trust (alice, gw["AUD"](100)));
        env (trust (bob, gw["AUD"](100)));

        env (pay (gw, alice, alice["AUD"](1)));
        env.close();

        env.require (balance (alice, gw["AUD"](1)));

        // alice sends bob 1 AUD
        env (pay (alice, bob, gw["AUD"](1)));
        env.close();

        env.require (balance (alice, gw["AUD"](0)));
        env.require (balance (bob, gw["AUD"](1)));
        env.require (balance (gw, bob["AUD"](-1)));

        if(with_rate)
        {
            // set a transfer rate
            env (rate (gw, 1.1));
            env.close();
            // bob sends alice 0.5 AUD with a max to spend
            env (pay (bob, alice, gw["AUD"](0.5)), sendmax(gw["AUD"](0.55)));
        }
        else
        {
            // bob sends alice 0.5 AUD
            env (pay (bob, alice, gw["AUD"](0.5)));
        }

        env.require (balance (alice, gw["AUD"](0.5)));
        env.require (balance (bob, gw["AUD"](with_rate ? 0.45 : 0.5)));
        env.require (balance (gw, bob["AUD"](with_rate ? -0.45 : -0.5)));

        if (subscribe)
        {
            Json::Value jvs;
            jvs[jss::accounts] = Json::arrayValue;
            jvs[jss::accounts].append(gw.human());
            jvs[jss::streams] = Json::arrayValue;
            jvs[jss::streams].append("transactions");
            jvs[jss::streams].append("ledger");
            auto jv = wsc->invoke("subscribe", jvs);
            BEAST_EXPECT(jv[jss::status] == "success");

            env.close();

            using namespace std::chrono_literals;
            BEAST_EXPECT(wsc->findMsg(5s,
                [](auto const& jv)
                {
                    auto const& t = jv[jss::transaction];
                    return t[jss::TransactionType] == "Payment";
                }));
            BEAST_EXPECT(wsc->findMsg(5s,
                [](auto const& jv)
                {
                    return jv[jss::type] == "ledgerClosed";
                }));

            BEAST_EXPECT(wsc->invoke("unsubscribe",jv)[jss::status] == "success");
        }
    }

    void
    testWithPath (FeatureBitset features)
    {
        testcase ("Payments With Paths and Fees");
        using namespace test::jtx;

        Env env {*this, features};
        Account gw {"gateway"};
        Account alice {"alice"};
        Account bob {"bob"};

        env.fund (XRP(10000), gw, alice, bob);
        env.close();

        // set a transfer rate
        env (rate (gw, 1.1));

        env (trust (alice, gw["AUD"](100)));
        env (trust (bob, gw["AUD"](100)));

        env (pay (gw, alice, alice["AUD"](4.4)));
        env.require (balance (alice, gw["AUD"](4.4)));

        // alice sends gw issues to bob with a max spend that allows for the
        // xfer rate
        env (pay (alice, bob, gw["AUD"](1)), sendmax(gw["AUD"](1.1)));
        env.require (balance (alice, gw["AUD"](3.3)));
        env.require (balance (bob, gw["AUD"](1)));

        // alice sends bob issues to bob with a max spend
        env (pay (alice, bob, bob["AUD"](1)), sendmax(gw["AUD"](1.1)));
        env.require (balance (alice, gw["AUD"](2.2)));
        env.require (balance (bob, gw["AUD"](2)));

        // alice sends gw issues to bob with a max spend
        env (pay (alice, bob, gw["AUD"](1)), sendmax(alice["AUD"](1.1)));
        env.require (balance (alice, gw["AUD"](1.1)));
        env.require (balance (bob, gw["AUD"](3)));

        // alice sends bob issues to bob with a max spend in alice issues.
        // expect fail since gw is not involved
        env (pay (alice, bob, bob["AUD"](1)), sendmax(alice["AUD"](1.1)),
            ter(tecPATH_DRY));

        env.require (balance (alice, gw["AUD"](1.1)));
        env.require (balance (bob, gw["AUD"](3)));
    }

    void
    testIndirect (FeatureBitset features)
    {
        testcase ("Indirect Payment");
        using namespace test::jtx;

        Env env {*this, features};
        Account gw {"gateway"};
        Account alice {"alice"};
        Account bob {"bob"};

        env.fund (XRP(10000), gw, alice, bob);
        env.close();

        env (trust (alice, gw["USD"](600)));
        env (trust (bob, gw["USD"](700)));

        env (pay (gw, alice, alice["USD"](70)));
        env (pay (gw, bob, bob["USD"](50)));

        env.require (balance (alice, gw["USD"](70)));
        env.require (balance (bob, gw["USD"](50)));

        // alice sends more than has to issuer: 100 out of 70
        env (pay (alice, gw, gw["USD"](100)), ter(tecPATH_PARTIAL));

        // alice sends more than has to bob: 100 out of 70
        env (pay (alice, bob, gw["USD"](100)), ter(tecPATH_PARTIAL));

        env.close();

        env.require (balance (alice, gw["USD"](70)));
        env.require (balance (bob, gw["USD"](50)));

        // send with an account path
        env (pay (alice, bob, gw["USD"](5)), test::jtx::path(gw));

        env.require (balance (alice, gw["USD"](65)));
        env.require (balance (bob, gw["USD"](55)));
    }

    void
    testIndirectMultiPath (bool with_rate, FeatureBitset features)
    {
        testcase (std::string("Indirect Payment, Multi Path, ") +
                (with_rate ? "With " : "Without ") + " Xfer Fee, ");
        using namespace test::jtx;

        Env env {*this, features};
        Account gw {"gateway"};
        Account amazon {"amazon"};
        Account alice {"alice"};
        Account bob {"bob"};
        Account carol {"carol"};

        env.fund (XRP(10000), gw, amazon, alice, bob, carol);
        env.close();

        env (trust (amazon, gw["USD"](2000)));
        env (trust (bob, alice["USD"](600)));
        env (trust (bob, gw["USD"](1000)));
        env (trust (carol, alice["USD"](700)));
        env (trust (carol, gw["USD"](1000)));

        if (with_rate)
            env (rate (gw, 1.1));

        env (pay (gw, bob, bob["USD"](100)));
        env (pay (gw, carol, carol["USD"](100)));
        env.close();

        // alice pays amazon via multiple paths
        if (with_rate)
            env (pay (alice, amazon, gw["USD"](150)),
                sendmax(alice["USD"](200)),
                test::jtx::path(bob),
                test::jtx::path(carol));
        else
            env (pay (alice, amazon, gw["USD"](150)),
                test::jtx::path(bob),
                test::jtx::path(carol));

        if (with_rate)
        {
            // 65.00000000000001 is correct.
            // This is result of limited precision.
            env.require (balance (
                alice,
                STAmount(
                    carol["USD"].issue(),
                    6500000000000001ull,
                    -14,
                    false,
                    true,
                    STAmount::unchecked{})));
            env.require (balance (carol, gw["USD"](35)));
        }
        else
        {
            env.require (balance (alice, carol["USD"](-50)));
            env.require (balance (carol, gw["USD"](50)));
        }
        env.require (balance (alice, bob["USD"](-100)));
        env.require (balance (amazon, gw["USD"](150)));
        env.require (balance (bob, gw["USD"](0)));
    }

    void
    testInvoiceID (FeatureBitset features)
    {
        testcase ("Set Invoice ID on Payment");
        using namespace test::jtx;

        Env env {*this, features};
        Account alice {"alice"};
        auto wsc = test::makeWSClient(env.app().config());

        env.fund (XRP(10000), alice);
        env.close();

        Json::Value jvs;
        jvs[jss::accounts] = Json::arrayValue;
        jvs[jss::accounts].append(env.master.human());
        jvs[jss::streams] = Json::arrayValue;
        jvs[jss::streams].append("transactions");
        BEAST_EXPECT(wsc->invoke("subscribe", jvs)[jss::status] == "success");

        Json::Value jv;
        auto tx = env.jt (
            pay (env.master, alice, XRP(10000)),
            json(sfInvoiceID.fieldName, "DEADBEEF"));
        jv[jss::tx_blob] = strHex (tx.stx->getSerializer().slice());
        auto jrr = wsc->invoke("submit", jv) [jss::result];
        BEAST_EXPECT(jrr[jss::status] == "success");
        BEAST_EXPECT(jrr[jss::tx_json][sfInvoiceID.fieldName] ==
            "0000000000000000"
            "0000000000000000"
            "0000000000000000"
            "00000000DEADBEEF");
        env.close();

        using namespace std::chrono_literals;
        BEAST_EXPECT(wsc->findMsg(2s,
            [](auto const& jv)
            {
                auto const& t = jv[jss::transaction];
                return
                    t[jss::TransactionType] == "Payment" &&
                    t[sfInvoiceID.fieldName] ==
                        "0000000000000000"
                        "0000000000000000"
                        "0000000000000000"
                        "00000000DEADBEEF";
            }));

        BEAST_EXPECT(wsc->invoke("unsubscribe",jv)[jss::status] == "success");
    }

public:
    void run () override
    {
        testTrustNonexistent ();
        testCreditLimit ();

        auto testWithFeatures = [this](FeatureBitset features) {
            testPayNonexistent(features);
            testDirectRipple(features);
            testWithTransferFee(false, false, features);
            testWithTransferFee(false, true, features);
            testWithTransferFee(true, false, features);
            testWithTransferFee(true, true, features);
            testWithPath(features);
            testIndirect(features);
            testIndirectMultiPath(true, features);
            testIndirectMultiPath(false, features);
            testInvoiceID(features);
        };

        using namespace test::jtx;
        auto const sa = supported_amendments();
        testWithFeatures(sa - featureFlow - fix1373 - featureFlowCross);
        testWithFeatures(sa               - fix1373 - featureFlowCross);
        testWithFeatures(sa                          -featureFlowCross);
        testWithFeatures(sa);
    }
};

BEAST_DEFINE_TESTSUITE_PRIO (TrustAndBalance, app, ripple, 1);

}  // ripple


