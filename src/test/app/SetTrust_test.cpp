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
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

namespace ripple {

namespace test {

class SetTrust_test : public beast::unit_test::suite
{
    FeatureBitset const disallowIncoming{featureDisallowIncoming};

public:
    void
    testTrustLineDelete()
    {
        testcase(
            "Test deletion of trust lines: revert trust line limit to zero");

        using namespace jtx;
        Env env(*this);

        Account const alice = Account{"alice"};
        Account const becky = Account{"becky"};

        env.fund(XRP(10000), becky, alice);
        env.close();

        // becky wants to hold at most 50 tokens of alice["USD"]
        // becky is the customer, alice is the issuer
        // becky can be sent at most 50 tokens of alice's USD
        env(trust(becky, alice["USD"](50)));
        env.close();

        // Since the settings of the trust lines are non-default for both
        // alice and becky, both of them will be charged an owner reserve
        // Irrespective of whether the issuer or the customer initiated
        // the trust-line creation, both will be charged
        env.require(lines(alice, 1));
        env.require(lines(becky, 1));

        // Fetch the trust-lines via RPC for verification
        Json::Value jv;
        jv["account"] = becky.human();
        auto beckyLines = env.rpc("json", "account_lines", to_string(jv));

        jv["account"] = alice.human();
        auto aliceLines = env.rpc("json", "account_lines", to_string(jv));

        BEAST_EXPECT(aliceLines[jss::result][jss::lines].size() == 1);
        BEAST_EXPECT(beckyLines[jss::result][jss::lines].size() == 1);

        //         reset the trust line limits to zero
        env(trust(becky, alice["USD"](0)));
        env.close();

        // the reset of the trust line limits deletes the trust-line
        // this occurs despite the authorization of the trust-line by the
        // issuer(alice, in this unit test)
        env.require(lines(becky, 0));
        env.require(lines(alice, 0));

        // second verification check via RPC calls
        jv["account"] = becky.human();
        beckyLines = env.rpc("json", "account_lines", to_string(jv));

        jv["account"] = alice.human();
        aliceLines = env.rpc("json", "account_lines", to_string(jv));

        BEAST_EXPECT(aliceLines[jss::result][jss::lines].size() == 0);
        BEAST_EXPECT(beckyLines[jss::result][jss::lines].size() == 0);

        // additionally, verify that account_objects is an empty array
        jv["account"] = becky.human();
        auto const beckyObj = env.rpc("json", "account_objects", to_string(jv));
        BEAST_EXPECT(beckyObj[jss::result][jss::account_objects].size() == 0);

        jv["account"] = alice.human();
        auto const aliceObj = env.rpc("json", "account_objects", to_string(jv));
        BEAST_EXPECT(aliceObj[jss::result][jss::account_objects].size() == 0);
    }

    void
    testTrustLineResetWithAuthFlag()
    {
        testcase(
            "Reset trust line limit with Authorised Lines: Verify "
            "deletion of trust lines");

        using namespace jtx;
        Env env(*this);

        Account const alice = Account{"alice"};
        Account const becky = Account{"becky"};

        env.fund(XRP(10000), becky, alice);
        env.close();

        // alice wants to ensure that all holders of her tokens are authorised
        env(fset(alice, asfRequireAuth));
        env.close();

        // becky wants to hold at most 50 tokens of alice["USD"]
        // becky is the customer, alice is the issuer
        // becky can be sent at most 50 tokens of alice's USD
        env(trust(becky, alice["USD"](50)));
        env.close();

        // alice authorizes becky to hold alice["USD"] tokens
        env(trust(alice, alice["USD"](0), becky, tfSetfAuth));
        env.close();

        // Since the settings of the trust lines are non-default for both
        // alice and becky, both of them will be charged an owner reserve
        // Irrespective of whether the issuer or the customer initiated
        // the trust-line creation, both will be charged
        env.require(lines(alice, 1));
        env.require(lines(becky, 1));

        // Fetch the trust-lines via RPC for verification
        Json::Value jv;
        jv["account"] = becky.human();
        auto beckyLines = env.rpc("json", "account_lines", to_string(jv));

        jv["account"] = alice.human();
        auto aliceLines = env.rpc("json", "account_lines", to_string(jv));

        BEAST_EXPECT(aliceLines[jss::result][jss::lines].size() == 1);
        BEAST_EXPECT(beckyLines[jss::result][jss::lines].size() == 1);

        //         reset the trust line limits to zero
        env(trust(becky, alice["USD"](0)));
        env.close();

        // the reset of the trust line limits deletes the trust-line
        // this occurs despite the authorization of the trust-line by the
        // issuer(alice, in this unit test)
        env.require(lines(becky, 0));
        env.require(lines(alice, 0));

        // second verification check via RPC calls
        jv["account"] = becky.human();
        beckyLines = env.rpc("json", "account_lines", to_string(jv));

        jv["account"] = alice.human();
        aliceLines = env.rpc("json", "account_lines", to_string(jv));

        BEAST_EXPECT(aliceLines[jss::result][jss::lines].size() == 0);
        BEAST_EXPECT(beckyLines[jss::result][jss::lines].size() == 0);
    }

