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

#include <ripple/protocol/Feature.h>
#include <ripple/protocol/jss.h>
#include <test/jtx.h>

namespace ripple {

class AccountDelete_test : public beast::unit_test::suite
{
private:
    std::uint32_t openLedgerSeq (test::jtx::Env& env)
    {
        return env.current()->seq();
    }

public:

    void testBasics()
    {
        using namespace test::jtx;

        testcase("Basics");

        Env env {*this, supported_amendments()};
        Account const alice("alice");
        Account const becky("becky");
        Account const carol("carol");
        Account const gw("gw");

        env.fund(XRP(10000), alice, becky, carol, gw);
        env.close();

        // Alice can't delete her account and then give herself the XRP.
        env (acctdelete (alice, alice), ter (temDST_IS_SRC));

        // Invalid flags.
        env (acctdelete (alice, becky),
            txflags (tfImmediateOrCancel), ter (temINVALID_FLAG));

        // alice's account is created too recently to be deleted.
        env (acctdelete (alice, becky), ter (tecTOO_SOON));

        // Give becky a trustline.  She is no longer deletable.
        env (trust (becky, gw["USD"](1000)));
        env.close();

        // Give carol a deposit preauthorization, an offer, and a signer list.
        // Even with all that she's still deletable.
        env (deposit::auth (carol, becky));
        std::uint32_t const carolOfferSeq  {env.seq (carol)};
        env (offer (carol, gw["USD"](51), XRP (51)));
        env (signers (carol, 1, {{alice, 1}, {becky, 1}}));

        // Deleting should fail with TOO_SOON, which is a relatively 
        // cheap check compared to validating the contents of her directory.
        env (acctdelete (alice, becky), ter (tecTOO_SOON));

        // Close enough ledgers to almost be able to delete alice's account.
        std::uint32_t const ledgerCount {
            openLedgerSeq (env) + 256 - env.seq (alice)};

        for (std::uint32_t i = 0; i < ledgerCount; ++i)
            env.close();

        // alice's account is still created too recently to be deleted.
        env (acctdelete (alice, becky), ter (tecTOO_SOON));

        // The most recent delete attempt advanced alice's sequence.  So
        // close two ledgers and her account should be deletable.
        env.close();
        env.close();

        auto const fee = env.current ()->fees ().base;
        {
            auto const aliceOldBalance {env.balance (alice)};
            auto const beckyOldBalance {env.balance (becky)};

            // Verify that alice's account exists but she has no directory.
            BEAST_EXPECT (env.closed()->exists(keylet::account (alice.id())));
            BEAST_EXPECT (! env.closed()->exists(keylet::ownerDir(alice.id())));

            env (acctdelete (alice, becky));
            env.close();

            // Verify that alice's account and directory are actually gone.
            BEAST_EXPECT (! env.closed()->exists(keylet::account (alice.id())));
            BEAST_EXPECT (! env.closed()->exists(keylet::ownerDir(alice.id())));

            // Verify that alice's XRP, minus the fee, was transferred to becky.
            BEAST_EXPECT (
                env.balance (becky) == aliceOldBalance + beckyOldBalance - fee);
        }

        // Attempt to delete becky's account but get stopped by the trust line.
        env (acctdelete (becky, carol), ter (tecHAS_OBLIGATIONS));
        env.close();

        // Verify that becky's account is still there.
        env (noop (becky));

        {
            auto const beckyOldBalance {env.balance (becky)};
            auto const carolOldBalance {env.balance (carol)};

            // Verify that Carol's account, directory, deposit
            // preauthorization, offer, and signer list exist.
            BEAST_EXPECT (env.closed()->exists (keylet::account (carol.id())));
            BEAST_EXPECT (env.closed()->exists (keylet::ownerDir(carol.id())));
            BEAST_EXPECT (env.closed()->exists (keylet::depositPreauth(
                carol.id(), becky.id())));
            BEAST_EXPECT (env.closed()->exists (keylet::offer(
                carol.id(), carolOfferSeq)));
            BEAST_EXPECT (env.closed()->exists (keylet::signers (carol.id())));

            // Delete carol's account even with stuff in her directory.
            env (acctdelete (carol, becky));
            env.close();

            // Verify that Carol's account, directory, and other stuff are gone.
            BEAST_EXPECT (! env.closed()->exists(keylet::account (carol.id())));
            BEAST_EXPECT (! env.closed()->exists(keylet::ownerDir(carol.id())));
            BEAST_EXPECT (! env.closed()->exists(keylet::depositPreauth (
                carol.id(), becky.id())));
            BEAST_EXPECT (! env.closed()->exists(keylet::offer (
                carol.id(), carolOfferSeq)));
            BEAST_EXPECT (! env.closed()->exists(keylet::signers (carol.id())));

            // Verify that Carol's XRP, minus the fee, was transferred to becky.
            BEAST_EXPECT (
                env.balance (becky) == carolOldBalance + beckyOldBalance - fee);
        }
    }

    void run() override
    {
        testBasics();
//        testOwnedTypes();      Combinations of owned types
//        testResurrection();    Test a resurrected account
//        testAmendmentEnable(); Account behavior before and after amendment
    }

};

BEAST_DEFINE_TESTSUITE(AccountDelete,app,ripple);

}

