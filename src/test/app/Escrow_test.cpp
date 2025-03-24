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

#include <test/jtx.h>

#include <xrpld/app/tx/applySteps.h>
#include <xrpld/ledger/Dir.h>

#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <iterator>

namespace ripple {
namespace test {

struct Escrow_test : public beast::unit_test::suite
{
    // A PreimageSha256 fulfillments and its associated condition.
    std::array<std::uint8_t, 4> const fb1 = {{0xA0, 0x02, 0x80, 0x00}};

    std::array<std::uint8_t, 39> const cb1 = {
        {0xA0, 0x25, 0x80, 0x20, 0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC,
         0x1C, 0x14, 0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24,
         0x27, 0xAE, 0x41, 0xE4, 0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95,
         0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55, 0x81, 0x01, 0x00}};

    // Another PreimageSha256 fulfillments and its associated condition.
    std::array<std::uint8_t, 7> const fb2 = {
        {0xA0, 0x05, 0x80, 0x03, 0x61, 0x61, 0x61}};

    std::array<std::uint8_t, 39> const cb2 = {
        {0xA0, 0x25, 0x80, 0x20, 0x98, 0x34, 0x87, 0x6D, 0xCF, 0xB0,
         0x5C, 0xB1, 0x67, 0xA5, 0xC2, 0x49, 0x53, 0xEB, 0xA5, 0x8C,
         0x4A, 0xC8, 0x9B, 0x1A, 0xDF, 0x57, 0xF2, 0x8F, 0x2F, 0x9D,
         0x09, 0xAF, 0x10, 0x7E, 0xE8, 0xF0, 0x81, 0x01, 0x03}};

    // Another PreimageSha256 fulfillment and its associated condition.
    std::array<std::uint8_t, 8> const fb3 = {
        {0xA0, 0x06, 0x80, 0x04, 0x6E, 0x69, 0x6B, 0x62}};

    std::array<std::uint8_t, 39> const cb3 = {
        {0xA0, 0x25, 0x80, 0x20, 0x6E, 0x4C, 0x71, 0x45, 0x30, 0xC0,
         0xA4, 0x26, 0x8B, 0x3F, 0xA6, 0x3B, 0x1B, 0x60, 0x6F, 0x2D,
         0x26, 0x4A, 0x2D, 0x85, 0x7B, 0xE8, 0xA0, 0x9C, 0x1D, 0xFD,
         0x57, 0x0D, 0x15, 0x85, 0x8B, 0xD4, 0x81, 0x01, 0x04}};

    void
    testEnablement()
    {
        testcase("Enablement");

        using namespace jtx;
        using namespace std::chrono;

        Env env(*this);
        env.fund(XRP(5000), "alice", "bob");
        env(escrow("alice", "bob", XRP(1000)), finish_time(env.now() + 1s));
        env.close();

        auto const seq1 = env.seq("alice");

        env(escrow("alice", "bob", XRP(1000)),
            condition(cb1),
            finish_time(env.now() + 1s),
            fee(1500));
        env.close();
        env(finish("bob", "alice", seq1),
            condition(cb1),
            fulfillment(fb1),
            fee(1500));

        auto const seq2 = env.seq("alice");

        env(escrow("alice", "bob", XRP(1000)),
            condition(cb2),
            finish_time(env.now() + 1s),
            cancel_time(env.now() + 2s),
            fee(1500));
        env.close();
        env(cancel("bob", "alice", seq2), fee(1500));
    }

    void
    testTiming()
    {
        using namespace jtx;
        using namespace std::chrono;

        {
            testcase("Timing: Finish Only");
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob");
            env.close();

            // We create an escrow that can be finished in the future
            auto const ts = env.now() + 97s;

            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", XRP(1000)), finish_time(ts));

            // Advance the ledger, verifying that the finish won't complete
            // prematurely.
            for (; env.now() < ts; env.close())
                env(finish("bob", "alice", seq),
                    fee(1500),
                    ter(tecNO_PERMISSION));

            env(finish("bob", "alice", seq), fee(1500));
        }

        {
            testcase("Timing: Cancel Only");
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob");
            env.close();

            // We create an escrow that can be cancelled in the future
            auto const ts = env.now() + 117s;

            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", XRP(1000)),
                condition(cb1),
                cancel_time(ts));

            // Advance the ledger, verifying that the cancel won't complete
            // prematurely.
            for (; env.now() < ts; env.close())
                env(cancel("bob", "alice", seq),
                    fee(1500),
                    ter(tecNO_PERMISSION));

            // Verify that a finish won't work anymore.
            env(finish("bob", "alice", seq),
                condition(cb1),
                fulfillment(fb1),
                fee(1500),
                ter(tecNO_PERMISSION));

            // Verify that the cancel will succeed
            env(cancel("bob", "alice", seq), fee(1500));
        }

        {
            testcase("Timing: Finish and Cancel -> Finish");
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob");
            env.close();

            // We create an escrow that can be cancelled in the future
            auto const fts = env.now() + 117s;
            auto const cts = env.now() + 192s;

            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", XRP(1000)),
                finish_time(fts),
                cancel_time(cts));

            // Advance the ledger, verifying that the finish and cancel won't
            // complete prematurely.
            for (; env.now() < fts; env.close())
            {
                env(finish("bob", "alice", seq),
                    fee(1500),
                    ter(tecNO_PERMISSION));
                env(cancel("bob", "alice", seq),
                    fee(1500),
                    ter(tecNO_PERMISSION));
            }

            // Verify that a cancel still won't work
            env(cancel("bob", "alice", seq), fee(1500), ter(tecNO_PERMISSION));

            // And verify that a finish will
            env(finish("bob", "alice", seq), fee(1500));
        }

        {
            testcase("Timing: Finish and Cancel -> Cancel");
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob");
            env.close();

            // We create an escrow that can be cancelled in the future
            auto const fts = env.now() + 109s;
            auto const cts = env.now() + 184s;

            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", XRP(1000)),
                finish_time(fts),
                cancel_time(cts));

            // Advance the ledger, verifying that the finish and cancel won't
            // complete prematurely.
            for (; env.now() < fts; env.close())
            {
                env(finish("bob", "alice", seq),
                    fee(1500),
                    ter(tecNO_PERMISSION));
                env(cancel("bob", "alice", seq),
                    fee(1500),
                    ter(tecNO_PERMISSION));
            }

            // Continue advancing, verifying that the cancel won't complete
            // prematurely. At this point a finish would succeed.
            for (; env.now() < cts; env.close())
                env(cancel("bob", "alice", seq),
                    fee(1500),
                    ter(tecNO_PERMISSION));

            // Verify that finish will no longer work, since we are past the
            // cancel activation time.
            env(finish("bob", "alice", seq), fee(1500), ter(tecNO_PERMISSION));