    void
    testFreeTrustlines(
        FeatureBitset features,
        bool thirdLineCreatesLE,
        bool createOnHighAcct)
    {
        if (thirdLineCreatesLE)
            testcase("Allow two free trustlines");
        else
            testcase("Dynamic reserve for trustline");

        using namespace jtx;
        Env env(*this, features);

        auto const gwA = Account{"gwA"};
        auto const gwB = Account{"gwB"};
        auto const acctC = Account{"acctC"};
        auto const acctD = Account{"acctD"};

        auto const& creator = createOnHighAcct ? acctD : acctC;
        auto const& assistor = createOnHighAcct ? acctC : acctD;

        auto const txFee = env.current()->fees().base;
        auto const baseReserve = env.current()->fees().accountReserve(0);
        auto const threelineReserve = env.current()->fees().accountReserve(3);

        env.fund(XRP(10000), gwA, gwB, assistor);

        // Fund creator with ...
        env.fund(
            baseReserve /* enough to hold an account */
                + drops(3 * txFee) /* and to pay for 3 transactions */,
            creator);

        env(trust(creator, gwA["USD"](100)), require(lines(creator, 1)));
        env(trust(creator, gwB["USD"](100)), require(lines(creator, 2)));

        if (thirdLineCreatesLE)
        {
            // creator does not have enough for the third trust line
            env(trust(creator, assistor["USD"](100)),
                ter(tecNO_LINE_INSUF_RESERVE),
                require(lines(creator, 2)));
        }
        else
        {
            // First establish opposite trust direction from assistor
            env(trust(assistor, creator["USD"](100)),
                require(lines(creator, 3)));

            // creator does not have enough to create the other direction on
            // the existing trust line ledger entry
            env(trust(creator, assistor["USD"](100)),
                ter(tecINSUF_RESERVE_LINE));
        }

        // Fund creator additional amount to cover
        env(pay(env.master, creator, STAmount{threelineReserve - baseReserve}));

        if (thirdLineCreatesLE)
        {
            env(trust(creator, assistor["USD"](100)),
                require(lines(creator, 3)));
        }
        else
        {
            env(trust(creator, assistor["USD"](100)),
                require(lines(creator, 3)));

            Json::Value jv;
            jv["account"] = creator.human();
            auto const lines = env.rpc("json", "account_lines", to_string(jv));
            // Verify that all lines have 100 limit from creator
            BEAST_EXPECT(lines[jss::result][jss::lines].isArray());
            BEAST_EXPECT(lines[jss::result][jss::lines].size() == 3);
            for (auto const& line : lines[jss::result][jss::lines])
            {
                BEAST_EXPECT(line[jss::limit] == "100");
            }
        }
    }

