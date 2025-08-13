//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2025 Ripple Labs Inc.

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

#include <xrpld/ledger/Dir.h>
#include <xrpld/rpc/detail/RPCHelpers.h>

#include <xrpl/basics/chrono.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/PayChan.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

namespace ripple {
namespace test {
struct PayChanToken_test : public beast::unit_test::suite
{
    void
    testIOUEnablement(FeatureBitset features)
    {
        testcase("IOU Enablement");

        using namespace jtx;
        using namespace std::chrono;

        for (bool const withTokenPaychan : {false, true})
        {
            auto const amend =
                withTokenPaychan ? features : features - featureTokenPaychan;
            Env env{*this, amend};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5'000)));
            env(pay(gw, bob, USD(5'000)));
            env.close();

            auto const openResult =
                withTokenPaychan ? ter(tesSUCCESS) : ter(temBAD_AMOUNT);
            auto const closeResult =
                withTokenPaychan ? ter(tesSUCCESS) : ter(tecNO_TARGET);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, USD(1'000), settleDelay, pk),
                openResult);
            env.close();
            env(paychan::fund(alice, chan, USD(1'000)), openResult);
            env.close();
            env(paychan::claim(bob, chan), txflags(tfClose), closeResult);
            env.close();
        }
    }

    void
    testIOUAllowLockingFlag(FeatureBitset features)
    {
        testcase("IOU Allow Locking Flag");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(5000), alice, bob, gw);
        env(fset(gw, asfAllowTrustLineLocking));
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(5'000)));
        env(pay(gw, bob, USD(5'000)));
        env.close();

        // Create PayChan
        auto const pk = alice.pk();
        auto const settleDelay = 100s;
        auto const chan = paychan::channel(alice, bob, env.seq(alice));
        env(paychan::create(alice, bob, USD(1'000), settleDelay, pk),
            ter(tesSUCCESS));
        env.close();

        // Clear the asfAllowTrustLineLocking flag
        env(fclear(gw, asfAllowTrustLineLocking));
        env.close();
        env.require(nflags(gw, asfAllowTrustLineLocking));

        // Cannot Create PayChan without asfAllowTrustLineLocking
        env(paychan::create(alice, bob, USD(1'000), settleDelay, pk),
            ter(tecNO_PERMISSION));
        env.close();

        // Can Fund PayChan without asfAllowTrustLineLocking
        env(paychan::fund(alice, chan, USD(1'000)), ter(tesSUCCESS));
        env.close();

        // Can claim the paychan created before the flag was cleared
        auto const sig =
            paychan::signClaimAuth(alice.pk(), alice.sk(), chan, USD(1'000));
        env(paychan::claim(
                bob, chan, USD(1'000), USD(1'000), Slice(sig), alice.pk()),
            ter(tesSUCCESS));
        env.close();
    }

    void
    testIOUCreatePreflight(FeatureBitset features)
    {
        testcase("IOU Create Preflight");
        using namespace test::jtx;
        using namespace std::literals;

        // temBAD_FEE: Exercises invalid preflight1.
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5'000), alice, bob, gw);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, USD(1), settleDelay, pk),
                fee(XRP(-1)),
                ter(temBAD_FEE));
            env.close();
        }

        // temBAD_AMOUNT: amount <= 0
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5'000), alice, bob, gw);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, USD(-1), settleDelay, pk),
                ter(temBAD_AMOUNT));
            env.close();
        }

        // temBAD_CURRENCY: badCurrency() == amount.getCurrency()
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const BAD = IOU(gw, badCurrency());
            env.fund(XRP(5'000), alice, bob, gw);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, BAD(1), settleDelay, pk),
                ter(temBAD_CURRENCY));
            env.close();
        }
    }

    void
    testIOUCreatePreclaim(FeatureBitset features)
    {
        testcase("IOU Create Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_PERMISSION: issuer is the same as the account
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);

            env(paychan::create(gw, alice, USD(1), 100s, alice.pk()),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // tecNO_ISSUER: Issuer does not exist
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob);
            env.close();
            env.memoize(gw);

            env(paychan::create(alice, bob, USD(1), 100s, alice.pk()),
                ter(tecNO_ISSUER));
            env.close();
        }

        // tecNO_PERMISSION: asfAllowTrustLineLocking is not set
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env.trust(USD(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            env(paychan::create(gw, alice, USD(1), 100s, alice.pk()),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // tecNO_LINE: account does not have a trustline to the issuer
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();

            env(paychan::create(alice, bob, USD(1), 100s, alice.pk()),
                ter(tecNO_LINE));
            env.close();
        }

        // tecNO_PERMISSION: Not testable
        // tecNO_PERMISSION: Not testable
        // tecNO_AUTH: requireAuth
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(fset(gw, asfRequireAuth));
            env.close();
            env.trust(USD(10'000), alice, bob);
            env.close();

            env(paychan::create(alice, bob, USD(1), 100s, alice.pk()),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecNO_AUTH: requireAuth
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            auto const aliceUSD = alice["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, aliceUSD(10'000)), txflags(tfSetfAuth));
            env.trust(USD(10'000), alice, bob);
            env.close();

            env(paychan::create(alice, bob, USD(1), 100s, alice.pk()),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecFROZEN: account is frozen
        {
            // Env Setup
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, USD(100'000)));
            env(trust(bob, USD(100'000)));
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // set freeze on alice trustline
            env(trust(gw, USD(10'000), alice, tfSetFreeze));
            env.close();

            env(paychan::create(alice, bob, USD(1), 100s, alice.pk()),
                ter(tecFROZEN));
            env.close();
        }

        // tecFROZEN: dest is frozen
        {
            // Env Setup
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, USD(100'000)));
            env(trust(bob, USD(100'000)));
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10'000), bob, tfSetFreeze));
            env.close();

            env(paychan::create(alice, bob, USD(1), 100s, alice.pk()),
                ter(tecFROZEN));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS
        {
            // Env Setup
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, USD(100'000)));
            env(trust(bob, USD(100'000)));
            env.close();

            env(paychan::create(alice, bob, USD(1), 100s, alice.pk()),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS
        {
            // Env Setup
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, USD(100'000)));
            env(trust(bob, USD(100'000)));
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            env(paychan::create(alice, bob, USD(10'001), 100s, alice.pk()),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }

        // tecPRECISION_LOSS
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(100000000000000000), alice);
            env.trust(USD(100000000000000000), bob);
            env.close();
            env(pay(gw, alice, USD(10000000000000000)));
            env(pay(gw, bob, USD(1)));
            env.close();

            // alice cannot create paychan for 1/10 iou - precision loss
            env(paychan::create(alice, bob, USD(1), 100s, alice.pk()),
                ter(tecPRECISION_LOSS));
            env.close();
        }
    }

    void
    testIOUClaimPreclaim(FeatureBitset features)
    {
        testcase("IOU Claim Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_AUTH: requireAuth set: dest not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            auto const aliceUSD = alice["USD"];
            auto const bobUSD = bob["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, aliceUSD(10'000)), txflags(tfSetfAuth));
            env(trust(gw, bobUSD(10'000)), txflags(tfSetfAuth));
            env.trust(USD(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, USD(1), 100s, alice.pk()),
                ter(tesSUCCESS));
            env.close();

            env(pay(bob, gw, USD(10'000)));
            env(trust(gw, bobUSD(0)), txflags(tfSetfAuth));
            env(trust(bob, USD(0)));
            env.close();

            env.trust(USD(10'000), bob);
            env.close();

            // bob cannot claim because he is not authorized
            auto const sig =
                paychan::signClaimAuth(alice.pk(), alice.sk(), chan, USD(1));
            env(paychan::claim(
                    bob, chan, USD(1), USD(1), Slice(sig), alice.pk()),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecFROZEN: issuer has deep frozen the dest
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, USD(1), 100s, alice.pk()),
                ter(tesSUCCESS));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10'000), bob, tfSetFreeze | tfSetDeepFreeze));

            // bob cannot claim because of deep freeze
            auto const sig =
                paychan::signClaimAuth(alice.pk(), alice.sk(), chan, USD(1));
            env(paychan::claim(
                    bob, chan, USD(1), USD(1), Slice(sig), alice.pk()),
                ter(tecFROZEN));
            env.close();
        }
    }

    void
    testIOUClaimDoApply(FeatureBitset features)
    {
        testcase("IOU Claim Do Apply");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_LINE_INSUF_RESERVE: insufficient reserve to create line
        {
            Env env{*this, features};
            auto const acctReserve = env.current()->fees().accountReserve(0);
            auto const incReserve = env.current()->fees().increment;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, gw);
            env.fund(acctReserve + (incReserve - 1), bob);
            env.close();
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(10'000), alice);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env.close();

            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, USD(1), 100s, alice.pk()),
                ter(tesSUCCESS));
            env.close();

            // bob cannot claim because insufficient reserve to create line
            auto const sig =
                paychan::signClaimAuth(alice.pk(), alice.sk(), chan, USD(1));
            env(paychan::claim(
                    bob, chan, USD(1), USD(1), Slice(sig), alice.pk()),
                ter(tecNO_LINE_INSUF_RESERVE));
            env.close();
        }

        // tecNO_LINE: alice submits; claim IOU not created
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(10'000), alice);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env.close();

            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, USD(1), 100s, alice.pk()),
                ter(tesSUCCESS));
            env.close();

            // alice cannot claim because bob does not have a trustline
            env(paychan::claim(alice, chan, USD(1), USD(1)), ter(tecNO_LINE));
            env.close();
        }

        // tecLIMIT_EXCEEDED: alice submits; IOU Limit < balance + amount
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(1000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env.close();

            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, USD(5), 100s, alice.pk()),
                ter(tesSUCCESS));
            env.close();

            env.trust(USD(1), bob);
            env.close();

            // alice cannot claim because bobs limit is too low
            env(paychan::claim(alice, chan, USD(5), USD(5)),
                ter(tecLIMIT_EXCEEDED));
            env.close();
        }

        // tesSUCCESS: bob submits; IOU Limit < balance + amount
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(1000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env.close();

            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, USD(5), 100s, alice.pk()),
                ter(tesSUCCESS));
            env.close();

            env.trust(USD(1), bob);
            env.close();

            auto const bobPreLimit = env.limit(bob, USD);

            // bob can claim even if bobs limit is too low
            auto const sig =
                paychan::signClaimAuth(alice.pk(), alice.sk(), chan, USD(5));
            env(paychan::claim(
                    bob, chan, USD(5), USD(5), Slice(sig), alice.pk()),
                ter(tesSUCCESS));
            env.close();

            // bobs limit is not changed
            BEAST_EXPECT(env.limit(bob, USD) == bobPreLimit);
        }
    }

    void
    testIOUBalances(FeatureBitset features)
    {
        testcase("IOU Balances");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(5000), alice, bob, gw);
        env(fset(gw, asfAllowTrustLineLocking));
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(5'000)));
        env(pay(gw, bob, USD(5'000)));
        env.close();

        auto const outstandingUSD = USD(10'000);

        // Create & Claim (Dest) PayChan
        auto const chan = paychan::channel(alice, bob, env.seq(alice));
        {
            auto const preAliceUSD = env.balance(alice, USD);
            auto const preBobUSD = env.balance(bob, USD);
            env(paychan::create(alice, bob, USD(1'000), 1s, alice.pk()),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAliceUSD - USD(1'000));
            BEAST_EXPECT(env.balance(bob, USD) == preBobUSD);
            BEAST_EXPECT(
                issuerBalance(env, gw, USD) == outstandingUSD - USD(1'000));
            BEAST_EXPECT(issuerEscrowed(env, gw, USD) == USD(1'000));
        }
        {
            auto const preAliceUSD = env.balance(alice, USD);
            auto const preBobUSD = env.balance(bob, USD);
            auto const sig = paychan::signClaimAuth(
                alice.pk(), alice.sk(), chan, USD(1'000));
            env(paychan::claim(
                    bob, chan, USD(1'000), USD(1'000), Slice(sig), alice.pk()),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAliceUSD);
            BEAST_EXPECT(env.balance(bob, USD) == preBobUSD + USD(1'000));
            BEAST_EXPECT(issuerBalance(env, gw, USD) == outstandingUSD);
            BEAST_EXPECT(issuerEscrowed(env, gw, USD) == USD(0));
        }

        // Create & Claim (Account) PayChan
        auto const chan2 = paychan::channel(alice, bob, env.seq(alice));
        {
            auto const preAliceUSD = env.balance(alice, USD);
            auto const preBobUSD = env.balance(bob, USD);
            env(paychan::create(alice, bob, USD(1'000), 100s, alice.pk()),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAliceUSD - USD(1'000));
            BEAST_EXPECT(env.balance(bob, USD) == preBobUSD);
            BEAST_EXPECT(
                issuerBalance(env, gw, USD) == outstandingUSD - USD(1'000));
            BEAST_EXPECT(issuerEscrowed(env, gw, USD) == USD(1'000));
        }
        {
            auto const preAliceUSD = env.balance(alice, USD);
            auto const preBobUSD = env.balance(bob, USD);
            env(paychan::claim(alice, chan2, USD(1'000), USD(1'000)),
                txflags(tfClose),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAliceUSD);
            BEAST_EXPECT(env.balance(bob, USD) == preBobUSD + USD(1'000));
            BEAST_EXPECT(issuerBalance(env, gw, USD) == outstandingUSD);
            BEAST_EXPECT(issuerEscrowed(env, gw, USD) == USD(0));
        }
    }

    void
    testIOUMetaAndOwnership(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        {
            testcase("IOU Metadata to other");

            Env env{*this, features};
            env.fund(XRP(5000), alice, bob, carol, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(10'000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bob);

            auto const pk = alice.pk();
            auto const pk2 = bob.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, USD(1'000), settleDelay, pk));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close();
            env(paychan::create(bob, carol, USD(1'000), settleDelay, pk2));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close();

            auto const ab = env.le(keylet::payChan(alice.id(), bob.id(), aseq));
            BEAST_EXPECT(ab);

            auto const bc = env.le(keylet::payChan(bob.id(), carol.id(), bseq));
            BEAST_EXPECT(bc);

            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) != aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 3);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) != bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) != bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);
                BEAST_EXPECT(
                    std::find(cod.begin(), cod.end(), bc) != cod.end());

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 5);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ab) != iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bc) != iod.end());
            }

            auto const chan_ab = paychan::channel(alice, bob, aseq);
            env(paychan::claim(alice, chan_ab, USD(1'000), USD(1'000)),
                txflags(tfClose));
            {
                BEAST_EXPECT(
                    !env.le(keylet::payChan(alice.id(), bob.id(), aseq)));
                BEAST_EXPECT(
                    env.le(keylet::payChan(bob.id(), carol.id(), bseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) == bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) != bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 4);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ab) == iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bc) != iod.end());
            }

            env.close();
            auto const chan_bc = paychan::channel(bob, carol, bseq);
            env(paychan::claim(bob, chan_bc, USD(1'000), USD(1'000)),
                txflags(tfClose));
            {
                BEAST_EXPECT(
                    !env.le(keylet::payChan(alice.id(), bob.id(), aseq)));
                BEAST_EXPECT(
                    !env.le(keylet::payChan(bob.id(), carol.id(), bseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) == bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) == bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ab) == iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bc) == iod.end());
            }
        }

        {
            testcase("IOU Metadata to issuer");

            Env env{*this, features};
            env.fund(XRP(5000), alice, carol, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(10'000), alice, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();
            auto const aseq = env.seq(alice);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, gw, USD(1'000), settleDelay, pk));

            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close();
            env(paychan::create(gw, carol, USD(1'000), settleDelay, alice.pk()),
                ter(tecNO_PERMISSION));
            env.close();

            auto const ag = env.le(keylet::payChan(alice.id(), gw.id(), aseq));
            BEAST_EXPECT(ag);

            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ag) != aod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ag) != iod.end());
            }

            auto const chan_ag = paychan::channel(alice, gw, aseq);
            env(paychan::claim(alice, chan_ag, USD(1'000), USD(1'000)),
                txflags(tfClose));
            {
                BEAST_EXPECT(
                    !env.le(keylet::payChan(alice.id(), gw.id(), aseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ag) == aod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 2);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ag) == iod.end());
            }
        }
    }

    void
    testIOURippleState(FeatureBitset features)
    {
        testcase("IOU RippleState");
        using namespace test::jtx;
        using namespace std::literals;

        struct TestAccountData
        {
            Account src;
            Account dst;
            Account gw;
            bool hasTrustline;
            bool negative;
        };

        std::array<TestAccountData, 8> tests = {{
            // src > dst && src > issuer && dst no trustline
            {Account("alice2"), Account("bob0"), Account{"gw0"}, false, true},
            // src < dst && src < issuer && dst no trustline
            {Account("carol0"), Account("dan1"), Account{"gw1"}, false, false},
            // dst > src && dst > issuer && dst no trustline
            {Account("dan1"), Account("alice2"), Account{"gw0"}, false, true},
            // dst < src && dst < issuer && dst no trustline
            {Account("bob0"), Account("carol0"), Account{"gw1"}, false, false},
            // src > dst && src > issuer && dst has trustline
            {Account("alice2"), Account("bob0"), Account{"gw0"}, true, true},
            // src < dst && src < issuer && dst has trustline
            {Account("carol0"), Account("dan1"), Account{"gw1"}, true, false},
            // dst > src && dst > issuer && dst has trustline
            {Account("dan1"), Account("alice2"), Account{"gw0"}, true, true},
            // dst < src && dst < issuer && dst has trustline
            {Account("bob0"), Account("carol0"), Account{"gw1"}, true, false},
        }};

        for (auto const& t : tests)
        {
            Env env{*this, features};
            auto const USD = t.gw["USD"];
            env.fund(XRP(5000), t.src, t.dst, t.gw);
            env(fset(t.gw, asfAllowTrustLineLocking));
            env.close();

            if (t.hasTrustline)
                env.trust(USD(100'000), t.src, t.dst);
            else
                env.trust(USD(100'000), t.src);
            env.close();

            env(pay(t.gw, t.src, USD(10'000)));
            if (t.hasTrustline)
                env(pay(t.gw, t.dst, USD(10'000)));
            env.close();

            // src can create paychan
            auto const seq1 = env.seq(t.src);
            auto const delta = USD(1'000);
            auto const pk = t.src.pk();
            auto const settleDelay = 100s;
            env(paychan::create(t.src, t.dst, delta, settleDelay, pk));
            env.close();

            // dst can claim paychan
            auto const preSrc = env.balance(t.src, USD);
            auto const preDst = env.balance(t.dst, USD);

            auto const chan = paychan::channel(t.src, t.dst, seq1);
            auto const sig =
                paychan::signClaimAuth(pk, t.src.sk(), chan, delta);
            env(paychan::claim(t.dst, chan, delta, delta, Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(t.src, USD) == preSrc);
            BEAST_EXPECT(env.balance(t.dst, USD) == preDst + delta);
        }
    }

    void
    testIOUGateway(FeatureBitset features)
    {
        testcase("IOU Gateway");
        using namespace test::jtx;
        using namespace std::literals;

        // issuer is source
        {
            auto const gw = Account{"gateway"};
            auto const alice = Account{"alice"};
            Env env{*this, features};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(100'000), alice);
            env.close();

            env(pay(gw, alice, USD(10'000)));
            env.close();

            // issuer cannot create paychan
            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            env(paychan::create(gw, alice, USD(1'000), settleDelay, pk),
                ter(tecNO_PERMISSION));
            env.close();
        }

        struct TestAccountData
        {
            Account src;
            Account dst;
            bool hasTrustline;
        };

        std::array<TestAccountData, 4> gwDstTests = {{
            // src > dst && src > issuer && dst has trustline
            {Account("alice2"), Account{"gw0"}, true},
            // src < dst && src < issuer && dst has trustline
            {Account("carol0"), Account{"gw1"}, true},
            // dst > src && dst > issuer && dst has trustline
            {Account("dan1"), Account{"gw0"}, true},
            // dst < src && dst < issuer && dst has trustline
            {Account("bob0"), Account{"gw1"}, true},
        }};

        // issuer is destination
        for (auto const& t : gwDstTests)
        {
            Env env{*this, features};
            auto const USD = t.dst["USD"];
            env.fund(XRP(5000), t.dst, t.src);
            env(fset(t.dst, asfAllowTrustLineLocking));
            env.close();

            env.trust(USD(100'000), t.src);
            env.close();

            env(pay(t.dst, t.src, USD(10'000)));
            env.close();

            // issuer can receive paychan
            auto const seq1 = env.seq(t.src);
            auto const preSrc = env.balance(t.src, USD);
            auto const pk = t.src.pk();
            auto const settleDelay = 100s;
            env(paychan::create(t.src, t.dst, USD(1'000), settleDelay, pk));
            env.close();

            // issuer can claim paychan, no dest trustline
            auto const chan = paychan::channel(t.src, t.dst, seq1);
            auto const sig =
                paychan::signClaimAuth(pk, t.src.sk(), chan, USD(1'000));
            env(paychan::claim(
                t.dst, chan, USD(1'000), USD(1'000), Slice(sig), pk));
            env.close();
            auto const preAmount = 10'000;
            BEAST_EXPECT(preSrc == USD(preAmount));
            auto const postAmount = 9000;
            BEAST_EXPECT(env.balance(t.src, USD) == USD(postAmount));
            BEAST_EXPECT(env.balance(t.dst, USD) == USD(0));
        }
    }

    void
    testIOULockedRate(FeatureBitset features)
    {
        testcase("IOU Locked Rate");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test locked rate
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();
            auto const transferRate = paychan::rate(env, alice, bob, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // bob can claim paychan
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10'100));
        }
        // test rate change - higher
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();
            auto transferRate = paychan::rate(env, alice, bob, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // issuer changes rate higher
            env(rate(gw, 1.26));
            env.close();

            // bob can claim paychan - rate unchanged
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10'100));
        }
        // test rate change - lower
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();
            auto transferRate = paychan::rate(env, alice, bob, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // issuer changes rate lower
            env(rate(gw, 1.00));
            env.close();

            // bob can claim paychan - rate changed
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10125));
        }

        // test claim/close doesnt charge rate
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();
            auto transferRate = paychan::rate(env, alice, bob, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // issuer changes rate lower
            env(rate(gw, 1.00));
            env.close();

            // alice can close paychan - rate is not charged
            auto const chan = paychan::channel(alice, bob, seq1);
            env(paychan::claim(bob, chan), txflags(tfClose));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10000));
        }
    }

    void
    testIOULimitAmount(FeatureBitset features)
    {
        testcase("IOU Limit");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test LimitAmount
        {
            Env env{*this, features};
            env.fund(XRP(1'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1'000)));
            env(pay(gw, bob, USD(1'000)));
            env.close();

            // alice can create paychan
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // bob can claim
            auto const preBobLimit = env.limit(bob, USD);
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();
            auto const postBobLimit = env.limit(bob, USD);
            // bobs limit is NOT changed
            BEAST_EXPECT(postBobLimit == preBobLimit);
        }
    }

    void
    testIOURequireAuth(FeatureBitset features)
    {
        testcase("IOU Require Auth");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

        Env env{*this, features};
        env.fund(XRP(1'000), alice, bob, gw);
        env(fset(gw, asfAllowTrustLineLocking));
        env(fset(gw, asfRequireAuth));
        env.close();
        env(trust(gw, aliceUSD(10'000)), txflags(tfSetfAuth));
        env(trust(alice, USD(10'000)));
        env(trust(bob, USD(10'000)));
        env.close();
        env(pay(gw, alice, USD(1'000)));
        env.close();

        // alice cannot create paychan - fails without auth
        auto seq1 = env.seq(alice);
        auto const delta = USD(125);
        auto const pk = alice.pk();
        auto const settleDelay = 100s;
        env(paychan::create(alice, bob, delta, settleDelay, pk),
            ter(tecNO_AUTH));
        env.close();

        // set auth on bob
        env(trust(gw, bobUSD(10'000)), txflags(tfSetfAuth));
        env(trust(bob, USD(10'000)));
        env.close();
        env(pay(gw, bob, USD(1'000)));
        env.close();

        // alice can create paychan - bob has auth
        seq1 = env.seq(alice);
        env(paychan::create(alice, bob, delta, settleDelay, pk));
        env.close();

        // bob can claim
        auto const chan = paychan::channel(alice, bob, seq1);
        auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, delta);
        env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
        env.close();
    }

    void
    testIOUFreeze(FeatureBitset features)
    {
        testcase("IOU Freeze");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test Global Freeze
        {
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // setup transaction
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;

            // create paychan fails - frozen trustline
            env(paychan::create(alice, bob, delta, settleDelay, pk),
                ter(tecFROZEN));
            env.close();

            // clear global freeze
            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // create paychan success
            seq1 = env.seq(alice);
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // set global freeze
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // bob claim paychan success regardless of frozen assets
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();

            // clear global freeze
            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // create paychan success
            seq1 = env.seq(alice);
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // set global freeze
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // alice close paychan success regardless of frozen assets
            auto const chan2 = paychan::channel(alice, bob, seq1);
            env(paychan::claim(alice, chan2, delta, delta), txflags(tfClose));
            env.close();
        }

        // test Individual Freeze
        {
            // Env Setup
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, USD(100'000)));
            env(trust(bob, USD(100'000)));
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // set freeze on alice trustline
            env(trust(gw, USD(10'000), alice, tfSetFreeze));
            env.close();

            // setup transaction
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;

            // create paychan fails - frozen trustline
            env(paychan::create(alice, bob, delta, settleDelay, pk),
                ter(tecFROZEN));
            env.close();

            // clear freeze on alice trustline
            env(trust(gw, USD(10'000), alice, tfClearFreeze));
            env.close();

            // create paychan success
            seq1 = env.seq(alice);
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10'000), bob, tfSetFreeze));
            env.close();

            // bob claim paychan success regardless of frozen assets
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();

            // reset freeze on bob and alice trustline
            env(trust(gw, USD(10'000), alice, tfClearFreeze));
            env(trust(gw, USD(10'000), bob, tfClearFreeze));
            env.close();

            // create paychan success
            seq1 = env.seq(alice);
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10'000), bob, tfSetFreeze));
            env.close();

            // alice close paychan success regardless of frozen assets
            auto const chan2 = paychan::channel(alice, bob, seq1);
            env(paychan::claim(alice, chan2, delta, delta), txflags(tfClose));
            env.close();
        }

        // test Deep Freeze
        {
            // Env Setup
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, USD(100'000)));
            env(trust(bob, USD(100'000)));
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // set freeze on alice trustline
            env(trust(gw, USD(10'000), alice, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // setup transaction
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;

            // create paychan fails - frozen trustline
            env(paychan::create(alice, bob, delta, settleDelay, pk),
                ter(tecFROZEN));
            env.close();

            // clear freeze on alice trustline
            env(trust(
                gw, USD(10'000), alice, tfClearFreeze | tfClearDeepFreeze));
            env.close();

            // create paychan success
            seq1 = env.seq(alice);
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10'000), bob, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // bob claim paychan fails because of deep frozen assets
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk),
                ter(tecFROZEN));
            env.close();

            // reset freeze on alice and bob trustline
            env(trust(
                gw, USD(10'000), alice, tfClearFreeze | tfClearDeepFreeze));
            env(trust(gw, USD(10'000), bob, tfClearFreeze | tfClearDeepFreeze));
            env.close();

            // create paychan success
            seq1 = env.seq(alice);
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10'000), bob, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // bob close paychan success regardless of deep frozen assets
            auto const chan2 = paychan::channel(alice, bob, seq1);
            env(paychan::claim(bob, chan2), txflags(tfClose));
            env.close();
        }
    }

    void
    testIOUINSF(FeatureBitset features)
    {
        testcase("IOU Insufficient Funds");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        {
            // test tecPATH_PARTIAL
            // ie. has 10'000, paychan 1'000 then try to pay 10'000
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // create paychan success
            auto const delta = USD(1'000);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();
            env(pay(alice, gw, USD(10'000)), ter(tecPATH_PARTIAL));
        }
        {
            // test tecINSUFFICIENT_FUNDS
            // ie. has 10'000 paychan 1'000 then try to paychan 10'000
            Env env{*this, features};
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            auto const delta = USD(1'000);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, delta, settleDelay, pk));
            env.close();

            env(paychan::create(alice, bob, USD(10'000), settleDelay, pk),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }
    }

    void
    testIOUPrecisionLoss(FeatureBitset features)
    {
        testcase("IOU Precision Loss");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test min create precision loss
        {
            Env env(*this, features);
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(100000000000000000), alice);
            env.trust(USD(100000000000000000), bob);
            env.close();
            env(pay(gw, alice, USD(10000000000000000)));
            env(pay(gw, bob, USD(1)));
            env.close();

            // alice cannot create paychan for 1/10 iou - precision loss
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, USD(1), settleDelay, pk),
                ter(tecPRECISION_LOSS));
            env.close();

            auto const seq1 = env.seq(alice);
            // alice can create paychan for 1'000 iou
            env(paychan::create(alice, bob, USD(1'000), settleDelay, pk));
            env.close();

            // bob claim paychan success
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, USD(1'000));
            env(paychan::claim(
                bob, chan, USD(1'000), USD(1'000), Slice(sig), pk));
            env.close();
        }
    }

    void
    testMPTEnablement(FeatureBitset features)
    {
        testcase("MPT Enablement");

        using namespace jtx;
        using namespace std::chrono;

        for (bool const withTokenPaychan : {false, true})
        {
            auto const amend =
                withTokenPaychan ? features : features - featureTokenPaychan;
            Env env{*this, amend};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(5000), bob);

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const openResult =
                withTokenPaychan ? ter(tesSUCCESS) : ter(temBAD_AMOUNT);
            auto const closeResult =
                withTokenPaychan ? ter(tesSUCCESS) : ter(tecNO_TARGET);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, MPT(1'000), settleDelay, pk),
                openResult);
            env.close();
            env(paychan::fund(alice, chan, MPT(1'000)), openResult);
            env.close();
            env(paychan::claim(bob, chan), txflags(tfClose), closeResult);
            env.close();
        }
    }

    void
    testMPTCreatePreflight(FeatureBitset features)
    {
        testcase("MPT Create Preflight");
        using namespace test::jtx;
        using namespace std::literals;

        for (bool const withMPT : {true, false})
        {
            auto const amend =
                withMPT ? features : features - featureMPTokensV1;
            Env env{*this, amend};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(1'000), alice, bob, gw);

            Json::Value jv =
                paychan::create(alice, bob, XRP(1), 100s, alice.pk());
            jv.removeMember(jss::Amount);
            jv[jss::Amount][jss::mpt_issuance_id] =
                "00000004A407AF5856CCF3C42619DAA925813FC955C72983";
            jv[jss::Amount][jss::value] = "-1";

            auto const result = withMPT ? ter(temBAD_AMOUNT) : ter(temDISABLED);
            env(jv, result);
            env.close();
        }

        // temBAD_AMOUNT: amount < 0
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(-1), settleDelay, pk),
                ter(temBAD_AMOUNT));
            env.close();
        }
    }

    void
    testMPTCreatePreclaim(FeatureBitset features)
    {
        testcase("MPT Create Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_PERMISSION: issuer is the same as the account
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            env(paychan::create(gw, alice, MPT(1), settleDelay, pk),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // tecOBJECT_NOT_FOUND: mpt does not exist
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), alice, bob, gw);
            env.close();

            auto const mpt = ripple::test::jtx::MPT(
                alice.name(), makeMptID(env.seq(alice), alice));
            Json::Value jv =
                paychan::create(alice, bob, mpt(2), 100s, alice.pk());
            jv[jss::Amount][jss::mpt_issuance_id] =
                "00000004A407AF5856CCF3C42619DAA925813FC955C72983";
            env(jv, ter(tecOBJECT_NOT_FOUND));
            env.close();
        }

        // tecNO_PERMISSION: tfMPTCanEscrow is not enabled
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(3), settleDelay, pk),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // tecOBJECT_NOT_FOUND: account does not have the mpt
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            auto const MPT = mptGw["MPT"];

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(4), settleDelay, pk),
                ter(tecOBJECT_NOT_FOUND));
            env.close();
        }

        // tecNO_AUTH: requireAuth set: account not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags =
                     tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            // unauthorize account
            mptGw.authorize(
                {.account = gw, .holder = alice, .flags = tfMPTUnauthorize});

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(5), settleDelay, pk),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecNO_AUTH: requireAuth set: dest not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags =
                     tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = gw, .holder = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            // unauthorize dest
            mptGw.authorize(
                {.account = gw, .holder = bob, .flags = tfMPTUnauthorize});

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(6), settleDelay, pk),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecLOCKED: issuer has locked the account
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            // lock account
            mptGw.set({.account = gw, .holder = alice, .flags = tfMPTLock});

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(7), settleDelay, pk),
                ter(tecLOCKED));
            env.close();
        }

        // tecLOCKED: issuer has locked the dest
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            // lock dest
            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(8), settleDelay, pk),
                ter(tecLOCKED));
            env.close();
        }

        // tecNO_AUTH: mpt cannot be transferred
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(9), settleDelay, pk),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: spendable amount is zero
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, bob, MPT(10)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(11), settleDelay, pk),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: spendable amount is less than the amount
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10)));
            env(pay(gw, bob, MPT(10)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(11), settleDelay, pk),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }
    }

    void
    testMPTClaimPreclaim(FeatureBitset features)
    {
        testcase("MPT Claim Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_AUTH: requireAuth set: dest not authorized
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags =
                     tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = gw, .holder = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = gw, .holder = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, MPT(10), settleDelay, pk),
                ter(tesSUCCESS));
            env.close();

            // unauthorize dest
            mptGw.authorize(
                {.account = gw, .holder = bob, .flags = tfMPTUnauthorize});

            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, MPT(10));
            env(paychan::claim(bob, chan, MPT(10), MPT(10), Slice(sig), pk),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecLOCKED: issuer has locked the dest
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, MPT(8), settleDelay, pk),
                ter(tesSUCCESS));
            env.close();

            // lock dest
            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, MPT(8));
            env(paychan::claim(bob, chan, MPT(8), MPT(8), Slice(sig), pk),
                ter(tecLOCKED));
            env.close();
        }
    }

    void
    testMPTClaimDoApply(FeatureBitset features)
    {
        testcase("MPT Claim Do Apply");
        using namespace test::jtx;
        using namespace std::literals;

        // tecINSUFFICIENT_RESERVE: insufficient reserve to create MPT
        {
            Env env{*this, features};
            auto const acctReserve = env.current()->fees().accountReserve(0);
            auto const incReserve = env.current()->fees().increment;

            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(acctReserve + (incReserve - 1), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, MPT(10), settleDelay, pk),
                ter(tesSUCCESS));
            env.close();

            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, MPT(10));
            env(paychan::claim(bob, chan, MPT(10), MPT(10), Slice(sig), pk),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }

        // tesSUCCESS: bob submits; claim MPT created
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, MPT(10), settleDelay, pk),
                ter(tesSUCCESS));
            env.close();

            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, MPT(10));
            env(paychan::claim(bob, chan, MPT(10), MPT(10), Slice(sig), pk),
                ter(tesSUCCESS));
            env.close();
        }

        // tecNO_PERMISSION: alice submits; claim MPT not created
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, MPT(10), settleDelay, pk),
                ter(tesSUCCESS));
            env.close();

            env(paychan::claim(alice, chan, MPT(10), MPT(10)),
                ter(tecNO_PERMISSION));
            env.close();
        }
    }

    void
    testMPTBalances(FeatureBitset features)
    {
        testcase("MPT Balances");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gw");
        env.fund(XRP(5000), bob);

        MPTTester mptGw(env, gw, {.holders = {alice, carol}});
        mptGw.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanEscrow | tfMPTCanTransfer});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = carol});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10'000)));
        env(pay(gw, carol, MPT(10'000)));
        env.close();

        auto outstandingMPT = env.balance(gw, MPT);

        // Create & Claim (Dest) PayChan
        auto const chan = paychan::channel(alice, bob, env.seq(alice));
        {
            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preBobMPT = env.balance(bob, MPT);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(1'000), settleDelay, pk),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 1'000);
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 1'000);
        }
        {
            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preBobMPT = env.balance(bob, MPT);
            auto const pk = alice.pk();
            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, MPT(1'000));
            env(paychan::claim(
                    bob, chan, MPT(1'000), MPT(1'000), Slice(sig), pk),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT + MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 0);
        }

        // Create & Claim (Account) PayChan
        auto const chan2 = paychan::channel(alice, bob, env.seq(alice));
        {
            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preBobMPT = env.balance(bob, MPT);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(1'000), settleDelay, pk),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 1'000);
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 1'000);
        }
        {
            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preBobMPT = env.balance(bob, MPT);
            env(paychan::claim(alice, chan2, MPT(1'000), MPT(1'000)),
                txflags(tfClose),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT + MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 0);
        }

        // Multiple PayChans
        {
            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preBobMPT = env.balance(bob, MPT);
            auto const preCarolMPT = env.balance(carol, MPT);
            auto const pk = alice.pk();
            auto const pk2 = carol.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(1'000), settleDelay, pk),
                ter(tesSUCCESS));
            env.close();

            env(paychan::create(carol, bob, MPT(1'000), settleDelay, pk2),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 1'000);
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(carol, MPT) == preCarolMPT - MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, carol, MPT) == 1'000);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 2'000);
        }

        // Max MPT Amount Issued (PayChan 1 MPT)
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(maxMPTokenAmount)));
            env.close();

            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preBobMPT = env.balance(bob, MPT);
            auto const outstandingMPT = env.balance(gw, MPT);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, MPT(1), settleDelay, pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 1);
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 1);

            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, MPT(1));
            env(paychan::claim(bob, chan, MPT(1), MPT(1), Slice(sig), pk),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(!env.le(keylet::mptoken(MPT.mpt(), alice))
                              ->isFieldPresent(sfLockedAmount));
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT + MPT(1));
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 0);
            BEAST_EXPECT(!env.le(keylet::mptIssuance(MPT.mpt()))
                              ->isFieldPresent(sfLockedAmount));
        }

        // Max MPT Amount Issued (PayChan Max MPT)
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(maxMPTokenAmount)));
            env.close();

            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preBobMPT = env.balance(bob, MPT);
            auto const outstandingMPT = env.balance(gw, MPT);

            // PayChan Max MPT - 10
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan1 = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(
                alice, bob, MPT(maxMPTokenAmount - 10), settleDelay, pk));
            env.close();

            // PayChan 10 MPT
            auto const chan2 = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, MPT(10), settleDelay, pk));
            env.close();

            BEAST_EXPECT(
                env.balance(alice, MPT) == preAliceMPT - MPT(maxMPTokenAmount));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == maxMPTokenAmount);
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == maxMPTokenAmount);

            auto const sig1 = paychan::signClaimAuth(
                pk, alice.sk(), chan1, MPT(maxMPTokenAmount - 10));
            env(paychan::claim(
                    bob,
                    chan1,
                    MPT(maxMPTokenAmount - 10),
                    MPT(maxMPTokenAmount - 10),
                    Slice(sig1),
                    pk),
                ter(tesSUCCESS));
            env.close();

            auto const sig2 =
                paychan::signClaimAuth(pk, alice.sk(), chan2, MPT(10));
            env(paychan::claim(bob, chan2, MPT(10), MPT(10), Slice(sig2), pk),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(
                env.balance(alice, MPT) == preAliceMPT - MPT(maxMPTokenAmount));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(
                env.balance(bob, MPT) == preBobMPT + MPT(maxMPTokenAmount));
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 0);
        }
    }

    void
    testMPTMetaAndOwnership(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        {
            testcase("MPT Metadata to other");

            Env env{*this, features};
            MPTTester mptGw(env, gw, {.holders = {alice, bob, carol}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            mptGw.authorize({.account = carol});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env(pay(gw, carol, MPT(10'000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bob);

            auto const pk = alice.pk();
            auto const pk2 = bob.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(1'000), settleDelay, pk));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close();
            env(paychan::create(bob, carol, MPT(1'000), settleDelay, pk2));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close();

            auto const ab = env.le(keylet::payChan(alice.id(), bob.id(), aseq));
            BEAST_EXPECT(ab);

            auto const bc = env.le(keylet::payChan(bob.id(), carol.id(), bseq));
            BEAST_EXPECT(bc);

            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) != aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 3);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) != bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) != bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);
                BEAST_EXPECT(
                    std::find(cod.begin(), cod.end(), bc) != cod.end());
            }

            auto const chan_ab = paychan::channel(alice, bob, aseq);
            env(paychan::claim(alice, chan_ab, MPT(1'000), MPT(1'000)),
                txflags(tfClose));
            {
                BEAST_EXPECT(
                    !env.le(keylet::payChan(alice.id(), bob.id(), aseq)));
                BEAST_EXPECT(
                    env.le(keylet::payChan(bob.id(), carol.id(), bseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) == bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) != bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);
            }

            env.close();
            auto const chan_bc = paychan::channel(bob, carol, bseq);
            env(paychan::claim(bob, chan_bc, MPT(1'000), MPT(1'000)),
                txflags(tfClose));
            {
                BEAST_EXPECT(
                    !env.le(keylet::payChan(alice.id(), bob.id(), aseq)));
                BEAST_EXPECT(
                    !env.le(keylet::payChan(bob.id(), carol.id(), bseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) == bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) == bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);
            }
        }
    }

    void
    testMPTGateway(FeatureBitset features)
    {
        testcase("MPT Gateway Balances");
        using namespace test::jtx;
        using namespace std::literals;

        // issuer is source
        {
            auto const gw = Account{"gateway"};
            auto const alice = Account{"alice"};
            Env env{*this, features};

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            // issuer cannot create paychan
            auto const pk = gw.pk();
            auto const settleDelay = 100s;
            env(paychan::create(gw, alice, MPT(1'000), settleDelay, pk),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // issuer is dest; alice w/ authorization
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            // issuer can be destination
            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preOutstanding = env.balance(gw, MPT);
            auto const preEscrowed = issuerMPTEscrowed(env, MPT);
            BEAST_EXPECT(preOutstanding == MPT(10'000));
            BEAST_EXPECT(preEscrowed == 0);

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, gw, env.seq(alice));
            env(paychan::create(alice, gw, MPT(1'000), settleDelay, pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 1'000);
            BEAST_EXPECT(env.balance(gw, MPT) == preOutstanding);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == preEscrowed + 1'000);

            // issuer (dest) can claim paychan
            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, MPT(1'000));
            env(paychan::claim(
                gw, chan, MPT(1'000), MPT(1'000), Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == preOutstanding - MPT(1'000));
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == preEscrowed);
        }
    }

    void
    testMPTLockedRate(FeatureBitset features)
    {
        testcase("MPT Locked Rate");
        using namespace test::jtx;
        using namespace std::literals;

        // test locked rate: claim
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.transferFee = 25000,
                 .ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const preAlice = env.balance(alice, MPT);
            auto const seq1 = env.seq(alice);
            auto const delta = MPT(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(125), settleDelay, pk));
            env.close();
            auto const transferRate = paychan::rate(env, alice, bob, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // bob can claim paychan
            auto const chan = paychan::channel(alice, bob, seq1);
            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, delta);
            env(paychan::claim(bob, chan, delta, delta, Slice(sig), pk));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, MPT) == MPT(10'100));
        }

        // test locked rate: close
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.transferFee = 25000,
                 .ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            // alice can create paychan w/ xfer rate
            auto const preAlice = env.balance(alice, MPT);
            auto const preBob = env.balance(bob, MPT);
            auto const seq1 = env.seq(alice);
            auto const delta = MPT(125);
            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            env(paychan::create(alice, bob, MPT(125), settleDelay, pk));
            env.close();
            auto const transferRate = paychan::rate(env, alice, bob, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // bob can close paychan
            auto const chan = paychan::channel(alice, bob, seq1);
            env(paychan::claim(bob, chan), txflags(tfClose));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAlice);
            BEAST_EXPECT(env.balance(bob, MPT) == preBob);
        }
    }

    // void
    // testMPTRequireAuth(FeatureBitset features)
    // {
    //     testcase("MPT Require Auth");
    //     using namespace test::jtx;
    //     using namespace std::literals;

    //     Env env{*this, features};
    //     auto const baseFee = env.current()->fees().base;
    //     auto const alice = Account("alice");
    //     auto const bob = Account("bob");
    //     auto const gw = Account("gw");

    //     MPTTester mptGw(env, gw, {.holders = {alice, bob}});
    //     mptGw.create(
    //         {.ownerCount = 1,
    //          .holderCount = 0,
    //          .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
    //     mptGw.authorize({.account = alice});
    //     mptGw.authorize({.account = gw, .holder = alice});
    //     mptGw.authorize({.account = bob});
    //     mptGw.authorize({.account = gw, .holder = bob});
    //     auto const MPT = mptGw["MPT"];
    //     env(pay(gw, alice, MPT(10'000)));
    //     env.close();

    //     auto seq = env.seq(alice);
    //     auto const delta = MPT(125);
    //     // alice can create escrow - is authorized
    //     env(escrow::create(alice, bob, MPT(100)),
    //         escrow::condition(escrow::cb1),
    //         escrow::finish_time(env.now() + 1s),
    //         fee(baseFee * 150));
    //     env.close();

    //     // bob can finish escrow - is authorized
    //     env(escrow::finish(bob, alice, seq),
    //         escrow::condition(escrow::cb1),
    //         escrow::fulfillment(escrow::fb1),
    //         fee(baseFee * 150));
    //     env.close();
    // }

    void
    testMPTLock(FeatureBitset features)
    {
        testcase("MPT Lock");
        using namespace test::jtx;
        using namespace std::literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        mptGw.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTCanLock});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = bob});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10'000)));
        env(pay(gw, bob, MPT(10'000)));
        env.close();

        // alice create paychan
        auto const pk = alice.pk();
        auto const settleDelay = 100s;
        auto const chan = paychan::channel(alice, bob, env.seq(alice));
        env(paychan::create(alice, bob, MPT(100), settleDelay, pk));
        env.close();

        // lock account & dest
        mptGw.set({.account = gw, .holder = alice, .flags = tfMPTLock});
        mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

        // bob cannot claim
        auto const sig = paychan::signClaimAuth(pk, alice.sk(), chan, MPT(100));
        env(paychan::claim(bob, chan, MPT(100), MPT(100), Slice(sig), pk),
            ter(tecLOCKED));
        env.close();

        // bob can claim/close
        env(paychan::claim(bob, chan), txflags(tfClose));
        env.close();
    }

    void
    testMPTCanTransfer(FeatureBitset features)
    {
        testcase("MPT Can Transfer");
        using namespace test::jtx;
        using namespace std::literals;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        mptGw.create(
            {.ownerCount = 1, .holderCount = 0, .flags = tfMPTCanEscrow});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = bob});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10'000)));
        env(pay(gw, bob, MPT(10'000)));
        env.close();

        // alice cannot create paychan to non issuer
        auto const pk = alice.pk();
        auto const settleDelay = 100s;
        env(paychan::create(alice, bob, MPT(100), settleDelay, pk),
            ter(tecNO_AUTH));
        env.close();

        // PayChan Create & Claim
        {
            // alice can create paychan to issuer
            auto const chan = paychan::channel(alice, gw, env.seq(alice));
            env(paychan::create(alice, gw, MPT(100), settleDelay, pk));
            env.close();

            // gw can claim
            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, MPT(100));
            env(paychan::claim(gw, chan, MPT(100), MPT(100), Slice(sig), pk));
            env.close();
        }

        // PayChan Create & Close
        {
            // alice can create paychan to issuer
            auto const chan = paychan::channel(alice, gw, env.seq(alice));
            env(paychan::create(alice, gw, MPT(100), settleDelay, pk));
            env.close();

            // gw can claim/close
            env(paychan::claim(gw, chan), txflags(tfClose));
            env.close();
        }
    }

    void
    testMPTDestroy(FeatureBitset features)
    {
        testcase("MPT Destroy");
        using namespace test::jtx;
        using namespace std::literals;

        // tecHAS_OBLIGATIONS: issuer cannot destroy issuance
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            mptGw.authorize({.account = bob});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env(pay(gw, bob, MPT(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, MPT(10), settleDelay, pk));
            env.close();

            env(pay(alice, gw, MPT(10'000)), ter(tecPATH_PARTIAL));
            env(pay(alice, gw, MPT(9'990)));
            env(pay(bob, gw, MPT(10'000)));
            BEAST_EXPECT(env.balance(alice, MPT) == MPT(0));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 10);
            BEAST_EXPECT(env.balance(bob, MPT) == MPT(0));
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == MPT(10));
            mptGw.authorize({.account = bob, .flags = tfMPTUnauthorize});
            mptGw.destroy(
                {.id = mptGw.issuanceID(),
                 .ownerCount = 1,
                 .err = tecHAS_OBLIGATIONS});

            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, MPT(10));
            env(paychan::claim(bob, chan, MPT(10), MPT(10), Slice(sig), pk),
                ter(tesSUCCESS));
            env.close();

            env(pay(bob, gw, MPT(10)));
            mptGw.destroy({.id = mptGw.issuanceID(), .ownerCount = 0});
        }

        // tecHAS_OBLIGATIONS: holder cannot destroy mptoken
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob);
            env.close();

            MPTTester mptGw(env, gw, {.holders = {alice}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            mptGw.authorize({.account = alice});
            auto const MPT = mptGw["MPT"];
            env(pay(gw, alice, MPT(10'000)));
            env.close();

            auto const pk = alice.pk();
            auto const settleDelay = 100s;
            auto const chan = paychan::channel(alice, bob, env.seq(alice));
            env(paychan::create(alice, bob, MPT(10), settleDelay, pk),
                ter(tesSUCCESS));
            env.close();

            env(pay(alice, gw, MPT(9'990)));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == MPT(0));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 10);
            mptGw.authorize(
                {.account = alice,
                 .flags = tfMPTUnauthorize,
                 .err = tecHAS_OBLIGATIONS});

            auto const sig =
                paychan::signClaimAuth(pk, alice.sk(), chan, MPT(10));
            env(paychan::claim(bob, chan, MPT(10), MPT(10), Slice(sig), pk),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == MPT(0));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            mptGw.authorize({.account = alice, .flags = tfMPTUnauthorize});
            BEAST_EXPECT(!env.le(keylet::mptoken(MPT.mpt(), alice)));
        }
    }

    void
    testIOUWithFeats(FeatureBitset features)
    {
        testIOUEnablement(features);
        testIOUAllowLockingFlag(features);
        testIOUCreatePreflight(features);
        testIOUCreatePreclaim(features);
        testIOUClaimPreclaim(features);
        testIOUClaimDoApply(features);
        // testIOUClaimClosePreclaim(features);
        testIOUBalances(features);
        testIOUMetaAndOwnership(features);
        testIOURippleState(features);
        testIOUGateway(features);
        testIOULockedRate(features);
        testIOULimitAmount(features);
        testIOURequireAuth(features);
        testIOUFreeze(features);
        testIOUINSF(features);
        testIOUPrecisionLoss(features);
    }

    void
    testMPTWithFeats(FeatureBitset features)
    {
        testMPTEnablement(features);
        testMPTCreatePreflight(features);
        testMPTCreatePreclaim(features);
        testMPTClaimPreclaim(features);
        testMPTClaimDoApply(features);
        // testMPTClaimClosePreclaim(features);
        testMPTBalances(features);
        testMPTMetaAndOwnership(features);
        testMPTGateway(features);
        testMPTLockedRate(features);
        // testMPTRequireAuth(features);
        testMPTLock(features);
        testMPTCanTransfer(features);
        testMPTDestroy(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testable_amendments()};
        testIOUWithFeats(all);
        testMPTWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(PayChanToken, app, ripple);
}  // namespace test
}  // namespace ripple
