#include <test/jtx/Env.h>

#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/shamap/Family.h>

#include <cstdint>

namespace xrpl::test {

struct NodeFamily_test : public beast::unit_test::Suite
{
    /**
     * Every missing-node report that names a ledger sequence starts an
     * acquisition for that ledger.
     *
     * The ledgers reported are in the local node store, so each acquisition
     * completes at once and InboundLedgers::find() shows which ones were
     * started.
     */
    void
    testMissingNodeAcquireBySeqRepeats()
    {
        testcase("missingNodeAcquireBySeq dispatches every report");
        using namespace jtx;

        Env env{*this};
        // Enough closed ledgers for three sequences below the closed one.
        for (int i = 0; i < 4; ++i)
            env.close();

        auto& family = env.app().getNodeFamily();
        auto& ledgers = env.app().getInboundLedgers();
        auto& master = env.app().getLedgerMaster();

        std::uint32_t const first = env.closed()->header().seq - 2;
        std::uint32_t const lower = first - 1;
        std::uint32_t const higher = first + 1;
        auto const firstHash = master.getHashBySeq(first);
        auto const lowerHash = master.getHashBySeq(lower);
        auto const higherHash = master.getHashBySeq(higher);
        if (!BEAST_EXPECT(firstHash.isNonZero() && lowerHash.isNonZero() && higherHash.isNonZero()))
            return;

        // The node hash only names the report in the log, so any value serves.
        UInt256 const nodeHash{1};

        // The first report starts an acquisition, which completes from the local store.
        BEAST_EXPECT(!ledgers.find(firstHash));
        family.missingNodeAcquireBySeq(first, nodeHash);
        auto const acquired = ledgers.find(firstHash);
        if (!BEAST_EXPECT(acquired))
            return;
        BEAST_EXPECT(acquired->isComplete());

        // A report for a lower sequence starts an acquisition once the first pass has finished.
        BEAST_EXPECT(!ledgers.find(lowerHash));
        family.missingNodeAcquireBySeq(lower, nodeHash);
        BEAST_EXPECT(ledgers.find(lowerHash));

        // A report for a higher sequence starts its own acquisition once the first pass has
        // finished.
        BEAST_EXPECT(!ledgers.find(higherHash));
        family.missingNodeAcquireBySeq(higher, nodeHash);
        BEAST_EXPECT(ledgers.find(higherHash));
    }

    void
    run() override
    {
        testMissingNodeAcquireBySeqRepeats();
    }
};

BEAST_DEFINE_TESTSUITE(NodeFamily, app, xrpl);

}  // namespace xrpl::test
