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
#include <test/jtx.h>
#include <test/jtx/WSClient.h>
#include <test/jtx/PathSet.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/Quality.h>

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
        auto feeDrops = env.current()->fees().base;
        return drops (dropsPerXRP<std::int64_t>::value * xrpAmount - feeDrops);
    };

    static auto
    ledgerEntryState(jtx::Env & env, jtx::Account const& acct_a, jtx::Account const & acct_b, std::string const& currency)
    {
        Json::Value jvParams;
        jvParams[jss::ledger_index] = "current";
        jvParams[jss::ripple_state][jss::currency] = currency;
        jvParams[jss::ripple_state][jss::accounts] = Json::arrayValue;
        jvParams[jss::ripple_state][jss::accounts].append(acct_a.human());
        jvParams[jss::ripple_state][jss::accounts].append(acct_b.human());
        return env.rpc ("json", "ledger_entry", to_string(jvParams))[jss::result];
    };

    static auto
    ledgerEntryRoot (jtx::Env & env, jtx::Account const& acct)
    {
        Json::Value jvParams;
        jvParams[jss::ledger_index] = "current";
        jvParams[jss::account_root] = acct.human();
        return env.rpc ("json", "ledger_entry", to_string(jvParams))[jss::result];
    };

    static auto
    ledgerEntryOffer (jtx::Env & env, jtx::Account const& acct, std::uint32_t offer_seq)
    {
        Json::Value jvParams;
        jvParams[jss::offer][jss::account] = acct.human();
        jvParams[jss::offer][jss::seq] = offer_seq;
        return env.rpc ("json", "ledger_entry", to_string(jvParams))[jss::result];
    };

    static auto
    getBookOffers(jtx::Env & env, Issue const& taker_pays, Issue const& taker_gets)
    {
        Json::Value jvbp;
        jvbp[jss::ledger_index] = "current";
        jvbp[jss::taker_pays][jss::currency] = to_string(taker_pays.currency);
        jvbp[jss::taker_pays][jss::issuer] = to_string(taker_pays.account);
        jvbp[jss::taker_gets][jss::currency] = to_string(taker_gets.currency);
        jvbp[jss::taker_gets][jss::issuer] = to_string(taker_gets.account);
        return env.rpc("json", "book_offers", to_string(jvbp)) [jss::result];
    }

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
        Env env {*this};

        // ledger close times have a dynamic resolution depending on network
        // conditions it appears the resolution in test is 10 seconds
        env.close ();

        auto const gw = Account {"gateway"};
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];
        Account const alice {"alice"};
        Account const bob {"bob"};
        Account const carol {"carol"};

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

        env (pay (alice, bob, USD (100)), json (paths.json ()),
            sendmax (BTC (1000)), txflags (tfPartialPayment));

        env.require (balance (bob, USD (100)));
        BEAST_EXPECT(!isOffer (env, carol, BTC (1), USD (100)) &&
            isOffer (env, carol, BTC (49), XRP (49)));
    }

    void testCanceledOffer ()
    {
        testcase ("Removing Canceled Offers");

        using namespace jtx;
        Env env {*this};
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const USD = gw["USD"];

        env.fund (XRP (10000), alice, gw);
        env.trust (USD (100), alice);

        env (pay (gw, alice, USD (50)));

        auto const firstOfferSeq = env.seq (alice);

        env (offer (alice, XRP (500), USD (100)),
            require (offers (alice, 1)));

        BEAST_EXPECT(isOffer (env, alice, XRP (500), USD (100)));

        // cancel the offer above and replace it with a new offer
        env (offer (alice, XRP (300), USD (100)),
             json (jss::OfferSequence, firstOfferSeq),
             require (offers (alice, 1)));

        BEAST_EXPECT(isOffer (env, alice, XRP (300), USD (100)) &&
            !isOffer (env, alice, XRP (500), USD (100)));

        // Test canceling non-existent offer.
        env (offer (alice, XRP (400), USD (200)),
             json (jss::OfferSequence, firstOfferSeq),
             require (offers (alice, 2)));

        BEAST_EXPECT(isOffer (env, alice, XRP (300), USD (100)) &&
            isOffer (env, alice, XRP (400), USD (200)));

        // Test cancellation now with OfferCancel tx
        auto const nextOfferSeq = env.seq (alice);
        env (offer (alice, XRP (222), USD (111)),
            require (offers (alice, 3)));

        BEAST_EXPECT(isOffer (env, alice, XRP (222), USD (111)));

        Json::Value cancelOffer;
        cancelOffer[jss::Account] = alice.human();
        cancelOffer[jss::OfferSequence] = nextOfferSeq;
        cancelOffer[jss::TransactionType] = "OfferCancel";
        env (cancelOffer);
        BEAST_EXPECT(env.seq(alice) == nextOfferSeq + 2);

        BEAST_EXPECT(!isOffer (env, alice, XRP (222), USD (111)));
    }

    void testTinyPayment ()
    {
        testcase ("Tiny payments");

        // Regression test for tiny payments
        using namespace jtx;
        using namespace std::chrono_literals;
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const carol = Account {"carol"};
        auto const gw = Account {"gw"};

        auto const USD = gw["USD"];
        auto const EUR = gw["EUR"];

        Env env {*this};

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
            env (pay (alice, bob, EUR (epsilon)), path (~EUR),
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
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const carol = Account {"carol"};
        auto const dan = Account {"dan"};
        auto const erin = Account {"erin"};
        auto const gw = Account {"gw"};

        auto const USD = gw["USD"];

        for (auto withFix : {false, true})
        {
            Env env {*this};

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

                BEAST_EXPECT(offerCount (carol) == 0);
                BEAST_EXPECT(offerCount (dan) == 1);
                if (!withFix)
                {
                    // funded offer was removed
                    BEAST_EXPECT(offerCount (erin) == 0);
                    env.require (balance ("erin", USD (1)));
                }
                else
                {
                    // offer was correctly consumed. There is stil some
                    // liquidity left on that offer.
                    BEAST_EXPECT(offerCount (erin) == 1);
                    env.require (balance ("erin", USD (0.99999)));
                }
            }
        }
    }

    void testEnforceNoRipple ()
    {
        testcase ("Enforce No Ripple");

        using namespace jtx;

        auto const gw = Account {"gateway"};
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];
        auto const EUR = gw["EUR"];
        Account const alice {"alice"};
        Account const bob {"bob"};
        Account const carol {"carol"};
        Account const dan {"dan"};

        {
            // No ripple with an implied account step after an offer
            Env env {*this};
            auto const gw1 = Account {"gw1"};
            auto const USD1 = gw1["USD"];
            auto const gw2 = Account {"gw2"};
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

        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const carol = Account {"carol"};
        auto const USD = gw["USD"];

        auto const usdOffer = USD (1000);
        auto const xrpOffer = XRP (1000);

        // No crossing:
        {
            Env env {*this};
            env.fund (XRP (1000000), gw);

            auto const f = env.current ()->fees ().base;
            auto const r = reserve (env, 0);

            env.fund (r + f, alice);

            env (trust (alice, usdOffer),           ter(tesSUCCESS));
            env (pay (gw, alice, usdOffer),         ter(tesSUCCESS));
            env (offer (alice, xrpOffer, usdOffer), ter(tecINSUF_RESERVE_OFFER));

            env.require (
                balance (alice, r - f),
                owners (alice, 1));
        }

        // Partial cross:
        {
            Env env(*this);
            env.fund (XRP (1000000), gw);

            auto const f = env.current ()->fees ().base;
            auto const r = reserve (env, 0);

            auto const usdOffer2 = USD (500);
            auto const xrpOffer2 = XRP (500);

            env.fund (r + f + xrpOffer, bob);
            env (offer (bob, usdOffer2, xrpOffer2),   ter(tesSUCCESS));
            env.fund (r + f, alice);
            env (trust (alice, usdOffer),             ter(tesSUCCESS));
            env (pay (gw, alice, usdOffer),           ter(tesSUCCESS));
            env (offer (alice, xrpOffer, usdOffer),   ter(tesSUCCESS));

            env.require (
                balance (alice, r - f + xrpOffer2),
                balance (alice, usdOffer2),
                owners (alice, 1),
                balance (bob, r + xrpOffer2),
                balance (bob, usdOffer2),
                owners (bob, 1));
        }

        // Account has enough reserve as is, but not enough
        // if an offer were added. Attempt to sell IOUs to
        // buy XRP. If it fully crosses, we succeed.
        {
            Env env {*this};
            env.fund (XRP (1000000), gw);

            auto const f = env.current ()->fees ().base;
            auto const r = reserve (env, 0);

            auto const usdOffer2 = USD (500);
            auto const xrpOffer2 = XRP (500);

            env.fund (r + f + xrpOffer, bob, carol);
            env (offer (bob, usdOffer2, xrpOffer2),    ter(tesSUCCESS));
            env (offer (carol, usdOffer, xrpOffer),    ter(tesSUCCESS));

            env.fund (r + f, alice);
            env (trust (alice, usdOffer),              ter(tesSUCCESS));
            env (pay (gw, alice, usdOffer),            ter(tesSUCCESS));
            env (offer (alice, xrpOffer, usdOffer),    ter(tesSUCCESS));

            env.require (
                balance (alice, r - f + xrpOffer),
                balance (alice, USD(0)),
                owners (alice, 1),
                balance (bob, r + xrpOffer2),
                balance (bob, usdOffer2),
                owners (bob, 1),
                balance (carol, r + xrpOffer2),
                balance (carol, usdOffer2),
                owners (carol, 2));
        }
    }

    void
    testFillModes ()
    {
        testcase ("Fill Modes");

        using namespace jtx;

        auto const startBalance = XRP (1000000);
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const USD = gw["USD"];

        // Fill or Kill - unless we fully cross, just charge
        // a fee and not place the offer on the books:
        {
            Env env {*this};
            env.fund (startBalance, gw);

            auto const f = env.current ()->fees ().base;

            env.fund (startBalance, alice, bob);
            env (offer (bob, USD (500), XRP (500)), ter(tesSUCCESS));
            env (trust (alice, USD (1000)),         ter(tesSUCCESS));
            env (pay (gw, alice, USD (1000)),       ter(tesSUCCESS));

            // Order that can't be filled:
            env (offer (alice, XRP (1000), USD (1000)),
                txflags (tfFillOrKill),              ter(tesSUCCESS));

            env.require (
                balance (alice, startBalance - f - f),
                balance (alice, USD (1000)),
                owners (alice, 1),
                offers (alice, 0),
                balance (bob, startBalance - f),
                balance (bob, USD (none)),
                owners (bob, 1),
                offers (bob, 1));

            // Order that can be filled
            env (offer (alice, XRP (500), USD (500)),
                txflags (tfFillOrKill),              ter(tesSUCCESS));

            env.require (
                balance (alice, startBalance - f - f - f + XRP (500)),
                balance (alice, USD (500)),
                owners (alice, 1),
                offers (alice, 0),
                balance (bob, startBalance - f - XRP (500)),
                balance (bob, USD (500)),
                owners (bob, 1),
                offers (bob, 0));
        }

        // Immediate or Cancel - cross as much as possible
        // and add nothing on the books:
        {
            Env env {*this};
            env.fund (startBalance, gw);

            auto const f = env.current ()->fees ().base;

            env.fund (startBalance, alice, bob);

            env (trust (alice, USD (1000)),                 ter(tesSUCCESS));
            env (pay (gw, alice, USD (1000)),               ter(tesSUCCESS));

            // No cross:
            env (offer (alice, XRP (1000), USD (1000)),
                txflags (tfImmediateOrCancel),               ter(tesSUCCESS));

            env.require (
                balance (alice, startBalance - f - f),
                balance (alice, USD (1000)),
                owners (alice, 1),
                offers (alice, 0));

            // Partially cross:
            env (offer (bob, USD (50), XRP (50)),            ter(tesSUCCESS));
            env (offer (alice, XRP (1000), USD (1000)),
                txflags (tfImmediateOrCancel),               ter(tesSUCCESS));

            env.require (
                balance (alice, startBalance - f - f - f + XRP (50)),
                balance (alice, USD (950)),
                owners (alice, 1),
                offers (alice, 0),
                balance (bob, startBalance - f - XRP (50)),
                balance (bob, USD (50)),
                owners (bob, 1),
                offers (bob, 0));

            // Fully cross:
            env (offer (bob, USD (50), XRP (50)),            ter(tesSUCCESS));
            env (offer (alice, XRP (50), USD (50)),
                txflags (tfImmediateOrCancel),               ter(tesSUCCESS));

            env.require (
                balance (alice, startBalance - f - f - f - f + XRP (100)),
                balance (alice, USD (900)),
                owners (alice, 1),
                offers (alice, 0),
                balance (bob, startBalance - f - f - XRP (100)),
                balance (bob, USD (100)),
                owners (bob, 1),
                offers (bob, 0));
        }
    }

    void
    testMalformed()
    {
        testcase ("Malformed Detection");

        using namespace jtx;

        auto const startBalance = XRP(1000000);
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const USD = gw["USD"];

        Env env {*this};
        env.fund (startBalance, gw);

        env.fund (startBalance, alice);

        // Order that has invalid flags
        env (offer (alice, USD (1000), XRP (1000)),
            txflags (tfImmediateOrCancel + 1),            ter(temINVALID_FLAG));
        env.require (
            balance (alice, startBalance),
            owners (alice, 0),
            offers (alice, 0));

        // Order with incompatible flags
        env (offer (alice, USD (1000), XRP (1000)),
            txflags (tfImmediateOrCancel | tfFillOrKill), ter(temINVALID_FLAG));
        env.require (
            balance (alice, startBalance),
            owners (alice, 0),
            offers (alice, 0));

        // Sell and buy the same asset
        {
            // Alice tries an XRP to XRP order:
            env (offer (alice, XRP (1000), XRP (1000)),   ter(temBAD_OFFER));
            env.require (
                owners (alice, 0),
                offers (alice, 0));

            // Alice tries an IOU to IOU order:
            env (trust (alice, USD (1000)),              ter(tesSUCCESS));
            env (pay (gw, alice, USD (1000)),            ter(tesSUCCESS));
            env (offer (alice, USD (1000), USD (1000)),   ter(temREDUNDANT));
            env.require (
                owners (alice, 1),
                offers (alice, 0));
        }

        // Offers with negative amounts
        {
            env (offer (alice, -USD (1000), XRP (1000)),  ter(temBAD_OFFER));
            env.require (
                owners (alice, 1),
                offers (alice, 0));

            env (offer (alice, USD (1000), -XRP (1000)),  ter(temBAD_OFFER));
            env.require (
                owners (alice, 1),
                offers (alice, 0));
        }

        // Offer with a bad expiration
        {
            Json::StaticString const key {"Expiration"};

            env (offer (alice, USD (1000), XRP (1000)),
                json (key, std::uint32_t (0)),            ter(temBAD_EXPIRATION));
            env.require (
                owners (alice, 1),
                offers (alice, 0));
        }

        // Offer with a bad offer sequence
        {
            env (offer (alice, USD (1000), XRP (1000)),
                json (jss::OfferSequence, std::uint32_t (0)),            ter(temBAD_SEQUENCE));
            env.require (
                owners (alice, 1),
                offers (alice, 0));
        }

        // Use XRP as a currency code
        {
            auto const BAD = IOU(gw, badCurrency());

            env (offer (alice, XRP (1000), BAD (1000)),   ter(temBAD_CURRENCY));
            env.require (
                owners (alice, 1),
                offers (alice, 0));
        }
    }

    void
    testExpiration()
    {
        testcase ("Offer Expiration");

        using namespace jtx;

        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const USD = gw["USD"];

        auto const startBalance = XRP (1000000);
        auto const usdOffer = USD (1000);
        auto const xrpOffer = XRP (1000);

        Json::StaticString const key ("Expiration");

        Env env(*this);
        env.fund (startBalance, gw, alice, bob);
        env.close();

        auto const f = env.current ()->fees ().base;

        // Place an offer that should have already expired
        env (trust (alice, usdOffer),             ter(tesSUCCESS));
        env (pay (gw, alice, usdOffer),           ter(tesSUCCESS));
        env.close();
        env.require (
            balance (alice, startBalance - f),
            balance (alice, usdOffer),
            offers (alice, 0),
            owners (alice, 1));

        env (offer (alice, xrpOffer, usdOffer),
            json (key, lastClose(env)),             ter(tesSUCCESS));
        env.require (
            balance (alice, startBalance - f - f),
            balance (alice, usdOffer),
            offers (alice, 0),
            owners (alice, 1));
        env.close();

        // Add an offer that's expires before the next ledger close
        env (offer (alice, xrpOffer, usdOffer),
            json (key, lastClose(env) + 1),         ter(tesSUCCESS));
        env.require (
            balance (alice, startBalance - f - f - f),
            balance (alice, usdOffer),
            offers (alice, 1),
            owners (alice, 2));

        // The offer expires (it's not removed yet)
        env.close ();
        env.require (
            balance (alice, startBalance - f - f - f),
            balance (alice, usdOffer),
            offers (alice, 1),
            owners (alice, 2));

        // Add offer - the expired offer is removed
        env (offer (bob, usdOffer, xrpOffer),     ter(tesSUCCESS));
        env.require (
            balance (alice, startBalance - f - f - f),
            balance (alice, usdOffer),
            offers (alice, 0),
            owners (alice, 1),
            balance (bob, startBalance - f),
            balance (bob, USD (none)),
            offers (bob, 1),
            owners (bob, 1));
    }

    void
    testUnfundedCross()
    {
        testcase ("Unfunded Crossing");

        using namespace jtx;

        auto const gw = Account {"gateway"};
        auto const USD = gw["USD"];

        auto const usdOffer = USD (1000);
        auto const xrpOffer = XRP (1000);

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

    void
    testSelfCross(bool use_partner)
    {
        testcase (std::string("Self-crossing") +
            (use_partner ? ", with partner account" : ""));

        using namespace jtx;

        auto const gw = Account {"gateway"};
        auto const partner = Account {"partner"};
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];

        Env env {*this};
        env.fund (XRP (10000), gw);
        if (use_partner)
        {
            env.fund (XRP (10000), partner);
            env (trust (partner, USD (100)));
            env (trust (partner, BTC (500)));
            env (pay (gw, partner, USD(100)));
            env (pay (gw, partner, BTC(500)));
        }
        auto const& account_to_test = use_partner ? partner : gw;

        env.close();
        env.require (offers (account_to_test, 0));

        // PART 1:
        // we will make two offers that can be used to bridge BTC to USD
        // through XRP
        env (offer (account_to_test, BTC (250), XRP (1000)),
                 offers (account_to_test, 1));

        // validate that the book now shows a BTC for XRP offer
        BEAST_EXPECT(isOffer(env, account_to_test, BTC(250), XRP(1000)));

        auto const secondLegSeq = env.seq(account_to_test);
        env (offer (account_to_test, XRP(1000), USD (50)),
                 offers (account_to_test, 2));

        // validate that the book also shows a XRP for USD offer
        BEAST_EXPECT(isOffer(env, account_to_test, XRP(1000), USD(50)));

        // now make an offer that will cross and autobridge, meaning
        // the outstanding offers will be taken leaving us with none
        env (offer (account_to_test, USD (50), BTC (250)));

        // NOTE :
        // at this point, all offers are expected to be consumed.
        // alas, they are not - because of bug in the current autobridging
        // implementation (to be replaced in the not-so-distant future).
        // The current implementation (incorrect) leaves an empty offer in the
        // second leg of the bridge. validate the current behavior as-is and
        // expect this test to be changed in the future.
        env.require (offers (account_to_test, 1));

        auto jrr = getBookOffers(env, USD, BTC);
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == 0);

        jrr = getBookOffers(env, BTC, XRP);
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == 0);

        BEAST_EXPECT(isOffer(env, account_to_test, XRP(0), USD(0)));

        // cancel that lingering second offer so that it doesn't interfere with the
        // next set of offers we test. this will not be needed once the bridging
        // bug is fixed
        Json::Value cancelOffer;
        cancelOffer[jss::Account] = account_to_test.human();
        cancelOffer[jss::OfferSequence] = secondLegSeq;
        cancelOffer[jss::TransactionType] = "OfferCancel";
        env (cancelOffer);
        env.require (offers (account_to_test, 0));

        // PART 2:
        // simple direct crossing  BTC to USD and then USD to BTC which causes
        // the first offer to be replaced
        env (offer (account_to_test, BTC (250), USD (50)),
                 offers (account_to_test, 1));

        // validate that the book shows one BTC for USD offer and no USD for
        // BTC offers
        BEAST_EXPECT(isOffer(env, account_to_test, BTC(250), USD(50)));

        jrr = getBookOffers(env, USD, BTC);
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == 0);

        // this second offer would self-cross directly, so it causes the first
        // offer by the same owner/taker to be removed
        env (offer (account_to_test, USD (50), BTC (250)),
                 offers (account_to_test, 1));

        // validate that we now have just the second offer...the first was removed
        jrr = getBookOffers(env, BTC, USD);
        BEAST_EXPECT(jrr[jss::offers].isArray());
        BEAST_EXPECT(jrr[jss::offers].size() == 0);

        BEAST_EXPECT(isOffer(env, account_to_test, USD(50), BTC(250)));
    }

    void
    testNegativeBalance()
    {
        // This test creates an offer test for negative balance
        // with transfer fees and miniscule funds.
        testcase ("Negative Balance");

        using namespace jtx;

        Env env {*this};
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const USD = gw["USD"];
        auto const BTC = gw["BTC"];

        // these *interesting* amounts were taken
        // from the original JS test that was ported here
        auto const gw_initial_balance = 1149999730;
        auto const alice_initial_balance = 499946999680;
        auto const bob_initial_balance = 10199999920;
        auto const small_amount =
            STAmount{ bob["USD"].issue(), UINT64_C(2710505431213761), -33};

        env.fund (drops (gw_initial_balance), gw);
        env.fund (drops (alice_initial_balance), alice);
        env.fund (drops (bob_initial_balance), bob);

        env (rate (gw, 1.005));

        env (trust (alice, USD (500)));
        env (trust (bob, USD (50)));
        env (trust (gw, alice["USD"] (100)));

        env (pay (gw, alice, alice["USD"] (50)));
        env (pay (gw, bob, small_amount));

        env (offer (alice, USD (50), XRP (150000)));

        // unfund the offer
        env (pay (alice, gw, USD (100)));

        // drop the trust line (set to 0)
        env (trust (gw, alice["USD"] (0)));

        // verify balances
        auto jrr = ledgerEntryState (env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "50");

        jrr = ledgerEntryState (env, bob, gw, "USD");
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName][jss::value] ==
                "-2710505431213761e-33");

        // create crossing offer
        env (offer (bob, XRP (2000), USD (1)));

        // verify balances again
        jrr = ledgerEntryState (env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "50");
        jrr = ledgerEntryRoot (env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            std::to_string(
                alice_initial_balance - env.current ()->fees ().base * 3)
        );

        jrr = ledgerEntryState (env, bob, gw, "USD");
        BEAST_EXPECT( jrr[jss::node][sfBalance.fieldName][jss::value] == "0");
        jrr = ledgerEntryRoot (env, bob);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            std::to_string(
                bob_initial_balance - env.current ()->fees ().base * 2)
        );
    }

    void
    testOfferCrossWithXRP(bool reverse_order)
    {
        testcase (std::string("Offer Crossing with XRP, ") +
                (reverse_order ? "Reverse" : "Normal") +
                " order");

        using namespace jtx;

        Env env {*this};
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const USD = gw["USD"];

        env.fund (XRP (10000), gw, alice, bob);

        env (trust (alice, USD (1000)));
        env (trust (bob, USD (1000)));

        env (pay (gw, alice, alice["USD"] (500)));

        if (reverse_order)
            env (offer (bob, USD (1), XRP (4000)));

        env (offer (alice, XRP (150000), USD (50)));

        if (! reverse_order)
            env (offer (bob, USD (1), XRP (4000)));

        // Existing offer pays better than this wants.
        // Fully consume existing offer.
        // Pay 1 USD, get 4000 XRP.

        auto jrr = ledgerEntryState (env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-1");
        jrr = ledgerEntryRoot (env, bob);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            std::to_string(
                XRP (10000).value ().mantissa () -
                XRP (reverse_order ? 4000 : 3000).value ().mantissa () -
                env.current ()->fees ().base * 2)
        );

        jrr = ledgerEntryState (env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-499");
        jrr = ledgerEntryRoot (env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            std::to_string(
                XRP (10000).value ().mantissa( ) +
                XRP(reverse_order ? 4000 : 3000).value ().mantissa () -
                env.current ()->fees ().base * 2)
        );
    }

    void
    testOfferCrossWithLimitOverride()
    {
        testcase ("Offer Crossing with Limit Override");

        using namespace jtx;

        Env env {*this};
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const USD = gw["USD"];

        env.fund (XRP (100000), gw, alice, bob);

        env (trust (alice, USD (1000)));

        env (pay (gw, alice, alice["USD"] (500)));

        env (offer (alice, XRP (150000), USD (50)));
        env (offer (bob, USD (1), XRP (3000)));

        auto jrr = ledgerEntryState (env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-1");
        jrr = ledgerEntryRoot(env, bob);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            std::to_string(
                XRP (100000).value ().mantissa () -
                XRP (3000).value ().mantissa () -
                env.current ()->fees ().base * 1)
        );

        jrr = ledgerEntryState (env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-499");
        jrr = ledgerEntryRoot (env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            std::to_string(
                XRP (100000).value ().mantissa () +
                XRP (3000).value ().mantissa () -
                env.current ()->fees ().base * 2)
        );
    }

    void
    testOfferAcceptThenCancel()
    {
        testcase ("Offer Accept then Cancel.");

        using namespace jtx;

        Env env {*this};
        auto const USD = env.master["USD"];

        auto const nextOfferSeq = env.seq (env.master);
        env (offer (env.master, XRP (500), USD (100)));
        env.close ();

        Json::Value cancelOffer;
        cancelOffer[jss::Account] = env.master.human();
        cancelOffer[jss::OfferSequence] = nextOfferSeq;
        cancelOffer[jss::TransactionType] = "OfferCancel";
        env (cancelOffer);
        BEAST_EXPECT(env.seq (env.master) == nextOfferSeq + 2);

        // ledger_accept, call twice and verify no odd behavior
        env.close();
        env.close();
        BEAST_EXPECT(env.seq (env.master) == nextOfferSeq + 2);
    }

    void
    testOfferCancelPastAndFuture()
    {

        testcase ("Offer Cancel Past and Future Sequence.");

        using namespace jtx;

        Env env {*this};
        auto const alice = Account {"alice"};

        auto const nextOfferSeq = env.seq (env.master);
        env.fund (XRP (10000), alice);

        Json::Value cancelOffer;
        cancelOffer[jss::Account] = env.master.human();
        cancelOffer[jss::OfferSequence] = nextOfferSeq;
        cancelOffer[jss::TransactionType] = "OfferCancel";
        env (cancelOffer);

        cancelOffer[jss::OfferSequence] = env.seq (env.master);
        env (cancelOffer, ter(temBAD_SEQUENCE));

        cancelOffer[jss::OfferSequence] = env.seq (env.master) + 1;
        env (cancelOffer, ter(temBAD_SEQUENCE));

        env.close();
        env.close();
    }

    void
    testCurrencyConversionEntire()
    {
        testcase ("Currency Conversion: Entire Offer");

        using namespace jtx;

        Env env {*this};
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const USD = gw["USD"];

        env.fund (XRP (10000), gw, alice, bob);
        env.require (owners (bob, 0));

        env (trust (alice, USD (100)));
        env (trust (bob, USD (1000)));

        env.require (
            owners (alice, 1),
            owners (bob, 1));

        env (pay (gw, alice, alice["USD"] (100)));
        auto const bobOfferSeq = env.seq (bob);
        env (offer (bob, USD (100), XRP (500)));

        env.require (
            owners (alice, 1),
            owners (bob, 2));
        auto jro = ledgerEntryOffer (env, bob, bobOfferSeq);
        BEAST_EXPECT(
            jro[jss::node][jss::TakerGets] == XRP (500).value ().getText ());
        BEAST_EXPECT(
            jro[jss::node][jss::TakerPays] == USD (100).value ().getJson (0));

        env (pay (alice, alice, XRP (500)), sendmax (USD (100)));

        auto jrr = ledgerEntryState (env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "0");
        jrr = ledgerEntryRoot (env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            std::to_string(
                XRP (10000).value ().mantissa () +
                XRP (500).value ().mantissa () -
                env.current ()->fees ().base * 2)
        );

        jrr = ledgerEntryState (env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-100");

        jro = ledgerEntryOffer(env, bob, bobOfferSeq);
        BEAST_EXPECT(jro[jss::error] == "entryNotFound");

        env.require (
            owners (alice, 1),
            owners (bob, 1));
    }

    void
    testCurrencyConversionIntoDebt()
    {
        testcase ("Currency Conversion: Offerer Into Debt");

        using namespace jtx;

        Env env {*this};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const carol = Account {"carol"};

        env.fund (XRP (10000), alice, bob, carol);

        env (trust (alice, carol["EUR"] (2000)));
        env (trust (bob, alice["USD"] (100)));
        env (trust (carol, bob["EUR"] (1000)));

        auto const bobOfferSeq = env.seq (bob);
        env (offer (bob, alice["USD"] (50), carol["EUR"] (200)),
            ter(tecUNFUNDED_OFFER));

        env (offer (alice, carol["EUR"] (200), alice["USD"] (50)));

        auto jro = ledgerEntryOffer(env, bob, bobOfferSeq);
        BEAST_EXPECT(jro[jss::error] == "entryNotFound");
    }

    void
    testCurrencyConversionInParts()
    {
        testcase ("Currency Conversion: In Parts");

        using namespace jtx;

        Env env {*this};
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const USD = gw["USD"];

        env.fund (XRP (10000), gw, alice, bob);

        env (trust (alice, USD (200)));
        env (trust (bob, USD (1000)));

        env (pay (gw, alice, alice["USD"] (200)));

        auto const bobOfferSeq = env.seq (bob);
        env (offer (bob, USD (100), XRP (500)));

        env (pay (alice, alice, XRP (200)), sendmax (USD (100)));

        // The previous payment reduced the remaining offer amount by 200 XRP
        auto jro = ledgerEntryOffer (env, bob, bobOfferSeq);
        BEAST_EXPECT(
            jro[jss::node][jss::TakerGets] == XRP (300).value ().getText ());
        BEAST_EXPECT(
            jro[jss::node][jss::TakerPays] == USD (60).value ().getJson (0));

        // the balance between alice and gw is 160 USD..200 less the 40 taken
        // by the offer
        auto jrr = ledgerEntryState (env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-160");
        // alice now has 200 more XRP from the payment
        jrr = ledgerEntryRoot (env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            std::to_string(
                XRP (10000).value ().mantissa () +
                XRP (200).value ().mantissa () -
                env.current ()->fees ().base * 2)
        );

        // bob got 40 USD from partial consumption of the offer
        jrr = ledgerEntryState (env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-40");

        // Alice converts USD to XRP which should fail
        // due to PartialPayment.
        env (pay (alice, alice, XRP (600)), sendmax (USD (100)),
            ter(tecPATH_PARTIAL));

        // Alice converts USD to XRP, should succeed because
        // we permit partial payment
        env (pay (alice, alice, XRP (600)), sendmax (USD (100)),
            txflags (tfPartialPayment));

        // Verify the offer was consumed
        jro = ledgerEntryOffer (env, bob, bobOfferSeq);
        BEAST_EXPECT(jro[jss::error] == "entryNotFound");

        // verify balances look right after the partial payment
        // only 300 XRP should be have been payed since that's all
        // that remained in the offer from bob. The alice balance is now
        // 100 USD because another 60 USD were transferred to bob in the second
        // payment
        jrr = ledgerEntryState (env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-100");
        jrr = ledgerEntryRoot (env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            std::to_string(
                XRP (10000).value ().mantissa () +
                XRP (200).value ().mantissa () +
                XRP (300).value ().mantissa () -
                env.current ()->fees ().base * 4)
        );

        // bob now has 100 USD - 40 from the first payment and 60 from the
        // second (partial) payment
        jrr = ledgerEntryState (env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-100");
    }

    void
    testCrossCurrencyStartXRP()
    {
        testcase ("Cross Currency Payment: Start with XRP");

        using namespace jtx;

        Env env {*this};
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const carol = Account {"carol"};
        auto const USD = gw["USD"];

        env.fund (XRP (10000), gw, alice, bob, carol);

        env (trust (carol, USD (1000)));
        env (trust (bob, USD (2000)));

        env (pay (gw, carol, carol["USD"] (500)));

        auto const carolOfferSeq = env.seq (carol);
        env (offer (carol, XRP (500), USD (50)));

        env (pay (alice, bob, USD (25)), sendmax (XRP (333)));

        auto jrr = ledgerEntryState (env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-25");

        jrr = ledgerEntryState (env, carol, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-475");

        auto jro = ledgerEntryOffer (env, carol, carolOfferSeq);
        BEAST_EXPECT(
            jro[jss::node][jss::TakerGets] == USD (25).value ().getJson (0));
        BEAST_EXPECT(
            jro[jss::node][jss::TakerPays] == XRP (250).value ().getText ());
    }

    void
    testCrossCurrencyEndXRP()
    {
        testcase ("Cross Currency Payment: End with XRP");

        using namespace jtx;

        Env env {*this};
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const carol = Account {"carol"};
        auto const USD = gw["USD"];

        env.fund (XRP (10000), gw, alice, bob, carol);

        env (trust (alice, USD (1000)));
        env (trust (carol, USD (2000)));

        env (pay (gw, alice, alice["USD"] (500)));

        auto const carolOfferSeq = env.seq (carol);
        env (offer (carol, USD (50), XRP (500)));

        env (pay (alice, bob, XRP (250)), sendmax (USD (333)));

        auto jrr = ledgerEntryState (env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-475");

        jrr = ledgerEntryState (env, carol, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-25");

        jrr = ledgerEntryRoot (env, bob);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] ==
            std::to_string(
                XRP (10000).value ().mantissa () +
                XRP (250).value ().mantissa ())
        );

        auto jro = ledgerEntryOffer (env, carol, carolOfferSeq);
        BEAST_EXPECT(
            jro[jss::node][jss::TakerGets] == XRP (250).value ().getText ());
        BEAST_EXPECT(
            jro[jss::node][jss::TakerPays] == USD (25).value ().getJson (0));
    }

    void
    testCrossCurrencyBridged()
    {
        testcase ("Cross Currency Payment: Bridged");

        using namespace jtx;

        Env env {*this};
        auto const gw1 = Account {"gateway_1"};
        auto const gw2 = Account {"gateway_2"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const carol = Account {"carol"};
        auto const dan = Account {"dan"};
        auto const USD = gw1["USD"];
        auto const EUR = gw2["EUR"];

        env.fund (XRP (10000), gw1, gw2, alice, bob, carol, dan);

        env (trust (alice, USD (1000)));
        env (trust (bob, EUR (1000)));
        env (trust (carol, USD (1000)));
        env (trust (dan, EUR (1000)));

        env (pay (gw1, alice, alice["USD"] (500)));
        env (pay (gw2, dan, dan["EUR"] (400)));

        auto const carolOfferSeq = env.seq (carol);
        env (offer (carol, USD (50), XRP (500)));

        auto const danOfferSeq = env.seq (dan);
        env (offer (dan, XRP (500), EUR (50)));

        Json::Value jtp {Json::arrayValue};
        jtp[0u][0u][jss::currency] = "XRP";
        env (pay (alice, bob, EUR (30)),
            json (jss::Paths, jtp),
            sendmax (USD (333)));

        auto jrr = ledgerEntryState (env, alice, gw1, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "470");

        jrr = ledgerEntryState (env, bob, gw2, "EUR");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-30");

        jrr = ledgerEntryState (env, carol, gw1, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-30");

        jrr = ledgerEntryState (env, dan, gw2, "EUR");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-370");

        auto jro = ledgerEntryOffer (env, carol, carolOfferSeq);
        BEAST_EXPECT(
            jro[jss::node][jss::TakerGets] == XRP (200).value ().getText ());
        BEAST_EXPECT(
            jro[jss::node][jss::TakerPays] == USD (20).value ().getJson (0));

        jro = ledgerEntryOffer (env, dan, danOfferSeq);
        BEAST_EXPECT(
            jro[jss::node][jss::TakerGets] == gw2["EUR"] (20).value ().getJson (0));
        BEAST_EXPECT(
            jro[jss::node][jss::TakerPays] == XRP (200).value ().getText ());
    }

    void
    testOfferFeesConsumeFunds()
    {
        testcase ("Offer Fees Consume Funds");

        using namespace jtx;

        Env env {*this};
        auto const gw1 = Account {"gateway_1"};
        auto const gw2 = Account {"gateway_2"};
        auto const gw3 = Account {"gateway_3"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const USD1 = gw1["USD"];
        auto const USD2 = gw2["USD"];
        auto const USD3 = gw3["USD"];

        // Provide micro amounts to compensate for fees to make results round
        // nice.
        // reserve: Alice has 3 entries in the ledger, via trust lines
        // fees:
        //  1 for each trust limit == 3 (alice < mtgox/amazon/bitstamp) +
        //  1 for payment          == 4
        auto const starting_xrp = XRP (100) +
            env.current ()->fees ().accountReserve (3) +
            env.current ()->fees ().base * 4;

        env.fund (starting_xrp, gw1, gw2, gw3, alice, bob);

        env (trust (alice, USD1 (1000)));
        env (trust (alice, USD2 (1000)));
        env (trust (alice, USD3 (1000)));
        env (trust (bob, USD1 (1000)));
        env (trust (bob, USD2 (1000)));

        env (pay (gw1, bob, bob["USD"] (500)));

        env (offer (bob, XRP (200), USD1 (200)));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        env (offer (alice, USD1 (200), XRP (200)));

        auto jrr = ledgerEntryState (env, alice, gw1, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "100");
        jrr = ledgerEntryRoot (env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] == XRP (350).value ().getText ()
        );

        jrr = ledgerEntryState (env, bob, gw1, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-400");
    }

    void
    testOfferCreateThenCross()
    {
        testcase ("Offer Create, then Cross");

        using namespace jtx;

        Env env {*this};
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const USD = gw["USD"];

        env.fund (XRP (10000), gw, alice, bob);

        env (rate (gw, 1.005));

        env (trust (alice, USD (1000)));
        env (trust (bob, USD (1000)));
        env (trust (gw, alice["USD"] (50)));

        env (pay (gw, bob, bob["USD"] (1)));
        env (pay (alice, gw, USD (50)));

        env (trust (gw, alice["USD"] (0)));

        env (offer (alice, USD (50), XRP (150000)));
        env (offer (bob, XRP (100), USD (0.1)));

        auto jrr = ledgerEntryState (env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "49.96666666666667");
        jrr = ledgerEntryState (env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-0.966500000033334");
    }

    void
    testSellFlagBasic()
    {
        testcase ("Offer tfSell: Basic Sell");

        using namespace jtx;

        Env env {*this};
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const USD = gw["USD"];

        auto const starting_xrp = XRP (100) +
            env.current ()->fees ().accountReserve (1) +
            env.current ()->fees ().base * 2;

        env.fund (starting_xrp, gw, alice, bob);

        env (trust (alice, USD (1000)));
        env (trust (bob, USD (1000)));

        env (pay (gw, bob, bob["USD"] (500)));

        env (offer (bob, XRP (200), USD (200)), json(jss::Flags, tfSell));
        // Alice has 350 + fees - a reserve of 50 = 250 reserve = 100 available.
        // Alice has 350 + fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        env (offer (alice, USD (200), XRP (200)), json(jss::Flags, tfSell));

        auto jrr = ledgerEntryState (env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-100");
        jrr = ledgerEntryRoot (env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] == XRP (250).value ().getText ()
        );

        jrr = ledgerEntryState (env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-400");
    }

    void
    testSellFlagExceedLimit()
    {
        testcase ("Offer tfSell: 2x Sell Exceed Limit");

        using namespace jtx;

        Env env {*this};
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const USD = gw["USD"];

        auto const starting_xrp = XRP (100) +
            env.current ()->fees ().accountReserve (1) +
            env.current ()->fees ().base * 2;

        env.fund (starting_xrp, gw, alice, bob);

        env (trust (alice, USD (150)));
        env (trust (bob, USD (1000)));

        env (pay (gw, bob, bob["USD"] (500)));

        env (offer (bob, XRP (100), USD (200)));
        // Alice has 350 fees - a reserve of 50 = 250 reserve = 100 available.
        // Ask for more than available to prove reserve works.
        // Taker pays 100 USD for 100 XRP.
        // Selling XRP.
        // Will sell all 100 XRP and get more USD than asked for.
        env (offer (alice, USD (100), XRP (100)), json(jss::Flags, tfSell));

        auto jrr = ledgerEntryState (env, alice, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-200");
        jrr = ledgerEntryRoot (env, alice);
        BEAST_EXPECT(
            jrr[jss::node][sfBalance.fieldName] == XRP (250).value ().getText ()
        );

        jrr = ledgerEntryState (env, bob, gw, "USD");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-300");
    }

    void
    testGatewayCrossCurrency()
    {
        testcase ("Client Issue #535: Gateway Cross Currency");

        using namespace jtx;

        Env env {*this};
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const bob = Account {"bob"};
        auto const XTS = gw["XTS"];
        auto const XXX = gw["XXX"];

        auto const starting_xrp = XRP (100.1) +
            env.current ()->fees ().accountReserve (1) +
            env.current ()->fees ().base * 2;

        env.fund (starting_xrp, gw, alice, bob);

        env (trust (alice, XTS (1000)));
        env (trust (alice, XXX (1000)));
        env (trust (bob, XTS (1000)));
        env (trust (bob, XXX (1000)));

        env (pay (gw, alice, alice["XTS"] (100)));
        env (pay (gw, alice, alice["XXX"] (100)));
        env (pay (gw, bob, bob["XTS"] (100)));
        env (pay (gw, bob, bob["XXX"] (100)));

        env (offer (alice, XTS (100), XXX (100)));

        //WS client is used here because the RPC client could not
        //be convinced to pass the build_path argument
        auto wsc = makeWSClient(env.app().config());
        Json::Value payment;
        payment[jss::secret] = toBase58(generateSeed("bob"));
        payment[jss::id] = env.seq (bob);
        payment[jss::build_path] = true;
        payment[jss::tx_json] = pay (bob, bob, bob["XXX"] (1));
        payment[jss::tx_json][jss::Sequence] =
            env.current ()->read (
                keylet::account (bob.id ()))->getFieldU32 (sfSequence);
        payment[jss::tx_json][jss::Fee] =
            std::to_string( env.current ()->fees ().base);
        payment[jss::tx_json][jss::SendMax] =
            bob ["XTS"] (1.5).value ().getJson (0);
        auto jrr = wsc->invoke("submit", payment);
        BEAST_EXPECT(jrr[jss::status] == "success");
        BEAST_EXPECT(jrr[jss::result][jss::engine_result] == "tesSUCCESS");
        if (wsc->version() == 2)
        {
            BEAST_EXPECT(jrr.isMember(jss::jsonrpc) && jrr[jss::jsonrpc] == "2.0");
            BEAST_EXPECT(jrr.isMember(jss::ripplerpc) && jrr[jss::ripplerpc] == "2.0");
            BEAST_EXPECT(jrr.isMember(jss::id) && jrr[jss::id] == 5);
        }

        jrr = ledgerEntryState (env, alice, gw, "XTS");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-101");
        jrr = ledgerEntryState (env, alice, gw, "XXX");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-99");

        jrr = ledgerEntryState (env, bob, gw, "XTS");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-99");
        jrr = ledgerEntryState (env, bob, gw, "XXX");
        BEAST_EXPECT(jrr[jss::node][sfBalance.fieldName][jss::value] == "-101");
    }

    void testTickSize ()
    {
        testcase ("Tick Size");

        using namespace jtx;

        // Try to set tick size without enabling feature
        {
            Env env {*this};
            auto const gw = Account {"gateway"};
            env.fund (XRP(10000), gw);

            auto txn = noop(gw);
            txn[sfTickSize.fieldName] = 0;
            env(txn, ter(temDISABLED));
        }

        // Try to set tick size out of range
        {
            Env env {*this, features (featureTickSize)};
            auto const gw = Account {"gateway"};
            env.fund (XRP(10000), gw);

            auto txn = noop(gw);
            txn[sfTickSize.fieldName] = Quality::minTickSize - 1;
            env(txn, ter (temBAD_TICK_SIZE));

            txn[sfTickSize.fieldName] = Quality::minTickSize;
            env(txn);
            BEAST_EXPECT ((*env.le(gw))[sfTickSize]
                == Quality::minTickSize);

            txn = noop (gw);
            txn[sfTickSize.fieldName] = Quality::maxTickSize;
            env(txn);
            BEAST_EXPECT (! env.le(gw)->isFieldPresent (sfTickSize));

            txn = noop (gw);
            txn[sfTickSize.fieldName] = Quality::maxTickSize - 1;
            env(txn);
            BEAST_EXPECT ((*env.le(gw))[sfTickSize]
                == Quality::maxTickSize - 1);

            txn = noop (gw);
            txn[sfTickSize.fieldName] = Quality::maxTickSize + 1;
            env(txn, ter (temBAD_TICK_SIZE));

            txn[sfTickSize.fieldName] = 0;
            env(txn, tesSUCCESS);
            BEAST_EXPECT (! env.le(gw)->isFieldPresent (sfTickSize));
        }

        Env env {*this, features (featureTickSize)};
        auto const gw = Account {"gateway"};
        auto const alice = Account {"alice"};
        auto const XTS = gw["XTS"];
        auto const XXX = gw["XXX"];

        env.fund (XRP (10000), gw, alice);

        {
            // Gateway sets its tick size to 5
            auto txn = noop(gw);
            txn[sfTickSize.fieldName] = 5;
            env(txn);
            BEAST_EXPECT ((*env.le(gw))[sfTickSize] == 5);
        }

        env (trust (alice, XTS (1000)));
        env (trust (alice, XXX (1000)));

        env (pay (gw, alice, alice["XTS"] (100)));
        env (pay (gw, alice, alice["XXX"] (100)));

        env (offer (alice, XTS (10), XXX (30)));
        env (offer (alice, XTS (30), XXX (10)));
        env (offer (alice, XTS (10), XXX (30)),
            json(jss::Flags, tfSell));
        env (offer (alice, XTS (30), XXX (10)),
            json(jss::Flags, tfSell));

        std::map <std::uint32_t, std::pair<STAmount, STAmount>> offers;
        forEachItem (*env.current(), alice,
            [&](std::shared_ptr<SLE const> const& sle)
        {
            if (sle->getType() == ltOFFER)
                offers.emplace((*sle)[sfSequence],
                    std::make_pair((*sle)[sfTakerPays],
                        (*sle)[sfTakerGets]));
        });

        // first offer
        auto it = offers.begin();
        BEAST_EXPECT (it != offers.end());
        BEAST_EXPECT (it->second.first == XTS(10) &&
            it->second.second < XXX(30) &&
            it->second.second > XXX(29.9994));

        // second offer
        ++it;
        BEAST_EXPECT (it != offers.end());
        BEAST_EXPECT (it->second.first == XTS(30) &&
            it->second.second == XXX(10));

        // third offer
        ++it;
        BEAST_EXPECT (it != offers.end());
        BEAST_EXPECT (it->second.first == XTS(10.0002) &&
            it->second.second == XXX(30));

        // fourth offer
        // exact TakerPays is XTS(1/.033333)
        ++it;
        BEAST_EXPECT (it != offers.end());
        BEAST_EXPECT (it->second.first == XTS(30) &&
            it->second.second == XXX(10));

        BEAST_EXPECT (++it == offers.end());
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
        testSelfCross (false);
        testSelfCross (true);
        testNegativeBalance ();
        testOfferCrossWithXRP (true);
        testOfferCrossWithXRP (false);
        testOfferCrossWithLimitOverride ();
        testOfferAcceptThenCancel ();
        testOfferCancelPastAndFuture ();
        testCurrencyConversionEntire ();
        testCurrencyConversionIntoDebt ();
        testCurrencyConversionInParts ();
        testCrossCurrencyStartXRP ();
        testCrossCurrencyEndXRP ();
        testCrossCurrencyBridged ();
        testOfferFeesConsumeFunds ();
        testOfferCreateThenCross ();
        testSellFlagBasic ();
        testSellFlagExceedLimit ();
        testGatewayCrossCurrency ();
        testTickSize ();
    }
};

BEAST_DEFINE_TESTSUITE (Offer, tx, ripple);

}  // test
}  // ripple




