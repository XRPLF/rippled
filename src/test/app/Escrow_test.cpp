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

#include <BeastConfig.h>
#include <test/jtx.h>
#include <ripple/app/tx/applySteps.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {
namespace test {

struct Escrow_test : public beast::unit_test::suite
{
    // A PreimageSha256 fulfillments and its associated condition.
    std::array<std::uint8_t, 4> const fb1 =
    {{
        0xA0, 0x02, 0x80, 0x00
    }};

    std::array<std::uint8_t, 39> const cb1 =
    {{
        0xA0, 0x25, 0x80, 0x20, 0xE3, 0xB0, 0xC4, 0x42, 0x98, 0xFC, 0x1C, 0x14,
        0x9A, 0xFB, 0xF4, 0xC8, 0x99, 0x6F, 0xB9, 0x24, 0x27, 0xAE, 0x41, 0xE4,
        0x64, 0x9B, 0x93, 0x4C, 0xA4, 0x95, 0x99, 0x1B, 0x78, 0x52, 0xB8, 0x55,
        0x81, 0x01, 0x00
    }};

    // Another PreimageSha256 fulfillments and its associated condition.
    std::array<std::uint8_t, 7> const fb2 =
    {{
        0xA0, 0x05, 0x80, 0x03, 0x61, 0x61, 0x61
    }};

    std::array<std::uint8_t, 39> const cb2 =
    {{
        0xA0, 0x25, 0x80, 0x20, 0x98, 0x34, 0x87, 0x6D, 0xCF, 0xB0, 0x5C, 0xB1,
        0x67, 0xA5, 0xC2, 0x49, 0x53, 0xEB, 0xA5, 0x8C, 0x4A, 0xC8, 0x9B, 0x1A,
        0xDF, 0x57, 0xF2, 0x8F, 0x2F, 0x9D, 0x09, 0xAF, 0x10, 0x7E, 0xE8, 0xF0,
        0x81, 0x01, 0x03
    }};

    // Another PreimageSha256 fulfillment and its associated condition.
    std::array<std::uint8_t, 8> const fb3 =
    {{
        0xA0, 0x06, 0x80, 0x04, 0x6E, 0x69, 0x6B, 0x62
    }};

    std::array<std::uint8_t, 39> const cb3 =
    {{
        0xA0, 0x25, 0x80, 0x20, 0x6E, 0x4C, 0x71, 0x45, 0x30, 0xC0, 0xA4, 0x26,
        0x8B, 0x3F, 0xA6, 0x3B, 0x1B, 0x60, 0x6F, 0x2D, 0x26, 0x4A, 0x2D, 0x85,
        0x7B, 0xE8, 0xA0, 0x9C, 0x1D, 0xFD, 0x57, 0x0D, 0x15, 0x85, 0x8B, 0xD4,
        0x81, 0x01, 0x04
    }};

    static
    Json::Value
    condpay (jtx::Account const& account, jtx::Account const& to,
        STAmount const& amount, Slice condition,
            NetClock::time_point const& cancelAfter)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = "EscrowCreate";
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv[jss::Destination] = to.human();
        jv[jss::Amount] = amount.getJson(0);
        jv["CancelAfter"] =
            cancelAfter.time_since_epoch().count();
        jv["Condition"] = strHex(condition);
        return jv;
    }

    static
    Json::Value
    condpay (jtx::Account const& account, jtx::Account const& to,
        STAmount const& amount, Slice condition,
            NetClock::time_point const& cancelAfter,
                NetClock::time_point const& finishAfter)
    {
        auto jv = condpay (account, to, amount, condition, cancelAfter);
        jv ["FinishAfter"] = finishAfter.time_since_epoch().count();
        return jv;
    }

