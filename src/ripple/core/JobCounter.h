//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2017 Ripple Labs Inc.

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

#ifndef RIPPLE_CORE_JOB_COUNTER_H_INCLUDED
#define RIPPLE_CORE_JOB_COUNTER_H_INCLUDED

#include <ripple/core/Job.h>
#include <boost/optional.hpp>
#include <atomic>
#include <condition_variable>
#include <mutex>
#include <type_traits>

namespace ripple {

// A class that does reference counting for Jobs.  The reference counting
// allows a Stoppable to assure that all child Jobs in the JobQueue are
// completed before the Stoppable declares itself stopped().
class JobCounter
{
private:
    std::mutex mutable mutex_ {};
    std::condition_variable allJobsDoneCond_ {};  // guard with mutex_
    bool waitForJobs_ {false};                    // guard with mutex_
    std::atomic<int> jobCount_ {0};

    // Increment the count.
    JobCounter& operator++()
    {
        ++jobCount_;
        return *this;
    }

    // Decrement the count.  If we're stopping and the count drops to zero
    // notify allJobsDoneCond_.
    JobCounter&  operator--()
    {
        // Even though jobCount_ is atomic, we decrement its value under a
        // lock.  This removes a small timing window that occurs if the
        // waiting thread is handling a spurious wakeup when jobCount_
        // drops to zero.
        std::lock_guard<std::mutex> lock {mutex_};

        // Update jobCount_.  Notify if we're stopping and all jobs are done.
        if ((--jobCount_ == 0) && waitForJobs_)
            allJobsDoneCond_.notify_all();
        return *this;
    }

    // A private template class that helps count the number of Jobs
    // in flight.  This allows Stoppables to hold off declaring stopped()
    // until all their JobQueue Jobs are dispatched.
    template <typename JobHandler>
    class CountedJob
    {
    private:
        JobCounter& counter_;
        JobHandler handler_;

        static_assert (
            std::is_same<decltype(
                handler_(std::declval<Job&>())), void>::value,
                "JobHandler must be callable with Job&");

    public:
        CountedJob() = delete;

        CountedJob (CountedJob const& rhs)
        : counter_ (rhs.counter_)
        , handler_ (rhs.handler_)
        {
            ++counter_;
        }

        CountedJob (CountedJob&& rhs)
        : counter_ (rhs.counter_)
        , handler_ (std::move (rhs.handler_))
        {
            ++counter_;
        }

        CountedJob (JobCounter& counter, JobHandler&& handler)
        : counter_ (counter)
        , handler_ (std::move (handler))
        {
            ++counter_;
        }

        CountedJob& operator=(CountedJob const& rhs) = delete;
        CountedJob& operator=(CountedJob&& rhs) = delete;

        ~CountedJob()
        {
            --counter_;
        }

        void operator ()(Job& job)
        {
            return handler_ (job);
        }
    };

public:
    JobCounter() = default;
    // Not copyable or movable.  Outstanding counts would be hard to sort out.
    JobCounter (JobCounter const&) = delete;

    JobCounter& operator=(JobCounter const&) = delete;

    /** Destructor verifies all in-flight jobs are complete. */
    ~JobCounter()
    {
        join();
    }

    /** Returns once all counted in-flight Jobs are destroyed. */
    void join()
    {
        std::unique_lock<std::mutex> lock {mutex_};
        waitForJobs_ = true;
        if (jobCount_ > 0)
        {
            allJobsDoneCond_.wait (
                lock, [this] { return jobCount_ == 0; });
        }
    }

    /** Wrap the passed lambda with a reference counter.

        @param handler Lambda that accepts a Job& parameter and returns void.
        @return If join() has been called returns boost::none.  Otherwise
                returns a boost::optional that wraps handler with a
                reference counter.
    */
    template <class JobHandler>
    boost::optional<CountedJob<JobHandler>>
    wrap (JobHandler&& handler)
    {
        // The current intention is that wrap() may only be called with an
        // rvalue lambda.  That can be adjusted in the future if needed,
        // but the following static_assert covers current expectations.
        static_assert (std::is_rvalue_reference<decltype (handler)>::value,
            "JobCounter::wrap() only supports rvalue lambdas.");

        boost::optional<CountedJob<JobHandler>> ret;

        std::lock_guard<std::mutex> lock {mutex_};
        if (! waitForJobs_)
            ret.emplace (*this, std::move (handler));

        return ret;
    }

    /** Current number of Jobs outstanding.  Only useful for testing. */
    int count() const
    {
        return jobCount_;
    }

    /** Returns true if this has been joined.

        Even if true is returned, counted Jobs may still be in flight.
        However if (joined() && (count() == 0)) there should be no more
        counted Jobs in flight.
    */
    bool joined() const
    {
        std::lock_guard<std::mutex> lock {mutex_};
        return waitForJobs_;
    }
};

} // ripple

#endif // RIPPLE_CORE_JOB_COUNTER_H_INCLUDED
