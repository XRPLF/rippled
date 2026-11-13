#include <test/jtx/Account.h>
#include <test/jtx/Env.h>
#include <test/jtx/amount.h>
#include <test/jtx/envconfig.h>

#include <xrpld/app/ledger/LedgerMaster.h>
#include <xrpld/app/ledger/LedgerPersistence.h>

#include <xrpl/beast/unit_test/suite.h>
#include <xrpl/core/HashRouter.h>
#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/ledger/Ledger.h>
#include <xrpl/ledger/PendingSaves.h>
#include <xrpl/nodestore/NodeObject.h>
#include <xrpl/protocol/Serializer.h>
#include <xrpl/rdb/RelationalDatabase.h>
#include <xrpl/shamap/Family.h>
#include <xrpl/shamap/SHAMap.h>
#include <xrpl/shamap/SHAMapTreeNode.h>

#include <latch>
#include <memory>
#include <thread>
#include <utility>

namespace xrpl::test {

class LedgerPersistence_test : public beast::unit_test::Suite
{
    /**
     * Close a ledger and wait until nothing else is saving it.
     *
     * Publication saves the ledger again on a job thread. Waiting for it keeps
     * the saves a test makes from interleaving with that write.
     *
     * @param env The environment to close a ledger in.
     * @return The closed ledger.
     */
    std::shared_ptr<Ledger const>
    closeAndSettle(jtx::Env& env)
    {
        env.close();
        auto& lm = env.app().getLedgerMaster();
        auto const ledger = lm.getClosedLedger();

        env.app().getJobQueue().rendezvous();
        auto const published = lm.getPublishedLedger();
        BEAST_EXPECT(published && published->header().seq >= ledger->header().seq);
        return ledger;
    }

    /**
     * A synchronous save must write the ledger even when the SAVED flag is set.
     *
     * A save that throws deletes the ledger's row before it fails, and its
     * cleanup clears the SAVED flag separately from the pending entry. A
     * synchronous retry can observe the flag set and no pending entry at the
     * same time. That state is reproduced here directly: the row is deleted
     * while the flag stays set. The retry must then write the row again
     * instead of trusting the flag.
     */
    void
    testSynchronousSaveWritesWhenFlagged()
    {
        testcase("synchronous save writes when flagged");
        using namespace jtx;

        Env env(*this);
        auto const ledger = closeAndSettle(env);
        auto const seq = ledger->header().seq;
        auto& db = env.app().getRelationalDatabase();

        // The first save writes the row and sets the SAVED flag.
        BEAST_EXPECT(pendSaveValidated(env.app(), ledger, true, false));
        BEAST_EXPECT(db.getLedgerInfoByIndex(seq).has_value());
        BEAST_EXPECT(any(
            env.app().getHashRouter().getFlags(ledger->header().hash) & HashRouterFlags::SAVED));

        // The state a failed save leaves behind between its two cleanup steps.
        db.deleteBeforeLedgerSeq(seq + 1);
        BEAST_EXPECT(!db.getLedgerInfoByIndex(seq).has_value());
        BEAST_EXPECT(!env.app().getPendingSaves().pending(seq));

        // A synchronous retry writes the row again.
        BEAST_EXPECT(pendSaveValidated(env.app(), ledger, true, false));
        BEAST_EXPECT(db.getLedgerInfoByIndex(seq).has_value());
    }

    /**
     * A synchronous save must wait for a save in progress, then write itself.
     *
     * The save in progress stands in for one that fails after deleting the
     * row. A synchronous caller that returned on the strength of that save
     * would report a ledger the database does not hold.
     */
    void
    testSynchronousSaveWaitsForSaveInProgress()
    {
        testcase("synchronous save waits for a save in progress");
        using namespace jtx;

        Env env(*this);
        auto const ledger = closeAndSettle(env);
        auto const seq = ledger->header().seq;
        auto& db = env.app().getRelationalDatabase();
        auto& saves = env.app().getPendingSaves();

        // Another thread is saving this ledger and has deleted its row so far.
        BEAST_EXPECT(saves.shouldWork(seq));
        BEAST_EXPECT(saves.startWork(seq));
        db.deleteBeforeLedgerSeq(seq + 1);

        // That the claim waits is checked by the PendingSaves gtests, where no database write
        // sits between the claim and the check. Here the end result is checked: once the save in
        // progress has released the sequence without a row, the synchronous save writes it.
        std::latch started{1};
        std::thread saver([&] {
            started.count_down();
            pendSaveValidated(env.app(), ledger, true, false);
        });

        started.wait();
        saves.finishWork(seq);
        saver.join();
        BEAST_EXPECT(db.getLedgerInfoByIndex(seq).has_value());
        BEAST_EXPECT(!saves.pending(seq));
    }

