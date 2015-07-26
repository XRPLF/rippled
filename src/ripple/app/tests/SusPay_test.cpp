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
#include <ripple/test/jtx.h>
#include <ripple/protocol/digest.h>
#include <ripple/protocol/Indexes.h>
#include <ripple/protocol/JsonFields.h>
#include <ripple/protocol/TxFlags.h>

namespace ripple {
namespace test {

struct SusPay_test : public beast::unit_test::suite
{
    template <class... Args>
    static
    uint256
    digest (Args&&... args)
    {
        sha256_hasher h;
        using beast::hash_append;
        hash_append(h, args...);
        auto const d = static_cast<
            sha256_hasher::result_type>(h);
        uint256 result;
        std::memcpy(result.data(), d.data(), d.size());
        return result;
    }

    // Create condition
    // First is digest, second is pre-image
    static
    std::pair<uint256, uint256>
    cond (std::string const& receipt)
    {
        std::pair<uint256, uint256> result;
        result.second = digest(receipt);
        result.first = digest(result.second);
        return result;
    }

    static
    Json::Value
    condpay (jtx::Account const& account, jtx::Account const& to,
        STAmount const& amount, uint256 const& digest,
            NetClock::time_point const& expiry)
    {
        using namespace jtx;
        Json::Value jv;
        jv[jss::TransactionType] = "SuspendedPaymentCreate";
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv[jss::Destination] = to.human();
        jv[jss::Amount] = amount.getJson(0);
        jv["CancelAfter"] =
            expiry.time_since_epoch().count();
        jv["Digest"] = to_string(digest);
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
            uint256 const& digest, uint256 const& preimage)
    {
        Json::Value jv;
        jv[jss::TransactionType] = "SuspendedPaymentFinish";
        jv[jss::Flags] = tfUniversal;
        jv[jss::Account] = account.human();
        jv["Owner"] = from.human();
        jv["OfferSequence"] = seq;
        jv["Method"] = 1;
        jv["Digest"] = to_string(digest);
        jv["Proof"] = to_string(preimage);
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
        using namespace jtx;
        using namespace std::chrono;
        using S = seconds;
        {
            Env env(*this);
            auto const T = [&env](NetClock::duration const& d)
                { return env.clock.now() + d; };
            env.fund(XRP(5000), "alice", "bob");
            auto const c = cond("receipt");
            // syntax
            env(condpay("alice", "bob", XRP(1000), c.first, T(S{1})));
            env.disable_testing();
            // disabled in production
            env(condpay("alice", "bob", XRP(1000), c.first, T(S{1})),       ter(temDISABLED));
            env(finish("bob", "alice", 1),                                  ter(temDISABLED));
            env(cancel("bob", "alice", 1),                                  ter(temDISABLED));
        }
    }

    void
    testTags()
    {
        using namespace jtx;
        using namespace std::chrono;
        using S = seconds;
        {
            Env env(*this);
            auto const alice = Account("alice");
            auto const T = [&env](NetClock::duration const& d)
                { return env.clock.now() + d; };
            env.fund(XRP(5000), alice, "bob");
            auto const c = cond("receipt");
            auto const seq = env.seq(alice);
            // set source and dest tags
            env(condpay(alice, "bob", XRP(1000), c.first, T(S{1})), stag(1), dtag(2));
            auto const sle = env.le(keylet::susPay(alice.id(), seq));
            expect((*sle)[sfSourceTag] == 1);
            expect((*sle)[sfDestinationTag] == 2);
        }
    }

    void
    testFails()
    {
        using namespace jtx;
        using namespace std::chrono;
        using S = seconds;
        {
            Env env(*this);
            auto const T = [&env](NetClock::duration const& d)
                { return env.clock.now() + d; };
            env.fund(XRP(5000), "alice", "bob");
            auto const c = cond("receipt");
            // VFALCO Should we enforce this?
            // expiration in the past
            //env(condpay("alice", "bob", XRP(1000), c.first, T(S{-1})),      ter(tecNO_PERMISSION));
            // expiration beyond the limit
            env(condpay("alice", "bob", XRP(1000), c.first, T(days(7+1))),  ter(tecNO_PERMISSION));
            // no destination account
            env(condpay("alice", "carol", XRP(1000), c.first, T(S{1})),     ter(tecNO_DST));
            env.fund(XRP(5000), "carol");
            env(condpay("alice", "carol",
                XRP(1000), c.first, T(S{1})), stag(2));
            env(condpay("alice", "carol",
                XRP(1000), c.first, T(S{1})), stag(3), dtag(4));
            env(fset("carol", asfRequireDest));
            // missing destination tag
            env(condpay("alice", "carol", XRP(1000), c.first, T(S{1})),     ter(tecDST_TAG_NEEDED));
            env(condpay("alice", "carol",
                XRP(1000), c.first, T(S{1})), dtag(1));
        }
    }

