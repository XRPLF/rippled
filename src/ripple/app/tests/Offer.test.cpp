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

namespace ripple {
namespace test {

class Offer_test : public beast::unit_test::suite
{
public:
    void testRmFundedOffer ()
    {
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
        // Regression test for tiny payments
        using namespace jtx;
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

        auto const switchoverTime = STAmountCalcSwitchovers::enableUnderflowFixCloseTime ();

        for (auto timeDelta : {-10, 10}){
            auto const closeTime = switchoverTime + timeDelta;
            STAmountCalcSwitchovers switchover (closeTime);
            env.close (NetClock::time_point (std::chrono::seconds (closeTime)));
            // Will fail without the underflow fix
            auto expectedResult = switchover.enableUnderflowFix () ?
                    tesSUCCESS : tecPATH_PARTIAL;
            env (pay ("alice", "bob", EUR (epsilon)), path (~EUR),
                sendmax (USD (100)), ter (expectedResult));
        }
    }
    void run ()
    {
        testCanceledOffer ();
        testRmFundedOffer ();
        testTinyPayment ();
    }
};

BEAST_DEFINE_TESTSUITE (Offer, tx, ripple)

}  // test
}  // ripple
