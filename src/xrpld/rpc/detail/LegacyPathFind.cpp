/** @file
 *  Implements `LegacyPathFind`, the RAII admission-control guard for the
 *  synchronous `ripple_path_find` RPC operation.
 *
 *  The constructor applies three sequential checks for non-admin callers:
 *
 *  1. **Job-queue pressure** — rejected if `getJobCountGE(JtClient)` exceeds
 *     `Tuning::kMAX_PATHFIND_JOB_COUNT` (50) or local fee load is elevated.
 *     These two conditions are ORed so either is sufficient to refuse.
 *  2. **Concurrent-pathfind ceiling** — enforced via a lock-free CAS loop on
 *     the static `inProgress` counter. The loop retries on CAS failure
 *     (caused by a racing increment) rather than over-counting. On success,
 *     `std::memory_order_release` makes the increment visible to other threads
 *     that subsequently load `inProgress` with acquire semantics; relaxed
 *     ordering is used on failure because nothing was changed.
 *
 *  The ceiling (`kMAX_PATHFINDS_IN_PROGRESS = 2`) is deliberately low because
 *  a single path-find can be orders of magnitude more expensive than a typical
 *  RPC call.
 */
#include <xrpld/rpc/detail/LegacyPathFind.h>

#include <xrpld/app/main/Application.h>
#include <xrpld/rpc/detail/Tuning.h>

#include <xrpl/core/Job.h>
#include <xrpl/core/JobQueue.h>
#include <xrpl/server/LoadFeeTrack.h>

#include <atomic>

namespace xrpl::RPC {

LegacyPathFind::LegacyPathFind(bool isAdmin, Application& app)
{
    if (isAdmin)
    {
        ++inProgress;
        isOk_ = true;
        return;
    }

    auto const& jobCount = app.getJobQueue().getJobCountGE(JtClient);
    if (jobCount > Tuning::kMAX_PATHFIND_JOB_COUNT || app.getFeeTrack().isLoadedLocal())
        return;

    while (true)
    {
        int prevVal = inProgress.load();
        if (prevVal >= Tuning::kMAX_PATHFINDS_IN_PROGRESS)
            return;

        if (inProgress.compare_exchange_strong(
                prevVal, prevVal + 1, std::memory_order_release, std::memory_order_relaxed))
        {
            isOk_ = true;
            return;
        }
    }
}

LegacyPathFind::~LegacyPathFind()
{
    if (isOk_)
        --inProgress;
}

std::atomic<int> LegacyPathFind::inProgress(0);

}  // namespace xrpl::RPC
