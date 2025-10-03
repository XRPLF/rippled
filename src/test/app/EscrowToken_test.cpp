//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2024 Ripple Labs Inc.

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

#include <xrpld/app/tx/applySteps.h>

#include <xrpl/ledger/Dir.h>
#include <xrpl/ledger/Sandbox.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/STAmount.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <iterator>

namespace ripple {
namespace test {

struct EscrowToken_test : public beast::unit_test::suite
{
    static uint64_t
    mptEscrowed(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::MPT const& mpt)
    {
        auto const sle = env.le(keylet::mptoken(mpt.mpt(), account));
        if (sle && sle->isFieldPresent(sfLockedAmount))
            return (*sle)[sfLockedAmount];
        return 0;
    }

    static uint64_t
    issuerMPTEscrowed(jtx::Env const& env, jtx::MPT const& mpt)
    {
        auto const sle = env.le(keylet::mptIssuance(mpt.mpt()));
        if (sle && sle->isFieldPresent(sfLockedAmount))
            return (*sle)[sfLockedAmount];
        return 0;
    }

    jtx::PrettyAmount
    issuerBalance(
        jtx::Env& env,
        jtx::Account const& account,
        Issue const& issue)
    {
        Json::Value params;
        params[jss::account] = account.human();
        auto jrr = env.rpc("json", "gateway_balances", to_string(params));
        auto const result = jrr[jss::result];
        auto const obligations =
            result[jss::obligations][to_string(issue.currency)];
        if (obligations.isNull())
            return {STAmount(issue, 0), account.name()};
        STAmount const amount = amountFromString(issue, obligations.asString());
        return {amount, account.name()};
    }

    jtx::PrettyAmount
    issuerEscrowed(
        jtx::Env& env,
        jtx::Account const& account,
        Issue const& issue)
    {
        Json::Value params;
        params[jss::account] = account.human();
        auto jrr = env.rpc("json", "gateway_balances", to_string(params));
        auto const result = jrr[jss::result];
        auto const locked = result[jss::locked][to_string(issue.currency)];
        if (locked.isNull())
            return {STAmount(issue, 0), account.name()};
        STAmount const amount = amountFromString(issue, locked.asString());
        return {amount, account.name()};
    }

    void
    testIOUEnablement(FeatureBitset features)
    {
        testcase("IOU Enablement");

        using namespace jtx;
        using namespace std::chrono;

        for (bool const withTokenEscrow : {false, true})
        {
            auto const amend =
                withTokenEscrow ? features : features - featureTokenEscrow;
            Env env{*this, amend};
            auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            auto const createResult =
                withTokenEscrow ? ter(tesSUCCESS) : ter(temBAD_AMOUNT);
            auto const finishResult =
                withTokenEscrow ? ter(tesSUCCESS) : ter(tecNO_TARGET);

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, USD(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                createResult);
            env.close();
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                finishResult);
            env.close();

            auto const seq2 = env.seq(alice);
            env(escrow::create(alice, bob, USD(1'000)),
                escrow::condition(escrow::cb2),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s),
                fee(baseFee * 150),
                createResult);
            env.close();
            env(escrow::cancel(bob, alice, seq2), finishResult);
            env.close();
        }

        for (bool const withTokenEscrow : {false, true})
        {
            auto const amend =
                withTokenEscrow ? features : features - featureTokenEscrow;
            Env env{*this, amend};
            auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            auto const seq1 = env.seq(alice);
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tecNO_TARGET));
            env.close();

