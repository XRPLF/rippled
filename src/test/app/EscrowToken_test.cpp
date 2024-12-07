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
#include <xrpld/ledger/Dir.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/TxFlags.h>
#include <xrpl/protocol/jss.h>

#include <algorithm>
#include <iterator>

namespace ripple {
namespace test {

struct EscrowToken_test : public beast::unit_test::suite
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
    limitAmount(
        jtx::Env const& env,
        jtx::Account const& account,
        jtx::Account const& gw,
        jtx::IOU const& iou)
    {
        auto const aHigh = account.id() > gw.id();
        auto const sle = env.le(keylet::line(account, gw, iou.currency));
        if (sle && sle->isFieldPresent(aHigh ? sfLowLimit : sfHighLimit))
            return (*sle)[aHigh ? sfLowLimit : sfHighLimit];
        return STAmount(iou, 0);
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
    testTokenEnablement(FeatureBitset features)
    {
        testcase("Token Enablement");

        using namespace jtx;
        using namespace std::chrono;

        for (bool const withTokenEscrow : {false, true})
        {
            auto const amend =
                withTokenEscrow ? features : features - featureTokenEscrow;
            Env env{*this, amend};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            auto const createResult =
                withTokenEscrow ? ter(tesSUCCESS) : ter(temDISABLED);
            auto const finishResult =
                withTokenEscrow ? ter(tesSUCCESS) : ter(tecNO_TARGET);
            env(escrow(alice, bob, USD(1000)),
                finish_time(env.now() + 1s),
                createResult);
            env.close();

            auto const seq1 = env.seq(alice);

            env(escrow(alice, bob, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                createResult);
            env.close();
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500),
                finishResult);

            auto const seq2 = env.seq(alice);

            env(escrow(alice, bob, USD(1000)),
                condition(cb2),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 2s),
                fee(1500),
                createResult);
            env.close();
            env(cancel(bob, alice, seq2), fee(1500), finishResult);
        }
    }

    void
    testTokenTiming(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        {
            testcase("Timing: Token Finish Only");
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
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
            testcase("Timing: Token Cancel Only");
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
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
            testcase("Timing: Token Finish and Cancel -> Finish");
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
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
            testcase("Timing: Token Finish and Cancel -> Cancel");
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
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
    testTokenTags(FeatureBitset features)
    {
        testcase("Token Tags");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(5000), alice, bob, gw);
        env(fset(gw, asfAllowTokenLocking));
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
    testToken1571(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        {
            testcase("Token Implied Finish Time (without fix1571)");

            Env env(*this, supported_amendments() - fix1571);
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env(fset(gw, asfAllowTokenLocking));
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
            BEAST_EXPECT(env.balance(bob, USD) == USD(5100));
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
            BEAST_EXPECT(env.balance(bob, USD) == USD(5200));
        }

        {
            testcase("Token Implied Finish Time (with fix1571)");

            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const carol = Account("carol");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, carol, gw);
            env(fset(gw, asfAllowTokenLocking));
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
            BEAST_EXPECT(env.balance(bob, USD) == USD(5100));
        }
    }

    void
    testTokenFails(FeatureBitset features)
    {
        testcase("Token Failure Cases");

        using namespace jtx;
        using namespace std::chrono;

        Env env{*this, features};
        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        env.fund(XRP(5000), alice, bob, gw);
        env(fset(gw, asfAllowTokenLocking));
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

        env(fclear(gw, asfAllowTokenLocking));
        // issuer has not set sallow token locking
        env(escrow(alice, carol, USD(1000)),
            finish_time(env.now() + 1s),
            ter(tecNO_PERMISSION));

        env(fset(gw, asfAllowTokenLocking));

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
    testTokenLockup(FeatureBitset features)
    {
        testcase("Token Lockup");

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
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(10000), alice);
            env.trust(USD(10000), bob);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, bob, USD(5000)));
            env.close();

            auto const seq = env.seq(alice);
            env(escrow(alice, alice, USD(1000)), finish_time(env.now() + 5s));

            env.require(balance(alice, XRP(5000) - drops(10)));
            env.require(balance(alice, USD(4000)));

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
            env.require(balance(alice, USD(5000)));
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
            env(fset(gw, asfAllowTokenLocking));
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
            env.require(balance(alice, XRP(5000) - drops(10)));
            env.require(balance(alice, USD(4000)));

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

            env.require(balance(alice, XRP(5000) - drops(10)));
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
            env(fset(gw, asfAllowTokenLocking));
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
            env.require(balance(alice, XRP(5000) - drops(10)));
            env.require(balance(alice, USD(4000)));

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
            env(fset(gw, asfAllowTokenLocking));
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

