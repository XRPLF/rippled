#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/fee.h>
#include <test/jtx/offer.h>
#include <test/jtx/pay.h>
#include <test/jtx/seq.h>
#include <test/jtx/trust.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/ledger/OrderBookIndex.h>
#include <xrpl/protocol/Indexes.h>
#include <xrpl/protocol/Issue.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/tx/apply.h>

#include <memory>
#include <vector>

namespace xrpl::test {

/** Proves OrderBookIndex's rebuild/walk against a real SHAMap-backed book, and
    that an index maintained by inserting offers in creation order matches the
    canonical directory walk (the determinism assumption behind P9.3). */
class OrderBookIndex_test : public beast::unit_test::Suite
{
    // Read an offer's quality-directory root (the key the index levels on).
    static uint256
    bookDirOf(ReadView const& view, uint256 const& offerKey)
    {
        auto const sle = view.read(keylet::offer(offerKey));
        return sle ? sle->getFieldH256(sfBookDirectory) : uint256{};
    }

    void
    testRebuildMatchesWalk()
    {
        testcase("rebuild matches SHAMap walk, ordered best-quality-first");
        using namespace jtx;

        Env env{*this};
        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];
        Account const maker{"maker"};

        env.fund(XRP(10'000'000), gw, maker);
        env.close();
        env.trust(USD(100'000'000), maker);
        env.close();
        env(pay(gw, maker, USD(10'000'000)));
        env.close();

        // Book the maker's offers populate: in = TakerPays asset (XRP),
        // out = TakerGets asset (USD). (OfferCreate.cpp builds it this way.)
        Book const book{xrpIssue(), USD.issue(), std::nullopt};

        // Place offers, recording each offer's key in creation order.
        // - 5 distinct qualities (distinct TakerPays => distinct levels)
        // - one quality with 40 offers to force a multi-page directory level
        //   (exercises cdirNext across pages in the walk).
        std::vector<uint256> created;
        auto place = [&](int xrpPays, int usdGets) {
            auto const seq = env.seq(maker);
            env(offer(maker, XRP(xrpPays), USD(usdGets)));
            created.push_back(keylet::offer(maker, seq).key);
        };

        for (int q = 0; q < 5; ++q)
            place(500 + q, 100);          // 5 distinct qualities
        for (int i = 0; i < 40; ++i)
            place(800, 100);              // 40 offers at one shared quality
        env.close();

        auto const view = env.closed();

        // Rebuild from the authoritative state.
        OrderBookIndex rebuilt;
        rebuilt.rebuildBook(*view, book);

        BEAST_EXPECT(rebuilt.offerCount(book) == created.size());
        BEAST_EXPECT(rebuilt.validateMatchesShaMap(*view, book));
        BEAST_EXPECT(rebuilt.rebuilds() == 1u);

        // Flattened order must be non-decreasing in quality (best first).
        auto const flat = rebuilt.flatten(book);
        BEAST_EXPECT(flat.size() == created.size());
        bool ordered = true;
        for (std::size_t i = 1; i < flat.size(); ++i)
        {
            auto const prev = getQuality(bookDirOf(*view, flat[i - 1]));
            auto const cur = getQuality(bookDirOf(*view, flat[i]));
            if (cur < prev)
                ordered = false;
        }
        BEAST_EXPECT(ordered);

        // An index maintained by inserting in creation order (simulating the
        // P9.3 apply-path hooks, no deletions) must equal the rebuilt index.
        OrderBookIndex maintained;
        for (auto const& offerKey : created)
            maintained.insertOffer(book, bookDirOf(*view, offerKey), offerKey);
        BEAST_EXPECT(maintained.flatten(book) == flat);
        BEAST_EXPECT(maintained.validateMatchesShaMap(*view, book));
    }

