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

#include <ripple/app/ledger/impl/TimeoutCounter.h>
#include <ripple/app/main/Application.h>
#include <ripple/core/JobQueue.h>
#include <ripple/overlay/Overlay.h>

namespace ripple {

using namespace std::chrono_literals;

TimeoutCounter::TimeoutCounter(
    Application& app,
    uint256 const& hash,
    std::chrono::milliseconds interval,
    QueueJobParameter&& jobParameter,
    std::uint8_t userdata,
    beast::Journal journal)
    : app_(app)
    , journal_(journal)
    , timerInterval_(interval)
    , queueJobParameter_(std::move(jobParameter))
    , hash_(hash)
    , userdata_(userdata)
    , timer_(app_.getIOService())
{
    assert((timerInterval_ > 10ms) && (timerInterval_ < 30s));
}

void
TimeoutCounter::setTimer(ScopedLockType& sl)
{
    if (isDone())
        return;
    timer_.expires_after(timerInterval_);
    timer_.async_wait(
        [wptr = pmDowncast()](boost::system::error_code const& ec) {
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
        JLOG(journal_.debug()) << "Deferring " << queueJobParameter_.jobName
                               << " timer due to load";
        setTimer(sl);
        return;
    }

    app_.getJobQueue().addJob(
        queueJobParameter_.jobType,
        queueJobParameter_.jobName,
        [wptr = pmDowncast()]() {
            if (auto sptr = wptr.lock(); sptr)
            {
                ScopedLockType sl(sptr->mtx_);

                if (sptr->isDone())
                    return;

                auto const progress = [&sptr]() {
                    if (sptr->state_.fetch_and(~PROGRESSING) & PROGRESSING)
                        return true;
                    return false;
                }();

                if (!progress)
                {
                    ++sptr->timeouts_;
                    JLOG(sptr->journal_.debug())
                        << "Acquiring " << sptr->hash_ << ": timeout ("
                        << sptr->timeouts_ << ")";
                }

                sptr->onTimer(progress, sl);

                if (!sptr->isDone())
                    sptr->setTimer(sl);
            }
        });
}

void
TimeoutCounter::cancel()
{
    ScopedLockType sl(mtx_);
    if (!isDone())
    {
        markFailed();
        JLOG(journal_.info()) << "Cancel " << hash_;
    }
}

}  // namespace ripple
