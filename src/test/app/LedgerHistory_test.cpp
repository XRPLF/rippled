#include <test/jtx.h>
#include <test/jtx/CheckMessageLogs.h>

#include <xrpld/app/ledger/LedgerHistory.h>
#include <xrpld/app/ledger/LedgerMaster.h>

#include <xrpl/beast/insight/NullCollector.h>
#include <xrpl/beast/unit_test.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/tx/apply.h>

#include <chrono>
#include <sstream>

namespace xrpl {
namespace test {

class LedgerHistory_test : public beast::unit_test::suite
{
public:
    /** Generate a new ledger by hand, applying a specific close time offset
        and optionally inserting a transaction.

        If prev is nullptr, then the genesis ledger is made and no offset or
        transaction is applied.

    */
    static std::shared_ptr<Ledger>
    makeLedger(
        std::shared_ptr<Ledger const> const& prev,
        jtx::Env& env,
        LedgerHistory& lh,
        NetClock::duration closeOffset,
        std::shared_ptr<STTx const> stx = {})
    {
        if (!prev)
        {
            assert(!stx);
            return std::make_shared<Ledger>(
                create_genesis,
                env.app().config(),
                std::vector<uint256>{},
                env.app().getNodeFamily());
        }
        auto res = std::make_shared<Ledger>(*prev, prev->header().closeTime + closeOffset);

        if (stx)
        {
            OpenView accum(&*res);
            applyTransaction(env.app(), accum, *stx, false, tapNONE, env.journal);
            accum.apply(*res);
        }
        res->updateSkipList();

        {
            res->stateMap().flushDirty(hotACCOUNT_NODE);
            res->txMap().flushDirty(hotTRANSACTION_NODE);
        }
        res->unshare();

        // Accept ledger
        res->setAccepted(
            res->header().closeTime,
            res->header().closeTimeResolution,
            true /* close time correct*/);
        lh.insert(res, false);
        return res;
    }

    void
    testHashIndexInvariant()
    {
        testcase("LedgerHistory hash/index invariant");
        using namespace jtx;
        using namespace std::chrono;

        Env env{*this};
        LedgerHistory lh{beast::insight::NullCollector::New(), env.app()};

        // Create and insert validated ledgers
        auto const genesis = makeLedger({}, env, lh, 0s);
        auto const ledger1 = makeLedger(genesis, env, lh, 4s);
        auto const ledger2 = makeLedger(ledger1, env, lh, 4s);
        auto const ledger3 = makeLedger(ledger2, env, lh, 4s);

        // Insert as validated (so they go into by_index)
        lh.insert(genesis, true);
        lh.insert(ledger1, true);
        lh.insert(ledger2, true);
        lh.insert(ledger3, true);

        // Verify the hash/index invariant holds
        // Can retrieve by sequence and get correct hash
        BEAST_EXPECT(lh.getLedgerHash(genesis->header().seq) == genesis->header().hash);
        BEAST_EXPECT(lh.getLedgerHash(ledger1->header().seq) == ledger1->header().hash);
        BEAST_EXPECT(lh.getLedgerHash(ledger2->header().seq) == ledger2->header().hash);
        BEAST_EXPECT(lh.getLedgerHash(ledger3->header().seq) == ledger3->header().hash);

        // Can retrieve by sequence and get correct ledger
        auto fetched1 = lh.getLedgerBySeq(ledger1->header().seq);
        BEAST_EXPECT(fetched1 != nullptr);
        BEAST_EXPECT(fetched1->header().hash == ledger1->header().hash);

        auto fetched2 = lh.getLedgerBySeq(ledger2->header().seq);
        BEAST_EXPECT(fetched2 != nullptr);
        BEAST_EXPECT(fetched2->header().hash == ledger2->header().hash);

        // Clear ledgers prior to ledger2's sequence
        lh.clearLedgerCachePrior(ledger2->header().seq);

        // Verify old entries are gone from the in-memory by_index map
        // Note: getLedgerHash checks by_index directly without DB fallback
        BEAST_EXPECT(lh.getLedgerHash(genesis->header().seq).isZero());
        BEAST_EXPECT(lh.getLedgerHash(ledger1->header().seq).isZero());

        // Verify newer entries are still present in by_index
        BEAST_EXPECT(lh.getLedgerHash(ledger2->header().seq) == ledger2->header().hash);
        BEAST_EXPECT(lh.getLedgerHash(ledger3->header().seq) == ledger3->header().hash);

        // Verify newer entries remain retrievable and consistent
        // getLedgerBySeq uses by_index first, then falls back to DB if needed
        auto fetched2After = lh.getLedgerBySeq(ledger2->header().seq);
        BEAST_EXPECT(fetched2After != nullptr);
        BEAST_EXPECT(fetched2After->header().hash == ledger2->header().hash);

        auto fetched3After = lh.getLedgerBySeq(ledger3->header().seq);
        BEAST_EXPECT(fetched3After != nullptr);
        BEAST_EXPECT(fetched3After->header().hash == ledger3->header().hash);
    }

