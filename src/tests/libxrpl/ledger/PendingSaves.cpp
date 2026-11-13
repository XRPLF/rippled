#include <xrpl/ledger/PendingSaves.h>

#include <xrpl/protocol/Protocol.h>

#include <gtest/gtest.h>

#include <atomic>
#include <latch>
#include <thread>

namespace xrpl::tests {

namespace {

// Each test uses its own PendingSaves, so one sequence serves them all.
constexpr LedgerIndex kSeq = 3;

}  // namespace

TEST(PendingSaves, claim_work_owns_a_new_sequence)
{
    PendingSaves saves;
    saves.claimWork(kSeq);
    EXPECT_TRUE(saves.pending(kSeq));

    // The claim is in progress, so a job cannot start it and an asynchronous
    // request is already covered.
    EXPECT_FALSE(saves.startWork(kSeq));
    EXPECT_FALSE(saves.shouldWork(kSeq));

    saves.finishWork(kSeq);
    EXPECT_FALSE(saves.pending(kSeq));
}

TEST(PendingSaves, claim_work_takes_a_scheduled_sequence)
{
    PendingSaves saves;

    // Scheduled by an asynchronous request, but not dispatched yet.
    EXPECT_TRUE(saves.shouldWork(kSeq));

    saves.claimWork(kSeq);

    // The job that was scheduled finds the work taken.
    EXPECT_FALSE(saves.startWork(kSeq));
    saves.finishWork(kSeq);
    EXPECT_FALSE(saves.pending(kSeq));
}

TEST(PendingSaves, claim_work_waits_for_a_save_in_progress)
{
    PendingSaves saves;
    EXPECT_TRUE(saves.shouldWork(kSeq));
    EXPECT_TRUE(saves.startWork(kSeq));

    // The claimer records whether the save had finished when its claim returned. A correct
    // claim returns only after finishWork(), so the record is always true, whatever the
    // scheduling. A claim that returned early reads it before this thread sets it, unless this
    // thread wins the race from the latch, so a regression is caught with high probability and
    // correct code never fails.
    std::latch started{1};
    std::atomic<bool> finished{false};
    std::atomic<bool> finishedWhenClaimed{false};
    std::thread claimer([&] {
        started.count_down();
        saves.claimWork(kSeq);
        finishedWhenClaimed = finished.load();
    });

    started.wait();
    finished = true;
    saves.finishWork(kSeq);
    claimer.join();
    EXPECT_TRUE(finishedWhenClaimed);

    // The claimer now owns the save.
    EXPECT_TRUE(saves.pending(kSeq));
    EXPECT_FALSE(saves.startWork(kSeq));
    saves.finishWork(kSeq);
    EXPECT_FALSE(saves.pending(kSeq));
}

}  // namespace xrpl::tests
