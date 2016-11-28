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
#include <test/support/jtx.h>
#include <ripple/app/tx/applySteps.h>
#include <ripple/protocol/Feature.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {
namespace test {

struct SusPay_test : public beast::unit_test::suite
{
    // An Ed25519 conditional trigger fulfillment and its
    // condition
    std::array<std::uint8_t, 99> const fb1 =
    {{
        0x00, 0x04, 0x60, 0x3B, 0x6A, 0x27, 0xBC, 0xCE, 0xB6, 0xA4, 0x2D, 0x62,
        0xA3, 0xA8, 0xD0, 0x2A, 0x6F, 0x0D, 0x73, 0x65, 0x32, 0x15, 0x77, 0x1D,
        0xE2, 0x43, 0xA6, 0x3A, 0xC0, 0x48, 0xA1, 0x8B, 0x59, 0xDA, 0x29, 0x8F,
        0x89, 0x5B, 0x3C, 0xAF, 0xE2, 0xC9, 0x50, 0x60, 0x39, 0xD0, 0xE2, 0xA6,
        0x63, 0x82, 0x56, 0x80, 0x04, 0x67, 0x4F, 0xE8, 0xD2, 0x37, 0x78, 0x50,
        0x92, 0xE4, 0x0D, 0x6A, 0xAF, 0x48, 0x3E, 0x4F, 0xC6, 0x01, 0x68, 0x70,
        0x5F, 0x31, 0xF1, 0x01, 0x59, 0x61, 0x38, 0xCE, 0x21, 0xAA, 0x35, 0x7C,
        0x0D, 0x32, 0xA0, 0x64, 0xF4, 0x23, 0xDC, 0x3E, 0xE4, 0xAA, 0x3A, 0xBF,
        0x53, 0xF8, 0x03,
    }};

    std::array<std::uint8_t, 39> const cb1 =
    {{
        0x00, 0x04, 0x01, 0x20, 0x20, 0x3B, 0x6A, 0x27, 0xBC, 0xCE, 0xB6, 0xA4,
        0x2D, 0x62, 0xA3, 0xA8, 0xD0, 0x2A, 0x6F, 0x0D, 0x73, 0x65, 0x32, 0x15,
        0x77, 0x1D, 0xE2, 0x43, 0xA6, 0x3A, 0xC0, 0x48, 0xA1, 0x8B, 0x59, 0xDA,
        0x29, 0x01, 0x60
    }};

    // A prefix.prefix.ed25519 conditional trigger fulfillment:
    std::array<std::uint8_t, 106> const fb2 =
    {{
        0x00, 0x01, 0x67, 0x03, 0x61, 0x62, 0x63, 0x00, 0x04, 0x60, 0x76, 0xA1,
        0x59, 0x20, 0x44, 0xA6, 0xE4, 0xF5, 0x11, 0x26, 0x5B, 0xCA, 0x73, 0xA6,
        0x04, 0xD9, 0x0B, 0x05, 0x29, 0xD1, 0xDF, 0x60, 0x2B, 0xE3, 0x0A, 0x19,
        0xA9, 0x25, 0x76, 0x60, 0xD1, 0xF5, 0xAE, 0xC6, 0xAB, 0x6A, 0x91, 0x22,
        0xAF, 0xF0, 0xF7, 0xDC, 0xB9, 0x66, 0x7F, 0xF6, 0x13, 0x13, 0x68, 0x94,
        0x73, 0x2B, 0x6E, 0x78, 0xC2, 0x6F, 0x5B, 0x67, 0x31, 0x01, 0xE2, 0x67,
        0xFE, 0x2E, 0x2B, 0x65, 0xFA, 0x4D, 0x53, 0xDA, 0xD4, 0x78, 0xA1, 0xAD,
        0xA6, 0x4D, 0x50, 0xFD, 0x1D, 0xFD, 0xB7, 0xD9, 0x49, 0x20, 0xDC, 0x3E,
        0x1A, 0x56, 0x4A, 0x64, 0x7B, 0x1C, 0xBA, 0x35, 0x60, 0x01,
    }};

    std::array<std::uint8_t, 39> const cb2 =
    {{

        0x00, 0x01, 0x01, 0x25, 0x20, 0x28, 0x7A, 0x8B, 0xD8, 0xAD, 0xAE, 0x8A,
        0xCA, 0x0C, 0x87, 0x1C, 0xE7, 0xC2, 0x5F, 0xBA, 0xA5, 0xA8, 0xBE, 0x10,
        0xD0, 0xE4, 0xDB, 0x1F, 0x56, 0xAE, 0xEE, 0x8B, 0xB3, 0xAD, 0xCE, 0xE5,
        0x5B, 0x01, 0x64
    }};

    // A prefix+preimage conditional trigger fulfillment
    std::array<std::uint8_t, 7> const fb3 =
    {{
        0x00, 0x01, 0x04, 0x00, 0x00, 0x00, 0x00,
    }};

    std::array<std::uint8_t, 39> const cb3 =
    {{

        0x00, 0x01, 0x01, 0x07, 0x20, 0x62, 0x36, 0xB7, 0xA8, 0x58, 0xFB, 0x35,
        0x2F, 0xD5, 0xC3, 0x01, 0x3B, 0x68, 0x98, 0xCF, 0x26, 0x8B, 0x3E, 0xB8,
        0x50, 0xB3, 0x4A, 0xD2, 0x65, 0x24, 0xB0, 0xF8, 0x56, 0xC3, 0x72, 0xD9,
        0x73, 0x01, 0x01
    }};

