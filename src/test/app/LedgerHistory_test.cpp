#include <test/jtx/Account.h>
#include <test/jtx/CheckMessageLogs.h>
#include <test/jtx/Env.h>
#include <test/jtx/JTx.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/noop.h>

#include <xrpld/app/ledger/LedgerHistory.h>
#include <xrpld/app/ledger/LedgerMaster.h>

#include <xrpl/basics/base_uint.h>
#include <xrpl/basics/chrono.h>
#include <xrpl/beast/insight/NullCollector.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/json/json_value.h>
#include <xrpl/ledger/ApplyView.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/OpenView.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/STTx.h>
#include <xrpl/tx/apply.h>

#include <cassert>
#include <memory>
#include <vector>

namespace xrpl::test {

class LedgerHistory_test : public beast::unit_test::Suite
{
public:
    /**
     * Generate a new ledger by hand, applying a specific close time offset
     * and optionally inserting a transaction.
     *
     * If prev is nullptr, then the genesis ledger is made and no offset or
     * transaction is applied.
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
                kCreateGenesis,
                Rules{env.app().config().features},
                env.app().config().fees.toFees(),
                std::vector<UInt256>{},
                env.app().getNodeFamily());
        }
        auto res = std::make_shared<Ledger>(*prev, prev->header().closeTime + closeOffset);

        if (stx)
        {
            OpenView accum(&*res);
            applyTransaction(env.app(), accum, *stx, false, TapNone, env.journal);
            accum.apply(*res);
        }
        res->updateSkipList();

        {
            res->stateMap().flushDirty(NodeObjectType::AccountNode);
            res->txMap().flushDirty(NodeObjectType::TransactionNode);
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
        LedgerHistory lh{beast::insight::NullCollector::make(), env.app()};

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
        if (BEAST_EXPECT(fetched1 != nullptr))
            BEAST_EXPECT(fetched1->header().hash == ledger1->header().hash);

        auto fetched2 = lh.getLedgerBySeq(ledger2->header().seq);
        if (BEAST_EXPECT(fetched2 != nullptr))
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

        // The by_hash cache must be pruned to the same cutoff. A cache hit
        // hands back the very object that was inserted, so identity tells a
        // hit from a miss. (A null check would not: ledger1 is a transaction-
        // free successor of genesis and therefore has the same hash as the
        // Env's own ledger 2, which getLedgerByHash can reload from SQL.)
        BEAST_EXPECT(lh.getLedgerByHash(ledger2->header().hash).get() == ledger2.get());
        BEAST_EXPECT(lh.getLedgerByHash(ledger3->header().hash).get() == ledger3.get());
        BEAST_EXPECT(lh.getLedgerByHash(ledger1->header().hash).get() != ledger1.get());

        // Verify newer entries remain retrievable and consistent
        // getLedgerBySeq uses by_index first, then falls back to DB if needed
        auto fetched2After = lh.getLedgerBySeq(ledger2->header().seq);
        if (BEAST_EXPECT(fetched2After != nullptr))
            BEAST_EXPECT(fetched2After->header().hash == ledger2->header().hash);

        auto fetched3After = lh.getLedgerBySeq(ledger3->header().seq);
        if (BEAST_EXPECT(fetched3After != nullptr))
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
            LedgerHistory lh{beast::insight::NullCollector::make(), env.app()};
            auto const genesis = makeLedger({}, env, lh, 0s);
            UInt256 const dummyTxHash{1};
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
            LedgerHistory lh{beast::insight::NullCollector::make(), env.app()};
            auto const genesis = makeLedger({}, env, lh, 0s);
            auto const ledgerA = makeLedger(genesis, env, lh, 4s);
            auto const ledgerB = makeLedger(genesis, env, lh, 40s);

            UInt256 const dummyTxHash{1};
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
            LedgerHistory lh{beast::insight::NullCollector::make(), env.app()};
            auto const genesis = makeLedger({}, env, lh, 0s);
            auto const ledgerA = makeLedger(genesis, env, lh, 4s);
            auto const ledgerB = makeLedger(genesis, env, lh, 40s);
            auto const ledgerAC = makeLedger(ledgerA, env, lh, 4s);
            auto const ledgerBD = makeLedger(ledgerB, env, lh, 4s);

            UInt256 const dummyTxHash{1};
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
            LedgerHistory lh{beast::insight::NullCollector::make(), env.app()};

            Account const alice{"A1"};
            Account const bob{"A2"};
            env.fund(XRP(1000), alice, bob);
            env.close();

            auto const ledgerBase = env.app().getLedgerMaster().getClosedLedger();

            JTx const txAlice = env.jt(noop(alice));
            auto const ledgerA = makeLedger(ledgerBase, env, lh, 4s, txAlice.stx);

            JTx const txBob = env.jt(noop(bob));
            auto const ledgerB = makeLedger(ledgerBase, env, lh, 4s, txBob.stx);

            lh.builtLedger(ledgerA, txAlice.stx->getTransactionID(), {});
            // Simulate the bug by claiming ledgerB had the same consensus hash
            // as ledgerA, but somehow generated different ledgers
            lh.validatedLedger(
                ledgerB, txBug ? txAlice.stx->getTransactionID() : txBob.stx->getTransactionID());

            BEAST_EXPECT(found);
        }

        // Reverse order: validatedLedger arrives first, then builtLedger
        // detects the mismatch. Covers the mismatch branch in builtLedger.
        {
            bool found = false;
            Env env{
                *this,
                envconfig(),
                std::make_unique<CheckMessageLogs>("MISMATCH on close time", &found)};
            LedgerHistory lh{beast::insight::NullCollector::make(), env.app()};
            auto const genesis = makeLedger({}, env, lh, 0s);
            auto const ledgerA = makeLedger(genesis, env, lh, 4s);
            auto const ledgerB = makeLedger(genesis, env, lh, 40s);

            UInt256 const dummyTxHash{1};
            lh.validatedLedger(ledgerB, dummyTxHash);
            lh.builtLedger(ledgerA, dummyTxHash, {});

            BEAST_EXPECT(found);
        }

        // The consensus JSON is captured inside the cache lock and handed to
        // handleMismatch after it is released. The payload must survive that
        // hand-off whichever side reports first: builtLedger passes its own
        // argument, validatedLedger the copy stored in the cache entry.
        for (bool const builtFirst : {true, false})
        {
            bool found = false;
            // handleMismatch logs the consensus data at debug level, so raise
            // the Env's log threshold (it defaults to Error) to capture it.
            Env env{
                *this,
                envconfig(),
                std::make_unique<CheckMessageLogs>("consensus-payload-marker", &found),
                beast::Severity::Debug};
            LedgerHistory lh{beast::insight::NullCollector::make(), env.app()};
            auto const genesis = makeLedger({}, env, lh, 0s);
            auto const ledgerA = makeLedger(genesis, env, lh, 4s);
            auto const ledgerB = makeLedger(genesis, env, lh, 40s);

            json::Value consensus{json::ValueType::Object};
            consensus["marker"] = "consensus-payload-marker";

            UInt256 const dummyTxHash{1};
            if (builtFirst)
            {
                lh.builtLedger(ledgerA, dummyTxHash, consensus);
                lh.validatedLedger(ledgerB, dummyTxHash);
            }
            else
            {
                lh.validatedLedger(ledgerB, dummyTxHash);
                lh.builtLedger(ledgerA, dummyTxHash, consensus);
            }

            BEAST_EXPECT(found);
        }
    }

    void
    testFixIndex()
    {
        testcase("LedgerHistory fixIndex");
        using namespace jtx;
        using namespace std::chrono;

        Env env{*this};
        LedgerHistory lh{beast::insight::NullCollector::make(), env.app()};

        auto const genesis = makeLedger({}, env, lh, 0s);
        auto const ledger1 = makeLedger(genesis, env, lh, 4s);
        lh.insert(ledger1, true);

        // Unknown index: returns true, no repair.
        BEAST_EXPECT(lh.fixIndex(999, ledger1->header().hash));

        // Known index with the same hash: returns true, no repair.
        BEAST_EXPECT(lh.fixIndex(ledger1->header().seq, ledger1->header().hash));

        // Known index with a different hash: returns false and repairs.
        UInt256 const bogusHash{42};
        BEAST_EXPECT(!lh.fixIndex(ledger1->header().seq, bogusHash));
        BEAST_EXPECT(lh.getLedgerHash(ledger1->header().seq) == bogusHash);
    }

    void
    run() override
    {
        testHashIndexInvariant();
        testHandleMismatch();
        testFixIndex();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerHistory, app, xrpl);

}  // namespace xrpl::test
