#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/ledger/PendingSaves.h>
#include <xrpl/protocol/Protocol.h>

namespace xrpl::test {

struct PendingSaves_test : public beast::unit_test::Suite
{
    void
    testSaves()
    {
        constexpr LedgerIndex kSeq = 0;
        PendingSaves ps;

        // Basic test
        BEAST_EXPECT(!ps.pending(kSeq));
        BEAST_EXPECT(!ps.startWork(kSeq));
        BEAST_EXPECT(ps.shouldWork(kSeq));
        BEAST_EXPECT(ps.startWork(kSeq));
        BEAST_EXPECT(ps.pending(kSeq));
        BEAST_EXPECT(!ps.shouldWork(kSeq));
        ps.finishWork(kSeq);
        BEAST_EXPECT(!ps.pending(kSeq));

        // Test work stealing
        BEAST_EXPECT(ps.shouldWork(kSeq));
        BEAST_EXPECT(ps.pending(kSeq));
        ps.claimWork(kSeq);
        BEAST_EXPECT(ps.pending(kSeq));
        BEAST_EXPECT(!ps.startWork(kSeq));
        ps.finishWork(kSeq);
        BEAST_EXPECT(!ps.pending(kSeq));
    }

    void
    run() override
    {
        testSaves();
    }
};

BEAST_DEFINE_TESTSUITE(PendingSaves, ledger, xrpl);

}  // namespace xrpl::test