    /**
     * An asynchronous save runs on the job queue, and a repeat is deduplicated.
     *
     * The SAVED flag is cleared first, since the close already saved the
     * ledger. With the flag set again by the first request, a second request
     * schedules nothing.
     */
    void
    testAsynchronousSaveRunsOnJobQueue()
    {
        testcase("asynchronous save runs on the job queue");
        using namespace jtx;

        Env env(*this);
        auto const ledger = closeAndSettle(env);
        auto const seq = ledger->header().seq;
        auto& db = env.app().getRelationalDatabase();
        auto& saves = env.app().getPendingSaves();

        BEAST_EXPECT(
            env.app().getHashRouter().clearFlags(ledger->header().hash, HashRouterFlags::SAVED));
        db.deleteBeforeLedgerSeq(seq + 1);
        BEAST_EXPECT(!db.getLedgerInfoByIndex(seq).has_value());

        // The request returns at once, and the job writes the row.
        BEAST_EXPECT(pendSaveValidated(env.app(), ledger, false, false));
        env.app().getJobQueue().rendezvous();
        BEAST_EXPECT(db.getLedgerInfoByIndex(seq).has_value());
        BEAST_EXPECT(!saves.pending(seq));

        // A repeat is deduplicated by the flag, so nothing is scheduled.
        db.deleteBeforeLedgerSeq(seq + 1);
        BEAST_EXPECT(pendSaveValidated(env.app(), ledger, false, false));
        BEAST_EXPECT(!saves.pending(seq));
        env.app().getJobQueue().rendezvous();
        BEAST_EXPECT(!db.getLedgerInfoByIndex(seq).has_value());
    }

    /**
     * A queued save that lost the write to a synchronous caller does nothing.
     *
     * The job queue runs at most two old-ledger publish jobs at once. Two jobs
     * that wait on a latch hold both slots, so the save job stays queued while
     * a synchronous caller claims and performs the write. Once released, the
     * job finds the work gone and aborts.
     */
    void
    testQueuedSaveAbortsWhenWriteWasTaken()
    {
        testcase("queued save aborts when the write was taken");
        using namespace jtx;

        // Standalone mode runs one job thread. Use enough that the two held
        // slots cannot starve the queue.
        Env env(*this, envconfig([](std::unique_ptr<Config> cfg) {
            cfg->forceMultiThread = true;
            cfg->workers = 4;
            return cfg;
        }));
        auto const ledger = closeAndSettle(env);
        auto const seq = ledger->header().seq;
        auto& db = env.app().getRelationalDatabase();
        auto& jobs = env.app().getJobQueue();
        auto& saves = env.app().getPendingSaves();

        std::latch holding{2};
        std::latch release{1};
        for (int i = 0; i < 2; ++i)
        {
            BEAST_EXPECT(jobs.addJob(JtPuboldledger, "hold", [&] {
                holding.count_down();
                release.wait();
            }));
        }
        // Both slots are taken once both jobs have started.
        holding.wait();

        BEAST_EXPECT(
            env.app().getHashRouter().clearFlags(ledger->header().hash, HashRouterFlags::SAVED));
        db.deleteBeforeLedgerSeq(seq + 1);

        // The save job is queued behind the held slots.
        BEAST_EXPECT(pendSaveValidated(env.app(), ledger, false, false));
        BEAST_EXPECT(saves.pending(seq));
        BEAST_EXPECT(!db.getLedgerInfoByIndex(seq).has_value());

        // A synchronous caller takes the scheduled work and writes.
        BEAST_EXPECT(pendSaveValidated(env.app(), ledger, true, false));
        BEAST_EXPECT(db.getLedgerInfoByIndex(seq).has_value());
        BEAST_EXPECT(!saves.pending(seq));

        // The job now runs, finds the work gone, and leaves the row alone.
        release.count_down();
        jobs.rendezvous();
        BEAST_EXPECT(db.getLedgerInfoByIndex(seq).has_value());
        BEAST_EXPECT(!saves.pending(seq));
    }