            // And verify that a cancel will succeed.
            env(cancel("bob", "alice", seq), fee(1500));
        }
    }

    void
    testTags()
    {
        testcase("Tags");

        using namespace jtx;
        using namespace std::chrono;

        Env env(*this);

        auto const alice = Account("alice");
        auto const bob = Account("bob");

        env.fund(XRP(5000), alice, bob);

        // Check to make sure that we correctly detect if tags are really
        // required:
        env(fset(bob, asfRequireDest));
        env(escrow(alice, bob, XRP(1000)),
            finish_time(env.now() + 1s),
            ter(tecDST_TAG_NEEDED));

        // set source and dest tags
        auto const seq = env.seq(alice);

        env(escrow(alice, bob, XRP(1000)),
            finish_time(env.now() + 1s),
            stag(1),
            dtag(2));

        auto const sle = env.le(keylet::escrow(alice.id(), seq));
        BEAST_EXPECT(sle);
        BEAST_EXPECT((*sle)[sfSourceTag] == 1);
        BEAST_EXPECT((*sle)[sfDestinationTag] == 2);
    }

    void
    testDisallowXRP()
    {
        testcase("Disallow XRP");

        using namespace jtx;
        using namespace std::chrono;

        {
            // Respect the "asfDisallowXRP" account flag:
            Env env(*this, supported_amendments() - featureDepositAuth);

            env.fund(XRP(5000), "bob", "george");
            env(fset("george", asfDisallowXRP));
            env(escrow("bob", "george", XRP(10)),
                finish_time(env.now() + 1s),
                ter(tecNO_TARGET));
        }
        {
            // Ignore the "asfDisallowXRP" account flag, which we should
            // have been doing before.
            Env env(*this);

            env.fund(XRP(5000), "bob", "george");
            env(fset("george", asfDisallowXRP));
            env(escrow("bob", "george", XRP(10)), finish_time(env.now() + 1s));
        }
    }

    void
    test1571()
    {
        using namespace jtx;
        using namespace std::chrono;

        {
            testcase("Implied Finish Time (without fix1571)");

            Env env(*this, supported_amendments() - fix1571);
            env.fund(XRP(5000), "alice", "bob", "carol");
            env.close();

            // Creating an escrow without a finish time and finishing it
            // is allowed without fix1571:
            auto const seq1 = env.seq("alice");
            env(escrow("alice", "bob", XRP(100)),
                cancel_time(env.now() + 1s),
                fee(1500));
            env.close();
            env(finish("carol", "alice", seq1), fee(1500));
            BEAST_EXPECT(env.balance("bob") == XRP(5100));

            env.close();

            // Creating an escrow without a finish time and a condition is
            // also allowed without fix1571:
            auto const seq2 = env.seq("alice");
            env(escrow("alice", "bob", XRP(100)),
                cancel_time(env.now() + 1s),
                condition(cb1),
                fee(1500));
            env.close();
            env(finish("carol", "alice", seq2),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            BEAST_EXPECT(env.balance("bob") == XRP(5200));
        }

        {
            testcase("Implied Finish Time (with fix1571)");

            Env env(*this);
            env.fund(XRP(5000), "alice", "bob", "carol");
            env.close();

            // Creating an escrow with only a cancel time is not allowed:
            env(escrow("alice", "bob", XRP(100)),
                cancel_time(env.now() + 90s),
                fee(1500),
                ter(temMALFORMED));

            // Creating an escrow with only a cancel time and a condition is
            // allowed:
            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", XRP(100)),
                cancel_time(env.now() + 90s),
                condition(cb1),
                fee(1500));
            env.close();
            env(finish("carol", "alice", seq),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            BEAST_EXPECT(env.balance("bob") == XRP(5100));
        }
    }

    void
    testFails()
    {
        testcase("Failure Cases");

        using namespace jtx;
        using namespace std::chrono;

        Env env(*this);
        env.fund(XRP(5000), "alice", "bob");
        env.close();

        // Finish time is in the past
        env(escrow("alice", "bob", XRP(1000)),
            finish_time(env.now() - 5s),
            ter(tecNO_PERMISSION));

        // Cancel time is in the past
        env(escrow("alice", "bob", XRP(1000)),
            condition(cb1),
            cancel_time(env.now() - 5s),
            ter(tecNO_PERMISSION));

        // no destination account
        env(escrow("alice", "carol", XRP(1000)),
            finish_time(env.now() + 1s),
            ter(tecNO_DST));

        env.fund(XRP(5000), "carol");

        // Using non-XRP:
        env(escrow("alice", "carol", Account("alice")["USD"](500)),
            finish_time(env.now() + 1s),
            ter(temBAD_AMOUNT));

        // Sending zero or no XRP:
        env(escrow("alice", "carol", XRP(0)),
            finish_time(env.now() + 1s),
            ter(temBAD_AMOUNT));
        env(escrow("alice", "carol", XRP(-1000)),
            finish_time(env.now() + 1s),
            ter(temBAD_AMOUNT));

        // Fail if neither CancelAfter nor FinishAfter are specified:
        env(escrow("alice", "carol", XRP(1)), ter(temBAD_EXPIRATION));

        // Fail if neither a FinishTime nor a condition are attached:
        env(escrow("alice", "carol", XRP(1)),
            cancel_time(env.now() + 1s),
            ter(temMALFORMED));

        // Fail if FinishAfter has already passed:
        env(escrow("alice", "carol", XRP(1)),
            finish_time(env.now() - 1s),
            ter(tecNO_PERMISSION));

        // If both CancelAfter and FinishAfter are set, then CancelAfter must
        // be strictly later than FinishAfter.
        env(escrow("alice", "carol", XRP(1)),
            condition(cb1),
            finish_time(env.now() + 10s),
            cancel_time(env.now() + 10s),
            ter(temBAD_EXPIRATION));

        env(escrow("alice", "carol", XRP(1)),
            condition(cb1),
            finish_time(env.now() + 10s),
            cancel_time(env.now() + 5s),
            ter(temBAD_EXPIRATION));

        // Carol now requires the use of a destination tag
        env(fset("carol", asfRequireDest));

        // missing destination tag
        env(escrow("alice", "carol", XRP(1)),
            condition(cb1),
            cancel_time(env.now() + 1s),
            ter(tecDST_TAG_NEEDED));

        // Success!
        env(escrow("alice", "carol", XRP(1)),
            condition(cb1),
            cancel_time(env.now() + 1s),
            dtag(1));

        {  // Fail if the sender wants to send more than he has:
            auto const accountReserve = drops(env.current()->fees().reserve);
            auto const accountIncrement =
                drops(env.current()->fees().increment);

            env.fund(accountReserve + accountIncrement + XRP(50), "daniel");
            env(escrow("daniel", "bob", XRP(51)),
                finish_time(env.now() + 1s),
                ter(tecUNFUNDED));

            env.fund(accountReserve + accountIncrement + XRP(50), "evan");
            env(escrow("evan", "bob", XRP(50)),
                finish_time(env.now() + 1s),
                ter(tecUNFUNDED));

            env.fund(accountReserve, "frank");
            env(escrow("frank", "bob", XRP(1)),
                finish_time(env.now() + 1s),
                ter(tecINSUFFICIENT_RESERVE));
        }

        {  // Specify incorrect sequence number
            env.fund(XRP(5000), "hannah");
            auto const seq = env.seq("hannah");
            env(escrow("hannah", "hannah", XRP(10)),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            env(finish("hannah", "hannah", seq + 7),
                fee(1500),
                ter(tecNO_TARGET));
        }

        {  // Try to specify a condition for a non-conditional payment
            env.fund(XRP(5000), "ivan");
            auto const seq = env.seq("ivan");

            env(escrow("ivan", "ivan", XRP(10)), finish_time(env.now() + 1s));
            env.close();
            env(finish("ivan", "ivan", seq),
                condition(cb1),
                fulfillment(fb1),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
        }
    }

    void
    testLockup()
    {
        testcase("Lockup");

        using namespace jtx;
        using namespace std::chrono;

        {
            // Unconditional
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(escrow("alice", "alice", XRP(1000)),
                finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(10)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq), ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible
            env(cancel("bob", "alice", seq), ter(tecNO_PERMISSION));

            // Finish should succeed. Verify funds.
            env(finish("bob", "alice", seq));
            env.require(balance("alice", XRP(5000) - drops(10)));
        }
        {
            // Unconditionally pay from Alice to Bob.  Zelda (neither source nor
            // destination) signs all cancels and finishes.  This shows that
            // Escrow will make a payment to Bob with no intervention from Bob.
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob", "zelda");
            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", XRP(1000)), finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(10)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(cancel("zelda", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("zelda", "alice", seq), ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible
            env(cancel("zelda", "alice", seq), ter(tecNO_PERMISSION));

            // Finish should succeed. Verify funds.
            env(finish("zelda", "alice", seq));
            env.close();

            env.require(balance("alice", XRP(4000) - drops(10)));
            env.require(balance("bob", XRP(6000)));
            env.require(balance("zelda", XRP(5000) - drops(40)));
        }
        {
            // Bob sets DepositAuth so only Bob can finish the escrow.
            Env env(*this);

            env.fund(XRP(5000), "alice", "bob", "zelda");
            env(fset("bob", asfDepositAuth));
            env.close();

            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", XRP(1000)), finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(10)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(cancel("zelda", "alice", seq), ter(tecNO_PERMISSION));
            env(cancel("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("zelda", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq), ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible. Finish will only succeed for
            // Bob, because of DepositAuth.
            env(cancel("zelda", "alice", seq), ter(tecNO_PERMISSION));
            env(cancel("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("zelda", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq));
            env.close();

            auto const baseFee = env.current()->fees().base;
            env.require(balance("alice", XRP(4000) - (baseFee * 5)));
            env.require(balance("bob", XRP(6000) - (baseFee * 5)));
            env.require(balance("zelda", XRP(5000) - (baseFee * 4)));
        }
        {
            // Bob sets DepositAuth but preauthorizes Zelda, so Zelda can
            // finish the escrow.
            Env env(*this);

            env.fund(XRP(5000), "alice", "bob", "zelda");
            env(fset("bob", asfDepositAuth));
            env.close();
            env(deposit::auth("bob", "zelda"));
            env.close();

            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", XRP(1000)), finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(10)));
            env.close();

            // DepositPreauth allows Finish to succeed for either Zelda or
            // Bob. But Finish won't succeed for Alice since she is not
            // preauthorized.
            env(finish("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("zelda", "alice", seq));
            env.close();

            auto const baseFee = env.current()->fees().base;
            env.require(balance("alice", XRP(4000) - (baseFee * 2)));
            env.require(balance("bob", XRP(6000) - (baseFee * 2)));
            env.require(balance("zelda", XRP(5000) - (baseFee * 1)));
        }
        {
            // Conditional
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(escrow("alice", "alice", XRP(1000)),
                condition(cb2),
                finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(10)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(cancel("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("alice", "alice", seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500),
                ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500),
                ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible. Finish is possible but
            // requires the fulfillment associated with the escrow.
            env(cancel("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("alice", "alice", seq), ter(tecCRYPTOCONDITION_ERROR));
            env.close();

            env(finish("bob", "alice", seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500));
        }
        {
            // Self-escrowed conditional with DepositAuth.
            Env env(*this);

            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(escrow("alice", "alice", XRP(1000)),
                condition(cb3),
                finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(10)));
            env.close();

            // Finish is now possible but requires the cryptocondition.
            env(finish("bob", "alice", seq), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("alice", "alice", seq), ter(tecCRYPTOCONDITION_ERROR));

            // Enable deposit authorization. After this only Alice can finish
            // the escrow.
            env(fset("alice", asfDepositAuth));
            env.close();

            env(finish("alice", "alice", seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(cb3),
                fulfillment(fb3),
                fee(1500),
                ter(tecNO_PERMISSION));
            env(finish("alice", "alice", seq),
                condition(cb3),
                fulfillment(fb3),
                fee(1500));
        }
        {
            // Self-escrowed conditional with DepositAuth and DepositPreauth.
            Env env(*this);

            env.fund(XRP(5000), "alice", "bob", "zelda");
            auto const seq = env.seq("alice");
            env(escrow("alice", "alice", XRP(1000)),
                condition(cb3),
                finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(10)));
            env.close();

            // Alice preauthorizes Zelda for deposit, even though Alice has not
            // set the lsfDepositAuth flag (yet).
            env(deposit::auth("alice", "zelda"));
            env.close();

            // Finish is now possible but requires the cryptocondition.
            env(finish("alice", "alice", seq), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("zelda", "alice", seq), ter(tecCRYPTOCONDITION_ERROR));

            // Alice enables deposit authorization. After this only Alice or
            // Zelda (because Zelda is preauthorized) can finish the escrow.
            env(fset("alice", asfDepositAuth));
            env.close();

            env(finish("alice", "alice", seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(cb3),
                fulfillment(fb3),
                fee(1500),
                ter(tecNO_PERMISSION));
            env(finish("zelda", "alice", seq),
                condition(cb3),
                fulfillment(fb3),
                fee(1500));
        }
    }

    void
    testEscrowConditions()
    {
        testcase("Escrow with CryptoConditions");

        using namespace jtx;
        using namespace std::chrono;

        {  // Test cryptoconditions
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob", "carol");
            auto const seq = env.seq("alice");
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(escrow("alice", "carol", XRP(1000)),
                condition(cb1),
                cancel_time(env.now() + 1s));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env.require(balance("alice", XRP(4000) - drops(10)));
            env.require(balance("carol", XRP(5000)));
            env(cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish without a fulfillment
            env(finish("bob", "alice", seq), ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with a condition instead of a fulfillment
            env(finish("bob", "alice", seq),
                condition(cb1),
                fulfillment(cb1),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq),
                condition(cb1),
                fulfillment(cb2),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq),
                condition(cb1),
                fulfillment(cb3),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with an incorrect condition and various
            // combinations of correct and incorrect fulfillments.
            env(finish("bob", "alice", seq),
                condition(cb2),
                fulfillment(fb1),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq),
                condition(cb2),
                fulfillment(fb3),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with the correct condition & fulfillment
            env(finish("bob", "alice", seq),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));

            // SLE removed on finish
            BEAST_EXPECT(!env.le(keylet::escrow(Account("alice").id(), seq)));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env.require(balance("carol", XRP(6000)));
            env(cancel("bob", "alice", seq), ter(tecNO_TARGET));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(cancel("bob", "carol", 1), ter(tecNO_TARGET));
        }
        {  // Test cancel when condition is present
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob", "carol");
            auto const seq = env.seq("alice");
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(escrow("alice", "carol", XRP(1000)),
                condition(cb2),
                cancel_time(env.now() + 1s));
            env.close();
            env.require(balance("alice", XRP(4000) - drops(10)));
            // balance restored on cancel
            env(cancel("bob", "alice", seq));
            env.require(balance("alice", XRP(5000) - drops(10)));
            // SLE removed on cancel
            BEAST_EXPECT(!env.le(keylet::escrow(Account("alice").id(), seq)));
        }
        {
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob", "carol");
            env.close();
            auto const seq = env.seq("alice");
            env(escrow("alice", "carol", XRP(1000)),
                condition(cb3),
                cancel_time(env.now() + 1s));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            // cancel fails before expiration
            env(cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env.close();
            // finish fails after expiration
            env(finish("bob", "alice", seq),
                condition(cb3),
                fulfillment(fb3),
                fee(1500),
                ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env.require(balance("carol", XRP(5000)));
        }
        {  // Test long & short conditions during creation
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob", "carol");

            std::vector<std::uint8_t> v;
            v.resize(cb1.size() + 2, 0x78);
            std::memcpy(v.data() + 1, cb1.data(), cb1.size());

            auto const p = v.data();
            auto const s = v.size();

            auto const ts = env.now() + 1s;

            // All these are expected to fail, because the
            // condition we pass in is malformed in some way
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{p, s}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{p, s - 1}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{p, s - 2}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{p + 1, s - 1}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{p + 1, s - 3}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{p + 2, s - 2}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{p + 2, s - 3}),
                cancel_time(ts),
                ter(temMALFORMED));

            auto const seq = env.seq("alice");
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{p + 1, s - 2}),
                cancel_time(ts),
                fee(100));
            env(finish("bob", "alice", seq),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.require(balance("alice", XRP(4000) - drops(100)));
            env.require(balance("bob", XRP(5000) - drops(1500)));
            env.require(balance("carol", XRP(6000)));
        }
        {  // Test long and short conditions & fulfillments during finish
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob", "carol");

            std::vector<std::uint8_t> cv;
            cv.resize(cb2.size() + 2, 0x78);
            std::memcpy(cv.data() + 1, cb2.data(), cb2.size());

            auto const cp = cv.data();
            auto const cs = cv.size();

            std::vector<std::uint8_t> fv;
            fv.resize(fb2.size() + 2, 0x13);
            std::memcpy(fv.data() + 1, fb2.data(), fb2.size());

            auto const fp = fv.data();
            auto const fs = fv.size();

            auto const ts = env.now() + 1s;

            // All these are expected to fail, because the
            // condition we pass in is malformed in some way
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{cp, cs}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{cp, cs - 1}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{cp, cs - 2}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{cp + 1, cs - 1}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{cp + 1, cs - 3}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{cp + 2, cs - 2}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{cp + 2, cs - 3}),
                cancel_time(ts),
                ter(temMALFORMED));

            auto const seq = env.seq("alice");
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{cp + 1, cs - 2}),
                cancel_time(ts),
                fee(100));

            // Now, try to fulfill using the same sequence of
            // malformed conditions.
            env(finish("bob", "alice", seq),
                condition(Slice{cp, cs}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp, cs - 1}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp, cs - 2}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 1}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 3}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 2, cs - 2}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 2, cs - 3}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));

            // Now, using the correct condition, try malformed fulfillments:
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp, fs - 1}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp, fs - 2}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 1, fs - 1}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 1, fs - 3}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 1, fs - 3}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 2, fs - 2}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 2, fs - 3}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));

            // Now try for the right one
            env(finish("bob", "alice", seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500));
            env.require(balance("alice", XRP(4000) - drops(100)));
            env.require(balance("carol", XRP(6000)));
        }
        {  // Test empty condition during creation and
           // empty condition & fulfillment during finish
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob", "carol");

            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{}),
                cancel_time(env.now() + 1s),
                ter(temMALFORMED));

            auto const seq = env.seq("alice");
            env(escrow("alice", "carol", XRP(1000)),
                condition(cb3),
                cancel_time(env.now() + 1s));

            env(finish("bob", "alice", seq),
                condition(Slice{}),
                fulfillment(Slice{}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(cb3),
                fulfillment(Slice{}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{}),
                fulfillment(fb3),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));

            // Assemble finish that is missing the Condition or the Fulfillment
            // since either both must be present, or neither can:
            env(finish("bob", "alice", seq), condition(cb3), ter(temMALFORMED));
            env(finish("bob", "alice", seq),
                fulfillment(fb3),
                ter(temMALFORMED));

            // Now finish it.
            env(finish("bob", "alice", seq),
                condition(cb3),
                fulfillment(fb3),
                fee(1500));
            env.require(balance("carol", XRP(6000)));
            env.require(balance("alice", XRP(4000) - drops(10)));
        }
        {  // Test a condition other than PreimageSha256, which
           // would require a separate amendment
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob");

            std::array<std::uint8_t, 45> cb = {
                {0xA2, 0x2B, 0x80, 0x20, 0x42, 0x4A, 0x70, 0x49, 0x49,
                 0x52, 0x92, 0x67, 0xB6, 0x21, 0xB3, 0xD7, 0x91, 0x19,
                 0xD7, 0x29, 0xB2, 0x38, 0x2C, 0xED, 0x8B, 0x29, 0x6C,
                 0x3C, 0x02, 0x8F, 0xA9, 0x7D, 0x35, 0x0F, 0x6D, 0x07,
                 0x81, 0x03, 0x06, 0x34, 0xD2, 0x82, 0x02, 0x03, 0xC8}};

            // FIXME: this transaction should, eventually, return temDISABLED
            //        instead of temMALFORMED.
            env(escrow("alice", "bob", XRP(1000)),
                condition(cb),
                cancel_time(env.now() + 1s),
                ter(temMALFORMED));
        }
    }

    void
    testMetaAndOwnership()
    {
        using namespace jtx;
        using namespace std::chrono;

        auto const alice = Account("alice");
        auto const bruce = Account("bruce");
        auto const carol = Account("carol");

        {
            testcase("Metadata to self");

            Env env(*this);
            env.fund(XRP(5000), alice, bruce, carol);
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bruce);

            env(escrow(alice, alice, XRP(1000)),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 500s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            auto const aa = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(aa);

            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), aa) != aod.end());
            }

            env(escrow(bruce, bruce, XRP(1000)),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            auto const bb = env.le(keylet::escrow(bruce.id(), bseq));
            BEAST_EXPECT(bb);

            {
                ripple::Dir bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) != bod.end());
            }

            env.close(5s);
            env(finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(
                    (*env.meta())[sfTransactionResult] ==
                    static_cast<std::uint8_t>(tesSUCCESS));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 0);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), aa) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) != bod.end());
            }

            env.close(5s);
            env(cancel(bruce, bruce, bseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(bruce.id(), bseq)));
                BEAST_EXPECT(
                    (*env.meta())[sfTransactionResult] ==
                    static_cast<std::uint8_t>(tesSUCCESS));

                ripple::Dir bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 0);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bb) == bod.end());
            }
        }
        {
            testcase("Metadata to other");

            Env env(*this);
            env.fund(XRP(5000), alice, bruce, carol);
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bruce);

            env(escrow(alice, bruce, XRP(1000)), finish_time(env.now() + 1s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow(bruce, carol, XRP(1000)),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);

            auto const ab = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(ab);

            auto const bc = env.le(keylet::escrow(bruce.id(), bseq));
            BEAST_EXPECT(bc);

            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) != aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 2);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) != bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) != bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);
                BEAST_EXPECT(
                    std::find(cod.begin(), cod.end(), bc) != cod.end());
            }

            env.close(5s);
            env(finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(env.le(keylet::escrow(bruce.id(), bseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 0);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 1);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) == bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) != bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 1);
            }

            env.close(5s);
            env(cancel(bruce, bruce, bseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(!env.le(keylet::escrow(bruce.id(), bseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 0);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ab) == aod.end());

                ripple::Dir bod(*env.current(), keylet::ownerDir(bruce.id()));
                BEAST_EXPECT(std::distance(bod.begin(), bod.end()) == 0);
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), ab) == bod.end());
                BEAST_EXPECT(
                    std::find(bod.begin(), bod.end(), bc) == bod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 0);
            }
        }
    }

    void
    testConsequences()
    {
        testcase("Consequences");

        using namespace jtx;
        using namespace std::chrono;
        Env env(*this);

        env.memoize("alice");
        env.memoize("bob");
        env.memoize("carol");

        {
            auto const jtx = env.jt(
                escrow("alice", "carol", XRP(1000)),
                finish_time(env.now() + 1s),
                seq(1),
                fee(10));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(10));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(1000));
        }

        {
            auto const jtx = env.jt(cancel("bob", "alice", 3), seq(1), fee(10));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(10));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(0));
        }

        {
            auto const jtx = env.jt(finish("bob", "alice", 3), seq(1), fee(10));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(10));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(0));
        }
    }

    void
    testEscrowWithTickets()
    {
        testcase("Escrow with tickets");

        using namespace jtx;
        using namespace std::chrono;
        Account const alice{"alice"};
        Account const bob{"bob"};

        {
            // Create escrow and finish using tickets.
            Env env(*this);
            env.fund(XRP(5000), alice, bob);
            env.close();

            // alice creates a ticket.
            std::uint32_t const aliceTicket{env.seq(alice) + 1};
            env(ticket::create(alice, 1));

            // bob creates a bunch of tickets because he will be burning
            // through them with tec transactions.  Just because we can
            // we'll use them up starting from largest and going smaller.
            constexpr static std::uint32_t bobTicketCount{20};
            env(ticket::create(bob, bobTicketCount));
            env.close();
            std::uint32_t bobTicket{env.seq(bob)};
            env.require(tickets(alice, 1));
            env.require(tickets(bob, bobTicketCount));

            // Note that from here on all transactions use tickets.  No account
            // root sequences should change.
            std::uint32_t const aliceRootSeq{env.seq(alice)};
            std::uint32_t const bobRootSeq{env.seq(bob)};

            // alice creates an escrow that can be finished in the future
            auto const ts = env.now() + 97s;

            std::uint32_t const escrowSeq = aliceTicket;
            env(escrow(alice, bob, XRP(1000)),
                finish_time(ts),
                ticket::use(aliceTicket));
            BEAST_EXPECT(env.seq(alice) == aliceRootSeq);
            env.require(tickets(alice, 0));
            env.require(tickets(bob, bobTicketCount));

            // Advance the ledger, verifying that the finish won't complete
            // prematurely.  Note that each tec consumes one of bob's tickets.
            for (; env.now() < ts; env.close())
            {
                env(finish(bob, alice, escrowSeq),
                    fee(1500),
                    ticket::use(--bobTicket),
                    ter(tecNO_PERMISSION));
                BEAST_EXPECT(env.seq(bob) == bobRootSeq);
            }

            // bob tries to re-use a ticket, which is rejected.
            env(finish(bob, alice, escrowSeq),
                fee(1500),
                ticket::use(bobTicket),
                ter(tefNO_TICKET));

            // bob uses one of his remaining tickets.  Success!
            env(finish(bob, alice, escrowSeq),
                fee(1500),
                ticket::use(--bobTicket));
            env.close();
            BEAST_EXPECT(env.seq(bob) == bobRootSeq);
        }

        {
            // Create escrow and cancel using tickets.
            Env env(*this);
            env.fund(XRP(5000), alice, bob);
            env.close();

            // alice creates a ticket.
            std::uint32_t const aliceTicket{env.seq(alice) + 1};
            env(ticket::create(alice, 1));

            // bob creates a bunch of tickets because he will be burning
            // through them with tec transactions.
            constexpr std::uint32_t bobTicketCount{20};
            std::uint32_t bobTicket{env.seq(bob) + 1};
            env(ticket::create(bob, bobTicketCount));
            env.close();
            env.require(tickets(alice, 1));
            env.require(tickets(bob, bobTicketCount));

            // Note that from here on all transactions use tickets.  No account
            // root sequences should change.
            std::uint32_t const aliceRootSeq{env.seq(alice)};
            std::uint32_t const bobRootSeq{env.seq(bob)};

            // alice creates an escrow that can be finished in the future.
            auto const ts = env.now() + 117s;

            std::uint32_t const escrowSeq = aliceTicket;
            env(escrow(alice, bob, XRP(1000)),
                condition(cb1),
                cancel_time(ts),
                ticket::use(aliceTicket));
            BEAST_EXPECT(env.seq(alice) == aliceRootSeq);
            env.require(tickets(alice, 0));
            env.require(tickets(bob, bobTicketCount));

            // Advance the ledger, verifying that the cancel won't complete
            // prematurely.
            for (; env.now() < ts; env.close())
            {
                env(cancel(bob, alice, escrowSeq),
                    fee(1500),
                    ticket::use(bobTicket++),
                    ter(tecNO_PERMISSION));
                BEAST_EXPECT(env.seq(bob) == bobRootSeq);
            }

            // Verify that a finish won't work anymore.
            env(finish(bob, alice, escrowSeq),
                condition(cb1),
                fulfillment(fb1),
                fee(1500),
                ticket::use(bobTicket++),
                ter(tecNO_PERMISSION));
            BEAST_EXPECT(env.seq(bob) == bobRootSeq);

            // Verify that the cancel succeeds.
            env(cancel(bob, alice, escrowSeq),
                fee(1500),
                ticket::use(bobTicket++));
            env.close();
            BEAST_EXPECT(env.seq(bob) == bobRootSeq);

            // Verify that bob actually consumed his tickets.
            env.require(tickets(bob, env.seq(bob) - bobTicket));
        }
    }

    void
    testCredentials()
    {
        testcase("Test with credentials");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dillon{"dillon"};
        Account const zelda{"zelda"};

        const char credType[] = "abcde";

        {
            // Credentials amendment not enabled
            Env env(*this, supported_amendments() - featureCredentials);
            env.fund(XRP(5000), alice, bob);
            env.close();

            auto const seq = env.seq(alice);
            env(escrow(alice, bob, XRP(1000)), finish_time(env.now() + 1s));
            env.close();

            env(fset(bob, asfDepositAuth));
            env.close();
            env(deposit::auth(bob, alice));
            env.close();

            std::string const credIdx =
                "48004829F915654A81B11C4AB8218D96FED67F209B58328A72314FB6EA288B"
                "E4";
            env(finish(bob, alice, seq),
                credentials::ids({credIdx}),
                ter(temDISABLED));
        }

        {
            Env env(*this);

            env.fund(XRP(5000), alice, bob, carol, dillon, zelda);
            env.close();

            env(credentials::create(carol, zelda, credType));
            env.close();
            auto const jv =
                credentials::ledgerEntry(env, carol, zelda, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            auto const seq = env.seq(alice);
            env(escrow(alice, bob, XRP(1000)), finish_time(env.now() + 50s));
            env.close();

            // Bob require preauthorization
            env(fset(bob, asfDepositAuth));
            env.close();

            // Fail, credentials not accepted
            env(finish(carol, alice, seq),
                credentials::ids({credIdx}),
                ter(tecBAD_CREDENTIALS));

            env.close();

            env(credentials::accept(carol, zelda, credType));
            env.close();

            // Fail, credentials doesn’t belong to root account
            env(finish(dillon, alice, seq),
                credentials::ids({credIdx}),
                ter(tecBAD_CREDENTIALS));

            // Fail, no depositPreauth
            env(finish(carol, alice, seq),
                credentials::ids({credIdx}),
                ter(tecNO_PERMISSION));

            env(deposit::authCredentials(bob, {{zelda, credType}}));
            env.close();

            // Success
            env.close();
            env(finish(carol, alice, seq), credentials::ids({credIdx}));
            env.close();
        }

        {
            testcase("Escrow with credentials without depositPreauth");
            using namespace std::chrono;

            Env env(*this);

            env.fund(XRP(5000), alice, bob, carol, dillon, zelda);
            env.close();

            env(credentials::create(carol, zelda, credType));
            env.close();
            env(credentials::accept(carol, zelda, credType));
            env.close();
            auto const jv =
                credentials::ledgerEntry(env, carol, zelda, credType);
            std::string const credIdx = jv[jss::result][jss::index].asString();

            auto const seq = env.seq(alice);
            env(escrow(alice, bob, XRP(1000)), finish_time(env.now() + 50s));
            // time advance
            env.close();
            env.close();
            env.close();
            env.close();
            env.close();
            env.close();

            // Succeed, Bob doesn't require preauthorization
            env(finish(carol, alice, seq), credentials::ids({credIdx}));
            env.close();

            {
                const char credType2[] = "fghijk";

                env(credentials::create(bob, zelda, credType2));
                env.close();
                env(credentials::accept(bob, zelda, credType2));
                env.close();
                auto const credIdxBob =
                    credentials::ledgerEntry(
                        env, bob, zelda, credType2)[jss::result][jss::index]
                        .asString();

                auto const seq = env.seq(alice);
                env(escrow(alice, bob, XRP(1000)), finish_time(env.now() + 1s));
                env.close();

                // Bob require preauthorization
                env(fset(bob, asfDepositAuth));
                env.close();
                env(deposit::authCredentials(bob, {{zelda, credType}}));
                env.close();

                // Use any valid credentials if account == dst
                env(finish(bob, alice, seq), credentials::ids({credIdxBob}));
                env.close();
            }
        }
    }

    void
    testFinishFunctionPreflight()
    {
        testcase("Test preflight checks involving FinishFunction");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        // Tests whether the ledger index is >= 5
        // #[no_mangle]
        // pub fn ready() -> bool {
        //     unsafe { host_lib::getLedgerSqn() >= 5}
        // }
        static auto wasmHex =
            "0061736d010000000105016000017f02190108686f73745f6c69620c6765744c65"
            "6467657253716e0000030201000405017001010105030100100619037f01418080"
            "c0000b7f00418080c0000b7f00418080c0000b072d04066d656d6f727902000572"
            "6561647900010a5f5f646174615f656e6403010b5f5f686561705f626173650302"
            "0a0d010b0010808080800041044a0b006c046e616d65000e0d7761736d5f6c6962"
            "2e7761736d01410200375f5a4e387761736d5f6c696238686f73745f6c69623132"
            "6765744c656467657253716e313768303033306666356636376562356638314501"
            "057265616479071201000f5f5f737461636b5f706f696e74657200550970726f64"
            "756365727302086c616e6775616765010452757374000c70726f6365737365642d"
            "62790105727573746325312e38332e302d6e696768746c79202863326637346333"
            "663920323032342d30392d30392900490f7461726765745f666561747572657304"
            "2b0a6d756c746976616c75652b0f6d757461626c652d676c6f62616c732b0f7265"
            "666572656e63652d74797065732b087369676e2d657874";

        {
            // featureSmartEscrow disabled
            Env env(*this, supported_amendments() - featureSmartEscrow);
            env.fund(XRP(5000), alice, carol);
            XRPAmount const txnFees = env.current()->fees().base + 1000;
            auto escrowCreate = escrow(alice, carol, XRP(1000));
            env(escrowCreate,
                finish_function(wasmHex),
                cancel_time(env.now() + 100s),
                fee(txnFees),
                ter(temDISABLED));
            env.close();
        }

        Env env(*this);
        XRPAmount const txnFees = env.current()->fees().base + 1000;
        // create escrow
        env.fund(XRP(5000), alice, carol);

        auto escrowCreate = escrow(alice, carol, XRP(1000));

        // Success situations
        {
            // FinishFunction + CancelAfter
            env(escrowCreate,
                finish_function(wasmHex),
                cancel_time(env.now() + 100s),
                fee(txnFees));
            env.close();
        }
        {
            // FinishFunction + Condition + CancelAfter
            env(escrowCreate,
                finish_function(wasmHex),
                cancel_time(env.now() + 100s),
                condition(cb1),
                fee(txnFees));
            env.close();
        }
        {
            // FinishFunction + FinishAfter + CancelAfter
            env(escrowCreate,
                finish_function(wasmHex),
                cancel_time(env.now() + 100s),
                finish_time(env.now() + 2s),
                fee(txnFees));
            env.close();
        }
        {
            // FinishFunction + FinishAfter + Condition + CancelAfter
            env(escrowCreate,
                finish_function(wasmHex),
                cancel_time(env.now() + 100s),
                condition(cb1),
                finish_time(env.now() + 2s),
                fee(txnFees));
            env.close();
        }

        // Failure situations (i.e. all other combinations)
        {
            // only FinishFunction
            env(escrowCreate,
                finish_function(wasmHex),
                fee(txnFees),
                ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // FinishFunction + FinishAfter
            env(escrowCreate,
                finish_function(wasmHex),
                finish_time(env.now() + 2s),
                fee(txnFees),
                ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // FinishFunction + Condition
            env(escrowCreate,
                finish_function(wasmHex),
                condition(cb1),
                fee(txnFees),
                ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // FinishFunction + FinishAfter + Condition
            env(escrowCreate,
                finish_function(wasmHex),
                condition(cb1),
                finish_time(env.now() + 2s),
                fee(txnFees),
                ter(temBAD_EXPIRATION));
            env.close();
        }
        {
            // FinishFunction 0 length
            env(escrowCreate,
                finish_function(""),
                cancel_time(env.now() + 100s),
                fee(txnFees),
                ter(temMALFORMED));
            env.close();
        }
        // {
        //     // FinishFunction > max length
        //     std::string longWasmHex = "00";
        //     // TODO: fix to use the config setting
        //     // TODO: make this test more efficient
        //     // uncomment when that's done
        //     for (int i = 0; i < 4294967295; i++)
        //     {
        //         longWasmHex += "11";
        //     }
        //     env(escrowCreate,
        //         finish_function(longWasmHex),
        //         cancel_time(env.now() + 100s),
        //         fee(txnFees),
        //         ter(temMALFORMED));
        //     env.close();
        // }
    }

    void
    testFinishFunction()
    {
        testcase("PoC escrow function");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const carol{"carol"};

        // Tests whether the ledger index is >= 5
        // #[no_mangle]
        // pub fn ready() -> bool {
        //     unsafe { host_lib::getLedgerSqn() >= 5}
        // }
        static auto wasmHex =
            "0061736d010000000105016000017f02190108686f73745f6c69620c6765744c65"
            "6467657253716e0000030201000405017001010105030100100619037f01418080"
            "c0000b7f00418080c0000b7f00418080c0000b072d04066d656d6f727902000572"
            "6561647900010a5f5f646174615f656e6403010b5f5f686561705f626173650302"
            "0a0d010b0010808080800041044a0b006c046e616d65000e0d7761736d5f6c6962"
            "2e7761736d01410200375f5a4e387761736d5f6c696238686f73745f6c69623132"
            "6765744c656467657253716e313768303033306666356636376562356638314501"
            "057265616479071201000f5f5f737461636b5f706f696e74657200550970726f64"
            "756365727302086c616e6775616765010452757374000c70726f6365737365642d"
            "62790105727573746325312e38332e302d6e696768746c79202863326637346333"
            "663920323032342d30392d30392900490f7461726765745f666561747572657304"
            "2b0a6d756c746976616c75652b0f6d757461626c652d676c6f62616c732b0f7265"
            "666572656e63652d74797065732b087369676e2d657874";

        {
            // basic FinishFunction situation
            Env env(*this);
            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 0);
            auto escrowCreate = escrow(alice, carol, XRP(1000));
            XRPAmount txnFees = env.current()->fees().base + 1000;
            env(escrowCreate,
                finish_function(wasmHex),
                cancel_time(env.now() + 100s),
                fee(txnFees));
            env.close();

            if (BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2))
            {
                env.require(balance(alice, XRP(4000) - txnFees));
                env.require(balance(carol, XRP(5000)));

                env(finish(carol, alice, seq),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));
                env(finish(alice, alice, seq),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));
                env(finish(alice, alice, seq),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));
                env(finish(carol, alice, seq),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));
                env(finish(carol, alice, seq),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));
                env.close();
                env(finish(alice, alice, seq), fee(txnFees), ter(tesSUCCESS));
                env.close();

                BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 0);
            }
        }

        {
            // FinishFunction + Condition
            Env env(*this);
            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 0);
            auto escrowCreate = escrow(alice, carol, XRP(1000));
            XRPAmount txnFees = env.current()->fees().base + 1000;
            env(escrowCreate,
                finish_function(wasmHex),
                condition(cb1),
                cancel_time(env.now() + 100s),
                fee(txnFees));
            env.close();

            if (BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2))
            {
                env.require(balance(alice, XRP(4000) - txnFees));
                env.require(balance(carol, XRP(5000)));

                // no fulfillment provided, function fails
                env(finish(carol, alice, seq),
                    fee(txnFees),
                    ter(tecCRYPTOCONDITION_ERROR));
                // fulfillment provided, function fails
                env(finish(carol, alice, seq),
                    condition(cb1),
                    fulfillment(fb1),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));
                env.close();
                // no fulfillment provided, function succeeds
                env(finish(alice, alice, seq),
                    fee(txnFees),
                    ter(tecCRYPTOCONDITION_ERROR));
                // wrong fulfillment provided, function succeeds
                env(finish(alice, alice, seq),
                    condition(cb1),
                    fulfillment(fb2),
                    fee(txnFees),
                    ter(tecCRYPTOCONDITION_ERROR));
                // fulfillment provided, function succeeds, tx succeeds
                env(finish(alice, alice, seq),
                    condition(cb1),
                    fulfillment(fb1),
                    fee(txnFees),
                    ter(tesSUCCESS));
                env.close();

                BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 0);
            }
        }

        {
            // FinishFunction + FinishAfter
            Env env(*this);
            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 0);
            auto escrowCreate = escrow(alice, carol, XRP(1000));
            XRPAmount txnFees = env.current()->fees().base + 1000;
            auto const ts = env.now() + 97s;
            env(escrowCreate,
                finish_function(wasmHex),
                finish_time(ts),
                cancel_time(env.now() + 1000s),
                fee(txnFees));
            env.close();

            if (BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2))
            {
                env.require(balance(alice, XRP(4000) - txnFees));
                env.require(balance(carol, XRP(5000)));

                // finish time hasn't passed, function fails
                env(finish(carol, alice, seq),
                    fee(txnFees + 1),
                    ter(tecNO_PERMISSION));
                env.close();
                // finish time hasn't passed, function succeeds
                for (; env.now() < ts; env.close())
                    env(finish(carol, alice, seq),
                        fee(txnFees + 2),
                        ter(tecNO_PERMISSION));

                env(finish(carol, alice, seq),
                    fee(txnFees + 1),
                    ter(tesSUCCESS));
                BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 0);
            }
        }

        {
            // FinishFunction + FinishAfter #2
            Env env(*this);
            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 0);
            auto escrowCreate = escrow(alice, carol, XRP(1000));
            XRPAmount txnFees = env.current()->fees().base + 1000;
            env(escrowCreate,
                finish_function(wasmHex),
                finish_time(env.now() + 2s),
                cancel_time(env.now() + 100s),
                fee(txnFees));
            // Don't close the ledger here

            if (BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2))
            {
                env.require(balance(alice, XRP(4000) - txnFees));
                env.require(balance(carol, XRP(5000)));

                // finish time hasn't passed, function fails
                env(finish(carol, alice, seq),
                    fee(txnFees),
                    ter(tecNO_PERMISSION));
                env.close();

                // finish time has passed, function fails
                env(finish(carol, alice, seq),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));
                env.close();
                // finish time has passed, function succeeds, tx succeeds
                env(finish(carol, alice, seq), fee(txnFees), ter(tesSUCCESS));
                env.close();

                BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 0);
            }
        }
    }

    void
    testAllHostFunctions()
    {
        testcase("Test all host functions");

        using namespace jtx;
        using namespace std::chrono;

        // TODO: figure out how to make this a fixture in a separate file
        auto wasmHex =
            "0061736d0100000001690f60037f7f7f017f60027f7f017f60017f0060027f7f00"
            "60057f7f7f7f7f017f6000017f60037e7f7f017f60057f7f7f7f7f0060037f7f7f"
            "0060067f7f7f7f7f7f017f600b7f7f7f7f7f7f7f7f7f7f7f017f60017f017f6004"
            "7f7f7f7f0060000060057f7e7e7e7e00028c010508686f73745f6c696205707269"
            "6e74000308686f73745f6c69620a67657454784669656c64000108686f73745f6c"
            "69621a67657443757272656e744c6564676572456e7472794669656c6400010868"
            "6f73745f6c6962136765744c6564676572456e7472794669656c64000408686f73"
            "745f6c696213676574506172656e744c656467657254696d650005035453020603"
            "070101080901010a01000202010102080008000b0c030101010104050802030303"
            "0d03010204030008010101010d040001010801010b02030d0d0203010101020d0c"
            "0c0001010d030302020c0300000e0405017001212105030100110619037f014180"
            "80c0000b7f0041dca2c0000b7f0041e0a2c0000b074506066d656d6f7279020005"
            "7265616479002308616c6c6f63617465003d0a6465616c6c6f63617465003f0a5f"
            "5f646174615f656e6403010b5f5f686561705f6261736503020926010041010b20"
            "31322b0e091f0d2133343c453b464f54121815101420131e37383944474b4c4d0a"
            "e1ca0153de0101027f23808080800041c0006b2201248080808000200141003602"
            "14200142808080801037020c200141033a00382001412036022820014100360234"
            "2001418080c08000360230200141003602202001410036021820012001410c6a36"
            "022c024020002000411f7522027320026bad2000417f73411f76200141186a1086"
            "808080000d00200128020c21002001280210220220012802141080808080000240"
            "2000450d00200220001087808080000b200141c0006a2480808080000f0b41a880"
            "c0800041372001413f6a419880c0800041ac81c08000108880808000000bec0203"
            "027f017e037f23808080800041306b220324808080800041272104024002402000"
            "4290ce005a0d00200021050c010b412721040340200341096a20046a2206417c6a"
            "20004290ce0080220542f0b1037e20007ca7220741ffff037141e4006e22084101"
            "74419a85c080006a2f00003b00002006417e6a2008419c7f6c20076a41ffff0371"
            "410174419a85c080006a2f00003b00002004417c6a2104200042ffc1d72f562106"
            "2005210020060d000b0b02400240200542e300560d002005a721060c010b200341"
            "096a2004417e6a22046a2005a7220741ffff037141e4006e2206419c7f6c20076a"
            "41ffff0371410174419a85c080006a2f00003b00000b024002402006410a490d00"
            "200341096a2004417e6a22046a2006410174419a85c080006a2f00003b00000c01"
            "0b200341096a2004417f6a22046a20064130723a00000b20022001410141002003"
            "41096a20046a412720046b108c808080002104200341306a24808080800020040b"
            "6c01027f024002402000417c6a2802002202417871220341044108200241037122"
            "021b20016a490d0002402002450d002003200141276a4b0d020b200010a5808080"
            "000f0b41818ec0800041b08ec0800010a680808000000b41c08ec0800041f08ec0"
            "800010a680808000000b8f0101017f23808080800041c0006b2205248080808000"
            "2005200136020c2005200036020820052003360214200520023602102005410236"
            "021c200541b884c08000360218200542023702242005418180808000ad42208620"
            "0541106aad843703382005418280808000ad422086200541086aad843703302005"
            "200541306a360220200541186a200410aa80808000000b9e0301067f2380808080"
            "0041c0006b220224808080800002400240200028020022032d00000d0020012802"
            "14419582c080004104200128021828020c118080808000808080800021000c010b"
            "4101210020012802142204419982c0800041042001280218220528020c22061180"
            "8080800080808080000d00200341016a210302400240200128021c22074104710d"
            "0041012100200441f184c080004101200611808080800080808080000d02200320"
            "01108a80808000450d010c020b200441f284c08000410220061180808080008080"
            "8080000d0141012100200241013a001b200220053602102002200436020c200220"
            "07360238200241c884c08000360234200220012d00203a003c2002200128021036"
            "022c200220012902083702242002200129020037021c20022002411b6a36021420"
            "022002410c6a36023020032002411c6a108a808080000d01200228023041ec84c0"
            "80004102200228023428020c11808080800080808080000d010b2001280214419c"
            "97c080004101200128021828020c118080808000808080800021000b200241c000"
            "6a24808080800020000be90201057f2380808080004180016b2202248080808000"
            "0240024002400240200128021c22034110710d0020034120710d01200031000041"
            "01200110868080800021000c030b20002d0000210041ff00210303402002200322"
            "046a22052000410f712203413072200341d7006a2003410a491b3a00002004417f"
            "6a2103200041ff017122064104762100200641104f0d000c020b0b20002d000021"
            "0041ff00210303402002200322046a22052000410f712203413072200341376a20"
            "03410a491b3a00002004417f6a2103200041ff017122064104762100200641104f"
            "0d000b02402004418101490d002004418001418885c08000108b80808000000b20"
            "014101419885c0800041022005418101200441016a6b108c8080800021000c010b"
            "02402004418101490d002004418001418885c08000108b80808000000b20014101"
            "419885c0800041022005418101200441016a6b108c8080800021000b2002418001"
            "6a24808080800020000b7902017f017e23808080800041306b2203248080808000"
            "20032000360200200320013602042003410236020c200341d887c0800036020820"
            "0342023702142003418380808000ad4220862204200341046aad84370328200320"
            "042003ad843703202003200341206a360210200341086a200210aa80808000000b"
            "cb0501077f0240024020010d00200541016a2106200028021c2107412d21080c01"
            "0b412b418080c400200028021c220741017122011b2108200120056a21060b0240"
            "024020074104710d00410021020c010b0240024020030d00410021090c010b0240"
            "2003410371220a0d000c010b41002109200221010340200920012c000041bf7f4a"
            "6a2109200141016a2101200a417f6a220a0d000b0b200920066a21060b02402000"
            "2802000d000240200028021422012000280218220920082002200310ad80808000"
            "450d0041010f0b200120042005200928020c11808080800080808080000f0b0240"
            "0240024002402000280204220120064b0d00200028021422012000280218220920"
            "082002200310ad80808000450d0141010f0b2007410871450d0120002802102107"
            "2000413036021020002d0020210b4101210c200041013a00202000280214220920"
            "00280218220a20082002200310ad808080000d02200120066b41016a2101024003"
            "402001417f6a2201450d0120094130200a2802101181808080008080808000450d"
            "000b41010f0b0240200920042005200a28020c1180808080008080808000450d00"
            "41010f0b2000200b3a00202000200736021041000f0b200120042005200928020c"
            "1180808080008080808000210c0c010b200120066b210702400240024020002d00"
            "2022010e0402000100020b20072101410021070c010b2007410176210120074101"
            "6a41017621070b200141016a210120002802102106200028021821092000280214"
            "210a024003402001417f6a2201450d01200a200620092802101181808080008080"
            "808000450d000b41010f0b4101210c200a200920082002200310ad808080000d00"
            "200a20042005200928020c11808080800080808080000d00410021010340024020"
            "072001470d0020072007490f0b200141016a2101200a2006200928021011818080"
            "80008080808000450d000b2001417f6a2007490f0b200c0b6601017f2380808080"
            "0041106b220224808080800020022000280200220041046a36020c200141e181c0"
            "8000410941ea81c08000410b200041848080800041f581c0800041092002410c6a"
            "418580808000108f808080002100200241106a24808080800020000be70201057f"
            "2380808080004180016b22022480808080000240024002400240200128021c2203"
            "4110710d0020034120710d0120003502004101200110868080800021000c030b20"
            "00280200210041ff00210303402002200322046a22052000410f71220341307220"
            "0341d7006a2003410a491b3a00002004417f6a2103200041104921062000410476"
            "21002006450d000c020b0b2000280200210041ff00210303402002200322046a22"
            "052000410f712203413072200341376a2003410a491b3a00002004417f6a210320"
            "004110492106200041047621002006450d000b02402004418101490d0020044180"
            "01418885c08000108b80808000000b20014101419885c080004102200541810120"
            "0441016a6b108c8080800021000c010b02402004418101490d0020044180014188"
            "85c08000108b80808000000b20014101419885c080004102200541810120044101"
            "6a6b108c8080800021000b20024180016a24808080800020000bf50101017f2380"
            "8080800041106b220b248080808000200028021420012002200028021828020c11"
            "808080800080808080002102200b41003a000d200b20023a000c200b2000360208"
            "200b41086a200320042005200610b680808000200720082009200a10b680808000"
            "210a200b2d000d2202200b2d000c2201722100024020024101470d002001410171"
            "0d000240200a28020022002d001c4104710d00200028021441ef84c08000410220"
            "0028021828020c118080808000808080800021000c010b200028021441ee84c080"
            "004101200028021828020c118080808000808080800021000b200b41106a248080"
            "80800020004101710b12002000418080c0800020011091808080000bbf05010a7f"
            "23808080800041306b2203248080808000200341033a002c2003412036021c4100"
            "210420034100360228200320013602242003200036022020034100360214200341"
            "0036020c02400240024002400240200228021022050d00200228020c2200450d01"
            "20022802082101200041037421062000417f6a41ffffffff017141016a21042002"
            "280200210003400240200041046a2802002207450d002003280220200028020020"
            "07200328022428020c11808080800080808080000d040b20012802002003410c6a"
            "200128020411818080800080808080000d03200141086a2101200041086a210020"
            "0641786a22060d000c020b0b20022802142201450d00200141057421082001417f"
            "6a41ffffff3f7141016a2104200228020821092002280200210041002106034002"
            "40200041046a2802002201450d0020032802202000280200200120032802242802"
            "0c11808080800080808080000d030b2003200520066a220141106a28020036021c"
            "20032001411c6a2d00003a002c2003200141186a2802003602282001410c6a2802"
            "0021074100210a4100210b024002400240200141086a2802000e03010002010b20"
            "07410374210c4100210b2009200c6a220c2802040d01200c28020021070b410121"
            "0b0b200320073602102003200b36020c200141046a280200210702400240024020"
            "012802000e03010002010b2007410374210b2009200b6a220b2802040d01200b28"
            "020021070b4101210a0b200320073602182003200a3602142009200141146a2802"
            "004103746a22012802002003410c6a200128020411818080800080808080000d02"
            "200041086a21002008200641206a2206470d000b0b200420022802044f0d012003"
            "280220200228020020044103746a22012802002001280204200328022428020c11"
            "80808080008080808000450d010b410121010c010b410021010b200341306a2480"
            "8080800020010b1e01017f024020002802002201450d0020002802042001108780"
            "8080000b0b1e01017f024020002802002201450d00200028020420011087808080"
            "000b0b2200200128021441dc81c080004105200128021828020c11808080800080"
            "808080000be30201027f23808080800041106b2202248080808000024002400240"
            "02402001418001490d002002410036020c2001418010490d010240200141808004"
            "4f0d0020022001413f71418001723a000e20022001410c7641e001723a000c2002"
            "2001410676413f71418001723a000d410321010c030b20022001413f7141800172"
            "3a000f2002200141127641f001723a000c20022001410676413f71418001723a00"
            "0e20022001410c76413f71418001723a000d410421010c020b0240200028020822"
            "032000280200470d0020001096808080000b2000200341016a3602082000280204"
            "20036a20013a00000c020b20022001413f71418001723a000d2002200141067641"
            "c001723a000c410221010b02402000280200200028020822036b20014f0d002000"
            "20032001109780808000200028020821030b200028020420036a2002410c6a2001"
            "10d6808080001a2000200320016a3602080b200241106a24808080800041000b55"
            "01017f23808080800041106b2201248080808000200141086a2000200028020041"
            "01109c80808000024020012802082200418180808078460d002000200128020c10"
            "9d80808000000b200141106a2480808080000b5201017f23808080800041106b22"
            "03248080808000200341086a200020012002109c80808000024020032802082202"
            "418180808078460d002002200328020c109d80808000000b200341106a24808080"
            "80000b4b01017f02402000280200200028020822036b20024f0d00200020032002"
            "109780808000200028020821030b200028020420036a2001200210d6808080001a"
            "2000200320026a36020841000b6f01017f0240024002402002280204450d000240"
            "200228020822030d0041002d00b89ec080001a0c020b200228020020032001109a"
            "8080800021020c020b41002d00b89ec080001a0b2001109b8080800021020b2000"
            "200136020820002002410120021b36020420002002453602000b800601057f0240"
            "024002402000417c6a22032802002204417871220541044108200441037122061b"
            "20016a490d0002402006450d002005200141276a4b0d020b41102002410b6a4178"
            "712002410b491b210102400240024020060d002001418002490d01200520014104"
            "72490d01200520016b418180084f0d010c020b200041786a220720056a21060240"
            "024002400240200520014f0d00200641002802aca2c08000460d03200641002802"
            "a8a2c08000460d02200628020422044102710d042004417871220420056a220520"
            "01490d042006200410a780808000200520016b22024110490d0120032001200328"
            "020041017172410272360200200720016a22012002410372360204200720056a22"
            "0520052802044101723602042001200210a88080800020000f0b200520016b2202"
            "410f4d0d0420032001200441017172410272360200200720016a22052002410372"
            "360204200620062802044101723602042005200210a88080800020000f0b200320"
            "05200328020041017172410272360200200720056a220220022802044101723602"
            "0420000f0b41002802a0a2c0800020056a22052001490d0102400240200520016b"
            "2202410f4b0d0020032004410171200572410272360200200720056a2202200228"
            "020441017236020441002102410021010c010b2003200120044101717241027236"
            "0200200720016a22012002410172360204200720056a2205200236020020052005"
            "280204417e713602040b410020013602a8a2c08000410020023602a0a2c0800020"
            "000f0b41002802a4a2c0800020056a220520014b0d040b02402002109b80808000"
            "22050d0041000f0b20052000417c4178200328020022014103711b20014178716a"
            "2201200220012002491b10d6808080002102200010a580808000200221000b2000"
            "0f0b41818ec0800041b08ec0800010a680808000000b41c08ec0800041f08ec080"
            "0010a680808000000b20032001200441017172410272360200200720016a220220"
            "0520016b2205410172360204410020053602a4a2c08000410020023602aca2c080"
            "0020000bcb2502087f017e02400240024002400240024002400240200041f50149"
            "0d0041002101200041cdff7b4f0d052000410b6a22014178712102410028029ca2"
            "c080002203450d04411f21040240200041f4ffff074b0d00200241062001410876"
            "6722006b7641017120004101746b413e6a21040b410020026b2101024020044102"
            "7441809fc080006a28020022050d0041002100410021060c020b41002100200241"
            "00411920044101766b2004411f461b742107410021060340024020052205280204"
            "41787122082002490d00200820026b220820014f0d00200821012005210620080d"
            "004100210120052106200521000c040b200528021422082000200820052007411d"
            "764104716a41106a2802002205471b200020081b2100200741017421072005450d"
            "020c000b0b02404100280298a2c08000220541102000410b6a41f803712000410b"
            "491b22024103762201762200410371450d00024002402000417f7341017120016a"
            "220741037422004190a0c080006a220120004198a0c080006a2802002202280208"
            "2206460d002006200136020c200120063602080c010b41002005417e2007777136"
            "0298a2c080000b20022000410372360204200220006a2200200028020441017236"
            "0204200241086a0f0b200241002802a0a2c080004d0d0302400240024020000d00"
            "410028029ca2c080002200450d0620006841027441809fc080006a280200220628"
            "020441787120026b21012006210503400240200628021022000d00200628021422"
            "000d0020052802182104024002400240200528020c22002005470d002005411441"
            "10200528021422001b6a28020022060d01410021000c020b200528020822062000"
            "36020c200020063602080c010b200541146a200541106a20001b21070340200721"
            "082006220041146a200041106a200028021422061b210720004114411020061b6a"
            "28020022060d000b200841003602000b2004450d040240200528021c4102744180"
            "9fc080006a22062802002005460d0020044110411420042802102005461b6a2000"
            "3602002000450d050c040b2006200036020020000d034100410028029ca2c08000"
            "417e200528021c777136029ca2c080000c040b200028020441787120026b220620"
            "01200620014922061b21012000200520061b2105200021060c000b0b0240024020"
            "0020017441022001742200410020006b727168220841037422014190a0c080006a"
            "220620014198a0c080006a28020022002802082207460d002007200636020c2006"
            "20073602080c010b41002005417e20087771360298a2c080000b20002002410372"
            "360204200020026a2207200120026b2206410172360204200020016a2006360200"
            "024041002802a0a2c080002205450d0020054178714190a0c080006a2101410028"
            "02a8a2c080002102024002404100280298a2c08000220841012005410376742205"
            "710d0041002008200572360298a2c08000200121050c010b200128020821050b20"
            "0120023602082005200236020c2002200136020c200220053602080b4100200736"
            "02a8a2c08000410020063602a0a2c08000200041086a0f0b200020043602180240"
            "20052802102206450d0020002006360210200620003602180b2005280214220645"
            "0d0020002006360214200620003602180b02400240024020014110490d00200520"
            "02410372360204200520026a22022001410172360204200220016a200136020041"
            "002802a0a2c080002207450d0120074178714190a0c080006a210641002802a8a2"
            "c080002100024002404100280298a2c08000220841012007410376742207710d00"
            "41002008200772360298a2c08000200621070c010b200628020821070b20062000"
            "3602082007200036020c2000200636020c200020073602080c010b200520012002"
            "6a2200410372360204200520006a220020002802044101723602040c010b410020"
            "023602a8a2c08000410020013602a0a2c080000b200541086a0f0b024020002006"
            "720d004100210641022004742200410020006b722003712200450d032000684102"
            "7441809fc080006a28020021000b2000450d010b03402000200620002802044178"
            "71220520026b220820014922041b2103200520024921072008200120041b210802"
            "40200028021022050d00200028021421050b2006200320071b2106200120082007"
            "1b21012005210020050d000b0b2006450d00024041002802a0a2c0800022002002"
            "490d002001200020026b4f0d010b20062802182104024002400240200628020c22"
            "002006470d00200641144110200628021422001b6a28020022050d01410021000c"
            "020b20062802082205200036020c200020053602080c010b200641146a20064110"
            "6a20001b21070340200721082005220041146a200041106a200028021422051b21"
            "0720004114411020051b6a28020022050d000b200841003602000b2004450d0302"
            "40200628021c41027441809fc080006a22052802002006460d0020044110411420"
            "042802102006461b6a20003602002000450d040c030b2005200036020020000d02"
            "4100410028029ca2c08000417e200628021c777136029ca2c080000c030b024002"
            "40024002400240024041002802a0a2c08000220020024f0d00024041002802a4a2"
            "c08000220020024b0d0041002101200241af80046a220641107640002200417f46"
            "22070d0720004110742205450d07410041002802b0a2c08000410020064180807c"
            "7120071b22086a22003602b0a2c08000410041002802b4a2c08000220120002001"
            "20004b1b3602b4a2c0800002400240024041002802aca2c080002201450d004180"
            "a0c080002100034020002802002206200028020422076a2005460d022000280208"
            "22000d000c030b0b0240024041002802bca2c080002200450d00200020054d0d01"
            "0b410020053602bca2c080000b410041ff1f3602c0a2c0800041002008360284a0"
            "c0800041002005360280a0c0800041004190a0c0800036029ca0c0800041004198"
            "a0c080003602a4a0c0800041004190a0c08000360298a0c08000410041a0a0c080"
            "003602aca0c0800041004198a0c080003602a0a0c08000410041a8a0c080003602"
            "b4a0c08000410041a0a0c080003602a8a0c08000410041b0a0c080003602bca0c0"
            "8000410041a8a0c080003602b0a0c08000410041b8a0c080003602c4a0c0800041"
            "0041b0a0c080003602b8a0c08000410041c0a0c080003602cca0c08000410041b8"
            "a0c080003602c0a0c08000410041c8a0c080003602d4a0c08000410041c0a0c080"
            "003602c8a0c080004100410036028ca0c08000410041d0a0c080003602dca0c080"
            "00410041c8a0c080003602d0a0c08000410041d0a0c080003602d8a0c080004100"
            "41d8a0c080003602e4a0c08000410041d8a0c080003602e0a0c08000410041e0a0"
            "c080003602eca0c08000410041e0a0c080003602e8a0c08000410041e8a0c08000"
            "3602f4a0c08000410041e8a0c080003602f0a0c08000410041f0a0c080003602fc"
            "a0c08000410041f0a0c080003602f8a0c08000410041f8a0c08000360284a1c080"
            "00410041f8a0c08000360280a1c0800041004180a1c0800036028ca1c080004100"
            "4180a1c08000360288a1c0800041004188a1c08000360294a1c0800041004188a1"
            "c08000360290a1c0800041004190a1c0800036029ca1c0800041004198a1c08000"
            "3602a4a1c0800041004190a1c08000360298a1c08000410041a0a1c080003602ac"
            "a1c0800041004198a1c080003602a0a1c08000410041a8a1c080003602b4a1c080"
            "00410041a0a1c080003602a8a1c08000410041b0a1c080003602bca1c080004100"
            "41a8a1c080003602b0a1c08000410041b8a1c080003602c4a1c08000410041b0a1"
            "c080003602b8a1c08000410041c0a1c080003602cca1c08000410041b8a1c08000"
            "3602c0a1c08000410041c8a1c080003602d4a1c08000410041c0a1c080003602c8"
            "a1c08000410041d0a1c080003602dca1c08000410041c8a1c080003602d0a1c080"
            "00410041d8a1c080003602e4a1c08000410041d0a1c080003602d8a1c080004100"
            "41e0a1c080003602eca1c08000410041d8a1c080003602e0a1c08000410041e8a1"
            "c080003602f4a1c08000410041e0a1c080003602e8a1c08000410041f0a1c08000"
            "3602fca1c08000410041e8a1c080003602f0a1c08000410041f8a1c08000360284"
            "a2c08000410041f0a1c080003602f8a1c0800041004180a2c0800036028ca2c080"
            "00410041f8a1c08000360280a2c0800041004188a2c08000360294a2c080004100"
            "4180a2c08000360288a2c08000410020053602aca2c0800041004188a2c0800036"
            "0290a2c080004100200841586a22003602a4a2c080002005200041017236020420"
            "0520006a4128360204410041808080013602b8a2c080000c080b200120054f0d00"
            "200620014b0d00200028020c450d030b410041002802bca2c08000220020052000"
            "2005491b3602bca2c08000200520086a21064180a0c08000210002400240024003"
            "40200028020022072006460d01200028020822000d000c020b0b200028020c450d"
            "010b4180a0c0800021000240034002402000280200220620014b0d002001200620"
            "002802046a2206490d020b200028020821000c000b0b410020053602aca2c08000"
            "4100200841586a22003602a4a2c0800020052000410172360204200520006a4128"
            "360204410041808080013602b8a2c080002001200641606a41787141786a220020"
            "00200141106a491b2207411b3602044100290280a0c080002109200741106a4100"
            "290288a0c080003702002007200937020841002008360284a0c080004100200536"
            "0280a0c080004100200741086a360288a0c080004100410036028ca0c080002007"
            "411c6a2100034020004107360200200041046a22002006490d000b20072001460d"
            "0720072007280204417e713602042001200720016b220041017236020420072000"
            "36020002402000418002490d002001200010d0808080000c080b200041f8017141"
            "90a0c080006a2106024002404100280298a2c08000220541012000410376742200"
            "710d0041002005200072360298a2c08000200621000c010b200628020821000b20"
            "0620013602082000200136020c2001200636020c200120003602080c070b200020"
            "053602002000200028020420086a360204200520024103723602042007410f6a41"
            "787141786a2201200520026a22006b2102200141002802aca2c08000460d032001"
            "41002802a8a2c08000460d040240200128020422064103714101470d0020012006"
            "417871220610a780808000200620026a2102200120066a220128020421060b2001"
            "2006417e7136020420002002410172360204200020026a20023602000240200241"
            "8002490d002000200210d0808080000c060b200241f801714190a0c080006a2101"
            "024002404100280298a2c08000220641012002410376742202710d004100200620"
            "0272360298a2c08000200121020c010b200128020821020b200120003602082002"
            "200036020c2000200136020c200020023602080c050b4100200020026b22013602"
            "a4a2c08000410041002802aca2c08000220020026a22063602aca2c08000200620"
            "0141017236020420002002410372360204200041086a21010c060b41002802a8a2"
            "c08000210102400240200020026b2206410f4b0d00410041003602a8a2c0800041"
            "0041003602a0a2c0800020012000410372360204200120006a2200200028020441"
            "01723602040c010b410020063602a0a2c080004100200120026a22053602a8a2c0"
            "800020052006410172360204200120006a2006360200200120024103723602040b"
            "200141086a0f0b2000200720086a360204410041002802aca2c080002200410f6a"
            "417871220141786a22063602aca2c080004100200020016b41002802a4a2c08000"
            "20086a22016a41086a22053602a4a2c0800020062005410172360204200020016a"
            "4128360204410041808080013602b8a2c080000c030b410020003602aca2c08000"
            "410041002802a4a2c0800020026a22023602a4a2c0800020002002410172360204"
            "0c010b410020003602a8a2c08000410041002802a0a2c0800020026a22023602a0"
            "a2c0800020002002410172360204200020026a20023602000b200541086a0f0b41"
            "00210141002802a4a2c08000220020024d0d004100200020026b22013602a4a2c0"
            "8000410041002802aca2c08000220020026a22063602aca2c08000200620014101"
            "7236020420002002410372360204200041086a0f0b20010f0b2000200436021802"
            "4020062802102205450d0020002005360210200520003602180b20062802142205"
            "450d0020002005360214200520003602180b0240024020014110490d0020062002"
            "410372360204200620026a22002001410172360204200020016a20013602000240"
            "2001418002490d002000200110d0808080000c020b200141f801714190a0c08000"
            "6a2102024002404100280298a2c08000220541012001410376742201710d004100"
            "2005200172360298a2c08000200221010c010b200228020821010b200220003602"
            "082001200036020c2000200236020c200020013602080c010b2006200120026a22"
            "00410372360204200620006a220020002802044101723602040b200641086a0be9"
            "0101037f23808080800041206b2204248080808000024002400240200220036a22"
            "0320024f0d00410021020c010b4100210220012802002205410174220620032006"
            "20034b1b22034108200341084b1b22034100480d000240024020050d0041002102"
            "0c010b2004200536021c20042001280204360214410121020b2004200236021820"
            "0441086a2003200441146a109980808000024020042802080d00200428020c2102"
            "200120033602002001200236020441818080807821020c010b2004280210210120"
            "0428020c21020c010b0b2000200136020420002002360200200441206a24808080"
            "80000b1000024020000d0010a9808080000b000b6101017f23808080800041106b"
            "220224808080800020022000410c6a36020c200141fe81c08000410d418b82c080"
            "0041052000418680808000419082c0800041052002410c6a418780808000108f80"
            "8080002100200241106a24808080800020000be00301097f23808080800041c000"
            "6b2202248080808000200028020821032000280204210441012105200128021441"
            "b083c080004101200128021828020c118080808000808080800021000240200345"
            "0d0041002106034020062107410121062000410171210841012100024020080d00"
            "02400240200128021c22084104710d002007410171450d01410121002001280214"
            "41e784c080004102200128021828020c1180808080008080808000450d010c020b"
            "200128021821092001280214210a024020074101710d0041012100200a41888bc0"
            "80004101200928020c11808080800080808080000d020b200241013a001b200220"
            "093602102002200a36020c20022008360238200241c884c0800036023420022001"
            "2d00203a003c2002200128021036022c2002200129020837022420022001290200"
            "37021c20022002411b6a36021420022002410c6a360230024020042002411c6a10"
            "8a808080000d00200228023041ec84c080004102200228023428020c1180808080"
            "00808080800021000c020b410121000c010b20042001108a8080800021000b2004"
            "41016a21042003417f6a22030d000b0b024020000d00200128021441f484c08000"
            "4101200128021828020c118080808000808080800021050b200241c0006a248080"
            "80800020050b4a01017f23808080800041106b2202248080808000200220003602"
            "0c200141ee8ac0800041fb8ac080002002410c6a41888080800010a28080800021"
            "00200241106a24808080800020000b3d00200128021420002802002d0000410274"
            "220041a09ec080006a2802002000418c9ec080006a280200200128021828020c11"
            "808080800080808080000be70101017f23808080800041106b2205248080808000"
            "20002802142001410d200028021828020c11808080800080808080002101200541"
            "003a000d200520013a000c20052000360208200541086a200241042003200410b6"
            "80808000210320052d000d220120052d000c2204722100024020014101470d0020"
            "044101710d000240200328020022002d001c4104710d00200028021441ef84c080"
            "004102200028021828020c118080808000808080800021000c010b200028021441"
            "ee84c080004101200028021828020c118080808000808080800021000b20054110"
            "6a24808080800020004101710bf513050b7f017e057f027e057f23808080800041"
            "c0006b220024808080800041002d00b89ec080001a024002400240024002400240"
            "02400240024002400240024002404107109b808080002201450d00200141036a41"
            "002800f18bc08000360000200141002800ee8bc080003600002001410710818080"
            "800022022800042103200228000021042002410810878080800020014107108780"
            "80800041002d00b89ec080001a4107109b808080002201450d00200141036a4100"
            "2800f18bc08000360000200141002800ee8bc08000360000200141071082808080"
            "002202280004210520022800002106200241081087808080002001410710878080"
            "800041002d00b89ec080001a410b109b808080002201450d00200141076a410028"
            "00fc8bc08000360000200141002900f58bc080003700002001410b108280808000"
            "2202280004210720022800002108200241081087808080002001410b1087808080"
            "0041002d00b89ec080001a4107109b808080002201450d00200141036a41002800"
            "9f8cc080003600002001410028009c8cc0800036000041e1002008200720014107"
            "108380808000220928000421022009280000210a20094108108780808000200041"
            "186a200a200210a48080800002400240024020002d00184101460d002000290320"
            "210b02402002450d00200a20021087808080000b20014107108780808000410021"
            "0941002d00b89ec080001a4104109b808080002201450d03200141c4c2d18b0636"
            "0000200141041082808080002202280000210c2002280004210a20024108108780"
            "80800020014104108780808000024002400240200a4100480d000240200a0d0041"
            "0121014100210d0c030b41002d00b89ec080001a200a109b8080800022010d0141"
            "0121090b2009200a109d80808000000b200a210d0b2001200c200a10d680808000"
            "2102200a450d014100200a41796a22012001200a4b1b210e200241036a417c7120"
            "026b210f4100210103400240024002400240200220016a2d00002209c022104100"
            "480d00200f20016b4103710d012001200e4f0d020340200220016a220928020420"
            "0928020072418081828478710d03200141086a2201200e490d000c030b0b428080"
            "808080202111428080808010211202400240024002400240024002400240024002"
            "40024002402009418888c080006a2d0000417e6a0e030003010b0b200141016a22"
            "09200a490d01420021110c090b42002111200141016a2213200a490d020c080b42"
            "80808080802021114280808080102112200220096a2c000041bf7f4a0d080c060b"
            "42002111200141016a2213200a4f0d06200220136a2c0000211302400240024020"
            "0941e001460d00200941ed01460d012010411f6a41ff0171410c490d022010417e"
            "71416e470d0420134140480d050c040b201341607141a07f460d040c030b201341"
            "9f7f4a0d020c030b20134140480d020c010b200220136a2c000021130240024002"
            "400240200941907e6a0e050100000002000b2010410f6a41ff017141024b0d0320"
            "1341404e0d030c020b201341f0006a41ff017141304f0d020c010b2013418f7f4a"
            "0d010b200141026a2209200a4f0d05200220096a2c000041bf7f4a0d0242002112"
            "200141036a2209200a4f0d06200220096a2c000041bf7f4c0d04428080808080e0"
            "0021110c030b4280808080802021110c020b42002112200141026a2209200a4f0d"
            "04200220096a2c000041bf7f4c0d020b428080808080c00021110b428080808010"
            "21120c020b200941016a21010c040b420021120b20112012842001ad8421110240"
            "200d418080808078470d00200a21142002210d0c070b200020113702242000200d"
            "3602182000200aad4220862002ad8437021c41988ac08000412b200041186a41cc"
            "81c0800041a882c08000108880808000000b200141016a21010c010b2001200a4f"
            "0d000340200220016a2c00004100480d01200a200141016a2201470d000c040b0b"
            "2001200a490d000c020b0b200020002d00193a000c41988ac08000412b2000410c"
            "6a41888ac0800041a48cc08000108880808000000b200aad2111200221140b2000"
            "41186a20142011a710a48080800020002d00184101460d01200029032021124100"
            "210f108480808000211541002d00b89ec080001a410b109b808080002216450d00"
            "201641076a41002800878cc08000360000201641002900808cc080003700002016"
            "410b10828080800022012800002117200128000421132001410810878080800002"
            "400240024020130e020f00010b4101210f20172d0000220141556a0e030e010e01"
            "0b20172d000021010b0240200141ff017141556a0e03040600060b2013417f6a21"
            "09201741016a210220134109490d024100210103402009450d0a20022d00004150"
            "6a220e41094b0d084103210f2001ac420a7e2211422088a72011a72210411f7547"
            "0d0d200241016a21022009417f6a2109200e41004a2010200e6b22012010487345"
            "0d000c0d0b0b000b200020002d00193a000c41988ac08000412b2000410c6a41bc"
            "81c0800041b882c08000108880808000000b2009450d01410021014101210f0340"
            "20022d000041506a220e41094b0d0a200241016a21022001410a6c200e6b210120"
            "09417f6a22090d000c070b0b2013417f6a2109201741016a2102201341094f0d02"
            "20090d040b410021010c050b201321092017210220134108490d020b4100210103"
            "402009450d0320022d000041506a220e41094b0d014102210f2001ac420a7e2211"
            "422088a72011a72210411f75470d06200241016a21022009417f6a2109200e4100"
            "482010200e6a220120104873450d000c060b0b4101210f0c040b41002101410121"
            "0f034020022d000041506a220e41094b0d04200241016a2102200e2001410a6c6a"
            "21012009417f6a22090d000b0b2013450d010b201720131087808080000b201641"
            "0b1087808080002004200310808080800020062005108080808000200820071080"
            "80808000200c200a10808080800020004100360214200042808080801037020c20"
            "0041033a003820004120360228200041003602342000418080c080003602302000"
            "41003602202000410036021820002000410c6a36022c0240200b4101200041186a"
            "1086808080000d00200028020c2102200028021022092000280214108080808000"
            "02402002450d00200920021087808080000b201510858080800020011085808080"
            "0041002102024020032005470d0020042006200310d58080800045200b20125871"
            "201520014e7121020b0240200d450d002014200d1087808080000b0240200a450d"
            "00200c200a1087808080000b02402007450d00200820071087808080000b024020"
            "05450d00200620051087808080000b02402003450d00200420031087808080000b"
            "200041c0006a24808080800020020f0b41a880c0800041372000413f6a419880c0"
            "800041ac81c08000108880808000000b2000200f3a001841988ac08000412b2000"
            "41186a41888ac08000418c8cc08000108880808000000bd60202027f027e238080"
            "80800041106b220324808080800002400240024002400240024002400240024002"
            "40024020020e020200010b4101210220012d000041556a0e03060306030b20012d"
            "0000412b470d01200141016a2101200241124921042002417f6a210220040d020c"
            "030b200041003a00010c050b200241114f0d010b420021050c010b420021050340"
            "2002450d04200320054200420a420010d78080800020012d000041506a2204410a"
            "4f0d02024020032903084200510d00200041023a00010c040b200141016a210120"
            "02417f6a2102200329030022062004ad7c220520065a0d000b200041023a00010c"
            "020b034020012d000041506a2204410a4f0d01200141016a21012005420a7e2004"
            "ad7c21052002417f6a2202450d030c000b0b41012101200041013a00010c020b41"
            "0121010c010b20002005370308410021010b200020013a0000200341106a248080"
            "8080000bbe0601057f200041786a22012000417c6a280200220241787122006a21"
            "030240024020024101710d002002410271450d012001280200220220006a210002"
            "40200120026b220141002802a8a2c08000470d0020032802044103714103470d01"
            "410020003602a0a2c0800020032003280204417e71360204200120004101723602"
            "04200320003602000f0b2001200210a7808080000b024002400240024002400240"
            "200328020422024102710d00200341002802aca2c08000460d02200341002802a8"
            "a2c08000460d0320032002417871220210a7808080002001200220006a22004101"
            "72360204200120006a2000360200200141002802a8a2c08000470d014100200036"
            "02a0a2c080000f0b20032002417e7136020420012000410172360204200120006a"
            "20003602000b2000418002490d022001200010d080808000410021014100410028"
            "02c0a2c08000417f6a22003602c0a2c0800020000d0402404100280288a0c08000"
            "2200450d00410021010340200141016a2101200028020822000d000b0b41002001"
            "41ff1f200141ff1f4b1b3602c0a2c080000f0b410020013602aca2c08000410041"
            "002802a4a2c0800020006a22003602a4a2c0800020012000410172360204024020"
            "0141002802a8a2c08000470d00410041003602a0a2c08000410041003602a8a2c0"
            "80000b200041002802b8a2c0800022044d0d0341002802aca2c080002200450d03"
            "4100210241002802a4a2c0800022054129490d024180a0c0800021010340024020"
            "01280200220320004b0d002000200320012802046a490d040b200128020821010c"
            "000b0b410020013602a8a2c08000410041002802a0a2c0800020006a22003602a0"
            "a2c0800020012000410172360204200120006a20003602000f0b200041f8017141"
            "90a0c080006a2103024002404100280298a2c08000220241012000410376742200"
            "710d0041002002200072360298a2c08000200321000c010b200328020821000b20"
            "0320013602082000200136020c2001200336020c200120003602080f0b02404100"
            "280288a0c080002201450d00410021020340200241016a2102200128020822010d"
            "000b0b4100200241ff1f200241ff1f4b1b3602c0a2c08000200520044d0d004100"
            "417f3602b8a2c080000b0b4d01017f23808080800041206b220224808080800020"
            "02410036021020024101360204200242043702082002412e36021c200220003602"
            "182002200241186a3602002002200110aa80808000000b820301047f200028020c"
            "21020240024002402001418002490d002000280218210302400240024020022000"
            "470d00200041144110200028021422021b6a28020022010d01410021020c020b20"
            "002802082201200236020c200220013602080c010b200041146a200041106a2002"
            "1b21040340200421052001220241146a200241106a200228021422011b21042002"
            "4114411020011b6a28020022010d000b200541003602000b2003450d0202402000"
            "28021c41027441809fc080006a22012802002000460d0020034110411420032802"
            "102000461b6a20023602002002450d030c020b2001200236020020020d01410041"
            "0028029ca2c08000417e200028021c777136029ca2c080000c020b024020022000"
            "2802082204460d002004200236020c200220043602080f0b41004100280298a2c0"
            "8000417e20014103767771360298a2c080000f0b20022003360218024020002802"
            "102201450d0020022001360210200120023602180b20002802142201450d002002"
            "2001360214200120023602180f0b0ba00401027f200020016a2102024002402000"
            "28020422034101710d002003410271450d012000280200220320016a2101024020"
            "0020036b220041002802a8a2c08000470d0020022802044103714103470d014100"
            "20013602a0a2c0800020022002280204417e713602042000200141017236020420"
            "0220013602000c020b2000200310a7808080000b02400240024002402002280204"
            "22034102710d00200241002802aca2c08000460d02200241002802a8a2c0800046"
            "0d0320022003417871220310a7808080002000200320016a220141017236020420"
            "0020016a2001360200200041002802a8a2c08000470d01410020013602a0a2c080"
            "000f0b20022003417e7136020420002001410172360204200020016a2001360200"
            "0b02402001418002490d002000200110d0808080000f0b200141f801714190a0c0"
            "80006a2102024002404100280298a2c08000220341012001410376742201710d00"
            "41002003200172360298a2c08000200221010c010b200228020821010b20022000"
            "3602082001200036020c2000200236020c200020013602080f0b410020003602ac"
            "a2c08000410041002802a4a2c0800020016a22013602a4a2c08000200020014101"
            "72360204200041002802a8a2c08000470d01410041003602a0a2c0800041004100"
            "3602a8a2c080000f0b410020003602a8a2c08000410041002802a0a2c080002001"
            "6a22013602a0a2c0800020002001410172360204200020016a20013602000f0b0b"
            "4701017f23808080800041206b2200248080808000200041003602182000410136"
            "020c200041dc82c0800036020820004204370210200041086a41f882c0800010aa"
            "80808000000b5601017f23808080800041206b2202248080808000200241106a20"
            "0041106a290200370300200241086a200041086a290200370300200241013b011c"
            "2002200136021820022000290200370300200210ac80808000000b110020003502"
            "00410120011086808080000b5d01027f23808080800041206b2201248080808000"
            "20002802182102200141106a200041106a290200370300200141086a200041086a"
            "2902003703002001200036021c2001200236021820012000290200370300200110"
            "d180808000000b490002402002418080c400460d00200020022001280210118180"
            "8080008080808000450d0041010f0b024020030d0041000f0b2000200320042001"
            "28020c11808080800080808080000b7d02017f017e23808080800041306b220224"
            "808080800020022000360200200220013602042002410236020c200241f887c080"
            "00360208200242023702142002418380808000ad4220862203200241046aad8437"
            "0328200220032002ad843703202002200241206a360210200241086a419487c080"
            "0010aa80808000000bc20b010b7f20002802082103024002400240024020002802"
            "0022040d002003410171450d010b02402003410171450d00200120026a21050240"
            "0240200028020c22060d0041002107200121080c010b4100210741002109200121"
            "080340200822032005460d020240024020032c00002208417f4c0d00200341016a"
            "21080c010b0240200841604f0d00200341026a21080c010b0240200841704f0d00"
            "200341036a21080c010b200341046a21080b200820036b20076a21072006200941"
            "016a2209470d000b0b20082005460d00024020082c00002203417f4a0d00200341"
            "60491a0b024002402007450d000240200720024f0d00200120076a2c000041bf7f"
            "4a0d01410021030c020b20072002460d00410021030c010b200121030b20072002"
            "20031b21022003200120031b21010b024020040d00200028021420012002200028"
            "021828020c11808080800080808080000f0b2000280204210a024020024110490d"
            "0020022001200141036a417c7122076b22096a220b410371210441002106410021"
            "03024020012007460d004100210302402009417c4b0d0041002103410021050340"
            "2003200120056a22082c000041bf7f4a6a200841016a2c000041bf7f4a6a200841"
            "026a2c000041bf7f4a6a200841036a2c000041bf7f4a6a2103200541046a22050d"
            "000b0b200121080340200320082c000041bf7f4a6a2103200841016a2108200941"
            "016a22090d000b0b02402004450d002007200b417c716a22082c000041bf7f4a21"
            "0620044101460d00200620082c000141bf7f4a6a210620044102460d0020062008"
            "2c000241bf7f4a6a21060b200b4102762105200620036a21060340200721042005"
            "450d04200541c001200541c001491b220b410371210c200b410274210d41002108"
            "024020054104490d002004200d41f007716a210941002108200421030340200328"
            "020c2207417f7341077620074106767241818284087120032802082207417f7341"
            "077620074106767241818284087120032802042207417f73410776200741067672"
            "41818284087120032802002207417f734107762007410676724181828408712008"
            "6a6a6a6a2108200341106a22032009470d000b0b2005200b6b21052004200d6a21"
            "07200841087641ff81fc0771200841ff81fc07716a418180046c41107620066a21"
            "06200c450d000b2004200b41fc01714102746a22082802002203417f7341077620"
            "03410676724181828408712103200c4101460d0220082802042207417f73410776"
            "20074106767241818284087120036a2103200c4102460d0220082802082208417f"
            "7341077620084106767241818284087120036a21030c020b024020020d00410021"
            "060c030b2002410371210802400240200241044f0d0041002106410021090c010b"
            "41002106200121032002410c71220921070340200620032c000041bf7f4a6a2003"
            "41016a2c000041bf7f4a6a200341026a2c000041bf7f4a6a200341036a2c000041"
            "bf7f4a6a2106200341046a21032007417c6a22070d000b0b2008450d0220012009"
            "6a21030340200620032c000041bf7f4a6a2106200341016a21032008417f6a2208"
            "0d000c030b0b200028021420012002200028021828020c11808080800080808080"
            "000f0b200341087641ff811c71200341ff81fc07716a418180046c41107620066a"
            "21060b02400240200a20064d0d00200a20066b2105410021030240024002402000"
            "2d00200e0402000102020b20052103410021050c010b2005410176210320054101"
            "6a41017621050b200341016a210320002802102109200028021821082000280214"
            "210703402003417f6a2203450d0220072009200828021011818080800080808080"
            "00450d000b41010f0b200028021420012002200028021828020c11808080800080"
            "808080000f0b0240200720012002200828020c1180808080008080808000450d00"
            "41010f0b410021030340024020052003470d0020052005490f0b200341016a2103"
            "2007200920082802101181808080008080808000450d000b2003417f6a2005490b"
            "820302017f017e23808080800041f0006b2203248080808000200341b08dc08000"
            "36020c20032000360208200341b08dc08000360214200320013602102003410236"
            "021c200341b183c08000360218024020022802000d002003410336025c200341e4"
            "83c08000360258200342033702642003418180808000ad4220862204200341106a"
            "ad8437034820032004200341086aad843703402003418280808000ad4220862003"
            "41186aad843703382003200341386a360260200341d8006a41e899c0800010aa80"
            "808000000b200341206a41106a200241106a290200370300200341206a41086a20"
            "0241086a290200370300200320022902003703202003410436025c2003419884c0"
            "8000360258200342043702642003418180808000ad4220862204200341106aad84"
            "37035020032004200341086aad843703482003418980808000ad42208620034120"
            "6aad843703402003418280808000ad422086200341186aad843703382003200341"
            "386a360260200341d8006a41e899c0800010aa80808000000b1c00200028020020"
            "01200028020428020c11818080800080808080000b140020012000280200200028"
            "020410af808080000b14002001280214200128021820001091808080000b220020"
            "01280214418883c08000410e200128021828020c11808080800080808080000b60"
            "01017f23808080800041306b22002480808080002000410136020c200041a883c0"
            "8000360208200042013702142000418a80808000ad4220862000412f6aad843703"
            "202000200041206a360210200041086a41cc97c0800010aa80808000000be70302"
            "057f017e23808080800041c0006b220524808080800041012106024020002d0004"
            "0d0020002d0005210702402000280200220828021c22094104710d004101210620"
            "0828021441e784c0800041e484c08000200741017122071b4102410320071b2008"
            "28021828020c11808080800080808080000d012008280214200120022008280218"
            "28020c11808080800080808080000d01200828021441ef97c08000410220082802"
            "1828020c11808080800080808080000d0120032008200411818080800080808080"
            "0021060c010b41012106024020074101710d00200828021441e984c08000410320"
            "0828021828020c11808080800080808080000d01200828021c21090b4101210620"
            "0541013a001b2005200829021437020c200541c884c0800036023420052005411b"
            "6a360214200520082902083702242008290200210a200520093602382005200828"
            "021036022c200520082d00203a003c2005200a37021c20052005410c6a36023020"
            "05410c6a2001200210b7808080000d002005410c6a41ef97c08000410210b78080"
            "80000d0020032005411c6a200411818080800080808080000d00200528023041ec"
            "84c080004102200528023428020c118080808000808080800021060b200041013a"
            "0005200020063a0004200541c0006a24808080800020000bdf04010c7f2001417f"
            "6a2103200028020421042000280200210520002802082106410021074100210841"
            "0021094100210a02400340200a4101710d0102400240200920024b0d0003402001"
            "20096a210a0240024002400240200220096b220b41074b0d0020022009470d0120"
            "0221090c050b02400240200a41036a417c71220c200a6b220d450d004100210003"
            "40200a20006a2d0000410a460d05200d200041016a2200470d000b200d200b4178"
            "6a220e4d0d010c030b200b41786a210e0b03404180828408200c2802002200418a"
            "94a8d000736b2000724180828408200c41046a2802002200418a94a8d000736b20"
            "00727141808182847871418081828478470d02200c41086a210c200d41086a220d"
            "200e4d0d000c020b0b410021000340200a20006a2d0000410a460d02200b200041"
            "016a2200470d000b200221090c030b0240200d200b470d00200221090c030b200a"
            "200d6a210c2002200d6b20096b210b4100210002400340200c20006a2d0000410a"
            "460d01200b200041016a2200470d000b200221090c030b2000200d6a21000b2000"
            "20096a220c41016a21090240200c20024f0d00200a20006a2d0000410a470d0041"
            "00210a2009210d200921000c030b200920024d0d000b0b20082002460d02410121"
            "0a2008210d200221000b0240024020062d0000450d00200541e084c08000410420"
            "0428020c11808080800080808080000d010b200020086b210b4100210c02402000"
            "2008460d00200320006a2d0000410a46210c0b200120086a21002006200c3a0000"
            "200d210820052000200b200428020c1180808080008080808000450d010b0b4101"
            "21070b20070b6001027f2000280204210220002802002103024020002802082200"
            "2d0000450d00200341e084c080004104200228020c118080808000808080800045"
            "0d0041010f0b20002001410a463a00002003200120022802101181808080008080"
            "8080000b1200200041c884c0800020011091808080000b6a01017f238080808000"
            "41306b22032480808080002003200136020c200320003602082003410136021420"
            "0341d490c080003602102003420137021c2003418280808000ad42208620034108"
            "6aad843703282003200341286a360218200341106a200210aa80808000000b2701"
            "017f200028020022002000411f7522027320026bad2000417f73411f7620011086"
            "808080000b830201087f2380808080004180016b22022480808080002001280204"
            "21032001280200210420002802002100200128021c220521060240200541047145"
            "0d002005410872210620040d0020014281808080a0013702000b20012006410472"
            "36021c41ff00210603402002200622076a22082000410f712206413072200641d7"
            "006a2006410a491b3a00002007417f6a2106200041104921092000410476210020"
            "09450d000b02402007418101490d002007418001418885c08000108b8080800000"
            "0b20014101419885c0800041022008418101200741016a6b108c80808000210020"
            "01200536021c200120033602042001200436020020024180016a24808080800020"
            "000baf0101017f23808080800041306b2201248080808000024002402000417f4c"
            "0d000240024020000d00410121000c010b41002d00b89ec080001a2000109b8080"
            "80002200450d020b2001200036020c200141023602142001418c8bc08000360210"
            "2001420137021c2001418b8080800036022c2001200141286a3602182001200141"
            "0c6a360228200141106a10be80808000200128020c2100200141306a2480808080"
            "0020000f0b10a9808080000b000bbe0604017f017e037f017e23808080800041c0"
            "006b22012480808080002001410636020c2001419498c08000360208024041002d"
            "00e89ec080004103460d0010c8808080000b0240024002400240024041002903d0"
            "a2c0800022024200520d00024041002802d8a2c0800022030d0010c18080800041"
            "002802d8a2c0800021030b20032003280200220441016a3602002004417f4c0d01"
            "2003450d02200320032802002204417f6a3602002003290308210220044101470d"
            "00200310c2808080000b024002400240200241002903c09ec08000510d0041002d"
            "00cc9ec08000210441012103410041013a00cc9ec08000200120043a0018200445"
            "0d012001420037023420014281808080c00037022c200141bc99c0800036022820"
            "0141186a200141286a10c380808000000b024041002802c89ec080002203417f46"
            "0d00200341016a21030c020b419c9ac08000412641e09ac0800010ba8080800000"
            "0b410020023703c09ec080000b410020033602c89ec08000200141c09ec0800036"
            "021041042103200141043a00182001200141106a360220200141186a41c08dc080"
            "002000109180808000210020012d001821040240024020000d0042002102411720"
            "0441ff0171764101710d01200128021c220328020021000240200341046a280200"
            "22042802002205450d002000200511828080800080808080000b02402004280204"
            "2204450d00200020041087808080000b2003410c108780808000410421030c010b"
            "200441ff01714104460d032001290318220642807e8321022006a721030b200128"
            "021022002000280208417f6a2204360208024020040d00200041003a000c200042"
            "003703000b200341ff01714104470d03200141c0006a2480808080000f0b000b41"
            "808fc0800041de0041f48fc0800010ba80808000000b2001410036023820014101"
            "36022c2001418499c0800036022820014204370230200141286a418c99c0800010"
            "aa80808000000b200120022003ad42ff0183843703102001410236022c200141f4"
            "97c08000360228200142023702342001418c80808000ad422086200141106aad84"
            "3703202001418280808000ad422086200141086aad843703182001200141186a36"
            "0230200141286a418498c0800010aa80808000000b7f01017f2380808080004130"
            "6b22022480808080002002200036020c20024102360214200241a88bc080003602"
            "102002420137021c2002418b8080800036022c2002200241286a36021820022002"
            "410c6a360228200241106a10be8080800002402001450d00200228020c20011087"
            "808080000b200241306a2480808080000b4701017f23808080800041206b220024"
            "8080808000200041003602182000410136020c200041bc90c08000360208200042"
            "04370210200041086a41c490c0800010aa80808000000bf90103027f037e017f23"
            "808080800041206b220024808080800041002d00b89ec080001a02400240024041"
            "20109b808080002201450d00200141023602102001428180808010370300410029"
            "03f89ec08000210203402002427f510d024100200242017c220341002903f89ec0"
            "80002204200420025122051b3703f89ec08000200421022005450d000b41002003"
            "3703d0a2c080002001200337030841002802d8a2c08000450d0220004100360218"
            "2000410136020c200041c48cc0800036020820004204370210200041086a419c8d"
            "c0800010aa808080000b000b10c080808000000b410020013602d8a2c080002000"
            "41206a2480808080000b5b01027f024020002802104101470d0020002802142201"
            "41003a000020002802182202450d00200120021087808080000b02402000417f46"
            "0d00200020002802042201417f6a36020420014101470d00200041201087808080"
            "000b0b3a01017f23808080800041106b2202248080808000200241ac8dc0800036"
            "020c20022000360208200241086a2002410c6a200110b080808000000b30000240"
            "20002802002d00000d00200141e286c08000410510af808080000f0b200141e786"
            "c08000410410af808080000be50301017f23808080800041c0006b220224808080"
            "800002400240024002400240024020002d00000e0400010203000b200220002802"
            "0436020441002d00b89ec080001a4114109b808080002200450d04200041106a41"
            "002800809bc08000360000200041086a41002900f89ac080003700002000410029"
            "00f09ac08000370000200241143602102002200036020c20024114360208200241"
            "0336022c200241a097c08000360228200242023702342002418d80808000ad4220"
            "86200241046aad843703202002418e80808000ad422086200241086aad84370318"
            "2002200241186a36023020012802142001280218200241286a1091808080002100"
            "20022802082201450d03200228020c20011087808080000c030b20002d00012100"
            "2002410136022c200241d490c08000360228200242013702342002418280808000"
            "ad422086200241186aad8437030820022000410274220041c49bc080006a280200"
            "36021c2002200041e89cc080006a2802003602182002200241086a360230200128"
            "02142001280218200241286a10918080800021000c020b20012000280204220028"
            "0200200028020410af8080800021000c010b200028020422002802002001200028"
            "0204280210118180808000808080800021000b200241c0006a2480808080002000"
            "0f0b000b140020012000280204200028020810af808080000b7001037f20002802"
            "0421010240024020002d0000220041044b0d0020004103470d010b200128020021"
            "000240200141046a28020022022802002203450d00200020031182808080008080"
            "8080000b024020022802042202450d00200020021087808080000b2001410c1087"
            "808080000b0bf10101027f23808080800041206b22002480808080000240024002"
            "40024041002d00e89ec080000e0400000301000b410041023a00e89ec080004100"
            "2d00b89ec080001a418008109b808080002201450d01410041033a00e89ec08000"
            "410020013602d89ec08000410042808080808080013703d09ec080004100420037"
            "03c09ec08000410041003a00e09ec08000410041003602dc9ec08000410041003a"
            "00cc9ec08000410041003602c89ec080000b200041206a2480808080000f0b000b"
            "200041003602182000410136020c200041bc9bc080003602082000420437021020"
            "0041086a418c9ac0800010aa80808000000bb108010a7f23808080800041206b22"
            "042480808080000240024002400240024020012802100d002001417f3602102003"
            "41002003200241036a417c7120026b22056b41077120032005491b22066b210720"
            "032006490d0102402006450d0002400240200220036a2208417f6a22092d000041"
            "0a470d002006417f6a21060c010b200220076a220a2009460d0102402008417e6a"
            "22092d0000410a470d002006417e6a21060c010b200a2009460d0102402008417d"
            "6a22092d0000410a470d002006417d6a21060c010b200a2009460d010240200841"
            "7c6a22092d0000410a470d002006417c6a21060c010b200a2009460d0102402008"
            "417b6a22092d0000410a470d002006417b6a21060c010b200a2009460d01024020"
            "08417a6a22092d0000410a470d002006417a6a21060c010b200a2009460d010240"
            "200841796a22092d0000410a470d00200641796a21060c010b200a2009460d0120"
            "0641787221060b200620076a41016a21060c040b20052003200320054b1b210b41"
            "0020066b21082002417c6a210c2006417f7320026a210a02400340200a21052008"
            "210620072209200b4d0d01200641786a2108200541786a210a4180828408200220"
            "0941786a22076a280200220d418a94a8d000736b200d724180828408200c20096a"
            "280200220d418a94a8d000736b200d727141808182847871418081828478460d00"
            "0b0b200920034b0d0202400340200320066a450d012006417f6a2106200520036a"
            "21092005417f6a210520092d0000410a470d000b200320066a41016a21060c040b"
            "024002402001411c6a28020022060d00410021060c010b2006200141186a280200"
            "6a417f6a2d0000410a470d0041002106200141003a00202001411c6a4100360200"
            "0b0240200128021420066b20034b0d002000200141146a2002200310ca80808000"
            "0c050b200128021820066a2002200310d6808080001a200041043a00002001411c"
            "6a200620036a3602000c040b10b580808000000b20072003418487c08000108b80"
            "808000000b2009200310ae80808000000b0240200320064f0d0020044100360218"
            "2004410136020c2004418c91c0800036020820044204370210200441086a419491"
            "c0800010aa80808000000b02402001411c6a2802002205450d0002400240200128"
            "021420056b20064d0d00200141186a28020020056a2002200610d6808080001a20"
            "01411c6a200520066a22053602000c010b200441086a200141146a2002200610ca"
            "80808000024020042d00084104460d00200020042903083702000c030b2001411c"
            "6a28020021050b2005450d00200141003a00202001411c6a41003602000b200220"
            "066a210502402001280214200320066b22064b0d002000200141146a2005200610"
            "ca808080000c010b200141186a2802002005200610d6808080001a200041043a00"
            "002001411c6a20063602000b2001200128021041016a360210200441206a248080"
            "8080000b7101027f20012802002104024020012802082205450d00200420056b20"
            "034f0d004100210520014100360208200141003a000c0b0240200420034d0d0020"
            "0128020420056a2002200310d6808080001a200041043a00002001200520036a36"
            "02080f0b20004204370200200141003a000c0bc90103027f017e027f2380808080"
            "0041106b2203248080808000200341086a20002802082802002001200210c98080"
            "8000024020032d000822024104460d002000280204210420032903082105024002"
            "4020002d0000220141044b0d0020014103470d010b200428020021010240200441"
            "046a28020022062802002207450d002001200711828080800080808080000b0240"
            "20062802042206450d00200120061087808080000b2004410c1087808080000b20"
            "0020053702000b200341106a24808080800020024104470b9c0303027f017e037f"
            "23808080800041106b220224808080800020024100360204024002400240024020"
            "01418001490d002001418010490d012001418080044f0d0220022001413f714180"
            "01723a000620022001410c7641e001723a000420022001410676413f7141800172"
            "3a0005410321010c030b200220013a0004410121010c020b20022001413f714180"
            "01723a00052002200141067641c001723a0004410221010c010b20022001413f71"
            "418001723a00072002200141127641f001723a000420022001410676413f714180"
            "01723a000620022001410c76413f71418001723a0005410421010b200241086a20"
            "00280208280200200241046a200110c980808000024020022d000822014104460d"
            "0020002802042103200229030821040240024020002d0000220541044b0d002005"
            "4103470d010b200328020021050240200341046a28020022062802002207450d00"
            "2005200711828080800080808080000b024020062802042206450d002005200610"
            "87808080000b2003410c1087808080000b200020043702000b200241106a248080"
            "80800020014104470b1200200041c08dc0800020011091808080000b0300000b09"
            "00200041003602000bc30201047f411f21020240200141ffffff074b0d00200141"
            "0620014108766722026b7641017120024101746b413e6a21020b20004200370210"
            "2000200236021c200241027441809fc080006a21030240410028029ca2c0800041"
            "012002742204710d0020032000360200200020033602182000200036020c200020"
            "003602084100410028029ca2c0800020047236029ca2c080000f0b024002400240"
            "200328020022042802044178712001470d00200421020c010b2001410041192002"
            "4101766b2002411f461b742103034020042003411d764104716a41106a22052802"
            "002202450d02200341017421032002210420022802044178712001470d000b0b20"
            "022802082203200036020c20022000360208200041003602182000200236020c20"
            "0020033602080f0b20052000360200200020043602182000200036020c20002000"
            "3602080b0b00200010d280808000000bb50101037f23808080800041106b220124"
            "8080808000200028020c2102024002400240024020002802040e020001020b2002"
            "0d0141012102410021030c020b20020d0020002802002202280204210320022802"
            "0021020c010b20014180808080783602002001200036020c2001418f8080800020"
            "0028021c22002d001c20002d001d10d380808000000b2001200336020420012002"
            "3602002001419080808000200028021c22002d001c20002d001d10d38080800000"
            "0b990101027f23808080800041106b2204248080808000410041002802f49ec080"
            "00220541016a3602f49ec08000024020054100480d000240024041002d00c8a2c0"
            "80000d00410041002802c4a2c0800041016a3602c4a2c0800041002802f09ec080"
            "00417f4a0d010c020b200441086a200020011183808080008080808000000b4100"
            "41003a00c8a2c080002002450d0010ce80808000000b000b0c0020002001290200"
            "3703000b4a01037f4100210302402002450d000240034020002d0000220420012d"
            "00002205470d01200041016a2100200141016a21012002417f6a2202450d020c00"
            "0b0b200420056b21030b20030bc10201087f02400240200241104f0d0020002103"
            "0c010b2000410020006b41037122046a210502402004450d002000210320012106"
            "0340200320062d00003a0000200641016a2106200341016a22032005490d000b0b"
            "2005200220046b2207417c7122086a210302400240200120046a2209410371450d"
            "0020084101480d012009410374220641187121022009417c71220a41046a210141"
            "0020066b4118712104200a28020021060340200520062002762001280200220620"
            "047472360200200141046a2101200541046a22052003490d000c020b0b20084101"
            "480d0020092101034020052001280200360200200141046a2101200541046a2205"
            "2003490d000b0b20074103712102200920086a21010b02402002450d0020032002"
            "6a21050340200320012d00003a0000200141016a2101200341016a22032005490d"
            "000b0b20000b6e01067e2000200342ffffffff0f832205200142ffffffff0f8322"
            "067e22072003422088220820067e22062005200142208822097e7c22054220867c"
            "220a3703002000200820097e2005200654ad4220862005422088847c200a200754"
            "ad7c200420017e200320027e7c7c3703080b0bbe1e0100418080c0000bb41e1100"
            "00000c000000040000001200000013000000140000000000000000000000010000"
            "00150000006120446973706c617920696d706c656d656e746174696f6e20726574"
            "75726e656420616e206572726f7220756e65787065637465646c792f7275737463"
            "2f6332663734633366393238616562353033663135623465396566353737386537"
            "37663330353862382f6c6962726172792f616c6c6f632f7372632f737472696e67"
            "2e727300005f0010004b000000060a00000e000000000000000100000001000000"
            "16000000170000001400000004000000180000004572726f72557466384572726f"
            "7276616c69645f75705f746f6572726f725f6c656e46726f6d557466384572726f"
            "7262797465736572726f724e6f6e65536f6d657372632f6c69622e7273001d0110"
            "000a0000000c0000003d0000001d0110000a0000000d0000003700000063617061"
            "63697479206f766572666c6f770000004801100011000000616c6c6f632f737263"
            "2f7261775f7665632e727364011000140000001800000005000000426f72726f77"
            "4d75744572726f72616c726561647920626f72726f7765643a2096011000120000"
            "005b3d3d617373657274696f6e20606c6566742020726967687460206661696c65"
            "640a20206c6566743a200a2072696768743a2000b301100010000000c301100017"
            "000000da0110000900000020726967687460206661696c65643a200a20206c6566"
            "743a20000000b301100010000000fc011000100000000c02100009000000da0110"
            "00090000000100000000000000ef0b100002000000000000000c00000004000000"
            "190000001a0000001b00000020202020207b202c20207b0a2c0a7d207d28280a5d"
            "636f72652f7372632f666d742f6e756d2e72737502100013000000660000001700"
            "000030783030303130323033303430353036303730383039313031313132313331"
            "343135313631373138313932303231323232333234323532363237323832393330"
            "333133323333333433353336333733383339343034313432343334343435343634"
            "373438343935303531353235333534353535363537353835393630363136323633"
            "363436353636363736383639373037313732373337343735373637373738373938"
            "303831383238333834383538363837383838393930393139323933393439353936"
            "39373938393966616c736574727565636f72652f7372632f736c6963652f6d656d"
            "6368722e7273006b03100018000000830000001e0000006b031000180000009f00"
            "00000900000072616e676520737461727420696e64657820206f7574206f662072"
            "616e676520666f7220736c696365206f66206c656e67746820a403100012000000"
            "b60310002200000072616e676520656e6420696e64657820e803100010000000b6"
            "031000220000000101010101010101010101010101010101010101010101010101"
            "010101010101010101010101010101010101010101010101010101010101010101"
            "010101010101010101010101010101010101010101010101010101010101010101"
            "010101010101010101010101010101010101010101010101010101010101010101"
            "010101000000000000000000000000000000000000000000000000000000000000"
            "000000000000000000000000000000000000000000000000000000000000000000"
            "000000020202020202020202020202020202020202020202020202020202020202"
            "030303030303030303030303030303030404040404000000000000000000000000"
            "00000001000000010000001600000063616c6c65642060526573756c743a3a756e"
            "77726170282960206f6e20616e2060457272602076616c7565456d707479496e76"
            "616c69644469676974506f734f766572666c6f774e65674f766572666c6f775a65"
            "726f5061727365496e744572726f726b696e64616c6c6f63617465200a0000007f"
            "0510000900000088051000010000006465616c6c6f6361746520009c0510000b00"
            "000088051000010000002f686f6d652f7077616e672f7761736d2f72782d776173"
            "6d2d70726f746f747970652f7872706c2d7374642f7372632f6c69622e72734163"
            "636f756e7444657374696e6174696f6e46696e697368416674657200b805100036"
            "000000690000001600000042616c616e636500b8051000360000007e0000001600"
            "00007265656e7472616e7420696e69740000340610000e0000002f72757374632f"
            "633266373463336639323861656235303366313562346539656635373738653737"
            "663330353862382f6c6962726172792f636f72652f7372632f63656c6c2f6f6e63"
            "652e72730000004c0610004d000000230100004200000000000000000000000400"
            "0000040000001c0000001d0000000c000000040000001e0000001f000000200000"
            "002f727573742f646570732f646c6d616c6c6f632d302e322e362f7372632f646c"
            "6d616c6c6f632e7273617373657274696f6e206661696c65643a207073697a6520"
            "3e3d2073697a65202b206d696e5f6f7665726865616400d806100029000000a804"
            "000009000000617373657274696f6e206661696c65643a207073697a65203c3d20"
            "73697a65202b206d61785f6f766572686561640000d806100029000000ae040000"
            "0d000000757365206f66207374643a3a7468726561643a3a63757272656e742829"
            "206973206e6f7420706f737369626c652061667465722074686520746872656164"
            "2773206c6f63616c206461746120686173206265656e2064657374726f79656473"
            "74642f7372632f7468726561642f6d6f642e727300de07100015000000f1020000"
            "130000006661696c656420746f2067656e657261746520756e6971756520746872"
            "6561642049443a2062697473706163652065786861757374656400040810003700"
            "0000de07100015000000c40400000d00000001000000000000007374642f737263"
            "2f696f2f62756666657265642f6c696e657772697465727368696d2e72736d6964"
            "203e206c656e000081081000090000005c081000250000000f0100002900000065"
            "6e74697479206e6f7420666f756e647065726d697373696f6e2064656e69656463"
            "6f6e6e656374696f6e2072656675736564636f6e6e656374696f6e207265736574"
            "686f737420756e726561636861626c656e6574776f726b20756e72656163686162"
            "6c65636f6e6e656374696f6e2061626f727465646e6f7420636f6e6e6563746564"
            "6164647265737320696e2075736561646472657373206e6f7420617661696c6162"
            "6c656e6574776f726b20646f776e62726f6b656e2070697065656e746974792061"
            "6c7265616479206578697374736f7065726174696f6e20776f756c6420626c6f63"
            "6b6e6f742061206469726563746f727969732061206469726563746f7279646972"
            "6563746f7279206e6f7420656d707479726561642d6f6e6c792066696c65737973"
            "74656d206f722073746f72616765206d656469756d66696c6573797374656d206c"
            "6f6f70206f7220696e646972656374696f6e206c696d69742028652e672e207379"
            "6d6c696e6b206c6f6f70297374616c65206e6574776f726b2066696c652068616e"
            "646c65696e76616c696420696e70757420706172616d65746572696e76616c6964"
            "206461746174696d6564206f75747772697465207a65726f6e6f2073746f726167"
            "652073706163657365656b206f6e20756e7365656b61626c652066696c6566696c"
            "6573797374656d2071756f746120657863656564656466696c6520746f6f206c61"
            "7267657265736f75726365206275737965786563757461626c652066696c652062"
            "757379646561646c6f636b63726f73732d646576696365206c696e6b206f722072"
            "656e616d65746f6f206d616e79206c696e6b73696e76616c69642066696c656e61"
            "6d65617267756d656e74206c69737420746f6f206c6f6e676f7065726174696f6e"
            "20696e746572727570746564756e737570706f72746564756e6578706563746564"
            "20656e64206f662066696c656f7574206f66206d656d6f72796f74686572206572"
            "726f72756e63617465676f72697a6564206572726f7220286f73206572726f7220"
            "290000000100000000000000910b10000b0000009c0b1000010000007374642f73"
            "72632f696f2f737464696f2e727300b80b1000130000002c030000140000006661"
            "696c6564207072696e74696e6720746f203a20000000dc0b100013000000ef0b10"
            "0002000000b80b1000130000005d040000090000007374646f75747374642f7372"
            "632f696f2f6d6f642e72736120666f726d617474696e6720747261697420696d70"
            "6c656d656e746174696f6e2072657475726e656420616e206572726f7220776865"
            "6e2074686520756e6465726c79696e672073747265616d20646964206e6f740000"
            "002b0c1000560000001a0c100011000000280700001500000063616e6e6f742072"
            "65637572736976656c792061637175697265206d757465789c0c10002000000073"
            "74642f7372632f7379732f73796e632f6d757465782f6e6f5f746872656164732e"
            "7273c40c10002400000014000000090000007374642f7372632f73796e632f6f6e"
            "63652e7273f80c100014000000d9000000140000006c6f636b20636f756e74206f"
            "766572666c6f7720696e207265656e7472616e74206d757465787374642f737263"
            "2f73796e632f7265656e7472616e745f6c6f636b2e7273420d10001e0000002201"
            "00002d0000006f7065726174696f6e207375636365737366756c6f6e652d74696d"
            "6520696e697469616c697a6174696f6e206d6179206e6f7420626520706572666f"
            "726d6564207265637572736976656c79840d100038000000100000001100000012"
            "000000100000001000000013000000120000000d0000000e000000150000000c00"
            "00000b00000015000000150000000f0000000e0000001300000026000000380000"
            "0019000000170000000c000000090000000a000000100000001700000019000000"
            "0e0000000d00000014000000080000001b0000000e000000100000001600000015"
            "0000000b000000160000000d0000000b00000013000000a4081000b4081000c508"
            "1000d7081000e7081000f70810000a0910001c09100029091000370910004c0910"
            "005809100063091000780910008d0910009c091000aa091000bd091000e3091000"
            "1b0a1000340a10004b0a1000570a1000600a10006a0a10007a0a1000910a1000aa"
            "0a1000b80a1000c50a1000d90a1000e10a1000fc0a10000a0b10001a0b1000300b"
            "1000450b1000500b1000660b1000730b10007e0b1000050000000c0000000b0000"
            "000b000000040000004305100048051000540510005f0510006a05100000c62e04"
            "6e616d65000e0d7761736d5f6c69622e7761736d018e2e5800325f5a4e31306865"
            "6c7065725f6c696238686f73745f6c6962357072696e7431376864336330313266"
            "3765666531663636334501385f5a4e313068656c7065725f6c696238686f73745f"
            "6c6962313067657454784669656c64313768623836623962643665383439353163"
            "634502485f5a4e313068656c7065725f6c696238686f73745f6c69623236676574"
            "43757272656e744c6564676572456e7472794669656c6431376861346138303037"
            "3262396335613761644503415f5a4e313068656c7065725f6c696238686f73745f"
            "6c696231396765744c6564676572456e7472794669656c64313768633661326634"
            "323734313038306331384504415f5a4e313068656c7065725f6c696238686f7374"
            "5f6c69623139676574506172656e744c656467657254696d653137683731643366"
            "39663165383665663230374505315f5a4e313068656c7065725f6c696231327072"
            "696e745f6e756d626572313768343432633966366462343461613636374506305f"
            "5a4e34636f726533666d74336e756d33696d7037666d745f753634313768643532"
            "3166613665663661303637326145070e5f5f727573745f6465616c6c6f6308325f"
            "5a4e34636f726536726573756c743133756e777261705f6661696c656431376866"
            "3839396364303037373637303035314509475f5a4e34325f244c54242452462454"
            "247532302461732475323024636f72652e2e666d742e2e44656275672447542433"
            "666d7431376831323761303230623939303135656661450a475f5a4e34325f244c"
            "54242452462454247532302461732475323024636f72652e2e666d742e2e446562"
            "75672447542433666d7431376833326438343961303132376564636461450b445f"
            "5a4e34636f726535736c69636535696e6465783236736c6963655f73746172745f"
            "696e6465785f6c656e5f6661696c31376866393161336166653837623164343433"
            "450c385f5a4e34636f726533666d7439466f726d617474657231327061645f696e"
            "74656772616c31376863346561303761306263313335366334450d475f5a4e3432"
            "5f244c54242452462454247532302461732475323024636f72652e2e666d742e2e"
            "44656275672447542433666d743137683562646335303561663532336432393945"
            "0e5e5f5a4e34636f726533666d74336e756d35325f244c5424696d706c24753230"
            "24636f72652e2e666d742e2e44656275672475323024666f722475323024757369"
            "7a652447542433666d7431376836336361623039386234313233343130450f465f"
            "5a4e34636f726533666d7439466f726d6174746572323664656275675f73747275"
            "63745f6669656c64325f66696e6973683137683135666166363733326663303964"
            "62644510305f5a4e34636f726533666d743557726974653977726974655f666d74"
            "313768396461663134643536353865323530364511265f5a4e34636f726533666d"
            "743577726974653137683933353534653462653731663263376145124c5f5a4e34"
            "636f726533707472343264726f705f696e5f706c616365244c5424616c6c6f632e"
            "2e737472696e672e2e537472696e67244754243137683230373631353664386431"
            "65323961384513535f5a4e34636f726533707472343964726f705f696e5f706c61"
            "6365244c5424616c6c6f632e2e737472696e672e2e46726f6d557466384572726f"
            "7224475424313768323066303937633266353863396661374514525f5a4e35335f"
            "244c5424636f72652e2e666d742e2e4572726f7224753230246173247532302463"
            "6f72652e2e666d742e2e44656275672447542433666d7431376866376165323835"
            "35623234396462633545155f5f5a4e35385f244c5424616c6c6f632e2e73747269"
            "6e672e2e537472696e67247532302461732475323024636f72652e2e666d742e2e"
            "577269746524475424313077726974655f63686172313768323134333931636238"
            "656231353263364516435f5a4e35616c6c6f63377261775f766563313952617756"
            "6563244c54245424432441244754243867726f775f6f6e65313768666166636338"
            "3935356337386333653545175a5f5a4e35616c6c6f63377261775f766563323052"
            "6177566563496e6e6572244c5424412447542437726573657276653231646f5f72"
            "6573657276655f616e645f68616e646c6531376862356335336362636666396436"
            "31653745185d5f5a4e35385f244c5424616c6c6f632e2e737472696e672e2e5374"
            "72696e67247532302461732475323024636f72652e2e666d742e2e577269746524"
            "4754243977726974655f7374723137683539396439653537383934366464393845"
            "19325f5a4e35616c6c6f63377261775f766563313166696e6973685f67726f7731"
            "376832313261636366633461323839333362451a0e5f5f727573745f7265616c6c"
            "6f631b435f5a4e38646c6d616c6c6f6338646c6d616c6c6f633137446c6d616c6c"
            "6f63244c54244124475424366d616c6c6f63313768653635393339613463383937"
            "63633135451c4b5f5a4e35616c6c6f63377261775f766563323052617756656349"
            "6e6e6572244c54244124475424313467726f775f616d6f7274697a656431376834"
            "623330643530396631323837393465451d335f5a4e35616c6c6f63377261775f76"
            "6563313268616e646c655f6572726f723137683937623764626430663732646437"
            "3838451e5e5f5a4e36355f244c5424616c6c6f632e2e737472696e672e2e46726f"
            "6d557466384572726f72247532302461732475323024636f72652e2e666d742e2e"
            "44656275672447542433666d743137683132313861313631643933363438653945"
            "1f5e5f5a4e36355f244c5424616c6c6f632e2e7665632e2e566563244c54245424"
            "43244124475424247532302461732475323024636f72652e2e666d742e2e446562"
            "75672447542433666d74313768613636623539636339336533383537344520615f"
            "5a4e36385f244c5424636f72652e2e6e756d2e2e6572726f722e2e506172736549"
            "6e744572726f72247532302461732475323024636f72652e2e666d742e2e446562"
            "75672447542433666d74313768633837363633386165616230633031664521475f"
            "5a4e34325f244c54242452462454247532302461732475323024636f72652e2e66"
            "6d742e2e44656275672447542433666d7431376839393432316563653462383633"
            "3034384522465f5a4e34636f726533666d7439466f726d61747465723236646562"
            "75675f7374727563745f6669656c64315f66696e69736831376862653338633662"
            "346233306235386332452305726561647924675f5a4e34636f7265336e756d3630"
            "5f244c5424696d706c2475323024636f72652e2e7374722e2e7472616974732e2e"
            "46726f6d5374722475323024666f722475323024753634244754243866726f6d5f"
            "737472313768356563336638363835643535346239644525415f5a4e38646c6d61"
            "6c6c6f6338646c6d616c6c6f633137446c6d616c6c6f63244c5424412447542434"
            "667265653137683339383334616161616533653839343645262c5f5a4e34636f72"
            "653970616e69636b696e673570616e696331376830346565623931376464393363"
            "32323945274a5f5a4e38646c6d616c6c6f6338646c6d616c6c6f633137446c6d61"
            "6c6c6f63244c542441244754243132756e6c696e6b5f6368756e6b313768393334"
            "6533646333383362623538613345284b5f5a4e38646c6d616c6c6f6338646c6d61"
            "6c6c6f633137446c6d616c6c6f63244c542441244754243133646973706f73655f"
            "6368756e6b313768366530636363643435383635373436334529385f5a4e35616c"
            "6c6f63377261775f766563313763617061636974795f6f766572666c6f77313768"
            "34393964343832613965643537313561452a305f5a4e34636f72653970616e6963"
            "6b696e673970616e69635f666d7431376836353430636362326435666463336162"
            "452b625f5a4e34636f726533666d74336e756d33696d7035325f244c5424696d70"
            "6c2475323024636f72652e2e666d742e2e446973706c61792475323024666f7224"
            "753230247533322447542433666d74313768626633653032323834383365333735"
            "61452c11727573745f626567696e5f756e77696e642d465f5a4e34636f72653366"
            "6d7439466f726d617474657231327061645f696e74656772616c31327772697465"
            "5f70726566697831376861396134333238306236303036643132452e425f5a4e34"
            "636f726535736c69636535696e6465783234736c6963655f656e645f696e646578"
            "5f6c656e5f6661696c31376830383862353665323939626561616166452f2e5f5a"
            "4e34636f726533666d7439466f726d617474657233706164313768343736396165"
            "3338393337346363353145303b5f5a4e34636f72653970616e69636b696e673139"
            "6173736572745f6661696c65645f696e6e65723137683666376533323537643834"
            "61353034324531475f5a4e34325f244c5424245246245424753230246173247532"
            "3024636f72652e2e666d742e2e44656275672447542433666d7431376833613662"
            "6161316262343761643230344532495f5a4e34345f244c54242452462454247532"
            "302461732475323024636f72652e2e666d742e2e446973706c6179244754243366"
            "6d74313768376666346430623836303963323437324533585f5a4e35395f244c54"
            "24636f72652e2e666d742e2e417267756d656e7473247532302461732475323024"
            "636f72652e2e666d742e2e446973706c61792447542433666d7431376836386133"
            "65386535303963616663363445345c5f5a4e36335f244c5424636f72652e2e6365"
            "6c6c2e2e426f72726f774d75744572726f72247532302461732475323024636f72"
            "652e2e666d742e2e44656275672447542433666d74313768313564336433343334"
            "626464636363384535395f5a4e34636f72653463656c6c323270616e69635f616c"
            "72656164795f626f72726f77656431376833313462353261316263343662666534"
            "45363c5f5a4e34636f726533666d74386275696c64657273313144656275675374"
            "72756374356669656c64313768333531353864666637643465616633354537675f"
            "5a4e36385f244c5424636f72652e2e666d742e2e6275696c646572732e2e506164"
            "41646170746572247532302461732475323024636f72652e2e666d742e2e577269"
            "7465244754243977726974655f7374723137683831386234396537653639613236"
            "66644538695f5a4e36385f244c5424636f72652e2e666d742e2e6275696c646572"
            "732e2e50616441646170746572247532302461732475323024636f72652e2e666d"
            "742e2e577269746524475424313077726974655f63686172313768393437396266"
            "363162306130356661314539305f5a4e34636f726533666d743557726974653977"
            "726974655f666d7431376835393430386336353062386232313531453a325f5a4e"
            "34636f7265366f7074696f6e31336578706563745f6661696c6564313768663038"
            "61393965326437333336633661453b625f5a4e34636f726533666d74336e756d33"
            "696d7035325f244c5424696d706c2475323024636f72652e2e666d742e2e446973"
            "706c61792475323024666f7224753230246933322447542433666d743137686365"
            "6439306337613633396330316464453c4f5f5a4e35305f244c5424244250246d75"
            "74247532302454247532302461732475323024636f72652e2e666d742e2e446562"
            "75672447542433666d7431376834366435353230663839333131346633453d0861"
            "6c6c6f636174653e2b5f5a4e3373746432696f35737464696f365f7072696e7431"
            "376838316334373231363630343666306663453f0a6465616c6c6f636174654039"
            "5f5a4e3373746436746872656164385468726561644964336e6577396578686175"
            "73746564313768333336626637613134383830343463384541425f5a4e34636f72"
            "653463656c6c346f6e636531374f6e636543656c6c244c54245424475424387472"
            "795f696e69743137686365363362663232383531393165373145423e5f5a4e3561"
            "6c6c6f633473796e633136417263244c54245424432441244754243964726f705f"
            "736c6f77313768656539616363636164396363313036394543355f5a4e34636f72"
            "653970616e69636b696e6731336173736572745f6661696c656431376832333236"
            "3266326333633738623661624544475f5a4e34325f244c54242452462454247532"
            "302461732475323024636f72652e2e666d742e2e44656275672447542433666d74"
            "313768653138373433383865303762666532354545595f5a4e36305f244c542473"
            "74642e2e696f2e2e6572726f722e2e4572726f7224753230246173247532302463"
            "6f72652e2e666d742e2e446973706c61792447542433666d743137683930323731"
            "63376232613663653833394546595f5a4e36305f244c5424616c6c6f632e2e7374"
            "72696e672e2e537472696e67247532302461732475323024636f72652e2e666d74"
            "2e2e446973706c61792447542433666d7431376863653432323661613166373236"
            "63316345477a5f5a4e34636f726533707472383864726f705f696e5f706c616365"
            "244c54247374642e2e696f2e2e57726974652e2e77726974655f666d742e2e4164"
            "6170746572244c5424616c6c6f632e2e7665632e2e566563244c54247538244754"
            "242447542424475424313768313636646336316162303333346331654548495f5a"
            "4e337374643473796e63396f6e63655f6c6f636b31374f6e63654c6f636b244c54"
            "2454244754243130696e697469616c697a65313768376635633530386461396531"
            "623039624549605f5a4e36315f244c54247374642e2e696f2e2e737464696f2e2e"
            "5374646f75744c6f636b2475323024617324753230247374642e2e696f2e2e5772"
            "697465244754243977726974655f616c6c31376832346238323631303436316432"
            "353666454a555f5a4e3373746432696f3862756666657265643962756677726974"
            "65723138427566577269746572244c54245724475424313477726974655f616c6c"
            "5f636f6c6431376835383462646262616562306662316262454b735f5a4e38305f"
            "244c54247374642e2e696f2e2e57726974652e2e77726974655f666d742e2e4164"
            "6170746572244c54245424475424247532302461732475323024636f72652e2e66"
            "6d742e2e5772697465244754243977726974655f73747231376837666163663562"
            "633065666364383038454c325f5a4e34636f726533666d74355772697465313077"
            "726974655f6368617231376866306233626531656331396465356537454d305f5a"
            "4e34636f726533666d743557726974653977726974655f666d7431376866383830"
            "386630646630653435313364454e0a727573745f70616e69634f375f5a4e34636f"
            "72653570616e6963313250616e69635061796c6f61643661735f73747231376836"
            "3134396631343264396132653032654550505f5a4e38646c6d616c6c6f6338646c"
            "6d616c6c6f633137446c6d616c6c6f63244c542441244754243138696e73657274"
            "5f6c617267655f6368756e6b313768656665383531613237353832646137624551"
            "455f5a4e3373746433737973396261636b747261636532365f5f727573745f656e"
            "645f73686f72745f6261636b747261636531376834646333646534376432323032"
            "3162394552585f5a4e337374643970616e69636b696e673139626567696e5f7061"
            "6e69635f68616e646c657232385f24753762242475376224636c6f737572652475"
            "37642424753764243137686531376133393737663839633131373845533b5f5a4e"
            "337374643970616e69636b696e673230727573745f70616e69635f776974685f68"
            "6f6f6b31376837373665373963396636353931626535455483015f5a4e39395f24"
            "4c54247374642e2e70616e69636b696e672e2e626567696e5f70616e69635f6861"
            "6e646c65722e2e5374617469635374725061796c6f616424753230246173247532"
            "3024636f72652e2e70616e69632e2e50616e69635061796c6f6164244754243661"
            "735f737472313768656233663732326432323465343266384555066d656d636d70"
            "56066d656d63707957085f5f6d756c746933071201000f5f5f737461636b5f706f"
            "696e746572090a0100072e726f6461746100550970726f64756365727302086c61"
            "6e6775616765010452757374000c70726f6365737365642d627901057275737463"
            "25312e38332e302d6e696768746c79202863326637346333663920323032342d30"
            "392d30392900490f7461726765745f6665617475726573042b0a6d756c74697661"
            "6c75652b0f6d757461626c652d676c6f62616c732b0f7265666572656e63652d74"
            "797065732b087369676e2d657874";
        //        let sender = get_tx_account_id();
        //        let owner = get_current_escrow_account_id();
        //        let dest = get_current_escrow_destination();
        //        let dest_balance = get_account_balance(dest);
        //        let escrow_data = get_current_escrow_data();
        //        let ed_str = String::from_utf8(escrow_data).unwrap();
        //        let threshold_balance = ed_str.parse::<u64>().unwrap();
        //        let pl_time = host_lib::getParentLedgerTime();
        //        let e_time = get_current_escrow_finish_after();
        //        sender == owner && dest_balance <= threshold_balance &&
        //        pl_time >= e_time

        Account const alice{"alice"};
        Account const carol{"carol"};

        {
            // basic FinishFunction situation
            Env env(*this);
            // create escrow
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 0);
            auto escrowCreate = escrow(alice, carol, XRP(1000));
            XRPAmount txnFees = env.current()->fees().base + 1000;
            env(escrowCreate,
                finish_function(wasmHex),
                cancel_time(env.now() + 10s),
                data("1000"),
                fee(txnFees));
            env.close();

            if (BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2))
            {
                env.require(balance(alice, XRP(4000) - txnFees));
                env.require(balance(carol, XRP(5000)));

                // tx sender not escrow creator (alice)
                env(finish(carol, alice, seq),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));

                // destination balance is too high
                env(finish(carol, alice, seq),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));

                env.close();

                // reduce the destination balance
                env(pay(carol, alice, XRP(4500)));
                env.close();

                // tx sender not escrow creator (alice)
                env(finish(carol, alice, seq),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));

                // FinishAfter time hasn't passed
                env(finish(alice, alice, seq),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));
                env(finish(alice, alice, seq),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));
                env(finish(carol, alice, seq),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));
                env(finish(carol, alice, seq),
                    fee(txnFees),
                    ter(tecWASM_REJECTED));
                env.close();
                env(finish(alice, alice, seq), fee(txnFees), ter(tesSUCCESS));
                env.close();

                BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 0);
            }
        }
    }

    void
    run() override
    {
        // testEnablement();
        // testTiming();
        // testTags();
        // testDisallowXRP();
        // test1571();
        // testFails();
        // testLockup();
        // testEscrowConditions();
        // testMetaAndOwnership();
        // testConsequences();
        // testEscrowWithTickets();
        // testCredentials();
        // testFinishFunctionPreflight();
        // testFinishFunction();
        testAllHostFunctions();
    }
};

BEAST_DEFINE_TESTSUITE(Escrow, app, ripple);

}  // namespace test
}  // namespace ripple
