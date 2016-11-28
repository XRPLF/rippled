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
#include <ripple/beast/unit_test.h>
#include <algorithm>

namespace ripple {
namespace test {

// Make sure "plump" order books don't have problems
class PlumpBook_test : public beast::unit_test::suite
{
public:
    void
    createOffers (jtx::Env& env,
        jtx::IOU const& iou, std::size_t n)
    {
        using namespace jtx;
        for (std::size_t i = 1; i <= n; ++i)
            env(offer("alice", XRP(i), iou(1)));
    }

    void
    test (std::size_t n)
    {
        using namespace jtx;
        auto const billion = 1000000000ul;
        Env env(*this);
        env.disable_sigs();
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        env.fund(XRP(billion), gw, "alice", "bob", "carol");
        env.trust(USD(billion), "alice", "bob", "carol");
        env(pay(gw, "alice", USD(billion)));
        createOffers(env, USD, n);
    }

    void
    run() override
    {
        test(10000);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(PlumpBook,tx,ripple);

//------------------------------------------------------------------------------

// Ensure that unsigned transactions succeed during automatic test runs.
class ThinBook_test : public PlumpBook_test
{
public:
    void
        run() override
    {
        test(1);
    }
};

BEAST_DEFINE_TESTSUITE(ThinBook, tx, ripple);

//------------------------------------------------------------------------------

class OversizeMeta_test : public beast::unit_test::suite
{
public:
    void
    createOffers (jtx::Env& env, jtx::IOU const& iou,
        std::size_t n)
    {
        using namespace jtx;
        for (std::size_t i = 1; i <= n; ++i)
            env(offer("alice", XRP(1), iou(1)));
    }

    void
    test()
    {
        std::size_t const n = 9000;
        using namespace jtx;
        auto const billion = 1000000000ul;
        Env env(*this);
        env.disable_sigs();
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        env.fund(XRP(billion), gw, "alice", "bob", "carol");
        env.trust(USD(billion), "alice", "bob", "carol");
        env(pay(gw, "alice", USD(billion)));
        createOffers(env, USD, n);
        env(pay("alice", gw, USD(billion)));
        env(offer("alice", USD(1), XRP(1)));
    }

    void
    run()
    {
        test();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(OversizeMeta,tx,ripple);

//------------------------------------------------------------------------------

class FindOversizeCross_test : public beast::unit_test::suite
{
public:
    // Return lowest x in [lo, hi] for which f(x)==true
    template <class Function>
    static
    std::size_t
    bfind(std::size_t lo, std::size_t hi, Function&& f)
    {
        auto len = hi - lo;
        while (len != 0)
        {
            auto l2 = len / 2;
            auto m = lo + l2;
            if (! f(m))
            {
                lo = ++m;
                len -= l2 + 1;
            }
            else
                len = l2;
        }
        return lo;
    }

    void
    createOffers (jtx::Env& env, jtx::IOU const& iou,
        std::size_t n)
    {
        using namespace jtx;
        for (std::size_t i = 1; i <= n; ++i)
            env(offer("alice", XRP(i), iou(1)));
    }

    bool
    oversize(std::size_t n)
    {
        using namespace jtx;
        auto const billion = 1000000000ul;
        Env env(*this);
        env.disable_sigs();
        auto const gw = Account("gateway");
        auto const USD = gw["USD"];
        env.fund(XRP(billion), gw, "alice", "bob", "carol");
        env.trust(USD(billion), "alice", "bob", "carol");
        env(pay(gw, "alice", USD(billion)));
        createOffers(env, USD, n);
        env(pay("alice", gw, USD(billion)));
        env(offer("alice", USD(1), XRP(1)), ter(std::ignore));
        return env.ter() == tecOVERSIZE;
    }

    void
    run()
    {
        auto const result = bfind(100, 9000,
            [&](std::size_t n) { return oversize(n); });
        log << "Min oversize offers = " << result << '\n';
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL(FindOversizeCross,tx,ripple);

} // test
} // ripple

