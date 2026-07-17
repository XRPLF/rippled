
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/offer.h>
#include <test/jtx/owners.h>  // IWYU pragma: keep
#include <test/jtx/pay.h>
#include <test/jtx/ter.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/protocol/TER.h>

#include <cstddef>
#include <tuple>

namespace xrpl::test {

// Make sure "plump" order books don't have problems
class PlumpBook_test : public beast::unit_test::Suite
{
public:
    static void
    createOffers(jtx::Env& env, jtx::IOU const& iou, std::size_t n)
    {
        using namespace jtx;
        for (std::size_t i = 1; i <= n; ++i)
        {
            env(offer("alice", XRP(i), iou(1)));
            env.close();
        }
    }

    void
    test(std::size_t n)
    {
        using namespace jtx;
        auto const billion = 1000000000ul;
        Env env(*this);
        env.disableSigs();
        auto const gw = Account("gateway");
        auto const usd = gw["USD"];
        env.fund(XRP(billion), gw, "alice");
        env.trust(usd(billion), "alice");
        env(pay(gw, "alice", usd(billion)));
        createOffers(env, usd, n);
    }

    void
    run() override
    {
        test(10000);
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(PlumpBook, app, xrpl, 5);

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

BEAST_DEFINE_TESTSUITE(ThinBook, app, xrpl);

//------------------------------------------------------------------------------

class OversizeMeta_test : public beast::unit_test::Suite
{
public:
    static void
    createOffers(jtx::Env& env, jtx::IOU const& iou, std::size_t n)
    {
        using namespace jtx;
        for (std::size_t i = 1; i <= n; ++i)
        {
            env(offer("alice", XRP(1), iou(1)));
            env.close();
        }
    }

    void
    test()
    {
        std::size_t const n = 9000;
        using namespace jtx;
        auto const billion = 1000000000ul;
        Env env(*this);
        env.disableSigs();
        auto const gw = Account("gateway");
        auto const usd = gw["USD"];
        env.fund(XRP(billion), gw, "alice");
        env.trust(usd(billion), "alice");
        env(pay(gw, "alice", usd(billion)));
        createOffers(env, usd, n);
        env(pay("alice", gw, usd(billion)));
        env(offer("alice", usd(1), XRP(1)));
    }

    void
    run() override
    {
        test();
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(OversizeMeta, app, xrpl, 3);

//------------------------------------------------------------------------------

class FindOversizeCross_test : public beast::unit_test::Suite
{
public:
    // Return lowest x in [lo, hi] for which f(x)==true
    template <class Function>
    static std::size_t
    bfind(std::size_t lo, std::size_t hi, Function&& f)
    {
        auto len = hi - lo;
        while (len != 0)
        {
            auto l2 = len / 2;
            auto m = lo + l2;
            if (!f(m))
            {
                lo = ++m;
                len -= l2 + 1;
            }
            else
            {
                len = l2;
            }
        }
        return lo;
    }

    static void
    createOffers(jtx::Env& env, jtx::IOU const& iou, std::size_t n)
    {
        using namespace jtx;
        for (std::size_t i = 1; i <= n; ++i)
        {
            env(offer("alice", XRP(i), iou(1)));
            env.close();
        }
    }

    bool
    oversize(std::size_t n)
    {
        using namespace jtx;
        auto const billion = 1000000000ul;
        Env env(*this);
        env.disableSigs();
        auto const gw = Account("gateway");
        auto const usd = gw["USD"];
        env.fund(XRP(billion), gw, "alice");
        env.trust(usd(billion), "alice");
        env(pay(gw, "alice", usd(billion)));
        createOffers(env, usd, n);
        env(pay("alice", gw, usd(billion)));
        env(offer("alice", usd(1), XRP(1)), Ter(std::ignore));
        return env.ter() == tecOVERSIZE;
    }

    void
    run() override
    {
        auto const result = bfind(100, 9000, [&](std::size_t n) { return oversize(n); });
        log << "Min oversize offers = " << result << '\n';
    }
};

BEAST_DEFINE_TESTSUITE_MANUAL_PRIO(FindOversizeCross, app, xrpl, 50);

}  // namespace xrpl::test