    static
    Json::Value
    condpay (jtx::Account const& account, jtx::Account const& to,
        STAmount const& amount, Slice condition,
            NetClock::time_point const& cancelAfter)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = "SuspendedPaymentCreate";
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
        jv[jss::TransactionType] = "SuspendedPaymentCreate";
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
        jv[jss::TransactionType] = "SuspendedPaymentCreate";
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
        jv[jss::TransactionType] = "SuspendedPaymentFinish";
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
        jv[jss::TransactionType] = "SuspendedPaymentFinish";
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
        jv[jss::TransactionType] = "SuspendedPaymentCancel";
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

        { // SusPay enabled
            Env env(*this, features(featureSusPay));
            env.fund(XRP(5000), "alice", "bob");
            env(lockup("alice", "bob", XRP(1000), env.now() + 1s));
        }

        { // SusPay not enabled
            Env env(*this);
            env.fund(XRP(5000), "alice", "bob");
            env(lockup("alice", "bob", XRP(1000), env.now() + 1s), ter(temDISABLED));
            env(finish("bob", "alice", 1),                         ter(temDISABLED));
            env(cancel("bob", "alice", 1),                         ter(temDISABLED));
        }

        { // SusPay enabled, CryptoConditions disabled
            Env env(*this,
                features(featureSusPay));

            env.fund(XRP(5000), "alice", "bob");

            auto const seq = env.seq("alice");

            // Fail: no cryptoconditions allowed
            env(condpay("alice", "bob", XRP(1000),
                makeSlice (cb1), env.now() + 1s),           ter(temDISABLED));

            // Succeed: doesn't have a cryptocondition
            env(lockup("alice", "bob", XRP(1000),
                env.now() + 1s));

            // Fail: can't specify conditional finishes if
            // cryptoconditions aren't allowed.
            {
                auto f = finish("bob", "alice", seq,
                    makeSlice(cb1), makeSlice(fb1));
                env (f,   ter(temDISABLED));

                auto fnc = f;
                fnc.removeMember ("Condition");
                env (fnc, ter(temDISABLED));

                auto fnf = f;
                fnf.removeMember ("Fulfillment");
                env (fnf, ter(temDISABLED));

            }

            // Succeeds
            env.close();
            env(finish("bob", "alice", seq));
        }

        { // SusPay enabled, CryptoConditions enabled
            Env env(*this,
                features(featureSusPay),
                features(featureCryptoConditions));

            env.fund(XRP(5000), "alice", "bob");

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

        {
            Env env(*this,
                features(featureSusPay),
                features(featureCryptoConditions));

            auto const alice = Account("alice");
            env.fund(XRP(5000), alice, "bob");

            auto const seq = env.seq(alice);
            // set source and dest tags
            env(condpay(alice, "bob", XRP(1000),
                    makeSlice (cb1), env.now() + 1s),
                stag(1), dtag(2));
            auto const sle = env.le(keylet::susPay(alice.id(), seq));
            BEAST_EXPECT((*sle)[sfSourceTag] == 1);
            BEAST_EXPECT((*sle)[sfDestinationTag] == 2);
        }
    }

    void
    testFails()
    {
        testcase ("Failure Cases");

        using namespace jtx;
        using namespace std::chrono;

        Env env(*this,
            features(featureSusPay),
            features(featureCryptoConditions));
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
            Env env(*this, features(featureSusPay));
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
            Env env(*this,
                features(featureSusPay),
                features(featureCryptoConditions));
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
    testCondPay()
    {
        testcase ("Conditional Payments");

        using namespace jtx;
        using namespace std::chrono;
        using S = seconds;

        { // Test cryptoconditions
            Env env(*this,
                features(featureSusPay),
                features(featureCryptoConditions));
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
            BEAST_EXPECT(! env.le(keylet::susPay(Account("alice").id(), seq)));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env.require(balance("carol", XRP(6000)));
            env(cancel("bob", "alice", seq),                                ter(tecNO_TARGET));
            BEAST_EXPECT((*env.le("alice"))[sfOwnerCount] == 0);
            env(cancel("bob", "carol", 1),                                  ter(tecNO_TARGET));
            env.close();
        }

        { // Test cancel when condition is present
            Env env(*this,
                features(featureSusPay),
                features(featureCryptoConditions));
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
            BEAST_EXPECT(! env.le(keylet::susPay(Account("alice").id(), seq)));
        }

        {
            Env env(*this,
                features(featureSusPay),
                features(featureCryptoConditions));
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
            Env env(*this,
                features(featureSusPay),
                features(featureCryptoConditions));
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
                features(featureSusPay),
                features(featureCryptoConditions));
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
            Env env(*this,
                features(featureSusPay),
                features(featureCryptoConditions));
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
    }

    void
    testMeta()
    {
        testcase ("Metadata");

        using namespace jtx;
        using namespace std::chrono;
        Env env(*this,
            features(featureSusPay),
            features(featureCryptoConditions));

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
        Env env(*this,
            features(featureSusPay),
            features(featureCryptoConditions));

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
        testCondPay();
        testMeta();
        testConsequences();
    }
};

BEAST_DEFINE_TESTSUITE(SusPay,app,ripple);

} // test
} // ripple
