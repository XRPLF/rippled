#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/trust.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/OrderBookIndex.h>
#include <xrpl/protocol/UintTypes.h>
#include <xrpl/protocol/jss.h>

#include <vector>

namespace xrpl::test {

/** Bit-exactness gate for the Plan 9 order-book index seam: a scripted
    crossing scenario must produce an identical sequence of ledger hashes with
    the index enabled (BookTip iterates the in-memory cursor) and disabled
    (BookTip walks the SHAMap with succ()). Any divergence in the cursor's order
    or contents changes consumed offers/amounts and therefore the ledger hash. */
class OrderBookCrossing_test : public beast::unit_test::Suite
{
    // Run a deterministic crossing scenario and return the ledger hash after
    // every close. The scenario exercises the cursor-specific paths:
    // multi-quality books, a multi-offer (shared-quality) level, an unfunded
    // offer, partial fills, and a pre-crossing cancel (peek-null-skip).
    std::vector<uint256>
    runScenario()
    {
        using namespace jtx;
        Env env{*this};
        std::vector<uint256> hashes;
        // accountHash is the consensus state root — it reflects every crossing
        // effect (consumed offers, balances, directories). If the cursor and
        // succ() paths diverge at all, this differs.
        auto snap = [&] { hashes.push_back(env.closed()->header().accountHash); };

        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];
        Account const alice{"alice"};  // maker, spread of qualities
        Account const bob{"bob"};      // maker, shared-quality level
        Account const carol{"carol"};  // maker, becomes unfunded
        Account const dave{"dave"};    // taker

        env.fund(XRP(10'000'000), gw, alice, bob, carol, dave);
        env.close();
        snap();
        env.trust(USD(100'000'000), alice, bob, carol, dave);
        env.close();
        env(pay(gw, alice, USD(1'000'000)));
        env(pay(gw, bob, USD(1'000'000)));
        env(pay(gw, carol, USD(1'000'000)));
        env.close();
        snap();

        // alice: 8 distinct qualities. bob: 4 offers at one shared quality
        // (a multi-entry level). carol: one offer she will defund.
        for (int i = 0; i < 8; ++i)
            env(offer(alice, XRP(500 + i), USD(100)));
        for (int i = 0; i < 4; ++i)
            env(offer(bob, XRP(503), USD(100)));
        env(offer(carol, XRP(501), USD(100)));
        env.close();
        snap();

        // Defund carol: move her USD away so her resting offer is unfunded at
        // cross time (exercises the unfunded-skip path through the cursor).
        env(pay(carol, gw, USD(1'000'000)));
        env.close();
        snap();

        // dave places an offer, then cancels it via an OfferCreate carrying
        // OfferSequence (pre-crossing delete → cursor peek-null-skip path).
        auto const daveOfferSeq = env.seq(dave);
        env(offer(dave, USD(100), XRP(2'000)));  // far from market: rests
        env.close();
        snap();

        // dave crosses: partial and full fills across alice/bob/carol levels.
        env(offer(dave, USD(250), XRP(1'255)));
        env.close();
        snap();
        env(offer(dave, USD(500), XRP(2'520)));
        env.close();
        snap();

        // A crossing OfferCreate that also cancels dave's resting offer.
        auto cross = offer(dave, USD(100), XRP(505));
        cross[jss::OfferSequence] = daveOfferSeq;
        env(cross);
        env.close();
        snap();

        return hashes;
    }

    void
    testIndexMatchesBaseline()
    {
        testcase("ledger hashes identical with order-book index on vs off");

        OrderBookIndex::setEnabled(false);
        auto const baseline = runScenario();

        OrderBookIndex::setEnabled(true);
        auto const withIndex = runScenario();

        OrderBookIndex::setEnabled(true);  // restore default

        BEAST_EXPECT(baseline.size() == withIndex.size());
        bool identical = baseline.size() == withIndex.size();
        for (std::size_t i = 0; i < baseline.size() && i < withIndex.size(); ++i)
        {
            if (baseline[i] != withIndex[i])
            {
                identical = false;
                log << "  ledger-hash divergence at close " << i << "\n";
            }
        }
        BEAST_EXPECT(identical);
    }

public:
    void
    run() override
    {
        testIndexMatchesBaseline();
    }
};

BEAST_DEFINE_TESTSUITE(OrderBookCrossing, app, xrpl);

}  // namespace xrpl::test