    static
    Json::Value
    lockup (jtx::Account const& account, jtx::Account const& to,
        STAmount const& amount, NetClock::time_point const& expiry)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = "EscrowCreate";
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv[jss::Destination] = to.human();
        jv[jss::Amount] = amount.getJson(0);
        jv["FinishAfter"] =
            expiry.time_since_epoch().count();
        return jv;
    }

    static
    Json::Value
    lockup (jtx::Account const& account, jtx::Account const& to,
        STAmount const& amount, Slice condition,
            NetClock::time_point const& expiry)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = "EscrowCreate";
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv[jss::Destination] = to.human();
        jv[jss::Amount] = amount.getJson(0);
        jv["FinishAfter"] =
            expiry.time_since_epoch().count();
        jv["Condition"] = strHex(condition);
        return jv;
    }

    static
    Json::Value
    finish (jtx::Account const& account,
        jtx::Account const& from, std::uint32_t seq)
    {
        Json::Value jv;
        jv[jss::TransactionType] = "EscrowFinish";
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv["Owner"] = from.human();
        jv["OfferSequence"] = seq;
        return jv;
    }

    static
    Json::Value
    finish (jtx::Account const& account,
        jtx::Account const& from, std::uint32_t seq,
            Slice condition, Slice fulfillment)
    {
        Json::Value jv;
        jv[jss::TransactionType] = "EscrowFinish";
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv["Owner"] = from.human();
        jv["OfferSequence"] = seq;
        jv["Condition"] = strHex(condition);
        jv["Fulfillment"] = strHex(fulfillment);
        return jv;
    }

    static
    Json::Value
    cancel (jtx::Account const& account,
        jtx::Account const& from, std::uint32_t seq)
    {
        Json::Value jv;
        jv[jss::TransactionType] = "EscrowCancel";
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv["Owner"] = from.human();
        jv["OfferSequence"] = seq;
        return jv;
    }

    void
    testEnablement()
    {
        testcase ("Enablement");

        using namespace jtx;
        using namespace std::chrono;

        { // Escrow not enabled
            Env env(*this, no_features);
            env.fund(XRP(5000), "alice", "bob");
            env(lockup("alice", "bob", XRP(1000), env.now() + 1s), ter(temDISABLED));
            env(finish("bob", "alice", 1),                         ter(temDISABLED));
            env(cancel("bob", "alice", 1),                         ter(temDISABLED));
        }

        { // Escrow enabled
            Env env(*this, with_features(featureEscrow));
            env.fund(XRP(5000), "alice", "bob");
            env(lockup("alice", "bob", XRP(1000), env.now() + 1s));
            env.close();

            auto const seq = env.seq("alice");

            env(condpay("alice", "bob", XRP(1000),
                makeSlice (cb1), env.now() + 1s), fee(1500));
            env(finish("bob", "alice", seq,
                makeSlice(cb1), makeSlice(fb1)), fee(1500));
        }
    }

    void
    testTags()
    {
        testcase ("Tags");

        using namespace jtx;
        using namespace std::chrono;

        Env env(*this, with_features(featureEscrow));

        auto const alice = Account("alice");
        env.fund(XRP(5000), alice, "bob");

        auto const seq = env.seq(alice);
        // set source and dest tags
        env(condpay(alice, "bob", XRP(1000),
                makeSlice (cb1), env.now() + 1s),
            stag(1), dtag(2));
        auto const sle = env.le(keylet::escrow(alice.id(), seq));
        BEAST_EXPECT(sle);
        BEAST_EXPECT((*sle)[sfSourceTag] == 1);
        BEAST_EXPECT((*sle)[sfDestinationTag] == 2);
    }

    void
    testFails()
    {
        testcase ("Failure Cases");

        using namespace jtx;
        using namespace std::chrono;

        Env env(*this, with_features(featureEscrow));
        env.fund(XRP(5000), "alice", "bob");
        env.close();

        // Expiration in the past
        env(condpay("alice", "bob", XRP(1000),
            makeSlice(cb1), env.now() - 1s),            ter(tecNO_PERMISSION));

        // no destination account
        env(condpay("alice", "carol", XRP(1000),
            makeSlice(cb1), env.now() + 1s),            ter(tecNO_DST));

        env.fund(XRP(5000), "carol");
        env(condpay("alice", "carol", XRP(1000),
            makeSlice(cb1), env.now() + 1s), stag(2));
        env(condpay("alice", "carol", XRP(1000),
            makeSlice(cb1), env.now() + 1s), stag(3), dtag(4));
        env(fset("carol", asfRequireDest));

        // missing destination tag
        env(condpay("alice", "carol", XRP(1000),
            makeSlice(cb1), env.now() + 1s),            ter(tecDST_TAG_NEEDED));
        env(condpay("alice", "carol", XRP(1000),
            makeSlice(cb1), env.now() + 1s), dtag(1));

        // Using non-XRP:
        env (lockup("alice", "carol", Account("alice")["USD"](500),
            env.now() + 1s),                            ter(temBAD_AMOUNT));

        // Sending zero or no XRP:
        env (lockup("alice", "carol", XRP(0),
            env.now() + 1s),                            ter(temBAD_AMOUNT));
        env (lockup("alice", "carol", XRP(-1000),
            env.now() + 1s),                            ter(temBAD_AMOUNT));

        // Fail if neither CancelAfter nor FinishAfter are specified:
        {
            auto j1 = lockup("alice", "carol", XRP(1), env.now() + 1s);
            j1.removeMember ("FinishAfter");
            env (j1, ter(temBAD_EXPIRATION));

            auto j2 = condpay("alice", "carol", XRP(1), makeSlice(cb1), env.now() + 1s);
            j2.removeMember ("CancelAfter");
            env (j2, ter(temBAD_EXPIRATION));
        }

        // Fail if FinishAfter has already passed:
        env (lockup("alice", "carol", XRP(1), env.now() - 1s), ter (tecNO_PERMISSION));

        // Both CancelAfter and FinishAfter
        env(condpay("alice", "carol", XRP(1), makeSlice(cb1),
            env.now() + 10s, env.now() + 10s), ter (temBAD_EXPIRATION));
        env(condpay("alice", "carol", XRP(1), makeSlice(cb1),
            env.now() + 10s, env.now() + 15s), ter (temBAD_EXPIRATION));

        // Fail if the sender wants to send more than he has:
        auto const accountReserve =
            drops(env.current()->fees().reserve);
        auto const accountIncrement =
            drops(env.current()->fees().increment);

        env.fund (accountReserve + accountIncrement + XRP(50), "daniel");
        env(lockup("daniel", "bob", XRP(51), env.now() + 1s), ter (tecUNFUNDED));

        env.fund (accountReserve + accountIncrement + XRP(50), "evan");
        env(lockup("evan", "bob", XRP(50), env.now() + 1s),   ter (tecUNFUNDED));

        env.fund (accountReserve, "frank");
        env(lockup("frank", "bob", XRP(1), env.now() + 1s),   ter (tecINSUFFICIENT_RESERVE));

        // Respect the "asfDisallowXRP" account flag:
        env.fund (accountReserve + accountIncrement, "george");
        env(fset("george", asfDisallowXRP));
        env(lockup("bob", "george", XRP(10), env.now() + 1s),  ter (tecNO_TARGET));

        { // Specify incorrect sequence number
            env.fund (XRP(5000), "hannah");
            auto const seq = env.seq("hannah");
            env(lockup("hannah", "hannah", XRP(10), env.now() + 1s));
            env(finish ("hannah", "hannah", seq + 7),         ter (tecNO_TARGET));
        }

        { // Try to specify a condition for a non-conditional payment
            env.fund (XRP(5000), "ivan");
            auto const seq = env.seq("ivan");

            auto j = lockup("ivan", "ivan", XRP(10), env.now() + 1s);
            j["CancelAfter"] = j.removeMember ("FinishAfter");
            env (j);
            env(finish("ivan", "ivan", seq,
                makeSlice(cb1), makeSlice(fb1)), fee(1500),   ter (tecCRYPTOCONDITION_ERROR));
        }
    }

    void
    testLockup()
    {
        testcase ("Lockup");

        using namespace jtx;
        using namespace std::chrono;

        { // Unconditional
            Env env(*this, with_features(featureEscrow));
            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(lockup("alice", "alice", XRP(1000), env.now() + 1s));
            env.require(balance("alice", XRP(4000) - drops(10)));

            env(cancel("bob", "alice", seq),                ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq),                ter(tecNO_PERMISSION));
            env.close();

            env(cancel("bob", "alice", seq),                ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq));
        }

        { // Conditional
            Env env(*this, with_features(featureEscrow));
            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(lockup("alice", "alice", XRP(1000), makeSlice(cb2), env.now() + 1s));
            env.require(balance("alice", XRP(4000) - drops(10)));

            env(cancel("bob", "alice", seq),                ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq),                ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq,
                makeSlice(cb2), makeSlice(fb2)), fee(1500), ter(tecNO_PERMISSION));
            env.close();

            env(cancel("bob", "alice", seq),                ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq),                ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq),                ter(tecCRYPTOCONDITION_ERROR));
            env.close();

            env(finish("bob", "alice", seq,
                makeSlice(cb2), makeSlice(fb2)), fee(1500));
        }
    }

    void
    testEscrowConditions()
    {
        testcase ("Escrow Conditions");

        using namespace jtx;
        using namespace std::chrono;
        using S = seconds;

        { // Test cryptoconditions
            Env env(*this,
                with_features(featureEscrow));
            auto T = [&env](NetClock::duration const& d)
                { return env.now() + d; };
            env.fund(XRP(5000), "alice", "bob", "carol");
            auto const seq = env.seq("alice");
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(condpay("alice", "carol", XRP(1000), makeSlice(cb1), T(S{1})));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env.require(balance("alice", XRP(4000) - drops(10)));
            env.require(balance("carol", XRP(5000)));
            env(cancel("bob", "alice", seq),                                 ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish without a fulfillment
            env(finish("bob", "alice", seq),                                 ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with a condition instead of a fulfillment
            env(finish("bob", "alice", seq, makeSlice(cb1), makeSlice(cb1)), fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq, makeSlice(cb1), makeSlice(cb2)), fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq, makeSlice(cb1), makeSlice(cb3)), fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with an incorrect condition and various
            // combinations of correct and incorrect fulfillments.
            env(finish("bob", "alice", seq, makeSlice(cb2), makeSlice(fb1)), fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq, makeSlice(cb2), makeSlice(fb2)), fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq, makeSlice(cb2), makeSlice(fb3)), fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);

            // Attempt to finish with the correct condition & fulfillment
            env(finish("bob", "alice", seq, makeSlice(cb1), makeSlice(fb1)), fee(1500));
            // SLE removed on finish
            BEAST_EXPECT(! env.le(keylet::escrow(Account("alice").id(), seq)));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env.require(balance("carol", XRP(6000)));
            env(cancel("bob", "alice", seq),                                ter(tecNO_TARGET));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(cancel("bob", "carol", 1),                                  ter(tecNO_TARGET));
            env.close();
        }

        { // Test cancel when condition is present
            Env env(*this,
                with_features(featureEscrow));
            auto T = [&env](NetClock::duration const& d)
                { return env.now() + d; };
            env.fund(XRP(5000), "alice", "bob", "carol");
            auto const seq = env.seq("alice");
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(condpay("alice", "carol", XRP(1000), makeSlice(cb2), T(S{1})));
            env.close();
            env.require(balance("alice", XRP(4000) - drops(10)));
            // balance restored on cancel
            env(cancel("bob", "alice", seq));
            env.require(balance("alice", XRP(5000) - drops(10)));
            // SLE removed on cancel
            BEAST_EXPECT(! env.le(keylet::escrow(Account("alice").id(), seq)));
        }

        {
            Env env(*this, with_features(featureEscrow));
            auto T = [&env](NetClock::duration const& d)
                { return env.now() + d; };
            env.fund(XRP(5000), "alice", "bob", "carol");
            env.close();
            auto const seq = env.seq("alice");
            env(condpay("alice", "carol", XRP(1000), makeSlice(cb3), T(S{1})));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            // cancel fails before expiration
            env(cancel("bob", "alice", seq),                                  ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env.close();
            // finish fails after expiration
            env(finish("bob", "alice", seq, makeSlice(cb3), makeSlice(fb3)),
                fee(1500),                                                    ter(tecNO_PERMISSION));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 1);
            env.require(balance("carol", XRP(5000)));
        }

        { // Test long & short conditions during creation
            Env env(*this, with_features(featureEscrow));
            auto T = [&env](NetClock::duration const& d)
                { return env.now() + d; };
            env.fund(XRP(5000), "alice", "bob", "carol");

            std::vector<std::uint8_t> v;
            v.resize(cb1.size() + 2, 0x78);
            std::memcpy (v.data() + 1, cb1.data(), cb1.size());

            auto const p = v.data();
            auto const s = v.size();

            // All these are expected to fail, because the
            // condition we pass in is malformed in some way
            env(condpay("alice", "carol", XRP(1000),
                Slice{p, s}, T(S{1})),              ter(temMALFORMED));
            env(condpay("alice", "carol", XRP(1000),
                Slice{p, s - 1}, T(S{1})),          ter(temMALFORMED));
            env(condpay("alice", "carol", XRP(1000),
                Slice{p, s - 2}, T(S{1})),          ter(temMALFORMED));
            env(condpay("alice", "carol", XRP(1000),
                Slice{p + 1, s - 1}, T(S{1})),      ter(temMALFORMED));
            env(condpay("alice", "carol", XRP(1000),
                Slice{p + 1, s - 3}, T(S{1})),      ter(temMALFORMED));
            env(condpay("alice", "carol", XRP(1000),
                Slice{p + 2, s - 2}, T(S{1})),      ter(temMALFORMED));
            env(condpay("alice", "carol", XRP(1000),
                Slice{p + 2, s - 3}, T(S{1})),      ter(temMALFORMED));

            auto const seq = env.seq("alice");
            env(condpay("alice", "carol", XRP(1000),
                Slice{p + 1, s - 2}, T(S{1})), fee(100));
            env(finish("bob", "alice", seq,
                makeSlice(cb1), makeSlice(fb1)), fee(1500));
            env.require(balance("alice", XRP(4000) - drops(100)));
            env.require(balance("bob", XRP(5000) - drops(1500)));
            env.require(balance("carol", XRP(6000)));
        }

        { // Test long and short conditions & fulfillments during finish
            Env env(*this,
                with_features(featureEscrow));
            auto T = [&env](NetClock::duration const& d)
                { return env.now() + d; };
            env.fund(XRP(5000), "alice", "bob", "carol");

            std::vector<std::uint8_t> cv;
            cv.resize(cb2.size() + 2, 0x78);
            std::memcpy (cv.data() + 1, cb2.data(), cb2.size());

            auto const cp = cv.data();
            auto const cs = cv.size();

            std::vector<std::uint8_t> fv;
            fv.resize(fb2.size() + 2, 0x13);
            std::memcpy(fv.data() + 1, fb2.data(), fb2.size());

            auto const fp = fv.data();
            auto const fs = fv.size();

            // All these are expected to fail, because the
            // condition we pass in is malformed in some way
            env(condpay("alice", "carol", XRP(1000),
                Slice{cp, cs}, T(S{1})),              ter(temMALFORMED));
            env(condpay("alice", "carol", XRP(1000),
                Slice{cp, cs - 1}, T(S{1})),          ter(temMALFORMED));
            env(condpay("alice", "carol", XRP(1000),
                Slice{cp, cs - 2}, T(S{1})),          ter(temMALFORMED));
            env(condpay("alice", "carol", XRP(1000),
                Slice{cp + 1, cs - 1}, T(S{1})),      ter(temMALFORMED));
            env(condpay("alice", "carol", XRP(1000),
                Slice{cp + 1, cs - 3}, T(S{1})),      ter(temMALFORMED));
            env(condpay("alice", "carol", XRP(1000),
                Slice{cp + 2, cs - 2}, T(S{1})),      ter(temMALFORMED));
            env(condpay("alice", "carol", XRP(1000),
                Slice{cp + 2, cs - 3}, T(S{1})),      ter(temMALFORMED));

            auto const seq = env.seq("alice");
            env(condpay("alice", "carol", XRP(1000),
                Slice{cp + 1, cs - 2}, T(S{1})), fee(100));

            // Now, try to fulfill using the same sequence of
            // malformed conditions.
            env(finish("bob", "alice", seq, Slice{cp, cs}, Slice{fp, fs}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, Slice{cp, cs - 1}, Slice{fp, fs}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, Slice{cp, cs - 2}, Slice{fp, fs}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, Slice{cp + 1, cs - 1}, Slice{fp, fs}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, Slice{cp + 1, cs - 3}, Slice{fp, fs}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, Slice{cp + 2, cs - 2}, Slice{fp, fs}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, Slice{cp + 2, cs - 3}, Slice{fp, fs}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));

            // Now, using the correct condition, try malformed
            // fulfillments:
            env(finish("bob", "alice", seq, Slice{cp + 1, cs - 2}, Slice{fp, fs}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, Slice{cp + 1, cs - 2}, Slice{fp, fs - 1}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, Slice{cp + 1, cs - 2}, Slice{fp, fs - 2}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, Slice{cp + 1, cs - 2}, Slice{fp + 1, fs - 1}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, Slice{cp + 1, cs - 2}, Slice{fp + 1, fs - 3}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, Slice{cp + 1, cs - 2}, Slice{fp + 1, fs - 3}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, Slice{cp + 1, cs - 2}, Slice{fp + 2, fs - 2}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, Slice{cp + 1, cs - 2}, Slice{fp + 2, fs - 3}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));

            // Now try for the right one
            env(finish("bob", "alice", seq,
                makeSlice(cb2), makeSlice(fb2)), fee(1500));
            env.require(balance("alice", XRP(4000) - drops(100)));
            env.require(balance("carol", XRP(6000)));
        }

        { // Test empty condition during creation and
          // empty condition & fulfillment during finish
            Env env(*this, with_features(featureEscrow));
            auto T = [&env](NetClock::duration const& d)
                { return env.now() + d; };
            env.fund(XRP(5000), "alice", "bob", "carol");

            env(condpay("alice", "carol", XRP(1000), {}, T(S{1})), ter(temMALFORMED));

            auto const seq = env.seq("alice");
            env(condpay("alice", "carol", XRP(1000), makeSlice(cb3), T(S{1})));

            env(finish("bob", "alice", seq, {}, {}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, makeSlice(cb3), {}),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));
            env(finish("bob", "alice", seq, {}, makeSlice(fb3)),
                fee(1500), ter(tecCRYPTOCONDITION_ERROR));

            auto correctFinish = finish("bob", "alice", seq,
                makeSlice(cb3), makeSlice(fb3));

            // Manually assemble finish that is missing the
            // Condition or the Fulfillment (either both must
            // be present, or neither can):
            {
                auto finishNoCondition = correctFinish;
                finishNoCondition.removeMember ("Condition");
                env (finishNoCondition, ter(temMALFORMED));

                auto finishNoFulfillment = correctFinish;
                finishNoFulfillment.removeMember ("Fulfillment");
                env (finishNoFulfillment, ter(temMALFORMED));
            }

            env(correctFinish, fee(1500));
            env.require(balance ("carol", XRP(6000)));
            env.require(balance ("alice", XRP(4000) - drops(10)));
        }

        { // Test a condition other than PreimageSha256, which
          // would require a separate amendment
            Env env(*this, with_features(featureEscrow));
            auto T = [&env](NetClock::duration const& d)
                { return env.now() + d; };
            env.fund(XRP(5000), "alice", "bob", "carol");

            std::array<std::uint8_t, 45> cb = 
            {{
                0xA2, 0x2B, 0x80, 0x20, 0x42, 0x4A, 0x70, 0x49, 0x49, 0x52,
                0x92, 0x67, 0xB6, 0x21, 0xB3, 0xD7, 0x91, 0x19, 0xD7, 0x29,
                0xB2, 0x38, 0x2C, 0xED, 0x8B, 0x29, 0x6C, 0x3C, 0x02, 0x8F,
                0xA9, 0x7D, 0x35, 0x0F, 0x6D, 0x07, 0x81, 0x03, 0x06, 0x34,
                0xD2, 0x82, 0x02, 0x03, 0xC8
            }};

            env(condpay("alice", "carol", XRP(1000), makeSlice(cb), T(S{1})),
                ter(temDISABLED));
        }
    }

    void
    testMeta()
    {
        testcase ("Metadata");

        using namespace jtx;
        using namespace std::chrono;
        Env env(*this, with_features(featureEscrow));

        env.fund(XRP(5000), "alice", "bob", "carol");
        env(condpay("alice", "carol", XRP(1000), makeSlice(cb1), env.now() + 1s));
        auto const m = env.meta();
        BEAST_EXPECT((*m)[sfTransactionResult] == tesSUCCESS);
    }

    void testConsequences()
    {
        testcase ("Consequences");

        using namespace jtx;
        using namespace std::chrono;
        Env env(*this, with_features(featureEscrow));

        env.memoize("alice");
        env.memoize("bob");
        env.memoize("carol");

        {
            auto const jtx = env.jt(
                condpay("alice", "carol", XRP(1000),
                    makeSlice(cb1), env.now() + 1s),
                seq(1), fee(10));
            auto const pf = preflight(env.app(), env.current()->rules(),
                *jtx.stx, tapNONE, env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            auto const conseq = calculateConsequences(pf);
            BEAST_EXPECT(conseq.category == TxConsequences::normal);
            BEAST_EXPECT(conseq.fee == drops(10));
            BEAST_EXPECT(conseq.potentialSpend == XRP(1000));
        }

        {
            auto const jtx = env.jt(cancel("bob", "alice", 3),
                seq(1), fee(10));
            auto const pf = preflight(env.app(), env.current()->rules(),
                *jtx.stx, tapNONE, env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            auto const conseq = calculateConsequences(pf);
            BEAST_EXPECT(conseq.category == TxConsequences::normal);
            BEAST_EXPECT(conseq.fee == drops(10));
            BEAST_EXPECT(conseq.potentialSpend == XRP(0));
        }

        {
            auto const jtx = env.jt(
                finish("bob", "alice", 3,
                    makeSlice(cb1), makeSlice(fb1)),
                seq(1), fee(10));
            auto const pf = preflight(env.app(), env.current()->rules(),
                *jtx.stx, tapNONE, env.journal);
            BEAST_EXPECT(pf.ter == tesSUCCESS);
            auto const conseq = calculateConsequences(pf);
            BEAST_EXPECT(conseq.category == TxConsequences::normal);
            BEAST_EXPECT(conseq.fee == drops(10));
            BEAST_EXPECT(conseq.potentialSpend == XRP(0));
        }
    }

    void run() override
    {
        testEnablement();
        testTags();
        testFails();
        testLockup();
        testEscrowConditions();
        testMeta();
        testConsequences();
    }
};

BEAST_DEFINE_TESTSUITE(Escrow,app,ripple);

} // test
} // ripple
