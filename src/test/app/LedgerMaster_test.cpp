#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>
#include <test/jtx/noop.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/misc/SHAMapStore.h>
#include <xrpld/core/Config.h>

#include <xrpl/basics/ToString.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/protocol/Feature.h>
#include <xrpl/protocol/Protocol.h>
#include <xrpl/protocol/SField.h>
#include <xrpl/protocol/STObject.h>
#include <xrpl/protocol/STTx.h>

#include <chrono>
#include <cstdint>
#include <memory>
#include <sstream>
#include <vector>

namespace xrpl::test {

class LedgerMaster_test : public beast::unit_test::Suite
{
    static std::unique_ptr<Config>
    makeNetworkConfig(uint32_t networkID)
    {
        using namespace jtx;
        return envconfig([&](std::unique_ptr<Config> cfg) {
            cfg->networkId = networkID;
            // This test relies on ledger hash so must lock it to fee 10.
            cfg->fees.referenceFee = 10;
            return cfg;
        });
    }

    void
    testTxnIdFromIndex(FeatureBitset features)
    {
        testcase("tx_id_from_index");

        using namespace test::jtx;
        using namespace std::literals;

        test::jtx::Env env{*this, makeNetworkConfig(11111)};

        auto const alice = Account("alice");
        env.fund(XRP(1000), alice);
        env.close();

        // build ledgers
        std::vector<std::shared_ptr<STTx const>> txns;
        std::vector<std::shared_ptr<STObject const>> metas;
        auto const startLegSeq = env.current()->header().seq;
        for (int i = 0; i < 2; ++i)
        {
            env(noop(alice));
            txns.emplace_back(env.tx());
            env.close();
            metas.emplace_back(env.closed()->txRead(env.tx()->getTransactionID()).second);
        }
        // add last (empty) ledger
        env.close();
        auto const endLegSeq = env.closed()->header().seq;

        // test invalid range
        {
            std::uint32_t const ledgerSeq = -1;
            std::uint32_t const txnIndex = 0;
            auto result = env.app().getLedgerMaster().txnIdFromIndex(ledgerSeq, txnIndex);
            BEAST_EXPECT(!result);
        }
        // test not in ledger
        {
            uint32_t const txnIndex = metas[0]->getFieldU32(sfTransactionIndex);
            auto result = env.app().getLedgerMaster().txnIdFromIndex(0, txnIndex);
            BEAST_EXPECT(!result);
        }
        // test empty ledger
        {
            auto result = env.app().getLedgerMaster().txnIdFromIndex(endLegSeq, 0);
            BEAST_EXPECT(!result);
        }
        // ended without result
        {
            uint32_t const txnIndex = metas[0]->getFieldU32(sfTransactionIndex);
            auto result = env.app().getLedgerMaster().txnIdFromIndex(endLegSeq + 1, txnIndex);
            BEAST_EXPECT(!result);
        }
        // success (first tx)
        {
            uint32_t const txnIndex = metas[0]->getFieldU32(sfTransactionIndex);
            auto result = env.app().getLedgerMaster().txnIdFromIndex(startLegSeq, txnIndex);
            BEAST_EXPECT(
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                *result ==
                uint256(
                    "277F4FD89C20B92457FEF05FF63F6405563AD0563C73D967A29727"
                    "72679ADC65"));
        }
        // success (second tx)
        {
            uint32_t const txnIndex = metas[1]->getFieldU32(sfTransactionIndex);
            auto result = env.app().getLedgerMaster().txnIdFromIndex(startLegSeq + 1, txnIndex);
            BEAST_EXPECT(
                // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
                *result ==
                uint256(
                    "293DF7335EBBAF4420D52E70ABF470EB4C5792CAEA2F91F76193C2"
                    "819F538FDE"));
        }
    }

    // Wait until the SHAMapStore has finished processing the ledger that the
    // preceding env.close() produced.
    //
    // env.close() returns as soon as the ledger_accept RPC returns, but the
    // validated ledger path -- LedgerMaster::setValidLedger() ->
    // SHAMapStore::onLedgerClosed() -- runs on a job queue thread. Without
    // draining the job queue first, the store may not have been handed the
    // ledger at all, in which case rendezvous() observes working_ == false and
    // returns immediately, before any work has been done.
    [[nodiscard]] static bool
    syncStore(jtx::Env& env)
    {
        // Drain the job queue first, so that onLedgerClosed() has run and
        // working_ is set. Then use the store's timeout overload, so a store
        // that never finishes fails this test instead of blocking on it.
        //
        // Only the second wait is bounded: JobQueue::rendezvous() has no
        // timeout overload, so a job that never completes hangs here. That is
        // pre-existing -- ~AppBundle waits on it the same way for every jtx
        // test -- but it does mean this helper is not hang-proof end to end.
        env.app().getJobQueue().rendezvous();
        return env.app().getSHAMapStore().rendezvous(std::chrono::seconds{60});
    }