    void
    testEmptyAndAbsentBook()
    {
        testcase("rebuild of an empty book yields nothing");
        using namespace jtx;
        Env env{*this};
        env.fund(XRP(10'000), Account{"gw"});
        env.close();

        Book const book{xrpIssue(), Account{"gw"}["USD"].issue(), std::nullopt};
        OrderBookIndex idx;
        idx.rebuildBook(*env.closed(), book);
        BEAST_EXPECT(idx.offerCount(book) == 0u);
        BEAST_EXPECT(idx.bookCount() == 0u);
        BEAST_EXPECT(idx.validateMatchesShaMap(*env.closed(), book));
    }

    // P9.3: an index seeded from state and then maintained through real
    // OfferCreate apply (crossings delete offers, placements insert them) must
    // stay byte-exactly equal to a fresh SHAMap walk. This proves the notify
    // hooks keep the index in sync without any read-path/seam involvement.
    void
    testMaintenanceInSync()
    {
        testcase("index stays in sync through real crossing/placement apply");
        using namespace jtx;

        Env env{*this};
        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];
        Account const maker{"maker"};
        Account const taker{"taker"};

        env.fund(XRP(10'000'000), gw, maker, taker);
        env.close();
        env.trust(USD(100'000'000), maker, taker);
        env.close();
        env(pay(gw, maker, USD(10'000'000)));
        env.close();

        Book const book{xrpIssue(), USD.issue(), std::nullopt};

        // Resting book: 30 offers across distinct qualities.
        for (int i = 0; i < 30; ++i)
            env(offer(maker, XRP(500 + i), USD(100)));
        env.close();

        // Owned OpenView over the closed state; seed the index by rebuild
        // (the attach-time / startup model).
        auto const base = env.current();
        OpenView accum(kOpenLedger, base->rules(), base);
        accum.orderBookIndex().rebuildBook(accum, book);
        BEAST_EXPECT(accum.orderBookIndex().validateMatchesShaMap(accum, book));
        BEAST_EXPECT(accum.orderBookIndex().offerCount(book) == 30u);

        // Pre-sign a mixed batch: taker crossings (consume → delete) and maker
        // placements at new qualities (insert), with explicit sequences.
        std::vector<std::shared_ptr<STTx const>> txns;
        std::uint32_t takerSeq = env.seq(taker);
        std::uint32_t makerSeq = env.seq(maker);
        for (int i = 0; i < 15; ++i)
        {
            txns.push_back(
                env.jt(offer(taker, USD(100), XRP(500 + i)), Seq(takerSeq++), Fee(100)).stx);
            txns.push_back(
                env.jt(offer(maker, XRP(700 + i), USD(100)), Seq(makerSeq++), Fee(100)).stx);
        }

        // Apply to the owned view; the index is maintained via the notify
        // hooks (flushed on each apply). Validate after every tx so a desync
        // is pinned to the exact transaction that caused it.
        for (auto const& tx : txns)
        {
            auto const r = apply(env.app(), accum, *tx, TapNone, env.journal);
            BEAST_EXPECT(r.applied);
            BEAST_EXPECT(accum.orderBookIndex().validateMatchesShaMap(accum, book));
        }

        // The index actually did work (both directions exercised).
        BEAST_EXPECT(accum.orderBookIndex().inserts() > 0u);
        BEAST_EXPECT(accum.orderBookIndex().deletes() > 0u);
    }

    // P9.6 Stage E: across a ledger close the open-round index is not carried
    // (the next round starts cold and warms via rebuild-on-touch). Confirm that
    // after real crossings + a close, the post-close state rebuilds clean — i.e.
    // the close handoff leaves no index/SHAMap drift.
    void
    testCloseHandoff()
    {
        testcase("index rebuilds clean across a ledger close");
        using namespace jtx;

        Env env{*this};
        auto const gw = Account{"gw"};
        auto const USD = gw["USD"];
        Account const maker{"maker"};
        Account const taker{"taker"};

        env.fund(XRP(10'000'000), gw, maker, taker);
        env.close();
        env.trust(USD(100'000'000), maker, taker);
        env.close();
        env(pay(gw, maker, USD(10'000'000)));
        env.close();

        Book const book{xrpIssue(), USD.issue(), std::nullopt};

        for (int i = 0; i < 20; ++i)
            env(offer(maker, XRP(500 + i), USD(100)));
        env.close();

        // Round 1: real crossings through the open ledger, then close.
        for (int i = 0; i < 8; ++i)
            env(offer(taker, USD(100), XRP(500 + i)));
        env.close();

        // After the close, a fresh index rebuilt from the post-close ledger must
        // match the SHAMap walk (no drift left by the round's crossings).
        {
            OrderBookIndex idx;
            idx.rebuildBook(*env.closed(), book);
            BEAST_EXPECT(idx.validateMatchesShaMap(*env.closed(), book));
        }

        // Round 2: more crossings on top of the post-close state, then re-check.
        for (int i = 8; i < 16; ++i)
            env(offer(taker, USD(100), XRP(500 + i)));
        env.close();
        {
            OrderBookIndex idx;
            idx.rebuildBook(*env.closed(), book);
            BEAST_EXPECT(idx.validateMatchesShaMap(*env.closed(), book));
        }
    }

public:
    void
    run() override
    {
        testRebuildMatchesWalk();
        testEmptyAndAbsentBook();
        testMaintenanceInSync();
        testCloseHandoff();
    }
};

BEAST_DEFINE_TESTSUITE(OrderBookIndex, ledger, xrpl);

}  // namespace xrpl::test
