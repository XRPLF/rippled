#include <xrpld/app/main/Application.h>
#include <xrpld/app/misc/LoadFeeTrack.h>
#include <xrpld/core/Job.h>
#include <xrpld/core/JobQueue.h>
#include <xrpld/rpc/detail/LegacyPathFind.h>
#include <xrpld/rpc/detail/Tuning.h>

namespace ripple {
namespace RPC {

LegacyPathFind::LegacyPathFind(bool isAdmin, Application& app) : m_isOk(false)
{
    if (isAdmin)
    {
        ++inProgress;
        m_isOk = true;
        return;
    }

    auto const& jobCount = app.getJobQueue().getJobCountGE(jtCLIENT);
    if (jobCount > Tuning::maxPathfindJobCount ||
        app.getFeeTrack().isLoadedLocal())
        return;

    while (true)
    {
        int prevVal = inProgress.load();
        if (prevVal >= Tuning::maxPathfindsInProgress)
            return;

        if (inProgress.compare_exchange_strong(
                prevVal,
                prevVal + 1,
                std::memory_order_release,
                std::memory_order_relaxed))
        {
            m_isOk = true;
            return;
        }
    }
}

LegacyPathFind::~LegacyPathFind()
{
    if (m_isOk)
        --inProgress;
}

std::atomic<int> LegacyPathFind::inProgress(0);

}  // namespace RPC
}  // namespace ripple
