//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

#ifndef RIPPLE_APP_LEDGER_TIMEOUTCOUNTER_H_INCLUDED
#define RIPPLE_APP_LEDGER_TIMEOUTCOUNTER_H_INCLUDED

#include <ripple/app/main/Application.h>
#include <ripple/beast/clock/abstract_clock.h>
#include <ripple/beast/utility/Journal.h>
#include <ripple/core/Job.h>
#include <boost/asio/basic_waitable_timer.hpp>
#include <mutex>

namespace ripple {

/**
    This class is an "active" object. It maintains its own timer
    and dispatches work to a job queue. Implementations derive
    from this class and override the abstract hook functions in
    the base.

    This class implements an asynchronous loop:

    1. The entry point is `setTimer`.

    2. After `mTimerInterval`, `queueJob` is called, which schedules a job to
       call `invokeOnTimer` (or loops back to setTimer if there are too many
       concurrent jobs).

    3. The job queue calls `invokeOnTimer` which either breaks the loop if
       `isDone` or calls `onTimer`.

    4. `onTimer` is the only "real" virtual method in this class. It is the
       callback for when the timeout expires. Generally, its only responsibility
       is to set `mFailed = true`. However, if it wants to implement a policy of
       retries, then it has a chance to just increment a count of expired
       timeouts.

    5. Once `onTimer` returns, if the object is still not `isDone`, then
       `invokeOnTimer` sets another timeout by looping back to setTimer.

   This loop executes concurrently with another asynchronous sequence,
   implemented by the subtype, that is trying to make progress and eventually
   set `mComplete = true`. While it is making progress but not complete, it
   should set `mProgress = true`, which is passed to onTimer so it can decide
   whether to postpone failure and reset the timeout. However, if it can
   complete all its work in one synchronous step (while it holds the lock), then
   it can ignore `mProgress`.
*/
class TimeoutCounter
{
public:
    /**
     * Cancel the task by marking it as failed if the task is not done.
     * @note this function does not attempt to cancel the scheduled timer or
     *       to remove the queued job if any. When the timer expires or
     *       the queued job starts, however, the code will see that
     *       the task is done and returns immediately, if it can lock
     *       the weak pointer of the task.
     */
    virtual void
    cancel();

protected:
    using ScopedLockType = std::unique_lock<std::recursive_mutex>;

    struct QueueJobParameter
    {
        JobType jobType;
        std::string jobName;
        std::optional<std::uint32_t> jobLimit;
    };

    TimeoutCounter(
        Application& app,
        uint256 const& targetHash,
        std::chrono::milliseconds timeoutInterval,
        QueueJobParameter&& jobParameter,
        beast::Journal journal);

    virtual ~TimeoutCounter() = default;

    /** Schedule a call to queueJob() after mTimerInterval. */
    void
    setTimer(ScopedLockType&);

    /** Queue a job to call invokeOnTimer(). */
    void
    queueJob(ScopedLockType&);

    /** Hook called from invokeOnTimer(). */
    virtual void
    onTimer(bool progress, ScopedLockType&) = 0;

    /** Return a weak pointer to this. */
    virtual std::weak_ptr<TimeoutCounter>
    pmDowncast() = 0;

    bool
    isDone() const
    {
        return complete_ || failed_;
    }

    // Used in this class for access to boost::asio::io_service and
    // ripple::Overlay. Used in subtypes for the kitchen sink.
    Application& app_;
    beast::Journal journal_;
    mutable std::recursive_mutex mtx_;

    /** The hash of the object (in practice, always a ledger) we are trying to
     * fetch. */
    uint256 const hash_;
    int timeouts_;
    bool complete_;
    bool failed_;
    /** Whether forward progress has been made. */
    bool progress_;
    /** The minimum time to wait between calls to execute(). */
    std::chrono::milliseconds timerInterval_;

    QueueJobParameter queueJobParameter_;

private:
    /** Calls onTimer() if in the right state.
     *  Only called by queueJob().
     */
    void
    invokeOnTimer();

    boost::asio::basic_waitable_timer<std::chrono::steady_clock> timer_;
};

}  // namespace ripple

#endif