            env(escrow::cancel(bob, alice, seq1), ter(tecNO_TARGET));
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
        auto const baseFee = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(5000), alice, bob, gw);
        env(fset(gw, asfAllowTrustLineLocking));
        env.close();
        env.trust(USD(10'000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(5000)));
        env(pay(gw, bob, USD(5000)));
        env.close();

        // Create Escrow #1 & #2
        auto const seq1 = env.seq(alice);
        env(escrow::create(alice, bob, USD(1'000)),
            escrow::condition(escrow::cb1),
            escrow::finish_time(env.now() + 1s),
            fee(baseFee * 150),
            ter(tesSUCCESS));
        env.close();

        auto const seq2 = env.seq(alice);
        env(escrow::create(alice, bob, USD(1'000)),
            escrow::finish_time(env.now() + 1s),
            escrow::cancel_time(env.now() + 3s),
            fee(baseFee),
            ter(tesSUCCESS));
        env.close();

        // Clear the asfAllowTrustLineLocking flag
        env(fclear(gw, asfAllowTrustLineLocking));
        env.close();
        env.require(nflags(gw, asfAllowTrustLineLocking));

        // Cannot Create Escrow without asfAllowTrustLineLocking
        env(escrow::create(alice, bob, USD(1'000)),
            escrow::condition(escrow::cb1),
            escrow::finish_time(env.now() + 1s),
            fee(baseFee * 150),
            ter(tecNO_PERMISSION));
        env.close();

        // Can finish the escrow created before the flag was cleared
        env(escrow::finish(bob, alice, seq1),
            escrow::condition(escrow::cb1),
            escrow::fulfillment(escrow::fb1),
            fee(baseFee * 150),
            ter(tesSUCCESS));
        env.close();

        // Can cancel the escrow created before the flag was cleared
        env(escrow::cancel(bob, alice, seq2), ter(tesSUCCESS));
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
            env.fund(XRP(5000), alice, bob, gw);

            env(escrow::create(alice, bob, USD(1)),
                escrow::finish_time(env.now() + 1s),
                fee(XRP(-1)),
                ter(temBAD_FEE));
            env.close();
        }

        // temBAD_AMOUNT: amount <= 0
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);

            env(escrow::create(alice, bob, USD(-1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(temBAD_AMOUNT));
            env.close();
        }

        // temBAD_CURRENCY: badCurrency() == amount.getCurrency()
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const BAD = IOU(gw, badCurrency());
            env.fund(XRP(5000), alice, bob, gw);

            env(escrow::create(alice, bob, BAD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
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
            auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);

            env(escrow::create(gw, alice, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // tecNO_ISSUER: Issuer does not exist
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob);
            env.close();
            env.memoize(gw);

            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecNO_ISSUER));
            env.close();
        }

        // tecNO_PERMISSION: asfAllowTrustLineLocking is not set
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(gw, alice, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // tecNO_LINE: account does not have a trustline to the issuer
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecNO_LINE));
            env.close();
        }

        // tecNO_PERMISSION: Not testable
        // tecNO_PERMISSION: Not testable
        // tecNO_AUTH: requireAuth
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecNO_AUTH: requireAuth
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
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
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
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
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
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
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, USD(100'000)));
            env(trust(bob, USD(100'000)));
            env.close();

            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
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
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env(trust(alice, USD(100'000)));
            env(trust(bob, USD(100'000)));
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            env(escrow::create(alice, bob, USD(10'001)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
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
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(100000000000000000), alice);
            env.trust(USD(100000000000000000), bob);
            env.close();
            env(pay(gw, alice, USD(10000000000000000)));
            env(pay(gw, bob, USD(1)));
            env.close();

            // alice cannot create escrow for 1/10 iou - precision loss
            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecPRECISION_LOSS));
            env.close();
        }
    }

    void
    testIOUFinishPreclaim(FeatureBitset features)
    {
        testcase("IOU Finish Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_AUTH: requireAuth set: dest not authorized
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            env(pay(bob, gw, USD(10'000)));
            env(trust(gw, bobUSD(0)), txflags(tfSetfAuth));
            env(trust(bob, USD(0)));
            env.close();

            env.trust(USD(10'000), bob);
            env.close();

            // bob cannot finish because he is not authorized
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecFROZEN: issuer has deep frozen the dest
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10'000), bob, tfSetFreeze | tfSetDeepFreeze));

            // bob cannot finish because of deep freeze
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tecFROZEN));
            env.close();
        }
    }

    void
    testIOUFinishDoApply(FeatureBitset features)
    {
        testcase("IOU Finish Do Apply");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_LINE_INSUF_RESERVE: insufficient reserve to create line
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            auto const acctReserve = env.current()->fees().reserve;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            // bob cannot finish because insufficient reserve to create line
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tecNO_LINE_INSUF_RESERVE));
            env.close();
        }

        // tecNO_LINE: alice submits; finish IOU not created
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            // alice cannot finish because bob does not have a trustline
            env(escrow::finish(alice, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tecNO_LINE));
            env.close();
        }

        // tecLIMIT_EXCEEDED: alice submits; IOU Limit < balance + amount
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, USD(5)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            env.trust(USD(1), bob);
            env.close();

            // alice cannot finish because bobs limit is too low
            env(escrow::finish(alice, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tecLIMIT_EXCEEDED));
            env.close();
        }

        // tesSUCCESS: bob submits; IOU Limit < balance + amount
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, USD(5)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            env.trust(USD(1), bob);
            env.close();

            // bob can finish even if bobs limit is too low
            auto const bobPreLimit = env.limit(bob, USD);

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            // bobs limit is not changed
            BEAST_EXPECT(env.limit(bob, USD) == bobPreLimit);
        }
    }

    void
    testIOUCancelPreclaim(FeatureBitset features)
    {
        testcase("IOU Cancel Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_AUTH: requireAuth set: account not authorized
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, USD(1)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s),
                fee(baseFee),
                ter(tesSUCCESS));
            env.close();

            env(pay(alice, gw, USD(9'999)));
            env(trust(gw, aliceUSD(0)), txflags(tfSetfAuth));
            env(trust(alice, USD(0)));
            env.close();

            env.trust(USD(10'000), alice);
            env.close();

            // alice cannot cancel because she is not authorized
            env(escrow::cancel(bob, alice, seq1),
                fee(baseFee),
                ter(tecNO_AUTH));
            env.close();
        }
    }

    void
    testIOUBalances(FeatureBitset features)
    {
        testcase("IOU Balances");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const baseFee = env.current()->fees().base;
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

        // Create & Finish Escrow
        auto const seq1 = env.seq(alice);
        {
            auto const preAliceUSD = env.balance(alice, USD);
            auto const preBobUSD = env.balance(bob, USD);
            env(escrow::create(alice, bob, USD(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
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
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAliceUSD);
            BEAST_EXPECT(env.balance(bob, USD) == preBobUSD + USD(1'000));
            BEAST_EXPECT(issuerBalance(env, gw, USD) == outstandingUSD);
            BEAST_EXPECT(issuerEscrowed(env, gw, USD) == USD(0));
        }

        // Create & Cancel Escrow
        auto const seq2 = env.seq(alice);
        {
            auto const preAliceUSD = env.balance(alice, USD);
            auto const preBobUSD = env.balance(bob, USD);
            env(escrow::create(alice, bob, USD(1'000)),
                escrow::condition(escrow::cb2),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s),
                fee(baseFee * 150),
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
            env(escrow::cancel(bob, alice, seq2), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAliceUSD + USD(1'000));
            BEAST_EXPECT(env.balance(bob, USD) == preBobUSD);
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
            testcase("IOU Metadata to self");

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

            env(escrow::create(alice, alice, USD(1'000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 500s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            auto const aa = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(aa);
            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), aa) != aod.end());
            }

            {
                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 4);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), aa) != iod.end());
            }

            env(escrow::create(bob, bob, USD(1'000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            auto const bb = env.le(keylet::escrow(bob.id(), bseq));
            BEAST_EXPECT(bb);

            {
                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) != bod.end());
            }

            {
                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 5);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bb) != iod.end());
            }

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(
                    (*env.meta())[sfTransactionResult] ==
                    static_cast<std::uint8_t>(tesSUCCESS));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), aa) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) != bod.end());

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 4);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bb) != iod.end());
            }

            env.close(5s);
            env(escrow::cancel(bob, bob, bseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(bob.id(), bseq)));
                BEAST_EXPECT(
                    (*env.meta())[sfTransactionResult] ==
                    static_cast<std::uint8_t>(tesSUCCESS));

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) == bod.end());

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bb) == iod.end());
            }
        }
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

            env(escrow::create(alice, bob, USD(1'000)),
                escrow::finish_time(env.now() + 1s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow::create(bob, carol, USD(1'000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);

            auto const ab = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(ab);

            auto const bc = env.le(keylet::escrow(bob.id(), bseq));
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

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(env.le(keylet::escrow(bob.id(), bseq)));

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

            env.close(5s);
            env(escrow::cancel(bob, bob, bseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(!env.le(keylet::escrow(bob.id(), bseq)));

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

            env(escrow::create(alice, gw, USD(1'000)),
                escrow::finish_time(env.now() + 1s));

            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow::create(gw, carol, USD(1'000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s),
                ter(tecNO_PERMISSION));
            env.close(5s);

            auto const ag = env.le(keylet::escrow(alice.id(), aseq));
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

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));

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
            auto const baseFee = env.current()->fees().base;
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

            // src can create escrow
            auto const seq1 = env.seq(t.src);
            auto const delta = USD(1'000);
            env(escrow::create(t.src, t.dst, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            // dst can finish escrow
            auto const preSrc = env.balance(t.src, USD);
            auto const preDst = env.balance(t.dst, USD);

            env(escrow::finish(t.dst, t.src, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
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

        struct TestAccountData
        {
            Account src;
            Account dst;
            bool hasTrustline;
        };

        // issuer is source
        {
            auto const gw = Account{"gateway"};
            auto const alice = Account{"alice"};
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(100'000), alice);
            env.close();

            env(pay(gw, alice, USD(10'000)));
            env.close();

            // issuer cannot create escrow
            env(escrow::create(gw, alice, USD(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecNO_PERMISSION));
            env.close();
        }

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
            auto const baseFee = env.current()->fees().base;
            auto const USD = t.dst["USD"];
            env.fund(XRP(5000), t.dst, t.src);
            env(fset(t.dst, asfAllowTrustLineLocking));
            env.close();

            env.trust(USD(100'000), t.src);
            env.close();

            env(pay(t.dst, t.src, USD(10'000)));
            env.close();

            // issuer can receive escrow
            auto const seq1 = env.seq(t.src);
            auto const preSrc = env.balance(t.src, USD);
            env(escrow::create(t.src, t.dst, USD(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            // issuer can finish escrow, no dest trustline
            env(escrow::finish(t.dst, t.src, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
            env.close();
            auto const preAmount = 10'000;
            BEAST_EXPECT(preSrc == USD(preAmount));
            auto const postAmount = 9000;
            BEAST_EXPECT(env.balance(t.src, USD) == USD(postAmount));
            BEAST_EXPECT(env.balance(t.dst, USD) == USD(0));
        }

        // issuer is source and destination
        {
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();

            // issuer cannot receive escrow
            env(escrow::create(gw, gw, USD(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecNO_PERMISSION));
            env.close();
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
            auto const baseFee = env.current()->fees().base;
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

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();
            auto const transferRate = escrow::rate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // bob can finish escrow
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10'100));
        }
        // test rate change - higher
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();
            auto transferRate = escrow::rate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // issuer changes rate higher
            env(rate(gw, 1.26));
            env.close();

            // bob can finish escrow - rate unchanged
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10'100));
        }

        // test rate change - lower
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();
            auto transferRate = escrow::rate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // issuer changes rate lower
            env(rate(gw, 1.00));
            env.close();

            // bob can finish escrow - rate changed
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10125));
        }

        // test cancel doesnt charge rate
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow::create(alice, bob, delta),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 3s),
                fee(baseFee));
            env.close();
            auto transferRate = escrow::rate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // issuer changes rate lower
            env(rate(gw, 1.00));
            env.close();

            // alice can cancel escrow - rate is not charged
            env(escrow::cancel(alice, alice, seq1), fee(baseFee));
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
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(1'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(10'000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1'000)));
            env(pay(gw, bob, USD(1'000)));
            env.close();

            // alice can create escrow
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            // bob can finish
            auto const preBobLimit = env.limit(bob, USD);
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
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
        auto const baseFee = env.current()->fees().base;
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

        // alice cannot create escrow - fails without auth
        auto seq1 = env.seq(alice);
        auto const delta = USD(125);
        env(escrow::create(alice, bob, delta),
            escrow::condition(escrow::cb1),
            escrow::finish_time(env.now() + 1s),
            fee(baseFee * 150),
            ter(tecNO_AUTH));
        env.close();

        // set auth on bob
        env(trust(gw, bobUSD(10'000)), txflags(tfSetfAuth));
        env(trust(bob, USD(10'000)));
        env.close();
        env(pay(gw, bob, USD(1'000)));
        env.close();

        // alice can create escrow - bob has auth
        seq1 = env.seq(alice);
        env(escrow::create(alice, bob, delta),
            escrow::condition(escrow::cb1),
            escrow::finish_time(env.now() + 1s),
            fee(baseFee * 150));
        env.close();

        // bob can finish
        env(escrow::finish(bob, alice, seq1),
            escrow::condition(escrow::cb1),
            escrow::fulfillment(escrow::fb1),
            fee(baseFee * 150));
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
            auto const baseFee = env.current()->fees().base;
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

            // create escrow fails - frozen trustline
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecFROZEN));
            env.close();

            // clear global freeze
            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // create escrow success
            seq1 = env.seq(alice);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            // set global freeze
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // bob finish escrow success regardless of frozen assets
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
            env.close();

            // clear global freeze
            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // create escrow success
            seq1 = env.seq(alice);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::cancel_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            // set global freeze
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // bob cancel escrow success regardless of frozen assets
            env(escrow::cancel(bob, alice, seq1), fee(baseFee));
            env.close();
        }

        // test Individual Freeze
        {
            // Env Setup
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            // create escrow fails - frozen trustline
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecFROZEN));
            env.close();

            // clear freeze on alice trustline
            env(trust(gw, USD(10'000), alice, tfClearFreeze));
            env.close();

            // create escrow success
            seq1 = env.seq(alice);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10'000), bob, tfSetFreeze));
            env.close();

            // bob finish escrow success regardless of frozen assets
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
            env.close();

            // reset freeze on bob and alice trustline
            env(trust(gw, USD(10'000), alice, tfClearFreeze));
            env(trust(gw, USD(10'000), bob, tfClearFreeze));
            env.close();

            // create escrow success
            seq1 = env.seq(alice);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::cancel_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10'000), bob, tfSetFreeze));
            env.close();

            // bob cancel escrow success regardless of frozen assets
            env(escrow::cancel(bob, alice, seq1), fee(baseFee));
            env.close();
        }

        // test Deep Freeze
        {
            // Env Setup
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            // create escrow fails - frozen trustline
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecFROZEN));
            env.close();

            // clear freeze on alice trustline
            env(trust(
                gw, USD(10'000), alice, tfClearFreeze | tfClearDeepFreeze));
            env.close();

            // create escrow success
            seq1 = env.seq(alice);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10'000), bob, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // bob finish escrow fails because of deep frozen assets
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tecFROZEN));
            env.close();

            // reset freeze on alice and bob trustline
            env(trust(
                gw, USD(10'000), alice, tfClearFreeze | tfClearDeepFreeze));
            env(trust(gw, USD(10'000), bob, tfClearFreeze | tfClearDeepFreeze));
            env.close();

            // create escrow success
            seq1 = env.seq(alice);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::cancel_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10'000), bob, tfSetFreeze | tfSetDeepFreeze));
            env.close();

            // bob cancel escrow fails because of deep frozen assets
            env(escrow::cancel(bob, alice, seq1),
                fee(baseFee),
                ter(tesSUCCESS));
            env.close();
        }
    }
    void
    testIOUINSF(FeatureBitset features)
    {
        testcase("IOU Insuficient Funds");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        {
            // test tecPATH_PARTIAL
            // ie. has 10'000, escrow 1'000 then try to pay 10'000
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(100'000), alice);
            env.trust(USD(100'000), bob);
            env.close();
            env(pay(gw, alice, USD(10'000)));
            env(pay(gw, bob, USD(10'000)));
            env.close();

            // create escrow success
            auto const delta = USD(1'000);
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();
            env(pay(alice, gw, USD(10'000)), ter(tecPATH_PARTIAL));
        }
        {
            // test tecINSUFFICIENT_FUNDS
            // ie. has 10'000 escrow 1'000 then try to escrow 10'000
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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
            env(escrow::create(alice, bob, delta),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            env(escrow::create(alice, bob, USD(10'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
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
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(10'000), alice, bob, gw);
            env(fset(gw, asfAllowTrustLineLocking));
            env.close();
            env.trust(USD(100000000000000000), alice);
            env.trust(USD(100000000000000000), bob);
            env.close();
            env(pay(gw, alice, USD(10000000000000000)));
            env(pay(gw, bob, USD(1)));
            env.close();

            // alice cannot create escrow for 1/10 iou - precision loss
            env(escrow::create(alice, bob, USD(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecPRECISION_LOSS));
            env.close();

            auto const seq1 = env.seq(alice);
            // alice can create escrow for 1'000 iou
            env(escrow::create(alice, bob, USD(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            // bob finish escrow success
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
            env.close();
        }
    }

    void
    testMPTEnablement(FeatureBitset features)
    {
        testcase("MPT Enablement");

        using namespace jtx;
        using namespace std::chrono;

        for (bool const withTokenEscrow : {false, true})
        {
            auto const amend =
                withTokenEscrow ? features : features - featureTokenEscrow;
            Env env{*this, amend};
            auto const baseFee = env.current()->fees().base;
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

            auto const createResult =
                withTokenEscrow ? ter(tesSUCCESS) : ter(temBAD_AMOUNT);
            auto const finishResult =
                withTokenEscrow ? ter(tesSUCCESS) : ter(tecNO_TARGET);

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                createResult);
            env.close();
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                finishResult);
            env.close();
            auto const seq2 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(1'000)),
                escrow::condition(escrow::cb2),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s),
                fee(baseFee * 150),
                createResult);
            env.close();
            env(escrow::cancel(bob, alice, seq2), finishResult);
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
            auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(1'000), alice, bob, gw);

            Json::Value jv = escrow::create(alice, bob, XRP(1));
            jv.removeMember(jss::Amount);
            jv[jss::Amount][jss::mpt_issuance_id] =
                "00000004A407AF5856CCF3C42619DAA925813FC955C72983";
            jv[jss::Amount][jss::value] = "-1";

            auto const result = withMPT ? ter(temBAD_AMOUNT) : ter(temDISABLED);
            env(jv,
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                result);
            env.close();
        }

        // temBAD_AMOUNT: amount < 0
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(alice, bob, MPT(-1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
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
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(gw, alice, MPT(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // tecOBJECT_NOT_FOUND: mpt does not exist
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), alice, bob, gw);
            env.close();

            auto const mpt = ripple::test::jtx::MPT(
                alice.name(), makeMptID(env.seq(alice), alice));
            Json::Value jv = escrow::create(alice, bob, mpt(2));
            jv[jss::Amount][jss::mpt_issuance_id] =
                "00000004A407AF5856CCF3C42619DAA925813FC955C72983";
            env(jv,
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecOBJECT_NOT_FOUND));
            env.close();
        }

        // tecNO_PERMISSION: tfMPTCanEscrow is not enabled
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(alice, bob, MPT(3)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecNO_PERMISSION));
            env.close();
        }

        // tecOBJECT_NOT_FOUND: account does not have the mpt
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account("gw");

            MPTTester mptGw(env, gw, {.holders = {alice, bob}});
            mptGw.create(
                {.ownerCount = 1,
                 .holderCount = 0,
                 .flags = tfMPTCanEscrow | tfMPTCanTransfer});
            auto const MPT = mptGw["MPT"];

            env(escrow::create(alice, bob, MPT(4)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecOBJECT_NOT_FOUND));
            env.close();
        }

        // tecNO_AUTH: requireAuth set: account not authorized
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(alice, bob, MPT(5)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecNO_AUTH: requireAuth set: dest not authorized
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(alice, bob, MPT(6)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecLOCKED: issuer has locked the account
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(alice, bob, MPT(7)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecLOCKED));
            env.close();
        }

        // tecLOCKED: issuer has locked the dest
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(alice, bob, MPT(8)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecLOCKED));
            env.close();
        }

        // tecNO_AUTH: mpt cannot be transferred
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(alice, bob, MPT(9)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: spendable amount is zero
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(alice, bob, MPT(11)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }

        // tecINSUFFICIENT_FUNDS: spendable amount is less than the amount
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            env(escrow::create(alice, bob, MPT(11)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }
    }

    void
    testMPTFinishPreclaim(FeatureBitset features)
    {
        testcase("MPT Finish Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_AUTH: requireAuth set: dest not authorized
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            // unauthorize dest
            mptGw.authorize(
                {.account = gw, .holder = bob, .flags = tfMPTUnauthorize});

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tecNO_AUTH));
            env.close();
        }

        // tecOBJECT_NOT_FOUND: MPT issuance does not exist
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(10'000), alice, bob);
            env.close();

            auto const seq1 = env.seq(alice);
            env.app().openLedger().modify(
                [&](OpenView& view, beast::Journal j) {
                    Sandbox sb(&view, tapNONE);
                    auto sleNew =
                        std::make_shared<SLE>(keylet::escrow(alice, seq1));
                    MPTIssue const mpt{
                        MPTIssue{makeMptID(1, AccountID(0x4985601))}};
                    STAmount amt(mpt, 10);
                    sleNew->setAccountID(sfDestination, bob);
                    sleNew->setFieldAmount(sfAmount, amt);
                    sb.insert(sleNew);
                    sb.apply(view);
                    return true;
                });

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tecOBJECT_NOT_FOUND));
            env.close();
        }

        // tecLOCKED: issuer has locked the dest
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(8)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            // lock dest
            mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tecLOCKED));
            env.close();
        }
    }

    void
    testMPTFinishDoApply(FeatureBitset features)
    {
        testcase("MPT Finish Do Apply");
        using namespace test::jtx;
        using namespace std::literals;

        // tecINSUFFICIENT_RESERVE: insufficient reserve to create MPT
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            auto const acctReserve = env.current()->fees().reserve;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tecINSUFFICIENT_RESERVE));
            env.close();
        }

        // tesSUCCESS: bob submits; finish MPT created
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();
        }

        // tecNO_PERMISSION: carol submits; finish MPT not created
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account("gw");
            env.fund(XRP(10'000), bob, carol);
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            env(escrow::finish(carol, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tecNO_PERMISSION));
            env.close();
        }
    }

    void
    testMPTCancelPreclaim(FeatureBitset features)
    {
        testcase("MPT Cancel Preclaim");
        using namespace test::jtx;
        using namespace std::literals;

        // tecNO_AUTH: requireAuth set: account not authorized
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::cancel_time(env.now() + 2s),
                escrow::condition(escrow::cb1),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            // unauthorize account
            mptGw.authorize(
                {.account = gw, .holder = alice, .flags = tfMPTUnauthorize});

            env(escrow::cancel(bob, alice, seq1), ter(tecNO_AUTH));
            env.close();
        }

        // tecOBJECT_NOT_FOUND: MPT issuance does not exist
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            env.fund(XRP(10'000), alice, bob);

            auto const seq1 = env.seq(alice);
            env.app().openLedger().modify(
                [&](OpenView& view, beast::Journal j) {
                    Sandbox sb(&view, tapNONE);
                    auto sleNew =
                        std::make_shared<SLE>(keylet::escrow(alice, seq1));
                    MPTIssue const mpt{
                        MPTIssue{makeMptID(1, AccountID(0x4985601))}};
                    STAmount amt(mpt, 10);
                    sleNew->setAccountID(sfDestination, bob);
                    sleNew->setFieldAmount(sfAmount, amt);
                    sb.insert(sleNew);
                    sb.apply(view);
                    return true;
                });

            env(escrow::cancel(bob, alice, seq1),
                fee(baseFee),
                ter(tecOBJECT_NOT_FOUND));
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
        auto const baseFee = env.current()->fees().base;
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

        // Create & Finish Escrow
        auto const seq1 = env.seq(alice);
        {
            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preBobMPT = env.balance(bob, MPT);
            env(escrow::create(alice, bob, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
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
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT + MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 0);
        }

        // Create & Cancel Escrow
        auto const seq2 = env.seq(alice);
        {
            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preBobMPT = env.balance(bob, MPT);
            env(escrow::create(alice, bob, MPT(1'000)),
                escrow::condition(escrow::cb2),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s),
                fee(baseFee * 150),
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
            env(escrow::cancel(bob, alice, seq2), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT + MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 0);
        }

        // Self Escrow Create & Finish
        {
            auto const seq = env.seq(alice);
            auto const preAliceMPT = env.balance(alice, MPT);
            env(escrow::create(alice, alice, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 1'000);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 1'000);

            env(escrow::finish(alice, alice, seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 0);
        }

        // Self Escrow Create & Cancel
        {
            auto const seq = env.seq(alice);
            auto const preAliceMPT = env.balance(alice, MPT);
            env(escrow::create(alice, alice, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 1'000);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 1'000);

            env(escrow::cancel(alice, alice, seq), ter(tesSUCCESS));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 0);
        }

        // Multiple Escrows
        {
            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preBobMPT = env.balance(bob, MPT);
            auto const preCarolMPT = env.balance(carol, MPT);
            env(escrow::create(alice, bob, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            env(escrow::create(carol, bob, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
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

        // Max MPT Amount Issued (Escrow 1 MPT)
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(1)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 1);
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 1);

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
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

        // Max MPT Amount Issued (Escrow Max MPT)
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            // Escrow Max MPT - 10
            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(maxMPTokenAmount - 10)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            // Escrow 10 MPT
            auto const seq2 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            BEAST_EXPECT(
                env.balance(alice, MPT) == preAliceMPT - MPT(maxMPTokenAmount));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == maxMPTokenAmount);
            BEAST_EXPECT(env.balance(bob, MPT) == preBobMPT);
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == outstandingMPT);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == maxMPTokenAmount);

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            env(escrow::finish(bob, alice, seq2),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
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
            testcase("MPT Metadata to self");

            Env env{*this, features};
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
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bob);

            env(escrow::create(alice, alice, MPT(1'000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 500s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            auto const aa = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(aa);
            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), aa) != aod.end());
            }

            {
                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 1);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), aa) == iod.end());
            }

            env(escrow::create(bob, bob, MPT(1'000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            auto const bb = env.le(keylet::escrow(bob.id(), bseq));
            BEAST_EXPECT(bb);

            {
                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) != bod.end());
            }

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(
                    (*env.meta())[sfTransactionResult] ==
                    static_cast<std::uint8_t>(tesSUCCESS));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), aa) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) != bod.end());
            }

            env.close(5s);
            env(escrow::cancel(bob, bob, bseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(bob.id(), bseq)));
                BEAST_EXPECT(
                    (*env.meta())[sfTransactionResult] ==
                    static_cast<std::uint8_t>(tesSUCCESS));

                ripple::Dir bod(*env.current(), keylet::ownerDir(bob.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) == bod.end());
            }
        }

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

            env(escrow::create(alice, bob, MPT(1'000)),
                escrow::finish_time(env.now() + 1s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow::create(bob, carol, MPT(1'000)),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);

            auto const ab = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(ab);

            auto const bc = env.le(keylet::escrow(bob.id(), bseq));
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

            env.close(5s);
            env(escrow::finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(env.le(keylet::escrow(bob.id(), bseq)));

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

            env.close(5s);
            env(escrow::cancel(bob, bob, bseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(!env.le(keylet::escrow(bob.id(), bseq)));

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

        // issuer is dest; alice w/ authorization
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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
            auto const seq1 = env.seq(alice);
            auto const preAliceMPT = env.balance(alice, MPT);
            auto const preOutstanding = env.balance(gw, MPT);
            auto const preEscrowed = issuerMPTEscrowed(env, MPT);
            BEAST_EXPECT(preOutstanding == MPT(-10'000));
            BEAST_EXPECT(preEscrowed == 0);

            env(escrow::create(alice, gw, MPT(1'000)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 1'000);
            BEAST_EXPECT(env.balance(gw, MPT) == preOutstanding);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == preEscrowed + 1'000);

            // issuer (dest) can finish escrow
            env(escrow::finish(gw, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAliceMPT - MPT(1'000));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == preOutstanding + MPT(1'000));
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == preEscrowed);
        }
    }

    void
    testMPTLockedRate(FeatureBitset features)
    {
        testcase("MPT Locked Rate");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test locked rate: finish
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, MPT);
            auto const seq1 = env.seq(alice);
            auto const delta = MPT(125);
            env(escrow::create(alice, bob, MPT(125)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();
            auto const transferRate = escrow::rate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 125);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 125);
            BEAST_EXPECT(env.balance(gw, MPT) == MPT(-20'000));

            // bob can finish escrow
            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, MPT) == MPT(10'100));

            auto const escrowedWithFix =
                env.current()->rules().enabled(fixTokenEscrowV1) ? 0 : 25;
            auto const outstandingWithFix =
                env.current()->rules().enabled(fixTokenEscrowV1) ? MPT(19'975)
                                                                 : MPT(20'000);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == escrowedWithFix);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == escrowedWithFix);
            BEAST_EXPECT(env.balance(gw, MPT) == -outstandingWithFix);
        }

        // test locked rate: cancel
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, MPT);
            auto const preBob = env.balance(bob, MPT);
            auto const seq1 = env.seq(alice);
            auto const delta = MPT(125);
            env(escrow::create(alice, bob, MPT(125)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 3s),
                fee(baseFee * 150));
            env.close();
            auto const transferRate = escrow::rate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            // alice can cancel escrow
            env(escrow::cancel(alice, alice, seq1), fee(baseFee));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAlice);
            BEAST_EXPECT(env.balance(bob, MPT) == preBob);
            BEAST_EXPECT(env.balance(gw, MPT) == MPT(-20'000));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 0);
        }

        // test locked rate: issuer is destination
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, MPT);
            auto const seq1 = env.seq(alice);
            auto const delta = MPT(125);
            env(escrow::create(alice, gw, MPT(125)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();
            auto const transferRate = escrow::rate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1'000'000'000 * 1.25));

            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 125);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 125);
            BEAST_EXPECT(env.balance(gw, MPT) == MPT(-20'000));

            // bob can finish escrow
            env(escrow::finish(gw, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
            env.close();

            BEAST_EXPECT(env.balance(alice, MPT) == preAlice - delta);
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 0);
            BEAST_EXPECT(issuerMPTEscrowed(env, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == MPT(-19'875));
        }
    }

    void
    testMPTRequireAuth(FeatureBitset features)
    {
        testcase("MPT Require Auth");
        using namespace test::jtx;
        using namespace std::literals;

        Env env{*this, features};
        auto const baseFee = env.current()->fees().base;
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account("gw");

        MPTTester mptGw(env, gw, {.holders = {alice, bob}});
        mptGw.create(
            {.ownerCount = 1,
             .holderCount = 0,
             .flags = tfMPTCanEscrow | tfMPTCanTransfer | tfMPTRequireAuth});
        mptGw.authorize({.account = alice});
        mptGw.authorize({.account = gw, .holder = alice});
        mptGw.authorize({.account = bob});
        mptGw.authorize({.account = gw, .holder = bob});
        auto const MPT = mptGw["MPT"];
        env(pay(gw, alice, MPT(10'000)));
        env.close();

        auto seq = env.seq(alice);
        auto const delta = MPT(125);
        // alice can create escrow - is authorized
        env(escrow::create(alice, bob, MPT(100)),
            escrow::condition(escrow::cb1),
            escrow::finish_time(env.now() + 1s),
            fee(baseFee * 150));
        env.close();

        // bob can finish escrow - is authorized
        env(escrow::finish(bob, alice, seq),
            escrow::condition(escrow::cb1),
            escrow::fulfillment(escrow::fb1),
            fee(baseFee * 150));
        env.close();
    }

    void
    testMPTLock(FeatureBitset features)
    {
        testcase("MPT Lock");
        using namespace test::jtx;
        using namespace std::literals;

        Env env{*this, features};
        auto const baseFee = env.current()->fees().base;
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

        // alice create escrow
        auto seq1 = env.seq(alice);
        env(escrow::create(alice, bob, MPT(100)),
            escrow::condition(escrow::cb1),
            escrow::finish_time(env.now() + 1s),
            escrow::cancel_time(env.now() + 2s),
            fee(baseFee * 150));
        env.close();

        // lock account & dest
        mptGw.set({.account = gw, .holder = alice, .flags = tfMPTLock});
        mptGw.set({.account = gw, .holder = bob, .flags = tfMPTLock});

        // bob cannot finish
        env(escrow::finish(bob, alice, seq1),
            escrow::condition(escrow::cb1),
            escrow::fulfillment(escrow::fb1),
            fee(baseFee * 150),
            ter(tecLOCKED));
        env.close();

        // bob can cancel
        env(escrow::cancel(bob, alice, seq1));
        env.close();
    }

    void
    testMPTCanTransfer(FeatureBitset features)
    {
        testcase("MPT Can Transfer");
        using namespace test::jtx;
        using namespace std::literals;

        Env env{*this, features};
        auto const baseFee = env.current()->fees().base;
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

        // alice cannot create escrow to non issuer
        env(escrow::create(alice, bob, MPT(100)),
            escrow::condition(escrow::cb1),
            escrow::finish_time(env.now() + 1s),
            escrow::cancel_time(env.now() + 2s),
            fee(baseFee * 150),
            ter(tecNO_AUTH));
        env.close();

        // Escrow Create & Finish
        {
            // alice an create escrow to issuer
            auto seq = env.seq(alice);
            env(escrow::create(alice, gw, MPT(100)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            // gw can finish
            env(escrow::finish(gw, alice, seq),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150));
            env.close();
        }

        // Escrow Create & Cancel
        {
            // alice an create escrow to issuer
            auto seq = env.seq(alice);
            env(escrow::create(alice, gw, MPT(100)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                escrow::cancel_time(env.now() + 2s),
                fee(baseFee * 150));
            env.close();

            // alice can cancel
            env(escrow::cancel(alice, alice, seq));
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
            auto const baseFee = env.current()->fees().base;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();

            env(pay(alice, gw, MPT(10'000)), ter(tecPATH_PARTIAL));
            env(pay(alice, gw, MPT(9'990)));
            env(pay(bob, gw, MPT(10'000)));
            BEAST_EXPECT(env.balance(alice, MPT) == MPT(0));
            BEAST_EXPECT(mptEscrowed(env, alice, MPT) == 10);
            BEAST_EXPECT(env.balance(bob, MPT) == MPT(0));
            BEAST_EXPECT(mptEscrowed(env, bob, MPT) == 0);
            BEAST_EXPECT(env.balance(gw, MPT) == MPT(-10));
            mptGw.authorize({.account = bob, .flags = tfMPTUnauthorize});
            mptGw.destroy(
                {.id = mptGw.issuanceID(),
                 .ownerCount = 1,
                 .err = tecHAS_OBLIGATIONS});

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
                ter(tesSUCCESS));
            env.close();

            env(pay(bob, gw, MPT(10)));
            mptGw.destroy({.id = mptGw.issuanceID(), .ownerCount = 0});
        }

        // tecHAS_OBLIGATIONS: holder cannot destroy mptoken
        {
            Env env{*this, features};
            auto const baseFee = env.current()->fees().base;
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

            auto const seq1 = env.seq(alice);
            env(escrow::create(alice, bob, MPT(10)),
                escrow::condition(escrow::cb1),
                escrow::finish_time(env.now() + 1s),
                fee(baseFee * 150),
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

            env(escrow::finish(bob, alice, seq1),
                escrow::condition(escrow::cb1),
                escrow::fulfillment(escrow::fb1),
                fee(baseFee * 150),
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
        testIOUFinishPreclaim(features);
        testIOUFinishDoApply(features);
        testIOUCancelPreclaim(features);
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
        testMPTFinishPreclaim(features);
        testMPTFinishDoApply(features);
        testMPTCancelPreclaim(features);
        testMPTBalances(features);
        testMPTMetaAndOwnership(features);
        testMPTGateway(features);
        testMPTLockedRate(features);
        testMPTRequireAuth(features);
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
        testMPTWithFeats(all - fixTokenEscrowV1);
    }
};

BEAST_DEFINE_TESTSUITE(EscrowToken, app, ripple);

}  // namespace test
}  // namespace ripple
