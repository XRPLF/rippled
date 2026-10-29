#include <test/jtx/Env.h>

#include <xrpld/app/ledger/InboundLedgers.h>
#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/main/Application.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/nodestore/Database.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/HashPrefix.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/shamap/Family.h>

#include <cstdint>
#include <memory>
#include <stdexcept>
#include <utility>

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

    /**
     * An exception out of the acquisition reaches the caller and releases
     * the latch.
     *
     * A ledger whose stored header is too short makes the acquisition throw
     * while reading the header, with the latch mutex released. The next
     * report must still start a pass of its own.
     */
    void
    testMissingNodeAcquireBySeqThrowReleasesLatch()
    {
        testcase("missingNodeAcquireBySeq releases the latch on an exception");
        using namespace jtx;

        Env env{*this};
        for (int i = 0; i < 4; ++i)
            env.close();

        auto& family = env.app().getNodeFamily();
        auto& ledgers = env.app().getInboundLedgers();
        auto& master = env.app().getLedgerMaster();

        // A child of the closed ledger that was never closed itself. Its state map is a copy of
        // the parent's, where a real close always updates the skip list, so no real ledger shares
        // its hash. Registering it as validated makes getHashBySeq() name it.
        auto const closed = master.getClosedLedger();
        auto const fake = std::make_shared<Ledger>(*closed, closed->header().closeTime);
        if (!BEAST_EXPECT(fake->setImmutable()))
            return;
        fake->setValidated();
        master.storeLedger(fake);

        std::uint32_t const fakeSeq = fake->header().seq;
        auto const fakeHash = fake->header().hash;
        if (!BEAST_EXPECT(master.getHashBySeq(fakeSeq) == fakeHash))
            return;

        // Every Env in the process shares one node store, so the hash must be free before the
        // short header lands under it.
        auto& db = family.db();
        if (!BEAST_EXPECT(!db.fetchNodeObject(fakeHash, fakeSeq)))
            return;

        // A prefix and four bytes: enough for the sequence, too short for the drops that follow.
        Serializer s;
        s.add32(HashPrefix::LedgerMaster);
        s.add32(0);
        db.store(NodeObjectType::Ledger, std::move(s.modData()), fakeHash, fakeSeq);

        // The node hash only names the report in the log, so any value serves.
        UInt256 const nodeHash{1};

        // Reading the short header throws out of the acquisition.
        bool threw = false;
        try
        {
            family.missingNodeAcquireBySeq(fakeSeq, nodeHash);
        }
        catch (std::runtime_error const&)
        {
            threw = true;
        }
        BEAST_EXPECT(threw);

        // The latch is released, so a report for a lower sequence starts a pass of its own.
        std::uint32_t const lower = fakeSeq - 2;
        auto const lowerHash = master.getHashBySeq(lower);
        if (!BEAST_EXPECT(lowerHash.isNonZero()))
            return;
        BEAST_EXPECT(!ledgers.find(lowerHash));
        family.missingNodeAcquireBySeq(lower, nodeHash);
        BEAST_EXPECT(ledgers.find(lowerHash));
    }

    void
    run() override
    {
        testMissingNodeAcquireBySeqRepeats();
        testMissingNodeAcquireBySeqThrowReleasesLatch();
    }
};

BEAST_DEFINE_TESTSUITE(NodeFamily, app, xrpl);

}  // namespace xrpl::test
