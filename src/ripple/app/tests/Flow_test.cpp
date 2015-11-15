//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.
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

#include <BeastConfig.h>
#include <ripple/test/jtx.h>
#include <ripple/app/paths/impl/Steps.h>
#include <ripple/basics/contract.h>
#include <ripple/core/Config.h>
#include <ripple/ledger/tests/PathSet.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/JsonFields.h>
namespace ripple {
namespace test {

struct Flow_test;

struct DirectStepInfo
{
    AccountID src;
    AccountID dst;
    Currency currency;
};

struct XRPEndpointStepInfo
{
    AccountID acc;
};

enum class TrustFlag {freeze, auth};

/*constexpr*/ std::uint32_t trustFlag (TrustFlag f, bool useHigh)
{
    switch(f)
    {
        case TrustFlag::freeze:
            if (useHigh)
                return lsfHighFreeze;
            return lsfLowFreeze;
        case TrustFlag::auth:
            if (useHigh)
                return lsfHighAuth;
            return lsfLowAuth;
    }
    return 0; // Silence warning about end of non-void function
}

bool getTrustFlag (jtx::Env const& env,
    jtx::Account const& src,
    jtx::Account const& dst,
    Currency const& cur,
    TrustFlag flag)
{
    if (auto sle = env.le (keylet::line (src, dst, cur)))
    {
        auto const useHigh = src.id() > dst.id();
        return sle->isFlag (trustFlag (flag, useHigh));
    }
    Throw<std::runtime_error> ("No line in getTrustFlag");
    return false; // silence warning
}

jtx::PrettyAmount
xrpMinusFee (jtx::Env const& env, std::int64_t xrpAmount)
{
    using namespace jtx;
    auto feeDrops = env.current ()->fees ().base;
    return drops (
        dropsPerXRP<std::int64_t>::value * xrpAmount - feeDrops);
};

bool equal (std::unique_ptr<Step> const& s1,
    DirectStepInfo const& dsi)
{
    if (!s1)
        return false;
    return test::directStepEqual (*s1, dsi.src, dsi.dst, dsi.currency);
}

bool equal (std::unique_ptr<Step> const& s1,
            XRPEndpointStepInfo const& xrpsi)
{
    if (!s1)
        return false;
    return test::xrpEndpointStepEqual (*s1, xrpsi.acc);
}

bool equal (std::unique_ptr<Step> const& s1, ripple::Book const& bsi)
{
    if (!s1)
        return false;
    return bookStepEqual (*s1, bsi);
}

template <class Iter>
bool strandEqualHelper (Iter i)
{
    // base case. all args processed and found equal.
    return true;
}

template <class Iter, class StepInfo, class... Args>
bool strandEqualHelper (Iter i, StepInfo&& si, Args&&... args)
{
    if (!equal (*i, std::forward<StepInfo> (si)))
        return false;
    return strandEqualHelper (++i, std::forward<Args> (args)...);
}

template <class... Args>
bool equal (Strand const& strand, Args&&... args)
{
    if (strand.size () != sizeof...(Args))
        return false;
    if (strand.empty ())
        return true;
    return strandEqualHelper (strand.begin (), std::forward<Args> (args)...);
}

struct Flow_test : public beast::unit_test::suite
{
    void testToStrand ()
    {
        testcase ("To Strand");

        using namespace jtx;
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        auto const gw = Account ("gw");

        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        auto const eurC = EUR.currency;
        auto const usdC = USD.currency;

        using D = DirectStepInfo;
        using B = ripple::Book;
        using XRPS = XRPEndpointStepInfo;

        // Account path element
        auto APE = [](AccountID const& a)
        {
            return STPathElement (
                STPathElement::typeAccount, a, xrpCurrency (), xrpAccount ());
        };

        // Issue path element
        auto IPE = [](Issue const& iss)
        {
            return STPathElement (
                STPathElement::typeCurrency | STPathElement::typeIssuer,
                xrpAccount (), iss.currency, iss.account);
        };

        // Currency path element
        auto CPE = [](Currency const& c)
        {
            return STPathElement (
                STPathElement::typeCurrency, xrpAccount (), c, xrpAccount ());
        };

        auto test = [&, this](jtx::Env& env, Issue const& deliver,
            boost::optional<Issue> const& sendMaxIssue, STPath const& path,
            TER expTer, auto&&... expSteps)
        {
            auto r = toStrand (*env.current (), alice, bob,
                deliver, sendMaxIssue, path, env.app ().logs ().journal ("Flow"));
            expect (r.first == expTer);
            if (sizeof...(expSteps))
                expect (equal (
                    r.second, std::forward<decltype (expSteps)> (expSteps)...));
        };

        {
            Env env (*this, features(featureFlowV2));
            env.fund (XRP (10000), alice, bob, carol, gw);

            test (env, USD, boost::none, STPath(), terNO_LINE);

            env.trust (USD (1000), alice, bob, carol);
            test (env, USD, boost::none, STPath(), tecPATH_DRY);

            env (pay (gw, alice, USD (100)));
            env (pay (gw, carol, USD (100)));

            // Insert implied account
            test (env, USD, boost::none, STPath(), tesSUCCESS,
                D{alice, gw, usdC}, D{gw, bob, usdC});
            env.trust (EUR (1000), alice, bob);

            // Insert implied offer
            test (env, EUR, USD.issue (), STPath(), tesSUCCESS,
                D{alice, gw, usdC}, B{USD, EUR}, D{gw, bob, eurC});

            // Path with explicit offer
            test (env, EUR, USD.issue (), STPath ({IPE (EUR)}),
                tesSUCCESS, D{alice, gw, usdC}, B{USD, EUR}, D{gw, bob, eurC});

            // Path with XRP src currency
            test (env, USD, xrpIssue (), STPath ({IPE (USD)}), tesSUCCESS,
                XRPS{alice}, B{XRP, USD}, D{gw, bob, usdC});

            // Path with XRP dst currency
            test (env, xrpIssue(), USD.issue (), STPath ({IPE (XRP)}),
                  tesSUCCESS, D{alice, gw, usdC}, B{USD, XRP}, XRPS{bob});

            // Path with XRP cross currency bridged payment
            test (env, EUR, USD.issue (), STPath ({CPE (xrpCurrency ())}),
                  tesSUCCESS,
                  D{alice, gw, usdC}, B{USD, XRP}, B{XRP, EUR}, D{gw, bob, eurC});

            // XRP -> XRP transaction can't include a path
            test (env, XRP, boost::none, STPath ({APE (carol)}), temBAD_PATH);

            {
                // The root account can't be the src or dst
                auto flowJournal = env.app ().logs ().journal ("Flow");
                {
                    // The root account can't be the dst
                    auto r = toStrand (*env.current (), alice,
                        xrpAccount (), XRP, USD.issue (), STPath (), flowJournal);
                    expect (r.first == temBAD_PATH);
                }
                {
                    // The root account can't be the src
                    auto r =
                        toStrand (*env.current (), xrpAccount (),
                            alice, XRP, boost::none, STPath (), flowJournal);
                    expect (r.first == temBAD_PATH);
                }
                {
                    // The root account can't be the src
                    auto r = toStrand (*env.current (),
                        noAccount (), bob, USD, boost::none, STPath (), flowJournal);
                    expect (r.first == terNO_ACCOUNT);
                }
            }

            // Create an offer with the same in/out issue
            test (env, EUR, USD.issue (), STPath ({IPE (USD), IPE (EUR)}),
                  temBAD_PATH);

            // Path element with type zero
            test (env, USD, boost::none,
                STPath ({STPathElement (
                    0, xrpAccount (), xrpCurrency (), xrpAccount ())}),
                temBAD_PATH);

            // The same account can't appear more than once on a path
            // `gw` will be used from alice->carol and implied between carol
            // and bob
            test (env, USD, boost::none, STPath ({APE (gw), APE (carol)}),
                temBAD_PATH_LOOP);

            // The same offer can't appear more than once on a path
            test (env, EUR, USD.issue (), STPath ({IPE (EUR), IPE (USD), IPE (EUR)}),
                  temBAD_PATH_LOOP);
        }
        {
            Env env (*this, features(featureFlowV2));
            env.fund (XRP (10000), alice, bob, noripple (gw));
            env.trust (USD (1000), alice, bob);
            env (pay (gw, alice, USD (100)));
            test (env, USD, boost::none, STPath (), terNO_RIPPLE);
        }

        {
            // check global freeze
            Env env (*this, features(featureFlowV2));
            env.fund (XRP (10000), alice, bob, gw);
            env.trust (USD (1000), alice, bob);
            env (pay (gw, alice, USD (100)));

            // Account can still issue payments
            env(fset(alice, asfGlobalFreeze));
            test (env, USD, boost::none, STPath (), tesSUCCESS);
            env(fclear(alice, asfGlobalFreeze));
            test (env, USD, boost::none, STPath (), tesSUCCESS);

            // Account can not issue funds
            env(fset(gw, asfGlobalFreeze));
            test (env, USD, boost::none, STPath (), terNO_LINE);
            env(fclear(gw, asfGlobalFreeze));
            test (env, USD, boost::none, STPath (), tesSUCCESS);

            // Account can not receive funds
            env(fset(bob, asfGlobalFreeze));
            test (env, USD, boost::none, STPath (), terNO_LINE);
            env(fclear(bob, asfGlobalFreeze));
            test (env, USD, boost::none, STPath (), tesSUCCESS);
        }
        {
            // Freeze between gw and alice
            Env env (*this, features(featureFlowV2));
            env.fund (XRP (10000), alice, bob, gw);
            env.trust (USD (1000), alice, bob);
            env (pay (gw, alice, USD (100)));
            test (env, USD, boost::none, STPath (), tesSUCCESS);
            env (trust (gw, alice["USD"] (0), tfSetFreeze));
            expect (getTrustFlag (env, gw, alice, usdC, TrustFlag::freeze));
            test (env, USD, boost::none, STPath (), terNO_LINE);
        }
        {
            // check no auth
            // An account may require authorization to receive IOUs from an
            // issuer
            Env env (*this, features(featureFlowV2));
            env.fund (XRP (10000), alice, bob, gw);
            env (fset (gw, asfRequireAuth));
            env.trust (USD (1000), alice, bob);
            // Authorize alice but not bob
            env (trust (gw, alice ["USD"] (1000), tfSetfAuth));
            expect (getTrustFlag (env, gw, alice, usdC, TrustFlag::auth));
            env (pay (gw, alice, USD (100)));
            env.require (balance (alice, USD (100)));
            test (env, USD, boost::none, STPath (), terNO_AUTH);

            // Check pure issue redeem still works
            auto r = toStrand (*env.current (), alice, gw, USD,
                boost::none, STPath (), env.app ().logs ().journal ("Flow"));
            expect (r.first == tesSUCCESS);
            expect (equal (r.second, D{alice, gw, usdC}));
        }
    }
    void testDirectStep ()
    {
        testcase ("Direct Step");

        using namespace jtx;
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        auto const dan = Account ("dan");
        auto const erin = Account ("erin");
        auto const USDA = alice["USD"];
        auto const USDB = bob["USD"];
        auto const USDC = carol["USD"];
        auto const USDD = dan["USD"];
        auto const gw = Account ("gw");
        auto const USD = gw["USD"];
        {
            // Pay USD, trivial path
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, gw);
            env.trust (USD (1000), alice, bob);
            env (pay (gw, alice, USD (100)));
            env (pay (alice, bob, USD (10)), paths (USD));
            env.require (balance (bob, USD (10)));
        }
        {
            // XRP transfer
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob);
            env (pay (alice, bob, XRP (100)));
            env.require (balance (bob, XRP (10000 + 100)));
            env.require (balance (alice, xrpMinusFee (env, 10000 - 100)));
        }
        {
            // Partial payments
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, gw);
            env.trust (USD (1000), alice, bob);
            env (pay (gw, alice, USD (100)));
            env (pay (alice, bob, USD (110)), paths (USD),
                ter (tecPATH_PARTIAL));
            env.require (balance (bob, USD (0)));
            env (pay (alice, bob, USD (110)), paths (USD),
                txflags (tfPartialPayment));
            env.require (balance (bob, USD (100)));
        }
        {
            // Pay by rippling through accounts, use path finder
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, carol, dan);
            env.trust (USDA (10), bob);
            env.trust (USDB (10), carol);
            env.trust (USDC (10), dan);
            env (pay (alice, dan, USDC (10)), paths (USDA));
            env.require (
                balance (bob, USDA (10)),
                balance (carol, USDB (10)),
                balance (dan, USDC (10)));
        }
        {
            // Pay by rippling through accounts, specify path
            // and charge a transfer fee
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, carol, dan);
            env.trust (USDA (10), bob);
            env.trust (USDB (10), carol);
            env.trust (USDC (10), dan);
            env (rate (bob, 1.1));

            env (pay (alice, dan, USDC (5)), path (bob, carol),
                sendmax (USDA (6)), txflags (tfNoRippleDirect));
            env.require (balance (dan, USDC (5)));
            env.require (balance (bob, USDA (5.5)));
        }
        {
            // test best quality path is taken
            // Paths: A->B->D->E ; A->C->D->E
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, carol, dan, erin);
            env.trust (USDA (10), bob, carol);
            env.trust (USDB (10), dan);
            env.trust (USDC (10), dan);
            env.trust (USDD (20), erin);
            env (rate (bob, 1));
            env (rate (carol, 1.1));

            env (pay (alice, erin, USDD (5)), path (carol, dan),
                path (bob, dan), txflags (tfNoRippleDirect));

            env.require (balance (erin, USDD (5)));
            env.require (balance (dan, USDB (5)));
            env.require (balance (dan, USDC (0)));
        }
        {
            // Limit quality
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, carol);
            env.trust (USDA (10), bob);
            env.trust (USDB (10), carol);

            env (pay (alice, carol, USDB (5)), sendmax (USDA (4)),
                txflags (tfLimitQuality | tfPartialPayment), ter (tecPATH_DRY));
            env.require (balance (carol, USDB (0)));

            env (pay (alice, carol, USDB (5)), sendmax (USDA (4)),
                txflags (tfPartialPayment));
            env.require (balance (carol, USDB (4)));
        }
    }

    void testBookStep ()
    {
        testcase ("Book Step");

        using namespace jtx;

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];
        auto const EUR = gw["EUR"];
        Account const alice ("alice");
        Account const bob ("bob");
        Account const carol ("carol");

        {
            // simple IOU/IOU offer
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, carol, gw);
            env.trust (USD (1000), alice, bob, carol);
            env.trust (BTC (1000), alice, bob, carol);

            env (pay (gw, alice, BTC (50)));
            env (pay (gw, bob, USD (50)));

            env (offer (bob, BTC (50), USD (50)));

            env (pay (alice, carol, USD (50)), path (~USD), sendmax (BTC (50)));

            env.require (balance (alice, BTC (0)));
            env.require (balance (bob, BTC (50)));
            env.require (balance (bob, USD (0)));
            env.require (balance (carol, USD (50)));
            expect (!isOffer (env, bob, BTC (50), USD (50)));
        }
        {
            // simple IOU/XRP XRP/IOU offer
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, carol, gw);
            env.trust (USD (1000), alice, bob, carol);
            env.trust (BTC (1000), alice, bob, carol);

            env (pay (gw, alice, BTC (50)));
            env (pay (gw, bob, USD (50)));

            env (offer (bob, BTC (50), XRP (50)));
            env (offer (bob, XRP (50), USD (50)));

            env (pay (alice, carol, USD (50)), path (~XRP, ~USD),
                sendmax (BTC (50)));

            env.require (balance (alice, BTC (0)));
            env.require (balance (bob, BTC (50)));
            env.require (balance (bob, USD (0)));
            env.require (balance (carol, USD (50)));
            expect (!isOffer (env, bob, XRP (50), USD (50)));
            expect (!isOffer (env, bob, BTC (50), XRP (50)));
        }
        {
            // simple XRP -> USD through offer and sendmax
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, carol, gw);
            env.trust (USD (1000), alice, bob, carol);
            env.trust (BTC (1000), alice, bob, carol);

            env (pay (gw, bob, USD (50)));

            env (offer (bob, XRP (50), USD (50)));

            env (pay (alice, carol, USD (50)), path (~USD),
                 sendmax (XRP (50)));

            env.require (balance (alice, xrpMinusFee (env, 10000 - 50)));
            env.require (balance (bob, xrpMinusFee (env, 10000 + 50)));
            env.require (balance (bob, USD (0)));
            env.require (balance (carol, USD (50)));
            expect (!isOffer (env, bob, XRP (50), USD (50)));
        }
        {
            // simple USD -> XRP through offer and sendmax
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, carol, gw);
            env.trust (USD (1000), alice, bob, carol);
            env.trust (BTC (1000), alice, bob, carol);

            env (pay (gw, alice, USD (50)));

            env (offer (bob, USD (50), XRP (50)));

            env (pay (alice, carol, XRP (50)), path (~XRP),
                 sendmax (USD (50)));

            env.require (balance (alice, USD (0)));
            env.require (balance (bob, xrpMinusFee (env, 10000 - 50)));
            env.require (balance (bob, USD (50)));
            env.require (balance (carol, XRP (10000 + 50)));
            expect (!isOffer (env, bob, USD (50), XRP (50)));
        }
        {
            // test unfunded offers are removed
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, carol, gw);
            env.trust (USD (1000), alice, bob, carol);
            env.trust (BTC (1000), alice, bob, carol);
            env.trust (EUR (1000), alice, bob, carol);

            env (pay (gw, alice, BTC (60)));
            env (pay (gw, bob, USD (50)));
            env (pay (gw, bob, EUR (50)));

            env (offer (bob, BTC (50), USD (50)));
            env (offer (bob, BTC (60), EUR (50)));
            env (offer (bob, EUR (50), USD (50)));

            // unfund offer
            env (pay (bob, gw, EUR (50)));
            expect (isOffer (env, bob, BTC (50), USD (50)));
            expect (isOffer (env, bob, BTC (60), EUR (50)));
            expect (isOffer (env, bob, EUR (50), USD (50)));

            env (pay (alice, carol, USD (50)),
                path (~USD), path (~EUR, ~USD),
                sendmax (BTC (60)));

            env.require (balance (alice, BTC (10)));
            env.require (balance (bob, BTC (50)));
            env.require (balance (bob, USD (0)));
            env.require (balance (bob, EUR (0)));
            env.require (balance (carol, USD (50)));
            // used in the payment
            expect (!isOffer (env, bob, BTC (50), USD (50)));
            // found unfunded
            expect (!isOffer (env, bob, BTC (60), EUR (50)));
            // unfunded, but should not yet be found unfunded
            expect (isOffer (env, bob, EUR (50), USD (50)));
        }
    }

    void testTransferRate ()
    {
        testcase ("Transfer Rate");

        using namespace jtx;

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];
        auto const EUR = gw["EUR"];
        Account const alice ("alice");
        Account const bob ("bob");
        Account const carol ("carol");


        {
            // Simple payment through a gateway with a
            // transfer rate
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust (USD (1000), alice, bob, carol);
            env (pay (gw, alice, USD (50)));
            env.require (balance (alice, USD (50)));
            env (pay (alice, bob, USD (40)), sendmax (USD (50)));
            env.require (balance (bob, USD (40)), balance (alice, USD (0)));
        }
        {
            // transfer rate is not charged when issuer is src or dst
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust (USD (1000), alice, bob, carol);
            env (pay (gw, alice, USD (50)));
            env.require (balance (alice, USD (50)));
            env (pay (alice, gw, USD (40)), sendmax (USD (40)));
            env.require (balance (alice, USD (10)));
        }
        {
            // transfer fee on an offer
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust (USD (1000), alice, bob, carol);
            env (pay (gw, bob, USD (50)));

            env (offer (bob, XRP (50), USD (50)));

            env (pay (alice, carol, USD (40)), path (~USD), sendmax (XRP (50)));
            env.require (
                balance (alice, xrpMinusFee (env, 10000 - 50)),
                balance (bob, USD (0)),
                balance (carol, USD (40)));
        }

        {
            // Transfer fee two consecutive offers
            Env env (*this, features(featureFlowV2));

            env.fund (XRP (10000), alice, bob, carol, gw);
            env(rate(gw, 1.25));
            env.trust (USD (1000), alice, bob, carol);
            env.trust (EUR (1000), alice, bob, carol);
            env (pay (gw, bob, USD (50)));
            env (pay (gw, bob, EUR (50)));

            env (offer (bob, XRP (50), USD (50)));
            env (offer (bob, USD (50), EUR (50)));

            env (pay (alice, carol, EUR (32)), path (~USD, ~EUR), sendmax (XRP (50)));
            env.require (
                balance (alice,  xrpMinusFee (env, 10000 - 50)),
                balance (bob, USD (40)),
                balance (bob, EUR (50 - 40)),
                balance (carol, EUR (32)));
        }
    }

    void run() override
    {
        testDirectStep ();
        testBookStep ();
        testTransferRate ();
        testToStrand ();
    }
};

BEAST_DEFINE_TESTSUITE(Flow,app,ripple);

} // test
} // ripple
