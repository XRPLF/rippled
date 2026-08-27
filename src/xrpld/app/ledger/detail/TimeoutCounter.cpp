#include <xrpld/app/ledger/detail/TimeoutCounter.h>

#include <xrpld/app/main/Application.h>

#ifdef XRPL_ENABLE_TELEMETRY
// The two guarded recording calls below are the only things here that name
// AcquireStats, so without telemetry the include has no user.
#include <xrpld/app/ledger/AcquireStats.h>
#endif

#include <xrpl/basics/Log.h>
#include <xrpl/basics/base_uint.h>
#include <xrpl/beast/utility/Journal.h>
#include <xrpl/beast/utility/instrumentation.h>
#include <xrpl/core/JobQueue.h>

#include <boost/asio/error.hpp>
#include <boost/system/detail/error_code.hpp>

#include <chrono>
#include <utility>

namespace xrpl {

using namespace std::chrono_literals;

TimeoutCounter::TimeoutCounter(
    Application& app,
    uint256 const& hash,
    std::chrono::milliseconds interval,
    QueueJobParameter&& jobParameter,
    beast::Journal journal)
    : app_(app)
    , journal_(journal)
    , hash_(hash)
    , timerInterval_(interval)
    , queueJobParameter_(std::move(jobParameter))
    , timer_(app_.getIOContext())
{
    XRPL_ASSERT(
        (timerInterval_ > 10ms) && (timerInterval_ < 30s),
        "xrpl::TimeoutCounter::TimeoutCounter : interval input inside range");
}

void
TimeoutCounter::setTimer(ScopedLockType& sl)
{
    if (isDone())
        return;
    timer_.expires_after(timerInterval_);
    timer_.async_wait([wptr = pmDowncast()](boost::system::error_code const& ec) {
        if (ec == boost::asio::error::operation_aborted)
            return;

        if (auto ptr = wptr.lock())
        {
            ScopedLockType sl(ptr->mtx_);
            ptr->queueJob(sl);
        }
    });
}

void
TimeoutCounter::queueJob(ScopedLockType& sl)
{
    if (isDone())
        return;
    if (queueJobParameter_.jobLimit &&
        app_.getJobQueue().getJobCountTotal(queueJobParameter_.jobType) >=
            queueJobParameter_.jobLimit)
    {
#ifdef XRPL_ENABLE_TELEMETRY
        // Counted separately from timeouts: this path re-arms the timer
        // without running invokeOnTimer, so timeouts_ does not advance and the
        // give-up test that reads it cannot fire while the lane stays full.
        //
        // Guarded because it runs on every deferred tick of every in-flight
        // task, and the argument compares the job name against a string
        // literal each time. The acquire metrics are its only reader.
        app_.getAcquireStats().recordDeferral(isLedgerAcquisition());
#endif
        JLOG(journal_.debug()) << "Deferring " << queueJobParameter_.jobName
                               << " timer due to load";
        setTimer(sl);
        return;
    }

    app_.getJobQueue().addJob(
        queueJobParameter_.jobType, queueJobParameter_.jobName, [wptr = pmDowncast()]() {
            if (auto sptr = wptr.lock(); sptr)
                sptr->invokeOnTimer();
        });
}

void
TimeoutCounter::invokeOnTimer()
{
    ScopedLockType sl(mtx_);

    if (isDone())
        return;

    if (!progress_)
    {
        ++timeouts_;
#ifdef XRPL_ENABLE_TELEMETRY
        // Same cost as the deferral above: one call per no-progress tick, with
        // a job-name string comparison to build the argument. timeouts_ stays
        // outside the guard because the give-up test reads it.
        app_.getAcquireStats().recordTimeout(isLedgerAcquisition());
#endif
        JLOG(journal_.debug()) << "Timeout(" << timeouts_ << ") "
                               << " acquiring " << hash_;
        onTimer(false, sl);
    }
    else
    {
        progress_ = false;
        onTimer(true, sl);
    }

    if (!isDone())
        setTimer(sl);
}

void
TimeoutCounter::cancel()
{
    ScopedLockType const sl(mtx_);
    if (!isDone())
    {
        failed_ = true;
        JLOG(journal_.info()) << "Cancel " << hash_;
    }
}

}  // namespace xrpl