    void
    testHandleMismatch()
    {
        testcase("LedgerHistory mismatch");
        using namespace jtx;
        using namespace std::chrono;

        // No mismatch
        {
            bool found = false;
            Env env{*this, envconfig(), std::make_unique<CheckMessageLogs>("MISMATCH ", &found)};
            LedgerHistory lh{beast::insight::NullCollector::New(), env.app()};
            auto const genesis = makeLedger({}, env, lh, 0s);
            uint256 const dummyTxHash{1};
            lh.builtLedger(genesis, dummyTxHash, {});
            lh.validatedLedger(genesis, dummyTxHash);

            BEAST_EXPECT(!found);
        }

        // Close time mismatch
        {
            bool found = false;
            Env env{
                *this,
                envconfig(),
                std::make_unique<CheckMessageLogs>("MISMATCH on close time", &found)};
            LedgerHistory lh{beast::insight::NullCollector::New(), env.app()};
            auto const genesis = makeLedger({}, env, lh, 0s);
            auto const ledgerA = makeLedger(genesis, env, lh, 4s);
            auto const ledgerB = makeLedger(genesis, env, lh, 40s);

            uint256 const dummyTxHash{1};
            lh.builtLedger(ledgerA, dummyTxHash, {});
            lh.validatedLedger(ledgerB, dummyTxHash);

            BEAST_EXPECT(found);
        }

        // Prior ledger mismatch
        {
            bool found = false;
            Env env{
                *this,
                envconfig(),
                std::make_unique<CheckMessageLogs>("MISMATCH on prior ledger", &found)};
            LedgerHistory lh{beast::insight::NullCollector::New(), env.app()};
            auto const genesis = makeLedger({}, env, lh, 0s);
            auto const ledgerA = makeLedger(genesis, env, lh, 4s);
            auto const ledgerB = makeLedger(genesis, env, lh, 40s);
            auto const ledgerAC = makeLedger(ledgerA, env, lh, 4s);
            auto const ledgerBD = makeLedger(ledgerB, env, lh, 4s);

            uint256 const dummyTxHash{1};
            lh.builtLedger(ledgerAC, dummyTxHash, {});
            lh.validatedLedger(ledgerBD, dummyTxHash);

            BEAST_EXPECT(found);
        }

        // Simulate a bug in which consensus may agree on transactions, but
        // somehow generate different ledgers
        for (bool const txBug : {true, false})
        {
            std::string const msg = txBug ? "MISMATCH with same consensus transaction set"
                                          : "MISMATCH on consensus transaction set";
            bool found = false;
            Env env{*this, envconfig(), std::make_unique<CheckMessageLogs>(msg, &found)};
            LedgerHistory lh{beast::insight::NullCollector::New(), env.app()};

            Account alice{"A1"};
            Account bob{"A2"};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const ledgerBase = env.app().getLedgerMaster().getClosedLedger();

            JTx txAlice = env.jt(noop(alice));
            auto const ledgerA = makeLedger(ledgerBase, env, lh, 4s, txAlice.stx);

            JTx txBob = env.jt(noop(bob));
            auto const ledgerB = makeLedger(ledgerBase, env, lh, 4s, txBob.stx);

            lh.builtLedger(ledgerA, txAlice.stx->getTransactionID(), {});
            // Simulate the bug by claiming ledgerB had the same consensus hash
            // as ledgerA, but somehow generated different ledgers
            lh.validatedLedger(
                ledgerB, txBug ? txAlice.stx->getTransactionID() : txBob.stx->getTransactionID());

            BEAST_EXPECT(found);
        }
    }

    void
    run() override
    {
        testHashIndexInvariant();
        testHandleMismatch();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerHistory, app, xrpl);

}  // namespace test
}  // namespace xrpl