    void
    testLockup()
    {
        using namespace jtx;
        using namespace std::chrono;
        using S = seconds;
        {
            Env env(*this);
            auto const T = [&env](NetClock::duration const& d)
                { return env.clock.now() + d; };
            env.fund(XRP(5000), "alice", "bob");
            auto const seq = env.seq("alice");
            env(lockup("alice", "alice", XRP(1000), T(S{1})));
            env.require(balance("alice", XRP(4000) - drops(10)));
            env(cancel("bob", "alice", seq),                                ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq),                                ter(tecNO_PERMISSION));
            env.close();
            env(cancel("bob", "alice", seq),                                ter(tecNO_PERMISSION));
            env(finish("bob", "alice", seq));
        }
    }

    void
    testCondPay()
    {
        using namespace jtx;
        using namespace std::chrono;
        using S = seconds;
        {
            Env env(*this);
            auto const T = [&env](NetClock::duration const& d)
                { return env.clock.now() + d; };
            env.fund(XRP(5000), "alice", "bob", "carol");
            auto const c = cond("receipt");
            auto const seq = env.seq("alice");
            expect((*env.le("alice"))[sfOwnerCount] == 0);
            env(condpay("alice", "carol", XRP(1000), c.first, T(S{1})));
            expect((*env.le("alice"))[sfOwnerCount] == 1);
            env.require(balance("alice", XRP(4000) - drops(10)));
            env.require(balance("carol", XRP(5000)));
            env(cancel("bob", "alice", seq),                                ter(tecNO_PERMISSION));
            expect((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq, c.first, c.first),              ter(temBAD_SIGNATURE));
            expect((*env.le("alice"))[sfOwnerCount] == 1);
            env(finish("bob", "alice", seq, c.first, c.second));
            // SLE removed on finish
            expect(! env.le(keylet::susPay(Account("alice").id(), seq)));
            expect((*env.le("alice"))[sfOwnerCount] == 0);
            env.require(balance("carol", XRP(6000)));
            env(cancel("bob", "alice", seq),                                ter(tecNO_TARGET));
            expect((*env.le("alice"))[sfOwnerCount] == 0);
            env(cancel("bob", "carol", 1),                                  ter(tecNO_TARGET));
            env.close();
        }
        {
            Env env(*this);
            auto const T = [&env](NetClock::duration const& d)
                { return env.clock.now() + d; };
            env.fund(XRP(5000), "alice", "bob", "carol");
            auto const c = cond("receipt");
            auto const seq = env.seq("alice");
            expect((*env.le("alice"))[sfOwnerCount] == 0);
            env(condpay("alice", "carol", XRP(1000), c.first, T(S{1})));
            env.close();
            env.require(balance("alice", XRP(4000) - drops(10)));
            // balance restored on cancel
            env(cancel("bob", "alice", seq));
            env.require(balance("alice", XRP(5000) - drops(10)));
            // SLE removed on cancel
            expect(! env.le(keylet::susPay(Account("alice").id(), seq)));
        }
        {
            Env env(*this);
            auto const T = [&env](NetClock::duration const& d)
                { return env.clock.now() + d; };
            env.fund(XRP(5000), "alice", "bob", "carol");
            env.close();
            auto const c = cond("receipt");
            auto const seq = env.seq("alice");
            env(condpay("alice", "carol", XRP(1000), c.first, T(S{1})));
            expect((*env.le("alice"))[sfOwnerCount] == 1);
            // cancel fails before expiration
            env(cancel("bob", "alice", seq),                                ter(tecNO_PERMISSION));
            expect((*env.le("alice"))[sfOwnerCount] == 1);
            env.close();
            // finish fails after expiration
            env(finish("bob", "alice", seq, c.first, c.second),             ter(tecNO_PERMISSION));
            expect((*env.le("alice"))[sfOwnerCount] == 1);
            env.require(balance("carol", XRP(5000)));
        }
    }

    void run() override
    {
        testEnablement();
        testTags();
        testFails();
        testLockup();
        testCondPay();
    }
};

BEAST_DEFINE_TESTSUITE(SusPay,app,ripple);

} // test
} // ripple
