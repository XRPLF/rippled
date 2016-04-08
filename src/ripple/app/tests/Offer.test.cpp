//------------------------------------------------------------------------------
/*
  This file is part of rippled: https://github.com/ripple/rippled
  Copyright (c) 2012-2015 Ripple Labs Inc.

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
#include <ripple/app/main/Application.h>
#include <ripple/app/ledger/Ledger.h>
#include <ripple/basics/Log.h>
#include <ripple/test/jtx.h>
#include <ripple/test/jtx/Account.h>
#include <ripple/ledger/tests/PathSet.h>
#include <ripple/protocol/SystemParameters.h>

namespace ripple {
namespace test {

class Offer_test : public beast::unit_test::suite
{
    XRPAmount reserve(jtx::Env& env, std::uint32_t count)
    {
        return env.current()->fees().accountReserve (count);
    }

    std::uint32_t lastClose (jtx::Env& env)
    {
        return env.current()->info().parentCloseTime.time_since_epoch().count();
    }

    static auto
    xrpMinusFee (jtx::Env const& env, std::int64_t xrpAmount)
        -> jtx::PrettyAmount
    {
        using namespace jtx;
        auto feeDrops = env.current ()->fees ().base;
        return drops (dropsPerXRP<std::int64_t>::value * xrpAmount - feeDrops);
    };

public:
    void testRmFundedOffer ()
    {
        testcase ("Incorrect Removal of Funded Offers");

        // We need at least two paths. One at good quality and one at bad quality.
        // The bad quality path needs two offer books in a row. Each offer book
        // should have two offers at the same quality, the offers should be
        // completely consumed, and the payment should should require both offers to
        // be satisified. The first offer must be "taker gets" XRP. Old, broken
        // would remove the first "taker gets" xrp offer, even though the offer is
        // still funded and not used for the payment.

        using namespace jtx;
        Env env (*this);

        // ledger close times have a dynamic resolution depending on network
        // conditions it appears the resolution in test is 10 seconds
        env.close ();

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];
        Account const alice ("alice");
        Account const bob ("bob");
        Account const carol ("carol");

        env.fund (XRP (10000), alice, bob, carol, gw);
        env.trust (USD (1000), alice, bob, carol);
        env.trust (BTC (1000), alice, bob, carol);

        env (pay (gw, alice, BTC (1000)));

        env (pay (gw, carol, USD (1000)));
        env (pay (gw, carol, BTC (1000)));

        // Must be two offers at the same quality
        // "taker gets" must be XRP
        // (Different amounts so I can distinguish the offers)
        env (offer (carol, BTC (49), XRP (49)));
        env (offer (carol, BTC (51), XRP (51)));

        // Offers for the poor quality path
        // Must be two offers at the same quality
        env (offer (carol, XRP (50), USD (50)));
        env (offer (carol, XRP (50), USD (50)));

        // Offers for the good quality path
        env (offer (carol, BTC (1), USD (100)));

        PathSet paths (Path (XRP, USD), Path (USD));

        env (pay ("alice", "bob", USD (100)), json (paths.json ()),
            sendmax (BTC (1000)), txflags (tfPartialPayment));

        env.require (balance ("bob", USD (100)));
        expect (!isOffer (env, "carol", BTC (1), USD (100)) &&
            isOffer (env, "carol", BTC (49), XRP (49)));
    }

    void testCanceledOffer ()
    {
        testcase ("Removing Canceled Offers");

        using namespace jtx;
        Env env (*this);
        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];

        env.fund (XRP (10000), "alice", gw);
        env.trust (USD (100), "alice");

        env (pay (gw, "alice", USD (50)));

        auto const firstOfferSeq = env.seq ("alice");
        Json::StaticString const osKey ("OfferSequence");

        env (offer ("alice", XRP (500), USD (100)),
            require (offers ("alice", 1)));

        expect (isOffer (env, "alice", XRP (500), USD (100)));

        // cancel the offer above and replace it with a new offer
        env (offer ("alice", XRP (300), USD (100)), json (osKey, firstOfferSeq),
            require (offers ("alice", 1)));

        expect (isOffer (env, "alice", XRP (300), USD (100)) &&
            !isOffer (env, "alice", XRP (500), USD (100)));

        // Test canceling non-existant offer.
        env (offer ("alice", XRP (400), USD (200)), json (osKey, firstOfferSeq),
            require (offers ("alice", 2)));

        expect (isOffer (env, "alice", XRP (300), USD (100)) &&
            isOffer (env, "alice", XRP (400), USD (200)));
    }

    void testTinyPayment ()
    {
        testcase ("Tiny payments");

        // Regression test for tiny payments
        using namespace jtx;
        using namespace std::chrono_literals;
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        auto const gw = Account ("gw");

        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        Env env (*this);


        env.fund (XRP (10000), alice, bob, carol, gw);
        env.trust (USD (1000), alice, bob, carol);
        env.trust (EUR (1000), alice, bob, carol);
        env (pay (gw, alice, USD (100)));
        env (pay (gw, carol, EUR (100)));

        // Create more offers than the loop max count in DeliverNodeReverse
        for (int i=0;i<101;++i)
            env (offer (carol, USD (1), EUR (2)));

        for (auto timeDelta : {
            - env.closed()->info().closeTimeResolution,
                env.closed()->info().closeTimeResolution} )
        {
            auto const closeTime = STAmountSO::soTime + timeDelta;
            env.close (closeTime);
            *stAmountCalcSwitchover = closeTime > STAmountSO::soTime;
            // Will fail without the underflow fix
            auto expectedResult = *stAmountCalcSwitchover ?
                tesSUCCESS : tecPATH_PARTIAL;
            env (pay ("alice", "bob", EUR (epsilon)), path (~EUR),
                sendmax (USD (100)), ter (expectedResult));
        }
    }

    void testXRPTinyPayment ()
    {
        testcase ("XRP Tiny payments");

        // Regression test for tiny xrp payments
        // In some cases, when the payment code calculates
        // the amount of xrp needed as input to an xrp->iou offer
        // it would incorrectly round the amount to zero (even when
        // round-up was set to true).
        // The bug would cause funded offers to be incorrectly removed
        // because the code thought they were unfunded.
        // The conditions to trigger the bug are:
        // 1) When we calculate the amount of input xrp needed for an offer from
        //    xrp->iou, the amount is less than 1 drop (after rounding up the float
        //    representation).
        // 2) There is another offer in the same book with a quality sufficiently bad that
        //    when calculating the input amount needed the amount is not set to zero.

        using namespace jtx;
        using namespace std::chrono_literals;
        auto const alice = Account ("alice");
        auto const bob = Account ("bob");
        auto const carol = Account ("carol");
        auto const dan = Account ("dan");
        auto const erin = Account ("erin");
        auto const gw = Account ("gw");

        auto const USD = gw["USD"];

        for (auto withFix : {false, true})
        {
            Env env (*this);

            auto closeTime = [&]
            {
                auto const delta =
                    100 * env.closed ()->info ().closeTimeResolution;
                if (withFix)
                    return STAmountSO::soTime2 + delta;
                else
                    return STAmountSO::soTime2 - delta;
            }();

            auto offerCount = [&env](jtx::Account const& account)
            {
                auto count = 0;
                forEachItem (*env.current (), account,
                    [&](std::shared_ptr<SLE const> const& sle)
                    {
                        if (sle->getType () == ltOFFER)
                            ++count;
                    });
                return count;
            };

            env.fund (XRP (10000), alice, bob, carol, dan, erin, gw);
            env.trust (USD (1000), alice, bob, carol, dan, erin);
            env (pay (gw, carol, USD (0.99999)));
            env (pay (gw, dan, USD (1)));
            env (pay (gw, erin, USD (1)));

            // Carol doen't quite have enough funds for this offer
            // The amount left after this offer is taken will cause
            // STAmount to incorrectly round to zero when the next offer
            // (at a good quality) is considered. (when the
            // stAmountCalcSwitchover2 patch is inactive)
            env (offer (carol, drops (1), USD (1)));
            // Offer at a quality poor enough so when the input xrp is calculated
            // in the reverse pass, the amount is not zero.
            env (offer (dan, XRP (100), USD (1)));

            env.close (closeTime);
            // This is the funded offer that will be incorrectly removed.
            // It is considered after the offer from carol, which leaves a
            // tiny amount left to pay. When calculating the amount of xrp
            // needed for this offer, it will incorrectly compute zero in both
            // the forward and reverse passes (when the stAmountCalcSwitchover2 is
            // inactive.)
            env (offer (erin, drops (1), USD (1)));

            {
                env (pay (alice, bob, USD (1)), path (~USD),
                    sendmax (XRP (102)),
                    txflags (tfNoRippleDirect | tfPartialPayment));

                expect (offerCount (carol) == 0);
                expect (offerCount (dan) == 1);
                if (!withFix)
                {
                    // funded offer was removed
                    expect (offerCount (erin) == 0);
                    env.require (balance ("erin", USD (1)));
                }
                else
                {
                    // offer was correctly consumed. There is stil some
                    // liquidity left on that offer.
                    expect (offerCount (erin) == 1);
                    env.require (balance ("erin", USD (0.99999)));
                }
            }
        }
    }

    void testEnforceNoRipple ()
    {
        testcase ("Enforce No Ripple");

        using namespace jtx;

        auto const gw = Account ("gateway");
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];
        auto const EUR = gw["EUR"];
        Account const alice ("alice");
        Account const bob ("bob");
        Account const carol ("carol");
        Account const dan ("dan");

        {
            // No ripple with an implied account step after an offer
            Env env (*this);
            auto const gw1 = Account ("gw1");
            auto const USD1 = gw1["USD"];
            auto const gw2 = Account ("gw2");
            auto const USD2 = gw2["USD"];

            env.fund (XRP (10000), alice, noripple (bob), carol, dan, gw1, gw2);
            env.trust (USD1 (1000), alice, carol, dan);
            env(trust (bob, USD1 (1000), tfSetNoRipple));
            env.trust (USD2 (1000), alice, carol, dan);
            env(trust (bob, USD2 (1000), tfSetNoRipple));

            env (pay (gw1, dan, USD1 (50)));
            env (pay (gw1, bob, USD1 (50)));
            env (pay (gw2, bob, USD2 (50)));

            env (offer (dan, XRP (50), USD1 (50)));

            env (pay (alice, carol, USD2 (50)), path (~USD1, bob), ter(tecPATH_DRY),
                sendmax (XRP (50)), txflags (tfNoRippleDirect));
        }
        {
            // Make sure payment works with default flags
            Env env (*this);
            auto const gw1 = Account ("gw1");
            auto const USD1 = gw1["USD"];
            auto const gw2 = Account ("gw2");
            auto const USD2 = gw2["USD"];

            env.fund (XRP (10000), alice, bob, carol, dan, gw1, gw2);
            env.trust (USD1 (1000), alice, bob, carol, dan);
            env.trust (USD2 (1000), alice, bob, carol, dan);

            env (pay (gw1, dan, USD1 (50)));
            env (pay (gw1, bob, USD1 (50)));
            env (pay (gw2, bob, USD2 (50)));

            env (offer (dan, XRP (50), USD1 (50)));

            env (pay (alice, carol, USD2 (50)), path (~USD1, bob),
                sendmax (XRP (50)), txflags (tfNoRippleDirect));

            env.require (balance (alice, xrpMinusFee (env, 10000 - 50)));
            env.require (balance (bob, USD1 (100)));
            env.require (balance (bob, USD2 (0)));
            env.require (balance (carol, USD2 (50)));
        }
    }

    void
    testInsufficientReserve ()
    {
        testcase ("Insufficient Reserve");

        // If an account places an offer and its balance
        // *before* the transaction began isn't high enough
        // to meet the reserve *after* the transaction runs,
        // then no offer should go on the books but if the
        // offer partially or fully crossed the tx succeeds.

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        auto const usdOffer = USD(1000);
        auto const xrpOffer = XRP(1000);

        // No crossing:
        {
            Env env(*this);
            env.fund (XRP(1000000), gw);

            auto const f = env.current ()->fees ().base;
            auto const r = reserve (env, 0);

            env.fund (r + f, "alice");

            env (trust ("alice", usdOffer),           ter(tesSUCCESS));
            env (pay (gw, "alice", usdOffer),         ter(tesSUCCESS));
            env (offer ("alice", xrpOffer, usdOffer), ter(tecINSUF_RESERVE_OFFER));

            env.require (
                balance ("alice", r - f),
                owners ("alice", 1));
        }

        // Partial cross:
        {
            Env env(*this);
            env.fund (XRP(1000000), gw);

            auto const f = env.current ()->fees ().base;
            auto const r = reserve (env, 0);

            auto const usdOffer2 = USD(500);
            auto const xrpOffer2 = XRP(500);

            env.fund (r + f + xrpOffer, "bob");
            env (offer ("bob", usdOffer2, xrpOffer2),   ter(tesSUCCESS));
            env.fund (r + f, "alice");
            env (trust ("alice", usdOffer),             ter(tesSUCCESS));
            env (pay (gw, "alice", usdOffer),           ter(tesSUCCESS));
            env (offer ("alice", xrpOffer, usdOffer),   ter(tesSUCCESS));

            env.require (
                balance ("alice", r - f + xrpOffer2),
                balance ("alice", usdOffer2),
                owners ("alice", 1),
                balance ("bob", r + xrpOffer2),
                balance ("bob", usdOffer2),
                owners ("bob", 1));
        }

        // Account has enough reserve as is, but not enough
        // if an offer were added. Attempt to sell IOUs to
        // buy XRP. If it fully crosses, we succeed.
        {
            Env env(*this);
            env.fund (XRP(1000000), gw);

            auto const f = env.current ()->fees ().base;
            auto const r = reserve (env, 0);

            auto const usdOffer2 = USD(500);
            auto const xrpOffer2 = XRP(500);

            env.fund (r + f + xrpOffer, "bob", "carol");
            env (offer ("bob", usdOffer2, xrpOffer2),    ter(tesSUCCESS));
            env (offer ("carol", usdOffer, xrpOffer),    ter(tesSUCCESS));

            env.fund (r + f, "alice");
            env (trust ("alice", usdOffer),              ter(tesSUCCESS));
            env (pay (gw, "alice", usdOffer),            ter(tesSUCCESS));
            env (offer ("alice", xrpOffer, usdOffer),    ter(tesSUCCESS));

            env.require (
                balance ("alice", r - f + xrpOffer),
                balance ("alice", USD(0)),
                owners ("alice", 1),
                balance ("bob", r + xrpOffer2),
                balance ("bob", usdOffer2),
                owners ("bob", 1),
                balance ("carol", r + xrpOffer2),
                balance ("carol", usdOffer2),
                owners ("carol", 2));
        }
    }

    void
    testFillModes ()
    {
        testcase ("Fill Modes");

        using namespace jtx;

        auto const startBalance = XRP(1000000);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        // Fill or Kill - unless we fully cross, just charge
        // a fee and not place the offer on the books:
        {
            Env env(*this);
            env.fund (startBalance, gw);

            auto const f = env.current ()->fees ().base;

            env.fund (startBalance, "alice", "bob");
            env (offer ("bob", USD(500), XRP(500)),  ter(tesSUCCESS));
            env (trust ("alice", USD(1000)),         ter(tesSUCCESS));
            env (pay (gw, "alice", USD(1000)),       ter(tesSUCCESS));

            // Order that can't be filled:
            env (offer ("alice", XRP(1000), USD(1000)),
                txflags (tfFillOrKill),              ter(tesSUCCESS));

            env.require (
                balance ("alice", startBalance - f - f),
                balance ("alice", USD(1000)),
                owners ("alice", 1),
                offers ("alice", 0),
                balance ("bob", startBalance - f),
                balance ("bob", USD(none)),
                owners ("bob", 1),
                offers ("bob", 1));

            // Order that can be filled
            env (offer ("alice", XRP(500), USD(500)),
                txflags (tfFillOrKill),              ter(tesSUCCESS));

            env.require (
                balance ("alice", startBalance - f - f - f + XRP(500)),
                balance ("alice", USD(500)),
                owners ("alice", 1),
                offers ("alice", 0),
                balance ("bob", startBalance - f - XRP(500)),
                balance ("bob", USD(500)),
                owners ("bob", 1),
                offers ("bob", 0));
        }

        // Immediate or Cancel - cross as much as possible
        // and add nothing on the books:
        {
            Env env(*this);
            env.fund (startBalance, gw);

            auto const f = env.current ()->fees ().base;

            env.fund (startBalance, "alice", "bob");

            env (trust ("alice", USD(1000)),                 ter(tesSUCCESS));
            env (pay (gw, "alice", USD(1000)),               ter(tesSUCCESS));

            // No cross:
            env (offer ("alice", XRP(1000), USD(1000)),
                txflags (tfImmediateOrCancel),               ter(tesSUCCESS));

            env.require (
                balance ("alice", startBalance - f - f),
                balance ("alice", USD(1000)),
                owners ("alice", 1),
                offers ("alice", 0));

            // Partially cross:
            env (offer ("bob", USD(50), XRP(50)),            ter(tesSUCCESS));
            env (offer ("alice", XRP(1000), USD(1000)),
                txflags (tfImmediateOrCancel),               ter(tesSUCCESS));

            env.require (
                balance ("alice", startBalance - f - f - f + XRP(50)),
                balance ("alice", USD(950)),
                owners ("alice", 1),
                offers ("alice", 0),
                balance ("bob", startBalance - f - XRP(50)),
                balance ("bob", USD(50)),
                owners ("bob", 1),
                offers ("bob", 0));

            // Fully cross:
            env (offer ("bob", USD(50), XRP(50)),            ter(tesSUCCESS));
            env (offer ("alice", XRP(50), USD(50)),
                txflags (tfImmediateOrCancel),               ter(tesSUCCESS));

            env.require (
                balance ("alice", startBalance - f - f - f - f + XRP(100)),
                balance ("alice", USD(900)),
                owners ("alice", 1),
                offers ("alice", 0),
                balance ("bob", startBalance - f - f - XRP(100)),
                balance ("bob", USD(100)),
                owners ("bob", 1),
                offers ("bob", 0));
        }
    }

    void
    testMalformed()
    {
        testcase ("Malformed Detection");

        using namespace jtx;

        auto const startBalance = XRP(1000000);
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        Env env(*this);
        env.fund (startBalance, gw);

        env.fund (startBalance, "alice");

        // Order that has invalid flags
        env (offer ("alice", USD(1000), XRP(1000)),
            txflags (tfImmediateOrCancel + 1),            ter(temINVALID_FLAG));
        env.require (
            balance ("alice", startBalance),
            owners ("alice", 0),
            offers ("alice", 0));

        // Order with incompatible flags
        env (offer ("alice", USD(1000), XRP(1000)),
            txflags (tfImmediateOrCancel | tfFillOrKill), ter(temINVALID_FLAG));
        env.require (
            balance ("alice", startBalance),
            owners ("alice", 0),
            offers ("alice", 0));

        // Sell and buy the same asset
        {
            // Alice tries an XRP to XRP order:
            env (offer ("alice", XRP(1000), XRP(1000)),   ter(temBAD_OFFER));
            env.require (
                owners ("alice", 0),
                offers ("alice", 0));

            // Alice tries an IOU to IOU order:
            env (trust ("alice", USD(1000)),              ter(tesSUCCESS));
            env (pay (gw, "alice", USD(1000)),            ter(tesSUCCESS));
            env (offer ("alice", USD(1000), USD(1000)),   ter(temREDUNDANT));
            env.require (
                owners ("alice", 1),
                offers ("alice", 0));
        }

        // Offers with negative amounts
        {
            env (offer ("alice", -USD(1000), XRP(1000)),  ter(temBAD_OFFER));
            env.require (
                owners ("alice", 1),
                offers ("alice", 0));

            env (offer ("alice", USD(1000), -XRP(1000)),  ter(temBAD_OFFER));
            env.require (
                owners ("alice", 1),
                offers ("alice", 0));
        }

        // Offer with a bad expiration
        {
            Json::StaticString const key ("Expiration");

            env (offer ("alice", USD(1000), XRP(1000)),
                json (key, std::uint32_t (0)),            ter(temBAD_EXPIRATION));
            env.require (
                owners ("alice", 1),
                offers ("alice", 0));
        }

        // Offer with a bad offer sequence
        {
            Json::StaticString const key ("OfferSequence");

            env (offer ("alice", USD(1000), XRP(1000)),
                json (key, std::uint32_t (0)),            ter(temBAD_SEQUENCE));
            env.require (
                owners ("alice", 1),
                offers ("alice", 0));
        }

        // Use XRP as a currency code
        {
            auto const BAD = IOU(gw, badCurrency());

            env (offer ("alice", XRP(1000), BAD(1000)),   ter(temBAD_CURRENCY));
            env.require (
                owners ("alice", 1),
                offers ("alice", 0));
        }
    }

    void
    testExpiration ()
    {
        testcase ("Offer Expiration");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        auto const startBalance = XRP(1000000);
        auto const usdOffer = USD(1000);
        auto const xrpOffer = XRP(1000);

        Json::StaticString const key ("Expiration");

        Env env(*this);
        env.fund (startBalance, gw, "alice", "bob");
        env.close();

        auto const f = env.current ()->fees ().base;

        // Place an offer that should have already expired
        env (trust ("alice", usdOffer),             ter(tesSUCCESS));
        env (pay (gw, "alice", usdOffer),           ter(tesSUCCESS));
        env.close();
        env.require (
            balance ("alice", startBalance - f),
            balance ("alice", usdOffer),
            offers ("alice", 0),
            owners ("alice", 1));

        env (offer ("alice", xrpOffer, usdOffer),
            json (key, lastClose(env)),             ter(tesSUCCESS));
        env.require (
            balance ("alice", startBalance - f - f),
            balance ("alice", usdOffer),
            offers ("alice", 0),
            owners ("alice", 1));
        env.close();

        // Add an offer that's expires before the next ledger close
        env (offer ("alice", xrpOffer, usdOffer),
            json (key, lastClose(env) + 1),         ter(tesSUCCESS));
        env.require (
            balance ("alice", startBalance - f - f - f),
            balance ("alice", usdOffer),
            offers ("alice", 1),
            owners ("alice", 2));

        // The offer expires (it's not removed yet)
        env.close ();
        env.require (
            balance ("alice", startBalance - f - f - f),
            balance ("alice", usdOffer),
            offers ("alice", 1),
            owners ("alice", 2));

        // Add offer - the expired offer is removed
        env (offer ("bob", usdOffer, xrpOffer),     ter(tesSUCCESS));
        env.require (
            balance ("alice", startBalance - f - f - f),
            balance ("alice", usdOffer),
            offers ("alice", 0),
            owners ("alice", 1),
            balance ("bob", startBalance - f),
            balance ("bob", USD(none)),
            offers ("bob", 1),
            owners ("bob", 1));
    }

    void
    testUnfundedCross()
    {
        testcase ("Unfunded Crossing");

        using namespace jtx;

        auto const gw = Account("gateway");
        auto const USD = gw["USD"];

        auto const usdOffer = USD(1000);
        auto const xrpOffer = XRP(1000);

        Env env(*this);
        env.fund (XRP(1000000), gw);

        // The fee that's charged for transactions
        auto const f = env.current ()->fees ().base;

        // Account is at the reserve, and will dip below once
        // fees are subtracted.
        env.fund (reserve (env, 0), "alice");
        env (offer ("alice", usdOffer, xrpOffer),     ter(tecUNFUNDED_OFFER));
        env.require (
            balance ("alice", reserve (env, 0) - f),
            owners ("alice", 0));

        // Account has just enough for the reserve and the
        // fee.
        env.fund (reserve (env, 0) + f, "bob");
        env (offer ("bob", usdOffer, xrpOffer),       ter(tecUNFUNDED_OFFER));
        env.require (
            balance ("bob", reserve (env, 0)),
            owners ("bob", 0));

        // Account has enough for the reserve, the fee and
        // the offer, and a bit more, but not enough for the
        // reserve after the offer is placed.
        env.fund (reserve (env, 0) + f + XRP(1), "carol");
        env (offer ("carol", usdOffer, xrpOffer),     ter(tecINSUF_RESERVE_OFFER));
        env.require (
            balance ("carol", reserve (env, 0) + XRP(1)),
            owners ("carol", 0));

        // Account has enough for the reserve plus one
        // offer, and the fee.
        env.fund (reserve (env, 1) + f, "dan");
        env (offer ("dan", usdOffer, xrpOffer),       ter(tesSUCCESS));
        env.require (
            balance ("dan", reserve (env, 1)),
            owners ("dan", 1));

        // Account has enough for the reserve plus one
        // offer, the fee and the entire offer amount.
        env.fund (reserve (env, 1) + f + xrpOffer, "eve");
        env (offer ("eve", usdOffer, xrpOffer),       ter(tesSUCCESS));
        env.require (
            balance ("eve", reserve (env, 1) + xrpOffer),
            owners ("eve", 1));
    }

    void run ()
    {
        testCanceledOffer ();
        testRmFundedOffer ();
        testTinyPayment ();
        testXRPTinyPayment ();
        testEnforceNoRipple ();
        testInsufficientReserve ();
        testFillModes ();
        testMalformed ();
        testExpiration ();
        testUnfundedCross ();
    }
};

BEAST_DEFINE_TESTSUITE (Offer, tx, ripple)

}  // test
}  // ripple