    void
    testCompleteLedgerRange(FeatureBitset features)
    {
        // Note that this test is intentionally very similar to
        // SHAMapStore_test::testLedgerGaps, but has a different
        // focus.

        testcase("Complete Ledger operations");

        using namespace test::jtx;

        auto const deleteInterval = 8;

        Env env{*this, envconfig(onlineDelete, deleteInterval)};

        auto const alice = Account("alice");
        env.fund(XRP(1000), alice);
        env.close();

        auto& lm = env.app().getLedgerMaster();
        LedgerIndex minSeq = 2;
        LedgerIndex maxSeq = env.closed()->header().seq;
        auto& store = env.app().getSHAMapStore();
        if (!BEAST_EXPECT(syncStore(env)))
            return;
        LedgerIndex lastRotated = store.getLastRotated();
        BEAST_EXPECTS(maxSeq == 3, to_string(maxSeq));
        BEAST_EXPECTS(lm.getCompleteLedgers() == "2-3", lm.getCompleteLedgers());
        // The store initializes lastRotated from the first validated ledger it
        // observes. onLedgerClosed() keeps only the most recent ledger in
        // newLedger_, so validated ledgers that arrive while the store thread
        // is still busy are coalesced away. Which of the existing complete
        // ledgers therefore wins is a timing detail; all this test needs is
        // that it is one of them. Everything below derives from the observed
        // value rather than assuming a particular one.
        //
        // This check and the syncStore() one above both end the testcase rather
        // than merely reporting, because lastRotated is the only value from the
        // store that enters minSeq. A lastRotated of 0 -- the value
        // getLastRotated() reports until the store has been handed a
        // validated ledger -- makes minSeq 0 below, and the minSeq - 1 and
        // minSeq - 2 ranges then underflow to first > last, which aborts a
        // Debug build inside missingFromCompleteLedgerRange().
        if (!BEAST_EXPECTS(lastRotated >= minSeq && lastRotated <= maxSeq, to_string(lastRotated)))
            return;
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == 0);
        BEAST_EXPECT(minSeq + 1 > maxSeq - 1);
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq - 1, maxSeq + 1) == 2);
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq - 2, maxSeq - 2) == 2);
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq + 2, maxSeq + 2) == 2);

        // Close enough ledgers to rotate a few times
        for (int i = 0; i < 24; ++i)
        {
            for (int t = 0; t < 3; ++t)
            {
                env(noop(alice));
            }
            env.close();
            BEAST_EXPECT(syncStore(env));

            ++maxSeq;

            if (maxSeq == lastRotated + deleteInterval)
            {
                minSeq = lastRotated;
                lastRotated = maxSeq;
            }
            BEAST_EXPECTS(
                env.closed()->header().seq == maxSeq, to_string(env.closed()->header().seq));
            BEAST_EXPECTS(store.getLastRotated() == lastRotated, to_string(store.getLastRotated()));
            std::stringstream expectedRange;
            expectedRange << minSeq << "-" << maxSeq;
            BEAST_EXPECTS(lm.getCompleteLedgers() == expectedRange.str(), lm.getCompleteLedgers());
            BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == 0);
            // missingFromCompleteLedgerRange() treats first > last as a
            // precondition violation and aborts a Debug build via UNREACHABLE.
            // The range can only collapse if this test's model of minSeq /
            // maxSeq has desynced from the store, so report that as a failure
            // instead of taking down the whole unit test job.
            if (minSeq + 1 <= maxSeq - 1)
            {
                BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq + 1, maxSeq - 1) == 0);
            }
            else
            {
                BEAST_EXPECTS(false, to_string(minSeq) + "-" + to_string(maxSeq));
            }
            BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq - 1, maxSeq + 1) == 2);
            BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq - 2, maxSeq - 2) == 2);
            BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq + 2, maxSeq + 2) == 2);
        }
    }

public:
    void
    run() override
    {
        using namespace test::jtx;
        FeatureBitset const all{testableAmendments()};
        testWithFeats(all);
    }

    void
    testWithFeats(FeatureBitset features)
    {
        testTxnIdFromIndex(features);
        testCompleteLedgerRange(features);
    }
};

BEAST_DEFINE_TESTSUITE(LedgerMaster, app, xrpl);

}  // namespace xrpl::test