    void
    testTicketSetTrust(FeatureBitset features)
    {
        testcase("SetTrust using a ticket");

        using namespace jtx;

        //  Verify that TrustSet transactions can use tickets.
        Env env{*this, features};
        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const USD = gw["USD"];

        env.fund(XRP(10000), gw, alice);
        env.close();

        // Cannot pay alice without a trustline.
        env(pay(gw, alice, USD(200)), ter(tecPATH_DRY));
        env.close();

        // Create a ticket.
        std::uint32_t const ticketSeq{env.seq(alice) + 1};
        env(ticket::create(alice, 1));
        env.close();

        // Use that ticket to create a trust line.
        env(trust(alice, USD(1000)), ticket::use(ticketSeq));
        env.close();

        // Now the payment succeeds.
        env(pay(gw, alice, USD(200)));
        env.close();
    }

    Json::Value
    trust_explicit_amt(jtx::Account const& a, STAmount const& amt)
    {
        Json::Value jv;
        jv[jss::Account] = a.human();
        jv[jss::LimitAmount] = amt.getJson(JsonOptions::none);
        jv[jss::TransactionType] = jss::TrustSet;
        jv[jss::Flags] = 0;
        return jv;
    }

    void
    testMalformedTransaction(FeatureBitset features)
    {
        testcase("SetTrust checks for malformed transactions");

        using namespace jtx;
        Env env{*this, features};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        env.fund(XRP(10000), gw, alice);

        // Require valid tf flags
        for (std::uint64_t badFlag = 1u;
             badFlag <= std::numeric_limits<std::uint32_t>::max();
             badFlag *= 2)
        {
            if (badFlag & tfTrustSetMask)
                env(trust(
                        alice,
                        gw["USD"](100),
                        static_cast<std::uint32_t>(badFlag)),
                    ter(temINVALID_FLAG));
        }

        // trust amount can't be XRP
        env(trust_explicit_amt(alice, drops(10000)), ter(temBAD_LIMIT));

        // trust amount can't be badCurrency IOU
        env(trust_explicit_amt(alice, gw[to_string(badCurrency())](100)),
            ter(temBAD_CURRENCY));

        // trust amount can't be negative
        env(trust(alice, gw["USD"](-1000)), ter(temBAD_LIMIT));

        // trust amount can't be from invalid issuer
        env(trust_explicit_amt(
                alice, STAmount{Issue{to_currency("USD"), noAccount()}, 100}),
            ter(temDST_NEEDED));

        // trust cannot be to self
        env(trust(alice, alice["USD"](100)), ter(temDST_IS_SRC));

        // tfSetAuth flag should not be set if not required by lsfRequireAuth
        env(trust(alice, gw["USD"](100), tfSetfAuth), ter(tefNO_AUTH_REQUIRED));
    }

    void
    testExceedTrustLineLimit()
    {
        testcase(
            "Ensure that trust line limits are respected in payment "
            "transactions");

        using namespace jtx;
        Env env{*this};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        env.fund(XRP(10000), gw, alice);

        // alice wants to hold at most 100 of gw's USD tokens
        env(trust(alice, gw["USD"](100)));
        env.close();

        // send a payment for a large quantity through the trust line
        env(pay(gw, alice, gw["USD"](200)), ter(tecPATH_PARTIAL));
        env.close();

        // on the other hand, smaller payments should succeed
        env(pay(gw, alice, gw["USD"](20)));
        env.close();
    }

    void
    testAuthFlagTrustLines()
    {
        testcase(
            "Ensure that authorised trust lines do not allow payments "
            "from unauthorised counter-parties");

        using namespace jtx;
        Env env{*this};

        auto const bob = Account{"bob"};
        auto const alice = Account{"alice"};
        env.fund(XRP(10000), bob, alice);

        // alice wants to ensure that all holders of her tokens are authorised
        env(fset(alice, asfRequireAuth));
        env.close();

        // create a trust line from bob to alice. bob wants to hold at most
        // 100 of alice's USD tokens. Note: alice hasn't authorised this
        // trust line yet.
        env(trust(bob, alice["USD"](100)));
        env.close();

        // send a payment from alice to bob, validate that the payment fails
        env(pay(alice, bob, alice["USD"](10)), ter(tecPATH_DRY));
        env.close();
    }