            env.require(balance(alice, XRP(5000) - drops(10)));
            env.require(balance(alice, USD(4000)));

            env.close();

            // DepositPreauth allows Finish to succeed for either Zelda or
            // Bob. But Finish won't succeed for Alice since she is not
            // preauthorized.
            env(finish(alice, alice, seq), ter(tecNO_PERMISSION));
            env(finish(carol, alice, seq));
            env.close();

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
            env(fset(gw, asfAllowTokenLocking));
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

            env.require(balance(alice, XRP(5000) - drops(10)));
            env.require(balance(alice, USD(4000)));

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
            env.close();

            env.require(balance(alice, USD(5000)));
        }
        {
            // Self-escrowed conditional with DepositAuth.
            Env env{*this, features};
            auto const alice = Account("alice");
            auto const bob = Account("bob");
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            env.fund(XRP(5000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
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

            env.require(balance(alice, XRP(5000) - drops(10)));
            env.require(balance(alice, USD(4000)));

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
            env.close();

            env.require(balance(alice, USD(5000)));
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
            env(fset(gw, asfAllowTokenLocking));
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

            env.require(balance(alice, XRP(5000) - drops(10)));
            env.require(balance(alice, USD(4000)));

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
            env.close();

            env.require(balance(alice, USD(5000)));
        }
    }

    void
    testTokenEscrowConditions(FeatureBitset features)
    {
        testcase("Token Escrow with CryptoConditions");

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
            env(fset(gw, asfAllowTokenLocking));
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
            env.require(balance(alice, XRP(5000) - drops(10)));
            env.require(balance(alice, USD(4000)));
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
            env(fset(gw, asfAllowTokenLocking));
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
            env.require(balance(alice, XRP(5000) - drops(10)));
            env.require(balance(alice, USD(4000)));
            // balance restored on cancel
            env(cancel(bob, alice, seq));

            env.require(balance(alice, XRP(5000) - drops(10)));
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
            env(fset(gw, asfAllowTokenLocking));
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
            env(fset(gw, asfAllowTokenLocking));
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
            env(fset(gw, asfAllowTokenLocking));
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
            env(fset(gw, asfAllowTokenLocking));
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
            env(fset(gw, asfAllowTokenLocking));
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
    testTokenMetaAndOwnership(FeatureBitset features)
    {
        using namespace jtx;
        using namespace std::chrono;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        {
            testcase("Token Metadata to self");

            Env env{*this, features};
            env.fund(XRP(5000), alice, bob, carol, gw);
            env(fset(gw, asfAllowTokenLocking));
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

            {
                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 4);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), aa) != iod.end());
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

            {
                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 5);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bb) != iod.end());
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

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 4);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bb) != iod.end());
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

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bb) == iod.end());
            }
        }
        {
            testcase("Token Metadata to other");

            Env env{*this, features};
            env.fund(XRP(5000), alice, bob, carol, gw);
            env(fset(gw, asfAllowTokenLocking));
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

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 5);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ab) != iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bc) != iod.end());
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

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 4);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ab) == iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bc) != iod.end());
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

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ab) == iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), bc) == iod.end());
            }
        }

        {
            testcase("Token Metadata to issuer");

            Env env{*this, features};
            env.fund(XRP(5000), alice, carol, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(10000), alice, carol);
            env.close();
            env(pay(gw, alice, USD(5000)));
            env(pay(gw, carol, USD(5000)));
            env.close();
            auto const aseq = env.seq(alice);
            auto const gseq = env.seq(gw);

            env(escrow(alice, gw, USD(1000)), finish_time(env.now() + 1s));

            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);
            env(escrow(gw, carol, USD(1000)),
                finish_time(env.now() + 1s),
                cancel_time(env.now() + 2s));
            BEAST_EXPECT(
                (*env.meta())[sfTransactionResult] ==
                static_cast<std::uint8_t>(tesSUCCESS));
            env.close(5s);

            auto const ag = env.le(keylet::escrow(alice.id(), aseq));
            BEAST_EXPECT(ag);

            auto const gc = env.le(keylet::escrow(gw.id(), gseq));
            BEAST_EXPECT(gc);

            {
                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 2);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ag) != aod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);
                BEAST_EXPECT(
                    std::find(cod.begin(), cod.end(), gc) != cod.end());

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 4);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ag) != iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), gc) != iod.end());
            }

            env.close(5s);
            env(finish(alice, alice, aseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(env.le(keylet::escrow(gw.id(), gseq)));

                ripple::Dir aod(*env.current(), keylet::ownerDir(alice.id()));
                BEAST_EXPECT(std::distance(aod.begin(), aod.end()) == 1);
                BEAST_EXPECT(
                    std::find(aod.begin(), aod.end(), ag) == aod.end());

                ripple::Dir cod(*env.current(), keylet::ownerDir(carol.id()));
                BEAST_EXPECT(std::distance(cod.begin(), cod.end()) == 2);

                ripple::Dir iod(*env.current(), keylet::ownerDir(gw.id()));
                BEAST_EXPECT(std::distance(iod.begin(), iod.end()) == 3);
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), ag) == iod.end());
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), gc) != iod.end());
            }

            env.close(5s);
            env(cancel(gw, gw, gseq));
            {
                BEAST_EXPECT(!env.le(keylet::escrow(alice.id(), aseq)));
                BEAST_EXPECT(!env.le(keylet::escrow(gw.id(), gseq)));

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
                BEAST_EXPECT(
                    std::find(iod.begin(), iod.end(), gc) == iod.end());
            }
        }
    }

    void
    testTokenConsequences(FeatureBitset features)
    {
        testcase("Token Consequences");

        using namespace jtx;
        using namespace std::chrono;
        Env env{*this, features};

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        env.fund(XRP(5000), alice, bob, carol, gw);
        env(fset(gw, asfAllowTokenLocking));
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
    testTokenEscrowWithTickets(FeatureBitset features)
    {
        testcase("Token Escrow with tickets");

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
            env(fset(gw, asfAllowTokenLocking));
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
            env(fset(gw, asfAllowTokenLocking));
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
    testTokenRippleState(FeatureBitset features)
    {
        testcase("Token RippleState");
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
            // // dst > src && dst > issuer && dst no trustline
            {Account("dan1"), Account("alice2"), Account{"gw0"}, false, true},
            // // dst < src && dst < issuer && dst no trustline
            {Account("bob0"), Account("carol0"), Account{"gw1"}, false, false},
            // // src > dst && src > issuer && dst has trustline
            {Account("alice2"), Account("bob0"), Account{"gw0"}, true, true},
            // // src < dst && src < issuer && dst has trustline
            {Account("carol0"), Account("dan1"), Account{"gw1"}, true, false},
            // // dst > src && dst > issuer && dst has trustline
            {Account("dan1"), Account("alice2"), Account{"gw0"}, true, true},
            // // dst < src && dst < issuer && dst has trustline
            {Account("bob0"), Account("carol0"), Account{"gw1"}, true, false},
        }};

        for (auto const& t : tests)
        {
            Env env{*this, features};
            auto const USD = t.gw["USD"];
            env.fund(XRP(5000), t.src, t.dst, t.gw);
            env(fset(t.gw, asfAllowTokenLocking));
            env.close();

            if (t.hasTrustline)
                env.trust(USD(100000), t.src, t.dst);
            else
                env.trust(USD(100000), t.src);
            env.close();

            env(pay(t.gw, t.src, USD(10000)));
            if (t.hasTrustline)
                env(pay(t.gw, t.dst, USD(10000)));
            env.close();

            // src can create escrow
            auto const seq1 = env.seq(t.src);
            auto const delta = USD(1000);
            env(escrow(t.src, t.dst, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // dst can finish escrow
            auto const preSrc = lineBalance(env, t.src, t.gw, USD);
            auto const preDst = lineBalance(env, t.dst, t.gw, USD);

            env(finish(t.dst, t.src, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(lineBalance(env, t.src, t.gw, USD) == preSrc);
            BEAST_EXPECT(
                lineBalance(env, t.dst, t.gw, USD) ==
                (t.negative ? (preDst - delta) : (preDst + delta)));
        }
    }

    void
    testTokenGateway(FeatureBitset features)
    {
        testcase("Token Gateway");
        using namespace test::jtx;
        using namespace std::literals;

        struct TestAccountData
        {
            Account src;
            Account dst;
            bool hasTrustline;
            bool negative;
        };

        std::array<TestAccountData, 8> gwSrcTests = {{
            // src > dst && src > issuer && dst no trustline
            {Account("gw0"), Account{"alice2"}, false, true},
            // // src < dst && src < issuer && dst no trustline
            {Account("gw1"), Account{"carol0"}, false, false},
            // // // // // dst > src && dst > issuer && dst no trustline
            {Account("gw0"), Account{"dan1"}, false, true},
            // // // // // dst < src && dst < issuer && dst no trustline
            {Account("gw1"), Account{"bob0"}, false, false},
            // // // // src > dst && src > issuer && dst has trustline
            {Account("gw0"), Account{"alice2"}, true, true},
            // // // // src < dst && src < issuer && dst has trustline
            {Account("gw1"), Account{"carol0"}, true, false},
            // // // // dst > src && dst > issuer && dst has trustline
            {Account("gw0"), Account{"dan1"}, true, true},
            // // // // dst < src && dst < issuer && dst has trustline
            {Account("gw1"), Account{"bob0"}, true, false},
        }};

        for (auto const& t : gwSrcTests)
        {
            Env env{*this, features};
            auto const USD = t.src["USD"];
            env.fund(XRP(5000), t.dst, t.src);
            env(fset(t.src, asfAllowTokenLocking));
            env.close();

            if (t.hasTrustline)
                env.trust(USD(100000), t.dst);

            env.close();

            if (t.hasTrustline)
                env(pay(t.src, t.dst, USD(10000)));

            env.close();

            // issuer can create escrow
            auto const seq1 = env.seq(t.src);
            auto const preDst = lineBalance(env, t.dst, t.src, USD);
            env(escrow(t.src, t.dst, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // src can finish escrow, no dest trustline
            env(finish(t.dst, t.src, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            auto const preAmount = t.hasTrustline ? 10000 : 0;
            BEAST_EXPECT(
                preDst == (t.negative ? -USD(preAmount) : USD(preAmount)));
            auto const postAmount = t.hasTrustline ? 11000 : 1000;
            BEAST_EXPECT(
                lineBalance(env, t.dst, t.src, USD) ==
                (t.negative ? -USD(postAmount) : USD(postAmount)));
            BEAST_EXPECT(lineBalance(env, t.src, t.src, USD) == USD(0));
        }

        std::array<TestAccountData, 4> gwDstTests = {{
            // // // // src > dst && src > issuer && dst has trustline
            {Account("alice2"), Account{"gw0"}, true, true},
            // // // // src < dst && src < issuer && dst has trustline
            {Account("carol0"), Account{"gw1"}, true, false},
            // // // // dst > src && dst > issuer && dst has trustline
            {Account("dan1"), Account{"gw0"}, true, true},
            // // // // dst < src && dst < issuer && dst has trustline
            {Account("bob0"), Account{"gw1"}, true, false},
        }};

        for (auto const& t : gwDstTests)
        {
            Env env{*this, features};
            auto const USD = t.dst["USD"];
            env.fund(XRP(5000), t.dst, t.src);
            env(fset(t.dst, asfAllowTokenLocking));
            env.close();

            env.trust(USD(100000), t.src);
            env.close();

            env(pay(t.dst, t.src, USD(10000)));
            env.close();

            // issuer can receive escrow
            auto const seq1 = env.seq(t.src);
            auto const preSrc = lineBalance(env, t.src, t.dst, USD);
            env(escrow(t.src, t.dst, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // issuer can finish escrow, no dest trustline
            env(finish(t.dst, t.src, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            auto const preAmount = 10000;
            BEAST_EXPECT(
                preSrc == (t.negative ? -USD(preAmount) : USD(preAmount)));
            auto const postAmount = 9000;
            BEAST_EXPECT(
                lineBalance(env, t.src, t.dst, USD) ==
                (t.negative ? -USD(postAmount) : USD(postAmount)));
            BEAST_EXPECT(lineBalance(env, t.dst, t.dst, USD) == USD(0));
        }

        // issuer is source and destination
        {
            auto const gw = Account{"gateway"};
            auto const USD = gw["USD"];
            Env env{*this, features};
            env.fund(XRP(5000), gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();

            // issuer can receive escrow
            auto const seq1 = env.seq(gw);
            env(escrow(gw, gw, USD(1000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // issuer can finish escrow
            env(finish(gw, gw, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
        }
    }

    void
    testTokenLockedRate(FeatureBitset features)
    {
        testcase("Token Locked Rate");
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
            env(fset(gw, asfAllowTokenLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
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

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10100));
        }
        // test rate change - higher
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
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

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10100));
        }
        // test rate change - lower
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // alice can create escrow w/ xfer rate
            auto const preAlice = env.balance(alice, USD);
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

            BEAST_EXPECT(env.balance(alice, USD) == preAlice - delta);
            BEAST_EXPECT(env.balance(bob, USD) == USD(10125));
        }
        // test issuer doesnt pay own rate
        {
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env(rate(gw, 1.25));
            env.close();
            env.trust(USD(100000), alice);
            env.trust(USD(100000), bob);
            env.close();
            env(pay(gw, alice, USD(10000)));
            env(pay(gw, bob, USD(10000)));
            env.close();

            // issuer with rate can create escrow
            auto const preAlice = env.balance(alice, USD);
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

            // alice can finish escrow - no rate charged
            env(finish(alice, gw, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();

            BEAST_EXPECT(env.balance(alice, USD) == preAlice + delta);
            BEAST_EXPECT(env.balance(alice, USD) == USD(10125));
        }
    }

    void
    testTokenTLLimitAmount(FeatureBitset features)
    {
        testcase("Token Trustline Limit");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test LimitAmount
        {
            Env env{*this, features};
            env.fund(XRP(1000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(10000), alice, bob);
            env.close();
            env(pay(gw, alice, USD(1000)));
            env(pay(gw, bob, USD(1000)));
            env.close();

            // alice can create escrow
            auto seq1 = env.seq(alice);
            auto const delta = USD(125);
            env(escrow(alice, bob, delta),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500));
            env.close();

            // bob can finish
            auto const preBobLimit = limitAmount(env, bob, gw, USD);
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
            auto const postBobLimit = limitAmount(env, bob, gw, USD);
            // bobs limit is NOT changed
            BEAST_EXPECT(postBobLimit == preBobLimit);
        }
    }

    void
    testTokenTLRequireAuth(FeatureBitset features)
    {
        testcase("Token Trustline Require Auth");
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
            env(fset(gw, asfAllowTokenLocking));
            env(fset(gw, asfRequireAuth));
            env.close();
            env(trust(gw, aliceUSD(10000)), txflags(tfSetfAuth));
            env(trust(alice, USD(10000)));
            env(trust(bob, USD(10000)));
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

            // alice can create escrow - bob has auth
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
    testTokenTLFreeze(FeatureBitset features)
    {
        testcase("Token Trustline Freeze");
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
            env(fset(gw, asfAllowTokenLocking));
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

            // bob finish escrow success regardless of frozen assets
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
            env(fset(gw, asfAllowTokenLocking));
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

            // bob finish escrow success regardless of frozen assets
            env(finish(bob, alice, seq1),
                condition(cb1),
                fulfillment(fb1),
                fee(1500));
            env.close();
        }
    }
    void
    testTokenTLINSF(FeatureBitset features)
    {
        testcase("Token Trustline Insuficient Funds");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const carol = Account("carol");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];
        {
            // test tecPATH_PARTIAL
            // ie. has 10000, escrow 1000 then try to pay 10000
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
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
            env(pay(alice, gw, USD(10000)), ter(tecPATH_PARTIAL));
        }
        {
            // test tecINSUFFICIENT_FUNDS
            // ie. has 10000 escrow 1000 then try to escrow 10000
            Env env{*this, features};
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
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

            env(escrow(alice, bob, USD(10000)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecINSUFFICIENT_FUNDS));
            env.close();
        }
    }

    void
    testTokenPrecisionLoss(FeatureBitset features)
    {
        testcase("Token Precision Loss");
        using namespace test::jtx;
        using namespace std::literals;

        auto const alice = Account("alice");
        auto const bob = Account("bob");
        auto const gw = Account{"gateway"};
        auto const USD = gw["USD"];

        // test min create precision loss
        {
            Env env(*this, features);
            env.fund(XRP(10000), alice, bob, gw);
            env(fset(gw, asfAllowTokenLocking));
            env.close();
            env.trust(USD(100000000000000000), alice);
            env.trust(USD(100000000000000000), bob);
            env.close();
            env(pay(gw, alice, USD(10000000000000000)));
            env(pay(gw, bob, USD(1)));
            env.close();

            // alice cannot create escrow for 1/10 iou - precision loss
            env(escrow(alice, bob, USD(1)),
                condition(cb1),
                finish_time(env.now() + 1s),
                fee(1500),
                ter(tecPRECISION_LOSS));
            env.close();

            auto const seq1 = env.seq(alice);
            // alice can create escrow for 1000 iou
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
        testTokenEnablement(features);
        testTokenTiming(features);
        testTokenTags(features);
        testToken1571(features);
        testTokenFails(features);
        testTokenLockup(features);
        testTokenEscrowConditions(features);
        testTokenMetaAndOwnership(features);
        testTokenConsequences(features);
        testTokenEscrowWithTickets(features);
        testTokenRippleState(features);
        testTokenGateway(features);
        testTokenLockedRate(features);
        testTokenTLLimitAmount(features);
        testTokenTLRequireAuth(features);
        testTokenTLFreeze(features);
        testTokenTLINSF(features);
        testTokenPrecisionLoss(features);
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

BEAST_DEFINE_TESTSUITE(EscrowToken, app, ripple);

}  // namespace test
}  // namespace ripple