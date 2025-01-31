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
        Account const carol{"carol"};

        // P4
        static auto wasmHex =
            "0061736d0100000001791160037f7f7f017f60027f7f017f60017f0060027f7f00"
            "60037e7f7f017f60037f7f7f0060067f7f7f7f7f7f017f60017f017f60047f7f7f"
            "7f017f60057f7f7f7f7f0060000060057f7f7f7f7f017f60077f7f7f7f7f7f7f01"
            "7f60047f7f7f7f0060067f7f7f7f7f7f0060047f7f7f7e0060057f7f7f7e7f0003"
            "7c7b01010301040506010202020202010102050001000103010007070303080500"
            "090201020a03020500030305020b05050101010001010a0505050c090901010105"
            "0903010101030d0d0502050d01030301010d0e020a0a02030101020a0d0d000101"
            "0a030302020d03030205050003030f0f1010101009030000000004050170011e1e"
            "05030100110619037f01418080c0000b7f004184e0c0000b7f004190e0c0000b07"
            "5106066d656d6f7279020008616c6c6f6361746500180a6465616c6c6f63617465"
            "001b11636f6d706172655f6163636f756e744944001c0a5f5f646174615f656e64"
            "03010b5f5f686561705f6261736503020923010041010b1d210307122f30433134"
            "3c3d443e59626708110e500d160942585a5e5f600a9188037b6601017f23808080"
            "800041106b220224808080800002400240200028020c450d00200021010c010b20"
            "0241086a200041086a280200360200200220002902003703002001200210818080"
            "80002101200041141082808080000b200241106a24808080800020010b8c010103"
            "7f23808080800041106b2202248080808000200241086a200028020c2000280210"
            "200028021410c78080800041002d00e0dbc080001a200228020c21032002280208"
            "21040240411410998080800022000d00000b2000200436020c2000200129020037"
            "020020002003360210200041086a200141086a280200360200200241106a248080"
            "80800020000b7001027f024002402000417c6a2802002202417871220341044108"
            "200241037122021b20016a490d0002402002450d002003200141276a4b0d020b20"
            "0010a5808080000f0b419db8c08000412e41ccb8c0800010a680808000000b41dc"
            "b8c08000412e418cb9c0800010a680808000000be90201057f2380808080004180"
            "016b22022480808080000240024002400240200128021c22034110710d00200341"
            "20710d0120003100004101200110848080800021000c030b20002d0000210041ff"
            "00210303402002200322046a22052000410f712203413072200341d7006a200341"
            "0a491b3a00002004417f6a2103200041ff017122064104762100200641104f0d00"
            "0c020b0b20002d0000210041ff00210303402002200322046a22052000410f7122"
            "03413072200341376a2003410a491b3a00002004417f6a2103200041ff01712206"
            "4104762100200641104f0d000b02402004418101490d002004418001419887c080"
            "00108580808000000b2001410141a887c0800041022005418101200441016a6b10"
            "868080800021000c010b02402004418101490d002004418001419887c080001085"
            "80808000000b2001410141a887c0800041022005418101200441016a6b10868080"
            "800021000b20024180016a24808080800020000bec0203027f017e037f23808080"
            "800041306b2203248080808000412721040240024020004290ce005a0d00200021"
            "050c010b412721040340200341096a20046a2206417c6a20004290ce0080220542"
            "f0b1037e20007ca7220741ffff037141e4006e220841017441aa87c080006a2f00"
            "003b00002006417e6a2008419c7f6c20076a41ffff037141017441aa87c080006a"
            "2f00003b00002004417c6a2104200042ffc1d72f5621062005210020060d000b0b"
            "02400240200542e300560d002005a721060c010b200341096a2004417e6a22046a"
            "2005a7220741ffff037141e4006e2206419c7f6c20076a41ffff037141017441aa"
            "87c080006a2f00003b00000b024002402006410a490d00200341096a2004417e6a"
            "22046a200641017441aa87c080006a2f00003b00000c010b200341096a2004417f"
            "6a22046a20064130723a00000b2002200141014100200341096a20046a41272004"
            "6b1086808080002104200341306a24808080800020040b7902017f017e23808080"
            "800041306b22032480808080002003200036020020032001360204200341023602"
            "0c2003419c8ac08000360208200342023702142003418180808000ad4220862204"
            "200341046aad84370328200320042003ad843703202003200341206a3602102003"
            "41086a200210a480808000000bcb0501077f0240024020010d00200541016a2106"
            "200028021c2107412d21080c010b412b418080c400200028021c22074101712201"
            "1b2108200120056a21060b0240024020074104710d00410021020c010b02400240"
            "20030d00410021090c010b02402003410371220a0d000c010b4100210920022101"
            "0340200920012c000041bf7f4a6a2109200141016a2101200a417f6a220a0d000b"
            "0b200920066a21060b024020002802000d00024020002802142201200028021822"
            "0920082002200310ac80808000450d0041010f0b200120042005200928020c1180"
            "8080800080808080000f0b02400240024002402000280204220120064b0d002000"
            "28021422012000280218220920082002200310ac80808000450d0141010f0b2007"
            "410871450d01200028021021072000413036021020002d0020210b4101210c2000"
            "41013a0020200028021422092000280218220a20082002200310ac808080000d02"
            "200120066b41016a2101024003402001417f6a2201450d0120094130200a280210"
            "1181808080008080808000450d000b41010f0b0240200920042005200a28020c11"
            "80808080008080808000450d0041010f0b2000200b3a0020200020073602104100"
            "0f0b200120042005200928020c1180808080008080808000210c0c010b20012006"
            "6b210702400240024020002d002022010e0402000100020b20072101410021070c"
            "010b20074101762101200741016a41017621070b200141016a2101200028021021"
            "06200028021821092000280214210a024003402001417f6a2201450d01200a2006"
            "20092802101181808080008080808000450d000b41010f0b4101210c200a200920"
            "082002200310ac808080000d00200a20042005200928020c118080808000808080"
            "80000d00410021010340024020072001470d0020072007490f0b200141016a2101"
            "200a200620092802101181808080008080808000450d000b2001417f6a2007490f"
            "0b200c0be70201057f2380808080004180016b2202248080808000024002400240"
            "0240200128021c22034110710d0020034120710d01200035020041012001108480"
            "80800021000c030b2000280200210041ff00210303402002200322046a22052000"
            "410f712203413072200341d7006a2003410a491b3a00002004417f6a2103200041"
            "10492106200041047621002006450d000c020b0b2000280200210041ff00210303"
            "402002200322046a22052000410f712203413072200341376a2003410a491b3a00"
            "002004417f6a210320004110492106200041047621002006450d000b0240200441"
            "8101490d002004418001419887c08000108580808000000b2001410141a887c080"
            "0041022005418101200441016a6b10868080800021000c010b0240200441810149"
            "0d002004418001419887c08000108580808000000b2001410141a887c080004102"
            "2005418101200441016a6b10868080800021000b20024180016a24808080800020"
            "000b1e01017f024020002802002201450d00200028020420011082808080000b0b"
            "970101047f024002400240200028020022002802000e020001020b200028020822"
            "01450d01200028020420011082808080000c010b20002d00044103470d00200028"
            "0208220128020021020240200128020422032802002204450d0020022004118280"
            "80800080808080000b024020032802042203450d00200220031082808080000b20"
            "01410c1082808080000b200041141082808080000b6801017f0240024002400240"
            "20002d00000e050303030102000b200041046a108b808080000c020b2000280204"
            "2201450d01200028020820011082808080000f0b200041046a108c808080002000"
            "2802042201450d002000280208200141186c1082808080000f0b0be30501067f23"
            "808080800041306b22012480808080004100210241002103024020002802002204"
            "450d00200120043602182001410036021420012004360208200141003602042001"
            "2000280204220336021c2001200336020c20002802082103410121020b20012003"
            "360220200120023602102001200236020002400240024003400240024002400240"
            "024020030d002001280200450d0820012802082104200128020422030d01410021"
            "00200128020c2203450d06034020042802980321042003417f6a22030d000c070b"
            "0b20012003417f6a360220024020024101712203450d0020012802040d00200128"
            "02082103200128020c2200450d03034020032802980321032000417f6a22000d00"
            "0c040b0b2003450d01200128020421030c030b200421000c050b41a8acc0800010"
            "a080808000000b200142003702082001200336020441012102200141013602000b"
            "200128020821000240200128020c220520032f019203490d000240034020014124"
            "6a2003200010bf8080800020012802242203450d0120012802282100200128022c"
            "220520032f019203490d020c000b0b4188a5c0800010a080808000000b20054101"
            "6a21040240024020000d002001200436020c20014100360208200120033602040c"
            "010b200320044102746a4198036a21040340200428020022064198036a21042000"
            "417f6a22000d000b20014200370208200120063602042003450d040b0240200320"
            "05410c6c6a418c026a22002802002204450d00200028020420041082808080000b"
            "02400240024002402003200541186c6a22032d00000e050303030102000b200341"
            "046a108b808080000c020b20032802042200450d01200328020820001082808080"
            "000c010b200341046a108c8080800020032802042200450d002003280208200041"
            "186c1082808080000b200128022021030c000b0b200421030b0340200141246a20"
            "03200010bf8080800020012802242203450d01200128022821000c000b0b200141"
            "306a2480808080000b950101027f024020002802082201450d0020002802044104"
            "6a2100034002400240024002402000417c6a2d00000e050303030102000b200010"
            "8b808080000c020b20002802002202450d01200041046a28020020021082808080"
            "000c010b2000108c8080800020002802002202450d00200041046a280200200241"
            "186c1082808080000b200041186a21002001417f6a22010d000b0b0b2200200128"
            "021441d4a6c080004105200128021828020c11808080800080808080000be30201"
            "027f23808080800041106b22022480808080000240024002400240200141800149"
            "0d002002410036020c2001418010490d0102402001418080044f0d002002200141"
            "3f71418001723a000e20022001410c7641e001723a000c20022001410676413f71"
            "418001723a000d410321010c030b20022001413f71418001723a000f2002200141"
            "127641f001723a000c20022001410676413f71418001723a000e20022001410c76"
            "413f71418001723a000d410421010c020b0240200028020822032000280200470d"
            "002000108f808080000b2000200341016a360208200028020420036a20013a0000"
            "0c020b20022001413f71418001723a000d2002200141067641c001723a000c4102"
            "21010b02402000280200200028020822036b20014f0d0020002003200110908080"
            "8000200028020821030b200028020420036a2002410c6a200110fa808080001a20"
            "00200320016a3602080b200241106a24808080800041000b5901017f2380808080"
            "0041106b2201248080808000200141086a2000200028020041014101410110d280"
            "808000024020012802082200418180808078460d002000200128020c109a808080"
            "00000b200141106a2480808080000b5601017f23808080800041106b2203248080"
            "808000200341086a2000200120024101410110d280808000024020032802082202"
            "418180808078460d002002200328020c109a80808000000b200341106a24808080"
            "80000b4b01017f02402000280200200028020822036b20024f0d00200020032002"
            "109080808000200028020821030b200028020420036a2001200210fa808080001a"
            "2000200320026a36020841000b1400200120002802042000280208109380808000"
            "0bc20b010b7f200028020821030240024002400240200028020022040d00200341"
            "0171450d010b02402003410171450d00200120026a210502400240200028020c22"
            "060d0041002107200121080c010b41002107410021092001210803402008220320"
            "05460d020240024020032c00002208417f4c0d00200341016a21080c010b024020"
            "0841604f0d00200341026a21080c010b0240200841704f0d00200341036a21080c"
            "010b200341046a21080b200820036b20076a21072006200941016a2209470d000b"
            "0b20082005460d00024020082c00002203417f4a0d0020034160491a0b02400240"
            "2007450d000240200720024f0d00200120076a2c000041bf7f4a0d01410021030c"
            "020b20072002460d00410021030c010b200121030b2007200220031b2102200320"
            "0120031b21010b024020040d00200028021420012002200028021828020c118080"
            "80800080808080000f0b2000280204210a024020024110490d0020022001200141"
            "036a417c7122076b22096a220b4103712104410021064100210302402001200746"
            "0d004100210302402009417c4b0d00410021034100210503402003200120056a22"
            "082c000041bf7f4a6a200841016a2c000041bf7f4a6a200841026a2c000041bf7f"
            "4a6a200841036a2c000041bf7f4a6a2103200541046a22050d000b0b2001210803"
            "40200320082c000041bf7f4a6a2103200841016a2108200941016a22090d000b0b"
            "02402004450d002007200b417c716a22082c000041bf7f4a210620044101460d00"
            "200620082c000141bf7f4a6a210620044102460d00200620082c000241bf7f4a6a"
            "21060b200b4102762105200620036a21060340200721042005450d04200541c001"
            "200541c001491b220b410371210c200b410274210d41002108024020054104490d"
            "002004200d41f007716a210941002108200421030340200328020c2207417f7341"
            "077620074106767241818284087120032802082207417f73410776200741067672"
            "41818284087120032802042207417f734107762007410676724181828408712003"
            "2802002207417f7341077620074106767241818284087120086a6a6a6a21082003"
            "41106a22032009470d000b0b2005200b6b21052004200d6a2107200841087641ff"
            "81fc0771200841ff81fc07716a418180046c41107620066a2106200c450d000b20"
            "04200b41fc01714102746a22082802002203417f73410776200341067672418182"
            "8408712103200c4101460d0220082802042207417f734107762007410676724181"
            "8284087120036a2103200c4102460d0220082802082208417f7341077620084106"
            "767241818284087120036a21030c020b024020020d00410021060c030b20024103"
            "71210802400240200241044f0d0041002106410021090c010b4100210620012103"
            "2002410c71220921070340200620032c000041bf7f4a6a200341016a2c000041bf"
            "7f4a6a200341026a2c000041bf7f4a6a200341036a2c000041bf7f4a6a21062003"
            "41046a21032007417c6a22070d000b0b2008450d02200120096a21030340200620"
            "032c000041bf7f4a6a2106200341016a21032008417f6a22080d000c030b0b2000"
            "28021420012002200028021828020c11808080800080808080000f0b2003410876"
            "41ff811c71200341ff81fc07716a418180046c41107620066a21060b0240024020"
            "0a20064d0d00200a20066b21054100210302400240024020002d00200e04020001"
            "02020b20052103410021050c010b20054101762103200541016a41017621050b20"
            "0341016a210320002802102109200028021821082000280214210703402003417f"
            "6a2203450d022007200920082802101181808080008080808000450d000b41010f"
            "0b200028021420012002200028021828020c11808080800080808080000f0b0240"
            "200720012002200828020c1180808080008080808000450d0041010f0b41002103"
            "0340024020052003470d0020052005490f0b200341016a21032007200920082802"
            "101181808080008080808000450d000b2003417f6a2005490b890503037f017e03"
            "7f23808080800041f0006b220224808080800041002103024020002d0000220420"
            "012d0000470d00410121030240024002400240024020040e06050001020304050b"
            "20002d000120012d00014621030c040b4100210320002903082205200129030852"
            "0d030240024002402005a70e03000102000b200029031020012903105121030c05"
            "0b200029031020012903105121030c040b20002b031020012b03106121030c030b"
            "41002103200028020c2204200128020c470d0220002802082001280208200410f7"
            "808080004521030c020b41002103200028020c2206200128020c470d0120012802"
            "08210420002802082100200641016a210103402001417f6a22014521032001450d"
            "02200020041094808080002106200441186a2104200041186a210020060d000c02"
            "0b0b41002103200028020c2204200128020c470d002002410036026c2002420037"
            "026420024100360254200241003602442002410036023020024100360220200220"
            "01280208220636025c2002200128020422033602582002200636024c2002200336"
            "024820022000280208220636023820022000280204220136023420022006360228"
            "2002200136022420022004410020031b3602602002200341004722033602502002"
            "200336024020022004410020011b36023c20022001410047220336022c20022003"
            "36021c200241c0006a21070340200241106a2002411c6a10958080800041012103"
            "20022802102201450d0120022802142104200241086a2007109580808000200228"
            "02082200450d0141002103200128020822062000280208470d01200228020c2108"
            "20012802042000280204200610f7808080000d01200420081094808080000d000b"
            "0b200241f0006a24808080800020030bed0201057f024002400240200128022022"
            "020d00410021020c010b20012002417f6a36022002400240024020012802004101"
            "470d0020012802040d01200128020821030240200128020c2202450d0003402003"
            "2802980321032002417f6a22020d000b0b20014200370208200120033602042001"
            "41013602000c020b41c8acc0800010a080808000000b200128020421030b200128"
            "0208210202400240200128020c220420032f0192034f0d00200321050c010b0340"
            "2003280288022205450d03200241016a210220032f019003210420052103200420"
            "052f0192034f0d000b0b200441016a21030240024020020d00200521060c010b20"
            "0520034102746a4198036a21030340200328020022064198036a21032002417f6a"
            "22020d000b410021030b2001200336020c20014100360208200120063602042005"
            "200441186c6a210320052004410c6c6a418c026a21020b20002003360204200020"
            "023602000f0b41b8acc0800010a080808000000bae0301057f2380808080004110"
            "6b220224808080800041012103024020012802142204419f81c08000410d200128"
            "0218220528020c220611808080800080808080000d00024020012d001c4104710d"
            "00200441f886c080004103200611808080800080808080000d01200441ac81c080"
            "004104200611808080800080808080000d012004418bc2c0800041022006118080"
            "80800080808080000d01200420002d0000410274220141dc83c080006a28020020"
            "0141c883c080006a280200200611808080800080808080000d012004418187c080"
            "0041022006118080808000808080800021030c010b200441fb86c0800041032006"
            "11808080800080808080000d002002200536020420022004360200410121032002"
            "41013a000f20022002410f6a360208200241ac81c0800041041097808080000d00"
            "2002418bc2c0800041021097808080000d00200220002d0000410274220141dc83"
            "c080006a280200200141c883c080006a2802001097808080000d00410121032002"
            "41fe86c0800041021097808080000d002004418087c08000410120061180808080"
            "00808080800021030b200241106a24808080800020030bdf04010c7f2001417f6a"
            "210320002802042104200028020021052000280208210641002107410021084100"
            "21094100210a02400340200a4101710d0102400240200920024b0d000340200120"
            "096a210a0240024002400240200220096b220b41074b0d0020022009470d012002"
            "21090c050b02400240200a41036a417c71220c200a6b220d450d00410021000340"
            "200a20006a2d0000410a460d05200d200041016a2200470d000b200d200b41786a"
            "220e4d0d010c030b200b41786a210e0b03404180828408200c2802002200418a94"
            "a8d000736b2000724180828408200c41046a2802002200418a94a8d000736b2000"
            "727141808182847871418081828478470d02200c41086a210c200d41086a220d20"
            "0e4d0d000c020b0b410021000340200a20006a2d0000410a460d02200b20004101"
            "6a2200470d000b200221090c030b0240200d200b470d00200221090c030b200a20"
            "0d6a210c2002200d6b20096b210b4100210002400340200c20006a2d0000410a46"
            "0d01200b200041016a2200470d000b200221090c030b2000200d6a21000b200020"
            "096a220c41016a21090240200c20024f0d00200a20006a2d0000410a470d004100"
            "210a2009210d200921000c030b200920024d0d000b0b20082002460d024101210a"
            "2008210d200221000b0240024020062d0000450d00200541f486c0800041042004"
            "28020c11808080800080808080000d010b200020086b210b4100210c0240200020"
            "08460d00200320006a2d0000410a46210c0b200120086a21002006200c3a000020"
            "0d210820052000200b200428020c1180808080008080808000450d010b0b410121"
            "070b20070b4901017f410021010240024020004100480d00024020000d00410121"
            "010c020b41002d00e0dbc080001a200010998080800022010d01410121010b2001"
            "2000109a80808000000b20010bcb2502087f017e02400240024002400240024002"
            "400240200041f501490d0041002101200041cdff7b4f0d052000410b6a22014178"
            "71210241002802c4dfc080002203450d04411f21040240200041f4ffff074b0d00"
            "2002410620014108766722006b7641017120004101746b413e6a21040b41002002"
            "6b21010240200441027441a8dcc080006a28020022050d0041002100410021060c"
            "020b4100210020024100411920044101766b2004411f461b742107410021060340"
            "02402005220528020441787122082002490d00200820026b220820014f0d002008"
            "21012005210620080d004100210120052106200521000c040b2005280214220820"
            "00200820052007411d764104716a41106a2802002205471b200020081b21002007"
            "41017421072005450d020c000b0b024041002802c0dfc08000220541102000410b"
            "6a41f803712000410b491b22024103762201762200410371450d00024002402000"
            "417f7341017120016a2207410374220041b8ddc080006a2201200041c0ddc08000"
            "6a28020022022802082206460d002006200136020c200120063602080c010b4100"
            "2005417e200777713602c0dfc080000b20022000410372360204200220006a2200"
            "2000280204410172360204200241086a0f0b200241002802c8dfc080004d0d0302"
            "400240024020000d0041002802c4dfc080002200450d0620006841027441a8dcc0"
            "80006a280200220628020441787120026b21012006210503400240200628021022"
            "000d00200628021422000d0020052802182104024002400240200528020c220020"
            "05470d00200541144110200528021422001b6a28020022060d01410021000c020b"
            "20052802082206200036020c200020063602080c010b200541146a200541106a20"
            "001b21070340200721082006220041146a200041106a200028021422061b210720"
            "004114411020061b6a28020022060d000b200841003602000b2004450d04024020"
            "0528021c41027441a8dcc080006a22062802002005460d00200441104114200428"
            "02102005461b6a20003602002000450d050c040b2006200036020020000d034100"
            "41002802c4dfc08000417e200528021c77713602c4dfc080000c040b2000280204"
            "41787120026b22062001200620014922061b21012000200520061b210520002106"
            "0c000b0b02400240200020017441022001742200410020006b7271682208410374"
            "220141b8ddc080006a2206200141c0ddc080006a28020022002802082207460d00"
            "2007200636020c200620073602080c010b41002005417e200877713602c0dfc080"
            "000b20002002410372360204200020026a2207200120026b220641017236020420"
            "0020016a2006360200024041002802c8dfc080002205450d00200541787141b8dd"
            "c080006a210141002802d0dfc0800021020240024041002802c0dfc08000220841"
            "012005410376742205710d00410020082005723602c0dfc08000200121050c010b"
            "200128020821050b200120023602082005200236020c2002200136020c20022005"
            "3602080b410020073602d0dfc08000410020063602c8dfc08000200041086a0f0b"
            "20002004360218024020052802102206450d002000200636021020062000360218"
            "0b20052802142206450d0020002006360214200620003602180b02400240024020"
            "014110490d0020052002410372360204200520026a220220014101723602042002"
            "20016a200136020041002802c8dfc080002207450d01200741787141b8ddc08000"
            "6a210641002802d0dfc0800021000240024041002802c0dfc08000220841012007"
            "410376742207710d00410020082007723602c0dfc08000200621070c010b200628"
            "020821070b200620003602082007200036020c2000200636020c20002007360208"
            "0c010b2005200120026a2200410372360204200520006a22002000280204410172"
            "3602040c010b410020023602d0dfc08000410020013602c8dfc080000b20054108"
            "6a0f0b024020002006720d004100210641022004742200410020006b7220037122"
            "00450d0320006841027441a8dcc080006a28020021000b2000450d010b03402000"
            "20062000280204417871220520026b220820014922041b21032005200249210720"
            "08200120041b21080240200028021022050d00200028021421050b200620032007"
            "1b21062001200820071b21012005210020050d000b0b2006450d00024041002802"
            "c8dfc0800022002002490d002001200020026b4f0d010b20062802182104024002"
            "400240200628020c22002006470d00200641144110200628021422001b6a280200"
            "22050d01410021000c020b20062802082205200036020c200020053602080c010b"
            "200641146a200641106a20001b21070340200721082005220041146a200041106a"
            "200028021422051b210720004114411020051b6a28020022050d000b2008410036"
            "02000b2004450d030240200628021c41027441a8dcc080006a2205280200200646"
            "0d0020044110411420042802102006461b6a20003602002000450d040c030b2005"
            "200036020020000d02410041002802c4dfc08000417e200628021c77713602c4df"
            "c080000c030b02400240024002400240024041002802c8dfc08000220020024f0d"
            "00024041002802ccdfc08000220020024b0d0041002101200241af80046a220641"
            "107640002200417f4622070d0720004110742205450d07410041002802d8dfc080"
            "00410020064180807c7120071b22086a22003602d8dfc08000410041002802dcdf"
            "c0800022012000200120004b1b3602dcdfc0800002400240024041002802d4dfc0"
            "80002201450d0041a8ddc080002100034020002802002206200028020422076a20"
            "05460d02200028020822000d000c030b0b0240024041002802e4dfc08000220045"
            "0d00200020054d0d010b410020053602e4dfc080000b410041ff1f3602e8dfc080"
            "00410020083602acddc08000410020053602a8ddc08000410041b8ddc080003602"
            "c4ddc08000410041c0ddc080003602ccddc08000410041b8ddc080003602c0ddc0"
            "8000410041c8ddc080003602d4ddc08000410041c0ddc080003602c8ddc0800041"
            "0041d0ddc080003602dcddc08000410041c8ddc080003602d0ddc08000410041d8"
            "ddc080003602e4ddc08000410041d0ddc080003602d8ddc08000410041e0ddc080"
            "003602ecddc08000410041d8ddc080003602e0ddc08000410041e8ddc080003602"
            "f4ddc08000410041e0ddc080003602e8ddc08000410041f0ddc080003602fcddc0"
            "8000410041e8ddc080003602f0ddc08000410041003602b4ddc08000410041f8dd"
            "c08000360284dec08000410041f0ddc080003602f8ddc08000410041f8ddc08000"
            "360280dec0800041004180dec0800036028cdec0800041004180dec08000360288"
            "dec0800041004188dec08000360294dec0800041004188dec08000360290dec080"
            "0041004190dec0800036029cdec0800041004190dec08000360298dec080004100"
            "4198dec080003602a4dec0800041004198dec080003602a0dec08000410041a0de"
            "c080003602acdec08000410041a0dec080003602a8dec08000410041a8dec08000"
            "3602b4dec08000410041a8dec080003602b0dec08000410041b0dec080003602bc"
            "dec08000410041b0dec080003602b8dec08000410041b8dec080003602c4dec080"
            "00410041c0dec080003602ccdec08000410041b8dec080003602c0dec080004100"
            "41c8dec080003602d4dec08000410041c0dec080003602c8dec08000410041d0de"
            "c080003602dcdec08000410041c8dec080003602d0dec08000410041d8dec08000"
            "3602e4dec08000410041d0dec080003602d8dec08000410041e0dec080003602ec"
            "dec08000410041d8dec080003602e0dec08000410041e8dec080003602f4dec080"
            "00410041e0dec080003602e8dec08000410041f0dec080003602fcdec080004100"
            "41e8dec080003602f0dec08000410041f8dec08000360284dfc08000410041f0de"
            "c080003602f8dec0800041004180dfc0800036028cdfc08000410041f8dec08000"
            "360280dfc0800041004188dfc08000360294dfc0800041004180dfc08000360288"
            "dfc0800041004190dfc0800036029cdfc0800041004188dfc08000360290dfc080"
            "0041004198dfc080003602a4dfc0800041004190dfc08000360298dfc080004100"
            "41a0dfc080003602acdfc0800041004198dfc080003602a0dfc08000410041a8df"
            "c080003602b4dfc08000410041a0dfc080003602a8dfc08000410041b0dfc08000"
            "3602bcdfc08000410041a8dfc080003602b0dfc08000410020053602d4dfc08000"
            "410041b0dfc080003602b8dfc080004100200841586a22003602ccdfc080002005"
            "2000410172360204200520006a4128360204410041808080013602e0dfc080000c"
            "080b200120054f0d00200620014b0d00200028020c450d030b410041002802e4df"
            "c080002200200520002005491b3602e4dfc08000200520086a210641a8ddc08000"
            "21000240024002400340200028020022072006460d01200028020822000d000c02"
            "0b0b200028020c450d010b41a8ddc0800021000240034002402000280200220620"
            "014b0d002001200620002802046a2206490d020b200028020821000c000b0b4100"
            "20053602d4dfc080004100200841586a22003602ccdfc080002005200041017236"
            "0204200520006a4128360204410041808080013602e0dfc080002001200641606a"
            "41787141786a22002000200141106a491b2207411b36020441002902a8ddc08000"
            "2109200741106a41002902b0ddc0800037020020072009370208410020083602ac"
            "ddc08000410020053602a8ddc080004100200741086a3602b0ddc0800041004100"
            "3602b4ddc080002007411c6a2100034020004107360200200041046a2200200649"
            "0d000b20072001460d0720072007280204417e713602042001200720016b220041"
            "01723602042007200036020002402000418002490d002001200010e3808080000c"
            "080b200041f8017141b8ddc080006a21060240024041002802c0dfc08000220541"
            "012000410376742200710d00410020052000723602c0dfc08000200621000c010b"
            "200628020821000b200620013602082000200136020c2001200636020c20012000"
            "3602080c070b200020053602002000200028020420086a36020420052002410372"
            "3602042007410f6a41787141786a2201200520026a22006b2102200141002802d4"
            "dfc08000460d03200141002802d0dfc08000460d04024020012802042206410371"
            "4101470d0020012006417871220610a880808000200620026a2102200120066a22"
            "0128020421060b20012006417e7136020420002002410172360204200020026a20"
            "0236020002402002418002490d002000200210e3808080000c060b200241f80171"
            "41b8ddc080006a21010240024041002802c0dfc080002206410120024103767422"
            "02710d00410020062002723602c0dfc08000200121020c010b200128020821020b"
            "200120003602082002200036020c2000200136020c200020023602080c050b4100"
            "200020026b22013602ccdfc08000410041002802d4dfc08000220020026a220636"
            "02d4dfc080002006200141017236020420002002410372360204200041086a2101"
            "0c060b41002802d0dfc08000210102400240200020026b2206410f4b0d00410041"
            "003602d0dfc08000410041003602c8dfc080002001200041037236020420012000"
            "6a220020002802044101723602040c010b410020063602c8dfc080004100200120"
            "026a22053602d0dfc0800020052006410172360204200120006a20063602002001"
            "20024103723602040b200141086a0f0b2000200720086a360204410041002802d4"
            "dfc080002200410f6a417871220141786a22063602d4dfc080004100200020016b"
            "41002802ccdfc0800020086a22016a41086a22053602ccdfc08000200620054101"
            "72360204200020016a4128360204410041808080013602e0dfc080000c030b4100"
            "20003602d4dfc08000410041002802ccdfc0800020026a22023602ccdfc0800020"
            "0020024101723602040c010b410020003602d0dfc08000410041002802c8dfc080"
            "0020026a22023602c8dfc0800020002002410172360204200020026a2002360200"
            "0b200541086a0f0b4100210141002802ccdfc08000220020024d0d004100200020"
            "026b22013602ccdfc08000410041002802d4dfc08000220020026a22063602d4df"
            "c080002006200141017236020420002002410372360204200041086a0f0b20010f"
            "0b20002004360218024020062802102205450d0020002005360210200520003602"
            "180b20062802142205450d0020002005360214200520003602180b024002402001"
            "4110490d0020062002410372360204200620026a22002001410172360204200020"
            "016a200136020002402001418002490d002000200110e3808080000c020b200141"
            "f8017141b8ddc080006a21020240024041002802c0dfc080002205410120014103"
            "76742201710d00410020052001723602c0dfc08000200221010c010b2002280208"
            "21010b200220003602082001200036020c2000200236020c200020013602080c01"
            "0b2006200120026a2200410372360204200620006a220020002802044101723602"
            "040b200641086a0b1000024020000d0010a3808080000b000b140002402001450d"
            "00200020011082808080000b0ba20b03087f017e017f2380808080004190016b22"
            "04248080808000200441e8006a20002001109d8080800002400240024002400240"
            "024002400240024002400240024020042d00684106460d00200441106a200441e8"
            "006a41106a2205290300370300200441086a200441e8006a41086a220629030037"
            "030020042004290368370300200441e8006a20022003109d8080800020042d0068"
            "4106460d01200441186a41106a2005290300370300200441186a41086a20062903"
            "003703002004200429036837031841dc81c0800041072004109e80808000220745"
            "0d0241dc81c080004107200441186a109e808080002208450d03418482c0800041"
            "04200441186a109e808080002206450d0420062d00004103462205450d05410021"
            "092006280208410020051b210502400240200628020c220a0e020c00010b410121"
            "0920052d000041556a0e030b080b080b20052d0000412b470d06200541016a2105"
            "200a410a492106200a417f6a2209210a20060d070c080b2004200428026c36024c"
            "41b880c08000412b200441cc006a41e480c0800041bc81c08000109f8080800000"
            "0b2004200428026c36024c41b880c08000412b200441cc006a41e480c0800041cc"
            "81c08000109f80808000000b41e481c0800010a080808000000b41f481c0800010"
            "a080808000000b418882c0800010a080808000000b419882c0800010a080808000"
            "000b200a2109200a41094f0d010b41002106034020052d000041506a220a41094b"
            "0d02200541016a2105200a2006410a6c6a21062009417f6a22090d000c040b0b41"
            "0021060340200a450d0320052d000041506a220b41094b0d01410221092006ad42"
            "0a7e220c422088a74100470d02200541016a2105200a417f6a210a200b200ca722"
            "0d6a2206200d4f0d000c020b0b410121090b200420093a006841b880c08000412b"
            "200441e8006a41a880c0800041a882c08000109f80808000000b20042006360230"
            "02400240200720081094808080000d00200441003a00370c010b20042006417f6a"
            "220536023002402005450d00200441003a00370c010b200441013a00370b200441"
            "0336026c200441d482c08000360268200442023702742004418180808000360258"
            "20044182808080003602502004200441cc006a3602702004200441306a36025420"
            "04200441376a36024c200441e8006a10a28080800041002d00e0dbc080001a0240"
            "02400240024041091099808080002205450d00200520042d00373a000020044100"
            "360254200442808080801037024c200441033a0088012004412036027820044100"
            "360284012004418080c08000360280012004410036027020044100360268200420"
            "0441cc006a36027c20043502304101200441e8006a1084808080000d0120044138"
            "6a41086a200441cc006a41086a2802003602002004200429024c37033841002d00"
            "e0dbc080001a200428023c210641041099808080002209450d0220092006360000"
            "2005200636000120042004280240220a36024841002d00e0dbc080001a20044104"
            "3602444104109980808000220b450d032005200a360005200b200a360000200441"
            "05360250200441a083c0800036024c200442043702582004418180808000360284"
            "01200441818080800036027c200441838080800036027420044184808080003602"
            "6c200420063602642004200441e8006a3602542004200441c8006a360280012004"
            "200441c4006a3602782004200441e4006a3602702004200441386a360268200441"
            "cc006a10a280808000200b41041082808080002009410410828080800020044118"
            "6a108a808080002004108a8080800002402003450d00200220031082808080000b"
            "02402001450d00200020011082808080000b20044190016a24808080800020050f"
            "0b41014109109a80808000000b41c0a5c080004137200441e4006a419880c08000"
            "41c4a6c08000109f80808000000b41014104109a80808000000b41014104109a80"
            "808000000bea0301057f23808080800041e0006b22032480808080002003410036"
            "0228200320023602242003200136022020034180013a002c2003410036021c2003"
            "428080808010370214200341c8006a200341146a10e88080800002400240024002"
            "4020032d00484106460d00200341306a41106a2204200341c8006a41106a290300"
            "370300200341306a41086a2205200341c8006a41086a2903003703002003200329"
            "03483703300240024020032802282202200328022422064f0d0020032802202107"
            "0340200720026a2d000041776a220141174b0d024101200174419380800471450d"
            "022006200241016a2202470d000b200320063602280b2000200329033037030020"
            "0041106a2004290300370300200041086a20052903003703002003280214220245"
            "0d04200328021820021082808080000c040b20032002360228200341086a200720"
            "062006200241016a220220062002491b10c78080800041002d00e0dbc080001a20"
            "0328020c21012003280208210641141099808080002202450d012002200636020c"
            "2002411636020020002002360204200041063a000020022001360210200341306a"
            "108a808080000c020b2000200328024c360204200041063a00000c010b000b2003"
            "2802142202450d00200328021820021082808080000b200341e0006a2480808080"
            "000be60101077f41002103024020022d00004105470d0020022802042204450d00"
            "2002280208210503402004418c026a210220042f0192032206410c6c2107417f21"
            "08024002400340024020070d00200621080c020b20022802082103200228020421"
            "09200841016a2108200741746a21072002410c6a2102417f200020092001200320"
            "012003491b10f7808080002209200120036b20091b220341004720034100481b22"
            "034101460d000b200341ff0171450d010b024020050d0041000f0b2005417f6a21"
            "05200420084102746a4198036a28020021040c010b0b2004200841186c6a21030b"
            "20030b8f0101017f23808080800041c0006b22052480808080002005200136020c"
            "2005200036020820052003360214200520023602102005410236021c200541e486"
            "c08000360218200542023702242005418580808000ad422086200541106aad8437"
            "03382005418680808000ad422086200541086aad843703302005200541306a3602"
            "20200541186a200410a480808000000b130041ec84c08000412b200010a6808080"
            "00000b11002000350200410120011084808080000bbe0604017f017e037f017e23"
            "808080800041c0006b22012480808080002001410636020c200141b0c2c0800036"
            "0208024041002d0090dcc080004103460d0010db808080000b0240024002400240"
            "024041002903f8dfc0800022024200520d0002404100280280e0c0800022030d00"
            "10d5808080004100280280e0c0800021030b20032003280200220441016a360200"
            "2004417f4c0d012003450d02200320032802002204417f6a360200200329030821"
            "0220044101470d00200310d6808080000b024002400240200241002903e8dbc080"
            "00510d0041002d00f4dbc08000210441012103410041013a00f4dbc08000200120"
            "043a00182004450d012001420037023420014281808080c00037022c200141d8c3"
            "c08000360228200141186a200141286a10d780808000000b024041002802f0dbc0"
            "80002203417f460d00200341016a21030c020b41b8c4c08000412641fcc4c08000"
            "10b780808000000b410020023703e8dbc080000b410020033602f0dbc080002001"
            "41e8dbc0800036021041042103200141043a00182001200141106a360220200141"
            "186a41dcb7c08000200010b280808000210020012d001821040240024020000d00"
            "420021024117200441ff0171764101710d01200128021c22032802002100024020"
            "0341046a28020022042802002205450d002000200511828080800080808080000b"
            "024020042802042204450d00200020041082808080000b2003410c108280808000"
            "410421030c010b200441ff01714104460d032001290318220642807e8321022006"
            "a721030b200128021022002000280208417f6a2204360208024020040d00200041"
            "003a000c200042003703000b200341ff01714104470d03200141c0006a24808080"
            "80000f0b000b419cb9c0800041de004190bac0800010b780808000000b20014100"
            "3602382001410136022c200141a0c3c0800036022820014204370230200141286a"
            "41a8c3c0800010a480808000000b200120022003ad42ff01838437031020014102"
            "36022c20014190c2c08000360228200142023702342001418780808000ad422086"
            "200141106aad843703202001418680808000ad422086200141086aad8437031820"
            "01200141186a360230200141286a41a0c2c0800010a480808000000b4701017f23"
            "808080800041206b2200248080808000200041003602182000410136020c200041"
            "8484c0800036020820004204370210200041086a41a084c0800010a48080800000"
            "0b5601017f23808080800041206b2202248080808000200241106a200041106a29"
            "0200370300200241086a200041086a290200370300200241013b011c2002200136"
            "021820022000290200370300200210ab80808000000bbe0601057f200041786a22"
            "012000417c6a280200220241787122006a21030240024020024101710d00200241"
            "0271450d012001280200220220006a21000240200120026b220141002802d0dfc0"
            "8000470d0020032802044103714103470d01410020003602c8dfc0800020032003"
            "280204417e7136020420012000410172360204200320003602000f0b2001200210"
            "a8808080000b024002400240024002400240200328020422024102710d00200341"
            "002802d4dfc08000460d02200341002802d0dfc08000460d032003200241787122"
            "0210a8808080002001200220006a2200410172360204200120006a200036020020"
            "0141002802d0dfc08000470d01410020003602c8dfc080000f0b20032002417e71"
            "36020420012000410172360204200120006a20003602000b2000418002490d0220"
            "01200010e38080800041002101410041002802e8dfc08000417f6a22003602e8df"
            "c0800020000d04024041002802b0ddc080002200450d0041002101034020014101"
            "6a2101200028020822000d000b0b4100200141ff1f200141ff1f4b1b3602e8dfc0"
            "80000f0b410020013602d4dfc08000410041002802ccdfc0800020006a22003602"
            "ccdfc08000200120004101723602040240200141002802d0dfc08000470d004100"
            "41003602c8dfc08000410041003602d0dfc080000b200041002802e0dfc0800022"
            "044d0d0341002802d4dfc080002200450d034100210241002802ccdfc080002205"
            "4129490d0241a8ddc080002101034002402001280200220320004b0d0020002003"
            "20012802046a490d040b200128020821010c000b0b410020013602d0dfc0800041"
            "0041002802c8dfc0800020006a22003602c8dfc080002001200041017236020420"
            "0120006a20003602000f0b200041f8017141b8ddc080006a210302400240410028"
            "02c0dfc08000220241012000410376742200710d00410020022000723602c0dfc0"
            "8000200321000c010b200328020821000b200320013602082000200136020c2001"
            "200336020c200120003602080f0b024041002802b0ddc080002201450d00410021"
            "020340200241016a2102200128020822010d000b0b4100200241ff1f200241ff1f"
            "4b1b3602e8dfc08000200520044d0d004100417f3602e0dfc080000b0b4d01017f"
            "23808080800041206b220324808080800020034100360210200341013602042003"
            "42043702082003200136021c200320003602182003200341186a36020020032002"
            "10a480808000000b840601057f0240024002402000417c6a220328020022044178"
            "71220541044108200441037122061b20016a490d0002402006450d002005200141"
            "276a4b0d020b41102002410b6a4178712002410b491b210102400240024020060d"
            "002001418002490d0120052001410472490d01200520016b418180084f0d010c02"
            "0b200041786a220720056a21060240024002400240200520014f0d002006410028"
            "02d4dfc08000460d03200641002802d0dfc08000460d0220062802042204410271"
            "0d042004417871220420056a22052001490d042006200410a88080800020052001"
            "6b22024110490d0120032001200328020041017172410272360200200720016a22"
            "012002410372360204200720056a220520052802044101723602042001200210a9"
            "8080800020000f0b200520016b2202410f4d0d0420032001200441017172410272"
            "360200200720016a22052002410372360204200620062802044101723602042005"
            "200210a98080800020000f0b200320052003280200410171724102723602002007"
            "20056a2202200228020441017236020420000f0b41002802c8dfc0800020056a22"
            "052001490d0102400240200520016b2202410f4b0d002003200441017120057241"
            "0272360200200720056a2202200228020441017236020441002102410021010c01"
            "0b20032001200441017172410272360200200720016a2201200241017236020420"
            "0720056a2205200236020020052005280204417e713602040b410020013602d0df"
            "c08000410020023602c8dfc0800020000f0b41002802ccdfc0800020056a220520"
            "014b0d040b0240200210998080800022050d0041000f0b20052000417c41782003"
            "28020022014103711b20014178716a2201200220012002491b10fa808080002102"
            "200010a580808000200221000b20000f0b419db8c08000412e41ccb8c0800010a6"
            "80808000000b41dcb8c08000412e418cb9c0800010a680808000000b2003200120"
            "0441017172410272360200200720016a2202200520016b22054101723602044100"
            "20053602ccdfc08000410020023602d4dfc0800020000b820301047f200028020c"
            "21020240024002402001418002490d002000280218210302400240024020022000"
            "470d00200041144110200028021422021b6a28020022010d01410021020c020b20"
            "002802082201200236020c200220013602080c010b200041146a200041106a2002"
            "1b21040340200421052001220241146a200241106a200228021422011b21042002"
            "4114411020011b6a28020022010d000b200541003602000b2003450d0202402000"
            "28021c41027441a8dcc080006a22012802002000460d0020034110411420032802"
            "102000461b6a20023602002002450d030c020b2001200236020020020d01410041"
            "002802c4dfc08000417e200028021c77713602c4dfc080000c020b024020022000"
            "2802082204460d002004200236020c200220043602080f0b410041002802c0dfc0"
            "8000417e200141037677713602c0dfc080000f0b20022003360218024020002802"
            "102201450d0020022001360210200120023602180b20002802142201450d002002"
            "2001360214200120023602180f0b0ba00401027f200020016a2102024002402000"
            "28020422034101710d002003410271450d012000280200220320016a2101024020"
            "0020036b220041002802d0dfc08000470d0020022802044103714103470d014100"
            "20013602c8dfc0800020022002280204417e713602042000200141017236020420"
            "0220013602000c020b2000200310a8808080000b02400240024002402002280204"
            "22034102710d00200241002802d4dfc08000460d02200241002802d0dfc0800046"
            "0d0320022003417871220310a8808080002000200320016a220141017236020420"
            "0020016a2001360200200041002802d0dfc08000470d01410020013602c8dfc080"
            "000f0b20022003417e7136020420002001410172360204200020016a2001360200"
            "0b02402001418002490d002000200110e3808080000f0b200141f8017141b8ddc0"
            "80006a21020240024041002802c0dfc08000220341012001410376742201710d00"
            "410020032001723602c0dfc08000200221010c010b200228020821010b20022000"
            "3602082001200036020c2000200236020c200020013602080f0b410020003602d4"
            "dfc08000410041002802ccdfc0800020016a22013602ccdfc08000200020014101"
            "72360204200041002802d0dfc08000470d01410041003602c8dfc0800041004100"
            "3602d0dfc080000f0b410020003602d0dfc08000410041002802c8dfc080002001"
            "6a22013602c8dfc0800020002001410172360204200020016a20013602000f0b0b"
            "7902017f017e23808080800041306b220324808080800020032001360204200320"
            "003602002003410236020c200341cc85c080003602082003420237021420034181"
            "80808000ad42208622042003ad8437032820032004200341046aad843703202003"
            "200341206a360210200341086a200210a480808000000b5d01027f238080808000"
            "41206b220124808080800020002802182102200141106a200041106a2902003703"
            "00200141086a200041086a2902003703002001200036021c200120023602182001"
            "2000290200370300200110e480808000000b490002402002418080c400460d0020"
            "00200220012802101181808080008080808000450d0041010f0b024020030d0041"
            "000f0b200020032004200128020c11808080800080808080000b7902017f017e23"
            "808080800041306b22032480808080002003200036020020032001360204200341"
            "0236020c200341bc8ac08000360208200342023702142003418180808000ad4220"
            "862204200341046aad84370328200320042003ad843703202003200341206a3602"
            "10200341086a200210a480808000000b820302017f017e23808080800041f0006b"
            "2203248080808000200341ccb7c0800036020c20032000360208200341ccb7c080"
            "00360214200320013602102003410236021c200341dc85c0800036021802402002"
            "2802000d002003410336025c2003419086c0800036025820034203370264200341"
            "8580808000ad4220862204200341106aad8437034820032004200341086aad8437"
            "03402003418680808000ad422086200341186aad843703382003200341386a3602"
            "60200341d8006a4184c4c0800010a480808000000b200341206a41106a20024110"
            "6a290200370300200341206a41086a200241086a29020037030020032002290200"
            "3703202003410436025c200341c486c08000360258200342043702642003418580"
            "808000ad4220862204200341106aad8437035020032004200341086aad84370348"
            "2003418880808000ad422086200341206aad843703402003418680808000ad4220"
            "86200341186aad843703382003200341386a360260200341d8006a4184c4c08000"
            "10a480808000000b1c0020002802002001200028020428020c1181808080008080"
            "8080000b14002001200028020020002802041093808080000b1400200128021420"
            "01280218200010b2808080000bbf05010a7f23808080800041306b220324808080"
            "8000200341033a002c2003412036021c4100210420034100360228200320013602"
            "2420032000360220200341003602142003410036020c0240024002400240024020"
            "0228021022050d00200228020c2200450d01200228020821012000410374210620"
            "00417f6a41ffffffff017141016a21042002280200210003400240200041046a28"
            "02002207450d00200328022020002802002007200328022428020c118080808000"
            "80808080000d040b20012802002003410c6a200128020411818080800080808080"
            "000d03200141086a2101200041086a2100200641786a22060d000c020b0b200228"
            "02142201450d00200141057421082001417f6a41ffffff3f7141016a2104200228"
            "02082109200228020021004100210603400240200041046a2802002201450d0020"
            "0328022020002802002001200328022428020c11808080800080808080000d030b"
            "2003200520066a220141106a28020036021c20032001411c6a2d00003a002c2003"
            "200141186a2802003602282001410c6a28020021074100210a4100210b02400240"
            "0240200141086a2802000e03010002010b2007410374210c4100210b2009200c6a"
            "220c2802040d01200c28020021070b4101210b0b200320073602102003200b3602"
            "0c200141046a280200210702400240024020012802000e03010002010b20074103"
            "74210b2009200b6a220b2802040d01200b28020021070b4101210a0b2003200736"
            "02182003200a3602142009200141146a2802004103746a22012802002003410c6a"
            "200128020411818080800080808080000d02200041086a21002008200641206a22"
            "06470d000b0b200420022802044f0d012003280220200228020020044103746a22"
            "012802002001280204200328022428020c1180808080008080808000450d010b41"
            "0121010c010b410021010b200341306a24808080800020010bd70201057f238080"
            "8080004180016b22022480808080000240024002400240200128021c2203411071"
            "0d0020034120710d012000ad4101200110848080800021000c030b41ff00210303"
            "402002200322046a22052000410f712203413072200341d7006a2003410a491b3a"
            "00002004417f6a210320004110492106200041047621002006450d000c020b0b41"
            "ff00210303402002200322046a22052000410f712203413072200341376a200341"
            "0a491b3a00002004417f6a210320004110492106200041047621002006450d000b"
            "02402004418101490d002004418001419887c08000108580808000000b20014101"
            "41a887c0800041022005418101200441016a6b10868080800021000c010b024020"
            "04418101490d002004418001419887c08000108580808000000b2001410141a887"
            "c0800041022005418101200441016a6b10868080800021000b20024180016a2480"
            "8080800020000b2200200128021441c284c08000410e200128021828020c118080"
            "80800080808080000b6001017f23808080800041306b2200248080808000200041"
            "0136020c200041e484c08000360208200042013702142000418980808000ad4220"
            "862000412f6aad843703202000200041206a360210200041086a41e8c1c0800010"
            "a480808000000b7902017f017e23808080800041306b2203248080808000200320"
            "00360200200320013602042003410236020c200341f08ac0800036020820034202"
            "3702142003418180808000ad4220862204200341046aad84370328200320042003"
            "ad843703202003200341206a360210200341086a200210a480808000000b6a0101"
            "7f23808080800041306b22032480808080002003200136020c2003200036020820"
            "034101360214200341f0bac080003602102003420137021c2003418680808000ad"
            "422086200341086aad843703282003200341286a360218200341106a200210a480"
            "808000000b920c01057f23808080800041206b2203248080808000024002400240"
            "024002400240024002400240024002400240024002400240024020010e28060101"
            "010101010101020401010301010101010101010101010101010101010101010901"
            "01010107000b200141dc00460d040b2001418006490d0b20024101710d060c0b0b"
            "20004180043b010a20004200370102200041dce8013b01000c0c0b20004180043b"
            "010a20004200370102200041dce4013b01000c0b0b20004180043b010a20004200"
            "370102200041dcdc013b01000c0a0b20004180043b010a20004200370102200041"
            "dcb8013b01000c090b20004180043b010a20004200370102200041dce0003b0100"
            "0c080b200241800271450d0620004180043b010a20004200370102200041dcce00"
            "3b01000c070b200141aa9d044b410474220220024108722202200241027441809b"
            "c080006a280200410b742001410b7422024b1b2204200441047222042004410274"
            "41809bc080006a280200410b7420024b1b22042004410272220420044102744180"
            "9bc080006a280200410b7420024b1b2204200441016a2204200441027441809bc0"
            "80006a280200410b7420024b1b2204200441016a2204200441027441809bc08000"
            "6a280200410b7420024b1b220441027441809bc080006a280200410b7422052002"
            "4620052002496a20046a220441204b0d01200441027441809bc080006a22052802"
            "00411576210241d70521060240024020044120460d002005280204411576210620"
            "040d00410021040c010b200441027441fc9ac080006a28020041ffffff00712104"
            "0b024020062002417f736a450d00200120046b2107200241d705200241d7054b1b"
            "21052006417f6a210641002104034020052002460d042004200241849cc080006a"
            "2d00006a220420074b0d012006200241016a2202470d000b200621020b20024101"
            "71450d04200341003a000a200341003b01082003200141147641b284c080006a2d"
            "00003a000b20032001410476410f7141b284c080006a2d00003a000f2003200141"
            "0876410f7141b284c080006a2d00003a000e20032001410c76410f7141b284c080"
            "006a2d00003a000d20032001411076410f7141b284c080006a2d00003a000c2003"
            "41086a20014101726741027622026a220441fb003a00002004417f6a41f5003a00"
            "00200341086a2002417e6a22026a41dc003a0000200341086a41086a2204200141"
            "0f7141b284c080006a2d00003a00002000410a3a000b200020023a000a20002003"
            "290208370200200341fd003a0011200041086a20042f01003b01000c060b200241"
            "808004710d020c040b2004412141e09ac0800010aa80808000000b200541d70541"
            "f09ac0800010aa80808000000b20004180043b010a20004200370102200041dcc4"
            "003b01000c020b024020014120490d00200141ff00490d01024020014180800449"
            "0d0002402001418080084f0d00200141c48fc08000412c419c90c0800041c40141"
            "e091c0800041c20310b980808000450d020c030b200141feffff0071419ef00a46"
            "0d01200141e0ffff007141e0cd0a460d01200141c091756a41794b0d01200141d0"
            "e2746a41714b0d0120014190a8746a41704b0d012001418090746a41dd6c4b0d01"
            "2001418080746a419d744b0d01200141b0d9736a417a4b0d0120014180fe476a41"
            "afc5544b0d01200141f083384f0d010c020b200141a295c08000412841f295c080"
            "0041a002419298c0800041ad0210b9808080000d010b200341003a001620034100"
            "3b01142003200141147641b284c080006a2d00003a001720032001410476410f71"
            "41b284c080006a2d00003a001b20032001410876410f7141b284c080006a2d0000"
            "3a001a20032001410c76410f7141b284c080006a2d00003a001920032001411076"
            "410f7141b284c080006a2d00003a0018200341146a20014101726741027622026a"
            "220441fb003a00002004417f6a41f5003a0000200341146a2002417e6a22026a41"
            "dc003a0000200341146a41086a22042001410f7141b284c080006a2d00003a0000"
            "2000410a3a000b200020023a000a20002003290214370200200341fd003a001d20"
            "0041086a20042f01003b01000c010b2000200136020420004180013a00000b2003"
            "41206a2480808080000be90201067f200120024101746a210720004180fe037141"
            "0876210841002109200041ff0171210a02400240024002400340200141026a210b"
            "200920012d000122026a210c024020012d000022012008460d00200120084b0d04"
            "200c2109200b2101200b2007470d010c040b200c2009490d01200c20044b0d0220"
            "0320096a21010340024020020d00200c2109200b2101200b2007470d020c050b20"
            "02417f6a210220012d00002109200141016a21012009200a470d000b0b41002102"
            "0c030b2009200c41b48fc0800010b680808000000b200c200441b48fc0800010ad"
            "80808000000b200041ffff03712109200520066a210c410121020340200541016a"
            "210a0240024020052c000022014100480d00200a21050c010b0240200a200c460d"
            "00200141ff007141087420052d0001722101200541026a21050c010b41a48fc080"
            "0010a080808000000b200920016b22094100480d01200241017321022005200c47"
            "0d000b0b20024101710b13002000200120022003200410bb80808000000bd10902"
            "057f017e23808080800041f0006b22052480808080002005200336020c20052002"
            "3602080240024002400240024002400240024002402001418102490d0002402000"
            "2c00800241bf7f4c0d00410321060c030b20002c00ff0141bf7f4c0d0141022106"
            "0c020b200520013602142005200036021041002106410121070c020b20002c00fe"
            "0141bf7f4a21060b2000200641fd016a22066a2c000041bf7f4c0d012005200636"
            "0214200520003602104105210641808dc0800021070b2005200636021c20052007"
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
            "6a36022c20054105360234200541888ec080003602302005420537023c20054186"
            "80808000ad422086220a200541186aad843703682005200a200541106aad843703"
            "602005418a80808000ad422086200541286aad843703582005418b80808000ad42"
            "2086200541246aad843703502005418180808000ad422086200541206aad843703"
            "482005200541c8006a360238200541306a200410a480808000000b200520022003"
            "20061b36022820054103360234200541c88ec080003602302005420337023c2005"
            "418680808000ad422086220a200541186aad843703582005200a200541106aad84"
            "3703502005418180808000ad422086200541286aad843703482005200541c8006a"
            "360238200541306a200410a480808000000b2000200141002006200410ba808080"
            "00000b20054104360234200541a88dc080003602302005420437023c2005418680"
            "808000ad422086220a200541186aad843703602005200a200541106aad84370358"
            "2005418180808000ad422086220a2005410c6aad843703502005200a200541086a"
            "ad843703482005200541c8006a360238200541306a200410a480808000000b2002"
            "200641f48ec0800010b680808000000b200410a080808000000b20002001200220"
            "01200410ba80808000000b4d01017f4101210202402000280200200110b3808080"
            "000d00200128021441b084c080004102200128021828020c118080808000808080"
            "80000d002000280204200110b38080800021020b20020bc40101047f2380808080"
            "0041106b2202248080808000410121030240200128021422044127200128021822"
            "05280210220111818080800080808080000d00200241046a200028020041810210"
            "b8808080000240024020022d0004418001470d0020042002280208200111818080"
            "80008080808000450d010c020b2004200241046a20022d000e22006a20022d000f"
            "20006b200528020c11808080800080808080000d010b2004412720011181808080"
            "00808080800021030b200241106a24808080800020030b2701017f200028020022"
            "002000411f7522027320026bad2000417f73411f7620011084808080000b500103"
            "7f200121032002210402402001280288022205450d00200241016a210320012f01"
            "900321040b200141c80341980320021b1082808080002000200536020020002004"
            "ad4220862003ad843702040bec0201047f2000418c026a22052001410c6c6a2106"
            "02400240200141016a220720002f01920322084d0d002006200229020037020020"
            "0641086a200241086a2802003602000c010b20052007410c6c6a2006200820016b"
            "2205410c6c10f9808080001a200641086a200241086a2802003602002006200229"
            "02003702002000200741186c6a2000200141186c6a200541186c10f9808080001a"
            "0b200841016a21022000200141186c6a22062003290300370300200641106a2003"
            "41106a290300370300200641086a200341086a29030037030020004198036a2103"
            "0240200141026a2205200841026a22064f0d00200320054102746a200320074102"
            "746a200820016b41027410f9808080001a0b200320074102746a20043602002000"
            "20023b0192030240200720064f0d00200841016a2103200141027420006a419c03"
            "6a2107034020072802002208200141016a22013b01900320082000360288022007"
            "41046a210720032001470d000b0b0bed04010a7f23808080800041d0006b220224"
            "808080800041002d00e0dbc080001a200128020022032f01920321040240024002"
            "400240024041c8031099808080002205450d002005410036028802200520012802"
            "082206417f7320032f01920322076a22083b019203200241286a41086a2003418c"
            "026a22092006410c6c6a220a41086a280200360200200241386a41086a20032006"
            "41186c6a220b41086a290300370300200241386a41106a200b41106a2903003703"
            "002002200a2902003703282002200b2903003703382008410c4f0d012007200641"
            "016a220b6b2008470d022005418c026a2009200b410c6c6a2008410c6c10fa8080"
            "80001a20052003200b41186c6a200841186c10fa80808000210b200320063b0192"
            "03200241086a200241286a41086a280200360200200241186a200241386a41086a"
            "290300370300200241206a200241c8006a29030037030020022002290328370300"
            "20022002290338370310200b2f019203220541016a21082005410c4f0d03200420"
            "066b220a2008470d04200b4198036a200320064102746a419c036a200a41027410"
            "fa80808000210a200128020421014100210602400340200a20064102746a280200"
            "220820063b0190032008200b36028802200620054f0d01200620062005496a2206"
            "20054d0d000b0b2000200136022c2000200336022820002002412810fa80808000"
            "220620013602342006200b360230200241d0006a2480808080000f0b000b200841"
            "0b41a0a4c0800010ad80808000000b41e8a3c0800041284190a4c0800010a68080"
            "8000000b2008410c41b0a4c0800010ad80808000000b41e8a3c0800041284190a4"
            "c0800010a680808000000b900801017f23808080800041f0006b22022480808080"
            "0020002802002100200241003602482002428080808010370240200241033a006c"
            "2002412036025c2002410036026820024198a5c080003602642002410036025420"
            "02410036024c2002200241c0006a36026002400240024002400240024002400240"
            "024002400240024002400240024002400240024002400240024002400240024002"
            "400240024020002802000e191718000102030405060708090a0b0c0d0e0f101112"
            "13141516170b200241c0006a41d9a6c0800041181091808080000d190c180b2002"
            "41c0006a41f1a6c08000411b1091808080000d180c170b200241c0006a418ca7c0"
            "8000411a1091808080000d170c160b200241c0006a41a6a7c08000411910918080"
            "80000d160c150b200241c0006a41bfa7c08000410c1091808080000d150c140b20"
            "0241c0006a41cba7c0800041131091808080000d140c130b200241c0006a41dea7"
            "c0800041131091808080000d130c120b200241c0006a41f1a7c08000410e109180"
            "8080000d120c110b200241c0006a41ffa7c08000410e1091808080000d110c100b"
            "200241c0006a418da8c08000410c1091808080000d100c0f0b200241c0006a4199"
            "a8c08000410e1091808080000d0f0c0e0b200241c0006a41a7a8c08000410e1091"
            "808080000d0e0c0d0b200241c0006a41b5a8c0800041131091808080000d0d0c0c"
            "0b200241c0006a41c8a8c08000411a1091808080000d0c0c0b0b200241c0006a41"
            "e2a8c08000413e1091808080000d0b0c0a0b200241c0006a41a0a9c08000411410"
            "91808080000d0a0c090b200241c0006a41b4a9c0800041341091808080000d090c"
            "080b200241c0006a41e8a9c08000412c1091808080000d080c070b200241c0006a"
            "4194aac0800041241091808080000d070c060b200241c0006a41b8aac08000410e"
            "1091808080000d060c050b200241c0006a41c6aac0800041131091808080000d05"
            "0c040b200241c0006a41d9aac08000411c1091808080000d040c030b200241c000"
            "6a41f5aac0800041181091808080000d030c020b200241c0006a20002802042000"
            "280208109180808000450d010c020b200041046a200241cc006a10c3808080000d"
            "010b200241306a41086a200241c0006a41086a2802003602002002200229024037"
            "0330200241818080800036022c20024181808080003602242002418c8080800036"
            "021c20024104360204200241a8abc080003602002002420337020c200220004110"
            "6a36022820022000410c6a3602202002200241306a3602182002200241186a3602"
            "0820012802142001280218200210b2808080002100024020022802302201450d00"
            "200228023420011082808080000b200241f0006a24808080800020000f0b41c0a5"
            "c080004137200241186a41b0a5c0800041c4a6c08000109f80808000000be50301"
            "017f23808080800041c0006b220224808080800002400240024002400240024020"
            "002d00000e0400010203000b2002200028020436020441002d00e0dbc080001a41"
            "141099808080002200450d04200041106a410028009cc5c0800036000020004108"
            "6a4100290094c5c080003700002000410029008cc5c08000370000200241143602"
            "102002200036020c200241143602082002410336022c200241bcc1c08000360228"
            "200242023702342002418d80808000ad422086200241046aad843703202002418e"
            "80808000ad422086200241086aad843703182002200241186a3602302001280214"
            "2001280218200241286a10b280808000210020022802082201450d03200228020c"
            "20011082808080000c030b20002d000121002002410136022c200241f0bac08000"
            "360228200242013702342002418680808000ad422086200241186aad8437030820"
            "022000410274220041e0c5c080006a28020036021c200220004184c7c080006a28"
            "02003602182002200241086a36023020012802142001280218200241286a10b280"
            "80800021000c020b20012000280204220028020020002802041093808080002100"
            "0c010b200028020422002802002001200028020428021011818080800080808080"
            "0021000b200241c0006a24808080800020000f0b000bd507010d7f238080808000"
            "41106b220224808080800020002802082103200028020421044101210502402001"
            "2802142206412220012802182207280210220811818080800080808080000d0002"
            "40024020030d0041002103410021000c010b410021094100210a2004210b200321"
            "0c024002400340200b200c6a210d4100210002400340200b20006a220e2d000022"
            "0141817f6a41ff017141a101490d0120014122460d01200141dc00460d01200c20"
            "0041016a2200470d000b200a200c6a210a0c030b02400240200e2c00002201417f"
            "4c0d00200e41016a210b200141ff017121010c010b200e2d0001413f71210b2001"
            "411f71210c02402001415f4b0d00200c410674200b722101200e41026a210b0c01"
            "0b200b410674200e2d0002413f7172210b0240200141704f0d00200b200c410c74"
            "722101200e41036a210b0c010b200b410674200e2d0003413f7172200c41127441"
            "8080f00071722101200e41046a210b0b2000200a6a2100200241046a2001418180"
            "0410b8808080000240024020022d0004418001460d0020022d000f20022d000e6b"
            "41ff01714101460d0020002009490d0302402009450d000240200920034f0d0020"
            "0420096a2c000041bf7f4a0d010c050b20092003470d040b02402000450d000240"
            "200020034f0d00200420006a2c000041bf7f4c0d050c010b20002003470d040b20"
            "06200420096a200020096b200728020c220e11808080800080808080000d010240"
            "024020022d0004418001470d002006200228020820081181808080008080808000"
            "450d010c030b2006200241046a20022d000e220c6a20022d000f200c6b200e1180"
            "8080800080808080000d020b0240024020014180014f0d004101210e0c010b0240"
            "20014180104f0d004102210e0c010b41034104200141808004491b210e0b200e20"
            "006a21090b0240024020014180014f0d00410121010c010b024020014180104f0d"
            "00410221010c010b41034104200141808004491b21010b200120006a210a200d20"
            "0b6b220c0d010c030b0b410121050c030b2004200320092000419089c0800010ba"
            "80808000000b02402009200a4b0d004100210002402009450d000240200920034f"
            "0d0020092100200420096a2c000041bf7f4c0d020c010b2003210020092003470d"
            "010b0240200a0d00410021030c020b0240200a20034f0d00200021092004200a6a"
            "2c000041bf7f4c0d01200a21030c020b20002109200a2003460d010b2004200320"
            "09200a41a089c0800010ba80808000000b2006200420006a200320006b20072802"
            "0c11808080800080808080000d0020064122200811818080800080808080002105"
            "0b200241106a24808080800020050b870102017c017e0240024002402001280200"
            "0e03000102000b20004202370308200020012b0308220239031020002002bd42ff"
            "ffffffffffffffff00834280808080808080f8ff00534101743a00000f0b200042"
            "00370308200041023a0000200020012903083703100f0b200041023a0000200020"
            "01290308220337031020002003423f883703080be411020b7f027e238080808000"
            "41c0016b2204248080808000024002400240024002400240024002400240024002"
            "400240024020012802002205450d00200228020821062002280204210720012802"
            "042108024003402005418c026a210920052f019203220a410c6c210b417f210c02"
            "40024003400240200b0d00200a210c0c020b2009280208210d2009280204210e20"
            "0c41016a210c200b41746a210b2009410c6a2109417f2007200e2006200d200620"
            "0d491b10f780808000220e2006200d6b200e1b220d410047200d4100481b220d41"
            "01460d000b200d41ff0171450d010b2008450d022008417f6a21082005200c4102"
            "746a4198036a28020021050c010b0b20022802002209450d0c2007200910828080"
            "80000c0c0b2002290204220fa721092002280200220b418080808078470d032009"
            "21050c010b2002290204220fa721052002280200220d418080808078470d010b20"
            "01210c0c090b41002d00e0dbc080001a4198031099808080002209450d02200941"
            "013b01920320094100360288022009200f422088a7ad4220862005ad8437039002"
            "2009200d36028c0220014280808080103702042001200936020020092003290300"
            "370300200941086a200341086a290300370300200941106a200341106a29030037"
            "03000c010b200f422088a7ad4220862009ad84210f024002400240024002402005"
            "2f0192032209410b490d00200441086a21084104210d200c4105490d03200c210d"
            "200c417b6a0e020302010b2005418c026a220e200c410c6c6a210d02400240200c"
            "41016a220620094d0d00200d200f370204200d200b3602000c010b200e2006410c"
            "6c6a200d2009200c6b220e410c6c10f9808080001a200d200f370204200d200b36"
            "02002005200641186c6a2005200c41186c6a200e41186c10f9808080001a0b2005"
            "200c41186c6a220d41106a200341106a290300370300200d200329030037030020"
            "0d41086a200341086a2903003703002005200941016a3b0192030c030b200c4179"
            "6a210c200441f8006a21084106210d0c010b4100210c200441f8006a2108410521"
            "0d0b41002d00e0dbc080001a4198031099808080002209450d02200941003b0192"
            "0320094100360288022009200d417f7320052f01920322076a22063b0192032004"
            "4188016a41086a2005200d41186c6a220e41086a29030037030020044188016a41"
            "106a200e41106a2903003703002004200e290300370388012006410c4f0d032007"
            "200d41016a220e6b2006470d042005418c026a2202200d410c6c6a220729020421"
            "10200728020021072009418c026a2002200e410c6c6a2006410c6c10fa80808000"
            "1a20092005200e41186c6a200641186c10fa8080800021062005200d3b01920320"
            "0441dc006a410c6a20044190016a290300370200200441f0006a20044198016a29"
            "030037020020042004290388013702602004200536020820042006360278200828"
            "0200220d418c026a200c410c6c6a210602400240200d2f019203220e200c4b0d00"
            "2006200f3702042006200b3602000c010b2006410c6a2006200e200c6b2208410c"
            "6c10f9808080001a2006200f3702042006200b360200200d200c41186c6a220b41"
            "186a200b200841186c10f9808080001a0b200d200c41186c6a220b41106a200341"
            "106a290300370300200b2003290300370300200b41086a200341086a2903003703"
            "00200d200e41016a3b0192032007418080808078460d00200441c4006a200441dc"
            "006a41086a290200370200200441cc006a200441dc006a41106a29020037020020"
            "0441306a41246a200441dc006a41186a2802003602002004201037023420042007"
            "3602302004200429025c37023c024002400240200528028802220b0d004100210c"
            "0c010b200441306a4104722108200441b8016a210220044188016a410472210720"
            "0441b0016a2103200441c0006a210e4100210c4100210603402006200c470d0820"
            "052f019003210d200b2f019203410b490d02200641016a21060240024002400240"
            "024002400240200d4105490d00200d417b6a0e020203010b200441043602800120"
            "04200636027c2004200b3602782003210b0c040b20044106360280012004200636"
            "027c2004200b360278200d41796a210d0c020b2004410536028001200420063602"
            "7c2004200b36027820044188016a200441f8006a10c18080800020042802b00141"
            "05200441306a200e200910c080808000200428028801210d200441086a20074124"
            "10fa808080001a0c030b20044105360280012004200636027c2004200b36027841"
            "00210d0b2002210b0b20044188016a200441f8006a10c180808000200b28020020"
            "0d200441306a200e200910c080808000200428028801210d200441086a20074124"
            "10fa808080001a0b20042802bc01210c20042802b801210920042802b401210620"
            "042802b0012105200d418080808078460d032004200d3602302008200441086a41"
            "2410fa808080001a200528028802220b0d000b0b2001280200220b450d0741002d"
            "00e0dbc080001a2001280204210641c803109980808000220d450d03200d200b36"
            "029803200d41003b019203200d410036028802200b41003b019003200b200d3602"
            "88022001200641016a3602042001200d3602002006200c470d08200d2004290330"
            "37028c02200d41013b019203200d2004290340370300200d200936029c03200d41"
            "94026a200441306a41086a280200360200200d41086a200441c8006a2903003703"
            "00200d41106a200441d0006a2903003703002009200d36028802200941013b0190"
            "030c010b200b200d200441306a200e200910c0808080000b200120012802084101"
            "6a3602080b200041063a00000c070b000b2006410b41a0a4c0800010ad80808000"
            "000b41e8a3c0800041284190a4c0800010a680808000000b41c0a4c08000413541"
            "f8a4c0800010a680808000000b41bca2c0800010a080808000000b41a7a3c08000"
            "413041d8a3c0800010a680808000000b20002005200c41186c6a22092903003703"
            "00200041106a200941106a220d290300370300200041086a200941086a220b2903"
            "0037030020092003290300370300200b200341086a290300370300200d20034110"
            "6a2903003703000b200441c0016a2480808080000be60301057f02400240024002"
            "400240024020022003490d00410121044100210520034101480d04200120036a21"
            "060240200341034b0d000340200620014d0d062006417f6a22062d0000410a470d"
            "000c050b0b024041808284082006417c6a2800002207418a94a8d000736b200772"
            "41808182847871418081828478460d000340200620014d0d062006417f6a22062d"
            "0000410a470d000c050b0b200320064103716b210720034109490d010340024002"
            "4020074108480d004180828408200120076a220641786a2802002208418a94a8d0"
            "00736b20087241808182847871418081828478460d010b200120076a21060c040b"
            "200741786a210741808284082006417c6a2802002208418a94a8d000736b200872"
            "41808182847871418081828478460d000c030b0b2003200241b8adc0800010ad80"
            "808000000b200120076a21060340200620014d0d032006417f6a22062d0000410a"
            "470d000c020b0b0340200620014d0d022006417f6a22062d0000410a470d000b0b"
            "200620016b41016a220520024b0d010b0240200120056a20014d0d004100210620"
            "0521070340200620012d0000410a466a2106200141016a21012007417f6a22070d"
            "000b200641016a21040b200020043602002000200320056b3602040f0b20052002"
            "41c8adc0800010ad80808000000b9b0d02097f017e23808080800041306b220324"
            "808080800002400240024002400240024002400240024003400240024020012802"
            "08220420012802042205460d00024002400240200420054f0d0020012802002206"
            "20046a2d000022074122460d01200741dc00460d0120074120490d012006200441"
            "016a22086a21094100200520086b417871220a6b210703402009210b024020070d"
            "002001200a20086a360208200110c9808080002001280204210520012802082107"
            "0c040b200741086a2107200b41086a2109200b290000220c42a2c48891a2c48891"
            "228542fffdfbf7efdfbfff7e7c200c42e0bffffefdfbf7ef5f7c84200c42dcb8f1"
            "e2c58b97aedc008542fffdfbf7efdfbfff7e7c84200c427f858342808182848890"
            "a0c0807f83220c500d000b2001200b20066b200c7aa74103766a22073602080c02"
            "0b2004200541d8adc0800010aa808080000c080b200421070b20072005470d0120"
            "0521040b200341086a20012802002004200410c78080800041002d00e0dbc08000"
            "1a200328020c210b2003280208210941141099808080002207450d052007200936"
            "020c2007410436020020002007360204200041023602002007200b3602100c0a0b"
            "024020072005490d002007200541e8adc0800010aa80808000000b024020012802"
            "00220b20076a2d0000220941dc00460d00024020094122470d002002280208450d"
            "0520072004490d072002200b20046a200720046b10ca808080004101210b200120"
            "0741016a360208200341286a20012002280204200228020810cb80808000200328"
            "02282207450d032000200328022c3602080c040b2001200741016a220736020820"
            "0341106a200b2005200710c78080800041002d00e0dbc080001a2003280214210b"
            "2003280210210941141099808080002207450d052007200936020c200741103602"
            "0020002007360204200041023602002007200b3602100c0a0b024020072004490d"
            "002002200b20046a200720046b10ca808080002001200741016a22093602080240"
            "20092005490d00200341206a200b2005200910c78080800041002d00e0dbc08000"
            "1a2003280224210b2003280220210941141099808080002207450d062007410436"
            "02000c090b2001200741026a220436020802400240024002400240024002400240"
            "024002400240200b20096a2d0000220741ed004a0d000240200741e1004a0d0020"
            "074122460d032007412f460d04200741dc00470d02024020022802082207200228"
            "0200470d002002108f808080000b2002200741016a360208200228020420076a41"
            "dc003a0000410021070c0b0b2007419e7f6a0e050401010105010b200741927f6a"
            "0e080500000006000708000b200341186a200b2005200410c78080800041002d00"
            "e0dbc080001a200328021c210b2003280218210941141099808080002207450d0e"
            "2007410c3602000c110b0240200228020822072002280200470d002002108f8080"
            "80000b2002200741016a360208200228020420076a41223a0000410021070c070b"
            "0240200228020822072002280200470d002002108f808080000b2002200741016a"
            "360208200228020420076a412f3a0000410021070c060b02402002280208220720"
            "02280200470d002002108f808080000b2002200741016a36020820022802042007"
            "6a41083a0000410021070c050b0240200228020822072002280200470d00200210"
            "8f808080000b2002200741016a360208200228020420076a410c3a000041002107"
            "0c040b0240200228020822072002280200470d002002108f808080000b20022007"
            "41016a360208200228020420076a410a3a0000410021070c030b02402002280208"
            "22072002280200470d002002108f808080000b2002200741016a36020820022802"
            "0420076a410d3a0000410021070c020b0240200228020822072002280200470d00"
            "2002108f808080000b2002200741016a360208200228020420076a41093a000041"
            "0021070c010b2001200210cc8080800021070b2007450d010c090b0b2004200741"
            "98aec0800010b680808000000b4102210b200328022c21070b2000200b36020020"
            "0020073602040c060b20072004490d022001200741016a360208200341286a2001"
            "200b20046a200720046b10cb808080000240024020032802282207450d00200020"
            "0328022c3602084100210b0c010b4102210b200328022c21070b2000200b360200"
            "200020073602040c050b000b2004200741f8adc0800010b680808000000b200420"
            "074188aec0800010b680808000000b2007200936020c2007200b3602100b200041"
            "02360200200020073602040b200341306a2480808080000b5301047f0240200028"
            "02082201200028020422024f0d00200028020021030340200320016a2d00002204"
            "4122460d01200441dc00460d0120044120490d012000200141016a220136020820"
            "022001470d000b0b0b4901017f02402000280200200028020822036b20024f0d00"
            "200020032002109080808000200028020821030b200028020420036a2001200210"
            "fa808080001a2000200320026a3602080bb10501077f23808080800041106b2204"
            "24808080800002402003450d004100200341796a2205200520034b1b2106200241"
            "036a417c7120026b21074100210503400240024002400240200220056a2d000022"
            "08c022094100480d00200720056b4103710d01200520064f0d020340200220056a"
            "2208280204200828020072418081828478710d03200541086a22052006490d000c"
            "030b0b0240024002400240024002400240200841808bc080006a2d0000417e6a0e"
            "03000102050b200541016a220520034f0d04200220056a2c000041bf7f4a0d040c"
            "050b200541016a220a20034f0d032002200a6a2c0000210a02400240200841e001"
            "460d00200841ed01460d012009411f6a41ff0171410c490d032009417e71416e47"
            "0d05200a4140480d040c050b200a41607141a07f460d030c040b200a419f7f4a0d"
            "030c020b200541016a220a20034f0d022002200a6a2c0000210a02400240024002"
            "40200841907e6a0e050100000002000b2009410f6a41ff017141024b0d05200a41"
            "40480d020c050b200a41f0006a41ff01714130490d010c040b200a418f7f4a0d03"
            "0b200541026a220820034f0d02200220086a2c000041bf7f4a0d02200541036a22"
            "0520034f0d02200220056a2c000041bf7f4c0d030c020b200a41404e0d010b2005"
            "41026a220520034f0d00200220056a2c000041bf7f4c0d010b200441086a200128"
            "02002001280204200128020810c7808080004100210241002d00e0dbc080001a20"
            "0428020c210520042802082108024041141099808080002203450d002003200836"
            "020c2003410f360200200320053602100c060b000b200541016a21050c020b2005"
            "41016a21050c010b200520034f0d000340200220056a2c00004100480d01200320"
            "0541016a2205470d000c030b0b20052003490d000b0b2000200236020020002003"
            "360204200441106a2480808080000be20601057f23808080800041206b22022480"
            "80808000200241146a200010cd808080000240024020022f01140d000240024002"
            "4002400240024020022f011622034180f803714180b803460d0020034180c8006a"
            "41ffff03714180f803490d04200241146a200010ce8080800020022d00140d0620"
            "022d0015210420002000280208220541016a360208200441dc00470d0320024114"
            "6a200010ce8080800020022d00140d0620022d001521042000200541026a360208"
            "200441f500470d02200241146a200010cd8080800020022f01140d0620022f0116"
            "22044180c0006a41ffff03714180f803490d0120034180d0006a41ffff0371410a"
            "7420044180c8006a41ffff0371722205418080046a210302402001280200200128"
            "020822006b41034b0d00200120004104109080808000200128020821000b200120"
            "0041046a360208200128020420006a2200200341127641f001723a000020004103"
            "6a2004413f71418001723a000020002005410676413f71418001723a0002200020"
            "03410c76413f71418001723a0001410021000c070b200220002802002000280204"
            "200028020810c78080800041002d00e0dbc080001a200228020421012002280200"
            "210341141099808080002200450d042000200336020c2000411436020020002001"
            "3602100c060b200241086a20002802002000280204200028020810c78080800041"
            "002d00e0dbc080001a200228020c21012002280208210341141099808080002200"
            "450d032000200336020c20004114360200200020013602100c050b200241173602"
            "142000200241146a10cf8080800021000c040b200241173602142000200241146a"
            "10cf8080800021000c030b0240024002402003418001490d000240200128020020"
            "0128020822046b41034b0d00200120044104109080808000200128020821040b20"
            "0128020420046a210020034180104f0d0120034106764140722106410221050c02"
            "0b0240200128020822002001280200470d002001108f808080000b200120004101"
            "6a360208200128020420006a20033a0000410021000c040b20002003410676413f"
            "71418001723a00012003410c764160722106410321050b200020063a0000200120"
            "0420056a360208200020056a417f6a2003413f71418001723a0000410021000c02"
            "0b000b200228021821000b200241206a24808080800020000b910301057f238080"
            "80800041106b220224808080800002400240024002402001280204220320012802"
            "082204490d000240200320046b41034b0d0020012003360208200241086a200128"
            "02002003200310c78080800041002d00e0dbc080001a200228020c210320022802"
            "08210441141099808080002201450d022001200436020c20014104360200200020"
            "01360204200120033602100c030b2001200441046a220536020802402001280200"
            "220620046a22012d000141017441b8aec080006a2f010020012d000041017441b8"
            "b2c080006a2f010072c141087420012d000241017441b8b2c080006a2e01007220"
            "012d000341017441b8aec080006a2e0100722201417f4a0d002002200620032005"
            "10c78080800041002d00e0dbc080001a2002280204210320022802002104411410"
            "99808080002201450d022001200436020c2001410c360200200020013602042001"
            "20033602100c030b200020013b0102410021010c030b2004200341a8aec0800010"
            "85808080000b000b410121010b200020013b0100200241106a2480808080000bb2"
            "0101037f23808080800041106b2202248080808000024002400240200128020822"
            "0320012802042204490d00200241086a20012802002004200310c7808080004100"
            "2d00e0dbc080001a200228020c2103200228020821044114109980808000220145"
            "0d022001200436020c200141043602002000200136020420012003360210410121"
            "010c010b2000200128020020036a2d00003a0001410021010b200020013a000020"
            "0241106a2480808080000f0b000b8c0101037f23808080800041106b2202248080"
            "808000200241086a20002802002000280204200028020810c78080800041002d00"
            "e0dbc080001a200228020c2103200228020821040240411410998080800022000d"
            "00000b2000200436020c2000200129020037020020002003360210200041086a20"
            "0141086a280200360200200241106a24808080800020000b1f0002402001280204"
            "0e020000000b200041b8b6c08000200110b2808080000b820101017f0240024002"
            "4002402003280204450d000240200328020822040d002002450d0341002d00e0db"
            "c080001a0c020b20032802002004200210a78080800021030c030b2002450d0141"
            "002d00e0dbc080001a0b200210998080800021030c010b200121030b2000200236"
            "020820002003200120031b36020420002003453602000b9f0202047f017e238080"
            "80800041206b2206248080808000024002400240200220036a220320024f0d0041"
            "0021020c010b41002102200420056a417f6a410020046b71ad4108410420054101"
            "461b22072001280200220841017422092003200920034b1b2203200720034b1b22"
            "07ad7e220a422088a70d00200aa7220941808080807820046b4b0d010240024020"
            "080d00410021020c010b2006200820056c36021c20062001280204360214200421"
            "020b20062002360218200641086a20042009200641146a10d18080800002402006"
            "2802080d00200628020c2102200120073602002001200236020441818080807821"
            "020c010b20062802102103200628020c21020c010b0b2000200336020420002002"
            "360200200641206a2480808080000b5901017f23808080800041106b2201248080"
            "808000200141086a2000200028020041014108411810d280808000024020012802"
            "082200418180808078460d002000200128020c109a80808000000b200141106a24"
            "80808080000b4701017f23808080800041206b2200248080808000200041003602"
            "182000410136020c200041d8bac0800036020820004204370210200041086a41e0"
            "bac0800010a480808000000bf90103027f037e017f23808080800041206b220024"
            "808080800041002d00e0dbc080001a02400240024041201099808080002201450d"
            "0020014102360210200142818080801037030041002903a0dcc080002102034020"
            "02427f510d024100200242017c220341002903a0dcc08000220420042002512205"
            "1b3703a0dcc08000200421022005450d000b410020033703f8dfc0800020012003"
            "3703084100280280e0c08000450d02200041003602182000410136020c200041e0"
            "b6c0800036020820004204370210200041086a41b8b7c0800010a4808080000b00"
            "0b10d480808000000b41002001360280e0c08000200041206a2480808080000b5b"
            "01027f024020002802104101470d002000280214220141003a0000200028021822"
            "02450d00200120021082808080000b02402000417f460d00200020002802042201"
            "417f6a36020420014101470d00200041201082808080000b0b3a01017f23808080"
            "800041106b2202248080808000200241c8b7c0800036020c200220003602082002"
            "41086a2002410c6a200110ae80808000000b3000024020002802002d00000d0020"
            "01418589c0800041051093808080000f0b2001418a89c080004104109380808000"
            "0b14002001200028020420002802081093808080000b7001037f20002802042101"
            "0240024020002d0000220041044b0d0020004103470d010b200128020021000240"
            "200141046a28020022022802002203450d00200020031182808080008080808000"
            "0b024020022802042202450d00200020021082808080000b2001410c1082808080"
            "000b0bf10101027f23808080800041206b22002480808080000240024002400240"
            "41002d0090dcc080000e0400000301000b410041023a0090dcc0800041002d00e0"
            "dbc080001a4180081099808080002201450d01410041033a0090dcc08000410020"
            "01360280dcc08000410042808080808080013703f8dbc08000410042003703e8db"
            "c08000410041003a0088dcc0800041004100360284dcc08000410041003a00f4db"
            "c08000410041003602f0dbc080000b200041206a2480808080000f0b000b200041"
            "003602182000410136020c200041d8c5c080003602082000420437021020004108"
            "6a41a8c4c0800010a480808000000bb708010a7f23808080800041206b22042480"
            "808080000240024002400240024020012802100d002001417f3602102003410020"
            "03200241036a417c7120026b22056b41077120032005491b22066b210720032006"
            "490d0102402006450d0002400240200220036a2208417f6a22092d0000410a470d"
            "002006417f6a21060c010b200220076a220a2009460d0102402008417e6a22092d"
            "0000410a470d002006417e6a21060c010b200a2009460d0102402008417d6a2209"
            "2d0000410a470d002006417d6a21060c010b200a2009460d0102402008417c6a22"
            "092d0000410a470d002006417c6a21060c010b200a2009460d0102402008417b6a"
            "22092d0000410a470d002006417b6a21060c010b200a2009460d0102402008417a"
            "6a22092d0000410a470d002006417a6a21060c010b200a2009460d010240200841"
            "796a22092d0000410a470d00200641796a21060c010b200a2009460d0120064178"
            "7221060b200620076a41016a21060c040b20052003200320054b1b210b41002006"
            "6b21082002417c6a210c2006417f7320026a210a02400340200a21052008210620"
            "072209200b4d0d01200641786a2108200541786a210a4180828408200220094178"
            "6a22076a280200220d418a94a8d000736b200d724180828408200c20096a280200"
            "220d418a94a8d000736b200d727141808182847871418081828478460d000b0b20"
            "0920034b0d0202400340200320066a450d012006417f6a2106200520036a210920"
            "05417f6a210520092d0000410a470d000b200320066a41016a21060c040b024002"
            "402001411c6a28020022060d00410021060c010b2006200141186a2802006a417f"
            "6a2d0000410a470d0041002106200141003a00202001411c6a41003602000b0240"
            "200128021420066b20034b0d002000200141146a2002200310dd808080000c050b"
            "200128021820066a2002200310fa808080001a200041043a00002001411c6a2006"
            "20036a3602000c040b10b580808000000b2007200341c889c08000108580808000"
            "000b2009200341d889c0800010ad80808000000b0240200320064f0d0020044100"
            "3602182004410136020c200441a8bbc0800036020820044204370210200441086a"
            "41b0bbc0800010a480808000000b02402001411c6a2802002205450d0002400240"
            "200128021420056b20064d0d00200141186a28020020056a2002200610fa808080"
            "001a2001411c6a200520066a22053602000c010b200441086a200141146a200220"
            "0610dd80808000024020042d00084104460d00200020042903083702000c030b20"
            "01411c6a28020021050b2005450d00200141003a00202001411c6a41003602000b"
            "200220066a210502402001280214200320066b22064b0d002000200141146a2005"
            "200610dd808080000c010b200141186a2802002005200610fa808080001a200041"
            "043a00002001411c6a20063602000b2001200128021041016a360210200441206a"
            "2480808080000b7101027f20012802002104024020012802082205450d00200420"
            "056b20034f0d004100210520014100360208200141003a000c0b0240200420034d"
            "0d00200128020420056a2002200310fa808080001a200041043a00002001200520"
            "036a3602080f0b20004204370200200141003a000c0bc90103027f017e027f2380"
            "8080800041106b2203248080808000200341086a20002802082802002001200210"
            "dc80808000024020032d000822024104460d002000280204210420032903082105"
            "0240024020002d0000220141044b0d0020014103470d010b200428020021010240"
            "200441046a28020022062802002207450d00200120071182808080008080808000"
            "0b024020062802042206450d00200120061082808080000b2004410c1082808080"
            "000b200020053702000b200341106a24808080800020024104470b9c0303027f01"
            "7e037f23808080800041106b220224808080800020024100360204024002400240"
            "02402001418001490d002001418010490d012001418080044f0d0220022001413f"
            "71418001723a000620022001410c7641e001723a000420022001410676413f7141"
            "8001723a0005410321010c030b200220013a0004410121010c020b20022001413f"
            "71418001723a00052002200141067641c001723a0004410221010c010b20022001"
            "413f71418001723a00072002200141127641f001723a000420022001410676413f"
            "71418001723a000620022001410c76413f71418001723a0005410421010b200241"
            "086a2000280208280200200241046a200110dc80808000024020022d0008220141"
            "04460d0020002802042103200229030821040240024020002d0000220541044b0d"
            "0020054103470d010b200328020021050240200341046a28020022062802002207"
            "450d002005200711828080800080808080000b024020062802042206450d002005"
            "20061082808080000b2003410c1082808080000b200020043702000b200241106a"
            "24808080800020014104470b1200200041dcb7c08000200110b2808080000b0300"
            "000b0900200041003602000bc30201047f411f21020240200141ffffff074b0d00"
            "2001410620014108766722026b7641017120024101746b413e6a21020b20004200"
            "3702102000200236021c200241027441a8dcc080006a2103024041002802c4dfc0"
            "800041012002742204710d0020032000360200200020033602182000200036020c"
            "20002000360208410041002802c4dfc080002004723602c4dfc080000f0b024002"
            "400240200328020022042802044178712001470d00200421020c010b2001410041"
            "1920024101766b2002411f461b742103034020042003411d764104716a41106a22"
            "052802002202450d02200341017421032002210420022802044178712001470d00"
            "0b0b20022802082203200036020c20022000360208200041003602182000200236"
            "020c200020033602080f0b20052000360200200020043602182000200036020c20"
            "0020003602080b0b00200010e580808000000bb50101037f23808080800041106b"
            "2201248080808000200028020c2102024002400240024020002802040e02000102"
            "0b20020d0141012102410021030c020b20020d0020002802002202280204210320"
            "0228020021020c010b20014180808080783602002001200036020c2001418f8080"
            "8000200028021c22002d001c20002d001d10e680808000000b2001200336020420"
            "0120023602002001419080808000200028021c22002d001c20002d001d10e68080"
            "8000000b990101027f23808080800041106b22042480808080004100410028029c"
            "dcc08000220541016a36029cdcc08000024020054100480d000240024041002d00"
            "f0dfc080000d00410041002802ecdfc0800041016a3602ecdfc080004100280298"
            "dcc08000417f4a0d010c020b200441086a20002001118380808000808080800000"
            "0b410041003a00f0dfc080002002450d0010e180808000000b000b0c0020002001"
            "2902003703000bf726020c7f017e2380808080004190036b220224808080800020"
            "0128020c2103024002400240024002400240024002400240024002400240024002"
            "400240024002400240024002400240024002400240024002400240200128021422"
            "04200128021022054f0d002001410c6a21060340200320046a2d0000220741776a"
            "220841174b0d024101200874419380800471450d022001200441016a2204360214"
            "20052004470d000b200521040b200241f8006a200320052005200441016a220420"
            "052004491b10c78080800041002d00e0dbc080001a200228027c21082002280278"
            "2101411410998080800022040d010c190b200741e5004a0d0820074122460d0620"
            "07412d460d07200741db00470d09200120012d0018417f6a22083a001820044101"
            "6a2104200841ff0171450d0520012004360214200241003602b002200242808080"
            "8080013702a80241082109200420054f0d02200241b8016a41086a210a200241b8"
            "016a410172210b410821094100210c4101210d0340200628020021030240034020"
            "0320046a2d0000220741776a220841174b0d014101200874419380800471450d01"
            "2001200441016a220436021420052004470d000b200521040c040b024002400240"
            "200741dd00460d00200d4101710d02200441016a210402402007412c470d002001"
            "20043602140240200420054f0d000340200320046a2d0000220741776a22084117"
            "4b0d044101200874419380800471450d042001200441016a220436021420052004"
            "470d000b200521040b200241c0006a200320052005200441016a22042005200449"
            "1b10c78080800041002d00e0dbc080001a20022802442104200228024021084114"
            "1099808080002206450d1d2006200836020c20064105360200200620043602100c"
            "080b200241d0006a200320052005200420052004491b10c78080800041002d00e0"
            "dbc080001a200228025421042002280250210841141099808080002206450d1c20"
            "06200836020c20064107360200200620043602100c070b20022902ac02210e2002"
            "2802a802210641042107410021090c070b200741dd00470d00200241c8006a2003"
            "20052005200441016a220420052004491b10c78080800041002d00e0dbc080001a"
            "200228024c21042002280248210841141099808080002206450d1a200620083602"
            "0c20064115360200200620043602100c050b200241b8016a200110e88080800002"
            "4020022d00b80122084106470d0020022802bc0121060c050b200241ec016a4102"
            "6a2205200b41026a2d00003a0000200241d8016a41086a2203200a41086a290300"
            "3703002002200b2f00003b01ec012002200a2903003703d80120022802bc012107"
            "0240200c20022802a802470d00200241a8026a10d3808080000b20022802ac0222"
            "09200c41186c6a220420022903d801370308200420083a0000200420022f01ec01"
            "3b000120042007360204200441106a2003290300370300200441036a20052d0000"
            "3a00002002200c41016a220c3602b0024100210d20012802142204200128021022"
            "054f0d020c000b0b2004200136020c200441053602002000200436020420004106"
            "3a0000200420083602100c160b200628020021030b200241386a20032005200520"
            "0441016a220420052004491b10c78080800041002d00e0dbc080001a200228023c"
            "21042002280238210841141099808080002206450d152006200836020c20064102"
            "360200200620043602100b200241a8026a108c80808000024020022802a8022204"
            "450d002009200441186c1082808080000b200128020c2103200128021421042001"
            "280210210541062107410121090b200120012d001841016a3a0018024002402004"
            "20054f0d0003400240024002400240024002400240200320046a2d00002208410c"
            "4a0d00200841776a4102490d060c010b02402008411f4a0d002008410d470d010c"
            "060b20084120460d052008412c460d01200841dd00460d020b200241186a200320"
            "052005200441016a220420052004491b10c78080800041002d00e0dbc080001a20"
            "0228021c21082002280218210541141099808080002204450d1b20044116360200"
            "0c070b2001200441016a2204360214200420054f0d020340200320046a2d000022"
            "0c41776a220841174b0d024101200874419380800471450d022001200441016a22"
            "0436021420052004470d000b200521040c020b2001200441016a3602142002200e"
            "3703c001200220063602bc01200220073a00b80102402009450d00410621072002"
            "41063a00800120022006360284010c160b20024180016a41106a200241b8016a41"
            "106a29030037030020024180016a41086a200241b8016a41086a29030037030020"
            "0220022903b801220e37038001200ea721070c150b200c41dd00470d0020024130"
            "6a200320052005200441016a220420052004491b10c78080800041002d00e0dbc0"
            "80001a200228023421082002280230210541141099808080002204450d18200441"
            "153602000c040b200241286a200320052005200441016a220420052004491b10c7"
            "8080800041002d00e0dbc080001a200228022c2108200228022821054114109980"
            "8080002204450d17200441163602000c030b2001200441016a2204360214200520"
            "04470d000b200521040b200241206a200320052005200441016a22042005200449"
            "1b10c78080800041002d00e0dbc080001a20022802242108200228022021054114"
            "1099808080002204450d14200441023602000b2004200536020c20042008360210"
            "200220043602d0012002200e3703c001200220063602bc01200220073a00b80102"
            "4020090d0041062107200241063a0080012002200436028401200241b8016a108a"
            "808080000c100b41062107200241063a0080012002200636028401200410e98080"
            "80000c0f0b200241106a200320052005200420052004491b10c78080800041002d"
            "00e0dbc080001a200228021421082002280210210141141099808080002204450d"
            "122004200136020c2004411836020020002004360204200041063a000020042008"
            "3602100c110b200141003602082001200441016a360214200241b8016a20062001"
            "10c88080800020022802bc0121080240024020022802b80122054102460d002002"
            "2802c0012104024020050d0020024180016a2008200410ea8080800020022d0080"
            "014106460d112000200229038001370300200041106a20024180016a41106a2903"
            "00370300200041086a20024180016a41086a2903003703000c130b410021010240"
            "20044100480d00024020040d0041012101410021050c030b41002d00e0dbc08000"
            "1a20042105200410998080800022010d02410121010b20012004109a8080800000"
            "0b200041063a0000200020083602040c110b20024180016a41086a220320012008"
            "200410fa808080003602002002200536028401200241033a008001200220043602"
            "8c01200041106a20024180016a41106a290300370300200041086a200329030037"
            "030020002002290380013703000c100b2001200441016a36021420024198016a20"
            "01410010eb8080800002402002290398014203510d0020024180016a2002419801"
            "6a10c580808000024020022d0080014106460d0020002002290380013703002000"
            "41106a20024180016a41106a290300370300200041086a20024180016a41086a29"
            "03003703000c110b20022802840120011080808080002104200041063a00002000"
            "20043602040c100b200020022802a001360204200041063a00000c0f0b02402007"
            "41f3004a0d00200741e600460d04200741ee00470d012001200441016a36021420"
            "0141d0dbc08000410310ec808080002204450d02200041063a0000200020043602"
            "040c0f0b200741f400460d02200741fb00460d040b200741506a41ff0171410a49"
            "0d04200241086a200320052005200441016a220420052004491b10c78080800041"
            "002d00e0dbc080001a200228020c21082002280208210541141099808080002204"
            "450d0e2004200536020c2004410a3602002004200836021020022004360284010c"
            "0b0b200241003a0080012000200229038001370300200041086a20024180016a41"
            "086a290300370300200041106a20024180016a41106a2903003703000c0c0b2001"
            "200441016a3602140240200141d3dbc08000410310ec808080002204450d002000"
            "41063a0000200020043602040c0c0b20024181023b018001200020022903800137"
            "0300200041086a20024180016a41086a290300370300200041106a20024180016a"
            "41106a2903003703000c0b0b2001200441016a3602140240200141d6dbc0800041"
            "0410ec808080002204450d00200041063a0000200020043602040c0b0b20024101"
            "3b0180012000200229038001370300200041086a20024180016a41086a29030037"
            "0300200041106a20024180016a41106a2903003703000c0a0b200120012d001841"
            "7f6a22083a0018200441016a2104200841ff0171450d0520012004360214200220"
            "013602f001200241013a00f401200241f8016a200241f0016a10ed808080004100"
            "210d410021064100210c024002400240024020022802f80122044180808080786a"
            "0e020200010b20022802fc0121060c060b20022902fc01210e2002410036028c02"
            "20024100360284022002200e3702ac02200220043602a80220024190026a200241"
            "f0016a10ee8080800020022d0090024106460d03200241b8016a20024184026a20"
            "0241a8026a20024190026a10c680808000024020022d00b8014106460d00200241"
            "b8016a108a808080000b200241a8026a41046a2104200241b8016a41046a210802"
            "400340200241ec026a200241f0016a10ed80808000024020022802ec0222054180"
            "808080786a0e020204000b20022902f002210e20022802f0022103200241f8026a"
            "200241f0016a10ee80808000024020022d00f8024106470d0020022802fc022106"
            "2005450d07200320051082808080000c070b200820022903f80237020020084110"
            "6a200241f8026a41106a290300370200200841086a200241f8026a41086a290300"
            "370200200241a8026a41086a200241b8016a41086a290200370300200241a8026a"
            "41106a200241b8016a41106a290200370300200241a8026a41186a200241b8016a"
            "41186a280200360200200220022902b8013703a802200220053602c4022002200e"
            "3e02c8022002200e4220883e02cc02200241d0026a41106a200441106a29020037"
            "0300200241d0026a41086a200441086a290200370300200220042902003703d002"
            "200241b8016a20024184026a200241c4026a200241d0026a10c68080800020022d"
            "00b8014106460d00200241b8016a108a808080000c000b0b200228028402210620"
            "02280288022109200228028c02210c0b410521070c050b20022802f00221060c02"
            "0b200241a8016a2001410110eb80808000024020022903a8014203510d00200241"
            "80016a200241a8016a10c580808000024020022d0080014106460d002000200229"
            "038001370300200041106a20024180016a41106a290300370300200041086a2002"
            "4180016a41086a2903003703000c0a0b2002280284012001108080808000210420"
            "0041063a0000200020043602040c090b200020022802b001360204200041063a00"
            "000c080b20022802940221062004450d00200ea720041082808080000b20024184"
            "026a108b808080000b410621074101210d0b200120012d001841016a3a00182001"
            "28020c21030240024020012802142204200128021022054f0d0003400240024002"
            "4002400240200320046a2d00002208410c4a0d00200841776a4102490d040c010b"
            "02402008411f4a0d002008410d470d010c040b20084120460d032008412c460d01"
            "200841fd00460d020b200241e0006a200320052005200441016a22042005200449"
            "1b10c78080800041002d00e0dbc080001a20022802642108200228026021054114"
            "1099808080002204450d0b200441163602000c050b200241f0006a200320052005"
            "200441016a220420052004491b10c78080800041002d00e0dbc080001a20022802"
            "7421082002280270210541141099808080002204450d0a200441153602000c040b"
            "2001200441016a3602140240200d450d0041062107200241063a00800120022006"
            "360284010c060b200220073a008001200220022f00a8023b0081012002200c3602"
            "8c01200220093602880120022006360284012002200241aa026a2d00003a008301"
            "0c050b2001200441016a220436021420052004470d000b200521040b200241e800"
            "6a200320052005200441016a220420052004491b10c78080800041002d00e0dbc0"
            "80001a200228026c21082002280268210541141099808080002204450d06200441"
            "033602000b2004200536020c20042008360210200220073a00b801200220022f00"
            "a8023b00b901200220043602d0012002200c3602c401200220093602c001200220"
            "063602bc012002200241aa026a2d00003a00bb010240200d0d0041062107200241"
            "063a0080012002200436028401200241b8016a108a808080000c020b4106210720"
            "0241063a0080012002200636028401200410e9808080000c010b200241d8006a20"
            "0320052005200420052004491b10c78080800041002d00e0dbc080001a20022802"
            "5c21082002280258210141141099808080002204450d042004200136020c200441"
            "1836020020002004360204200041063a0000200420083602100c030b200741ff01"
            "714106470d010b20022802840120011080808080002104200041063a0000200020"
            "043602040c010b2000200229038001370300200041106a20024180016a41106a29"
            "0300370300200041086a20024180016a41086a2903003703000b20024190036a24"
            "80808080000f0b000b920101047f02400240024020002802000e020001020b2000"
            "2802082201450d01200028020420011082808080000c010b20002d00044103470d"
            "002000280208220128020021020240200128020422032802002204450d00200220"
            "0411828080800080808080000b024020032802042203450d002002200310828080"
            "80000b2001410c1082808080000b200041141082808080000b7901027f41002103"
            "0240024020024100480d00024020020d0041002103410121040c020b41002d00e0"
            "dbc080001a20022103200210998080800022040d01410121030b20032002109a80"
            "808000000b20042001200210fa8080800021012000200236020c20002001360208"
            "20002003360204200041033a00000b950502067f017e23808080800041306b2203"
            "248080808000200128020c21040240024002400240024002402001280214220520"
            "0128021022064f0d002001200541016a2207360214200420056a2d000022084130"
            "470d020240200720064f0d00200420076a2d000041506a41ff0171410a490d020b"
            "200020012002420010ef808080000c050b200341186a20042006200510c7808080"
            "0041002d00e0dbc080001a200328021c2107200328021821044114109980808000"
            "2201450d022001200436020c200141053602002000200136020820004203370300"
            "200120073602100c040b200341086a200420062006200541026a22012006200149"
            "1b10c78080800041002d00e0dbc080001a200328020c2107200328020821044114"
            "1099808080002201450d012001200436020c2001410d3602002000200136020820"
            "004203370300200120073602100c030b02402008414f6a41ff01714109490d0020"
            "0341106a20042006200710c78080800041002d00e0dbc080001a20032802142107"
            "2003280210210441141099808080002201450d012001200436020c2001410d3602"
            "002000200136020820004203370300200120073602100c030b200841506aad42ff"
            "01832109200720064f0d010340200420076a2d000041506a220541ff0171220841"
            "0a4f0d020240024020094299b3e6cc99b3e6cc19540d0020094299b3e6cc99b3e6"
            "cc19520d01200841054b0d010b2001200741016a22073602142009420a7e2005ad"
            "42ff01837c210920062007470d010c030b0b200341206a20012002200910f08080"
            "80000240024020032802200d00200020032b0328390308420021090c010b200020"
            "03280224360208420321090b200020093703000c020b000b200020012002200910"
            "ef808080000b200341306a2480808080000ba20201087f23808080800041106b22"
            "032480808080002000280214220420002802102205200420054b1b210620002802"
            "0c210702400240024002400340024020020d00410021040c050b20062004460d01"
            "2000200441016a22083602142002417f6a2102200720046a210920012d0000210a"
            "20082104200141016a2101200a20092d0000460d000b200341086a200720052008"
            "10c78080800041002d00e0dbc080001a200328020c210120032802082102411410"
            "99808080002204450d01200441093602000c020b200320072005200610c7808080"
            "0041002d00e0dbc080001a20032802042101200328020021024114109980808000"
            "2204450d00200441053602000c010b000b2004200236020c200420013602100b20"
            "0341106a24808080800020040bae0201047f23808080800041106b220224808080"
            "8000200241046a200110f680808000024002400240024020022d00040d00024020"
            "022d00050d0020004180808080783602000c040b41002103200128020022014100"
            "3602082001200128021441016a360214200241046a2001410c6a200110c8808080"
            "002002280208210420022802044102460d010240200228020c22014100480d0002"
            "4020010d0041012103410021050c040b41002d00e0dbc080001a20012105200110"
            "998080800022030d03410121030b20032001109a80808000000b20002002280208"
            "36020420004181808080783602000c020b20004181808080783602002000200436"
            "02040c010b20032004200110fa8080800021042000200136020820002004360204"
            "200020053602000b200241106a2480808080000bdc0201067f2380808080004110"
            "6b22022480808080002001280200220328020c2104024002400240024002402003"
            "2802142201200341106a28020022054f0d000340200420016a2d0000220641776a"
            "220741174b0d024101200774419380800471450d022003200141016a2201360214"
            "20052001470d000b200521010b200241086a200420052005200141016a22012005"
            "2001491b10c78080800041002d00e0dbc080001a200228020c2107200228020821"
            "0541141099808080002201450d03200141033602000c010b02402006413a470d00"
            "2003200141016a3602142000200310e8808080000c020b20022004200520052001"
            "41016a220120052001491b10c78080800041002d00e0dbc080001a200228020421"
            "072002280200210541141099808080002201450d02200141063602000b20012005"
            "36020c20002001360204200041063a0000200120073602100b200241106a248080"
            "8080000f0b000b970202027f027e23808080800041106b22042480808080000240"
            "02400240024002400240024002402001280214220520012802104f0d0020012802"
            "0c20056a2d00002205412e460d01200541c500460d02200541e500460d020b2002"
            "450d02420121060c050b2004200120022003410010f18080800020042802000d02"
            "0c030b2004200120022003410010f2808080002004280200450d02200020042802"
            "04360208200042033703000c040b420021060240420020037d22074200590d0042"
            "022106200721030c030b2003babd428080808080808080807f8421030c020b2000"
            "2004280204360208200042033703000c020b20042903082103420021060b200020"
            "03370308200020063703000b200441106a2480808080000bbd0101057f41002104"
            "0240024020012802102205200128021422064d0d00200641016a2107200520066b"
            "2108200128020c20066a21054100210403400240200520046a2d0000220641506a"
            "41ff0171410a490d002006412e460d030240200641c500460d00200641e500470d"
            "030b2000200120022003200410f2808080000f0b2001200720046a360214200820"
            "0441016a2204470d000b200821040b2000200120022003200410f3808080000f0b"
            "2000200120022003200410f1808080000bfa0301097f23808080800041106b2205"
            "24808080800020012001280214220641016a220736021402400240024020072001"
            "28021022084f0d00200720086b2109200128020c210a4100210602400240034002"
            "40200a20076a2d0000220b41506a220c41ff0171220d410a490d00024020060d00"
            "2005200a20082008200741016a220720082007491b10c78080800041002d00e0db"
            "c080001a200528020421062005280200210c41141099808080002207450d072007"
            "200c36020c2007410d360200200020073602042000410136020020072006360210"
            "0c060b200620046a2107200b41207241e500470d032000200120022003200710f2"
            "808080000c050b024020034298b3e6cc99b3e6cc19580d0020034299b3e6cc99b3"
            "e6cc19520d02200d41054b0d020b2001200741016a22073602142006417f6a2106"
            "2003420a7e200cad42ff01837c210320072008470d000b200920046a21070c010b"
            "2000200120022003200620046a10f4808080000c020b2000200120022003200710"
            "f3808080000c010b200541086a200128020c20082008200641026a220720082007"
            "491b10c78080800041002d00e0dbc080001a200528020c21062005280208210c41"
            "141099808080002207450d012007200c36020c2007410536020020002007360204"
            "20004101360200200720063602100b200541106a2480808080000f0b000bb80401"
            "077f23808080800041106b22052480808080004101210620012001280214220741"
            "016a220836021402402008200128021022094f0d00410121060240024020012802"
            "0c20086a2d000041556a0e03010200020b410021060b2001200741026a22083602"
            "140b200128020c210a0240024002400240024002400240200820094f0d00200120"
            "0841016a2207360214200a20086a2d000041506a41ff01712208410a4f0d010240"
            "200720094f0d000340200a20076a2d000041506a41ff0171220b410a4f0d012001"
            "200741016a22073602140240200841cb99b3e6004c0d00200841cc99b3e600470d"
            "07200b41074b0d070b2008410a6c200b6a210820092007470d000b0b20060d0220"
            "0420086b2207411f75418080808078732007200841004a2007200448731b21070c"
            "030b200541086a200a2009200810c78080800041002d00e0dbc080001a20052802"
            "0c21012005280208210841141099808080002207450d042007200836020c200741"
            "053602002000200736020420004101360200200720013602100c050b2005200a20"
            "09200710c78080800041002d00e0dbc080001a2005280204210120052802002108"
            "41141099808080002207450d032007200836020c2007410d360200200020073602"
            "0420004101360200200720013602100c040b200420086a2207411f754180808080"
            "7873200720084100482007200448731b21070b2000200120022003200710f38080"
            "80000c020b200020012002200350200610f5808080000c010b000b200541106a24"
            "80808080000b9f0304017f017c017f017c23808080800041106b22052480808080"
            "002003ba2106024002400240024002400240024020042004411f7522077320076b"
            "220741b502490d0003402006440000000000000000610d062004417f4a0d022006"
            "44a0c8eb85f3cce17fa32106200441b4026a22042004411f7522077320076b2207"
            "41b4024b0d000b0b200741037441a8c8c080006a2b030021082004417f4a0d0120"
            "062008a321060c040b2005200128020c2001280210200128021410c78080800041"
            "002d00e0dbc080001a200528020421072005280200210141141099808080002204"
            "450d022004200136020c2004410e36020020002004360204200420073602100c01"
            "0b20062008a222069944000000000000f07f620d02200541086a200128020c2001"
            "280210200128021410c78080800041002d00e0dbc080001a200528020c21072005"
            "280208210141141099808080002204450d012004200136020c2004410e36020020"
            "002004360204200420073602100b410121040c020b000b2000200620069a20021b"
            "390308410021040b20002004360200200541106a2480808080000b7f01047f0240"
            "024020012802142205200128021022064f0d00200128020c210702400340200720"
            "056a2d0000220841506a41ff017141094b0d012001200541016a22053602142006"
            "2005470d000c020b0b200841207241e500460d010b2000200120022003200410f3"
            "808080000f0b2000200120022003200410f2808080000b840201027f2380808080"
            "0041106b220524808080800002400240024002402004450d002003450d010b2001"
            "2802142204200128021022034f0d01200128020c21060340200620046a2d000041"
            "506a41ff0171410a4f0d022001200441016a220436021420032004470d000c020b"
            "0b200541086a200128020c2001280210200128021410c78080800041002d00e0db"
            "c080001a200528020c210120052802082103024041141099808080002204450d00"
            "2004200336020c2004410e3602002000200436020420042001360210410121040c"
            "020b000b200044000000000000000044000000000000008020021b390308410021"
            "040b20002004360200200541106a2480808080000bb40701077f23808080800041"
            "306b22022480808080002001280200220328020c21040240024002400240200328"
            "02142205200341106a28020022064f0d000340200420056a2d0000220741776a22"
            "0841174b0d024101200874419380800471450d022003200541016a220536021420"
            "062005470d000b200621050b41012108200241286a200420062006200541016a22"
            "0520062005491b10c78080800041002d00e0dbc080001a200228022c2106200228"
            "0228210341141099808080002205450d022005200336020c200541033602002000"
            "2005360204200520063602100c010b0240200741fd00470d004100210820004100"
            "3a00010c010b02400240024020012d00040d00200541016a21052007412c470d01"
            "200320053602140240200520064f0d00034002400240024002400240200420056a"
            "2d00002208410c4a0d00200841776a41024f0d010c040b0240200841606a0e0304"
            "0102000b2008410d460d03200841fd00460d020b41012108200241086a20042006"
            "2006200541016a220520062005491b10c78080800041002d00e0dbc080001a2002"
            "28020c21062002280208210341141099808080002205450d092005200336020c20"
            "05411136020020002005360204200520063602100c080b200041013a0001410021"
            "080c070b41012108200241186a200420062006200541016a220520062005491b10"
            "c78080800041002d00e0dbc080001a200228021c21062002280218210341141099"
            "808080002205450d072005200336020c2005411536020020002005360204200520"
            "063602100c060b2003200541016a220536021420062005470d000b200621050b41"
            "012108200241106a200420062006200541016a220520062005491b10c780808000"
            "41002d00e0dbc080001a2002280214210620022802102103411410998080800022"
            "05450d042005200336020c2005410536020020002005360204200520063602100c"
            "030b41002108200141003a0004024020074122460d002002200420062006200541"
            "016a220520062005491b10c78080800041002d00e0dbc080001a20022802042108"
            "2002280200210641141099808080002205450d042005200636020c200541113602"
            "0020002005360204200520083602100c020b200041013a00010c020b200241206a"
            "200420062006200520062005491b10c78080800041002d00e0dbc080001a200228"
            "022421082002280220210641141099808080002205450d022005200636020c2005"
            "410836020020002005360204200520083602100b410121080b200020083a000020"
            "0241306a2480808080000f0b000b4a01037f4100210302402002450d0002400340"
            "20002d0000220420012d00002205470d01200041016a2100200141016a21012002"
            "417f6a2202450d020c000b0b200420056b21030b20030bac0501087f0240024002"
            "400240200020016b20024f0d00200120026a2103200020026a2104024020024110"
            "4f0d00200021050c030b2004417c7121054100200441037122066b210702402006"
            "450d00200120026a417f6a210803402004417f6a220420082d00003a0000200841"
            "7f6a210820052004490d000b0b2005200220066b2209417c7122066b2104024020"
            "0320076a2207410371450d0020064101480d022007410374220841187121022007"
            "417c71220a417c6a2101410020086b4118712103200a280200210803402005417c"
            "6a2205200820037420012802002208200276723602002001417c6a210120042005"
            "490d000c030b0b20064101480d01200920016a417c6a210103402005417c6a2205"
            "20012802003602002001417c6a210120042005490d000c020b0b02400240200241"
            "104f0d00200021040c010b2000410020006b41037122036a210502402003450d00"
            "20002104200121080340200420082d00003a0000200841016a2108200441016a22"
            "042005490d000b0b2005200220036b2209417c7122076a21040240024020012003"
            "6a2206410371450d0020074101480d012006410374220841187121022006417c71"
            "220a41046a2101410020086b4118712103200a2802002108034020052008200276"
            "2001280200220820037472360200200141046a2101200541046a22052004490d00"
            "0c020b0b20074101480d0020062101034020052001280200360200200141046a21"
            "01200541046a22052004490d000b0b20094103712102200620076a21010b200245"
            "0d02200420026a21050340200420012d00003a0000200141016a2101200441016a"
            "22042005490d000c030b0b20094103712201450d012007410020066b6a21032004"
            "20016b21050b2003417f6a210103402004417f6a220420012d00003a0000200141"
            "7f6a210120052004490d000b0b20000b0e0020002001200210f8808080000bc102"
            "01087f02400240200241104f0d00200021030c010b2000410020006b4103712204"
            "6a210502402004450d0020002103200121060340200320062d00003a0000200641"
            "016a2106200341016a22032005490d000b0b2005200220046b2207417c7122086a"
            "210302400240200120046a2209410371450d0020084101480d0120094103742206"
            "41187121022009417c71220a41046a2101410020066b4118712104200a28020021"
            "060340200520062002762001280200220620047472360200200141046a21012005"
            "41046a22052003490d000c020b0b20084101480d00200921010340200520012802"
            "00360200200141046a2101200541046a22052003490d000b0b2007410371210220"
            "0920086a21010b02402002450d00200320026a21050340200320012d00003a0000"
            "200141016a2101200341016a22032005490d000b0b20000b0be45b0100418080c0"
            "000bda5b110000000c000000040000001200000013000000140000000000000000"
            "00000001000000150000000000000001000000010000001600000063616c6c6564"
            "2060526573756c743a3a756e77726170282960206f6e20616e2060457272602076"
            "616c75650017000000040000000400000018000000456d707479496e76616c6964"
            "4469676974506f734f766572666c6f774e65674f766572666c6f775a65726f5061"
            "727365496e744572726f726b696e647372632f6c69622e72730000b00010000a00"
            "0000200000004b000000b00010000a000000210000004b0000004163636f756e74"
            "00b00010000a0000002200000033000000b00010000a0000002300000033000000"
            "44617461b00010000a0000002500000030000000b00010000a0000002600000024"
            "000000b00010000a00000027000000350000007465737420676f6f64203d202c20"
            "636f756e746572203d200a000000380110000c000000440110000c000000500110"
            "0001000000746573742064617461202c20706f696e746572203d202c20706f696e"
            "7465725f7533325f6c656e203d202c206c656e203d2000006c0110000a00000076"
            "0110000c0000008201100014000000960110000800000050011000010000000500"
            "00000c0000000b0000000b00000004000000740010007900100085001000900010"
            "009b0010006361706163697479206f766572666c6f77000000f001100011000000"
            "616c6c6f632f7372632f7261775f7665632e72730c021000140000001800000005"
            "0000002e2e30313233343536373839616263646566426f72726f774d7574457272"
            "6f72616c726561647920626f72726f7765643a200000500210001200000063616c"
            "6c656420604f7074696f6e3a3a756e77726170282960206f6e206120604e6f6e65"
            "602076616c7565696e646578206f7574206f6620626f756e64733a20746865206c"
            "656e20697320206275742074686520696e64657820697320000000970210002000"
            "0000b7021000120000003d3d617373657274696f6e20606c656674202072696768"
            "7460206661696c65640a20206c6566743a200a2072696768743a200000de021000"
            "10000000ee02100017000000050310000900000020726967687460206661696c65"
            "643a200a20206c6566743a20000000de0210001000000028031000100000003803"
            "100009000000050310000900000001000000000000000b21100002000000202020"
            "20207b20207b0a2c0a7d207d636f72652f7372632f666d742f6e756d2e72730000"
            "830310001300000066000000170000003078303030313032303330343035303630"
            "373038303931303131313231333134313531363137313831393230323132323233"
            "323432353236323732383239333033313332333333343335333633373338333934"
            "303431343234333434343534363437343834393530353135323533353435353536"
            "353735383539363036313632363336343635363636373638363937303731373237"
            "333734373537363737373837393830383138323833383438353836383738383839"
            "3930393139323933393439353936393739383939636f72652f7372632f666d742f"
            "6d6f642e727366616c736574727565000072041000130000009b09000026000000"
            "7204100013000000a40900001a000000636f72652f7372632f736c6963652f6d65"
            "6d6368722e7273b004100018000000830000001e000000b0041000180000009f00"
            "00000900000072616e676520737461727420696e64657820206f7574206f662072"
            "616e676520666f7220736c696365206f66206c656e67746820e804100012000000"
            "fa0410002200000072616e676520656e6420696e646578202c05100010000000fa"
            "04100022000000736c69636520696e646578207374617274732061742020627574"
            "20656e647320617420004c05100016000000620510000d00000001010101010101"
            "010101010101010101010101010101010101010101010101010101010101010101"
            "010101010101010101010101010101010101010101010101010101010101010101"
            "010101010101010101010101010101010101010101010101010101010101010101"
            "010101010101010101010101010101010101010101010000000000000000000000"
            "000000000000000000000000000000000000000000000000000000000000000000"
            "000000000000000000000000000000000000000000000202020202020202020202"
            "020202020202020202020202020202020202020303030303030303030303030303"
            "0303040404040400000000000000000000005b2e2e2e5d626567696e203c3d2065"
            "6e642028203c3d2029207768656e20736c6963696e67206060850610000e000000"
            "93061000040000009706100010000000a7061000010000006279746520696e6465"
            "7820206973206e6f742061206368617220626f756e646172793b20697420697320"
            "696e7369646520202862797465732029206f66206000c80610000b000000d30610"
            "0026000000f9061000080000000107100006000000a70610000100000020697320"
            "6f7574206f6620626f756e6473206f6620600000c80610000b0000003007100016"
            "000000a706100001000000636f72652f7372632f7374722f6d6f642e7273006007"
            "100013000000f00000002c000000636f72652f7372632f756e69636f64652f7072"
            "696e7461626c652e7273000000840710001d0000001a0000003600000084071000"
            "1d0000000a0000002b000000000601010301040205070702080809020a050b020e"
            "041001110212051311140115021702190d1c051d081f0124016a046b02af03b102"
            "bc02cf02d102d40cd509d602d702da01e005e102e704e802ee20f004f802fa03fb"
            "010c273b3e4e4f8f9e9e9f7b8b9396a2b2ba86b1060709363d3e56f3d0d1041418"
            "363756577faaaeafbd35e01287898e9e040d0e11122931343a4546494a4e4f6465"
            "5cb6b71b1c07080a0b141736393aa8a9d8d909379091a8070a3b3e66698f92116f"
            "5fbfeeef5a62f4fcff53549a9b2e2f2728559da0a1a3a4a7a8adbabcc4060b0c15"
            "1d3a3f4551a6a7cccda007191a22253e3fe7ecefffc5c604202325262833383a48"
            "4a4c50535556585a5c5e606365666b73787d7f8aa4aaafb0c0d0aeaf6e6fbe935e"
            "227b0503042d036603012f2e80821d03310f1c0424091e052b0544040e2a80aa06"
            "240424042808340b4e43813709160a08183b45390363080930160521031b050140"
            "38044b052f040a070907402027040c0936033a051a07040c07504937330d33072e"
            "080a8126524b2b082a161a261c1417094e042409440d19070a0648082709750b42"
            "3e2a063b050a0651060105100305808b621e48080a80a65e22450b0a060d133a06"
            "0a362c041780b93c64530c48090a46451b4808530d49070a80f6460a1d03474937"
            "030e080a0639070a813619073b031c56010f320d839b66750b80c48a4c630d8430"
            "10168faa8247a1b98239072a045c06260a460a28051382b05b654b043907114005"
            "0b020e97f80884d62a09a2e781330f011d060e0408818c89046b050d0309071092"
            "604709743c80f60a73087015467a140c140c570919808781470385420f1584501f"
            "060680d52b053e2101702d031a040281401f113a050181d02a82e680f7294c040a"
            "04028311444c3d80c23c06010455051b3402810e2c04640c560a80ae381d0d2c04"
            "0907020e06809a83d80411030d0377045f060c04010f0c0438080a062808224e81"
            "540c1d03090736080e040907090780cb250a840600010305050606020706080709"
            "110a1c0b190c1a0d100e0c0f0410031212130916011704180119031a071b011c02"
            "1f1620032b032d0b2e01300431023201a702a902aa04ab08fa02fb05fd02fe03ff"
            "09ad78798b8da23057588b8c901cdd0e0f4b4cfbfc2e2f3f5c5d5fe2848d8e9192"
            "a9b1babbc5c6c9cadee4e5ff00041112293134373a3b3d494a5d848e92a9b1b4ba"
            "bbc6cacecfe4e500040d0e11122931343a3b4546494a5e646584919b9dc9cecf0d"
            "11293a3b4549575b5c5e5f64658d91a9b4babbc5c9dfe4e5f00d11454964658084"
            "b2bcbebfd5d7f0f183858ba4a6bebfc5c7cfdadb4898bdcdc6cecf494e4f57595e"
            "5f898e8fb1b6b7bfc1c6c7d71116175b5cf6f7feff806d71dedf0e1f6e6f1c1d5f"
            "7d7eaeaf7fbbbc16171e1f46474e4f585a5c5e7e7fb5c5d4d5dcf0f1f572738f74"
            "7596262e2fa7afb7bfc7cfd7df9a00409798308f1fd2d4ceff4e4f5a5b07080f10"
            "272feeef6e6f373d3f42459091536775c8c9d0d1d8d9e7feff00205f2282df0482"
            "44081b04061181ac0e80ab051f09811b03190801042f043404070301070607110a"
            "500f1207550703041c0a090308030703020303030c0405030b06010e15054e071b"
            "0757070206170c500443032d03010411060f0c3a041d255f206d046a2580c80582"
            "b0031a0682fd03590716091809140c140c6a060a061a0659072b05460a2c040c04"
            "0103310b2c041a060b0380ac060a062f314d0380a4083c030f033c0738082b0582"
            "ff1118082f112d03210f210f808c048297190b158894052f053b07020e180980be"
            "22740c80d61a81100580df0bf29e033709815c1480b80880cb050a183b030a0638"
            "0846080c06740b1e035a0459098083181c0a16094c04808a06aba40c170431a104"
            "81da26070c050580a61081f50701202a064c04808d0480be031b030f0d636f7265"
            "2f7372632f756e69636f64652f756e69636f64655f646174612e7273003f0d1000"
            "2000000050000000280000003f0d1000200000005c000000160000000003000083"
            "042000910560005d13a0001217201f0c20601fef2ca02b2a30202c6fa6e02c02a8"
            "602d1efb602e00fe20369eff6036fd01e136010a2137240de137ab0e61392f18a1"
            "39301c6148f31ea14c40346150f06aa1514f6f21529dbca15200cf615365d1a153"
            "00da215400e0e155aee26157ece42159d0e8a1592000ee59f0017f5a0070000700"
            "2d0101010201020101480b30151001650702060202010423011e1b5b0b3a090901"
            "18040109010301052b033c082a180120370101010408040103070a021d013a0101"
            "010204080109010a021a010202390104020402020303011e0203010b0239010405"
            "010204011402160601013a0101020104080107030a021e013b0101010c01090128"
            "010301370101030503010407020b021d013a01020102010301050207020b021c02"
            "390201010204080109010a021d0148010401020301010801510102070c08620102"
            "090b0749021b0101010101370e01050102050b0124090166040106010202021902"
            "040310040d01020206010f01000300031d021e021e02400201070801020b09012d"
            "030101750222017603040209010603db0202013a010107010101010208060a0201"
            "301f310430070101050128090c0220040202010338010102030101033a08020298"
            "03010d0107040106010302c6400001c32100038d016020000669020004010a2002"
            "50020001030104011902050197021a120d012608190b2e03300102040202270143"
            "06020202020c0108012f01330101030202050201012a020801ee01020104010001"
            "0010101000020001e201950500030102050428030401a502000400025003460b31"
            "047b01360f290102020a033104020207013d03240501083e010c0234090a040201"
            "5f0302010102060102019d010308150239020101010116010e070305c308020301"
            "011701510102060101020101020102eb010204060201021b025508020101026a01"
            "01010206010165030204010500090102f5010a0201010401900402020401200a28"
            "0602040801090602032e0d010200070106010152160207010201027a0603010102"
            "0107010148020301010100020b023405050101010001060f00053b0700013f0451"
            "010002002e0217000101030405080802071e0494030037043208010e011605010f"
            "000701110207010201056401a00700013d04000400076d07006080f0002f727573"
            "74632f633266373463336639323861656235303366313562346539656635373738"
            "653737663330353862382f6c6962726172792f616c6c6f632f7372632f636f6c6c"
            "656374696f6e732f62747265652f6d61702f656e7472792e727300db1010006000"
            "000071010000360000002f72757374632f63326637346333663932386165623530"
            "3366313562346539656635373738653737663330353862382f6c6962726172792f"
            "616c6c6f632f7372632f636f6c6c656374696f6e732f62747265652f6e6f64652e"
            "7273617373657274696f6e206661696c65643a20656467652e686569676874203d"
            "3d2073656c662e686569676874202d2031004c1110005b000000af020000090000"
            "00617373657274696f6e206661696c65643a207372632e6c656e2829203d3d2064"
            "73742e6c656e28294c1110005b0000002f070000050000004c1110005b000000af"
            "040000230000004c1110005b000000ef04000024000000617373657274696f6e20"
            "6661696c65643a20656467652e686569676874203d3d2073656c662e6e6f64652e"
            "686569676874202d20310000004c1110005b000000f003000009000000c8151000"
            "5f0000005802000030000000110000000c00000004000000120000001300000014"
            "000000000000000000000001000000150000006120446973706c617920696d706c"
            "656d656e746174696f6e2072657475726e656420616e206572726f7220756e6578"
            "7065637465646c792f72757374632f633266373463336639323861656235303366"
            "313562346539656635373738653737663330353862382f6c6962726172792f616c"
            "6c6f632f7372632f737472696e672e72730000f71210004b000000060a00000e00"
            "00004572726f72454f46207768696c652070617273696e672061206c697374454f"
            "46207768696c652070617273696e6720616e206f626a656374454f46207768696c"
            "652070617273696e67206120737472696e67454f46207768696c65207061727369"
            "6e6720612076616c7565657870656374656420603a60657870656374656420602c"
            "60206f7220605d60657870656374656420602c60206f7220607d60657870656374"
            "6564206964656e7465787065637465642076616c75656578706563746564206022"
            "60696e76616c696420657363617065696e76616c6964206e756d6265726e756d62"
            "6572206f7574206f662072616e6765696e76616c696420756e69636f646520636f"
            "646520706f696e74636f6e74726f6c2063686172616374657220285c7530303030"
            "2d5c75303031462920666f756e64207768696c652070617273696e672061207374"
            "72696e676b6579206d757374206265206120737472696e67696e76616c69642076"
            "616c75653a206578706563746564206b657920746f2062652061206e756d626572"
            "20696e2071756f746573666c6f6174206b6579206d7573742062652066696e6974"
            "652028676f74204e614e206f72202b2f2d696e66296c6f6e65206c656164696e67"
            "20737572726f6761746520696e2068657820657363617065747261696c696e6720"
            "636f6d6d61747261696c696e672063686172616374657273756e65787065637465"
            "6420656e64206f662068657820657363617065726563757273696f6e206c696d69"
            "742065786365656465644572726f72282c206c696e653a202c20636f6c756d6e3a"
            "200000008d1510000600000093151000080000009b1510000a000000b820100001"
            "0000002f72757374632f6332663734633366393238616562353033663135623465"
            "39656635373738653737663330353862382f6c6962726172792f616c6c6f632f73"
            "72632f636f6c6c656374696f6e732f62747265652f6e617669676174652e727300"
            "c81510005f000000c600000027000000c81510005f000000160200002f000000c8"
            "1510005f000000a1000000240000002f686f6d652f7077616e672f2e636172676f"
            "2f72656769737472792f7372632f696e6465782e6372617465732e696f2d366631"
            "376432326262613135303031662f73657264655f6a736f6e2d312e302e3133352f"
            "7372632f726561642e727300581610005f000000a001000045000000581610005f"
            "000000a50100003d000000581610005f000000ad0100001a000000581610005f00"
            "0000fa01000013000000581610005f000000030200003e000000581610005f0000"
            "00ff01000033000000581610005f000000090200003a000000581610005f000000"
            "6802000019000000ffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffff0000010002000300040005000600070008000900ffffffffffffffff"
            "ffffffffffff0a000b000c000d000e000f00ffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffff0a000b000c000d000e000f00ffffffffffffffffffffffffffffffffff"
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
            "ffffffffffffffffffffffffffffffffffffffffffff0000100020003000400050"
            "006000700080009000ffffffffffffffffffffffffffffa000b000c000d000e000"
            "f000ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffa000b000c000d000e000f000"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffffff"
            "ffffffffffffffffff110000000c00000004000000120000001300000014000000"
            "7265656e7472616e7420696e69740000501b10000e0000002f72757374632f6332"
            "663734633366393238616562353033663135623465396566353737386537376633"
            "30353862382f6c6962726172792f636f72652f7372632f63656c6c2f6f6e63652e"
            "7273000000681b10004d0000002301000042000000000000000000000004000000"
            "04000000190000001a0000000c000000040000001b0000001c0000001d0000002f"
            "727573742f646570732f646c6d616c6c6f632d302e322e362f7372632f646c6d61"
            "6c6c6f632e7273617373657274696f6e206661696c65643a207073697a65203e3d"
            "2073697a65202b206d696e5f6f7665726865616400f41b100029000000a8040000"
            "09000000617373657274696f6e206661696c65643a207073697a65203c3d207369"
            "7a65202b206d61785f6f766572686561640000f41b100029000000ae0400000d00"
            "0000757365206f66207374643a3a7468726561643a3a63757272656e7428292069"
            "73206e6f7420706f737369626c6520616674657220746865207468726561642773"
            "206c6f63616c206461746120686173206265656e2064657374726f796564737464"
            "2f7372632f7468726561642f6d6f642e727300fa1c100015000000f10200001300"
            "00006661696c656420746f2067656e657261746520756e69717565207468726561"
            "642049443a2062697473706163652065786861757374656400201d100037000000"
            "fa1c100015000000c40400000d00000001000000000000007374642f7372632f69"
            "6f2f62756666657265642f6c696e657772697465727368696d2e72736d6964203e"
            "206c656e00009d1d100009000000781d1000250000000f01000029000000656e74"
            "697479206e6f7420666f756e647065726d697373696f6e2064656e696564636f6e"
            "6e656374696f6e2072656675736564636f6e6e656374696f6e207265736574686f"
            "737420756e726561636861626c656e6574776f726b20756e726561636861626c65"
            "636f6e6e656374696f6e2061626f727465646e6f7420636f6e6e65637465646164"
            "647265737320696e2075736561646472657373206e6f7420617661696c61626c65"
            "6e6574776f726b20646f776e62726f6b656e2070697065656e7469747920616c72"
            "65616479206578697374736f7065726174696f6e20776f756c6420626c6f636b6e"
            "6f742061206469726563746f727969732061206469726563746f72796469726563"
            "746f7279206e6f7420656d707479726561642d6f6e6c792066696c657379737465"
            "6d206f722073746f72616765206d656469756d66696c6573797374656d206c6f6f"
            "70206f7220696e646972656374696f6e206c696d69742028652e672e2073796d6c"
            "696e6b206c6f6f70297374616c65206e6574776f726b2066696c652068616e646c"
            "65696e76616c696420696e70757420706172616d65746572696e76616c69642064"
            "61746174696d6564206f75747772697465207a65726f6e6f2073746f7261676520"
            "73706163657365656b206f6e20756e7365656b61626c652066696c6566696c6573"
            "797374656d2071756f746120657863656564656466696c6520746f6f206c617267"
            "657265736f75726365206275737965786563757461626c652066696c6520627573"
            "79646561646c6f636b63726f73732d646576696365206c696e6b206f722072656e"
            "616d65746f6f206d616e79206c696e6b73696e76616c69642066696c656e616d65"
            "617267756d656e74206c69737420746f6f206c6f6e676f7065726174696f6e2069"
            "6e746572727570746564756e737570706f72746564756e65787065637465642065"
            "6e64206f662066696c656f7574206f66206d656d6f72796f74686572206572726f"
            "72756e63617465676f72697a6564206572726f7220286f73206572726f72202900"
            "00000100000000000000ad2010000b000000b8201000010000007374642f737263"
            "2f696f2f737464696f2e727300d4201000130000002c030000140000006661696c"
            "6564207072696e74696e6720746f203a20000000f8201000130000000b21100002"
            "000000d4201000130000005d040000090000007374646f75747374642f7372632f"
            "696f2f6d6f642e72736120666f726d617474696e6720747261697420696d706c65"
            "6d656e746174696f6e2072657475726e656420616e206572726f72207768656e20"
            "74686520756e6465726c79696e672073747265616d20646964206e6f7400000047"
            "211000560000003621100011000000280700001500000063616e6e6f7420726563"
            "7572736976656c792061637175697265206d75746578b821100020000000737464"
            "2f7372632f7379732f73796e632f6d757465782f6e6f5f746872656164732e7273"
            "e02110002400000014000000090000007374642f7372632f73796e632f6f6e6365"
            "2e72731422100014000000d9000000140000006c6f636b20636f756e74206f7665"
            "72666c6f7720696e207265656e7472616e74206d757465787374642f7372632f73"
            "796e632f7265656e7472616e745f6c6f636b2e72735e2210001e00000022010000"
            "2d0000006f7065726174696f6e207375636365737366756c6f6e652d74696d6520"
            "696e697469616c697a6174696f6e206d6179206e6f7420626520706572666f726d"
            "6564207265637572736976656c79a0221000380000001000000011000000120000"
            "00100000001000000013000000120000000d0000000e000000150000000c000000"
            "0b00000015000000150000000f0000000e00000013000000260000003800000019"
            "000000170000000c000000090000000a0000001000000017000000190000000e00"
            "00000d00000014000000080000001b0000000e0000001000000016000000150000"
            "000b000000160000000d0000000b00000013000000c01d1000d01d1000e11d1000"
            "f31d1000031e1000131e1000261e1000381e1000451e1000531e1000681e100074"
            "1e10007f1e1000941e1000a91e1000b81e1000c61e1000d91e1000ff1e1000371f"
            "1000501f1000671f1000731f10007c1f1000861f1000961f1000ad1f1000c61f10"
            "00d41f1000e11f1000f51f1000fd1f10001820100026201000362010004c201000"
            "612010006c201000822010008f2010009a201000000000000000f03f0000000000"
            "00244000000000000059400000000000408f40000000000088c34000000000006a"
            "f8400000000080842e4100000000d01263410000000084d797410000000065cdcd"
            "41000000205fa00242000000e876483742000000a2941a6d42000040e59c30a242"
            "0000901ec4bcd64200003426f56b0c430080e03779c3414300a0d8855734764300"
            "c84e676dc1ab43003d9160e458e143408cb5781daf154450efe2d6e41a4b4492d5"
            "4d06cff08044f64ae1c7022db544b49dd9794378ea449102282c2a8b2045350332"
            "b7f4ad54450284fee471d9894581121f2fe727c04521d7e6fae031f445ea8ca039"
            "593e294624b00888ef8d5f46176e05b5b5b893469cc94622e3a6c846037cd8ea9b"
            "d0fe46824dc77261423347e32079cff91268471b695743b8179e47b1a1162ad3ce"
            "d2471d4a9cf487820748a55cc3f129633d48e7191a37fa5d724861a0e0c478f5a6"
            "4879c818f6d6b2dc484c7dcf59c6ef11499e5c43f0b76b4649c63354eca5067c49"
            "5ca0b4b32784b14973c8a1a031e5e5498f3aca087e5e1b4a9a647ec50e1b514ac0"
            "fddd76d261854a307d951447baba4a3e6edd6c6cb4f04acec9148887e1244b41fc"
            "196ae9195a4ba93d50e23150904b134de45a3e64c44b57609df14d7df94b6db804"
            "6ea1dc2f4c44f3c2e4e4e9634c15b0f31d5ee4984c1b9c70a5751dcf4c91616687"
            "6972034df5f93fe9034f384d72f88fe3c4626e4d47fb390ebbfda24d197ac8d129"
            "bdd74d9f983a4674ac0d4e649fe4abc88b424e3dc7ddd6ba2e774e0c39958c69fa"
            "ac4ea743ddf7811ce24e9194d475a2a3164fb5b949138b4c4c4f11140eecd6af81"
            "4f169911a7cc1bb64f5bffd5d0bfa2eb4f99bf85e2b74521507f2f27db25975550"
            "5ffbf051effc8a501b9d369315dec050624404f89a15f5507b5505b6015b2a516d"
            "55c311e1786051c82a3456199794517a35c1abdfbcc9516cc158cb0b160052c7f1"
            "2ebe8e1b345239aeba6d72226952c75929090f6b9f521dd8b965e9a2d352244e28"
            "bfa38b0853ad61f2ae8cae3e530c7d57ed172d73534f5cade85df8a75363b3d862"
            "75f6dd531e70c75d09ba1254254c39b58b6847542e9f87a2ae427d547dc39425ad"
            "49b2545cf4f96e18dce6547371b88a1e931c55e846b316f3db5155a21860dcef52"
            "8655ca1e78d3abe7bb553f132b64cb70f1550ed8353dfecc2556124e83cc3d405b"
            "56cb10d29f26089156fe94c647304ac5563d3ab859bc9cfa56662413b8f5a13057"
            "80ed172673ca6457e0e89def0ffd99578cb1c2f5293ed057ef5d3373b44d04586b"
            "35009021613958c54200f469b96f58bb298038e2d3a3582a34a0c6dac8d8583541"
            "487811fb0e59c1282debea5c4359f172f8a525347859ad8f760f2f41ae59cc19aa"
            "69bde8e2593fa014c4eca2175a4fc819f5a78b4d5a321d30f94877825a7e247c37"
            "1b15b75a9e2d5b0562daec5a82fc58437d08225ba33b2f949c8a565b8c0a3bb943"
            "2d8c5b97e6c4534a9cc15b3d20b6e85c03f65b4da8e32234842b5c3049ce95a032"
            "615c7cdb41bb487f955c5b5212ea1adfca5c79734bd270cb005d5750de064dfe34"
            "5d6de49548e03d6a5dc4ae5d2dac66a05d751ab5385780d45d1261e2066da0095e"
            "ab7c4d244404405ed6db602d5505745ecc12b978aa06a95e7f57e7165548df5eaf"
            "96502e358d135f5bbce4798270485f72eb5d18a38c7e5f27b33aefe517b35ff15f"
            "096bdfdde75fedb7cb4557d51d60f4529f8b56a55260b127872eac4e87609df128"
            "3a5722bd60029759847635f260c3fc6f25d4c22661f4fbcb2e89735c61787d3fbd"
            "35c89161d65c8f2c433ac6610c34b3f7d3c8fb618700d07a845d3162a9008499e5"
            "b46562d400e5ff1e229b628420ef5f53f5d062a5e8ea37a8320563cfa2e545527f"
            "3a63c185af6b938f706332679b4678b3a463fe40425856e0d9639f6829f7352c10"
            "64c6c2f3744337446478b330521445796456e0bc665996af64360c36e0f7bde364"
            "438f43d875ad18651473544ed3d84e65ecc7f41084478365e8f931156519b86561"
            "787e5abe1fee653d0b8ff8d6d322660cceb2b6cc8857668f815fe4ff6a8d66f9b0"
            "bbeedf62c266389d6aea97fbf666864405e57dba2c67d44a23af8ef46167891dec"
            "5ab2719667eb24a7f11e0ecc6713770857d3880168d794ca2c08eb35680d3afd37"
            "ca656b684844fe629e1fa1685ad5bdfb8567d568b14aad7a67c10a69af4eacace0"
            "b840695a62d7d718e77469f13acd0ddf20aa69d644a0688b54e0690c56c842ae69"
            "146a8f6b7ad31984496a7306594820e57f6a08a4372d34efb36a0a8d853801ebe8"
            "6a4cf0a686c1251f6b305628f49877536bbb6b32317f55886baa067ffdde6abe6b"
            "2a646f5ecb02f36b353d0b367ec3276c820c8ec35db45d6cd1c7389aba90926cc6"
            "f9c640e934c76c37b8f8902302fd6c23739b3a5621326deb4f42c9aba9666de6e3"
            "92bb16549c6d70ce3b358eb4d16d0cc28ac2b121066e8f722d331eaa3b6e9967fc"
            "df524a716e7f81fb97e79ca56edf61fa7d2104db6e2c7dbcee94e2106f769c6b2a"
            "3a1b456f948306b508627a6f3d122471457db06fcc166dcd969ce46f7f5cc880bc"
            "c31970cf397dd0551a507043889c44eb20847054aac3152629b970e994349b6f73"
            "ef7011dd00c125a82371561441312f9258716b5991fdbab68e71e3d77ade3432c3"
            "71dc8d1916c2fef77153f19f9b72fe2d72d4f643a107bf627289f49489c96e9772"
            "ab31faeb7b4acd720b5f7c738d4e0273cd765bd030e2367381547204bd9a6c73d0"
            "74c722b6e0a173045279abe358d67386a657961cef0b7414c8f6dd71754174187a"
            "7455ced275749e98d1ea8147ab7463ffc232b10ce1743cbf737fdd4f15750baf50"
            "dfd4a34a75676d920b65a68075c008774efecfb475f1ca14e2fd03ea75d6fe4cad"
            "7e4220768c3ea0581e5354762f4ec8eee5678976bb617a6adfc1bf76157d8ca22b"
            "d9f3765a9c2f8b76cf28777083fb2d54035f772632bd9c14629377b07eecc3993a"
            "c8775c9ee7344049fe77f9c21021c8ed3278b8f354293aa96778a530aab388939d"
            "78675e4a70357cd27801f65ccc421b07798233747f13e23c7931a0a82f4c0d7279"
            "3dc8923b9f90a6794d7a770ac734dc7970ac8a66fca0117a8c572d803b09467a6f"
            "ad38608a8b7b7a656c237c3637b17a7f472c1b0485e57a5e59f72145e61a7bdb97"
            "3a35ebcf507bd23d8902e603857b468d2b83df44ba7b4c38fbb10b6bf07b5f067a"
            "9ece85247cf687184642a7597cfa54cf6b8908907c382ac3c6ab0ac47cc7f473b8"
            "560df97cf8f19066ac502f7d3b971ac06b92637d0a3d21b00677987d4c8c295cc8"
            "94ce7db0f79939fd1c037e9c7500883ce4377e039300aa4bdd6d7ee25b404a4faa"
            "a27eda72d01ce354d77e908f04e41b2a0d7fbad9826e513a427f299023cae5c876"
            "7f3374ac3c1f7bac7fa0c8eb85f3cce17f756c6c727565616c736500c04a046e61"
            "6d65000e0d7761736d5f6c69622e7761736d01884a7b003d5f5a4e313073657264"
            "655f6a736f6e356572726f72354572726f7231326669785f706f736974696f6e31"
            "3768386631666565323432343761346639634501435f5a4e313073657264655f6a"
            "736f6e3264653231446573657269616c697a6572244c5424522447542435657272"
            "6f723137683438663764306565626231623836343145020e5f5f727573745f6465"
            "616c6c6f63035b5f5a4e34636f726533666d74336e756d34395f244c5424696d70"
            "6c2475323024636f72652e2e666d742e2e44656275672475323024666f72247532"
            "302475382447542433666d74313768346234323333323664643863616231354504"
            "305f5a4e34636f726533666d74336e756d33696d7037666d745f75363431376864"
            "3532316661366566366130363732614505445f5a4e34636f726535736c69636535"
            "696e6465783236736c6963655f73746172745f696e6465785f6c656e5f6661696c"
            "313768663931613361666538376231643434334506385f5a4e34636f726533666d"
            "7439466f726d617474657231327061645f696e74656772616c3137686334656130"
            "376130626331333536633445075c5f5a4e34636f726533666d74336e756d35305f"
            "244c5424696d706c2475323024636f72652e2e666d742e2e446562756724753230"
            "24666f7224753230247533322447542433666d7431376835353339386231363535"
            "30643532376545084c5f5a4e34636f726533707472343264726f705f696e5f706c"
            "616365244c5424616c6c6f632e2e737472696e672e2e537472696e672447542431"
            "37683738323934613239653363373833306445094f5f5a4e34636f726533707472"
            "343564726f705f696e5f706c616365244c542473657264655f6a736f6e2e2e6572"
            "726f722e2e4572726f722447542431376866383763386436646339616234626335"
            "450a4f5f5a4e34636f726533707472343564726f705f696e5f706c616365244c54"
            "2473657264655f6a736f6e2e2e76616c75652e2e56616c75652447542431376835"
            "333262653330333764613162376564450b81015f5a4e39395f244c5424616c6c6f"
            "632e2e636f6c6c656374696f6e732e2e62747265652e2e6d61702e2e4254726565"
            "4d6170244c54244b24432456244324412447542424753230246173247532302463"
            "6f72652e2e6f70732e2e64726f702e2e44726f70244754243464726f7031376835"
            "346633306630323133646334313362450c645f5a4e37305f244c5424616c6c6f63"
            "2e2e7665632e2e566563244c542454244324412447542424753230246173247532"
            "3024636f72652e2e6f70732e2e64726f702e2e44726f70244754243464726f7031"
            "376864313538343863353832316334666665450d525f5a4e35335f244c5424636f"
            "72652e2e666d742e2e4572726f72247532302461732475323024636f72652e2e66"
            "6d742e2e44656275672447542433666d7431376866376165323835356232343964"
            "626335450e5f5f5a4e35385f244c5424616c6c6f632e2e737472696e672e2e5374"
            "72696e67247532302461732475323024636f72652e2e666d742e2e577269746524"
            "475424313077726974655f63686172313768323134333931636238656231353263"
            "36450f435f5a4e35616c6c6f63377261775f7665633139526177566563244c5424"
            "5424432441244754243867726f775f6f6e65313768363666383634616630346265"
            "6432623245105a5f5a4e35616c6c6f63377261775f766563323052617756656349"
            "6e6e6572244c5424412447542437726573657276653231646f5f72657365727665"
            "5f616e645f68616e646c653137683766656665376563326164336435616245115d"
            "5f5a4e35385f244c5424616c6c6f632e2e737472696e672e2e537472696e672475"
            "32302461732475323024636f72652e2e666d742e2e577269746524475424397772"
            "6974655f737472313768353939643965353738393436646439384512595f5a4e36"
            "305f244c5424616c6c6f632e2e737472696e672e2e537472696e67247532302461"
            "732475323024636f72652e2e666d742e2e446973706c61792447542433666d7431"
            "37686365343232366161316637323663316345132e5f5a4e34636f726533666d74"
            "39466f726d61747465723370616431376834373639616533383933373463633531"
            "45145d5f5a4e36355f244c542473657264655f6a736f6e2e2e76616c75652e2e56"
            "616c7565247532302461732475323024636f72652e2e636d702e2e506172746961"
            "6c4571244754243265713137683162323138393234373831393663383045158b01"
            "5f5a4e3130385f244c5424616c6c6f632e2e636f6c6c656374696f6e732e2e6274"
            "7265652e2e6d61702e2e49746572244c54244b2443245624475424247532302461"
            "732475323024636f72652e2e697465722e2e7472616974732e2e6974657261746f"
            "722e2e4974657261746f7224475424346e65787431376835363664323036316535"
            "6139376461644516615f5a4e36385f244c5424636f72652e2e6e756d2e2e657272"
            "6f722e2e5061727365496e744572726f72247532302461732475323024636f7265"
            "2e2e666d742e2e44656275672447542433666d7431376863383736363338616561"
            "6230633031664517675f5a4e36385f244c5424636f72652e2e666d742e2e627569"
            "6c646572732e2e50616441646170746572247532302461732475323024636f7265"
            "2e2e666d742e2e5772697465244754243977726974655f73747231376838313862"
            "343965376536396132366664451808616c6c6f6361746519435f5a4e38646c6d61"
            "6c6c6f6338646c6d616c6c6f633137446c6d616c6c6f63244c5424412447542436"
            "6d616c6c6f6331376865363539333961346338393763633135451a335f5a4e3561"
            "6c6c6f63377261775f766563313268616e646c655f6572726f7231376839376237"
            "646264306637326464373838451b0a6465616c6c6f636174651c11636f6d706172"
            "655f6163636f756e7449441d325f5a4e313073657264655f6a736f6e3264653130"
            "66726f6d5f736c69636531376831316365303837373634633961376230451e5c5f"
            "5a4e35355f244c542473747224753230246173247532302473657264655f6a736f"
            "6e2e2e76616c75652e2e696e6465782e2e496e646578244754243130696e646578"
            "5f696e746f31376864333238633634636161396431376163451f325f5a4e34636f"
            "726536726573756c743133756e777261705f6661696c6564313768663839396364"
            "303037373637303035314520325f5a4e34636f7265366f7074696f6e3133756e77"
            "7261705f6661696c6564313768333535313964653938613737363134664521625f"
            "5a4e34636f726533666d74336e756d33696d7035325f244c5424696d706c247532"
            "3024636f72652e2e666d742e2e446973706c61792475323024666f722475323024"
            "7533322447542433666d743137686266336530323238343833653337356145222b"
            "5f5a4e3373746432696f35737464696f365f7072696e7431376838316334373231"
            "3636303436663066634523385f5a4e35616c6c6f63377261775f76656331376361"
            "7061636974795f6f766572666c6f77313768343939643438326139656435373135"
            "614524305f5a4e34636f72653970616e69636b696e673970616e69635f666d7431"
            "3768363534306363623264356664633361624525415f5a4e38646c6d616c6c6f63"
            "38646c6d616c6c6f633137446c6d616c6c6f63244c542441244754243466726565"
            "3137683339383334616161616533653839343645262c5f5a4e34636f7265397061"
            "6e69636b696e673570616e69633137683034656562393137646439336332323945"
            "270e5f5f727573745f7265616c6c6f63284a5f5a4e38646c6d616c6c6f6338646c"
            "6d616c6c6f633137446c6d616c6c6f63244c542441244754243132756e6c696e6b"
            "5f6368756e6b3137683933346533646333383362623538613345294b5f5a4e3864"
            "6c6d616c6c6f6338646c6d616c6c6f633137446c6d616c6c6f63244c5424412447"
            "54243133646973706f73655f6368756e6b31376836653063636364343538363537"
            "343633452a3a5f5a4e34636f72653970616e69636b696e67313870616e69635f62"
            "6f756e64735f636865636b31376833643662386161346338303439363632452b11"
            "727573745f626567696e5f756e77696e642c465f5a4e34636f726533666d743946"
            "6f726d617474657231327061645f696e74656772616c313277726974655f707265"
            "66697831376861396134333238306236303036643132452d425f5a4e34636f7265"
            "35736c69636535696e6465783234736c6963655f656e645f696e6465785f6c656e"
            "5f6661696c31376830383862353665323939626561616166452e3b5f5a4e34636f"
            "72653970616e69636b696e6731396173736572745f6661696c65645f696e6e6572"
            "31376836663765333235376438346135303432452f475f5a4e34325f244c542424"
            "52462454247532302461732475323024636f72652e2e666d742e2e446562756724"
            "47542433666d74313768336136626161316262343761643230344530495f5a4e34"
            "345f244c54242452462454247532302461732475323024636f72652e2e666d742e"
            "2e446973706c61792447542433666d743137683766663464306238363039633234"
            "37324531585f5a4e35395f244c5424636f72652e2e666d742e2e417267756d656e"
            "7473247532302461732475323024636f72652e2e666d742e2e446973706c617924"
            "47542433666d74313768363861336538653530396361666336344532265f5a4e34"
            "636f726533666d7435777269746531376839333535346534626537316632633761"
            "45335f5f5a4e34636f726533666d74336e756d35305f244c5424696d706c247532"
            "3024636f72652e2e666d742e2e44656275672475323024666f7224753230247533"
            "322447542433666d7431376835353339386231363535306435323765452e323534"
            "5c5f5a4e36335f244c5424636f72652e2e63656c6c2e2e426f72726f774d757445"
            "72726f72247532302461732475323024636f72652e2e666d742e2e446562756724"
            "47542433666d74313768313564336433343334626464636363384535395f5a4e34"
            "636f72653463656c6c323270616e69635f616c72656164795f626f72726f776564"
            "313768333134623532613162633436626665344536405f5a4e34636f726535736c"
            "69636535696e6465783232736c6963655f696e6465785f6f726465725f6661696c"
            "313768353862336536383666653333373030654537325f5a4e34636f7265366f70"
            "74696f6e31336578706563745f6661696c65643137686630386139396532643733"
            "33366336614538535f5a4e34636f72653463686172376d6574686f647332325f24"
            "4c5424696d706c2475323024636861722447542431366573636170655f64656275"
            "675f657874313768656366613566303431373437393039384539345f5a4e34636f"
            "726537756e69636f6465397072696e7461626c6535636865636b31376836646136"
            "346638306663313630633761453a325f5a4e34636f7265337374723136736c6963"
            "655f6572726f725f6661696c31376862303364323439386438646362363433453b"
            "355f5a4e34636f7265337374723139736c6963655f6572726f725f6661696c5f72"
            "7431376832616462643139306563313832373933453c645f5a4e37315f244c5424"
            "636f72652e2e6f70732e2e72616e67652e2e52616e6765244c5424496478244754"
            "24247532302461732475323024636f72652e2e666d742e2e446562756724475424"
            "33666d7431376836636632383632303536616535653233453d465f5a4e34315f24"
            "4c542463686172247532302461732475323024636f72652e2e666d742e2e446562"
            "75672447542433666d7431376865613566643964626339343936626665453e625f"
            "5a4e34636f726533666d74336e756d33696d7035325f244c5424696d706c247532"
            "3024636f72652e2e666d742e2e446973706c61792475323024666f722475323024"
            "6933322447542433666d7431376863656439306337613633396330316464453fce"
            "015f5a4e35616c6c6f633131636f6c6c656374696f6e73356274726565346e6f64"
            "653132374e6f6465526566244c5424616c6c6f632e2e636f6c6c656374696f6e73"
            "2e2e62747265652e2e6e6f64652e2e6d61726b65722e2e4479696e672443244b24"
            "432456244324616c6c6f632e2e636f6c6c656374696f6e732e2e62747265652e2e"
            "6e6f64652e2e6d61726b65722e2e4c6561664f72496e7465726e616c2447542432"
            "316465616c6c6f636174655f616e645f617363656e643137683538396137326639"
            "343233626663656245409a025f5a4e35616c6c6f633131636f6c6c656374696f6e"
            "73356274726565346e6f646532313448616e646c65244c5424616c6c6f632e2e63"
            "6f6c6c656374696f6e732e2e62747265652e2e6e6f64652e2e4e6f646552656624"
            "4c5424616c6c6f632e2e636f6c6c656374696f6e732e2e62747265652e2e6e6f64"
            "652e2e6d61726b65722e2e4d75742443244b24432456244324616c6c6f632e2e63"
            "6f6c6c656374696f6e732e2e62747265652e2e6e6f64652e2e6d61726b65722e2e"
            "496e7465726e616c24475424244324616c6c6f632e2e636f6c6c656374696f6e73"
            "2e2e62747265652e2e6e6f64652e2e6d61726b65722e2e45646765244754243130"
            "696e736572745f6669743137686338613063663533396566663031313145419202"
            "5f5a4e35616c6c6f633131636f6c6c656374696f6e73356274726565346e6f6465"
            "32313248616e646c65244c5424616c6c6f632e2e636f6c6c656374696f6e732e2e"
            "62747265652e2e6e6f64652e2e4e6f6465526566244c5424616c6c6f632e2e636f"
            "6c6c656374696f6e732e2e62747265652e2e6e6f64652e2e6d61726b65722e2e4d"
            "75742443244b24432456244324616c6c6f632e2e636f6c6c656374696f6e732e2e"
            "62747265652e2e6e6f64652e2e6d61726b65722e2e496e7465726e616c24475424"
            "244324616c6c6f632e2e636f6c6c656374696f6e732e2e62747265652e2e6e6f64"
            "652e2e6d61726b65722e2e4b56244754243573706c697431376864303961343862"
            "37613831363331616145425a5f5a4e36315f244c542473657264655f6a736f6e2e"
            "2e6572726f722e2e4572726f72247532302461732475323024636f72652e2e666d"
            "742e2e44656275672447542433666d743137683430323537643666343265323962"
            "37344543595f5a4e36305f244c54247374642e2e696f2e2e6572726f722e2e4572"
            "726f72247532302461732475323024636f72652e2e666d742e2e446973706c6179"
            "2447542433666d74313768393032373163376232613663653833394544575f5a4e"
            "35385f244c5424616c6c6f632e2e737472696e672e2e537472696e672475323024"
            "61732475323024636f72652e2e666d742e2e44656275672447542433666d743137"
            "686236373265623139396333356431383645453a5f5a4e313073657264655f6a73"
            "6f6e32646531325061727365724e756d6265723576697369743137683836623839"
            "36383136626131306137654546565f5a4e35616c6c6f633131636f6c6c65637469"
            "6f6e73356274726565336d6170323542547265654d6170244c54244b2443245624"
            "4324412447542436696e7365727431376834643164623464613838343264346665"
            "4547455f5a4e313073657264655f6a736f6e347265616439536c69636552656164"
            "3137706f736974696f6e5f6f665f696e6465783137683236623431383938353234"
            "38333239364548695f5a4e37305f244c542473657264655f6a736f6e2e2e726561"
            "642e2e536c6963655265616424753230246173247532302473657264655f6a736f"
            "6e2e2e726561642e2e52656164244754243970617273655f737472313768616265"
            "386335353563386263643335354549475f5a4e313073657264655f6a736f6e3472"
            "65616439536c696365526561643139736b69705f746f5f6573636170655f736c6f"
            "7731376834373836633665323234666132336632454a465f5a4e35616c6c6f6333"
            "7665633136566563244c54245424432441244754243137657874656e645f66726f"
            "6d5f736c69636531376864626131346637346638653232366463454b2f5f5a4e31"
            "3073657264655f6a736f6e34726561643661735f73747231376866636436626234"
            "313731373865366635454c3e5f5a4e313073657264655f6a736f6e347265616432"
            "3070617273655f756e69636f64655f657363617065313768393634306663636162"
            "64303034613064454d725f5a4e37305f244c542473657264655f6a736f6e2e2e72"
            "6561642e2e536c6963655265616424753230246173247532302473657264655f6a"
            "736f6e2e2e726561642e2e526561642447542431376465636f64655f6865785f65"
            "736361706531376834376265353936383535663830346461454e355f5a4e313073"
            "657264655f6a736f6e347265616431317065656b5f6f725f656f66313768373363"
            "62313436306531616339386135454f2e5f5a4e313073657264655f6a736f6e3472"
            "656164356572726f72313768656635353237643333336339633236664550305f5a"
            "4e34636f726533666d743557726974653977726974655f666d7431376861333165"
            "6164363637646336373865304551325f5a4e35616c6c6f63377261775f76656331"
            "3166696e6973685f67726f77313768353338353962613338396237316433354552"
            "4b5f5a4e35616c6c6f63377261775f7665633230526177566563496e6e6572244c"
            "54244124475424313467726f775f616d6f7274697a656431376839386333363466"
            "6334356633643132344553435f5a4e35616c6c6f63377261775f76656331395261"
            "77566563244c54245424432441244754243867726f775f6f6e6531376866373333"
            "3137633566643665626336364554395f5a4e337374643674687265616438546872"
            "6561644964336e6577396578686175737465643137683333366266376131343838"
            "30343463384555425f5a4e34636f72653463656c6c346f6e636531374f6e636543"
            "656c6c244c54245424475424387472795f696e6974313768636536336266323238"
            "3531393165373145563e5f5a4e35616c6c6f633473796e633136417263244c5424"
            "5424432441244754243964726f705f736c6f773137686565396163636361643963"
            "63313036394557355f5a4e34636f72653970616e69636b696e6731336173736572"
            "745f6661696c6564313768323332363266326333633738623661624558475f5a4e"
            "34325f244c54242452462454247532302461732475323024636f72652e2e666d74"
            "2e2e44656275672447542433666d74313768653138373433383865303762666532"
            "3545595d5f5a4e36305f244c5424616c6c6f632e2e737472696e672e2e53747269"
            "6e67247532302461732475323024636f72652e2e666d742e2e446973706c617924"
            "47542433666d7431376863653432323661613166373236633163452e3238335a7a"
            "5f5a4e34636f726533707472383864726f705f696e5f706c616365244c54247374"
            "642e2e696f2e2e57726974652e2e77726974655f666d742e2e4164617074657224"
            "4c5424616c6c6f632e2e7665632e2e566563244c54247538244754242447542424"
            "47542431376831363664633631616230333334633165455b495f5a4e3373746434"
            "73796e63396f6e63655f6c6f636b31374f6e63654c6f636b244c54245424475424"
            "3130696e697469616c697a6531376837663563353038646139653162303962455c"
            "605f5a4e36315f244c54247374642e2e696f2e2e737464696f2e2e5374646f7574"
            "4c6f636b2475323024617324753230247374642e2e696f2e2e5772697465244754"
            "243977726974655f616c6c31376832346238323631303436316432353666455d55"
            "5f5a4e3373746432696f3862756666657265643962756677726974657231384275"
            "66577269746572244c54245724475424313477726974655f616c6c5f636f6c6431"
            "376835383462646262616562306662316262455e735f5a4e38305f244c54247374"
            "642e2e696f2e2e57726974652e2e77726974655f666d742e2e4164617074657224"
            "4c54245424475424247532302461732475323024636f72652e2e666d742e2e5772"
            "697465244754243977726974655f73747231376837666163663562633065666364"
            "383038455f325f5a4e34636f726533666d74355772697465313077726974655f63"
            "686172313768663062336265316563313964653565374560305f5a4e34636f7265"
            "33666d743557726974653977726974655f666d7431376866383830386630646630"
            "65343531336445610a727573745f70616e696362375f5a4e34636f72653570616e"
            "6963313250616e69635061796c6f61643661735f73747231376836313439663134"
            "3264396132653032654563505f5a4e38646c6d616c6c6f6338646c6d616c6c6f63"
            "3137446c6d616c6c6f63244c542441244754243138696e736572745f6c61726765"
            "5f6368756e6b313768656665383531613237353832646137624564455f5a4e3373"
            "746433737973396261636b747261636532365f5f727573745f656e645f73686f72"
            "745f6261636b747261636531376834646333646534376432323032316239456558"
            "5f5a4e337374643970616e69636b696e673139626567696e5f70616e69635f6861"
            "6e646c657232385f24753762242475376224636c6f737572652475376424247537"
            "64243137686531376133393737663839633131373845663b5f5a4e337374643970"
            "616e69636b696e673230727573745f70616e69635f776974685f686f6f6b313768"
            "37373665373963396636353931626535456783015f5a4e39395f244c5424737464"
            "2e2e70616e69636b696e672e2e626567696e5f70616e69635f68616e646c65722e"
            "2e5374617469635374725061796c6f6164247532302461732475323024636f7265"
            "2e2e70616e69632e2e50616e69635061796c6f6164244754243661735f73747231"
            "376865623366373232643232346534326638456888015f5a4e313073657264655f"
            "6a736f6e3576616c756532646537375f244c5424696d706c247532302473657264"
            "652e2e64652e2e446573657269616c697a652475323024666f7224753230247365"
            "7264655f6a736f6e2e2e76616c75652e2e56616c75652447542431316465736572"
            "69616c697a65313768333165353137383163383336383735394569535f5a4e3463"
            "6f726533707472343564726f705f696e5f706c616365244c542473657264655f6a"
            "736f6e2e2e6572726f722e2e4572726f7224475424313768663837633864366463"
            "39616234626335452e3331316a3c5f5a4e3573657264653264653756697369746f"
            "72313876697369745f626f72726f7765645f737472313768343564373131633837"
            "31363863326636456b4f5f5a4e313073657264655f6a736f6e3264653231446573"
            "657269616c697a6572244c54245224475424313670617273655f616e795f6e756d"
            "62657231376839316435333034653561396363663531456c4a5f5a4e3130736572"
            "64655f6a736f6e3264653231446573657269616c697a6572244c54245224475424"
            "313170617273655f6964656e743137683663353964643731393635353139313045"
            "6d735f5a4e37355f244c542473657264655f6a736f6e2e2e64652e2e4d61704163"
            "63657373244c5424522447542424753230246173247532302473657264652e2e64"
            "652e2e4d61704163636573732447542431336e6578745f6b65795f736565643137"
            "6865363235636133323138363233653036456e755f5a4e37355f244c5424736572"
            "64655f6a736f6e2e2e64652e2e4d6170416363657373244c542452244754242475"
            "3230246173247532302473657264652e2e64652e2e4d6170416363657373244754"
            "2431356e6578745f76616c75655f73656564313768656338353637376538303165"
            "39393133456f4b5f5a4e313073657264655f6a736f6e3264653231446573657269"
            "616c697a6572244c54245224475424313270617273655f6e756d62657231376837"
            "3833613431613462393130646432304570515f5a4e313073657264655f6a736f6e"
            "3264653231446573657269616c697a6572244c5424522447542431387061727365"
            "5f6c6f6e675f696e74656765723137686438313037386634613331633262653245"
            "714c5f5a4e313073657264655f6a736f6e3264653231446573657269616c697a65"
            "72244c54245224475424313370617273655f646563696d616c3137683661306333"
            "363832326663336530306145724d5f5a4e313073657264655f6a736f6e32646532"
            "31446573657269616c697a6572244c54245224475424313470617273655f657870"
            "6f6e656e743137683336646437646264323365346134656245734d5f5a4e313073"
            "657264655f6a736f6e3264653231446573657269616c697a6572244c5424522447"
            "542431346636345f66726f6d5f7061727473313768633863316239626161613836"
            "666637334574555f5a4e313073657264655f6a736f6e3264653231446573657269"
            "616c697a6572244c54245224475424323270617273655f646563696d616c5f6f76"
            "6572666c6f77313768336130306563656466383630313864334575565f5a4e3130"
            "73657264655f6a736f6e3264653231446573657269616c697a6572244c54245224"
            "475424323370617273655f6578706f6e656e745f6f766572666c6f773137683034"
            "3762396637333562616463666138457681015f5a4e37355f244c54247365726465"
            "5f6a736f6e2e2e64652e2e4d6170416363657373244c5424522447542424753230"
            "246173247532302473657264652e2e64652e2e4d61704163636573732447542431"
            "336e6578745f6b65795f7365656431326861735f6e6578745f6b65793137683564"
            "61326634303536653538313464394577066d656d636d7078365f5a4e3137636f6d"
            "70696c65725f6275696c74696e73336d656d376d656d6d6f766531376863383366"
            "3931363866353238616565364579076d656d6d6f76657a066d656d637079071201"
            "000f5f5f737461636b5f706f696e746572090a0100072e726f6461746100550970"
            "726f64756365727302086c616e6775616765010452757374000c70726f63657373"
            "65642d62790105727573746325312e38332e302d6e696768746c79202863326637"
            "346333663920323032342d30392d30392900490f7461726765745f666561747572"
            "6573042b0a6d756c746976616c75652b0f6d757461626c652d676c6f62616c732b"
            "0f7265666572656e63652d74797065732b087369676e2d657874";

        {
            // create escrow
            Env env(*this);
            env.fund(XRP(5000), alice, carol);
            auto const seq = env.seq(alice);
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 0);
            auto escrowCreate = escrow(alice, carol, XRP(1000));
            std::uint32_t const finishTime =
                (env.now() + 1s).time_since_epoch().count();
            escrowCreate[sfFinishAfter.jsonName] = finishTime;
            escrowCreate[sfFinishFunction.jsonName] = wasmHex;
            escrowCreate[sfData.jsonName] = "2";
            env(escrowCreate);
            env.close();

            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 1);
            env.require(balance(alice, XRP(4000) - drops(10)));
            env.require(balance(carol, XRP(5000)));

            env(finish(carol, alice, seq), ter(tecWASM_REJECTED));
            env(finish(alice, alice, seq), ter(tecWASM_REJECTED));
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