    void
    testTrustLineLimitsWithRippling()
    {
        testcase(
            "Check that trust line limits are respected in conjunction "
            "with rippling feature");

        using namespace jtx;
        Env env{*this};

        auto const bob = Account{"bob"};
        auto const alice = Account{"alice"};
        env.fund(XRP(10000), bob, alice);

        // create a trust line from bob to alice. bob wants to hold at most
        // 100 of alice's USD tokens.
        env(trust(bob, alice["USD"](100)));
        env.close();

        // archetypical payment transaction from alice to bob must succeed
        env(pay(alice, bob, alice["USD"](20)), ter(tesSUCCESS));
        env.close();

        // Issued tokens are fungible. i.e. alice's USD is identical to bob's
        // USD
        env(pay(bob, alice, bob["USD"](10)), ter(tesSUCCESS));
        env.close();

        // bob cannot place alice in his debt i.e. alice's balance of the USD
        // tokens cannot go below zero.
        env(pay(bob, alice, bob["USD"](11)), ter(tecPATH_PARTIAL));
        env.close();

        // payments that respect the trust line limits of alice should succeed
        env(pay(bob, alice, bob["USD"](10)), ter(tesSUCCESS));
        env.close();
    }

    void
    testModifyQualityOfTrustline(
        FeatureBitset features,
        bool createQuality,
        bool createOnHighAcct)
    {
        testcase << "SetTrust " << (createQuality ? "creates" : "removes")
                 << " quality of trustline for "
                 << (createOnHighAcct ? "high" : "low") << " account";

        using namespace jtx;
        Env env{*this, features};

        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};

        auto const& fromAcct = createOnHighAcct ? alice : bob;
        auto const& toAcct = createOnHighAcct ? bob : alice;

        env.fund(XRP(10000), fromAcct, toAcct);

        auto txWithoutQuality = trust(toAcct, fromAcct["USD"](100));
        txWithoutQuality["QualityIn"] = "0";
        txWithoutQuality["QualityOut"] = "0";

        auto txWithQuality = txWithoutQuality;
        txWithQuality["QualityIn"] = "1000";
        txWithQuality["QualityOut"] = "1000";

        auto& tx1 = createQuality ? txWithQuality : txWithoutQuality;
        auto& tx2 = createQuality ? txWithoutQuality : txWithQuality;

        auto check_quality = [&](const bool exists) {
            Json::Value jv;
            jv["account"] = toAcct.human();
            auto const lines = env.rpc("json", "account_lines", to_string(jv));
            auto quality = exists ? 1000 : 0;
            BEAST_EXPECT(lines[jss::result][jss::lines].isArray());
            BEAST_EXPECT(lines[jss::result][jss::lines].size() == 1);
            BEAST_EXPECT(
                lines[jss::result][jss::lines][0u][jss::quality_in] == quality);
            BEAST_EXPECT(
                lines[jss::result][jss::lines][0u][jss::quality_out] ==
                quality);
        };

        env(tx1, require(lines(toAcct, 1)), require(lines(fromAcct, 1)));
        check_quality(createQuality);

        env(tx2, require(lines(toAcct, 1)), require(lines(fromAcct, 1)));
        check_quality(!createQuality);
    }

