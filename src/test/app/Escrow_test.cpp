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
        auto const baseFee = env.current()->fees().base;
        env.fund(XRP(5000), "alice", "bob");
        env(escrow("alice", "bob", XRP(1000)), finish_time(env.now() + 1s));
        env.close();

        auto const seq1 = env.seq("alice");

        env(escrow("alice", "bob", XRP(1000)),
            condition(cb1),
            finish_time(env.now() + 1s),
            fee(baseFee * 150));
        env.close();
        env(finish("bob", "alice", seq1),
            condition(cb1),
            fulfillment(fb1),
            fee(baseFee * 150));

        auto const seq2 = env.seq("alice");

        env(escrow("alice", "bob", XRP(1000)),
            condition(cb2),
            finish_time(env.now() + 1s),
            cancel_time(env.now() + 2s),
            fee(baseFee * 150));
        env.close();
        env(cancel("bob", "alice", seq2), fee(baseFee * 150));
    }

    void
    testTiming()
    {
        using namespace jtx;
        using namespace std::chrono;

        {
            testcase("Timing: Finish Only");
            Env env(*this);
            auto const baseFee = env.current()->fees().base;
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
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));

            env(finish("bob", "alice", seq), fee(baseFee * 150));
        }

        {
            testcase("Timing: Cancel Only");
            Env env(*this);
            auto const baseFee = env.current()->fees().base;
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
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));

            // Verify that a finish won't work anymore.
            env(finish("bob", "alice", seq),
                condition(cb1),
                fulfillment(fb1),
                fee(baseFee * 150),
                ter(tecNO_PERMISSION));

            // Verify that the cancel will succeed
            env(cancel("bob", "alice", seq), fee(baseFee * 150));
        }

        {
            testcase("Timing: Finish and Cancel -> Finish");
            Env env(*this);
            auto const baseFee = env.current()->fees().base;
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
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));
                env(cancel("bob", "alice", seq),
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));
            }

            // Verify that a cancel still won't work
            env(cancel("bob", "alice", seq),
                fee(baseFee * 150),
                ter(tecNO_PERMISSION));

            // And verify that a finish will
            env(finish("bob", "alice", seq), fee(baseFee * 150));
        }

        {
            testcase("Timing: Finish and Cancel -> Cancel");
            Env env(*this);
            auto const baseFee = env.current()->fees().base;
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
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));
                env(cancel("bob", "alice", seq),
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));
            }

            // Continue advancing, verifying that the cancel won't complete
            // prematurely. At this point a finish would succeed.
            for (; env.now() < cts; env.close())
                env(cancel("bob", "alice", seq),
                    fee(baseFee * 150),
                    ter(tecNO_PERMISSION));

            // Verify that finish will no longer work, since we are past the
            // cancel activation time.
            env(finish("bob", "alice", seq),
                fee(baseFee * 150),
                ter(tecNO_PERMISSION));

            // And verify that a cancel will succeed.
            env(cancel("bob", "alice", seq), fee(baseFee * 150));
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
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob", "carol");
            env.close();

            // Creating an escrow without a finish time and finishing it
            // is allowed without fix1571:
            auto const seq1 = env.seq("alice");
            env(escrow("alice", "bob", XRP(100)),
                cancel_time(env.now() + 1s),
                fee(baseFee * 150));
            env.close();
            env(finish("carol", "alice", seq1), fee(baseFee * 150));
            BEAST_EXPECT(env.balance("bob") == XRP(5100));

            env.close();

            // Creating an escrow without a finish time and a condition is
            // also allowed without fix1571:
            auto const seq2 = env.seq("alice");
            env(escrow("alice", "bob", XRP(100)),
                cancel_time(env.now() + 1s),
                condition(cb1),
                fee(baseFee * 150));
            env.close();
            env(finish("carol", "alice", seq2),
                condition(cb1),
                fulfillment(fb1),
                fee(baseFee * 150));
            BEAST_EXPECT(env.balance("bob") == XRP(5200));
        }

        {
            testcase("Implied Finish Time (with fix1571)");

            Env env(*this);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob", "carol");
            env.close();

            // Creating an escrow with only a cancel time is not allowed:
            env(escrow("alice", "bob", XRP(100)),
                cancel_time(env.now() + 90s),
                fee(baseFee * 150),
                ter(temMALFORMED));

            // Creating an escrow with only a cancel time and a condition is
            // allowed:
            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", XRP(100)),
                cancel_time(env.now() + 90s),
                condition(cb1),
                fee(baseFee * 150));
            env.close();
            env(finish("carol", "alice", seq),
                condition(cb1),
                fulfillment(fb1),
                fee(baseFee * 150));
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
        auto const baseFee = env.current()->fees().base;
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
                fee(150 * baseFee));
            env.close();
            env(finish("hannah", "hannah", seq + 7),
                fee(150 * baseFee),
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
                fee(150 * baseFee),
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
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(escrow("alice", "alice", XRP(1000)),
                finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq), ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible
            env(cancel("bob", "alice", seq), ter(tecNO_PERMISSION));

            // Finish should succeed. Verify funds.
            env(finish("bob", "alice", seq));
            env.require(balance("alice", XRP(5000) - drops(baseFee)));
        }
        {
            // Unconditionally pay from Alice to Bob.  Zelda (neither source nor
            // destination) signs all cancels and finishes.  This shows that
            // Escrow will make a payment to Bob with no intervention from Bob.
            Env env(*this);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob", "zelda");
            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", XRP(1000)), finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));

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

            env.require(balance("alice", XRP(4000) - drops(baseFee)));
            env.require(balance("bob", XRP(6000)));
            env.require(balance("zelda", XRP(5000) - drops(4 * baseFee)));
        }
        {
            // Bob sets DepositAuth so only Bob can finish the escrow.
            Env env(*this);
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(5000), "alice", "bob", "zelda");
            env(fset("bob", asfDepositAuth));
            env.close();

            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", XRP(1000)), finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));

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

            env.require(balance("alice", XRP(4000) - (baseFee * 5)));
            env.require(balance("bob", XRP(6000) - (baseFee * 5)));
            env.require(balance("zelda", XRP(5000) - (baseFee * 4)));
        }
        {
            // Bob sets DepositAuth but preauthorizes Zelda, so Zelda can
            // finish the escrow.
            Env env(*this);
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(5000), "alice", "bob", "zelda");
            env(fset("bob", asfDepositAuth));
            env.close();
            env(deposit::auth("bob", "zelda"));
            env.close();

            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", XRP(1000)), finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));
            env.close();

            // DepositPreauth allows Finish to succeed for either Zelda or
            // Bob. But Finish won't succeed for Alice since she is not
            // preauthorized.
            env(finish("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("zelda", "alice", seq));
            env.close();

            env.require(balance("alice", XRP(4000) - (baseFee * 2)));
            env.require(balance("bob", XRP(6000) - (baseFee * 2)));
            env.require(balance("zelda", XRP(5000) - (baseFee * 1)));
        }
        {
            // Conditional
            Env env(*this);
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(escrow("alice", "alice", XRP(1000)),
                condition(cb2),
                finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(cancel("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(cancel("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("alice", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("alice", "alice", seq),
                condition(cb2),
                fulfillment(fb2),
                fee(150 * baseFee),
                ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq), ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq),
                condition(cb2),
                fulfillment(fb2),
                fee(150 * baseFee),
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
                fee(150 * baseFee));
        }
        {
            // Self-escrowed conditional with DepositAuth.
            Env env(*this);
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(escrow("alice", "alice", XRP(1000)),
                condition(cb3),
                finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));
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
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(cb3),
                fulfillment(fb3),
                fee(150 * baseFee),
                ter(tecNO_PERMISSION));
            env(finish("alice", "alice", seq),
                condition(cb3),
                fulfillment(fb3),
                fee(150 * baseFee));
        }
        {
            // Self-escrowed conditional with DepositAuth and DepositPreauth.
            Env env(*this);
            auto const baseFee = env.current()->fees().base;

            env.fund(XRP(5000), "alice", "bob", "zelda");
            auto const seq = env.seq("alice");
            env(escrow("alice", "alice", XRP(1000)),
                condition(cb3),
                finish_time(env.now() + 5s));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));
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
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(cb3),
                fulfillment(fb3),
                fee(150 * baseFee),
                ter(tecNO_PERMISSION));
            env(finish("zelda", "alice", seq),
                condition(cb3),
                fulfillment(fb3),
                fee(150 * baseFee));
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
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob", "carol");
            auto const seq = env.seq("alice");
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(escrow("alice", "carol", XRP(1000)),
                condition(cb1),
                cancel_time(env.now() + 1s));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env.require(balance("alice", XRP(4000) - drops(baseFee)));
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
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq),
                condition(cb1),
                fulfillment(cb2),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq),
                condition(cb1),
                fulfillment(cb3),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with an incorrect condition and various
            // combinations of correct and incorrect fulfillments.
            env(finish("bob", "alice", seq),
                condition(cb2),
                fulfillment(fb1),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq),
                condition(cb2),
                fulfillment(fb2),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq),
                condition(cb2),
                fulfillment(fb3),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with the correct condition & fulfillment
            env(finish("bob", "alice", seq),
                condition(cb1),
                fulfillment(fb1),
                fee(150 * baseFee));

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
            auto const baseFee = env.current()->fees().base;
            env.fund(XRP(5000), "alice", "bob", "carol");
            auto const seq = env.seq("alice");
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(escrow("alice", "carol", XRP(1000)),
                condition(cb2),
                cancel_time(env.now() + 1s));
            env.close();
            env.require(balance("alice", XRP(4000) - drops(baseFee)));
            // balance restored on cancel
            env(cancel("bob", "alice", seq));
            env.require(balance("alice", XRP(5000) - drops(baseFee)));
            // SLE removed on cancel
            BEAST_EXPECT(!env.le(keylet::escrow(Account("alice").id(), seq)));
        }
        {
            Env env(*this);
            auto const baseFee = env.current()->fees().base;
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
                fee(150 * baseFee),
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
            auto const baseFee = env.current()->fees().base;
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{p + 1, s - 2}),
                cancel_time(ts),
                fee(10 * baseFee));
            env(finish("bob", "alice", seq),
                condition(cb1),
                fulfillment(fb1),
                fee(150 * baseFee));
            env.require(balance("alice", XRP(4000) - drops(10 * baseFee)));
            env.require(balance("bob", XRP(5000) - drops(150 * baseFee)));
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
            auto const baseFee = env.current()->fees().base;
            env(escrow("alice", "carol", XRP(1000)),
                condition(Slice{cp + 1, cs - 2}),
                cancel_time(ts),
                fee(10 * baseFee));

            // Now, try to fulfill using the same sequence of
            // malformed conditions.
            env(finish("bob", "alice", seq),
                condition(Slice{cp, cs}),
                fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp, cs - 1}),
                fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp, cs - 2}),
                fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 1}),
                fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 3}),
                fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 2, cs - 2}),
                fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 2, cs - 3}),
                fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));

            // Now, using the correct condition, try malformed fulfillments:
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp, fs}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp, fs - 1}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp, fs - 2}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 1, fs - 1}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 1, fs - 3}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 1, fs - 3}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 2, fs - 2}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 2, fs - 3}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));

            // Now try for the right one
            env(finish("bob", "alice", seq),
                condition(cb2),
                fulfillment(fb2),
                fee(150 * baseFee));
            env.require(balance("alice", XRP(4000) - drops(10 * baseFee)));
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
            auto const baseFee = env.current()->fees().base;
            env(escrow("alice", "carol", XRP(1000)),
                condition(cb3),
                cancel_time(env.now() + 1s));

            env(finish("bob", "alice", seq),
                condition(Slice{}),
                fulfillment(Slice{}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(cb3),
                fulfillment(Slice{}),
                fee(150 * baseFee),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),
                condition(Slice{}),
                fulfillment(fb3),
                fee(150 * baseFee),
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
                fee(150 * baseFee));
            env.require(balance("carol", XRP(6000)));
            env.require(balance("alice", XRP(4000) - drops(baseFee)));
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
        auto const baseFee = env.current()->fees().base;

        env.memoize("alice");
        env.memoize("bob");
        env.memoize("carol");

        {
            auto const jtx = env.jt(
                escrow("alice", "carol", XRP(1000)),
                finish_time(env.now() + 1s),
                seq(1),
                fee(baseFee));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(baseFee));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(1000));
        }

        {
            auto const jtx =
                env.jt(cancel("bob", "alice", 3), seq(1), fee(baseFee));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(baseFee));
            BEAST_EXPECT(pf.consequences.potentialSpend() == XRP(0));
        }

        {
            auto const jtx =
                env.jt(finish("bob", "alice", 3), seq(1), fee(baseFee));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(baseFee));
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
            auto const baseFee = env.current()->fees().base;
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
                    fee(150 * baseFee),
                    ticket::use(--bobTicket),
                    ter(tecNO_PERMISSION));
                BEAST_EXPECT(env.seq(bob) == bobRootSeq);
            }

            // bob tries to re-use a ticket, which is rejected.
            env(finish(bob, alice, escrowSeq),
                fee(150 * baseFee),
                ticket::use(bobTicket),
                ter(tefNO_TICKET));

            // bob uses one of his remaining tickets.  Success!
            env(finish(bob, alice, escrowSeq),
                fee(150 * baseFee),
                ticket::use(--bobTicket));
            env.close();
            BEAST_EXPECT(env.seq(bob) == bobRootSeq);
        }
        {
            // Create escrow and cancel using tickets.
            Env env(*this);
            auto const baseFee = env.current()->fees().base;
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
                    fee(150 * baseFee),
                    ticket::use(bobTicket++),
                    ter(tecNO_PERMISSION));
                BEAST_EXPECT(env.seq(bob) == bobRootSeq);
            }

            // Verify that a finish won't work anymore.
            env(finish(bob, alice, escrowSeq),
                condition(cb1),
                fulfillment(fb1),
                fee(150 * baseFee),
                ticket::use(bobTicket++),
                ter(tecNO_PERMISSION));
            BEAST_EXPECT(env.seq(bob) == bobRootSeq);

            // Verify that the cancel succeeds.
            env(cancel(bob, alice, escrowSeq),
                fee(150 * baseFee),
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
        Account const dillon{"dillon "};
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
    run() override
    {
        testEnablement();
        testTiming();
        testTags();
        testDisallowXRP();
        test1571();
        testFails();
        testLockup();
        testEscrowConditions();
        testMetaAndOwnership();
        testConsequences();
        testEscrowWithTickets();
        testCredentials();
    }
};

BEAST_DEFINE_TESTSUITE(Escrow, app, ripple);

}  // namespace test
}  // namespace ripple
