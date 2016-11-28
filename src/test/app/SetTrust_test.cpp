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
#include <test/support/jtx.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/JsonFields.h>

namespace ripple {

namespace test {

class SetTrust_test : public beast::unit_test::suite
{
public:


    void testFreeTrustlines(bool thirdLineCreatesLE, bool createOnHighAcct)
    {
        if (thirdLineCreatesLE)
            testcase("Allow two free trustlines");
        else
            testcase("Dynamic reserve for trustline");

        using namespace jtx;
        Env env(*this);

        auto const gwA = Account{ "gwA" };
        auto const gwB = Account{ "gwB" };
        auto const acctC = Account{ "acctC" };
        auto const acctD = Account{ "acctD" };

        auto const & creator  = createOnHighAcct ? acctD : acctC;
        auto const & assistor = createOnHighAcct ? acctC : acctD;

        auto const txFee = env.current()->fees().base;
        auto const baseReserve = env.current()->fees().accountReserve(0);
        auto const threelineReserve = env.current()->fees().accountReserve(3);

        env.fund(XRP(10000), gwA, gwB, assistor);

        // Fund creator with ...
        env.fund(baseReserve /* enough to hold an account */
            + drops(3*txFee) /* and to pay for 3 transactions */, creator);

        env(trust(creator, gwA["USD"](100)), require(lines(creator,1)));
        env(trust(creator, gwB["USD"](100)), require(lines(creator,2)));

        if (thirdLineCreatesLE)
        {
            // creator does not have enough for the third trust line
            env(trust(creator, assistor["USD"](100)),
                ter(tecNO_LINE_INSUF_RESERVE), require(lines(creator, 2)));
        }
        else
        {
            // First establish opposite trust direction from assistor
            env(trust(assistor,creator["USD"](100)), require(lines(creator,3)));

            // creator does not have enough to create the other direction on
            //the existing trust line ledger entry
            env(trust(creator,assistor["USD"](100)),ter(tecINSUF_RESERVE_LINE));
        }

        // Fund creator additional amount to cover
        env(pay(env.master,creator,STAmount{ threelineReserve - baseReserve }));

        if (thirdLineCreatesLE)
        {
            env(trust(creator,assistor["USD"](100)),require(lines(creator, 3)));
        }
        else
        {
            env(trust(creator, assistor["USD"](100)),require(lines(creator,3)));

            Json::Value jv;
            jv["account"] = creator.human();
            auto const lines = env.rpc("json","account_lines", to_string(jv));
            // Verify that all lines have 100 limit from creator
            BEAST_EXPECT(lines[jss::result][jss::lines].isArray());
            BEAST_EXPECT(lines[jss::result][jss::lines].size() == 3);
            for (auto const & line : lines[jss::result][jss::lines])
            {
                BEAST_EXPECT(line[jss::limit] == "100");
            }
        }
    }

    Json::Value trust_explicit_amt(jtx::Account const & a, STAmount const & amt)
    {
        Json::Value jv;
        jv[jss::Account] = a.human();
        jv[jss::LimitAmount] = amt.getJson(0);
        jv[jss::TransactionType] = "TrustSet";
        jv[jss::Flags] = 0;
        return jv;
    }

    void testMalformedTransaction()
    {
        testcase("SetTrust checks for malformed transactions");

        using namespace jtx;
        Env env{ *this };

        auto const gw = Account{ "gateway" };
        auto const alice = Account{ "alice" };
        env.fund(XRP(10000), gw, alice);

        // Require valid tf flags
        for (std::uint64_t badFlag = 1u ;
            badFlag <= std::numeric_limits<std::uint32_t>::max(); badFlag *= 2)
        {
            if( badFlag & tfTrustSetMask)
                env(trust(alice, gw["USD"](100),
                    static_cast<std::uint32_t>(badFlag)), ter(temINVALID_FLAG));
        }

        // trust amount can't be XRP
        env(trust_explicit_amt(alice, drops(10000)), ter(temBAD_LIMIT));

        // trust amount can't be badCurrency IOU
        env(trust_explicit_amt(alice, gw[ to_string(badCurrency())](100)),
            ter(temBAD_CURRENCY));

        // trust amount can't be negative
        env(trust(alice, gw["USD"](-1000)), ter(temBAD_LIMIT));

        // trust amount can't be from invalid issuer
        env(trust_explicit_amt(alice, STAmount{Issue{to_currency("USD"),
            noAccount()}, 100 }), ter(temDST_NEEDED));

        // trust cannot be to self
        env(trust(alice, alice["USD"](100)), ter(temDST_IS_SRC));

        // tfSetAuth flag should not be set if not required by lsfRequireAuth
        env(trust(alice, gw["USD"](100), tfSetfAuth), ter(tefNO_AUTH_REQUIRED));
    }

    void testModifyQualityOfTrustline(bool createQuality, bool createOnHighAcct)
    {
        testcase << "SetTrust " << (createQuality ? "creates" : "removes")
            << " quality of trustline for "
            << (createOnHighAcct ? "high" : "low" )
            << " account" ;

        using namespace jtx;
        Env env{ *this };

        auto const alice = Account{ "alice" };
        auto const bob = Account{ "bob" };

        auto const & fromAcct = createOnHighAcct ? alice : bob;
        auto const & toAcct = createOnHighAcct ? bob : alice;

        env.fund(XRP(10000), fromAcct, toAcct);


        auto txWithoutQuality = trust(toAcct, fromAcct["USD"](100));
        txWithoutQuality["QualityIn"] = "0";
        txWithoutQuality["QualityOut"] = "0";

        auto txWithQuality = txWithoutQuality;
        txWithQuality["QualityIn"] = "1000";
        txWithQuality["QualityOut"] = "1000";

        auto & tx1 = createQuality ? txWithQuality : txWithoutQuality;
        auto & tx2 = createQuality ? txWithoutQuality : txWithQuality;

        auto check_quality = [&](const bool exists)
        {
            Json::Value jv;
            jv["account"] = toAcct.human();
            auto const lines = env.rpc("json","account_lines", to_string(jv));
            auto quality = exists ? 1000 : 0;
            BEAST_EXPECT(lines[jss::result][jss::lines].isArray());
            BEAST_EXPECT(lines[jss::result][jss::lines].size() == 1);
            BEAST_EXPECT(lines[jss::result][jss::lines][0u][jss::quality_in]
                == quality);
            BEAST_EXPECT(lines[jss::result][jss::lines][0u][jss::quality_out]
                == quality);
        };


        env(tx1, require(lines(toAcct, 1)), require(lines(fromAcct, 1)));
        check_quality(createQuality);

        env(tx2, require(lines(toAcct, 1)), require(lines(fromAcct, 1)));
        check_quality(!createQuality);

    }

    void run()
    {
        testFreeTrustlines(true, false);
        testFreeTrustlines(false, true);
        testFreeTrustlines(false, true);
        // true, true case doesn't matter since creating a trustline ledger
        // entry requires reserve from the creator
        // independent of hi/low account ids for endpoints
        testMalformedTransaction();
        testModifyQualityOfTrustline(false, false);
        testModifyQualityOfTrustline(false, true);
        testModifyQualityOfTrustline(true, false);
        testModifyQualityOfTrustline(true, true);
    }
};
BEAST_DEFINE_TESTSUITE(SetTrust, app, ripple);
} // test
} // ripple
