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
    if (jobCount > Tuning::kMaxPathfindJobCount || app.getFeeTrack().isLoadedLocal())
        return;

    while (true)
    {
        int prevVal = inProgress.load();
        if (prevVal >= Tuning::kMaxPathfindsInProgress)
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
