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

#include <ripple/basics/PerfLog.h>
#include <ripple/basics/contract.h>
#include <ripple/beast/core/CurrentThreadName.h>
#include <ripple/core/JobQueue.h>
#include <mutex>

namespace ripple {

namespace detail {

template <size_t... I>
std::array<JobTypeData, sizeof...(I)>
build_jobdata(
    beast::insight::Collector::ptr const& collector,
    Logs& logs,
    std::index_sequence<I...>)
{
    return {JobTypeData{jobTypes[I], collector, logs}...};
}

}  // namespace detail

JobQueue::JobQueue(
    int threadCount,
    beast::insight::Collector::ptr const& collector,
    beast::Journal journal,
    Logs& logs,
    perf::PerfLog& perfLog)
    : journal_(journal)
    , jobData_(detail::build_jobdata(
          collector,
          logs,
          std::make_index_sequence<jobTypes.size()>()))
    , workers_(*this, "JobQueue", threadCount)
    , perfLog_(perfLog)
    , collector_(collector)
{
    JLOG(journal_.info()) << "Using " << threadCount << "  threads";

    // This is important to do before dispatching any jobs
    // so that the logger knows the number of threads.
    perfLog_.resizeJobs(threadCount);

    hook_ = collector_->make_hook(std::bind(&JobQueue::collect, this));
    jobCountGauge_ = collector_->make_gauge("job_count");
}

JobQueue::~JobQueue()
{
    // Must unhook before destroying
    hook_ = beast::insight::Hook();
}

void
JobQueue::collect()
{
    std::lock_guard lock(mutex_);
    jobCountGauge_ = jobSet_.size();
    activeThreadsGauge_ = activeThreads_;
}

bool
JobQueue::addRefCountedJob(
    JobType type,
    std::string const& name,
    JobFunction func)
{
    JLOG(journal_.debug()) << "Adding job '" << name << "' (" << type << ")";

    // FIXME: Workaround incorrect client shutdown ordering
    // do not add jobs to a queue with no threads
    assert(
        (type >= jtCLIENT && type <= jtCLIENT_WEBSOCKET) ||
        workers_.count() > 0);

    std::lock_guard lock(mutex_);

    auto& data = jobData_[type];

    auto r = jobSet_.emplace(
        type, name, ++nextId_, data.load().sample(), std::move(func));
    assert(r.second && jobSet_.find(*r.first) != jobSet_.end());

    perfLog_.jobQueue(type);

    // Queue a task now if we are below the limit, otherwise defer:
    if (data.waiting + data.running < data.info.limit)
        workers_.addTask();
    else
        ++data.deferred;

    ++data.waiting;

    return true;
}

int
JobQueue::getJobCount(JobType t) const
{
    std::lock_guard lock(mutex_);
    return jobData_[t].waiting;
}

int
JobQueue::getJobCountTotal(JobType t) const
{
    std::lock_guard lock(mutex_);
    auto const& jtd = jobData_[t];
    return jtd.waiting + jtd.running;
}

int
JobQueue::getJobCountGE(JobType t) const
{
    // return the number of jobs at this priority level or greater
    int ret = 0;

    std::lock_guard lock(mutex_);

    for (auto const& jtd : jobData_)
    {
        if (jtd.type() >= t)
            ret += jtd.waiting;
    }

    return ret;
}

LoadEvent
JobQueue::createLoadEvent(JobType t, std::string name)
{
    return {jobData_[t].load().sample(), std::move(name), true};
}

void
JobQueue::addLoadEvents(JobType t, int count, std::chrono::milliseconds elapsed)
{
    // clang-format off
    if (!isStopped()) [[likely]]
        jobData_[t].load().addSamples(count, elapsed);
    // clang-format on
}

bool
JobQueue::isOverloaded()
{
    return std::any_of(jobData_.begin(), jobData_.end(), [](auto& jtd) {
        return jtd.load().isOver();
    });
}

Json::Value
JobQueue::getJson(int)
{
    using namespace std::chrono_literals;
    Json::Value ret(Json::objectValue);

    ret["threads"] = workers_.count();
    ret["coro.suspended"] = suspendedCoroutines_.load();
    ret["coro.total"] = totalCoroutines_.load();

    Json::Value priorities = Json::arrayValue;

    std::lock_guard lock(mutex_);

    for (auto& data : jobData_)
    {
        if (data.type() == jtGENERIC)
            continue;

        if (auto const s = data.stats();
            s.count || data.waiting || data.running || (s.peakLatency != 0ms))
        {
            Json::Value& pri = priorities.append(Json::objectValue);

            pri["job_type"] = data.name();

            if (s.isOverloaded)
                pri["over_target"] = true;

            if (data.waiting != 0)
                pri["waiting"] = data.waiting;

            if (s.count != 0)
                pri["per_second"] = static_cast<int>(s.count);

            if (s.peakLatency != 0ms)
                pri["peak_time"] = static_cast<int>(s.peakLatency.count());

            if (s.averageLatency != 0ms)
                pri["avg_time"] = static_cast<int>(s.averageLatency.count());

            if (data.running != 0)
                pri["in_progress"] = data.running;
        }
    }

    ret["job_types"] = priorities;

    return ret;
}

void
JobQueue::rendezvous()
{
    std::unique_lock<std::mutex> lock(mutex_);
    cv_.wait(lock, [this] { return activeThreads_ == 0 && jobSet_.empty(); });
}

void
JobQueue::stop()
{
    stopping_ = true;
    using namespace std::chrono_literals;
    jobCounter_.join("JobQueue", 1s, journal_);
    {
        // After the JobCounter is joined, all jobs have finished executing
        // (i.e. returned from `Job::doJob`) and no more are being accepted,
        // but there may still be some threads between the return of
        // `Job::doJob` and the return of `JobQueue::processTask`. That is why
        // we must wait on the condition variable to make these assertions.
        std::unique_lock<std::mutex> lock(mutex_);
        cv_.wait(
            lock, [this] { return activeThreads_ == 0 && jobSet_.empty(); });
        assert(activeThreads_ == 0);
        assert(jobSet_.empty());
        assert(suspendedCoroutines_ == 0);
        stopped_ = true;
    }
}

bool
JobQueue::isStopped() const
{
    return stopped_;
}

void
JobQueue::uncaughtException(unsigned int instance, std::exception_ptr eptr)
{
    try
    {
        if (eptr)
            std::rethrow_exception(eptr);

        LogicError(
            beast::getCurrentThreadName() +
            ": Uncaught exception handler invoked with no exception_ptr");
    }
    catch (std::exception const& e)
    {
        LogicError(
            beast::getCurrentThreadName() +
            ": Exception caught during task processing: " + e.what());
    }
    catch (...)
    {
        LogicError(
            beast::getCurrentThreadName() +
            ": Unknown exception caught during task processing");
    }
}

void
JobQueue::processTask(unsigned int instance)
{
    JobType type;

    {
        // Determine the next job that should run. The fact that we are in
        // processTask implies that such a job exists.
        Job job = [this] {
            std::lock_guard lock(mutex_);

            assert(!jobSet_.empty());

            for (auto iter = jobSet_.begin(); iter != jobSet_.end(); ++iter)
            {
                JobType const type = iter->getType();

                auto& data = jobData_[type];
                assert(data.running <= data.info.limit);

                // Run this job if we're running below the limit.
                if (data.running < data.info.limit)
                {
                    assert(data.waiting > 0);
                    --data.waiting;
                    ++data.running;
                    ++activeThreads_;
                    return std::move(jobSet_.extract(*iter).value());
                }
            }

            LogicError("Attempt to get a job when none are available");
        }();

        type = job.getType();

        using namespace std::chrono;

        auto const start_time = Job::clock_type::now();

        // The amount of time that the job was in the queue
        auto const q_time = ceil<microseconds>(start_time - job.queue_time());
        perfLog_.jobStart(type, q_time, start_time, instance);

        JLOG(journal_.trace())
            << "Starting: " << jobTypes[type].name
            << " (q_time=" << q_time.count() << " microseconds)";

        job.execute();

        // The amount of time it took to execute the job
        auto const x_time =
            ceil<microseconds>(Job::clock_type::now() - start_time);

        JLOG(journal_.trace())
            << "Finished: " << jobTypes[type].name
            << " (x_time=" << x_time.count() << " microseconds)";

        if (x_time >= 10ms || q_time >= 10ms)
        {
            jobData_[type].dequeue.notify(q_time);
            jobData_[type].execute.notify(x_time);
        }

        perfLog_.jobFinish(type, x_time, instance);

        // Note that the Job object gets destroyed at this point.
    }

    std::lock_guard lock(mutex_);

    // Queue a deferred task if possible. This doesn't mean the task will
    // run next; only that one is now runnable.
    if (jobData_[type].deferred > 0)
    {
        assert(
            jobData_[type].running + jobData_[type].waiting >=
            jobData_[type].info.limit);

        --jobData_[type].deferred;
        workers_.addTask();
    }

    --jobData_[type].running;

    if (--activeThreads_ == 0 && jobSet_.empty())
        cv_.notify_all();
}

}  // namespace ripple