    void
    testDisallowIncoming(FeatureBitset features)
    {
        testcase("Create trustline with disallow incoming");

        using namespace test::jtx;

        // test flag doesn't set unless amendment enabled
        {
            Env env{*this, features - disallowIncoming};
            Account const alice{"alice"};
            env.fund(XRP(10000), alice);
            env(fset(alice, asfDisallowIncomingTrustline));
            env.close();
            auto const sle = env.le(alice);
            uint32_t flags = sle->getFlags();
            BEAST_EXPECT(!(flags & lsfDisallowIncomingTrustline));
        }

        // fixDisallowIncomingV1
        {
            for (bool const withFix : {true, false})
            {
                auto const amend = withFix
                    ? features | disallowIncoming
                    : (features | disallowIncoming) - fixDisallowIncomingV1;

                Env env{*this, amend};
                auto const dist = Account("dist");
                auto const gw = Account("gw");
                auto const USD = gw["USD"];
                auto const distUSD = dist["USD"];

                env.fund(XRP(1000), gw, dist);
                env.close();

                env(fset(gw, asfRequireAuth));
                env.close();

                env(fset(dist, asfDisallowIncomingTrustline));
                env.close();

                env(trust(dist, USD(10000)));
                env.close();

                // withFix: can set trustline
                // withOutFix: cannot set trustline
                auto const trustResult =
                    withFix ? ter(tesSUCCESS) : ter(tecNO_PERMISSION);
                env(trust(gw, distUSD(10000)),
                    txflags(tfSetfAuth),
                    trustResult);
                env.close();

                auto const txResult =
                    withFix ? ter(tesSUCCESS) : ter(tecPATH_DRY);
                env(pay(gw, dist, USD(1000)), txResult);
                env.close();
            }
        }

        Env env{*this, features | disallowIncoming};

        auto const gw = Account{"gateway"};
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const USD = gw["USD"];

        env.fund(XRP(10000), gw, alice, bob);
        env.close();

        // Set flag on gateway
        env(fset(gw, asfDisallowIncomingTrustline));
        env.close();

        // Create a trustline which will fail
        env(trust(alice, USD(1000)), ter(tecNO_PERMISSION));
        env.close();

        // Unset the flag
        env(fclear(gw, asfDisallowIncomingTrustline));
        env.close();

        // Create a trustline which will now succeed
        env(trust(alice, USD(1000)));
        env.close();

        // Now the payment succeeds.
        env(pay(gw, alice, USD(200)));
        env.close();

        // Set flag on gateway again
        env(fset(gw, asfDisallowIncomingTrustline));
        env.close();

        // Destroy the balance by sending it back
        env(pay(gw, alice, USD(200)));
        env.close();

        // The trustline still exists in default state
        // So a further payment should work
        env(pay(gw, alice, USD(200)));
        env.close();

        // Also set the flag on bob
        env(fset(bob, asfDisallowIncomingTrustline));
        env.close();

        // But now bob can't open a trustline because he didn't already have one
        env(trust(bob, USD(1000)), ter(tecNO_PERMISSION));
        env.close();

        // The gateway also can't open this trustline because bob has the flag
        // set
        env(trust(gw, bob["USD"](1000)), ter(tecNO_PERMISSION));
        env.close();

        // Unset the flag only on the gateway
        env(fclear(gw, asfDisallowIncomingTrustline));
        env.close();

        // Now bob can open a trustline
        env(trust(bob, USD(1000)));
        env.close();

        // And the gateway can send bob a balance
        env(pay(gw, bob, USD(200)));
        env.close();
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testFreeTrustlines(features, true, false);
        testFreeTrustlines(features, false, true);
        testFreeTrustlines(features, false, true);
        // true, true case doesn't matter since creating a trustline ledger
        // entry requires reserve from the creator
        // independent of hi/low account ids for endpoints
        testTicketSetTrust(features);
        testMalformedTransaction(features);
        testModifyQualityOfTrustline(features, false, false);
        testModifyQualityOfTrustline(features, false, true);
        testModifyQualityOfTrustline(features, true, false);
        testModifyQualityOfTrustline(features, true, true);
        testDisallowIncoming(features);
        testTrustLineResetWithAuthFlag();
        testTrustLineDelete();
        testExceedTrustLineLimit();
        testAuthFlagTrustLines();
        testTrustLineLimitsWithRippling();
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        auto const sa = supported_amendments();
        testWithFeats(sa - disallowIncoming);
        testWithFeats(sa);
    }
};
BEAST_DEFINE_TESTSUITE(SetTrust, app, ripple);
}  // namespace test
}  // namespace ripple
