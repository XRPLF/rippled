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
#include <optional>
#include <ostream>
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

    // Bring the SHAMapStore to the point where it has been handed a validated
    // ledger and initialized lastRotated, and report how many extra ledgers had
    // to be closed to get it there (normally none). Returns std::nullopt if
    // syncStore() itself failed.
    //
    // syncStore() alone does not guarantee that, because
    // SHAMapStoreImp::run()'s loop does not use the notification and the
    // working_ flag safely:
    //
    //   * onLedgerClosed() notifies cond_ whether or not run()'s thread is
    //     parked on it, and run() waits on cond_ without a predicate, so a
    //     notification that lands while the thread is still starting up --
    //     before it first reaches that wait -- is lost.
    //   * run() clears working_ at the top of its loop without checking
    //     whether newLedger_ is still set, so rendezvous() can report the
    //     store idle with a validated ledger queued.
    //
    // Either way the store ends up parked with work pending, and only another
    // notification gets it moving again. In a standalone test nothing else
    // closes ledgers, so that has to come from here: this closes a ledger
    // rather than polling getLastRotated(), because polling would just time
    // out. onLedgerClosed() keeps only the most recent ledger in newLedger_,
    // so the ledger the store picks up -- and therefore lastRotated -- is a
    // timing detail, which is why the caller derives its expectations from the
    // value it observes instead of assuming one.
    //
    // run() is deliberately left as it is. In production the only effect is
    // latency: the trigger is validatedSeq >= lastRotated + deleteInterval, so
    // a lost notification delays rotation to the next validated ledger and
    // nothing is skipped or accumulated -- starting at 513 instead of 512 does
    // not matter. Two consequences do follow from leaving it in place, and both
    // hold today: nothing in production decides anything from working_ or
    // rendezvous() (rendezvous() has no production callers at all), and a node
    // whose ledgers only advance on demand -- standalone, driven by
    // ledger_accept -- can sit on a queued ledger until something closes the
    // next one, which is exactly the situation this helper is working around.
    //
    // So this helper is permanent rather than a stopgap. Working around the
    // race must not make it invisible, so every extra close is logged. That
    // keeps how often it is actually hit observable in the unit test output --
    // which is the only signal left once this testcase stops flaking on it.
    [[nodiscard]] std::optional<int>
    initializeStore(jtx::Env& env, int const maxExtraCloses = 3)
    {
        auto& store = env.app().getSHAMapStore();

        for (int extraCloses = 0;; ++extraCloses)
        {
            if (!syncStore(env))
                return std::nullopt;
            if (store.getLastRotated() != 0 || extraCloses == maxExtraCloses)
            {
                if (extraCloses != 0)
                {
                    log << "initializeStore: the store needed " << extraCloses
                        << " extra ledger close(s) to pick up a validated ledger. "
                           "SHAMapStoreImp::run() dropped the notification for the "
                           "first one; see the comment on initializeStore()."
                        << std::endl;
                }
                return extraCloses;
            }
            env.close();
        }
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
        auto& store = env.app().getSHAMapStore();
        // Which of the existing complete ledgers the store initializes
        // lastRotated from is a timing detail; all this test needs is that it is
        // one of them. Everything below derives from the observed value rather
        // than assuming a particular one.
        //
        // The range check and the initializeStore() one both end the testcase
        // rather than merely reporting, because lastRotated is the only value
        // from the store that enters minSeq. A lastRotated of 0 -- the value
        // getLastRotated() reports until the store has been handed a
        // validated ledger -- makes minSeq 0 below, and the minSeq - 1 and
        // minSeq - 2 ranges then underflow to first > last, which aborts a
        // Debug build inside missingFromCompleteLedgerRange().
        auto const extraCloses = initializeStore(env);
        if (!BEAST_EXPECT(extraCloses.has_value()))
            return;
        LedgerIndex maxSeq = env.closed()->header().seq;
        LedgerIndex lastRotated = store.getLastRotated();
        if (!BEAST_EXPECTS(lastRotated >= minSeq && lastRotated <= maxSeq, to_string(lastRotated)))
            return;
        // The BEAST_EXPECT above already returned if this is nullopt, but that
        // is invisible to clang-tidy's optional model.
        // NOLINTNEXTLINE(bugprone-unchecked-optional-access)
        BEAST_EXPECTS(maxSeq == 3 + *extraCloses, to_string(maxSeq));
        std::stringstream initialRange;
        initialRange << minSeq << "-" << maxSeq;
        BEAST_EXPECTS(lm.getCompleteLedgers() == initialRange.str(), lm.getCompleteLedgers());
        BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq, maxSeq) == 0);
        // The inner range is empty unless initializeStore() had to close extra
        // ledgers, and missingFromCompleteLedgerRange() treats first > last as a
        // precondition violation that aborts a Debug build via UNREACHABLE, so
        // only check it when it is well formed.
        if (minSeq + 1 <= maxSeq - 1)
        {
            BEAST_EXPECT(lm.missingFromCompleteLedgerRange(minSeq + 1, maxSeq - 1) == 0);
        }
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