    /**
     * Build a ledger whose transaction map cannot be read below its root.
     *
     * The next ledger is built in memory with the closed ledger's transactions,
     * and only the root node of its transaction map is written to the node
     * store. A ledger loaded from the header then reads a hole under that
     * root, which is the state a save finds when the accepted ledger is
     * missing nodes.
     *
     * @param env The environment whose closed ledger and node store are used.
     * @param closed The closed ledger to build on.
     * @return The loaded ledger with the hole.
     */
    std::shared_ptr<Ledger const>
    ledgerMissingTransactionNodes(jtx::Env& env, std::shared_ptr<Ledger const> const& closed)
    {
        // The keys are altered, since a leaf equal to one of the closed ledger's would already
        // be in the node store.
        auto const next = std::make_shared<Ledger>(*closed, env.timeKeeper().closeTime());
        for (auto const& [tx, meta] : closed->txs)
        {
            auto key = tx->getTransactionID();
            *key.begin() ^= 0xFF;
            next->rawTxInsert(
                key,
                std::make_shared<Serializer const>(tx->getSerializer()),
                std::make_shared<Serializer const>(meta->getSerializer()));
        }
        BEAST_EXPECT(next->setImmutable());

        // visitNodes reports the root first. Stop after it, so every node below stays absent.
        auto& family = env.app().getNodeFamily();
        bool stored = false;
        next->txMap().visitNodes([&](SHAMapTreeNode& node) {
            if (stored)
                return false;
            Serializer s;
            node.serializeWithPrefix(s);
            family.db().store(
                NodeObjectType::TransactionNode,
                std::move(s.modData()),
                node.getHash().asUInt256(),
                next->header().seq);
            stored = true;
            return true;
        });

        auto const ledger =
            loadLedgerHelper(next->header(), closed->rules(), closed->fees(), env.app(), false);
        BEAST_EXPECT(ledger && ledger->isImmutable());
        return ledger;
    }

    /**
     * A save the database reports as not done must leave nothing behind.
     *
     * The backend returns false, without throwing, when the accepted ledger is
     * missing nodes. The SAVED flag must then be cleared, so a later save is
     * not deduplicated away, and setFullLedger() must not leave the sequence
     * recorded as complete, since the database does not hold it. That holds
     * for a synchronous save and for one that fails on a job thread.
     */
    void
    testFailedSaveReleasesTheLedger()
    {
        testcase("save that reports failure releases the ledger");
        using namespace jtx;

        Env env(*this);
        Account const alice{"alice"};
        env.fund(XRP(10000), alice);
        auto const closed = closeAndSettle(env);
        BEAST_EXPECT(closed->txs.begin() != closed->txs.end());

        auto const ledger = ledgerMissingTransactionNodes(env, closed);
        if (!BEAST_EXPECT(ledger))
            return;
        auto const seq = ledger->header().seq;
        auto& db = env.app().getRelationalDatabase();
        auto& lm = env.app().getLedgerMaster();
        auto& router = env.app().getHashRouter();

        // The synchronous save reports failure, and the flag it set is gone again.
        BEAST_EXPECT(!pendSaveValidated(env.app(), ledger, true, false));
        BEAST_EXPECT(!db.getLedgerInfoByIndex(seq).has_value());
        BEAST_EXPECT(!any(router.getFlags(ledger->header().hash) & HashRouterFlags::SAVED));
        BEAST_EXPECT(!env.app().getPendingSaves().pending(seq));

        // Accepting the ledger does not advertise a sequence the database does not hold.
        lm.setFullLedger(ledger, true, false);
        BEAST_EXPECT(!lm.haveLedger(seq));
        BEAST_EXPECT(!any(router.getFlags(ledger->header().hash) & HashRouterFlags::SAVED));

        // Nor does an asynchronous acceptance, whose save fails on a job thread. The range is
        // registered before the job is queued, so the job's removal cannot be undone by it.
        lm.setFullLedger(ledger, false, false);
        env.app().getJobQueue().rendezvous();
        BEAST_EXPECT(!lm.haveLedger(seq));
        BEAST_EXPECT(!any(router.getFlags(ledger->header().hash) & HashRouterFlags::SAVED));
        BEAST_EXPECT(!env.app().getPendingSaves().pending(seq));
    }

    void
    run() override
    {
        testSynchronousSaveWritesWhenFlagged();
        testSynchronousSaveWaitsForSaveInProgress();
        testAsynchronousSaveRunsOnJobQueue();
        testQueuedSaveAbortsWhenWriteWasTaken();
        testFailedSaveReleasesTheLedger();
    }
};

BEAST_DEFINE_TESTSUITE(LedgerPersistence, app, xrpl);

}  // namespace xrpl::test
