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
    testFinishFunction()
    {
        testcase("PoC escrow function");

        using namespace jtx;
        using namespace std::chrono;

        Account const alice{"alice"};
        Account const bob{"bob"};
        Account const carol{"carol"};
        Account const dillon{"dillon"};
        Account const zelda{"zelda"};

        // P2P3
        static auto wasmHex =
            "0061736d0100000001791160017f0060037f7f7f017f60027f7f017f60027f7f00"
            "60037f7f7f0060047f7f7f7f0060017f017f60047f7f7f7e0060057f7f7f7e7f00"
            "60057f7f7f7f7f0060047f7f7f7f017f60000060037e7f7f017f60067f7f7f7f7f"
            "7f017f60057f7f7f7f7f017f60077f7f7f7f7f7f7f017f60067f7f7f7f7f7f0003"
            "616004030305060001070808080809040702000000040403030304030500020306"
            "0a0609000b0300040103030402040c000d0e04010202010204040f090902020204"
            "0903020201020200000405000203030402020205100b0303000005030101010104"
            "05017001121205030100110619037f01418080c0000b7f004195d3c0000b7f0041"
            "a0d3c0000b074405066d656d6f7279020008616c6c6f63617465001e11636f6d70"
            "6172655f6163636f756e744944001f0a5f5f646174615f656e6403010b5f5f6865"
            "61705f6261736503020917010041010b1134332b3c3d3e4345565b124247445251"
            "460ad3cf0260ea0301057f23808080800041e0006b220324808080800020034100"
            "360228200320023602242003200136022020034180013a002c2003410036021c20"
            "03428080808010370214200341c8006a200341146a108180808000024002400240"
            "024020032d00484106460d00200341306a41106a2204200341c8006a41106a2903"
            "00370300200341306a41086a2205200341c8006a41086a29030037030020032003"
            "2903483703300240024020032802282202200328022422064f0d00200328022021"
            "070340200720026a2d000041776a220141174b0d02410120017441938080047145"
            "0d022006200241016a2202470d000b200320063602280b20002003290330370300"
            "200041106a2004290300370300200041086a200529030037030020032802142202"
            "450d04200328021820021082808080000c040b20032002360228200341086a2007"
            "20062006200241016a220220062002491b10838080800041002d00c0cfc080001a"
            "200328020c21012003280208210641141084808080002202450d01200220063602"
            "0c2002411636020020002002360204200041063a00002002200136021020034130"
            "6a1085808080000c020b2000200328024c360204200041063a00000c010b000b20"
            "032802142202450d00200328021820021082808080000b200341e0006a24808080"
            "80000beb28020c7f037e2380808080004180036b2202248080808000200128020c"
            "210302400240024002400240024002400240024002400240024002400240024002"
            "400240024002400240024002400240024002400240024002400240200128021422"
            "04200128021022054f0d002001410c6a21060340200320046a2d0000220741776a"
            "220841174b0d024101200874419380800471450d022001200441016a2204360214"
            "20052004470d000b200521040b200241f0006a200320052005200441016a220420"
            "052004491b10838080800041002d00c0cfc080001a200228027421082002280270"
            "2101411410848080800022040d010c1b0b200741e5004a0d0820074122460d0620"
            "07412d460d07200741db00470d09200120012d0018417f6a22083a001820044101"
            "6a2104200841ff0171450d0520012004360214200241003602e002200242808080"
            "8080013702d80241082109200420054f0d02200241b0016a41086a210a200241b0"
            "016a410172210b410821094100210c4101210d0340200628020021030240034020"
            "0320046a2d0000220741776a220841174b0d014101200874419380800471450d01"
            "2001200441016a220436021420052004470d000b200521040c040b024002400240"
            "200741dd00460d00200d4101710d02200441016a210402402007412c470d002001"
            "20043602140240200420054f0d000340200320046a2d0000220741776a22084117"
            "4b0d044101200874419380800471450d042001200441016a220436021420052004"
            "470d000b200521040b200241386a200320052005200441016a220420052004491b"
            "10838080800041002d00c0cfc080001a200228023c210420022802382108411410"
            "84808080002207450d1f2007200836020c20074105360200200720043602100c08"
            "0b200241c8006a200320052005200420052004491b10838080800041002d00c0cf"
            "c080001a200228024c21042002280248210841141084808080002207450d1e2007"
            "200836020c20074107360200200720043602100c070b20022902dc02210e200228"
            "02d802210741042106410021090c070b200741dd00470d00200241c0006a200320"
            "052005200441016a220420052004491b10838080800041002d00c0cfc080001a20"
            "0228024421042002280240210841141084808080002207450d1c2007200836020c"
            "20074115360200200720043602100c050b200241b0016a20011081808080000240"
            "20022d00b00122084106470d0020022802b40121070c050b200241d4026a41026a"
            "2205200b41026a2d00003a0000200241c0026a41086a2203200a41086a29030037"
            "03002002200b2f00003b01d4022002200a2903003703c00220022802b401210702"
            "40200c20022802d802470d00200241d8026a1090808080000b20022802dc022209"
            "200c41186c6a220420022903c002370308200420083a0000200420022f01d4023b"
            "000120042007360204200441106a2003290300370300200441036a20052d00003a"
            "00002002200c41016a220c3602e0024100210d2001280214220420012802102205"
            "4f0d020c000b0b2004200136020c2004410536020020002004360204200041063a"
            "0000200420083602100c180b200628020021030b200241306a2003200520052004"
            "41016a220420052004491b10838080800041002d00c0cfc080001a200228023421"
            "042002280230210841141084808080002207450d172007200836020c2007410236"
            "0200200720043602100b200241d8026a109180808000024020022802d802220445"
            "0d002009200441186c1082808080000b200128020c210320012802142104200128"
            "0210210541062106410121090b200120012d001841016a3a001802400240200420"
            "054f0d0003400240024002400240024002400240200320046a2d00002208410c4a"
            "0d00200841776a4102490d060c010b02402008411f4a0d002008410d470d010c06"
            "0b20084120460d052008412c460d01200841dd00460d020b200241106a20032005"
            "2005200441016a220420052004491b10838080800041002d00c0cfc080001a2002"
            "28021421082002280210210541141084808080002204450d1d200441163602000c"
            "070b2001200441016a2204360214200420054f0d020340200320046a2d0000220c"
            "41776a220841174b0d024101200874419380800471450d022001200441016a2204"
            "36021420052004470d000b200521040c020b2001200441016a3602142002200e37"
            "03b801200220073602b401200220063a00b00102402009450d00200241063a0078"
            "2002200736027c0c180b200241f8006a41106a200241b0016a41106a2903003703"
            "00200241f8006a41086a200241b0016a41086a290300370300200220022903b001"
            "3703780c170b200c41dd00470d00200241286a200320052005200441016a220420"
            "052004491b10838080800041002d00c0cfc080001a200228022c21082002280228"
            "210541141084808080002204450d1a200441153602000c040b200241206a200320"
            "052005200441016a220420052004491b10838080800041002d00c0cfc080001a20"
            "0228022421082002280220210541141084808080002204450d1920044116360200"
            "0c030b2001200441016a220436021420052004470d000b200521040b200241186a"
            "200320052005200441016a220420052004491b10838080800041002d00c0cfc080"
            "001a200228021c21082002280218210541141084808080002204450d1620044102"
            "3602000b2004200536020c20042008360210200220043602c8012002200e3703b8"
            "01200220073602b401200220063a00b001024020090d00200241063a0078200220"
            "0436027c200241b0016a1085808080000c120b200241063a00782002200736027c"
            "200241c8016a1092808080000c110b200241086a20032005200520042005200449"
            "1b10838080800041002d00c0cfc080001a200228020c2108200228020821014114"
            "1084808080002204450d142004200136020c200441183602002000200436020420"
            "0041063a0000200420083602100c130b200141003602082001200441016a360214"
            "200241b0016a2006200110938080800020022802b40121080240024020022802b0"
            "0122054102460d0020022802b8012104024020050d00200241f8006a2008200410"
            "948080800020022d00784106460d1320002002290378370300200041106a200241"
            "f8006a41106a290300370300200041086a200241f8006a41086a2903003703000c"
            "150b41002101024020044100480d00024020040d0041012101410021050c030b41"
            "002d00c0cfc080001a20042105200410848080800022010d02410121010b200120"
            "04109580808000000b200041063a0000200020083602040c130b200241f8006a41"
            "086a220320012008200410df808080003602002002200536027c200241033a0078"
            "2002200436028401200041106a200241f8006a41106a290300370300200041086a"
            "2003290300370300200020022903783703000c120b2001200441016a3602142002"
            "4190016a20014100108d8080800002402002290390014203510d00200241f8006a"
            "20024190016a109680808000024020022d00784106460d00200020022903783703"
            "00200041106a200241f8006a41106a290300370300200041086a200241f8006a41"
            "086a2903003703000c130b200228027c2001108f808080002104200041063a0000"
            "200020043602040c120b2000200228029801360204200041063a00000c110b0240"
            "200741f3004a0d00200741e600460d04200741ee00470d012001200441016a3602"
            "14200141bb80c0800041031086808080002204450d02200041063a000020002004"
            "3602040c110b200741f400460d02200741fb00460d040b200741506a41ff017141"
            "0a490d042002200320052005200441016a220420052004491b1083808080004100"
            "2d00c0cfc080001a20022802042108200228020021054114108480808000220445"
            "0d102004200536020c2004410a360200200420083602102002200436027c0c0d0b"
            "200241003a007820002002290378370300200041086a200241f8006a41086a2903"
            "00370300200041106a200241f8006a41106a2903003703000c0e0b200120044101"
            "6a3602140240200141be80c0800041031086808080002204450d00200041063a00"
            "00200020043602040c0e0b20024181023b01782000200229037837030020004108"
            "6a200241f8006a41086a290300370300200041106a200241f8006a41106a290300"
            "3703000c0d0b2001200441016a3602140240200141c180c0800041041086808080"
            "002204450d00200041063a0000200020043602040c0d0b200241013b0178200020"
            "02290378370300200041086a200241f8006a41086a290300370300200041106a20"
            "0241f8006a41106a2903003703000c0c0b200120012d0018417f6a22083a001820"
            "0441016a2104200841ff0171450d0720012004360214200241013a00d801200220"
            "013602d401200241b0016a200241d4016a10978080800002400240024020022d00"
            "b0010d004105210620022d00b1010d01410021074200210e0c020b20022802b401"
            "21070c070b20022802d401220441003602082004200428021441016a3602142002"
            "41b0016a2004410c6a200410938080800020022802b401210720022802b0014102"
            "460d06200241d8026a200720022802b801109880808000024020022802d8022204"
            "418080808078470d0020022802dc0221070c070b20022802dc0221080240200441"
            "8180808078470d00200821070c070b20022802e0022105200241003602e4012002"
            "41003602dc01200220053602e002200220083602dc02200220043602d802200241"
            "e8016a200241d4016a10998080800020022d00e8014106460d04200241b0016a20"
            "0241dc016a200241d8026a200241e8016a109a80808000024020022d00b0014106"
            "460d00200241b0016a1085808080000b20024180026a41046a2108200241b0016a"
            "41046a21050340200241b0016a200241d4016a10978080800020022d00b0010d03"
            "024020022d00b101450d0020022802d40122044100360208200420042802144101"
            "6a360214200241b0016a2004410c6a200410938080800020022802b40121072002"
            "2802b0014102460d07200241f4026a200720022802b80110988080800002402002"
            "2802f4022204418080808078470d0020022802f80221070c080b20022802f80221"
            "072004418180808078460d0720022802fc022103200241b0016a200241d4016a10"
            "9980808000024020022d00b0014106470d0020022802b401210802402004450d00"
            "200720041082808080000b200821070c080b200241d8026a41106a200241b0016a"
            "41106a2209290300220e370300200241d8026a41086a200241b0016a41086a220c"
            "290300220f370300200220022903b00122103703d802200541106a200e37020020"
            "0541086a200f3702002005201037020020024180026a41086a200c290200370300"
            "20024180026a41106a200929020037030020024180026a41186a200241b0016a41"
            "186a280200360200200220022902b00137038002200220033602a4022002200736"
            "02a0022002200436029c02200241a8026a41106a200841106a2902003703002002"
            "41a8026a41086a200841086a290200370300200220082902003703a802200241b0"
            "016a200241dc016a2002419c026a200241a8026a109a8080800020022d00b00141"
            "06460d01200241b0016a1085808080000c010b0b20022802dc01210720022902e0"
            "01210e0b410021090c060b200241a0016a20014101108d8080800020022903a001"
            "4203510d01200241f8006a200241a0016a109680808000024020022d0078410646"
            "0d0020002002290378370300200041106a200241f8006a41106a29030037030020"
            "0041086a200241f8006a41086a2903003703000c0b0b200228027c2001108f8080"
            "80002104200041063a0000200020043602040c0a0b20022802b40121070c020b20"
            "0020022802a801360204200041063a00000c080b20022802ec0121072004450d00"
            "200820041082808080000b200241dc016a109b808080000b41062106410121090b"
            "200120012d001841016a3a0018200128020c210302400240024020012802142204"
            "200128021022054f0d00034002400240200320046a2d00002208410c4a0d002008"
            "41776a4102490d010c040b02402008411f4a0d002008410d470d040c010b200841"
            "20460d0002402008412c460d00200841fd00470d042001200441016a3602144100"
            "21040c050b200241e8006a200320052005200441016a220420052004491b108380"
            "80800041002d00c0cfc080001a200228026c210820022802682105411410848080"
            "80002204450d0a2004200536020c20044115360200200420083602100c040b2001"
            "200441016a220436021420052004470d000b200521040b200241e0006a20032005"
            "2005200441016a220420052004491b10838080800041002d00c0cfc080001a2002"
            "28026421082002280260210541141084808080002204450d072004200536020c20"
            "044103360200200420083602100c010b200241d8006a200320052005200441016a"
            "220420052004491b10838080800041002d00c0cfc080001a200228025c21082002"
            "280258210541141084808080002204450d062004200536020c2004411636020020"
            "0420083602100b200220063a00b001200220022f00d8023b00b101200220043602"
            "c8012002200e3703b801200220073602b4012002200241da026a2d00003a00b301"
            "024020090d00024020040d00200241f8006a41106a200241b0016a41106a290300"
            "370300200241f8006a41086a200241b0016a41086a290300370300200220022903"
            "b0013703780c030b200241063a00782002200436027c200241b0016a1085808080"
            "000c020b200241063a00782002200736027c2004450d01200241c8016a10928080"
            "80000c010b200241d0006a200320052005200420052004491b1083808080004100"
            "2d00c0cfc080001a20022802542108200228025021014114108480808000220445"
            "0d042004200136020c2004411836020020002004360204200041063a0000200420"
            "083602100c030b20022d00784106470d010b200228027c2001108f808080002104"
            "200041063a0000200020043602040c010b20002002290378370300200041106a20"
            "0241f8006a41106a290300370300200041086a200241f8006a41086a2903003703"
            "000b20024180036a2480808080000f0b000b7001027f024002402000417c6a2802"
            "002202417871220341044108200241037122021b20016a490d0002402002450d00"
            "2003200141276a4b0d020b200010a5808080000f0b41c9c5c08000412e41f8c5c0"
            "800010a680808000000b4188c6c08000412e41b8c6c0800010a680808000000be6"
            "0301057f02400240024002400240024020022003490d0041012104410021052003"
            "4101480d04200120036a21060240200341034b0d000340200620014d0d06200641"
            "7f6a22062d0000410a470d000c050b0b024041808284082006417c6a2800002207"
            "418a94a8d000736b20077241808182847871418081828478460d00034020062001"
            "4d0d062006417f6a22062d0000410a470d000c050b0b200320064103716b210720"
            "034109490d0103400240024020074108480d004180828408200120076a22064178"
            "6a2802002208418a94a8d000736b20087241808182847871418081828478460d01"
            "0b200120076a21060c040b200741786a210741808284082006417c6a2802002208"
            "418a94a8d000736b20087241808182847871418081828478460d000c030b0b2003"
            "20024188bcc0800010b180808000000b200120076a21060340200620014d0d0320"
            "06417f6a22062d0000410a470d000c020b0b0340200620014d0d022006417f6a22"
            "062d0000410a470d000b0b200620016b41016a220520024b0d010b024020012005"
            "6a20014d0d0041002106200521070340200620012d0000410a466a210620014101"
            "6a21012007417f6a22070d000b200641016a21040b200020043602002000200320"
            "056b3602040f0b200520024198bcc0800010b180808000000bc12502087f017e02"
            "400240024002400240024002400240200041f4014b0d0041002802e4d2c0800022"
            "0141102000410b6a41f803712000410b491b220241037622037622004103710d01"
            "200241002802ecd2c080004d0d0720000d0241002802e8d2c0800022000d030c07"
            "0b2000410b6a2203417871210241002802e8d2c080002204450d06411f21050240"
            "200041f4ffff074b0d002002410620034108766722006b7641017120004101746b"
            "413e6a21050b410020026b21030240200541027441cccfc080006a28020022010d"
            "0041002100410021060c040b4100210020024100411920054101766b2005411f46"
            "1b74210741002106034002402001220128020441787122082002490d0020082002"
            "6b220820034f0d00200821032001210620080d004100210320012106200121000c"
            "060b200128021422082000200820012007411d764104716a41106a280200220147"
            "1b200020081b2100200741017421072001450d040c000b0b024002402000417f73"
            "41017120036a2207410374220041dcd0c080006a2202200041e4d0c080006a2802"
            "0022032802082206460d002006200236020c200220063602080c010b4100200141"
            "7e200777713602e4d2c080000b20032000410372360204200320006a2200200028"
            "0204410172360204200341086a0f0b024002402000200374410220037422004100"
            "20006b7271682208410374220341dcd0c080006a2206200341e4d0c080006a2802"
            "0022002802082207460d002007200636020c200620073602080c010b4100200141"
            "7e200877713602e4d2c080000b20002002410372360204200020026a2207200320"
            "026b2202410172360204200020036a2002360200024041002802ecd2c080002201"
            "450d00200141787141dcd0c080006a210641002802f4d2c0800021030240024041"
            "002802e4d2c08000220841012001410376742201710d00410020082001723602e4"
            "d2c08000200621010c010b200628020821010b200620033602082001200336020c"
            "2003200636020c200320013602080b410020073602f4d2c08000410020023602ec"
            "d2c08000200041086a0f0b20006841027441cccfc080006a280200220628020441"
            "787120026b2103200621010240024003400240200628021022000d002006280214"
            "22000d0020012802182105024002400240200128020c22002001470d0020014114"
            "4110200128021422001b6a28020022060d01410021000c020b2001280208220620"
            "0036020c200020063602080c010b200141146a200141106a20001b210703402007"
            "21082006220041146a200041106a200028021422061b210720004114411020061b"
            "6a28020022060d000b200841003602000b2005450d030240200128021c41027441"
            "cccfc080006a22062802002001460d0020054110411420052802102001461b6a20"
            "003602002000450d040c030b2006200036020020000d02410041002802e8d2c080"
            "00417e200128021c77713602e8d2c080000c030b200028020441787120026b2206"
            "2003200620034922061b21032000200120061b2101200021060c000b0b20002005"
            "360218024020012802102206450d0020002006360210200620003602180b200128"
            "02142206450d0020002006360214200620003602180b0240024002402003411049"
            "0d0020012002410372360204200120026a22022003410172360204200220036a20"
            "0336020041002802ecd2c080002207450d01200741787141dcd0c080006a210641"
            "002802f4d2c0800021000240024041002802e4d2c0800022084101200741037674"
            "2207710d00410020082007723602e4d2c08000200621070c010b20062802082107"
            "0b200620003602082007200036020c2000200636020c200020073602080c010b20"
            "01200320026a2200410372360204200120006a220020002802044101723602040c"
            "010b410020023602f4d2c08000410020033602ecd2c080000b200141086a0f0b02"
            "4020002006720d004100210641022005742200410020006b722004712200450d03"
            "20006841027441cccfc080006a28020021000b2000450d010b0340200020062000"
            "280204417871220120026b220820034922051b2104200120024921072008200320"
            "051b21080240200028021022010d00200028021421010b2006200420071b210620"
            "03200820071b21032001210020010d000b0b2006450d00024041002802ecd2c080"
            "0022002002490d002003200020026b4f0d010b2006280218210502400240024020"
            "0628020c22002006470d00200641144110200628021422001b6a28020022010d01"
            "410021000c020b20062802082201200036020c200020013602080c010b20064114"
            "6a200641106a20001b21070340200721082001220041146a200041106a20002802"
            "1422011b210720004114411020011b6a28020022010d000b200841003602000b02"
            "402005450d0002400240200628021c41027441cccfc080006a2201280200200646"
            "0d0020054110411420052802102006461b6a20003602002000450d020c010b2001"
            "200036020020000d00410041002802e8d2c08000417e200628021c77713602e8d2"
            "c080000c010b20002005360218024020062802102201450d002000200136021020"
            "0120003602180b20062802142201450d0020002001360214200120003602180b02"
            "40024020034110490d0020062002410372360204200620026a2200200341017236"
            "0204200020036a200336020002402003418002490d002000200310d7808080000c"
            "020b200341f8017141dcd0c080006a21020240024041002802e4d2c08000220141"
            "012003410376742203710d00410020012003723602e4d2c08000200221030c010b"
            "200228020821030b200220003602082003200036020c2000200236020c20002003"
            "3602080c010b2006200320026a2200410372360204200620006a22002000280204"
            "4101723602040b200641086a0f0b024002400240024002400240024041002802ec"
            "d2c08000220020024f0d00024041002802f0d2c08000220020024b0d0041002100"
            "200241af80046a220641107640002203417f4622070d0720034110742201450d07"
            "410041002802fcd2c08000410020064180807c7120071b22086a22003602fcd2c0"
            "800041004100280280d3c0800022032000200320004b1b360280d3c08000024002"
            "40024041002802f8d2c080002203450d0041ccd0c0800021000340200028020022"
            "06200028020422076a2001460d02200028020822000d000c030b0b024002404100"
            "280288d3c080002200450d00200020014d0d010b41002001360288d3c080000b41"
            "0041ff1f36028cd3c08000410020083602d0d0c08000410020013602ccd0c08000"
            "410041dcd0c080003602e8d0c08000410041e4d0c080003602f0d0c08000410041"
            "dcd0c080003602e4d0c08000410041ecd0c080003602f8d0c08000410041e4d0c0"
            "80003602ecd0c08000410041f4d0c08000360280d1c08000410041ecd0c0800036"
            "02f4d0c08000410041fcd0c08000360288d1c08000410041f4d0c080003602fcd0"
            "c0800041004184d1c08000360290d1c08000410041fcd0c08000360284d1c08000"
            "4100418cd1c08000360298d1c0800041004184d1c0800036028cd1c08000410041"
            "94d1c080003602a0d1c080004100418cd1c08000360294d1c08000410041003602"
            "d8d0c080004100419cd1c080003602a8d1c0800041004194d1c0800036029cd1c0"
            "80004100419cd1c080003602a4d1c08000410041a4d1c080003602b0d1c0800041"
            "0041a4d1c080003602acd1c08000410041acd1c080003602b8d1c08000410041ac"
            "d1c080003602b4d1c08000410041b4d1c080003602c0d1c08000410041b4d1c080"
            "003602bcd1c08000410041bcd1c080003602c8d1c08000410041bcd1c080003602"
            "c4d1c08000410041c4d1c080003602d0d1c08000410041c4d1c080003602ccd1c0"
            "8000410041ccd1c080003602d8d1c08000410041ccd1c080003602d4d1c0800041"
            "0041d4d1c080003602e0d1c08000410041d4d1c080003602dcd1c08000410041dc"
            "d1c080003602e8d1c08000410041e4d1c080003602f0d1c08000410041dcd1c080"
            "003602e4d1c08000410041ecd1c080003602f8d1c08000410041e4d1c080003602"
            "ecd1c08000410041f4d1c08000360280d2c08000410041ecd1c080003602f4d1c0"
            "8000410041fcd1c08000360288d2c08000410041f4d1c080003602fcd1c0800041"
            "004184d2c08000360290d2c08000410041fcd1c08000360284d2c080004100418c"
            "d2c08000360298d2c0800041004184d2c0800036028cd2c0800041004194d2c080"
            "003602a0d2c080004100418cd2c08000360294d2c080004100419cd2c080003602"
            "a8d2c0800041004194d2c0800036029cd2c08000410041a4d2c080003602b0d2c0"
            "80004100419cd2c080003602a4d2c08000410041acd2c080003602b8d2c0800041"
            "0041a4d2c080003602acd2c08000410041b4d2c080003602c0d2c08000410041ac"
            "d2c080003602b4d2c08000410041bcd2c080003602c8d2c08000410041b4d2c080"
            "003602bcd2c08000410041c4d2c080003602d0d2c08000410041bcd2c080003602"
            "c4d2c08000410041ccd2c080003602d8d2c08000410041c4d2c080003602ccd2c0"
            "8000410041d4d2c080003602e0d2c08000410041ccd2c080003602d4d2c0800041"
            "0020013602f8d2c08000410041d4d2c080003602dcd2c080004100200841586a22"
            "003602f0d2c0800020012000410172360204200120006a41283602044100418080"
            "8001360284d3c080000c080b200320014f0d00200620034b0d00200028020c450d"
            "030b41004100280288d3c080002200200120002001491b360288d3c08000200120"
            "086a210641ccd0c0800021000240024002400340200028020022072006460d0120"
            "0028020822000d000c020b0b200028020c450d010b41ccd0c08000210002400340"
            "02402000280200220620034b0d002003200620002802046a2206490d020b200028"
            "020821000c000b0b410020013602f8d2c080004100200841586a22003602f0d2c0"
            "800020012000410172360204200120006a412836020441004180808001360284d3"
            "c080002003200641606a41787141786a22002000200341106a491b2207411b3602"
            "0441002902ccd0c080002109200741106a41002902d4d0c0800037020020072009"
            "370208410020083602d0d0c08000410020013602ccd0c080004100200741086a36"
            "02d4d0c08000410041003602d8d0c080002007411c6a2100034020004107360200"
            "200041046a22002006490d000b20072003460d0720072007280204417e71360204"
            "2003200720036b22004101723602042007200036020002402000418002490d0020"
            "03200010d7808080000c080b200041f8017141dcd0c080006a2106024002404100"
            "2802e4d2c08000220141012000410376742200710d00410020012000723602e4d2"
            "c08000200621000c010b200628020821000b200620033602082000200336020c20"
            "03200636020c200320003602080c070b200020013602002000200028020420086a"
            "360204200120024103723602042007410f6a41787141786a2206200120026a2200"
            "6b2103200641002802f8d2c08000460d03200641002802f4d2c08000460d040240"
            "200628020422024103714101470d0020062002417871220210a880808000200220"
            "036a2103200620026a220628020421020b20062002417e71360204200020034101"
            "72360204200020036a200336020002402003418002490d002000200310d7808080"
            "000c060b200341f8017141dcd0c080006a21020240024041002802e4d2c0800022"
            "0641012003410376742203710d00410020062003723602e4d2c08000200221030c"
            "010b200228020821030b200220003602082003200036020c2000200236020c2000"
            "20033602080c050b4100200020026b22033602f0d2c08000410041002802f8d2c0"
            "8000220020026a22063602f8d2c080002006200341017236020420002002410372"
            "360204200041086a21000c060b41002802f4d2c08000210302400240200020026b"
            "2206410f4b0d00410041003602f4d2c08000410041003602ecd2c0800020032000"
            "410372360204200320006a220020002802044101723602040c010b410020063602"
            "ecd2c080004100200320026a22013602f4d2c08000200120064101723602042003"
            "20006a2006360200200320024103723602040b200341086a0f0b2000200720086a"
            "360204410041002802f8d2c080002200410f6a417871220341786a22063602f8d2"
            "c080004100200020036b41002802f0d2c0800020086a22036a41086a22013602f0"
            "d2c0800020062001410172360204200020036a4128360204410041808080013602"
            "84d3c080000c030b410020003602f8d2c08000410041002802f0d2c0800020036a"
            "22033602f0d2c08000200020034101723602040c010b410020003602f4d2c08000"
            "410041002802ecd2c0800020036a22033602ecd2c0800020002003410172360204"
            "200020036a20033602000b200141086a0f0b4100210041002802f0d2c080002203"
            "20024d0d004100200320026b22033602f0d2c08000410041002802f8d2c0800022"
            "0020026a22063602f8d2c080002006200341017236020420002002410372360204"
            "200041086a0f0b20000b6801017f024002400240024020002d00000e0503030301"
            "02000b200041046a109b808080000c020b20002802042201450d01200028020820"
            "011082808080000f0b200041046a10918080800020002802042201450d00200028"
            "0208200141186c1082808080000f0b0ba20201087f23808080800041106b220324"
            "80808080002000280214220420002802102205200420054b1b2106200028020c21"
            "0702400240024002400340024020020d00410021040c050b20062004460d012000"
            "200441016a22083602142002417f6a2102200720046a210920012d0000210a2008"
            "2104200141016a2101200a20092d0000460d000b200341086a2007200520081083"
            "8080800041002d00c0cfc080001a200328020c2101200328020821024114108480"
            "8080002204450d01200441093602000c020b200320072005200610838080800041"
            "002d00c0cfc080001a200328020421012003280200210241141084808080002204"
            "450d00200441053602000c010b000b2004200236020c200420013602100b200341"
            "106a24808080800020040b970202027f027e23808080800041106b220424808080"
            "8000024002400240024002400240024002402001280214220520012802104f0d00"
            "200128020c20056a2d00002205412e460d01200541c500460d02200541e500460d"
            "020b2002450d02420121060c050b20042001200220034100108880808000200428"
            "02000d020c030b200420012002200341001089808080002004280200450d022000"
            "2004280204360208200042033703000c040b420021060240420020037d22074200"
            "590d0042022106200721030c030b2003babd428080808080808080807f8421030c"
            "020b20002004280204360208200042033703000c020b2004290308210342002106"
            "0b20002003370308200020063703000b200441106a2480808080000bfa0301097f"
            "23808080800041106b220524808080800020012001280214220641016a22073602"
            "140240024002402007200128021022084f0d00200720086b2109200128020c210a"
            "410021060240024003400240200a20076a2d0000220b41506a220c41ff0171220d"
            "410a490d00024020060d002005200a20082008200741016a220720082007491b10"
            "838080800041002d00c0cfc080001a200528020421062005280200210c41141084"
            "808080002207450d072007200c36020c2007410d36020020002007360204200041"
            "01360200200720063602100c060b200620046a2107200b41207241e500470d0320"
            "0020012002200320071089808080000c050b024020034298b3e6cc99b3e6cc1958"
            "0d0020034299b3e6cc99b3e6cc19520d02200d41054b0d020b2001200741016a22"
            "073602142006417f6a21062003420a7e200cad42ff01837c210320072008470d00"
            "0b200920046a21070c010b2000200120022003200620046a108a808080000c020b"
            "20002001200220032007108b808080000c010b200541086a200128020c20082008"
            "200641026a220720082007491b10838080800041002d00c0cfc080001a20052802"
            "0c21062005280208210c41141084808080002207450d012007200c36020c200741"
            "053602002000200736020420004101360200200720063602100b200541106a2480"
            "808080000f0b000bb80401077f23808080800041106b2205248080808000410121"
            "0620012001280214220741016a220836021402402008200128021022094f0d0041"
            "01210602400240200128020c20086a2d000041556a0e03010200020b410021060b"
            "2001200741026a22083602140b200128020c210a02400240024002400240024002"
            "40200820094f0d002001200841016a2207360214200a20086a2d000041506a41ff"
            "01712208410a4f0d010240200720094f0d000340200a20076a2d000041506a41ff"
            "0171220b410a4f0d012001200741016a22073602140240200841cb99b3e6004c0d"
            "00200841cc99b3e600470d07200b41074b0d070b2008410a6c200b6a2108200920"
            "07470d000b0b20060d02200420086b2207411f7541808080807873200720084100"
            "4a2007200448731b21070c030b200541086a200a2009200810838080800041002d"
            "00c0cfc080001a200528020c21012005280208210841141084808080002207450d"
            "042007200836020c20074105360200200020073602042000410136020020072001"
            "3602100c050b2005200a2009200710838080800041002d00c0cfc080001a200528"
            "020421012005280200210841141084808080002207450d032007200836020c2007"
            "410d3602002000200736020420004101360200200720013602100c040b20042008"
            "6a2207411f7541808080807873200720084100482007200448731b21070b200020"
            "01200220032007108b808080000c020b2000200120022003502006108c80808000"
            "0c010b000b200541106a2480808080000b7f01047f024002402001280214220520"
            "0128021022064f0d00200128020c210702400340200720056a2d0000220841506a"
            "41ff017141094b0d012001200541016a220536021420062005470d000c020b0b20"
            "0841207241e500460d010b20002001200220032004108b808080000f0b20002001"
            "2002200320041089808080000b9f0304017f017c017f017c23808080800041106b"
            "22052480808080002003ba2106024002400240024002400240024020042004411f"
            "7522077320076b220741b502490d0003402006440000000000000000610d062004"
            "417f4a0d02200644a0c8eb85f3cce17fa32106200441b4026a22042004411f7522"
            "077320076b220741b4024b0d000b0b200741037441f0a6c080006a2b0300210820"
            "04417f4a0d0120062008a321060c040b2005200128020c20012802102001280214"
            "10838080800041002d00c0cfc080001a2005280204210720052802002101411410"
            "84808080002204450d022004200136020c2004410e360200200020043602042004"
            "20073602100c010b20062008a222069944000000000000f07f620d02200541086a"
            "200128020c2001280210200128021410838080800041002d00c0cfc080001a2005"
            "28020c21072005280208210141141084808080002204450d012004200136020c20"
            "04410e36020020002004360204200420073602100b410121040c020b000b200020"
            "0620069a20021b390308410021040b20002004360200200541106a248080808000"
            "0b840201027f23808080800041106b220524808080800002400240024002402004"
            "450d002003450d010b20012802142204200128021022034f0d01200128020c2106"
            "0340200620046a2d000041506a41ff0171410a4f0d022001200441016a22043602"
            "1420032004470d000c020b0b200541086a200128020c2001280210200128021410"
            "838080800041002d00c0cfc080001a200528020c21012005280208210302404114"
            "1084808080002204450d002004200336020c2004410e3602002000200436020420"
            "042001360210410121040c020b000b200044000000000000000044000000000000"
            "008020021b390308410021040b20002004360200200541106a2480808080000b95"
            "0502067f017e23808080800041306b2203248080808000200128020c2104024002"
            "40024002400240024020012802142205200128021022064f0d002001200541016a"
            "2207360214200420056a2d000022084130470d020240200720064f0d0020042007"
            "6a2d000041506a41ff0171410a490d020b20002001200242001087808080000c05"
            "0b200341186a20042006200510838080800041002d00c0cfc080001a200328021c"
            "21072003280218210441141084808080002201450d022001200436020c20014105"
            "3602002000200136020820004203370300200120073602100c040b200341086a20"
            "0420062006200541026a220120062001491b10838080800041002d00c0cfc08000"
            "1a200328020c21072003280208210441141084808080002201450d012001200436"
            "020c2001410d3602002000200136020820004203370300200120073602100c030b"
            "02402008414f6a41ff01714109490d00200341106a200420062007108380808000"
            "41002d00c0cfc080001a2003280214210720032802102104411410848080800022"
            "01450d012001200436020c2001410d360200200020013602082000420337030020"
            "0120073602100c030b200841506aad42ff01832109200720064f0d010340200420"
            "076a2d000041506a220541ff01712208410a4f0d020240024020094299b3e6cc99"
            "b3e6cc19540d0020094299b3e6cc99b3e6cc19520d01200841054b0d010b200120"
            "0741016a22073602142009420a7e2005ad42ff01837c210920062007470d010c03"
            "0b0b200341206a200120022009108e808080000240024020032802200d00200020"
            "032b0328390308420021090c010b20002003280224360208420321090b20002009"
            "3703000c020b000b20002001200220091087808080000b200341306a2480808080"
            "000bbd0101057f410021040240024020012802102205200128021422064d0d0020"
            "0641016a2107200520066b2108200128020c20066a210541002104034002402005"
            "20046a2d0000220641506a41ff0171410a490d002006412e460d030240200641c5"
            "00460d00200641e500470d030b200020012002200320041089808080000f0b2001"
            "200720046a3602142008200441016a2204470d000b200821040b20002001200220"
            "032004108b808080000f0b200020012002200320041088808080000bc80101047f"
            "23808080800041206b2202248080808000024002400240200028020c450d002000"
            "21010c010b200241106a41086a2203200041086a28020036020020022000290200"
            "370310200241086a200128020c2001280210200128021410838080800041002d00"
            "c0cfc080001a200228020c21042002280208210541141084808080002201450d01"
            "200120022903103702002001200536020c20012004360210200141086a20032802"
            "00360200200041141082808080000b200241206a24808080800020010f0b000b59"
            "01017f23808080800041106b2201248080808000200141086a2000200028020041"
            "014108411810d480808000024020012802082200418180808078460d0020002001"
            "28020c109580808000000b200141106a2480808080000b950101027f0240200028"
            "02082201450d00200028020441046a2100034002400240024002402000417c6a2d"
            "00000e050303030102000b2000109b808080000c020b20002802002202450d0120"
            "0041046a28020020021082808080000c010b200010918080800020002802002202"
            "450d00200041046a280200200241186c1082808080000b200041186a2100200141"
            "7f6a22010d000b0b0b970101047f024002400240200028020022002802000e0200"
            "01020b20002802082201450d01200028020420011082808080000c010b20002d00"
            "044103470d00200028020822012802002102024020012802042203280200220445"
            "0d002002200411808080800080808080000b024020032802042203450d00200220"
            "031082808080000b2001410c1082808080000b200041141082808080000b9b0d02"
            "097f017e23808080800041306b2203248080808000024002400240024002400240"
            "0240024002400340024002402001280208220420012802042205460d0002400240"
            "0240200420054f0d002001280200220620046a2d000022074122460d01200741dc"
            "00460d0120074120490d012006200441016a22086a21094100200520086b417871"
            "220a6b210703402009210b024020070d002001200a20086a360208200110c88080"
            "800020012802042105200128020821070c040b200741086a2107200b41086a2109"
            "200b290000220c42a2c48891a2c48891228542fffdfbf7efdfbfff7e7c200c42e0"
            "bffffefdfbf7ef5f7c84200c42dcb8f1e2c58b97aedc008542fffdfbf7efdfbfff"
            "7e7c84200c427f858342808182848890a0c0807f83220c500d000b2001200b2006"
            "6b200c7aa74103766a22073602080c020b2004200541a8bcc0800010ac80808000"
            "0c080b200421070b20072005470d01200521040b200341086a2001280200200420"
            "0410838080800041002d00c0cfc080001a200328020c210b200328020821094114"
            "1084808080002207450d052007200936020c200741043602002000200736020420"
            "0041023602002007200b3602100c0a0b024020072005490d002007200541b8bcc0"
            "800010ac80808000000b02402001280200220b20076a2d0000220941dc00460d00"
            "024020094122470d002002280208450d0520072004490d072002200b20046a2007"
            "20046b10c9808080004101210b2001200741016a360208200341286a2001200228"
            "0204200228020810ca8080800020032802282207450d032000200328022c360208"
            "0c040b2001200741016a2207360208200341106a200b2005200710838080800041"
            "002d00c0cfc080001a2003280214210b2003280210210941141084808080002207"
            "450d052007200936020c2007411036020020002007360204200041023602002007"
            "200b3602100c0a0b024020072004490d002002200b20046a200720046b10c98080"
            "80002001200741016a2209360208024020092005490d00200341206a200b200520"
            "0910838080800041002d00c0cfc080001a2003280224210b200328022021094114"
            "1084808080002207450d06200741043602000c090b2001200741026a2204360208"
            "02400240024002400240024002400240024002400240200b20096a2d0000220741"
            "ed004a0d000240200741e1004a0d0020074122460d032007412f460d04200741dc"
            "00470d020240200228020822072002280200470d00200210cb808080000b200220"
            "0741016a360208200228020420076a41dc003a0000410021070c0b0b2007419e7f"
            "6a0e050401010105010b200741927f6a0e080500000006000708000b200341186a"
            "200b2005200410838080800041002d00c0cfc080001a200328021c210b20032802"
            "18210941141084808080002207450d0e2007410c3602000c110b02402002280208"
            "22072002280200470d00200210cb808080000b2002200741016a36020820022802"
            "0420076a41223a0000410021070c070b0240200228020822072002280200470d00"
            "200210cb808080000b2002200741016a360208200228020420076a412f3a000041"
            "0021070c060b0240200228020822072002280200470d00200210cb808080000b20"
            "02200741016a360208200228020420076a41083a0000410021070c050b02402002"
            "28020822072002280200470d00200210cb808080000b2002200741016a36020820"
            "0228020420076a410c3a0000410021070c040b0240200228020822072002280200"
            "470d00200210cb808080000b2002200741016a360208200228020420076a410a3a"
            "0000410021070c030b0240200228020822072002280200470d00200210cb808080"
            "000b2002200741016a360208200228020420076a410d3a0000410021070c020b02"
            "40200228020822072002280200470d00200210cb808080000b2002200741016a36"
            "0208200228020420076a41093a0000410021070c010b2001200210cc8080800021"
            "070b2007450d010c090b0b2004200741e8bcc0800010b780808000000b4102210b"
            "200328022c21070b2000200b360200200020073602040c060b20072004490d0220"
            "01200741016a360208200341286a2001200b20046a200720046b10ca8080800002"
            "40024020032802282207450d002000200328022c3602084100210b0c010b410221"
            "0b200328022c21070b2000200b360200200020073602040c050b000b2004200741"
            "c8bcc0800010b780808000000b2004200741d8bcc0800010b780808000000b2007"
            "200936020c2007200b3602100b20004102360200200020073602040b200341306a"
            "2480808080000b7901027f410021030240024020024100480d00024020020d0041"
            "002103410121040c020b41002d00c0cfc080001a20022103200210848080800022"
            "040d01410121030b20032002109580808000000b20042001200210df8080800021"
            "012000200236020c2000200136020820002003360204200041033a00000b100002"
            "4020000d0010a3808080000b000b870102017c017e02400240024020012802000e"
            "03000102000b20004202370308200020012b0308220239031020002002bd42ffff"
            "ffffffffffffff00834280808080808080f8ff00534101743a00000f0b20004200"
            "370308200041023a0000200020012903083703100f0b200041023a000020002001"
            "290308220337031020002003423f883703080bb40701077f23808080800041306b"
            "22022480808080002001280200220328020c210402400240024002402003280214"
            "2205200341106a28020022064f0d000340200420056a2d0000220741776a220841"
            "174b0d024101200874419380800471450d022003200541016a2205360214200620"
            "05470d000b200621050b41012108200241286a200420062006200541016a220520"
            "062005491b10838080800041002d00c0cfc080001a200228022c21062002280228"
            "210341141084808080002205450d022005200336020c2005410336020020002005"
            "360204200520063602100c010b0240200741fd00470d0041002108200041003a00"
            "010c010b02400240024020012d00040d00200541016a21052007412c470d012003"
            "20053602140240200520064f0d00034002400240024002400240200420056a2d00"
            "002208410c4a0d00200841776a41024f0d010c040b0240200841606a0e03040102"
            "000b2008410d460d03200841fd00460d020b41012108200241086a200420062006"
            "200541016a220520062005491b10838080800041002d00c0cfc080001a20022802"
            "0c21062002280208210341141084808080002205450d092005200336020c200541"
            "1136020020002005360204200520063602100c080b200041013a0001410021080c"
            "070b41012108200241186a200420062006200541016a220520062005491b108380"
            "80800041002d00c0cfc080001a200228021c210620022802182103411410848080"
            "80002205450d072005200336020c20054115360200200020053602042005200636"
            "02100c060b2003200541016a220536021420062005470d000b200621050b410121"
            "08200241106a200420062006200541016a220520062005491b1083808080004100"
            "2d00c0cfc080001a20022802142106200228021021034114108480808000220545"
            "0d042005200336020c2005410536020020002005360204200520063602100c030b"
            "41002108200141003a0004024020074122460d002002200420062006200541016a"
            "220520062005491b10838080800041002d00c0cfc080001a200228020421082002"
            "280200210641141084808080002205450d042005200636020c2005411136020020"
            "002005360204200520083602100c020b200041013a00010c020b200241206a2004"
            "20062006200520062005491b10838080800041002d00c0cfc080001a2002280224"
            "21082002280220210641141084808080002205450d022005200636020c20054108"
            "36020020002005360204200520083602100b410121080b200020083a0000200241"
            "306a2480808080000f0b000b7201027f410021030240024020024100480d000240"
            "20020d0041002103410121040c020b41002d00c0cfc080001a2002210320021084"
            "8080800022040d01410121030b20032002109580808000000b20042001200210df"
            "8080800021012000200236020820002001360204200020033602000bdc0201067f"
            "23808080800041106b22022480808080002001280200220328020c210402400240"
            "02400240024020032802142201200341106a28020022054f0d000340200420016a"
            "2d0000220641776a220741174b0d024101200774419380800471450d0220032001"
            "41016a220136021420052001470d000b200521010b200241086a20042005200520"
            "0141016a220120052001491b10838080800041002d00c0cfc080001a200228020c"
            "21072002280208210541141084808080002201450d03200141033602000c010b02"
            "402006413a470d002003200141016a360214200020031081808080000c020b2002"
            "200420052005200141016a220120052001491b10838080800041002d00c0cfc080"
            "001a200228020421072002280200210541141084808080002201450d0220014106"
            "3602000b2001200536020c20002001360204200041063a0000200120073602100b"
            "200241106a2480808080000f0b000be411020b7f027e23808080800041c0016b22"
            "042480808080000240024002400240024002400240024002400240024002400240"
            "20012802002205450d002002280208210620022802042107200128020421080240"
            "03402005418c026a210920052f019203220a410c6c210b417f210c024002400340"
            "0240200b0d00200a210c0c020b2009280208210d2009280204210e200c41016a21"
            "0c200b41746a210b2009410c6a2109417f2007200e2006200d2006200d491b10dc"
            "80808000220e2006200d6b200e1b220d410047200d4100481b220d4101460d000b"
            "200d41ff0171450d010b2008450d022008417f6a21082005200c4102746a419803"
            "6a28020021050c010b0b20022802002209450d0c200720091082808080000c0c0b"
            "2002290204220fa721092002280200220b418080808078470d03200921050c010b"
            "2002290204220fa721052002280200220d418080808078470d010b2001210c0c09"
            "0b41002d00c0cfc080001a4198031084808080002209450d02200941013b019203"
            "20094100360288022009200f422088a7ad4220862005ad84370390022009200d36"
            "028c02200142808080801037020420012009360200200920032903003703002009"
            "41086a200341086a290300370300200941106a200341106a2903003703000c010b"
            "200f422088a7ad4220862009ad84210f0240024002400240024020052f01920322"
            "09410b490d00200441086a21084104210d200c4105490d03200c210d200c417b6a"
            "0e020302010b2005418c026a220e200c410c6c6a210d02400240200c41016a2206"
            "20094d0d00200d200f370204200d200b3602000c010b200e2006410c6c6a200d20"
            "09200c6b220e410c6c10de808080001a200d200f370204200d200b360200200520"
            "0641186c6a2005200c41186c6a200e41186c10de808080001a0b2005200c41186c"
            "6a220d41106a200341106a290300370300200d2003290300370300200d41086a20"
            "0341086a2903003703002005200941016a3b0192030c030b200c41796a210c2004"
            "41f8006a21084106210d0c010b4100210c200441f8006a21084105210d0b41002d"
            "00c0cfc080001a4198031084808080002209450d02200941003b01920320094100"
            "360288022009200d417f7320052f01920322076a22063b01920320044188016a41"
            "086a2005200d41186c6a220e41086a29030037030020044188016a41106a200e41"
            "106a2903003703002004200e290300370388012006410c4f0d032007200d41016a"
            "220e6b2006470d042005418c026a2202200d410c6c6a2207290204211020072802"
            "0021072009418c026a2002200e410c6c6a2006410c6c10df808080001a20092005"
            "200e41186c6a200641186c10df8080800021062005200d3b019203200441dc006a"
            "410c6a20044190016a290300370200200441f0006a20044198016a290300370200"
            "200420042903880137026020042005360208200420063602782008280200220d41"
            "8c026a200c410c6c6a210602400240200d2f019203220e200c4b0d002006200f37"
            "02042006200b3602000c010b2006410c6a2006200e200c6b2208410c6c10de8080"
            "80001a2006200f3702042006200b360200200d200c41186c6a220b41186a200b20"
            "0841186c10de808080001a0b200d200c41186c6a220b41106a200341106a290300"
            "370300200b2003290300370300200b41086a200341086a290300370300200d200e"
            "41016a3b0192032007418080808078460d00200441c4006a200441dc006a41086a"
            "290200370200200441cc006a200441dc006a41106a290200370200200441306a41"
            "246a200441dc006a41186a28020036020020042010370234200420073602302004"
            "200429025c37023c024002400240200528028802220b0d004100210c0c010b2004"
            "41306a4104722108200441b8016a210220044188016a4104722107200441b0016a"
            "2103200441c0006a210e4100210c4100210603402006200c470d0820052f019003"
            "210d200b2f019203410b490d02200641016a210602400240024002400240024002"
            "40200d4105490d00200d417b6a0e020203010b2004410436028001200420063602"
            "7c2004200b3602782003210b0c040b20044106360280012004200636027c200420"
            "0b360278200d41796a210d0c020b20044105360280012004200636027c2004200b"
            "36027820044188016a200441f8006a10c18080800020042802b001410520044130"
            "6a200e200910c080808000200428028801210d200441086a2007412410df808080"
            "001a0c030b20044105360280012004200636027c2004200b3602784100210d0b20"
            "02210b0b20044188016a200441f8006a10c180808000200b280200200d20044130"
            "6a200e200910c080808000200428028801210d200441086a2007412410df808080"
            "001a0b20042802bc01210c20042802b801210920042802b401210620042802b001"
            "2105200d418080808078460d032004200d3602302008200441086a412410df8080"
            "80001a200528028802220b0d000b0b2001280200220b450d0741002d00c0cfc080"
            "001a2001280204210641c803108480808000220d450d03200d200b36029803200d"
            "41003b019203200d410036028802200b41003b019003200b200d36028802200120"
            "0641016a3602042001200d3602002006200c470d08200d200429033037028c0220"
            "0d41013b019203200d2004290340370300200d200936029c03200d4194026a2004"
            "41306a41086a280200360200200d41086a200441c8006a290300370300200d4110"
            "6a200441d0006a2903003703002009200d36028802200941013b0190030c010b20"
            "0b200d200441306a200e200910c0808080000b2001200128020841016a3602080b"
            "200041063a00000c070b000b2006410b41c89fc0800010b180808000000b41909f"
            "c08000412841b89fc0800010a680808000000b41e89fc08000413541a0a0c08000"
            "10a680808000000b41e49dc0800010a280808000000b41cf9ec08000413041809f"
            "c0800010a680808000000b20002005200c41186c6a220929030037030020004110"
            "6a200941106a220d290300370300200041086a200941086a220b29030037030020"
            "092003290300370300200b200341086a290300370300200d200341106a29030037"
            "03000b200441c0016a2480808080000be30501067f23808080800041306b220124"
            "80808080004100210241002103024020002802002204450d002001200436021820"
            "014100360214200120043602082001410036020420012000280204220336021c20"
            "01200336020c20002802082103410121020b200120033602202001200236021020"
            "01200236020002400240024003400240024002400240024020030d002001280200"
            "450d0820012802082104200128020422030d0141002100200128020c2203450d06"
            "034020042802980321042003417f6a22030d000c070b0b20012003417f6a360220"
            "024020024101712203450d0020012802040d0020012802082103200128020c2200"
            "450d03034020032802980321032000417f6a22000d000c040b0b2003450d012001"
            "28020421030c030b200421000c050b41f8bac0800010a280808000000b20014200"
            "3702082001200336020441012102200141013602000b2001280208210002402001"
            "28020c220520032f019203490d0002400340200141246a2003200010bf80808000"
            "20012802242203450d0120012802282100200128022c220520032f019203490d02"
            "0c000b0b41b0a0c0800010a280808000000b200541016a21040240024020000d00"
            "2001200436020c20014100360208200120033602040c010b200320044102746a41"
            "98036a21040340200428020022064198036a21042000417f6a22000d000b200142"
            "00370208200120063602042003450d040b024020032005410c6c6a418c026a2200"
            "2802002204450d00200028020420041082808080000b0240024002400240200320"
            "0541186c6a22032d00000e050303030102000b200341046a109b808080000c020b"
            "20032802042200450d01200328020820001082808080000c010b200341046a1091"
            "8080800020032802042200450d002003280208200041186c1082808080000b2001"
            "28022021030c000b0b200421030b0340200141246a2003200010bf808080002001"
            "2802242203450d01200128022821000c000b0b200141306a2480808080000b8905"
            "03037f017e037f23808080800041f0006b22022480808080004100210302402000"
            "2d0000220420012d0000470d00410121030240024002400240024020040e060500"
            "01020304050b20002d000120012d00014621030c040b4100210320002903082205"
            "2001290308520d030240024002402005a70e03000102000b200029031020012903"
            "105121030c050b200029031020012903105121030c040b20002b031020012b0310"
            "6121030c030b41002103200028020c2204200128020c470d022000280208200128"
            "0208200410dc808080004521030c020b41002103200028020c2206200128020c47"
            "0d012001280208210420002802082100200641016a210103402001417f6a220145"
            "21032001450d0220002004109c808080002106200441186a2104200041186a2100"
            "20060d000c020b0b41002103200028020c2204200128020c470d00200241003602"
            "6c2002420037026420024100360254200241003602442002410036023020024100"
            "36022020022001280208220636025c200220012802042203360258200220063602"
            "4c2002200336024820022000280208220636023820022000280204220136023420"
            "0220063602282002200136022420022004410020031b3602602002200341004722"
            "033602502002200336024020022004410020011b36023c20022001410047220336"
            "022c2002200336021c200241c0006a21070340200241106a2002411c6a109d8080"
            "80004101210320022802102201450d0120022802142104200241086a2007109d80"
            "80800020022802082200450d0141002103200128020822062000280208470d0120"
            "0228020c210820012802042000280204200610dc808080000d0120042008109c80"
            "8080000d000b0b200241f0006a24808080800020030bed0201057f024002400240"
            "200128022022020d00410021020c010b20012002417f6a36022002400240024020"
            "012802004101470d0020012802040d01200128020821030240200128020c220245"
            "0d00034020032802980321032002417f6a22020d000b0b20014200370208200120"
            "03360204200141013602000c020b4198bbc0800010a280808000000b2001280204"
            "21030b2001280208210202400240200128020c220420032f0192034f0d00200321"
            "050c010b03402003280288022205450d03200241016a210220032f019003210420"
            "052103200420052f0192034f0d000b0b200441016a21030240024020020d002005"
            "21060c010b200520034102746a4198036a21030340200328020022064198036a21"
            "032002417f6a22020d000b410021030b2001200336020c20014100360208200120"
            "063602042005200441186c6a210320052004410c6c6a418c026a21020b20002003"
            "360204200020023602000f0b4188bbc0800010a280808000000b4901017f410021"
            "010240024020004100480d00024020000d00410121010c020b41002d00c0cfc080"
            "001a200010848080800022010d01410121010b20012000109580808000000b2001"
            "0b9b0301037f23808080800041d0006b2204248080808000200441386a20002001"
            "108080808000024002400240024020042d00384106460d00200441086a41106a20"
            "0441386a41106a2205290300370300200441086a41086a200441386a41086a2206"
            "29030037030020042004290338370308200441386a200220031080808080002004"
            "2d00384106460d01200441206a41106a2005290300370300200441206a41086a20"
            "0629030037030020042004290338370320200441086a10a0808080002205450d02"
            "200441206a10a0808080002206450d0320052006109c808080002105200441206a"
            "108580808000200441086a10858080800002402003450d00200220031082808080"
            "000b02402001450d00200020011082808080000b200441d0006a24808080800020"
            "050f0b2004200428023c360220419080c08000412b200441206a418080c0800041"
            "d080c0800010a180808000000b2004200428023c360220419080c08000412b2004"
            "41206a418080c0800041e080c0800010a180808000000b41f880c0800010a28080"
            "8000000b418881c0800010a280808000000bea0101077f41002101024020002d00"
            "004105470d0020002802042202450d002000280208210303402002418c026a2100"
            "20022f0192032204410c6c2105417f2106024002400340024020050d0020042106"
            "0c020b2000280208210120002802042107200641016a2106200541746a21052000"
            "410c6a2100417f41f080c0800020072001410720014107491b10dc808080002207"
            "410720016b20071b220141004720014100481b22014101460d000b200141ff0171"
            "450d010b024020030d0041000f0b2003417f6a2103200220064102746a4198036a"
            "28020021020c010b0b2002200641186c6a21010b20010b8f0101017f2380808080"
            "0041c0006b22052480808080002005200136020c20052000360208200520033602"
            "14200520023602102005410236021c200541dc82c0800036021820054202370224"
            "2005418180808000ad422086200541106aad843703382005418280808000ad4220"
            "86200541086aad843703302005200541306a360220200541186a200410a4808080"
            "00000b130041ea81c08000412b200010a680808000000b4701017f238080808000"
            "41206b2200248080808000200041003602182000410136020c200041ac81c08000"
            "36020820004204370210200041086a41c881c0800010a480808000000b5601017f"
            "23808080800041206b2202248080808000200241106a200041106a290200370300"
            "200241086a200041086a290200370300200241013b011c20022001360218200220"
            "00290200370300200210ae80808000000bbe0601057f200041786a22012000417c"
            "6a280200220241787122006a21030240024020024101710d002002410271450d01"
            "2001280200220220006a21000240200120026b220141002802f4d2c08000470d00"
            "20032802044103714103470d01410020003602ecd2c0800020032003280204417e"
            "7136020420012000410172360204200320003602000f0b2001200210a880808000"
            "0b024002400240024002400240200328020422024102710d00200341002802f8d2"
            "c08000460d02200341002802f4d2c08000460d0320032002417871220210a88080"
            "80002001200220006a2200410172360204200120006a2000360200200141002802"
            "f4d2c08000470d01410020003602ecd2c080000f0b20032002417e713602042001"
            "2000410172360204200120006a20003602000b2000418002490d022001200010d7"
            "80808000410021014100410028028cd3c08000417f6a220036028cd3c080002000"
            "0d04024041002802d4d0c080002200450d00410021010340200141016a21012000"
            "28020822000d000b0b4100200141ff1f200141ff1f4b1b36028cd3c080000f0b41"
            "0020013602f8d2c08000410041002802f0d2c0800020006a22003602f0d2c08000"
            "200120004101723602040240200141002802f4d2c08000470d00410041003602ec"
            "d2c08000410041003602f4d2c080000b20004100280284d3c0800022044d0d0341"
            "002802f8d2c080002200450d034100210241002802f0d2c0800022054129490d02"
            "41ccd0c080002101034002402001280200220320004b0d00200020032001280204"
            "6a490d040b200128020821010c000b0b410020013602f4d2c08000410041002802"
            "ecd2c0800020006a22003602ecd2c0800020012000410172360204200120006a20"
            "003602000f0b200041f8017141dcd0c080006a21030240024041002802e4d2c080"
            "00220241012000410376742200710d00410020022000723602e4d2c08000200321"
            "000c010b200328020821000b200320013602082000200136020c2001200336020c"
            "200120003602080f0b024041002802d4d0c080002201450d004100210203402002"
            "41016a2102200128020822010d000b0b4100200241ff1f200241ff1f4b1b36028c"
            "d3c08000200520044d0d004100417f360284d3c080000b0b4d01017f2380808080"
            "0041206b2203248080808000200341003602102003410136020420034204370208"
            "2003200136021c200320003602182003200341186a3602002003200210a4808080"
            "00000b840601057f0240024002402000417c6a2203280200220441787122054104"
            "4108200441037122061b20016a490d0002402006450d002005200141276a4b0d02"
            "0b41102002410b6a4178712002410b491b210102400240024020060d0020014180"
            "02490d0120052001410472490d01200520016b418180084f0d010c020b20004178"
            "6a220720056a21060240024002400240200520014f0d00200641002802f8d2c080"
            "00460d03200641002802f4d2c08000460d02200628020422044102710d04200441"
            "7871220420056a22052001490d042006200410a880808000200520016b22024110"
            "490d0120032001200328020041017172410272360200200720016a220120024103"
            "72360204200720056a220520052802044101723602042001200210a98080800020"
            "000f0b200520016b2202410f4d0d04200320012004410171724102723602002007"
            "20016a22052002410372360204200620062802044101723602042005200210a980"
            "80800020000f0b20032005200328020041017172410272360200200720056a2202"
            "200228020441017236020420000f0b41002802ecd2c0800020056a22052001490d"
            "0102400240200520016b2202410f4b0d0020032004410171200572410272360200"
            "200720056a2202200228020441017236020441002102410021010c010b20032001"
            "200441017172410272360200200720016a22012002410172360204200720056a22"
            "05200236020020052005280204417e713602040b410020013602f4d2c080004100"
            "20023602ecd2c0800020000f0b41002802f0d2c0800020056a220520014b0d040b"
            "0240200210848080800022050d0041000f0b20052000417c417820032802002201"
            "4103711b20014178716a2201200220012002491b10df808080002102200010a580"
            "808000200221000b20000f0b41c9c5c08000412e41f8c5c0800010a68080800000"
            "0b4188c6c08000412e41b8c6c0800010a680808000000b20032001200441017172"
            "410272360200200720016a2202200520016b2205410172360204410020053602f0"
            "d2c08000410020023602f8d2c0800020000b820301047f200028020c2102024002"
            "4002402001418002490d002000280218210302400240024020022000470d002000"
            "41144110200028021422021b6a28020022010d01410021020c020b200028020822"
            "01200236020c200220013602080c010b200041146a200041106a20021b21040340"
            "200421052001220241146a200241106a200228021422011b210420024114411020"
            "011b6a28020022010d000b200541003602000b2003450d020240200028021c4102"
            "7441cccfc080006a22012802002000460d0020034110411420032802102000461b"
            "6a20023602002002450d030c020b2001200236020020020d01410041002802e8d2"
            "c08000417e200028021c77713602e8d2c080000c020b0240200220002802082204"
            "460d002004200236020c200220043602080f0b410041002802e4d2c08000417e20"
            "0141037677713602e4d2c080000f0b20022003360218024020002802102201450d"
            "0020022001360210200120023602180b20002802142201450d0020022001360214"
            "200120023602180f0b0ba00401027f200020016a21020240024020002802042203"
            "4101710d002003410271450d012000280200220320016a21010240200020036b22"
            "0041002802f4d2c08000470d0020022802044103714103470d01410020013602ec"
            "d2c0800020022002280204417e7136020420002001410172360204200220013602"
            "000c020b2000200310a8808080000b024002400240024020022802042203410271"
            "0d00200241002802f8d2c08000460d02200241002802f4d2c08000460d03200220"
            "03417871220310a8808080002000200320016a2201410172360204200020016a20"
            "01360200200041002802f4d2c08000470d01410020013602ecd2c080000f0b2002"
            "2003417e7136020420002001410172360204200020016a20013602000b02402001"
            "418002490d002000200110d7808080000f0b200141f8017141dcd0c080006a2102"
            "0240024041002802e4d2c08000220341012001410376742201710d004100200320"
            "01723602e4d2c08000200221010c010b200228020821010b200220003602082001"
            "200036020c2000200236020c200020013602080f0b410020003602f8d2c0800041"
            "0041002802f0d2c0800020016a22013602f0d2c080002000200141017236020420"
            "0041002802f4d2c08000470d01410041003602ecd2c08000410041003602f4d2c0"
            "80000f0b410020003602f4d2c08000410041002802ecd2c0800020016a22013602"
            "ecd2c0800020002001410172360204200020016a20013602000f0b0b7902017f01"
            "7e23808080800041306b2203248080808000200320003602002003200136020420"
            "03410236020c200341c485c08000360208200342023702142003418380808000ad"
            "4220862204200341046aad84370328200320042003ad843703202003200341206a"
            "360210200341086a200210a480808000000b110020003502004101200110ad8080"
            "80000b7902017f017e23808080800041306b220324808080800020032001360204"
            "200320003602002003410236020c200341c882c080003602082003420237021420"
            "03418380808000ad42208622042003ad8437032820032004200341046aad843703"
            "202003200341206a360210200341086a200210a480808000000bec0203027f017e"
            "037f23808080800041306b2203248080808000412721040240024020004290ce00"
            "5a0d00200021050c010b412721040340200341096a20046a2206417c6a20004290"
            "ce0080220542f0b1037e20007ca7220741ffff037141e4006e2208410174419283"
            "c080006a2f00003b00002006417e6a2008419c7f6c20076a41ffff037141017441"
            "9283c080006a2f00003b00002004417c6a2104200042ffc1d72f56210620052100"
            "20060d000b0b02400240200542e300560d002005a721060c010b200341096a2004"
            "417e6a22046a2005a7220741ffff037141e4006e2206419c7f6c20076a41ffff03"
            "71410174419283c080006a2f00003b00000b024002402006410a490d0020034109"
            "6a2004417e6a22046a2006410174419283c080006a2f00003b00000c010b200341"
            "096a2004417f6a22046a20064130723a00000b2002200141014100200341096a20"
            "046a412720046b10af808080002104200341306a24808080800020040b5d01027f"
            "23808080800041206b220124808080800020002802182102200141106a20004110"
            "6a290200370300200141086a200041086a2902003703002001200036021c200120"
            "0236021820012000290200370300200110d880808000000bcb0501077f02400240"
            "20010d00200541016a2106200028021c2107412d21080c010b412b418080c40020"
            "0028021c220741017122011b2108200120056a21060b0240024020074104710d00"
            "410021020c010b0240024020030d00410021090c010b02402003410371220a0d00"
            "0c010b41002109200221010340200920012c000041bf7f4a6a2109200141016a21"
            "01200a417f6a220a0d000b0b200920066a21060b024020002802000d0002402000"
            "28021422012000280218220920082002200310b080808000450d0041010f0b2001"
            "20042005200928020c11818080800080808080000f0b0240024002400240200028"
            "0204220120064b0d00200028021422012000280218220920082002200310b08080"
            "8000450d0141010f0b2007410871450d0120002802102107200041303602102000"
            "2d0020210b4101210c200041013a0020200028021422092000280218220a200820"
            "02200310b0808080000d02200120066b41016a2101024003402001417f6a220145"
            "0d0120094130200a2802101182808080008080808000450d000b41010f0b024020"
            "0920042005200a28020c1181808080008080808000450d0041010f0b2000200b3a"
            "00202000200736021041000f0b200120042005200928020c118180808000808080"
            "8000210c0c010b200120066b210702400240024020002d002022010e0402000100"
            "020b20072101410021070c010b20074101762101200741016a41017621070b2001"
            "41016a210120002802102106200028021821092000280214210a02400340200141"
            "7f6a2201450d01200a200620092802101182808080008080808000450d000b4101"
            "0f0b4101210c200a200920082002200310b0808080000d00200a20042005200928"
            "020c11818080800080808080000d00410021010340024020072001470d00200720"
            "07490f0b200141016a2101200a200620092802101182808080008080808000450d"
            "000b2001417f6a2007490f0b200c0b490002402002418080c400460d0020002002"
            "20012802101182808080008080808000450d0041010f0b024020030d0041000f0b"
            "200020032004200128020c11818080800080808080000b7902017f017e23808080"
            "800041306b22032480808080002003200036020020032001360204200341023602"
            "0c200341e485c08000360208200342023702142003418380808000ad4220862204"
            "200341046aad84370328200320042003ad843703202003200341206a3602102003"
            "41086a200210a480808000000bc20b010b7f200028020821030240024002400240"
            "200028020022040d002003410171450d010b02402003410171450d00200120026a"
            "210502400240200028020c22060d0041002107200121080c010b41002107410021"
            "09200121080340200822032005460d020240024020032c00002208417f4c0d0020"
            "0341016a21080c010b0240200841604f0d00200341026a21080c010b0240200841"
            "704f0d00200341036a21080c010b200341046a21080b200820036b20076a210720"
            "06200941016a2209470d000b0b20082005460d00024020082c00002203417f4a0d"
            "0020034160491a0b024002402007450d000240200720024f0d00200120076a2c00"
            "0041bf7f4a0d01410021030c020b20072002460d00410021030c010b200121030b"
            "2007200220031b21022003200120031b21010b024020040d002000280214200120"
            "02200028021828020c11818080800080808080000f0b2000280204210a02402002"
            "4110490d0020022001200141036a417c7122076b22096a220b4103712104410021"
            "0641002103024020012007460d004100210302402009417c4b0d00410021034100"
            "210503402003200120056a22082c000041bf7f4a6a200841016a2c000041bf7f4a"
            "6a200841026a2c000041bf7f4a6a200841036a2c000041bf7f4a6a210320054104"
            "6a22050d000b0b200121080340200320082c000041bf7f4a6a2103200841016a21"
            "08200941016a22090d000b0b02402004450d002007200b417c716a22082c000041"
            "bf7f4a210620044101460d00200620082c000141bf7f4a6a210620044102460d00"
            "200620082c000241bf7f4a6a21060b200b4102762105200620036a210603402007"
            "21042005450d04200541c001200541c001491b220b410371210c200b410274210d"
            "41002108024020054104490d002004200d41f007716a2109410021082004210303"
            "40200328020c2207417f7341077620074106767241818284087120032802082207"
            "417f7341077620074106767241818284087120032802042207417f734107762007"
            "4106767241818284087120032802002207417f7341077620074106767241818284"
            "087120086a6a6a6a2108200341106a22032009470d000b0b2005200b6b21052004"
            "200d6a2107200841087641ff81fc0771200841ff81fc07716a418180046c411076"
            "20066a2106200c450d000b2004200b41fc01714102746a22082802002203417f73"
            "4107762003410676724181828408712103200c4101460d0220082802042207417f"
            "7341077620074106767241818284087120036a2103200c4102460d022008280208"
            "2208417f7341077620084106767241818284087120036a21030c020b024020020d"
            "00410021060c030b2002410371210802400240200241044f0d0041002106410021"
            "090c010b41002106200121032002410c71220921070340200620032c000041bf7f"
            "4a6a200341016a2c000041bf7f4a6a200341026a2c000041bf7f4a6a200341036a"
            "2c000041bf7f4a6a2106200341046a21032007417c6a22070d000b0b2008450d02"
            "200120096a21030340200620032c000041bf7f4a6a2106200341016a2103200841"
            "7f6a22080d000c030b0b200028021420012002200028021828020c118180808000"
            "80808080000f0b200341087641ff811c71200341ff81fc07716a418180046c4110"
            "7620066a21060b02400240200a20064d0d00200a20066b21054100210302400240"
            "024020002d00200e0402000102020b20052103410021050c010b20054101762103"
            "200541016a41017621050b200341016a2103200028021021092000280218210820"
            "00280214210703402003417f6a2203450d02200720092008280210118280808000"
            "8080808000450d000b41010f0b200028021420012002200028021828020c118180"
            "80800080808080000f0b0240200720012002200828020c11818080800080808080"
            "00450d0041010f0b410021030340024020052003470d0020052005490f0b200341"
            "016a21032007200920082802101182808080008080808000450d000b2003417f6a"
            "2005490b140020012000280200200028020410b2808080000b1c00200028020020"
            "01200028020428020c11828080800080808080000bbf05010a7f23808080800041"
            "306b2203248080808000200341033a002c2003412036021c410021042003410036"
            "02282003200136022420032000360220200341003602142003410036020c024002"
            "40024002400240200228021022050d00200228020c2200450d0120022802082101"
            "200041037421062000417f6a41ffffffff017141016a2104200228020021000340"
            "0240200041046a2802002207450d00200328022020002802002007200328022428"
            "020c11818080800080808080000d040b20012802002003410c6a20012802041182"
            "8080800080808080000d03200141086a2101200041086a2100200641786a22060d"
            "000c020b0b20022802142201450d00200141057421082001417f6a41ffffff3f71"
            "41016a210420022802082109200228020021004100210603400240200041046a28"
            "02002201450d00200328022020002802002001200328022428020c118180808000"
            "80808080000d030b2003200520066a220141106a28020036021c20032001411c6a"
            "2d00003a002c2003200141186a2802003602282001410c6a28020021074100210a"
            "4100210b024002400240200141086a2802000e03010002010b2007410374210c41"
            "00210b2009200c6a220c2802040d01200c28020021070b4101210b0b2003200736"
            "02102003200b36020c200141046a280200210702400240024020012802000e0301"
            "0002010b2007410374210b2009200b6a220b2802040d01200b28020021070b4101"
            "210a0b200320073602182003200a3602142009200141146a2802004103746a2201"
            "2802002003410c6a200128020411828080800080808080000d02200041086a2100"
            "2008200641206a2206470d000b0b200420022802044f0d01200328022020022802"
            "0020044103746a22012802002001280204200328022428020c1181808080008080"
            "808000450d010b410121010c010b410021010b200341306a24808080800020010b"
            "d70201057f2380808080004180016b220224808080800002400240024002402001"
            "28021c22034110710d0020034120710d012000ad4101200110ad8080800021000c"
            "030b41ff00210303402002200322046a22052000410f712203413072200341d700"
            "6a2003410a491b3a00002004417f6a210320004110492106200041047621002006"
            "450d000c020b0b41ff00210303402002200322046a22052000410f712203413072"
            "200341376a2003410a491b3a00002004417f6a2103200041104921062000410476"
            "21002006450d000b02402004418101490d002004418001418083c0800010aa8080"
            "8000000b20014101419083c0800041022005418101200441016a6b10af80808000"
            "21000c010b02402004418101490d002004418001418083c0800010aa8080800000"
            "0b20014101419083c0800041022005418101200441016a6b10af8080800021000b"
            "20024180016a24808080800020000b7902017f017e23808080800041306b220324"
            "808080800020032000360200200320013602042003410236020c2003419886c080"
            "00360208200342023702142003418380808000ad4220862204200341046aad8437"
            "0328200320042003ad843703202003200341206a360210200341086a200210a480"
            "808000000b920c01057f23808080800041206b2203248080808000024002400240"
            "024002400240024002400240024002400240024002400240024020010e28060101"
            "010101010101020401010301010101010101010101010101010101010101010901"
            "01010107000b200141dc00460d040b2001418006490d0b20024101710d060c0b0b"
            "20004180043b010a20004200370102200041dce8013b01000c0c0b20004180043b"
            "010a20004200370102200041dce4013b01000c0b0b20004180043b010a20004200"
            "370102200041dcdc013b01000c0a0b20004180043b010a20004200370102200041"
            "dcb8013b01000c090b20004180043b010a20004200370102200041dce0003b0100"
            "0c080b200241800271450d0620004180043b010a20004200370102200041dcce00"
            "3b01000c070b200141aa9d044b410474220220024108722202200241027441a896"
            "c080006a280200410b742001410b7422024b1b2204200441047222042004410274"
            "41a896c080006a280200410b7420024b1b220420044102722204200441027441a8"
            "96c080006a280200410b7420024b1b2204200441016a2204200441027441a896c0"
            "80006a280200410b7420024b1b2204200441016a2204200441027441a896c08000"
            "6a280200410b7420024b1b220441027441a896c080006a280200410b7422052002"
            "4620052002496a20046a220441204b0d01200441027441a896c080006a22052802"
            "00411576210241d70521060240024020044120460d002005280204411576210620"
            "040d00410021040c010b200441027441a496c080006a28020041ffffff00712104"
            "0b024020062002417f736a450d00200120046b2107200241d705200241d7054b1b"
            "21052006417f6a210641002104034020052002460d042004200241ac97c080006a"
            "2d00006a220420074b0d012006200241016a2202470d000b200621020b20024101"
            "71450d04200341003a000a200341003b01082003200141147641da81c080006a2d"
            "00003a000b20032001410476410f7141da81c080006a2d00003a000f2003200141"
            "0876410f7141da81c080006a2d00003a000e20032001410c76410f7141da81c080"
            "006a2d00003a000d20032001411076410f7141da81c080006a2d00003a000c2003"
            "41086a20014101726741027622026a220441fb003a00002004417f6a41f5003a00"
            "00200341086a2002417e6a22026a41dc003a0000200341086a41086a2204200141"
            "0f7141da81c080006a2d00003a00002000410a3a000b200020023a000a20002003"
            "290208370200200341fd003a0011200041086a20042f01003b01000c060b200241"
            "808004710d020c040b20044121418896c0800010ac80808000000b200541d70541"
            "9896c0800010ac80808000000b20004180043b010a20004200370102200041dcc4"
            "003b01000c020b024020014120490d00200141ff00490d01024020014180800449"
            "0d0002402001418080084f0d00200141ec8ac08000412c41c48bc0800041c40141"
            "888dc0800041c20310b980808000450d020c030b200141feffff0071419ef00a46"
            "0d01200141e0ffff007141e0cd0a460d01200141c091756a41794b0d01200141d0"
            "e2746a41714b0d0120014190a8746a41704b0d012001418090746a41dd6c4b0d01"
            "2001418080746a419d744b0d01200141b0d9736a417a4b0d0120014180fe476a41"
            "afc5544b0d01200141f083384f0d010c020b200141ca90c080004128419a91c080"
            "0041a00241ba93c0800041ad0210b9808080000d010b200341003a001620034100"
            "3b01142003200141147641da81c080006a2d00003a001720032001410476410f71"
            "41da81c080006a2d00003a001b20032001410876410f7141da81c080006a2d0000"
            "3a001a20032001410c76410f7141da81c080006a2d00003a001920032001411076"
            "410f7141da81c080006a2d00003a0018200341146a20014101726741027622026a"
            "220441fb003a00002004417f6a41f5003a0000200341146a2002417e6a22026a41"
            "dc003a0000200341146a41086a22042001410f7141da81c080006a2d00003a0000"
            "2000410a3a000b200020023a000a20002003290214370200200341fd003a001d20"
            "0041086a20042f01003b01000c010b2000200136020420004180013a00000b2003"
            "41206a2480808080000be90201067f200120024101746a210720004180fe037141"
            "0876210841002109200041ff0171210a02400240024002400340200141026a210b"
            "200920012d000122026a210c024020012d000022012008460d00200120084b0d04"
            "200c2109200b2101200b2007470d010c040b200c2009490d01200c20044b0d0220"
            "0320096a21010340024020020d00200c2109200b2101200b2007470d020c050b20"
            "02417f6a210220012d00002109200141016a21012009200a470d000b0b41002102"
            "0c030b2009200c41dc8ac0800010b780808000000b200c200441dc8ac0800010b1"
            "80808000000b200041ffff03712109200520066a210c410121020340200541016a"
            "210a0240024020052c000022014100480d00200a21050c010b0240200a200c460d"
            "00200141ff007141087420052d0001722101200541026a21050c010b41cc8ac080"
            "0010a280808000000b200920016b22094100480d01200241017321022005200c47"
            "0d000b0b20024101710b13002000200120022003200410bb80808000000bd10902"
            "057f017e23808080800041f0006b22052480808080002005200336020c20052002"
            "3602080240024002400240024002400240024002402001418102490d0002402000"
            "2c00800241bf7f4c0d00410321060c030b20002c00ff0141bf7f4c0d0141022106"
            "0c020b200520013602142005200036021041002106410121070c020b20002c00fe"
            "0141bf7f4a21060b2000200641fd016a22066a2c000041bf7f4c0d012005200636"
            "0214200520003602104105210641a888c0800021070b2005200636021c20052007"
            "3602180240200220014b22060d00200320014b0d00200220034b0d020240200245"
            "0d00200220014f0d0020032002200020026a2c000041bf7f4a1b21030b20052003"
            "360220200121020240200320014f0d00200341016a220641002003417d6a220220"
            "0220034b1b2202490d04024020062002460d00200620026b21080240200020036a"
            "2c000041bf7f4c0d002008417f6a21070c010b20022003460d000240200020066a"
            "2206417e6a22032c000041bf7f4c0d002008417e6a21070c010b200020026a2209"
            "2003460d0002402006417d6a22032c000041bf7f4c0d002008417d6a21070c010b"
            "20092003460d0002402006417c6a22032c000041bf7f4c0d002008417c6a21070c"
            "010b20092003460d002008417b6a21070b200720026a21020b02402002450d0002"
            "40200220014f0d00200020026a2c000041bf7f4a0d010c070b20022001470d060b"
            "20022001460d040240024002400240200020026a22032c00002201417f4a0d0020"
            "032d0001413f7121002001411f7121062001415f4b0d0120064106742000722101"
            "0c020b2005200141ff0171360224410121010c020b200041067420032d0002413f"
            "717221000240200141704f0d0020002006410c747221010c010b20004106742003"
            "2d0003413f71722006411274418080f00071722201418080c400460d060b200520"
            "01360224024020014180014f0d00410121010c010b024020014180104f0d004102"
            "21010c010b41034104200141808004491b21010b20052002360228200520012002"
            "6a36022c20054105360234200541b089c080003602302005420537023c20054182"
            "80808000ad422086220a200541186aad843703682005200a200541106aad843703"
            "602005418480808000ad422086200541286aad843703582005418580808000ad42"
            "2086200541246aad843703502005418380808000ad422086200541206aad843703"
            "482005200541c8006a360238200541306a200410a480808000000b200520022003"
            "20061b36022820054103360234200541f089c080003602302005420337023c2005"
            "418280808000ad422086220a200541186aad843703582005200a200541106aad84"
            "3703502005418380808000ad422086200541286aad843703482005200541c8006a"
            "360238200541306a200410a480808000000b2000200141002006200410ba808080"
            "00000b20054104360234200541d088c080003602302005420437023c2005418280"
            "808000ad422086220a200541186aad843703602005200a200541106aad84370358"
            "2005418380808000ad422086220a2005410c6aad843703502005200a200541086a"
            "ad843703482005200541c8006a360238200541306a200410a480808000000b2002"
            "2006419c8ac0800010b780808000000b200410a280808000000b20002001200220"
            "01200410ba80808000000b4d01017f4101210202402000280200200110b6808080"
            "000d00200128021441d881c080004102200128021828020c118180808000808080"
            "80000d002000280204200110b68080800021020b20020bc40101047f2380808080"
            "0041106b2202248080808000410121030240200128021422044127200128021822"
            "05280210220111828080800080808080000d00200241046a200028020041810210"
            "b8808080000240024020022d0004418001470d0020042002280208200111828080"
            "80008080808000450d010c020b2004200241046a20022d000e22006a20022d000f"
            "20006b200528020c11818080800080808080000d010b2004412720011182808080"
            "00808080800021030b200241106a24808080800020030b2701017f200028020022"
            "002000411f7522027320026bad2000417f73411f76200110ad808080000b500103"
            "7f200121032002210402402001280288022205450d00200241016a210320012f01"
            "900321040b200141c80341980320021b1082808080002000200536020020002004"
            "ad4220862003ad843702040bec0201047f2000418c026a22052001410c6c6a2106"
            "02400240200141016a220720002f01920322084d0d002006200229020037020020"
            "0641086a200241086a2802003602000c010b20052007410c6c6a2006200820016b"
            "2205410c6c10de808080001a200641086a200241086a2802003602002006200229"
            "02003702002000200741186c6a2000200141186c6a200541186c10de808080001a"
            "0b200841016a21022000200141186c6a22062003290300370300200641106a2003"
            "41106a290300370300200641086a200341086a29030037030020004198036a2103"
            "0240200141026a2205200841026a22064f0d00200320054102746a200320074102"
            "746a200820016b41027410de808080001a0b200320074102746a20043602002000"
            "20023b0192030240200720064f0d00200841016a2103200141027420006a419c03"
            "6a2107034020072802002208200141016a22013b01900320082000360288022007"
            "41046a210720032001470d000b0b0bed04010a7f23808080800041d0006b220224"
            "808080800041002d00c0cfc080001a200128020022032f01920321040240024002"
            "400240024041c8031084808080002205450d002005410036028802200520012802"
            "082206417f7320032f01920322076a22083b019203200241286a41086a2003418c"
            "026a22092006410c6c6a220a41086a280200360200200241386a41086a20032006"
            "41186c6a220b41086a290300370300200241386a41106a200b41106a2903003703"
            "002002200a2902003703282002200b2903003703382008410c4f0d012007200641"
            "016a220b6b2008470d022005418c026a2009200b410c6c6a2008410c6c10df8080"
            "80001a20052003200b41186c6a200841186c10df80808000210b200320063b0192"
            "03200241086a200241286a41086a280200360200200241186a200241386a41086a"
            "290300370300200241206a200241c8006a29030037030020022002290328370300"
            "20022002290338370310200b2f019203220541016a21082005410c4f0d03200420"
            "066b220a2008470d04200b4198036a200320064102746a419c036a200a41027410"
            "df80808000210a200128020421014100210602400340200a20064102746a280200"
            "220820063b0190032008200b36028802200620054f0d01200620062005496a2206"
            "20054d0d000b0b2000200136022c2000200336022820002002412810df80808000"
            "220620013602342006200b360230200241d0006a2480808080000f0b000b200841"
            "0b41c89fc0800010b180808000000b41909fc08000412841b89fc0800010a68080"
            "8000000b2008410c41d89fc0800010b180808000000b41909fc08000412841b89f"
            "c0800010a680808000000bbb0b01037f2380808080004180016b22022480808080"
            "00200028020021002002410036022c2002428080808010370224200241033a0050"
            "200241203602402002410036024c200241c0a0c080003602482002410036023820"
            "0241003602302002200241246a3602440240024002400240024002400240024002"
            "400240024002400240024002400240024002400240024002400240024002400240"
            "02400240024020002802000e1918000102030405060708090a0b0c0d0e0f101112"
            "1314151617180b024002400240024020002d00040e0400010203000b2002200028"
            "020836025441002d00c0cfc080001a41141084808080002203450d1c200341106a"
            "41002800f4ccc08000360000200341086a41002900ecccc0800037000020034100"
            "2900e4ccc08000370000200241143602602002200336025c200241143602582002"
            "410336026c200241ccccc08000360268200242023702742002418680808000ad42"
            "2086200241d4006aad843703102002418780808000ad422086200241d8006aad84"
            "3703082002200241086a360270200241246a41c0a0c08000200241e8006a10b580"
            "8080002103024020022802582204450d00200228025c20041082808080000b2003"
            "0d1d0c1b0b20002d000521032002410136026c200241c8c6c08000360268200242"
            "013702742002418280808000ad422086200241086aad8437035820022003410274"
            "220341f8ccc080006a28020036020c20022003419ccec080006a28020036020820"
            "02200241d8006a360270200241246a41c0a0c08000200241e8006a10b580808000"
            "0d1c0c1a0b200241306a20002802082203280200200328020410b2808080000d1b"
            "0c190b20002802082203280200200241306a200328020428021011828080800080"
            "808080000d1a0c180b200241246a4181a2c08000411810c4808080000d190c170b"
            "200241246a4199a2c08000411b10c4808080000d180c160b200241246a41b4a2c0"
            "8000411a10c4808080000d170c150b200241246a41cea2c08000411910c4808080"
            "000d160c140b200241246a41e7a2c08000410c10c4808080000d150c130b200241"
            "246a41f3a2c08000411310c4808080000d140c120b200241246a4186a3c0800041"
            "1310c4808080000d130c110b200241246a4199a3c08000410e10c4808080000d12"
            "0c100b200241246a41a7a3c08000410e10c4808080000d110c0f0b200241246a41"
            "b5a3c08000410c10c4808080000d100c0e0b200241246a41c1a3c08000410e10c4"
            "808080000d0f0c0d0b200241246a41cfa3c08000410e10c4808080000d0e0c0c0b"
            "200241246a41dda3c08000411310c4808080000d0d0c0b0b200241246a41f0a3c0"
            "8000411a10c4808080000d0c0c0a0b200241246a418aa4c08000413e10c4808080"
            "000d0b0c090b200241246a41c8a4c08000411410c4808080000d0a0c080b200241"
            "246a41dca4c08000413410c4808080000d090c070b200241246a4190a5c0800041"
            "2c10c4808080000d080c060b200241246a41bca5c08000412410c4808080000d07"
            "0c050b200241246a41e0a5c08000410e10c4808080000d060c040b200241246a41"
            "eea5c08000411310c4808080000d050c030b200241246a4181a6c08000411c10c4"
            "808080000d040c020b200241246a419da6c08000411810c480808000450d010c03"
            "0b200241246a2000280204200028020810c4808080000d020b200241d8006a4108"
            "6a200241246a41086a280200360200200220022902243703582002418380808000"
            "36027c2002418380808000360274200241888080800036026c2002410436020c20"
            "0241d0a6c08000360208200242033702142002200041106a36027820022000410c"
            "6a3602702002200241d8006a3602682002200241e8006a36021020012802142001"
            "280218200241086a10b5808080002100024020022802582201450d00200228025c"
            "20011082808080000b20024180016a24808080800020000f0b000b41e8a0c08000"
            "4137200241e8006a41d8a0c0800041eca1c0800010a180808000000b1400200120"
            "00280204200028020810b2808080000b4b01017f02402000280200200028020822"
            "036b20024f0d0020002003200210cf80808000200028020821030b200028020420"
            "036a2001200210df808080001a2000200320026a36020841000bd507010d7f2380"
            "8080800041106b2202248080808000200028020821032000280204210441012105"
            "024020012802142206412220012802182207280210220811828080800080808080"
            "000d000240024020030d0041002103410021000c010b410021094100210a200421"
            "0b2003210c024002400340200b200c6a210d4100210002400340200b20006a220e"
            "2d0000220141817f6a41ff017141a101490d0120014122460d01200141dc00460d"
            "01200c200041016a2200470d000b200a200c6a210a0c030b02400240200e2c0000"
            "2201417f4c0d00200e41016a210b200141ff017121010c010b200e2d0001413f71"
            "210b2001411f71210c02402001415f4b0d00200c410674200b722101200e41026a"
            "210b0c010b200b410674200e2d0002413f7172210b0240200141704f0d00200b20"
            "0c410c74722101200e41036a210b0c010b200b410674200e2d0003413f7172200c"
            "411274418080f00071722101200e41046a210b0b2000200a6a2100200241046a20"
            "014181800410b8808080000240024020022d0004418001460d0020022d000f2002"
            "2d000e6b41ff01714101460d0020002009490d0302402009450d00024020092003"
            "4f0d00200420096a2c000041bf7f4a0d010c050b20092003470d040b0240200045"
            "0d000240200020034f0d00200420006a2c000041bf7f4c0d050c010b2000200347"
            "0d040b2006200420096a200020096b200728020c220e1181808080008080808000"
            "0d010240024020022d0004418001470d0020062002280208200811828080800080"
            "80808000450d010c030b2006200241046a20022d000e220c6a20022d000f200c6b"
            "200e11818080800080808080000d020b0240024020014180014f0d004101210e0c"
            "010b024020014180104f0d004102210e0c010b41034104200141808004491b210e"
            "0b200e20006a21090b0240024020014180014f0d00410121010c010b0240200141"
            "80104f0d00410221010c010b41034104200141808004491b21010b200120006a21"
            "0a200d200b6b220c0d010c030b0b410121050c030b200420032009200041f084c0"
            "800010ba80808000000b02402009200a4b0d004100210002402009450d00024020"
            "0920034f0d0020092100200420096a2c000041bf7f4c0d020c010b200321002009"
            "2003470d010b0240200a0d00410021030c020b0240200a20034f0d002000210920"
            "04200a6a2c000041bf7f4c0d01200a21030c020b20002109200a2003460d010b20"
            "0420032009200a418085c0800010ba80808000000b2006200420006a200320006b"
            "200728020c11818080800080808080000d00200641222008118280808000808080"
            "800021050b200241106a24808080800020050b2200200128021441fca1c0800041"
            "05200128021828020c11818080800080808080000b1e01017f0240200028020022"
            "01450d00200028020420011082808080000b0b5301047f02402000280208220120"
            "0028020422024f0d00200028020021030340200320016a2d000022044122460d01"
            "200441dc00460d0120044120490d012000200141016a220136020820022001470d"
            "000b0b0b4901017f02402000280200200028020822036b20024f0d002000200320"
            "0210cf80808000200028020821030b200028020420036a2001200210df80808000"
            "1a2000200320026a3602080bb10501077f23808080800041106b22042480808080"
            "0002402003450d004100200341796a2205200520034b1b2106200241036a417c71"
            "20026b21074100210503400240024002400240200220056a2d00002208c0220941"
            "00480d00200720056b4103710d01200520064f0d020340200220056a2208280204"
            "200828020072418081828478710d03200541086a22052006490d000c030b0b0240"
            "024002400240024002400240200841a886c080006a2d0000417e6a0e0300010205"
            "0b200541016a220520034f0d04200220056a2c000041bf7f4a0d040c050b200541"
            "016a220a20034f0d032002200a6a2c0000210a02400240200841e001460d002008"
            "41ed01460d012009411f6a41ff0171410c490d032009417e71416e470d05200a41"
            "40480d040c050b200a41607141a07f460d030c040b200a419f7f4a0d030c020b20"
            "0541016a220a20034f0d022002200a6a2c0000210a024002400240024020084190"
            "7e6a0e050100000002000b2009410f6a41ff017141024b0d05200a4140480d020c"
            "050b200a41f0006a41ff01714130490d010c040b200a418f7f4a0d030b20054102"
            "6a220820034f0d02200220086a2c000041bf7f4a0d02200541036a220520034f0d"
            "02200220056a2c000041bf7f4c0d030c020b200a41404e0d010b200541026a2205"
            "20034f0d00200220056a2c000041bf7f4c0d010b200441086a2001280200200128"
            "020420012802081083808080004100210241002d00c0cfc080001a200428020c21"
            "0520042802082108024041141084808080002203450d002003200836020c200341"
            "0f360200200320053602100c060b000b200541016a21050c020b200541016a2105"
            "0c010b200520034f0d000340200220056a2c00004100480d012003200541016a22"
            "05470d000c030b0b20052003490d000b0b20002002360200200020033602042004"
            "41106a2480808080000b5901017f23808080800041106b22012480808080002001"
            "41086a2000200028020041014101410110d4808080000240200128020822004181"
            "80808078460d002000200128020c109580808000000b200141106a248080808000"
            "0be20601057f23808080800041206b2202248080808000200241146a200010cd80"
            "8080000240024020022f01140d0002400240024002400240024020022f01162203"
            "4180f803714180b803460d0020034180c8006a41ffff03714180f803490d042002"
            "41146a200010ce8080800020022d00140d0620022d001521042000200028020822"
            "0541016a360208200441dc00470d03200241146a200010ce8080800020022d0014"
            "0d0620022d001521042000200541026a360208200441f500470d02200241146a20"
            "0010cd8080800020022f01140d0620022f011622044180c0006a41ffff03714180"
            "f803490d0120034180d0006a41ffff0371410a7420044180c8006a41ffff037172"
            "2205418080046a210302402001280200200128020822006b41034b0d0020012000"
            "410410cf80808000200128020821000b2001200041046a36020820012802042000"
            "6a2200200341127641f001723a0000200041036a2004413f71418001723a000020"
            "002005410676413f71418001723a000220002003410c76413f71418001723a0001"
            "410021000c070b200220002802002000280204200028020810838080800041002d"
            "00c0cfc080001a200228020421012002280200210341141084808080002200450d"
            "042000200336020c20004114360200200020013602100c060b200241086a200028"
            "02002000280204200028020810838080800041002d00c0cfc080001a200228020c"
            "21012002280208210341141084808080002200450d032000200336020c20004114"
            "360200200020013602100c050b200241173602142000200241146a10d080808000"
            "21000c040b200241173602142000200241146a10d08080800021000c030b024002"
            "4002402003418001490d0002402001280200200128020822046b41034b0d002001"
            "2004410410cf80808000200128020821040b200128020420046a21002003418010"
            "4f0d0120034106764140722106410221050c020b02402001280208220020012802"
            "00470d00200110cb808080000b2001200041016a360208200128020420006a2003"
            "3a0000410021000c040b20002003410676413f71418001723a00012003410c7641"
            "60722106410321050b200020063a00002001200420056a360208200020056a417f"
            "6a2003413f71418001723a0000410021000c020b000b200228021821000b200241"
            "206a24808080800020000b910301057f23808080800041106b2202248080808000"
            "02400240024002402001280204220320012802082204490d000240200320046b41"
            "034b0d0020012003360208200241086a2001280200200320031083808080004100"
            "2d00c0cfc080001a200228020c2103200228020821044114108480808000220145"
            "0d022001200436020c2001410436020020002001360204200120033602100c030b"
            "2001200441046a220536020802402001280200220620046a22012d000141017441"
            "88bdc080006a2f010020012d00004101744188c1c080006a2f010072c141087420"
            "012d00024101744188c1c080006a2e01007220012d00034101744188bdc080006a"
            "2e0100722201417f4a0d00200220062003200510838080800041002d00c0cfc080"
            "001a200228020421032002280200210441141084808080002201450d0220012004"
            "36020c2001410c36020020002001360204200120033602100c030b200020013b01"
            "02410021010c030b2004200341f8bcc0800010aa808080000b000b410121010b20"
            "0020013b0100200241106a2480808080000bb20101037f23808080800041106b22"
            "022480808080000240024002402001280208220320012802042204490d00200241"
            "086a20012802002004200310838080800041002d00c0cfc080001a200228020c21"
            "032002280208210441141084808080002201450d022001200436020c2001410436"
            "02002000200136020420012003360210410121010c010b2000200128020020036a"
            "2d00003a0001410021010b200020013a0000200241106a2480808080000f0b000b"
            "5601017f23808080800041106b2203248080808000200341086a20002001200241"
            "01410110d480808000024020032802082202418180808078460d00200220032802"
            "0c109580808000000b200341106a2480808080000b8c0101037f23808080800041"
            "106b2202248080808000200241086a200028020020002802042000280208108380"
            "80800041002d00c0cfc080001a200228020c210320022802082104024041141084"
            "8080800022000d00000b2000200436020c20002001290200370200200020033602"
            "10200041086a200141086a280200360200200241106a24808080800020000b1f00"
            "024020012802040e020000000b20004188c5c08000200110b5808080000be30201"
            "027f23808080800041106b22022480808080000240024002400240200141800149"
            "0d002002410036020c2001418010490d0102402001418080044f0d002002200141"
            "3f71418001723a000e20022001410c7641e001723a000c20022001410676413f71"
            "418001723a000d410321010c030b20022001413f71418001723a000f2002200141"
            "127641f001723a000c20022001410676413f71418001723a000e20022001410c76"
            "413f71418001723a000d410421010c020b0240200028020822032000280200470d"
            "00200010cb808080000b2000200341016a360208200028020420036a20013a0000"
            "0c020b20022001413f71418001723a000d2002200141067641c001723a000c4102"
            "21010b02402000280200200028020822036b20014f0d0020002003200110cf8080"
            "8000200028020821030b200028020420036a2002410c6a200110df808080001a20"
            "00200320016a3602080b200241106a24808080800041000b820101017f02400240"
            "024002402003280204450d000240200328020822040d002002450d0341002d00c0"
            "cfc080001a0c020b20032802002004200210a78080800021030c030b2002450d01"
            "41002d00c0cfc080001a0b200210848080800021030c010b200121030b20002002"
            "36020820002003200120031b36020420002003453602000b9f0202047f017e2380"
            "8080800041206b2206248080808000024002400240200220036a220320024f0d00"
            "410021020c010b41002102200420056a417f6a410020046b71ad41084104200541"
            "01461b22072001280200220841017422092003200920034b1b2203200720034b1b"
            "2207ad7e220a422088a70d00200aa7220941808080807820046b4b0d0102400240"
            "20080d00410021020c010b2006200820056c36021c200620012802043602142004"
            "21020b20062002360218200641086a20042009200641146a10d380808000024020"
            "062802080d00200628020c21022001200736020020012002360204418180808078"
            "21020c010b20062802102103200628020c21020c010b0b20002003360204200020"
            "02360200200641206a2480808080000b0300000b0900200041003602000bc30201"
            "047f411f21020240200141ffffff074b0d002001410620014108766722026b7641"
            "017120024101746b413e6a21020b200042003702102000200236021c2002410274"
            "41cccfc080006a2103024041002802e8d2c0800041012002742204710d00200320"
            "00360200200020033602182000200036020c20002000360208410041002802e8d2"
            "c080002004723602e8d2c080000f0b024002400240200328020022042802044178"
            "712001470d00200421020c010b20014100411920024101766b2002411f461b7421"
            "03034020042003411d764104716a41106a22052802002202450d02200341017421"
            "032002210420022802044178712001470d000b0b20022802082203200036020c20"
            "022000360208200041003602182000200236020c200020033602080f0b20052000"
            "360200200020043602182000200036020c200020003602080b0b00200010d98080"
            "8000000bb50101037f23808080800041106b2201248080808000200028020c2102"
            "024002400240024020002802040e020001020b20020d0141012102410021030c02"
            "0b20020d00200028020022022802042103200228020021020c010b200141808080"
            "80783602002001200036020c2001418980808000200028021c22002d001c20002d"
            "001d10da80808000000b20012003360204200120023602002001418a8080800020"
            "0028021c22002d001c20002d001d10da80808000000b990101027f238080808000"
            "41106b2204248080808000410041002802c8cfc08000220541016a3602c8cfc080"
            "00024020054100480d000240024041002d0094d3c080000d0041004100280290d3"
            "c0800041016a360290d3c0800041002802c4cfc08000417f4a0d010c020b200441"
            "086a200020011183808080008080808000000b410041003a0094d3c08000200245"
            "0d0010d580808000000b000b0c00200020012902003703000b4a01037f41002103"
            "02402002450d000240034020002d0000220420012d00002205470d01200041016a"
            "2100200141016a21012002417f6a2202450d020c000b0b200420056b21030b2003"
            "0bac0501087f0240024002400240200020016b20024f0d00200120026a21032000"
            "20026a21040240200241104f0d00200021050c030b2004417c7121054100200441"
            "037122066b210702402006450d00200120026a417f6a210803402004417f6a2204"
            "20082d00003a00002008417f6a210820052004490d000b0b2005200220066b2209"
            "417c7122066b21040240200320076a2207410371450d0020064101480d02200741"
            "0374220841187121022007417c71220a417c6a2101410020086b4118712103200a"
            "280200210803402005417c6a220520082003742001280200220820027672360200"
            "2001417c6a210120042005490d000c030b0b20064101480d01200920016a417c6a"
            "210103402005417c6a220520012802003602002001417c6a210120042005490d00"
            "0c020b0b02400240200241104f0d00200021040c010b2000410020006b41037122"
            "036a210502402003450d0020002104200121080340200420082d00003a00002008"
            "41016a2108200441016a22042005490d000b0b2005200220036b2209417c712207"
            "6a210402400240200120036a2206410371450d0020074101480d01200641037422"
            "0841187121022006417c71220a41046a2101410020086b4118712103200a280200"
            "21080340200520082002762001280200220820037472360200200141046a210120"
            "0541046a22052004490d000c020b0b20074101480d002006210103402005200128"
            "0200360200200141046a2101200541046a22052004490d000b0b20094103712102"
            "200620076a21010b2002450d02200420026a21050340200420012d00003a000020"
            "0141016a2101200441016a22042005490d000c030b0b20094103712201450d0120"
            "07410020066b6a2103200420016b21050b2003417f6a210103402004417f6a2204"
            "20012d00003a00002001417f6a210120052004490d000b0b20000b0e0020002001"
            "200210dd808080000bc10201087f02400240200241104f0d00200021030c010b20"
            "00410020006b41037122046a210502402004450d00200021032001210603402003"
            "20062d00003a0000200641016a2106200341016a22032005490d000b0b20052002"
            "20046b2207417c7122086a210302400240200120046a2209410371450d00200841"
            "01480d012009410374220641187121022009417c71220a41046a2101410020066b"
            "4118712104200a2802002106034020052006200276200128020022062004747236"
            "0200200141046a2101200541046a22052003490d000c020b0b20084101480d0020"
            "092101034020052001280200360200200141046a2101200541046a22052003490d"
            "000b0b20074103712102200920086a21010b02402002450d00200320026a210503"
            "40200320012d00003a0000200141016a2101200341016a22032005490d000b0b20"
            "000b0bca4f0100418080c0000bc04f0b00000004000000040000000c0000006361"
            "6c6c65642060526573756c743a3a756e77726170282960206f6e20616e20604572"
            "72602076616c7565756c6c727565616c73657372632f6c69622e72730045001000"
            "0a000000150000004b000000450010000a000000160000004b0000004163636f75"
            "6e7400450010000a0000001700000033000000450010000a000000180000003300"
            "00006361706163697479206f766572666c6f770000009800100011000000616c6c"
            "6f632f7372632f7261775f7665632e7273b4001000140000001800000005000000"
            "2e2e3031323334353637383961626364656663616c6c656420604f7074696f6e3a"
            "3a756e77726170282960206f6e206120604e6f6e65602076616c7565696e646578"
            "206f7574206f6620626f756e64733a20746865206c656e20697320206275742074"
            "686520696e6465782069732000150110002000000035011000120000003a200000"
            "01000000000000005801100002000000636f72652f7372632f666d742f6e756d2e"
            "7273006c0110001300000066000000170000003078303030313032303330343035"
            "303630373038303931303131313231333134313531363137313831393230323132"
            "323233323432353236323732383239333033313332333333343335333633373338"
            "333934303431343234333434343534363437343834393530353135323533353435"
            "353536353735383539363036313632363336343635363636373638363937303731"
            "373237333734373537363737373837393830383138323833383438353836383738"
            "3838393930393139323933393439353936393739383939636f72652f7372632f66"
            "6d742f6d6f642e72730000005a021000130000009b090000260000005a02100013"
            "000000a40900001a00000072616e676520737461727420696e64657820206f7574"
            "206f662072616e676520666f7220736c696365206f66206c656e67746820900210"
            "0012000000a20210002200000072616e676520656e6420696e64657820d4021000"
            "10000000a202100022000000736c69636520696e64657820737461727473206174"
            "202062757420656e64732061742000f4021000160000000a0310000d0000000101"
            "010101010101010101010101010101010101010101010101010101010101010101"
            "010101010101010101010101010101010101010101010101010101010101010101"
            "010101010101010101010101010101010101010101010101010101010101010101"
            "010101010101010101010101010101010101010101010101010101000000000000"
            "000000000000000000000000000000000000000000000000000000000000000000"
            "000000000000000000000000000000000000000000000000000000020202020202"
            "020202020202020202020202020202020202020202020202030303030303030303"
            "03030303030303040404040400000000000000000000005b2e2e2e5d626567696e"
            "203c3d20656e642028203c3d2029207768656e20736c6963696e672060602d0410"
            "000e0000003b041000040000003f041000100000004f0410000100000062797465"
            "20696e64657820206973206e6f742061206368617220626f756e646172793b2069"
            "7420697320696e7369646520202862797465732029206f66206000700410000b00"
            "00007b04100026000000a104100008000000a9041000060000004f041000010000"
            "00206973206f7574206f6620626f756e6473206f6620600000700410000b000000"
            "d8041000160000004f04100001000000636f72652f7372632f7374722f6d6f642e"
            "7273000805100013000000f00000002c000000636f72652f7372632f756e69636f"
            "64652f7072696e7461626c652e72730000002c0510001d0000001a000000360000"
            "002c0510001d0000000a0000002b00000000060101030104020507070208080902"
            "0a050b020e041001110212051311140115021702190d1c051d081f0124016a046b"
            "02af03b102bc02cf02d102d40cd509d602d702da01e005e102e704e802ee20f004"
            "f802fa03fb010c273b3e4e4f8f9e9e9f7b8b9396a2b2ba86b1060709363d3e56f3"
            "d0d1041418363756577faaaeafbd35e01287898e9e040d0e11122931343a454649"
            "4a4e4f64655cb6b71b1c07080a0b141736393aa8a9d8d909379091a8070a3b3e66"
            "698f92116f5fbfeeef5a62f4fcff53549a9b2e2f2728559da0a1a3a4a7a8adbabc"
            "c4060b0c151d3a3f4551a6a7cccda007191a22253e3fe7ecefffc5c60420232526"
            "2833383a484a4c50535556585a5c5e606365666b73787d7f8aa4aaafb0c0d0aeaf"
            "6e6fbe935e227b0503042d036603012f2e80821d03310f1c0424091e052b054404"
            "0e2a80aa06240424042808340b4e43813709160a08183b45390363080930160521"
            "031b05014038044b052f040a070907402027040c0936033a051a07040c07504937"
            "330d33072e080a8126524b2b082a161a261c1417094e042409440d19070a064808"
            "2709750b423e2a063b050a0651060105100305808b621e48080a80a65e22450b0a"
            "060d133a060a362c041780b93c64530c48090a46451b4808530d49070a80f6460a"
            "1d03474937030e080a0639070a813619073b031c56010f320d839b66750b80c48a"
            "4c630d843010168faa8247a1b98239072a045c06260a460a28051382b05b654b04"
            "39071140050b020e97f80884d62a09a2e781330f011d060e0408818c89046b050d"
            "0309071092604709743c80f60a73087015467a140c140c57091980878147038542"
            "0f1584501f060680d52b053e2101702d031a040281401f113a050181d02a82e680"
            "f7294c040a04028311444c3d80c23c06010455051b3402810e2c04640c560a80ae"
            "381d0d2c040907020e06809a83d80411030d0377045f060c04010f0c0438080a06"
            "2808224e81540c1d03090736080e040907090780cb250a84060001030505060602"
            "0706080709110a1c0b190c1a0d100e0c0f0410031212130916011704180119031a"
            "071b011c021f1620032b032d0b2e01300431023201a702a902aa04ab08fa02fb05"
            "fd02fe03ff09ad78798b8da23057588b8c901cdd0e0f4b4cfbfc2e2f3f5c5d5fe2"
            "848d8e9192a9b1babbc5c6c9cadee4e5ff00041112293134373a3b3d494a5d848e"
            "92a9b1b4babbc6cacecfe4e500040d0e11122931343a3b4546494a5e646584919b"
            "9dc9cecf0d11293a3b4549575b5c5e5f64658d91a9b4babbc5c9dfe4e5f00d1145"
            "4964658084b2bcbebfd5d7f0f183858ba4a6bebfc5c7cfdadb4898bdcdc6cecf49"
            "4e4f57595e5f898e8fb1b6b7bfc1c6c7d71116175b5cf6f7feff806d71dedf0e1f"
            "6e6f1c1d5f7d7eaeaf7fbbbc16171e1f46474e4f585a5c5e7e7fb5c5d4d5dcf0f1"
            "f572738f747596262e2fa7afb7bfc7cfd7df9a00409798308f1fd2d4ceff4e4f5a"
            "5b07080f10272feeef6e6f373d3f42459091536775c8c9d0d1d8d9e7feff00205f"
            "2282df048244081b04061181ac0e80ab051f09811b03190801042f043404070301"
            "070607110a500f1207550703041c0a090308030703020303030c0405030b06010e"
            "15054e071b0757070206170c500443032d03010411060f0c3a041d255f206d046a"
            "2580c80582b0031a0682fd03590716091809140c140c6a060a061a0659072b0546"
            "0a2c040c040103310b2c041a060b0380ac060a062f314d0380a4083c030f033c07"
            "38082b0582ff1118082f112d03210f210f808c048297190b158894052f053b0702"
            "0e180980be22740c80d61a81100580df0bf29e033709815c1480b80880cb050a18"
            "3b030a06380846080c06740b1e035a0459098083181c0a16094c04808a06aba40c"
            "170431a10481da26070c050580a61081f50701202a064c04808d0480be031b030f"
            "0d636f72652f7372632f756e69636f64652f756e69636f64655f646174612e7273"
            "00e70a1000200000005000000028000000e70a1000200000005c00000016000000"
            "0003000083042000910560005d13a0001217201f0c20601fef2ca02b2a30202c6f"
            "a6e02c02a8602d1efb602e00fe20369eff6036fd01e136010a2137240de137ab0e"
            "61392f18a139301c6148f31ea14c40346150f06aa1514f6f21529dbca15200cf61"
            "5365d1a15300da215400e0e155aee26157ece42159d0e8a1592000ee59f0017f5a"
            "00700007002d0101010201020101480b30151001650702060202010423011e1b5b"
            "0b3a09090118040109010301052b033c082a180120370101010408040103070a02"
            "1d013a0101010204080109010a021a010202390104020402020303011e0203010b"
            "0239010405010204011402160601013a0101020104080107030a021e013b010101"
            "0c01090128010301370101030503010407020b021d013a01020102010301050207"
            "020b021c02390201010204080109010a021d014801040102030101080151010207"
            "0c08620102090b0749021b0101010101370e01050102050b012409016604010601"
            "0202021902040310040d01020206010f01000300031d021e021e02400201070801"
            "020b09012d030101750222017603040209010603db0202013a0101070101010102"
            "08060a0201301f310430070101050128090c022004020201033801010203010103"
            "3a0802029803010d0107040106010302c6400001c32100038d0160200006690200"
            "04010a200250020001030104011902050197021a120d012608190b2e0330010204"
            "020227014306020202020c0108012f01330101030202050201012a020801ee0102"
            "01040100010010101000020001e201950500030102050428030401a50200040002"
            "5003460b31047b01360f290102020a033104020207013d03240501083e010c0234"
            "090a0402015f0302010102060102019d010308150239020101010116010e070305"
            "c308020301011701510102060101020101020102eb010204060201021b02550802"
            "0101026a0101010206010165030204010500090102f5010a020101040190040202"
            "0401200a280602040801090602032e0d010200070106010152160207010201027a"
            "06030101020107010148020301010100020b023405050101010001060f00053b07"
            "00013f0451010002002e0217000101030405080802071e0494030037043208010e"
            "011605010f000701110207010201056401a00700013d04000400076d07006080f0"
            "002f72757374632f63326637346333663932386165623530336631356234653965"
            "6635373738653737663330353862382f6c6962726172792f616c6c6f632f737263"
            "2f636f6c6c656374696f6e732f62747265652f6d61702f656e7472792e72730083"
            "0e10006000000071010000360000002f72757374632f6332663734633366393238"
            "61656235303366313562346539656635373738653737663330353862382f6c6962"
            "726172792f616c6c6f632f7372632f636f6c6c656374696f6e732f62747265652f"
            "6e6f64652e7273617373657274696f6e206661696c65643a20656467652e686569"
            "676874203d3d2073656c662e686569676874202d203100f40e10005b000000af02"
            "000009000000617373657274696f6e206661696c65643a207372632e6c656e2829"
            "203d3d206473742e6c656e2829f40e10005b0000002f07000005000000f40e1000"
            "5b000000af04000023000000f40e10005b000000ef040000240000006173736572"
            "74696f6e206661696c65643a20656467652e686569676874203d3d2073656c662e"
            "6e6f64652e686569676874202d2031000000f40e10005b000000f0030000090000"
            "00181d10005f00000058020000300000000d0000000c000000040000000e000000"
            "0f00000010000000000000000000000001000000110000006120446973706c6179"
            "20696d706c656d656e746174696f6e2072657475726e656420616e206572726f72"
            "20756e65787065637465646c792f72757374632f63326637346333663932386165"
            "6235303366313562346539656635373738653737663330353862382f6c69627261"
            "72792f616c6c6f632f7372632f737472696e672e727300009f1010004b00000006"
            "0a00000e0000004572726f72454f46207768696c652070617273696e672061206c"
            "697374454f46207768696c652070617273696e6720616e206f626a656374454f46"
            "207768696c652070617273696e67206120737472696e67454f46207768696c6520"
            "70617273696e6720612076616c7565657870656374656420603a60657870656374"
            "656420602c60206f7220605d60657870656374656420602c60206f7220607d6065"
            "78706563746564206964656e7465787065637465642076616c7565657870656374"
            "656420602260696e76616c696420657363617065696e76616c6964206e756d6265"
            "726e756d626572206f7574206f662072616e6765696e76616c696420756e69636f"
            "646520636f646520706f696e74636f6e74726f6c2063686172616374657220285c"
            "75303030302d5c75303031462920666f756e64207768696c652070617273696e67"
            "206120737472696e676b6579206d757374206265206120737472696e67696e7661"
            "6c69642076616c75653a206578706563746564206b657920746f2062652061206e"
            "756d62657220696e2071756f746573666c6f6174206b6579206d75737420626520"
            "66696e6974652028676f74204e614e206f72202b2f2d696e66296c6f6e65206c65"
            "6164696e6720737572726f6761746520696e206865782065736361706574726169"
            "6c696e6720636f6d6d61747261696c696e672063686172616374657273756e6578"
            "70656374656420656e64206f662068657820657363617065726563757273696f6e"
            "206c696d69742065786365656465644572726f72282c206c696e653a202c20636f"
            "6c756d6e3a2000000035131000060000003b13100008000000431310000a000000"
            "4826100001000000000000000000f03f0000000000002440000000000000594000"
            "00000000408f40000000000088c34000000000006af8400000000080842e410000"
            "0000d01263410000000084d797410000000065cdcd41000000205fa00242000000"
            "e876483742000000a2941a6d42000040e59c30a2420000901ec4bcd64200003426"
            "f56b0c430080e03779c3414300a0d8855734764300c84e676dc1ab43003d9160e4"
            "58e143408cb5781daf154450efe2d6e41a4b4492d54d06cff08044f64ae1c7022d"
            "b544b49dd9794378ea449102282c2a8b2045350332b7f4ad54450284fee471d989"
            "4581121f2fe727c04521d7e6fae031f445ea8ca039593e294624b00888ef8d5f46"
            "176e05b5b5b893469cc94622e3a6c846037cd8ea9bd0fe46824dc77261423347e3"
            "2079cff91268471b695743b8179e47b1a1162ad3ced2471d4a9cf487820748a55c"
            "c3f129633d48e7191a37fa5d724861a0e0c478f5a64879c818f6d6b2dc484c7dcf"
            "59c6ef11499e5c43f0b76b4649c63354eca5067c495ca0b4b32784b14973c8a1a0"
            "31e5e5498f3aca087e5e1b4a9a647ec50e1b514ac0fddd76d261854a307d951447"
            "baba4a3e6edd6c6cb4f04acec9148887e1244b41fc196ae9195a4ba93d50e23150"
            "904b134de45a3e64c44b57609df14d7df94b6db8046ea1dc2f4c44f3c2e4e4e963"
            "4c15b0f31d5ee4984c1b9c70a5751dcf4c916166876972034df5f93fe9034f384d"
            "72f88fe3c4626e4d47fb390ebbfda24d197ac8d129bdd74d9f983a4674ac0d4e64"
            "9fe4abc88b424e3dc7ddd6ba2e774e0c39958c69faac4ea743ddf7811ce24e9194"
            "d475a2a3164fb5b949138b4c4c4f11140eecd6af814f169911a7cc1bb64f5bffd5"
            "d0bfa2eb4f99bf85e2b74521507f2f27db259755505ffbf051effc8a501b9d3693"
            "15dec050624404f89a15f5507b5505b6015b2a516d55c311e1786051c82a345619"
            "9794517a35c1abdfbcc9516cc158cb0b160052c7f12ebe8e1b345239aeba6d7222"
            "6952c75929090f6b9f521dd8b965e9a2d352244e28bfa38b0853ad61f2ae8cae3e"
            "530c7d57ed172d73534f5cade85df8a75363b3d86275f6dd531e70c75d09ba1254"
            "254c39b58b6847542e9f87a2ae427d547dc39425ad49b2545cf4f96e18dce65473"
            "71b88a1e931c55e846b316f3db5155a21860dcef528655ca1e78d3abe7bb553f13"
            "2b64cb70f1550ed8353dfecc2556124e83cc3d405b56cb10d29f26089156fe94c6"
            "47304ac5563d3ab859bc9cfa56662413b8f5a1305780ed172673ca6457e0e89def"
            "0ffd99578cb1c2f5293ed057ef5d3373b44d04586b35009021613958c54200f469"
            "b96f58bb298038e2d3a3582a34a0c6dac8d8583541487811fb0e59c1282debea5c"
            "4359f172f8a525347859ad8f760f2f41ae59cc19aa69bde8e2593fa014c4eca217"
            "5a4fc819f5a78b4d5a321d30f94877825a7e247c371b15b75a9e2d5b0562daec5a"
            "82fc58437d08225ba33b2f949c8a565b8c0a3bb9432d8c5b97e6c4534a9cc15b3d"
            "20b6e85c03f65b4da8e32234842b5c3049ce95a032615c7cdb41bb487f955c5b52"
            "12ea1adfca5c79734bd270cb005d5750de064dfe345d6de49548e03d6a5dc4ae5d"
            "2dac66a05d751ab5385780d45d1261e2066da0095eab7c4d244404405ed6db602d"
            "5505745ecc12b978aa06a95e7f57e7165548df5eaf96502e358d135f5bbce47982"
            "70485f72eb5d18a38c7e5f27b33aefe517b35ff15f096bdfdde75fedb7cb4557d5"
            "1d60f4529f8b56a55260b127872eac4e87609df1283a5722bd60029759847635f2"
            "60c3fc6f25d4c22661f4fbcb2e89735c61787d3fbd35c89161d65c8f2c433ac661"
            "0c34b3f7d3c8fb618700d07a845d3162a9008499e5b46562d400e5ff1e229b6284"
            "20ef5f53f5d062a5e8ea37a8320563cfa2e545527f3a63c185af6b938f70633267"
            "9b4678b3a463fe40425856e0d9639f6829f7352c1064c6c2f3744337446478b330"
            "521445796456e0bc665996af64360c36e0f7bde364438f43d875ad18651473544e"
            "d3d84e65ecc7f41084478365e8f931156519b86561787e5abe1fee653d0b8ff8d6"
            "d322660cceb2b6cc8857668f815fe4ff6a8d66f9b0bbeedf62c266389d6aea97fb"
            "f666864405e57dba2c67d44a23af8ef46167891dec5ab2719667eb24a7f11e0ecc"
            "6713770857d3880168d794ca2c08eb35680d3afd37ca656b684844fe629e1fa168"
            "5ad5bdfb8567d568b14aad7a67c10a69af4eacace0b840695a62d7d718e77469f1"
            "3acd0ddf20aa69d644a0688b54e0690c56c842ae69146a8f6b7ad31984496a7306"
            "594820e57f6a08a4372d34efb36a0a8d853801ebe86a4cf0a686c1251f6b305628"
            "f49877536bbb6b32317f55886baa067ffdde6abe6b2a646f5ecb02f36b353d0b36"
            "7ec3276c820c8ec35db45d6cd1c7389aba90926cc6f9c640e934c76c37b8f89023"
            "02fd6c23739b3a5621326deb4f42c9aba9666de6e392bb16549c6d70ce3b358eb4"
            "d16d0cc28ac2b121066e8f722d331eaa3b6e9967fcdf524a716e7f81fb97e79ca5"
            "6edf61fa7d2104db6e2c7dbcee94e2106f769c6b2a3a1b456f948306b508627a6f"
            "3d122471457db06fcc166dcd969ce46f7f5cc880bcc31970cf397dd0551a507043"
            "889c44eb20847054aac3152629b970e994349b6f73ef7011dd00c125a823715614"
            "41312f9258716b5991fdbab68e71e3d77ade3432c371dc8d1916c2fef77153f19f"
            "9b72fe2d72d4f643a107bf627289f49489c96e9772ab31faeb7b4acd720b5f7c73"
            "8d4e0273cd765bd030e2367381547204bd9a6c73d074c722b6e0a173045279abe3"
            "58d67386a657961cef0b7414c8f6dd71754174187a7455ced275749e98d1ea8147"
            "ab7463ffc232b10ce1743cbf737fdd4f15750baf50dfd4a34a75676d920b65a680"
            "75c008774efecfb475f1ca14e2fd03ea75d6fe4cad7e4220768c3ea0581e535476"
            "2f4ec8eee5678976bb617a6adfc1bf76157d8ca22bd9f3765a9c2f8b76cf287770"
            "83fb2d54035f772632bd9c14629377b07eecc3993ac8775c9ee7344049fe77f9c2"
            "1021c8ed3278b8f354293aa96778a530aab388939d78675e4a70357cd27801f65c"
            "cc421b07798233747f13e23c7931a0a82f4c0d72793dc8923b9f90a6794d7a770a"
            "c734dc7970ac8a66fca0117a8c572d803b09467a6fad38608a8b7b7a656c237c36"
            "37b17a7f472c1b0485e57a5e59f72145e61a7bdb973a35ebcf507bd23d8902e603"
            "857b468d2b83df44ba7b4c38fbb10b6bf07b5f067a9ece85247cf687184642a759"
            "7cfa54cf6b8908907c382ac3c6ab0ac47cc7f473b8560df97cf8f19066ac502f7d"
            "3b971ac06b92637d0a3d21b00677987d4c8c295cc894ce7db0f79939fd1c037e9c"
            "7500883ce4377e039300aa4bdd6d7ee25b404a4faaa27eda72d01ce354d77e908f"
            "04e41b2a0d7fbad9826e513a427f299023cae5c8767f3374ac3c1f7bac7fa0c8eb"
            "85f3cce17f2f72757374632f633266373463336639323861656235303366313562"
            "346539656635373738653737663330353862382f6c6962726172792f616c6c6f63"
            "2f7372632f636f6c6c656374696f6e732f62747265652f6e617669676174652e72"
            "7300181d10005f000000c600000027000000181d10005f000000160200002f0000"
            "00181d10005f000000a1000000240000002f686f6d652f7077616e672f2e636172"
            "676f2f72656769737472792f7372632f696e6465782e6372617465732e696f2d36"
            "6631376432326262613135303031662f73657264655f6a736f6e2d312e302e3133"
            "352f7372632f726561642e727300a81d10005f000000a001000045000000a81d10"
            "005f000000a50100003d000000a81d10005f000000ad0100001a000000a81d1000"
            "5f000000fa01000013000000a81d10005f000000030200003e000000a81d10005f"
            "000000ff01000033000000a81d10005f000000090200003a000000a81d10005f00"
            "00006802000019000000ffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffff0000010002000300040005000600070008000900ffffffffffff"
            "ffffffffffffffff0a000b000c000d000e000f00ffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffff0a000b000c000d000e000f00ffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffff000010002000300040"
            "0050006000700080009000ffffffffffffffffffffffffffffa000b000c000d000"
            "e000f000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffa000b000c000d000e000"
            "f000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffff0d0000000c000000040000000e0000000f0000001000"
            "00002f727573742f646570732f646c6d616c6c6f632d302e322e362f7372632f64"
            "6c6d616c6c6f632e7273617373657274696f6e206661696c65643a207073697a65"
            "203e3d2073697a65202b206d696e5f6f7665726865616400a022100029000000a8"
            "04000009000000617373657274696f6e206661696c65643a207073697a65203c3d"
            "2073697a65202b206d61785f6f766572686561640000a022100029000000ae0400"
            "000d0000000100000000000000656e74697479206e6f7420666f756e647065726d"
            "697373696f6e2064656e696564636f6e6e656374696f6e2072656675736564636f"
            "6e6e656374696f6e207265736574686f737420756e726561636861626c656e6574"
            "776f726b20756e726561636861626c65636f6e6e656374696f6e2061626f727465"
            "646e6f7420636f6e6e65637465646164647265737320696e207573656164647265"
            "7373206e6f7420617661696c61626c656e6574776f726b20646f776e62726f6b65"
            "6e2070697065656e7469747920616c7265616479206578697374736f7065726174"
            "696f6e20776f756c6420626c6f636b6e6f742061206469726563746f7279697320"
            "61206469726563746f72796469726563746f7279206e6f7420656d707479726561"
            "642d6f6e6c792066696c6573797374656d206f722073746f72616765206d656469"
            "756d66696c6573797374656d206c6f6f70206f7220696e646972656374696f6e20"
            "6c696d69742028652e672e2073796d6c696e6b206c6f6f70297374616c65206e65"
            "74776f726b2066696c652068616e646c65696e76616c696420696e707574207061"
            "72616d65746572696e76616c6964206461746174696d6564206f75747772697465"
            "207a65726f6e6f2073746f726167652073706163657365656b206f6e20756e7365"
            "656b61626c652066696c6566696c6573797374656d2071756f7461206578636565"
            "64656466696c6520746f6f206c617267657265736f757263652062757379657865"
            "63757461626c652066696c652062757379646561646c6f636b63726f73732d6465"
            "76696365206c696e6b206f722072656e616d65746f6f206d616e79206c696e6b73"
            "696e76616c69642066696c656e616d65617267756d656e74206c69737420746f6f"
            "206c6f6e676f7065726174696f6e20696e746572727570746564756e737570706f"
            "72746564756e657870656374656420656e64206f662066696c656f7574206f6620"
            "6d656d6f72796f74686572206572726f72756e63617465676f72697a6564206572"
            "726f7220286f73206572726f72202900000001000000000000003d2610000b0000"
            "0048261000010000006f7065726174696f6e207375636365737366756c10000000"
            "1100000012000000100000001000000013000000120000000d0000000e00000015"
            "0000000c0000000b00000015000000150000000f0000000e000000130000002600"
            "00003800000019000000170000000c000000090000000a00000010000000170000"
            "00190000000e0000000d00000014000000080000001b0000000e00000010000000"
            "16000000150000000b000000160000000d0000000b000000130000005023100060"
            "231000712310008323100093231000a3231000b6231000c8231000d5231000e323"
            "1000f8231000042410000f24100024241000392410004824100056241000692410"
            "008f241000c7241000e0241000f7241000032510000c2510001625100026251000"
            "3d251000562510006425100071251000852510008d251000a8251000b6251000c6"
            "251000dc251000f1251000fc251000122610001f2610002a26100000ac3a046e61"
            "6d65000e0d7761736d5f6c69622e7761736d01f4396000325f5a4e313073657264"
            "655f6a736f6e326465313066726f6d5f736c696365313768313163653038373736"
            "34633961376230450188015f5a4e313073657264655f6a736f6e3576616c756532"
            "646537375f244c5424696d706c247532302473657264652e2e64652e2e44657365"
            "7269616c697a652475323024666f72247532302473657264655f6a736f6e2e2e76"
            "616c75652e2e56616c7565244754243131646573657269616c697a653137683331"
            "653531373831633833363837353945020e5f5f727573745f6465616c6c6f630345"
            "5f5a4e313073657264655f6a736f6e347265616439536c69636552656164313770"
            "6f736974696f6e5f6f665f696e6465783137683236623431383938353234383332"
            "39364504435f5a4e38646c6d616c6c6f6338646c6d616c6c6f633137446c6d616c"
            "6c6f63244c54244124475424366d616c6c6f633137686536353933396134633839"
            "376363313545054f5f5a4e34636f726533707472343564726f705f696e5f706c61"
            "6365244c542473657264655f6a736f6e2e2e76616c75652e2e56616c7565244754"
            "243137683533326265333033376461316237656445064a5f5a4e31307365726465"
            "5f6a736f6e3264653231446573657269616c697a6572244c542452244754243131"
            "70617273655f6964656e743137683663353964643731393635353139313045074b"
            "5f5a4e313073657264655f6a736f6e3264653231446573657269616c697a657224"
            "4c54245224475424313270617273655f6e756d6265723137683738336134316134"
            "623931306464323045084c5f5a4e313073657264655f6a736f6e32646532314465"
            "73657269616c697a6572244c54245224475424313370617273655f646563696d61"
            "6c3137683661306333363832326663336530306145094d5f5a4e31307365726465"
            "5f6a736f6e3264653231446573657269616c697a6572244c542452244754243134"
            "70617273655f6578706f6e656e7431376833366464376462643233653461346562"
            "450a555f5a4e313073657264655f6a736f6e3264653231446573657269616c697a"
            "6572244c54245224475424323270617273655f646563696d616c5f6f766572666c"
            "6f7731376833613030656365646638363031386433450b4d5f5a4e313073657264"
            "655f6a736f6e3264653231446573657269616c697a6572244c5424522447542431"
            "346636345f66726f6d5f7061727473313768633863316239626161613836666637"
            "33450c565f5a4e313073657264655f6a736f6e3264653231446573657269616c69"
            "7a6572244c54245224475424323370617273655f6578706f6e656e745f6f766572"
            "666c6f7731376830343762396637333562616463666138450d4f5f5a4e31307365"
            "7264655f6a736f6e3264653231446573657269616c697a6572244c542452244754"
            "24313670617273655f616e795f6e756d6265723137683931643533303465356139"
            "6363663531450e515f5a4e313073657264655f6a736f6e32646532314465736572"
            "69616c697a6572244c54245224475424313870617273655f6c6f6e675f696e7465"
            "67657231376864383130373866346133316332626532450f3d5f5a4e3130736572"
            "64655f6a736f6e356572726f72354572726f7231326669785f706f736974696f6e"
            "313768386631666565323432343761346639634510435f5a4e35616c6c6f633772"
            "61775f7665633139526177566563244c54245424432441244754243867726f775f"
            "6f6e65313768663733333137633566643665626336364511645f5a4e37305f244c"
            "5424616c6c6f632e2e7665632e2e566563244c5424542443244124475424247532"
            "302461732475323024636f72652e2e6f70732e2e64726f702e2e44726f70244754"
            "243464726f703137686431353834386335383231633466666545124f5f5a4e3463"
            "6f726533707472343564726f705f696e5f706c616365244c542473657264655f6a"
            "736f6e2e2e6572726f722e2e4572726f7224475424313768663837633864366463"
            "396162346263354513695f5a4e37305f244c542473657264655f6a736f6e2e2e72"
            "6561642e2e536c6963655265616424753230246173247532302473657264655f6a"
            "736f6e2e2e726561642e2e52656164244754243970617273655f73747231376861"
            "62653863353535633862636433353545143c5f5a4e357365726465326465375669"
            "7369746f72313876697369745f626f72726f7765645f7374723137683435643731"
            "31633837313638633266364515335f5a4e35616c6c6f63377261775f7665633132"
            "68616e646c655f6572726f72313768393762376462643066373264643738384516"
            "3a5f5a4e313073657264655f6a736f6e32646531325061727365724e756d626572"
            "35766973697431376838366238393638313662613130613765451781015f5a4e37"
            "355f244c542473657264655f6a736f6e2e2e64652e2e4d6170416363657373244c"
            "5424522447542424753230246173247532302473657264652e2e64652e2e4d6170"
            "4163636573732447542431336e6578745f6b65795f7365656431326861735f6e65"
            "78745f6b6579313768356461326634303536653538313464394518695f5a4e3730"
            "5f244c542473657264652e2e64652e2e696d706c732e2e537472696e6756697369"
            "746f7224753230246173247532302473657264652e2e64652e2e56697369746f72"
            "244754243976697369745f73747231376835356436653830653061376366383938"
            "4519755f5a4e37355f244c542473657264655f6a736f6e2e2e64652e2e4d617041"
            "6363657373244c5424522447542424753230246173247532302473657264652e2e"
            "64652e2e4d61704163636573732447542431356e6578745f76616c75655f736565"
            "6431376865633835363737653830316539393133451a565f5a4e35616c6c6f6331"
            "31636f6c6c656374696f6e73356274726565336d6170323542547265654d617024"
            "4c54244b24432456244324412447542436696e7365727431376834643164623464"
            "613838343264346665451b81015f5a4e39395f244c5424616c6c6f632e2e636f6c"
            "6c656374696f6e732e2e62747265652e2e6d61702e2e42547265654d6170244c54"
            "244b244324562443244124475424247532302461732475323024636f72652e2e6f"
            "70732e2e64726f702e2e44726f70244754243464726f7031376835346633306630"
            "323133646334313362451c5d5f5a4e36355f244c542473657264655f6a736f6e2e"
            "2e76616c75652e2e56616c7565247532302461732475323024636f72652e2e636d"
            "702e2e5061727469616c4571244754243265713137683162323138393234373831"
            "3936633830451d8b015f5a4e3130385f244c5424616c6c6f632e2e636f6c6c6563"
            "74696f6e732e2e62747265652e2e6d61702e2e49746572244c54244b2443245624"
            "475424247532302461732475323024636f72652e2e697465722e2e747261697473"
            "2e2e6974657261746f722e2e4974657261746f7224475424346e65787431376835"
            "363664323036316535613937646164451e08616c6c6f636174651f11636f6d7061"
            "72655f6163636f756e744944205c5f5a4e35355f244c5424737472247532302461"
            "73247532302473657264655f6a736f6e2e2e76616c75652e2e696e6465782e2e49"
            "6e646578244754243130696e6465785f696e746f31376864333238633634636161"
            "3964313761634521325f5a4e34636f726536726573756c743133756e777261705f"
            "6661696c6564313768663839396364303037373637303035314522325f5a4e3463"
            "6f7265366f7074696f6e3133756e777261705f6661696c65643137683335353139"
            "64653938613737363134664523385f5a4e35616c6c6f63377261775f7665633137"
            "63617061636974795f6f766572666c6f7731376834393964343832613965643537"
            "3135614524305f5a4e34636f72653970616e69636b696e673970616e69635f666d"
            "74313768363534306363623264356664633361624525415f5a4e38646c6d616c6c"
            "6f6338646c6d616c6c6f633137446c6d616c6c6f63244c54244124475424346672"
            "65653137683339383334616161616533653839343645262c5f5a4e34636f726539"
            "70616e69636b696e673570616e6963313768303465656239313764643933633232"
            "3945270e5f5f727573745f7265616c6c6f63284a5f5a4e38646c6d616c6c6f6338"
            "646c6d616c6c6f633137446c6d616c6c6f63244c542441244754243132756e6c69"
            "6e6b5f6368756e6b3137683933346533646333383362623538613345294b5f5a4e"
            "38646c6d616c6c6f6338646c6d616c6c6f633137446c6d616c6c6f63244c542441"
            "244754243133646973706f73655f6368756e6b3137683665306363636434353836"
            "3537343633452a445f5a4e34636f726535736c69636535696e6465783236736c69"
            "63655f73746172745f696e6465785f6c656e5f6661696c31376866393161336166"
            "653837623164343433452b625f5a4e34636f726533666d74336e756d33696d7035"
            "325f244c5424696d706c2475323024636f72652e2e666d742e2e446973706c6179"
            "2475323024666f7224753230247533322447542433666d74313768626633653032"
            "32383438336533373561452c3a5f5a4e34636f72653970616e69636b696e673138"
            "70616e69635f626f756e64735f636865636b313768336436623861613463383034"
            "39363632452d305f5a4e34636f726533666d74336e756d33696d7037666d745f75"
            "363431376864353231666136656636613036373261452e11727573745f62656769"
            "6e5f756e77696e642f385f5a4e34636f726533666d7439466f726d617474657231"
            "327061645f696e74656772616c3137686334656130376130626331333536633445"
            "30465f5a4e34636f726533666d7439466f726d617474657231327061645f696e74"
            "656772616c313277726974655f7072656669783137686139613433323830623630"
            "30366431324531425f5a4e34636f726535736c69636535696e6465783234736c69"
            "63655f656e645f696e6465785f6c656e5f6661696c313768303838623536653239"
            "3962656161616645322e5f5a4e34636f726533666d7439466f726d617474657233"
            "706164313768343736396165333839333734636335314533495f5a4e34345f244c"
            "54242452462454247532302461732475323024636f72652e2e666d742e2e446973"
            "706c61792447542433666d74313768376666346430623836303963323437324534"
            "475f5a4e34325f244c54242452462454247532302461732475323024636f72652e"
            "2e666d742e2e44656275672447542433666d743137683361366261613162623437"
            "61643230344535265f5a4e34636f726533666d7435777269746531376839333535"
            "34653462653731663263376145365c5f5a4e34636f726533666d74336e756d3530"
            "5f244c5424696d706c2475323024636f72652e2e666d742e2e4465627567247532"
            "3024666f7224753230247533322447542433666d74313768353533393862313635"
            "353064353237654537405f5a4e34636f726535736c69636535696e646578323273"
            "6c6963655f696e6465785f6f726465725f6661696c313768353862336536383666"
            "653333373030654538535f5a4e34636f72653463686172376d6574686f64733232"
            "5f244c5424696d706c2475323024636861722447542431366573636170655f6465"
            "6275675f657874313768656366613566303431373437393039384539345f5a4e34"
            "636f726537756e69636f6465397072696e7461626c6535636865636b3137683664"
            "6136346638306663313630633761453a325f5a4e34636f7265337374723136736c"
            "6963655f6572726f725f6661696c31376862303364323439386438646362363433"
            "453b355f5a4e34636f7265337374723139736c6963655f6572726f725f6661696c"
            "5f727431376832616462643139306563313832373933453c645f5a4e37315f244c"
            "5424636f72652e2e6f70732e2e72616e67652e2e52616e6765244c542449647824"
            "475424247532302461732475323024636f72652e2e666d742e2e44656275672447"
            "542433666d7431376836636632383632303536616535653233453d465f5a4e3431"
            "5f244c542463686172247532302461732475323024636f72652e2e666d742e2e44"
            "656275672447542433666d7431376865613566643964626339343936626665453e"
            "625f5a4e34636f726533666d74336e756d33696d7035325f244c5424696d706c24"
            "75323024636f72652e2e666d742e2e446973706c61792475323024666f72247532"
            "30246933322447542433666d743137686365643930633761363339633031646445"
            "3fce015f5a4e35616c6c6f633131636f6c6c656374696f6e73356274726565346e"
            "6f64653132374e6f6465526566244c5424616c6c6f632e2e636f6c6c656374696f"
            "6e732e2e62747265652e2e6e6f64652e2e6d61726b65722e2e4479696e67244324"
            "4b24432456244324616c6c6f632e2e636f6c6c656374696f6e732e2e6274726565"
            "2e2e6e6f64652e2e6d61726b65722e2e4c6561664f72496e7465726e616c244754"
            "2432316465616c6c6f636174655f616e645f617363656e64313768353839613732"
            "6639343233626663656245409a025f5a4e35616c6c6f633131636f6c6c65637469"
            "6f6e73356274726565346e6f646532313448616e646c65244c5424616c6c6f632e"
            "2e636f6c6c656374696f6e732e2e62747265652e2e6e6f64652e2e4e6f64655265"
            "66244c5424616c6c6f632e2e636f6c6c656374696f6e732e2e62747265652e2e6e"
            "6f64652e2e6d61726b65722e2e4d75742443244b24432456244324616c6c6f632e"
            "2e636f6c6c656374696f6e732e2e62747265652e2e6e6f64652e2e6d61726b6572"
            "2e2e496e7465726e616c24475424244324616c6c6f632e2e636f6c6c656374696f"
            "6e732e2e62747265652e2e6e6f64652e2e6d61726b65722e2e4564676524475424"
            "3130696e736572745f666974313768633861306366353339656666303131314541"
            "92025f5a4e35616c6c6f633131636f6c6c656374696f6e73356274726565346e6f"
            "646532313248616e646c65244c5424616c6c6f632e2e636f6c6c656374696f6e73"
            "2e2e62747265652e2e6e6f64652e2e4e6f6465526566244c5424616c6c6f632e2e"
            "636f6c6c656374696f6e732e2e62747265652e2e6e6f64652e2e6d61726b65722e"
            "2e4d75742443244b24432456244324616c6c6f632e2e636f6c6c656374696f6e73"
            "2e2e62747265652e2e6e6f64652e2e6d61726b65722e2e496e7465726e616c2447"
            "5424244324616c6c6f632e2e636f6c6c656374696f6e732e2e62747265652e2e6e"
            "6f64652e2e6d61726b65722e2e4b56244754243573706c69743137686430396134"
            "386237613831363331616145425a5f5a4e36315f244c542473657264655f6a736f"
            "6e2e2e6572726f722e2e4572726f72247532302461732475323024636f72652e2e"
            "666d742e2e44656275672447542433666d74313768343032353764366634326532"
            "396237344543595f5a4e36305f244c5424616c6c6f632e2e737472696e672e2e53"
            "7472696e67247532302461732475323024636f72652e2e666d742e2e446973706c"
            "61792447542433666d74313768636534323236616131663732366331634544615f"
            "5a4e35385f244c5424616c6c6f632e2e737472696e672e2e537472696e67247532"
            "302461732475323024636f72652e2e666d742e2e57726974652447542439777269"
            "74655f73747231376835393964396535373839343664643938452e31393245575f"
            "5a4e35385f244c5424616c6c6f632e2e737472696e672e2e537472696e67247532"
            "302461732475323024636f72652e2e666d742e2e44656275672447542433666d74"
            "313768623637326562313939633335643138364546555f5a4e35335f244c542463"
            "6f72652e2e666d742e2e4572726f72247532302461732475323024636f72652e2e"
            "666d742e2e44656275672447542433666d74313768663761653238353562323439"
            "64626335452e3734474c5f5a4e34636f726533707472343264726f705f696e5f70"
            "6c616365244c5424616c6c6f632e2e737472696e672e2e537472696e6724475424"
            "313768376236353738393966393837353963624548475f5a4e313073657264655f"
            "6a736f6e347265616439536c696365526561643139736b69705f746f5f65736361"
            "70655f736c6f77313768343738366336653232346661323366324549465f5a4e35"
            "616c6c6f63337665633136566563244c5424542443244124475424313765787465"
            "6e645f66726f6d5f736c6963653137686462613134663734663865323236646345"
            "4a2f5f5a4e313073657264655f6a736f6e34726561643661735f73747231376866"
            "636436626234313731373865366635454b435f5a4e35616c6c6f63377261775f76"
            "65633139526177566563244c54245424432441244754243867726f775f6f6e6531"
            "376836366638363461663034626564326232454c3e5f5a4e313073657264655f6a"
            "736f6e3472656164323070617273655f756e69636f64655f657363617065313768"
            "39363430666363616264303034613064454d725f5a4e37305f244c542473657264"
            "655f6a736f6e2e2e726561642e2e536c6963655265616424753230246173247532"
            "302473657264655f6a736f6e2e2e726561642e2e52656164244754243137646563"
            "6f64655f6865785f65736361706531376834376265353936383535663830346461"
            "454e355f5a4e313073657264655f6a736f6e347265616431317065656b5f6f725f"
            "656f6631376837336362313436306531616339386135454f5a5f5a4e35616c6c6f"
            "63377261775f7665633230526177566563496e6e6572244c542441244754243772"
            "6573657276653231646f5f726573657276655f616e645f68616e646c6531376837"
            "66656665376563326164336435616245502e5f5a4e313073657264655f6a736f6e"
            "3472656164356572726f7231376865663535323764333333633963323666455130"
            "5f5a4e34636f726533666d743557726974653977726974655f666d743137686133"
            "31656164363637646336373865304552635f5a4e35385f244c5424616c6c6f632e"
            "2e737472696e672e2e537472696e67247532302461732475323024636f72652e2e"
            "666d742e2e577269746524475424313077726974655f6368617231376832313433"
            "393163623865623135326336452e31393353325f5a4e35616c6c6f63377261775f"
            "766563313166696e6973685f67726f773137683533383539626133383962373164"
            "333545544b5f5a4e35616c6c6f63377261775f7665633230526177566563496e6e"
            "6572244c54244124475424313467726f775f616d6f7274697a6564313768393863"
            "3336346663343566336431323445550a727573745f70616e696356375f5a4e3463"
            "6f72653570616e6963313250616e69635061796c6f61643661735f737472313768"
            "363134396631343264396132653032654557505f5a4e38646c6d616c6c6f633864"
            "6c6d616c6c6f633137446c6d616c6c6f63244c542441244754243138696e736572"
            "745f6c617267655f6368756e6b3137686566653835316132373538326461376245"
            "58455f5a4e3373746433737973396261636b747261636532365f5f727573745f65"
            "6e645f73686f72745f6261636b7472616365313768346463336465343764323230"
            "323162394559585f5a4e337374643970616e69636b696e673139626567696e5f70"
            "616e69635f68616e646c657232385f24753762242475376224636c6f7375726524"
            "75376424247537642431376865313761333937376638396331313738455a3b5f5a"
            "4e337374643970616e69636b696e673230727573745f70616e69635f776974685f"
            "686f6f6b31376837373665373963396636353931626535455b83015f5a4e39395f"
            "244c54247374642e2e70616e69636b696e672e2e626567696e5f70616e69635f68"
            "616e646c65722e2e5374617469635374725061796c6f6164247532302461732475"
            "323024636f72652e2e70616e69632e2e50616e69635061796c6f61642447542436"
            "61735f73747231376865623366373232643232346534326638455c066d656d636d"
            "705d365f5a4e3137636f6d70696c65725f6275696c74696e73336d656d376d656d"
            "6d6f766531376863383366393136386635323861656536455e076d656d6d6f7665"
            "5f066d656d637079071201000f5f5f737461636b5f706f696e746572090a010007"
            "2e726f6461746100550970726f64756365727302086c616e677561676501045275"
            "7374000c70726f6365737365642d62790105727573746325312e38332e302d6e69"
            "6768746c79202863326637346333663920323032342d30392d30392900490f7461"
            "726765745f6665617475726573042b0a6d756c746976616c75652b0f6d75746162"
            "6c652d676c6f62616c732b0f7265666572656e63652d74797065732b087369676e"
            "2d657874";

        {
            // create escrow
            Env env(*this);
            env.fund(XRP(5000), alice, bob, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 0);
            auto escrowCreate = escrow(alice, carol, XRP(1000));
            std::uint32_t const finishTime =
                (env.now() + 1s).time_since_epoch().count();
            escrowCreate[sfFinishAfter.jsonName] = finishTime;
            escrowCreate[sfFinishFunction.jsonName] = wasmHex;
            env(escrowCreate);
            env.close();

            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 1);
            env.require(balance(alice, XRP(4000) - drops(10)));
            env.require(balance(carol, XRP(5000)));

            env(finish(carol, alice, seq), ter(tecNO_PERMISSION));
            env(finish(alice, alice, seq), ter(tesSUCCESS));
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
        testFinishFunction();
    }
};

BEAST_DEFINE_TESTSUITE(Escrow, app, ripple);

}  // namespace test
}  // namespace ripple
