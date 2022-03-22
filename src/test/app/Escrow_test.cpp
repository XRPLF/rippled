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

#include <ripple/app/tx/applySteps.h>
#include <ripple/ledger/Directory.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/TxFlags.h>
#include <ripple/protocol/jss.h>
#include <algorithm>
#include <iterator>
#include <test/jtx.h>

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

    /** Set the "FinishAfter" time tag on a JTx */
    struct finish_time
    {
    private:
        NetClock::time_point value_;

    public:
        explicit finish_time(NetClock::time_point const& value) : value_(value)
        {
        }

        void
        operator()(jtx::Env&, jtx::JTx& jt) const
        {
            jt.jv[sfFinishAfter.jsonName] = value_.time_since_epoch().count();
        }
    };

    /** Set the "CancelAfter" time tag on a JTx */
    struct cancel_time
    {
    private:
        NetClock::time_point value_;

    public:
        explicit cancel_time(NetClock::time_point const& value) : value_(value)
        {
        }

        void
        operator()(jtx::Env&, jtx::JTx& jt) const
        {
            jt.jv[sfCancelAfter.jsonName] = value_.time_since_epoch().count();
        }
    };

    struct condition
    {
    private:
        std::string value_;

    public:
        explicit condition(Slice cond) : value_(strHex(cond))
        {
        }

        template <size_t N>
        explicit condition(std::array<std::uint8_t, N> c)
            : condition(makeSlice(c))
        {
        }

        void
        operator()(jtx::Env&, jtx::JTx& jt) const
        {
            jt.jv[sfCondition.jsonName] = value_;
        }
    };

    struct fulfillment
    {
    private:
        std::string value_;

    public:
        explicit fulfillment(Slice condition) : value_(strHex(condition))
        {
        }

        template <size_t N>
        explicit fulfillment(std::array<std::uint8_t, N> f)
            : fulfillment(makeSlice(f))
        {
        }

        void
        operator()(jtx::Env&, jtx::JTx& jt) const
        {
            jt.jv[sfFulfillment.jsonName] = value_;
        }
    };

    static Json::Value
    escrow(
        jtx::Account const& account,
        jtx::Account const& to,
        STAmount const& amount)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = jss::EscrowCreate;
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv[jss::Destination] = to.human();
        jv[jss::Amount] = amount.getJson(JsonOptions::none);
        return jv;
    }

    static Json::Value
    finish(
        jtx::Account const& account,
        jtx::Account const& from,
        std::uint32_t seq)
    {
        Json::Value jv;
        jv[jss::TransactionType] = jss::EscrowFinish;
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv[sfOwner.jsonName] = from.human();
        jv[sfOfferSequence.jsonName] = seq;
        return jv;
    }

    static Json::Value
    cancel(
        jtx::Account const& account,
        jtx::Account const& from,
        std::uint32_t seq)
    {
        Json::Value jv;
        jv[jss::TransactionType] = jss::EscrowCancel;
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv[sfOwner.jsonName] = from.human();
        jv[sfOfferSequence.jsonName] = seq;
        return jv;
    }

    static STAmount
    lockedAmount(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::Account const& gw,
        jtx::IOU const& iou)
    {
        auto const sle = env.le(keylet::line(account, gw, iou.currency));
        if (sle->isFieldPresent(sfLockedBalance))
            return (*sle)[sfLockedBalance];
        return STAmount(iou, 0);
    }

    static Rate
    escrowRate(
        jtx::Env const& env,
        jtx::Account const& account,
        uint32_t const& seq)
    {
        auto const sle = env.le(keylet::escrow(account.id(), seq));
        if (sle->isFieldPresent(sfTransferRate))
            return ripple::Rate((*sle)[sfTransferRate]);
        return Rate{0};
    }

    static STAmount
    lineBalance(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::Account const& gw,
        jtx::IOU const& iou)
    {
        auto const sle = env.le(keylet::line(account, gw, iou.currency));
        if (sle && sle->isFieldPresent(sfBalance))
            return (*sle)[sfBalance];
        return STAmount(iou, 0);
    }

    void
    testEnablement(FeatureBitset features)
    {
        testcase("Enablement");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
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
    testTiming(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        {
            testcase("Timing: Finish Only");
            Env env{*this, features};
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
            Env env{*this, features};
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
            Env env{*this, features};
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
            Env env{*this, features};
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
    testTags(FeatureBitset features)
    {
        testcase("Tags");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};

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
    testDisallowXRP(FeatureBitset features)
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
            Env env{*this, features};

            env.fund(XRP(5000), "bob", "george");
            env(fset("george", asfDisallowXRP));
            env(escrow("bob", "george", XRP(10)), finish_time(env.now() + 1s));
        }
    }

    void
    test1571(FeatureBitset features)
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

            Env env{*this, features};
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
    testFails(FeatureBitset features)
    {
        testcase("Failure Cases");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
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

        // // Using non-XRP:
        // env(escrow("alice", "carol", Account("alice")["USD"](500)),
        //     finish_time(env.now() + 1s),
        //     ter(temBAD_AMOUNT));

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
    testLockup(FeatureBitset features)
    {
        testcase("Lockup");

        using namespace jtx;
        using namespace std::chrono;

        {
            // Unconditional
            Env env{*this, features};
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
            Env env{*this, features};
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
            Env env{*this, features};

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
            Env env{*this, features};

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
            Env env{*this, features};
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
            Env env{*this, features};

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
            Env env{*this, features};

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
    testEscrowConditions(FeatureBitset features)
    {
        testcase("Escrow with CryptoConditions");

        using namespace jtx;
        using namespace std::chrono;

        {  // Test cryptoconditions
            Env env{*this, features};
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
            Env env{*this, features};
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
            Env env{*this, features};
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
            Env env{*this, features};
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
            Env env{*this, features};
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
            Env env{*this, features};
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
            Env env{*this, features};
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
    testMetaAndOwnership(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        auto const alice = Account("alice");
        auto const bruce = Account("bruce");
        auto const carol = Account("carol");

        {
            testcase("Metadata to self");

            Env env{*this, features};
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

            Env env{*this, features};
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
    testConsequences(FeatureBitset features)
    {
        testcase("Consequences");

        using namespace jtx;
        using namespace std::chrono;
        Env env{*this, features};

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
    testEscrowWithTickets(FeatureBitset features)
    {
        testcase("Escrow with tickets");

        using namespace jtx;
        using namespace std::chrono;
        Account const alice{"alice"};
        Account const bob{"bob"};

        {
            // Create escrow and finish using tickets.
            Env env{*this, features};
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
            Env env{*this, features};
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
    testICEnablement(FeatureBitset features)
    {
        testcase("IC Enablement");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(5000), alice, bob, gw);
        env.close();
        env.trust(USD(10000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(5000)));
        env(pay(gw, bob, USD(5000)));
        env.close();

        env(escrow(alice, bob, USD(1000)), finish_time(env.now() + 1s));
        env.close();

        auto const seq1 = env.seq(alice);

        env(escrow(alice, bob, USD(1000)),
            condition(cb1),
            finish_time(env.now() + 1s),
            fee(1500));
        env.close();
        env(finish(bob, alice, seq1),
            condition(cb1),
            fulfillment(fb1),
            fee(1500));

        auto const seq2 = env.seq(alice);

        env(escrow(alice, bob, USD(1000)),
            condition(cb2),
            finish_time(env.now() + 1s),
            cancel_time(env.now() + 2s),
            fee(1500));
        env.close();
        env(cancel(bob, alice, seq2), fee(1500));
    }

    void
    testICTiming(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        {
            testcase("Timing: IC Finish Only");
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            // We create an escrow that can be finished in the future
            auto const ts = env.now() + 97s;

            auto const seq = env.seq(alice);
            env(escrow(alice, bob, USD(1000)), finish_time(ts));

            // Advance the ledger, verifying that the finish won't complete
            // prematurely.
            for (; env.now() < ts; env.close())
                env(finish(bob, alice, seq), fee(1500), ter(tecNO_PERMISSION));

            env(finish(bob, alice, seq), fee(1500));
        }

        {
            testcase("Timing: IC Cancel Only");
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            // We create an escrow that can be cancelled in the future
            auto const ts = env.now() + 117s;

            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", USD(1000)),
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
            testcase("Timing: IC Finish and Cancel -> Finish");
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            // We create an escrow that can be cancelled in the future
            auto const fts = env.now() + 117s;
            auto const cts = env.now() + 192s;

            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", USD(1000)),
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
            testcase("Timing: IC Finish and Cancel -> Cancel");
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            // We create an escrow that can be cancelled in the future
            auto const fts = env.now() + 109s;
            auto const cts = env.now() + 184s;

            auto const seq = env.seq("alice");
            env(escrow("alice", "bob", USD(1000)),
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
    testICTags(FeatureBitset features)
    {
        testcase("IC Tags");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(5000), alice, bob, gw);
        env.close();
        env.trust(USD(10000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(5000)));
        env(pay(gw, bob, USD(5000)));
        env.close();

        // Check to make sure that we correctly detect if tags are really
        // required:
        env(fset(bob, asfRequireDest));
        env(escrow(alice, bob, USD(1000)),
            finish_time(env.now() + 1s),
            ter(tecDST_TAG_NEEDED));

        // set source and dest tags
        auto const seq = env.seq(alice);

        env(escrow(alice, bob, USD(1000)),
            finish_time(env.now() + 1s),
            stag(1),
            dtag(2));

        auto const sle = env.le(keylet::escrow(alice.id(), seq));
        BEAST_EXPECT(sle);
        BEAST_EXPECT((*sle)[sfSourceTag] == 1);
        BEAST_EXPECT((*sle)[sfDestinationTag] == 2);
    }

    void
    testICDisallowXRP(FeatureBitset features)
    {
        testcase("IC Disallow XRP");

        using namespace jtx;
        using namespace std::chrono;

        auto const bob = Account("bob");
        auto const george = Account("george");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        {
            // Respect the "asfDisallowXRP" account flag:
            Env env(*this, supported_amendments() - featureDepositAuth);
            env.fund(XRP(5000), bob, george, gw);
            env.close();
            env.trust(USD(10000), bob, george);
            env.close();
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, george, USD(5000)));
            env.close();
            env(fset(george, asfDisallowXRP));
            env(escrow(bob, george, USD(10)), finish_time(env.now() + 1s));
        }
        {
            // Ignore the "asfDisallowXRP" account flag, which we should
            // have been doing before.
            Env env{*this, features};
            env.fund(XRP(5000), bob, george, gw);
            env.close();
            env.trust(USD(10000), bob, george);
            env.close();
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, george, USD(5000)));
            env.close();
            env(fset(george, asfDisallowXRP));
            env(escrow(bob, george, USD(10)), finish_time(env.now() + 1s));
        }
    }

    void
    testIC1571(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        {
            testcase("IC Implied Finish Time (without fix1571)");

            Env env(*this, supported_amendments() - fix1571);
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();

            // Creating an escrow without a finish time and finishing it
            // is allowed without fix1571:
            auto const seq1 = env.seq(alice);
            env(escrow(alice, bob, USD(100)),
                cancel_time(env.now() + 1s),
                fee(1500));
            env.close();
            env(finish(carol, alice, seq1), fee(1500));
            BEAST_EXPECT(env.balance(bob, USD.issue()).value() == USD(5100));
            env.close();

            // Creating an escrow without a finish time and a condition is
            // also allowed without fix1571:
            auto const seq2 = env.seq(alice);
            env(escrow(alice, bob, USD(100)),
                cancel_time(env.now() + 1s),
                condition(cb1),
                fee(1500));
            env.close();
            env(finish(carol, alice, seq2),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            BEAST_EXPECT(env.balance(bob, USD.issue()).value() == USD(5200));
        }

        {
            testcase("IC Implied Finish Time (with fix1571)");

            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();

            // Creating an escrow with only a cancel time is not allowed:
            env(escrow(alice, bob, USD(100)),
                cancel_time(env.now() + 90s),
                fee(1500),
                ter(temMALFORMED));

            // Creating an escrow with only a cancel time and a condition is
            // allowed:
            auto const seq = env.seq(alice);
            env(escrow(alice, bob, USD(100)),
                cancel_time(env.now() + 90s),
                condition(cb1),
                fee(1500));
            env.close();
            env(finish(carol, alice, seq),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            BEAST_EXPECT(env.balance(bob, USD.issue()).value() == USD(5100));
        }
    }

    void
    testICFails(FeatureBitset features)
    {
        testcase("IC Failure Cases");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(5000), alice, bob, gw);
        env.close();
        env.trust(USD(10000), alice, bob);
        env.close();
        env(pay(gw, alice, USD(5000)));
        env(pay(gw, bob, USD(5000)));
        env.close();

        // Finish time is in the past
        env(escrow(alice, bob, USD(1000)),
            finish_time(env.now() - 5s),
            ter(tecNO_PERMISSION));

        // Cancel time is in the past
        env(escrow(alice, bob, USD(1000)),
            condition(cb1),
            cancel_time(env.now() - 5s),
            ter(tecNO_PERMISSION));

        // no destination account
        env(escrow(alice, "carol", USD(1000)),
            finish_time(env.now() + 1s),
            ter(tecNO_DST));

        auto const carol = Account("carol");
        env.fund(XRP(5000), carol);
        env.close();
        env.trust(USD(10000), carol);
        env.close();
        env(pay(gw, carol, USD(5000)));
        env.close();

        // Sending zero or no XRP:
        env(escrow(alice, carol, USD(0)),
            finish_time(env.now() + 1s),
            ter(temBAD_AMOUNT));
        env(escrow(alice, carol, USD(-1000)),
            finish_time(env.now() + 1s),
            ter(temBAD_AMOUNT));

        // Fail if neither CancelAfter nor FinishAfter are specified:
        env(escrow(alice, carol, USD(1)), ter(temBAD_EXPIRATION));

        // Fail if neither a FinishTime nor a condition are attached:
        env(escrow(alice, carol, USD(1)),
            cancel_time(env.now() + 1s),
            ter(temMALFORMED));

        // Fail if FinishAfter has already passed:
        env(escrow(alice, carol, USD(1)),
            finish_time(env.now() - 1s),
            ter(tecNO_PERMISSION));

        // If both CancelAfter and FinishAfter are set, then CancelAfter must
        // be strictly later than FinishAfter.
        env(escrow(alice, carol, USD(1)),
            condition(cb1),
            finish_time(env.now() + 10s),
            cancel_time(env.now() + 10s),
            ter(temBAD_EXPIRATION));

        env(escrow(alice, carol, USD(1)),
            condition(cb1),
            finish_time(env.now() + 10s),
            cancel_time(env.now() + 5s),
            ter(temBAD_EXPIRATION));

        // Carol now requires the use of a destination tag
        env(fset(carol, asfRequireDest));

        // missing destination tag
        env(escrow(alice, carol, USD(1)),
            condition(cb1),
            cancel_time(env.now() + 1s),
            ter(tecDST_TAG_NEEDED));

        // Success!
        env(escrow(alice, carol, USD(1)),
            condition(cb1),
            cancel_time(env.now() + 1s),
            dtag(1));

        {  // Fail if the sender wants to send more than he has:
            auto const daniel = Account("daniel");
            env.fund(XRP(5000), daniel);
            env.close();
            env.trust(USD(100), daniel);
            env.close();
            env(pay(gw, daniel, USD(50)));
            env.close();

            env(escrow(daniel, bob, USD(51)),
                finish_time(env.now() + 1s),
                ter(tecINSUFFICIENT_FUNDS));

            // Removed 3 Account Reserve/Increment XRP tests
            // See line 602

            env(escrow(daniel, bob, USD(10)), finish_time(env.now() + 1s));
            env.close();
            env(escrow(daniel, bob, USD(51)),
                finish_time(env.now() + 1s),
                ter(tecINSUFFICIENT_FUNDS));
        }

        {  // Specify incorrect sequence number
            auto const hannah = Account("hannah");
            env.fund(XRP(5000), hannah);
            env.close();
            env.trust(USD(10000), hannah);
            env.close();
            env(pay(gw, hannah, USD(5000)));
            env.close();

            auto const seq = env.seq(hannah);
            env(escrow(hannah, hannah, USD(5000)),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            env(finish(hannah, hannah, seq + 7), fee(1500), ter(tecNO_TARGET));
        }

        {  // Try to specify a condition for a non-conditional payment
            auto const ivan = Account("ivan");
            env.fund(XRP(5000), ivan);
            env.close();
            env.trust(USD(10000), ivan);
            env.close();
            env(pay(gw, ivan, USD(5000)));
            env.close();

            auto const seq = env.seq(ivan);

            env(escrow(ivan, ivan, USD(10)), finish_time(env.now() + 1s));
            env.close();
            env(finish(ivan, ivan, seq),
                condition(cb1),
                fulfillment(fb1),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
        }
    }

    void
    testICLockup(FeatureBitset features)
    {
        testcase("IC Lockup");

        using namespace jtx;
        using namespace std::chrono;

        {
            // Unconditional
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice);
            env.trust(USD(10000), bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            auto const seq = env.seq(alice);
            env(escrow(alice, alice, USD(1000)), finish_time(env.now() + 5s));

            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env.require(balance(alice, XRP(5000) - drops(10)));
            env.require(balance(alice, USD(5000)));
            BEAST_EXPECT(preLocked == USD(1000));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(cancel(bob, alice, seq), ter(tecNO_PERMISSION));
            env(finish(bob, alice, seq), ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible
            env(cancel(bob, alice, seq), ter(tecNO_PERMISSION));

            // Finish should succeed. Verify funds.
            env(finish(bob, alice, seq));
            env.require(balance(alice, XRP(5000) - drops(10)));
        }
        {
            // Unconditionally pay from Alice to Bob.  Zelda (neither source nor
            // destination) signs all cancels and finishes.  This shows that
            // Escrow will make a payment to Bob with no intervention from Bob.
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();

            auto const seq = env.seq(alice);
            env(escrow(alice, bob, USD(1000)), finish_time(env.now() + 5s));

            // Verify amounts
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env.require(balance(alice, XRP(5000) - drops(10)));
            BEAST_EXPECT(preLocked == USD(1000));
            env.require(balance(alice, USD(5000)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(cancel(carol, alice, seq), ter(tecNO_PERMISSION));
            env(finish(carol, alice, seq), ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible
            env(cancel(carol, alice, seq), ter(tecNO_PERMISSION));

            // Finish should succeed. Verify funds.
            env(finish(carol, alice, seq));
            env.close();

            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            env.require(balance(alice, XRP(5000) - drops(10)));
            BEAST_EXPECT(postLocked == USD(0));
            env.require(balance(alice, USD(4000)));
            env.require(balance(bob, USD(6000)));
            env.require(balance(carol, XRP(5000) - drops(40)));
        }
        {
            // Bob sets DepositAuth so only Bob can finish the escrow.
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();

            env(fset(bob, asfDepositAuth));
            env.close();

            auto const seq = env.seq(alice);
            env(escrow(alice, bob, USD(1000)), finish_time(env.now() + 5s));

            // Verify amounts
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env.require(balance(alice, XRP(5000) - drops(10)));
            BEAST_EXPECT(preLocked == USD(1000));
            env.require(balance(alice, USD(5000)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(cancel(carol, alice, seq), ter(tecNO_PERMISSION));
            env(cancel(alice, alice, seq), ter(tecNO_PERMISSION));
            env(cancel(bob, alice, seq), ter(tecNO_PERMISSION));
            env(finish(carol, alice, seq), ter(tecNO_PERMISSION));
            env(finish(alice, alice, seq), ter(tecNO_PERMISSION));
            env(finish(bob, alice, seq), ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible. Finish will only succeed
            // for
            // Bob, because of DepositAuth.
            env(cancel(carol, alice, seq), ter(tecNO_PERMISSION));
            env(cancel(alice, alice, seq), ter(tecNO_PERMISSION));
            env(cancel(bob, alice, seq), ter(tecNO_PERMISSION));
            env(finish(carol, alice, seq), ter(tecNO_PERMISSION));
            env(finish(alice, alice, seq), ter(tecNO_PERMISSION));
            env(finish(bob, alice, seq));
            env.close();

            // Verify amounts
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(0));
            env.require(balance(alice, USD(4000)));
            env.require(balance(bob, USD(6000)));
            auto const baseFee = env.current()->fees().base;
            env.require(balance(alice, XRP(5000) - (baseFee * 5)));
            env.require(balance(bob, XRP(5000) - (baseFee * 5)));
            env.require(balance(carol, XRP(5000) - (baseFee * 4)));
        }
        {
            // Bob sets DepositAuth but preauthorizes Zelda, so Zelda can
            // finish the escrow.
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();

            env(fset(bob, asfDepositAuth));
            env.close();
            env(deposit::auth(bob, carol));
            env.close();

            auto const seq = env.seq(alice);
            env(escrow(alice, bob, USD(1000)), finish_time(env.now() + 5s));

            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env.require(balance(alice, XRP(5000) - drops(10)));
            BEAST_EXPECT(preLocked == USD(1000));
            env.require(balance(alice, USD(5000)));

            env.close();

            // DepositPreauth allows Finish to succeed for either Zelda or
            // Bob. But Finish won't succeed for Alice since she is not
            // preauthorized.
            env(finish(alice, alice, seq), ter(tecNO_PERMISSION));
            env(finish(carol, alice, seq));
            env.close();

            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(0));
            env.require(balance(alice, USD(4000)));
            env.require(balance(bob, USD(6000)));
            auto const baseFee = env.current()->fees().base;
            env.require(balance(alice, XRP(5000) - (baseFee * 2)));
            env.require(balance(bob, XRP(5000) - (baseFee * 2)));
            env.require(balance(carol, XRP(5000) - (baseFee * 1)));
        }
        {
            // Conditional
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            auto const seq = env.seq(alice);
            env(escrow(alice, alice, USD(1000)),
                condition(cb2),
                finish_time(env.now() + 5s));

            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env.require(balance(alice, XRP(5000) - drops(10)));
            BEAST_EXPECT(preLocked == USD(1000));
            env.require(balance(alice, USD(5000)));

            // Not enough time has elapsed for a finish and canceling isn't
            // possible.
            env(cancel(alice, alice, seq), ter(tecNO_PERMISSION));
            env(cancel(bob, alice, seq), ter(tecNO_PERMISSION));
            env(finish(alice, alice, seq), ter(tecNO_PERMISSION));
            env(finish(alice, alice, seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500),
                ter(tecNO_PERMISSION));
            env(finish(bob, alice, seq), ter(tecNO_PERMISSION));
            env(finish(bob, alice, seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500),
                ter(tecNO_PERMISSION));
            env.close();

            // Cancel continues to not be possible. Finish is possible but
            // requires the fulfillment associated with the escrow.
            env(cancel(alice, alice, seq), ter(tecNO_PERMISSION));
            env(cancel(bob, alice, seq), ter(tecNO_PERMISSION));
            env(finish(bob, alice, seq), ter(tecCRYPTOCONDITION_ERROR));
            env(finish(alice, alice, seq), ter(tecCRYPTOCONDITION_ERROR));
            env.close();

            env(finish(bob, alice, seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500));
        }
        {
            // Self-escrowed conditional with DepositAuth.
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            auto const seq = env.seq(alice);
            env(escrow(alice, alice, USD(1000)),
                condition(cb3),
                finish_time(env.now() + 5s));

            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env.require(balance(alice, XRP(5000) - drops(10)));
            BEAST_EXPECT(preLocked == USD(1000));
            env.require(balance(alice, USD(5000)));

            env.close();

            // Finish is now possible but requires the cryptocondition.
            env(finish(bob, alice, seq), ter(tecCRYPTOCONDITION_ERROR));
            env(finish(alice, alice, seq), ter(tecCRYPTOCONDITION_ERROR));

            // Enable deposit authorization. After this only Alice can finish
            // the escrow.
            env(fset(alice, asfDepositAuth));
            env.close();

            env(finish(alice, alice, seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(cb3),
                fulfillment(fb3),
                fee(1500),
                ter(tecNO_PERMISSION));
            env(finish(alice, alice, seq),
                condition(cb3),
                fulfillment(fb3),
                fee(1500));
        }
        {
            // Self-escrowed conditional with DepositAuth and DepositPreauth.
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();

            auto const seq = env.seq(alice);
            env(escrow(alice, alice, USD(1000)),
                condition(cb3),
                finish_time(env.now() + 5s));

            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env.require(balance(alice, XRP(5000) - drops(10)));
            BEAST_EXPECT(preLocked == USD(1000));
            env.require(balance(alice, USD(5000)));

            env.close();

            // Alice preauthorizes Zelda for deposit, even though Alice has
            // not
            // set the lsfDepositAuth flag (yet).
            env(deposit::auth(alice, carol));
            env.close();

            // Finish is now possible but requires the cryptocondition.
            env(finish(alice, alice, seq), ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq), ter(tecCRYPTOCONDITION_ERROR));
            env(finish(carol, alice, seq), ter(tecCRYPTOCONDITION_ERROR));

            // Alice enables deposit authorization. After this only Alice or
            // Zelda (because Zelda is preauthorized) can finish the escrow.
            env(fset(alice, asfDepositAuth));
            env.close();

            env(finish(alice, alice, seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(cb3),
                fulfillment(fb3),
                fee(1500),
                ter(tecNO_PERMISSION));
            env(finish(carol, alice, seq),
                condition(cb3),
                fulfillment(fb3),
                fee(1500));
        }
    }

    void
    testICEscrowConditions(FeatureBitset features)
    {
        testcase("IC Escrow with CryptoConditions");

        using namespace jtx;
        using namespace std::chrono;

        {  // Test cryptoconditions
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();

            auto const seq = env.seq(alice);
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 1);
            env(escrow(alice, carol, USD(1000)),
                condition(cb1),
                cancel_time(env.now() + 1s));

            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2);
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env.require(balance(alice, XRP(5000) - drops(10)));
            BEAST_EXPECT(preLocked == USD(1000));
            env.require(balance(alice, USD(5000)));
            env.require(balance(carol, USD(5000)));
            env(cancel(bob, alice, seq), ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2);

            // Attempt to finish without a fulfillment
            env(finish(bob, alice, seq), ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2);

            // Attempt to finish with a condition instead of a fulfillment
            env(finish(bob, alice, seq),
                condition(cb1),
                fulfillment(cb1),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2);
            env(finish(bob, alice, seq),
                condition(cb1),
                fulfillment(cb2),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2);
            env(finish(bob, alice, seq),
                condition(cb1),
                fulfillment(cb3),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2);

            // Attempt to finish with an incorrect condition and various
            // combinations of correct and incorrect fulfillments.
            env(finish(bob, alice, seq),
                condition(cb2),
                fulfillment(fb1),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2);
            env(finish(bob, alice, seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2);
            env(finish(bob, alice, seq),
                condition(cb2),
                fulfillment(fb3),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2);

            // Attempt to finish with the correct condition & fulfillment
            env(finish(bob, alice, seq),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));

            // SLE removed on finish
            BEAST_EXPECT(!env.le(keylet::escrow(Account(alice).id(), seq)));
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 1);
            env.require(balance(carol, USD(6000)));
            env(cancel(bob, alice, seq), ter(tecNO_TARGET));
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 1);
            env(cancel(bob, carol, 1), ter(tecNO_TARGET));
        }
        {  // Test cancel when condition is present
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();

            auto const seq = env.seq(alice);
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 1);
            env(escrow(alice, carol, USD(1000)),
                condition(cb2),
                cancel_time(env.now() + 1s));
            env.close();
            auto const preLocked = -lockedAmount(env, alice, gw, USD);
            env.require(balance(alice, XRP(5000) - drops(10)));
            BEAST_EXPECT(preLocked == USD(1000));
            env.require(balance(alice, USD(5000)));
            // balance restored on cancel
            env(cancel(bob, alice, seq));

            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            env.require(balance(alice, XRP(5000) - drops(10)));
            BEAST_EXPECT(postLocked == USD(0));
            env.require(balance(alice, USD(5000)));
            // SLE removed on cancel
            BEAST_EXPECT(!env.le(keylet::escrow(Account(alice).id(), seq)));
        }
        {
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();
            auto const seq = env.seq(alice);
            env(escrow(alice, carol, USD(1000)),
                condition(cb3),
                cancel_time(env.now() + 1s));
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2);
            // cancel fails before expiration
            env(cancel(bob, alice, seq), ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2);
            env.close();
            // finish fails after expiration
            env(finish(bob, alice, seq),
                condition(cb3),
                fulfillment(fb3),
                fee(1500),
                ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le(alice))[sfOwnerCount] == 2);
            env.require(balance(carol, USD(5000)));
        }
        {  // Test long & short conditions during creation
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();

            std::vector<std::uint8_t> v;
            v.resize(cb1.size() + 2, 0x78);
            std::memcpy(v.data() + 1, cb1.data(), cb1.size());

            auto const p = v.data();
            auto const s = v.size();

            auto const ts = env.now() + 1s;

            // All these are expected to fail, because the
            // condition we pass in is malformed in some way
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{p, s}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{p, s - 1}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{p, s - 2}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{p + 1, s - 1}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{p + 1, s - 3}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{p + 2, s - 2}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{p + 2, s - 3}),
                cancel_time(ts),
                ter(temMALFORMED));

            auto const seq = env.seq(alice);
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{p + 1, s - 2}),
                cancel_time(ts),
                fee(100));
            env(finish(bob, alice, seq),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));

            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(0));
            env.require(balance(alice, XRP(5000) - drops(100)));
            env.require(balance(alice, USD(4000)));
            env.require(balance(bob, XRP(5000) - drops(1500)));
            env.require(balance(bob, USD(5000)));
            env.require(balance(carol, XRP(5000)));
            env.require(balance(carol, USD(6000)));
        }
        {  // Test long and short conditions & fulfillments during finish
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();

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
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{cp, cs}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{cp, cs - 1}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{cp, cs - 2}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{cp + 1, cs - 1}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{cp + 1, cs - 3}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{cp + 2, cs - 2}),
                cancel_time(ts),
                ter(temMALFORMED));
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{cp + 2, cs - 3}),
                cancel_time(ts),
                ter(temMALFORMED));

            auto const seq = env.seq(alice);
            env(escrow(alice, carol, USD(1000)),
                condition(Slice{cp + 1, cs - 2}),
                cancel_time(ts),
                fee(100));

            // Now, try to fulfill using the same sequence of
            // malformed conditions.
            env(finish(bob, alice, seq),
                condition(Slice{cp, cs}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{cp, cs - 1}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{cp, cs - 2}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{cp + 1, cs - 1}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{cp + 1, cs - 3}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{cp + 2, cs - 2}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{cp + 2, cs - 3}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));

            // Now, using the correct condition, try malformed fulfillments:
            env(finish(bob, alice, seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp, fs}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp, fs - 1}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp, fs - 2}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 1, fs - 1}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 1, fs - 3}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 1, fs - 3}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 2, fs - 2}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{cp + 1, cs - 2}),
                fulfillment(Slice{fp + 2, fs - 3}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));

            // Now try for the right one
            env(finish(bob, alice, seq),
                condition(cb2),
                fulfillment(fb2),
                fee(1500));

            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(0));
            env.require(balance(alice, XRP(5000) - drops(100)));
            env.require(balance(alice, USD(4000)));
            env.require(balance(carol, XRP(5000)));
            env.require(balance(carol, USD(6000)));
        }
        {  // Test empty condition during creation and
           // empty condition & fulfillment during finish
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();

            env(escrow(alice, carol, USD(1000)),
                condition(Slice{}),
                cancel_time(env.now() + 1s),
                ter(temMALFORMED));

            auto const seq = env.seq(alice);
            env(escrow(alice, carol, USD(1000)),
                condition(cb3),
                cancel_time(env.now() + 1s));

            env(finish(bob, alice, seq),
                condition(Slice{}),
                fulfillment(Slice{}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(cb3),
                fulfillment(Slice{}),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));
            env(finish(bob, alice, seq),
                condition(Slice{}),
                fulfillment(fb3),
                fee(1500),
                ter(tecCRYPTOCONDITION_ERROR));

            // Assemble finish that is missing the Condition or the Fulfillment
            // since either both must be present, or neither can:
            env(finish(bob, alice, seq), condition(cb3), ter(temMALFORMED));
            env(finish(bob, alice, seq), fulfillment(fb3), ter(temMALFORMED));

            // Now finish it.
            env(finish(bob, alice, seq),
                condition(cb3),
                fulfillment(fb3),
                fee(1500));

            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(0));
            env.require(balance(alice, XRP(5000) - drops(10)));
            env.require(balance(alice, USD(4000)));
            env.require(balance(carol, XRP(5000)));
            env.require(balance(carol, USD(6000)));
        }
        {  // Test a condition other than PreimageSha256, which
           // would require a separate amendment
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            std::array<std::uint8_t, 45> cb = {
                {0xA2, 0x2B, 0x80, 0x20, 0x42, 0x4A, 0x70, 0x49, 0x49,
                 0x52, 0x92, 0x67, 0xB6, 0x21, 0xB3, 0xD7, 0x91, 0x19,
                 0xD7, 0x29, 0xB2, 0x38, 0x2C, 0xED, 0x8B, 0x29, 0x6C,
                 0x3C, 0x02, 0x8F, 0xA9, 0x7D, 0x35, 0x0F, 0x6D, 0x07,
                 0x81, 0x03, 0x06, 0x34, 0xD2, 0x82, 0x02, 0x03, 0xC8}};

            // FIXME: this transaction should, eventually, return temDISABLED
            //        instead of temMALFORMED.
            env(escrow(alice, bob, USD(1000)),
                condition(cb),
                cancel_time(env.now() + 1s),
                ter(temMALFORMED));
        }
    }

    void
    testICMetaAndOwnership(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        {
            testcase("IC Metadata to self");

            Env env{*this, features};
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bob);

            env(escrow(alice, alice, USD(1000)),
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
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), aa) != aod.end());
            }

            env(escrow(bob, bob, USD(1000)),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 2s));
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
            env(finish(alice, alice, aseq));
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
            env(cancel(bob, bob, bseq));
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
            testcase("IC Metadata to other");

            Env env{*this, features};
            env.fund(XRP(5000), alice, bob, carol, gw);
            env.close();
            env.trust(USD(10000), alice, bob, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const bseq = env.seq(bob);

            env(escrow(alice, bob, USD(1000)), finish_time(env.now() + 1s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow(bob, carol, USD(1000)),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 2s));
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
            env(finish(alice, alice, aseq));
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
            env(cancel(bob, bob, bseq));
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
    testICConsequences(FeatureBitset features)
    {
        testcase("IC Consequences");

        using namespace jtx;
        using namespace std::chrono;
        Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        env.fund(XRP(5000), alice, bob, carol, gw);
        env.close();
        env.trust(USD(10000), alice, bob, carol);
        env.close();
        env(pay(gw, alice, USD(5000)));
        env(pay(gw, bob, USD(5000)));
        env(pay(gw, carol, USD(5000)));
        env.close();

        env.memoize(alice);
        env.memoize(bob);
        env.memoize(carol);

        {
            auto const jtx = env.jt(
                escrow(alice, carol, USD(1000)),
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
            BEAST_EXPECT(pf.consequences.potentialSpend().value() == 0);
        }

        {
            auto const jtx = env.jt(cancel(bob, alice, 3), seq(1), fee(10));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(10));
            BEAST_EXPECT(pf.consequences.potentialSpend().value() == 0);
        }

        {
            auto const jtx = env.jt(finish(bob, alice, 3), seq(1), fee(10));
            auto const pf = preflight(
                env.app(),
                env.current()->rules(),
                *jtx.stx,
                tapNONE,
                env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            BEAST_EXPECT(!pf.consequences.isBlocker());
            BEAST_EXPECT(pf.consequences.fee() == drops(10));
            BEAST_EXPECT(pf.consequences.potentialSpend().value() == 0);
        }
    }

    void
    testICEscrowWithTickets(FeatureBitset features)
    {
        testcase("IC Escrow with tickets");

        using namespace jtx;
        using namespace std::chrono;
        auto const alice = Account{"alice"};
        auto const bob = Account{"bob"};
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        {
            // Create escrow and finish using tickets.
            Env env{*this, features};
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
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
            env(escrow(alice, bob, USD(1000)),
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
            Env env{*this, features};
            env.fund(XRP(5000), alice, bob, gw);
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
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
            env(escrow(alice, bob, USD(1000)),
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
    testICRippleState(FeatureBitset features)
    {
        testcase("IC RippleState");
        using namespace test::jtx;
        using namespace std::literals;

        // src > dst
        // src > issuer
        // dest no trustline
        // negative locked/tl balance
        {
            auto const src = Account("alice2");
            auto const dst = Account("bob0");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // src can create escrow
            auto const seq1 = env.seq(src);
            auto const delta = USD(1000);
            env(escrow(src, dst, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            auto const preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == -delta);

            // dst can finish escrow
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);

            env(finish(dst, src, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(
                lineBalance(env, src, gw, USD) == preSrc.value() + delta);
            BEAST_EXPECT(
                lineBalance(env, dst, gw, USD) == preDst.value() - delta);
            BEAST_EXPECT(preLocked == postLocked - delta);
        }
        // src < dst
        // src < issuer
        // dest no trustline
        // positive locked/tl balance
        {
            auto const src = Account("carol0");
            auto const dst = Account("dan1");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // src can create escrow
            auto const seq1 = env.seq(src);
            auto const delta = USD(1000);
            env(escrow(src, dst, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            auto const preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == delta);

            // dst can finish escrow
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);

            env(finish(dst, src, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(
                lineBalance(env, src, gw, USD) == preSrc.value() - delta);
            BEAST_EXPECT(
                lineBalance(env, dst, gw, USD) == preDst.value() + delta);
            BEAST_EXPECT(preLocked == postLocked + delta);
        }
        // dst > src
        // dst > issuer
        // dest no trustline
        // negative locked/tl balance
        {
            auto const src = Account("dan1");
            auto const dst = Account("alice2");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // src can create escrow
            auto const seq1 = env.seq(src);
            auto const delta = USD(1000);
            env(escrow(src, dst, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            auto const preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == -delta);

            // dst can finish escrow
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);

            env(finish(dst, src, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(
                lineBalance(env, src, gw, USD) == preSrc.value() + delta);
            BEAST_EXPECT(
                lineBalance(env, dst, gw, USD) == preDst.value() - delta);
            BEAST_EXPECT(preLocked == postLocked - delta);
        }
        // dst < src
        // dst < issuer
        // dest no trustline
        // positive locked/tl balance
        {
            auto const src = Account("bob0");
            auto const dst = Account("carol0");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // src can create escrow
            auto const seq1 = env.seq(src);
            auto const delta = USD(1000);
            env(escrow(src, dst, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            auto const preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == delta);

            // dst can finish escrow
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);

            env(finish(dst, src, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(
                lineBalance(env, src, gw, USD) == preSrc.value() - delta);
            BEAST_EXPECT(
                lineBalance(env, dst, gw, USD) == preDst.value() + delta);
            BEAST_EXPECT(preLocked == postLocked + delta);
        }
        // src > dst
        // src > issuer
        // dest has trustline
        // negative locked/tl balance
        {
            auto const src = Account("alice2");
            auto const dst = Account("bob0");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src, dst);
            env.close();
            env(pay(gw, src, USD(10000)));
            env(pay(gw, dst, USD(10000)));
            env.close();

            // src can create escrow
            auto const seq1 = env.seq(src);
            auto const delta = USD(1000);
            env(escrow(src, dst, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            auto const preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == -delta);

            // dst can finish escrow
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);

            env(finish(dst, src, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(
                lineBalance(env, src, gw, USD) == preSrc.value() + delta);
            BEAST_EXPECT(
                lineBalance(env, dst, gw, USD) == preDst.value() - delta);
            BEAST_EXPECT(preLocked == postLocked - delta);
        }
        // src < dst
        // src < issuer
        // dest has trustline
        // positive locked/tl balance
        {
            auto const src = Account("carol0");
            auto const dst = Account("dan1");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src, dst);
            env.close();
            env(pay(gw, src, USD(10000)));
            env(pay(gw, dst, USD(10000)));
            env.close();

            // src can create escrow
            auto const seq1 = env.seq(src);
            auto const delta = USD(1000);
            env(escrow(src, dst, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            auto const preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == delta);

            // dst can finish escrow
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);

            env(finish(dst, src, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(
                lineBalance(env, src, gw, USD) == preSrc.value() - delta);
            BEAST_EXPECT(
                lineBalance(env, dst, gw, USD) == preDst.value() + delta);
            BEAST_EXPECT(preLocked == postLocked + delta);
        }
        // dst > src
        // dst > issuer
        // dest has trustline
        // negative locked/tl balance
        {
            auto const src = Account("dan1");
            auto const dst = Account("alice2");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src, dst);
            env.close();
            env(pay(gw, src, USD(10000)));
            env(pay(gw, dst, USD(10000)));
            env.close();

            // src can create escrow
            auto const seq1 = env.seq(src);
            auto const delta = USD(1000);
            env(escrow(src, dst, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            auto const preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == -delta);

            // dst can finish escrow
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);

            env(finish(dst, src, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(
                lineBalance(env, src, gw, USD) == preSrc.value() + delta);
            BEAST_EXPECT(
                lineBalance(env, dst, gw, USD) == preDst.value() - delta);
            BEAST_EXPECT(preLocked == postLocked - delta);
        }
        // dst < src
        // dst < issuer
        // dest has trustline
        // positive locked/tl balance
        {
            auto const src = Account("bob0");
            auto const dst = Account("carol0");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, dst, gw);
            env.close();
            env.trust(USD(100000), src, dst);
            env.close();
            env(pay(gw, src, USD(10000)));
            env(pay(gw, dst, USD(10000)));
            env.close();

            // src can create escrow
            auto const seq1 = env.seq(src);
            auto const delta = USD(1000);
            env(escrow(src, dst, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            auto const preLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(preLocked == delta);

            // dst can finish escrow
            auto const preSrc = lineBalance(env, src, gw, USD);
            auto const preDst = lineBalance(env, dst, gw, USD);

            env(finish(dst, src, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            auto postLocked = lockedAmount(env, src, gw, USD);
            BEAST_EXPECT(
                lineBalance(env, src, gw, USD) == preSrc.value() - delta);
            BEAST_EXPECT(
                lineBalance(env, dst, gw, USD) == preDst.value() + delta);
            BEAST_EXPECT(preLocked == postLocked + delta);
        }
    }

    void
    testICGateway(FeatureBitset features)
    {
        testcase("IC Gateway");
        using namespace test::jtx;
        using namespace std::literals;

        // test escrow with issuer
        // src > issuer
        // no dest trustline
        // negative locked/tl balance
        {
            auto const src = Account("alice2");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, gw);
            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(gw);
            auto const preSrc = lineBalance(env, src, gw, USD);
            env(escrow(gw, src, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // src can finish escrow, no dest trustline
            env(finish(src, gw, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            BEAST_EXPECT(preSrc == USD(0));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == -USD(1000));
        }
        // test escrow with issuer
        // src < issuer
        // no dest trustline
        // positive locked/tl balance
        {
            auto const src = Account("carol0");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, gw);
            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(gw);
            auto const preSrc = lineBalance(env, src, gw, USD);
            env(escrow(gw, src, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // src can finish escrow, no dest trustline
            env(finish(src, gw, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            BEAST_EXPECT(preSrc == USD(0));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == USD(1000));
        }
        // test escrow with issuer
        // dst > issuer
        // no dest trustline
        // negative locked/tl balance
        {
            auto const src = Account("alice2");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, gw);
            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(gw);
            auto const preSrc = lineBalance(env, src, gw, USD);
            env(escrow(gw, src, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // src can finish escrow, no dest trustline
            env(finish(src, gw, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            BEAST_EXPECT(preSrc == USD(0));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == -USD(1000));
        }
        // test escrow with issuer, no dest trustline
        // dst < issuer
        // no dest trustline
        // positive locked/tl balance
        {
            auto const src = Account("carol0");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, gw);
            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(gw);
            auto const preSrc = lineBalance(env, src, gw, USD);
            env(escrow(gw, src, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // src can finish escrow, no dest trustline
            env(finish(src, gw, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            BEAST_EXPECT(preSrc == USD(0));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == USD(1000));
        }
        // test escrow with issuer
        // src > issuer
        // dest has trustline
        // negative locked/tl balance
        {
            auto const src = Account("alice2");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(gw);
            auto const preSrc = lineBalance(env, src, gw, USD);
            env(escrow(gw, src, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // src can finish escrow, no dest trustline
            env(finish(src, gw, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            BEAST_EXPECT(preSrc == -USD(10000));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == -USD(11000));
        }
        // test escrow with issuer
        // src < issuer
        // dest has trustline
        // positive locked/tl balance
        {
            auto const src = Account("carol0");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(gw);
            auto const preSrc = lineBalance(env, src, gw, USD);
            env(escrow(gw, src, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // src can finish escrow, no dest trustline
            env(finish(src, gw, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            BEAST_EXPECT(preSrc == USD(10000));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == USD(11000));
        }
        // test escrow with issuer
        // dst > issuer
        // dest has trustline
        // negative locked/tl balance
        {
            auto const src = Account("alice2");
            auto const gw = Account{"gw0"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(gw);
            auto const preSrc = lineBalance(env, src, gw, USD);
            env(escrow(gw, src, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // src can finish escrow, no dest trustline
            env(finish(src, gw, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            BEAST_EXPECT(preSrc == -USD(10000));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == -USD(11000));
        }
        // test escrow with issuer, no dest trustline
        // dst < issuer
        // dest has trustline
        // positive locked/tl balance
        {
            auto const src = Account("carol0");
            auto const gw = Account{"gw1"};
            auto const USD = gw["USD"];

            Env env{*this, features};
            env.fund(XRP(5000), src, gw);
            env.close();
            env.trust(USD(100000), src);
            env.close();
            env(pay(gw, src, USD(10000)));
            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(gw);
            auto const preSrc = lineBalance(env, src, gw, USD);
            env(escrow(gw, src, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // src can finish escrow, no dest trustline
            env(finish(src, gw, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            BEAST_EXPECT(preSrc == USD(10000));
            BEAST_EXPECT(lineBalance(env, src, gw, USD) == USD(11000));
        }
    }

    void
    testICLockedRate(FeatureBitset features)
    {
        testcase("IC Locked Rate");
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
            env.fund(XRP(10000), alice, bob, gw);
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD.issue());
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            auto const transferRate = escrowRate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1000000000 * 1.25));

            // bob can finish escrow
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(0));
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == USD(10100));
        }
        // test rate change - higher
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD.issue());
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            auto transferRate = escrowRate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1000000000 * 1.25));

            // issuer changes rate higher
            env(rate(gw, 1.26));
            env.close();

            // bob can finish escrow - rate unchanged
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(0));
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == USD(10100));
        }
        // test rate change - lower
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD.issue());
            auto const seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            auto transferRate = escrowRate(env, alice, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1000000000 * 1.25));

            // issuer changes rate higher
            env(rate(gw, 1.00));
            env.close();

            // bob can finish escrow - rate changed
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(0));
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD.issue()) == USD(10125));
        }
        // test issuer doesnt pay own rate
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // issuer with rate can create escrow
            auto const preAlice = env.balance(alice, USD.issue());
            auto const seq1 = env.seq(gw);
            auto const delta = USD(125);
            env(escrow(gw, alice, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            auto transferRate = escrowRate(env, gw, seq1);
            BEAST_EXPECT(
                transferRate.value == std::uint32_t(1000000000 * 1.25));

            // issuer can finish escrow - no rate charged
            env(finish(alice, gw, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            auto const postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(0));
            BEAST_EXPECT(env.balance(alice, USD.issue()) == preAlice + delta);
            BEAST_EXPECT(env.balance(alice, USD.issue()) == USD(10125));
        }
    }

    void
    testICTLRequireAuth(FeatureBitset features)
    {
        testcase("IC Trustline Require Auth");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        auto const aliceUSD = alice["USD"];
        auto const bobUSD = bob["USD"];

        // test asfRequireAuth
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, gw);
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, aliceUSD(10000)), txflags(tfSetfAuth));
            env(trust(alice, USD(10000)));
            env.close();
            env(pay(gw, alice, USD(1000)));
            env.close();

            // alice cannot create escrow - fails without auth
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecNO_AUTH));
            env.close();

            // set auth on bob
            env(trust(gw, bobUSD(10000)), txflags(tfSetfAuth));
            env(trust(bob, USD(10000)));
            env.close();
            env(pay(gw, bob, USD(1000)));
            env.close();

            // alice cannot create escrow - bob has auth
            seq1 = env.seq(alice);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // bob can finish
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
        }
    }

    void
    testICTLFreeze(FeatureBitset features)
    {
        testcase("IC Trustline Freeze");
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
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // setup transaction
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);

            // create escrow fails - frozen trustline
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecFROZEN));
            env.close();

            // clear global freeze
            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // create escrow success
            seq1 = env.seq(alice);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // set global freeze
            env(fset(gw, asfGlobalFreeze));
            env.close();

            // bob finish escrow fails - frozen trustline
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500),
                ter(tecFROZEN));
            env.close();

            // clear global freeze
            env(fclear(gw, asfGlobalFreeze));
            env.close();

            // bob finish escrow success
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
        }
        // test Individual Freeze
        {
            // Env Setup
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env(trust(alice, USD(100000)));
            env(trust(bob, USD(100000)));
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // set freeze on alice trustline
            env(trust(gw, USD(10000), alice, tfSetFreeze));
            env.close();

            // setup transaction
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);

            // create escrow fails - frozen trustline
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecFROZEN));
            env.close();

            // clear freeze on alice trustline
            env(trust(gw, USD(10000), alice, tfClearFreeze));
            env.close();

            // create escrow success
            seq1 = env.seq(alice);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // set freeze on bob trustline
            env(trust(gw, USD(10000), bob, tfSetFreeze));
            env.close();

            // bob finish escrow fails - frozen trustline
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500),
                ter(tecFROZEN));
            env.close();

            // clear freeze on bob trustline
            env(trust(gw, USD(10000), bob, tfClearFreeze));
            env.close();

            // bob finish escrow success
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
        }
    }
    void
    testICTLINSF(FeatureBitset features)
    {
        testcase("IC Trustline Insuficient Funds");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        {
            // test pay more than locked amount
            // ie. has 10000, lock 1000 then try to pay 10000
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // create escrow success
            auto const delta = USD(1000);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            BEAST_EXPECT(-lockedAmount(env, alice, gw, USD) == delta);
            env(pay(alice, gw, USD(10000)), ter(tecPATH_PARTIAL));
        }
        {
            // test lock more than balance + locked
            // ie. has 10000 lock 1000 then try to lock 10000
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            auto const delta = USD(1000);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            BEAST_EXPECT(-lockedAmount(env, alice, gw, USD) == delta);
            env(escrow(alice, bob, USD(10000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }
    }

    void
    testICPrecisionLoss(FeatureBitset features)
    {
        testcase("IC Precision Loss");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test max trustline precision loss
        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000000000000000), alice);
            env.trust(USD(100000000000000000), bob);
            env.close();
            env(pay(gw, alice, USD(10000000000000000)));
            env(pay(gw, bob, USD(10000000000000000)));
            env.close();

            // setup tx
            auto const seq1 = env.seq(alice);
            auto const delta = USD(10000000000000000);

            // create escrow success
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // create escrow fails - precision loss
            env(escrow(alice, bob, USD(1)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecPRECISION_LOSS));
            env.close();

            // bob finish escrow success
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            auto postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(0));
            // alice CAN fund again
            env(pay(gw, alice, USD(1)));
            env.close();
            // create escrow success
            env(escrow(alice, bob, USD(1)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();
            postLocked = -lockedAmount(env, alice, gw, USD);
            BEAST_EXPECT(postLocked == USD(1));
        }
        // test min create precision loss
        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env.close();
            env.trust(USD(100000000000000000), alice);
            env.trust(USD(100000000000000000), bob);
            env.close();
            env(pay(gw, alice, USD(10000000000000000)));
            env(pay(gw, bob, USD(1)));
            env.close();

            // alice cannot create escrow for 1/10/100 token - precision loss
            env(escrow(alice, bob, USD(1)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecPRECISION_LOSS));
            env(escrow(alice, bob, USD(10)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecPRECISION_LOSS));
            env(escrow(alice, bob, USD(100)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecPRECISION_LOSS));
            env.close();

            auto const seq1 = env.seq(alice);
            // alice can create escrow for 1000 token
            env(escrow(alice, bob, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // bob finish escrow success
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
        }
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testEnablement(features);
        testTiming(features);
        testTags(features);
        testDisallowXRP(features);
        test1571(features);
        testFails(features);
        testLockup(features);
        testEscrowConditions(features);
        testMetaAndOwnership(features);
        testConsequences(features);
        testEscrowWithTickets(features);
        testICEnablement(features);
        testICTiming(features);
        testICTags(features);
        testICDisallowXRP(features);
        testIC1571(features);
        testICFails(features);
        testICLockup(features);
        testICEscrowConditions(features);
        testICMetaAndOwnership(features);
        testICConsequences(features);
        testICEscrowWithTickets(features);
        testICRippleState(features);
        testICGateway(features);
        testICLockedRate(features);
        testICTLRequireAuth(features);
        testICTLFreeze(features);
        testICTLINSF(features);
        testICPrecisionLoss(features);
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{supported_amendments()};
        testWithFeats(all);
    }
};

BEAST_DEFINE_TESTSUITE(Escrow, app, ripple);

}  // namespace test
}  // namespace ripple
