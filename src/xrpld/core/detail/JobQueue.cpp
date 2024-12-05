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

#include <xrpld/core/JobQueue.h>
#include <xrpld/perflog/PerfLog.h>
#include <xrpl/basics/contract.h>
#include <mutex>

namespace ripple {

JobQueue::JobQueue(
    int threadCount,
    beast::insight::Collector::ptr const& collector,
    beast::Journal journal,
    Logs& logs,
    perf::PerfLog& perfLog)
    : m_journal(journal)
    , m_lastJob(0)
    , m_invalidJobData(JobTypes::instance().getInvalid(), collector, logs)
    , m_processCount(0)
    , m_workers(*this, &perfLog, "JobQueue", threadCount)
    , perfLog_(perfLog)
    , m_collector(collector)
{
    JLOG(m_journal.info()) << "Using " << threadCount << "  threads";

    hook = m_collector->make_hook(std::bind(&JobQueue::collect, this));
    job_count = m_collector->make_gauge("job_count");

    {
        std::lock_guard lock(m_mutex);

        for (auto const& x : JobTypes::instance())
        {
            JobTypeInfo const& jt = x.second;

            // And create dynamic information for all jobs
            auto const result(m_jobData.emplace(
                std::piecewise_construct,
                std::forward_as_tuple(jt.type()),
                std::forward_as_tuple(jt, m_collector, logs)));
            ASSERT(
                result.second == true,
                "ripple::JobQueue::JobQueue : jobs added");
            (void)result.second;
        }
    }
}

JobQueue::~JobQueue()
{
    // Must unhook before destroying
    hook = beast::insight::Hook();
}

void
JobQueue::collect()
{
    std::lock_guard lock(m_mutex);
    job_count = m_jobSet.size();
}

bool
JobQueue::addRefCountedJob(
    JobType type,
    std::string const& name,
    JobFunction const& func)
{
    ASSERT(
        type != jtINVALID,
        "ripple::JobQueue::addRefCountedJob : valid input job type");

    auto iter(m_jobData.find(type));
    ASSERT(
        iter != m_jobData.end(),
        "ripple::JobQueue::addRefCountedJob : job type found in jobs");
    if (iter == m_jobData.end())
        return false;

    JLOG(m_journal.debug())
        << __func__ << " : Adding job : " << name << " : " << type;
    JobTypeData& data(iter->second);

    // FIXME: Workaround incorrect client shutdown ordering
    // do not add jobs to a queue with no threads
    ASSERT(
        (type >= jtCLIENT && type <= jtCLIENT_WEBSOCKET) ||
            m_workers.getNumberOfThreads() > 0,
        "ripple::JobQueue::addRefCountedJob : threads available or job "
        "requires no threads");

    {
        std::lock_guard lock(m_mutex);
        auto result =
            m_jobSet.emplace(type, name, ++m_lastJob, data.load(), func);
        auto const& job = *result.first;

        JobType const type(job.getType());
        ASSERT(
            type != jtINVALID,
            "ripple::JobQueue::addRefCountedJob : has valid job type");
        ASSERT(
            m_jobSet.find(job) != m_jobSet.end(),
            "ripple::JobQueue::addRefCountedJob : job found");
        perfLog_.jobQueue(type);

        JobTypeData& data(getJobTypeData(type));

        if (data.waiting + data.running < getJobLimit(type))
        {
            m_workers.addTask();
        }
        else
        {
            // defer the task until we go below the limit
            ++data.deferred;
        }
        ++data.waiting;
    }
    return true;
}

int
JobQueue::getJobCount(JobType t) const
{
    std::lock_guard lock(m_mutex);

    JobDataMap::const_iterator c = m_jobData.find(t);

    return (c == m_jobData.end()) ? 0 : c->second.waiting;
}

int
JobQueue::getJobCountTotal(JobType t) const
{
    std::lock_guard lock(m_mutex);

    JobDataMap::const_iterator c = m_jobData.find(t);

    return (c == m_jobData.end()) ? 0 : (c->second.waiting + c->second.running);
}

int
JobQueue::getJobCountGE(JobType t) const
{
    // return the number of jobs at this priority level or greater
    int ret = 0;

    std::lock_guard lock(m_mutex);

    for (auto const& x : m_jobData)
    {
        if (x.first >= t)
            ret += x.second.waiting;
    }

    return ret;
}

std::unique_ptr<LoadEvent>
JobQueue::makeLoadEvent(JobType t, std::string const& name)
{
    JobDataMap::iterator iter(m_jobData.find(t));
    ASSERT(
        iter != m_jobData.end(),
        "ripple::JobQueue::makeLoadEvent : valid job type input");

    if (iter == m_jobData.end())
        return {};

    return std::make_unique<LoadEvent>(iter->second.load(), name, true);
}

void
JobQueue::addLoadEvents(JobType t, int count, std::chrono::milliseconds elapsed)
{
    if (isStopped())
        LogicError("JobQueue::addLoadEvents() called after JobQueue stopped");

    JobDataMap::iterator iter(m_jobData.find(t));
    ASSERT(
        iter != m_jobData.end(),
        "ripple::JobQueue::addLoadEvents : valid job type input");
    iter->second.load().addSamples(count, elapsed);
}

bool
JobQueue::isOverloaded()
{
    return std::any_of(m_jobData.begin(), m_jobData.end(), [](auto& entry) {
        return entry.second.load().isOver();
    });
}

Json::Value
JobQueue::getJson(int c)
{
    using namespace std::chrono_literals;
    Json::Value ret(Json::objectValue);

    ret["threads"] = m_workers.getNumberOfThreads();

    Json::Value priorities = Json::arrayValue;

    std::lock_guard lock(m_mutex);

    for (auto& x : m_jobData)
    {
        ASSERT(
            x.first != jtINVALID, "ripple::JobQueue::getJson : valid job type");

        if (x.first == jtGENERIC)
            continue;

        JobTypeData& data(x.second);

        LoadMonitor::Stats stats(data.stats());

        int waiting(data.waiting);
        int running(data.running);

        if ((stats.count != 0) || (waiting != 0) ||
            (stats.latencyPeak != 0ms) || (running != 0))
        {
            Json::Value& pri = priorities.append(Json::objectValue);

            pri["job_type"] = data.name();

            if (stats.isOverloaded)
                pri["over_target"] = true;

            if (waiting != 0)
                pri["waiting"] = waiting;

            if (stats.count != 0)
                pri["per_second"] = static_cast<int>(stats.count);

            if (stats.latencyPeak != 0ms)
                pri["peak_time"] = static_cast<int>(stats.latencyPeak.count());

            if (stats.latencyAvg != 0ms)
                pri["avg_time"] = static_cast<int>(stats.latencyAvg.count());

            if (running != 0)
                pri["in_progress"] = running;
        }
    }

    ret["job_types"] = priorities;

    return ret;
}

void
JobQueue::rendezvous()
{
    std::unique_lock<std::mutex> lock(m_mutex);
    cv_.wait(lock, [this] { return m_processCount == 0 && m_jobSet.empty(); });
}

JobTypeData&
JobQueue::getJobTypeData(JobType type)
{
    JobDataMap::iterator c(m_jobData.find(type));
    ASSERT(
        c != m_jobData.end(),
        "ripple::JobQueue::getJobTypeData : valid job type input");

    // NIKB: This is ugly and I hate it. We must remove jtINVALID completely
    //       and use something sane.
    if (c == m_jobData.end())
        return m_invalidJobData;

    return c->second;
}

void
JobQueue::stop()
{
    stopping_ = true;
    using namespace std::chrono_literals;
    jobCounter_.join("JobQueue", 1s, m_journal);
    {
        // After the JobCounter is joined, all jobs have finished executing
        // (i.e. returned from `Job::doJob`) and no more are being accepted,
        // but there may still be some threads between the return of
        // `Job::doJob` and the return of `JobQueue::processTask`. That is why
        // we must wait on the condition variable to make these assertions.
        std::unique_lock<std::mutex> lock(m_mutex);
        cv_.wait(
            lock, [this] { return m_processCount == 0 && m_jobSet.empty(); });
        ASSERT(
            m_processCount == 0,
            "ripple::JobQueue::stop : all processes completed");
        ASSERT(m_jobSet.empty(), "ripple::JobQueue::stop : all jobs completed");
        ASSERT(nSuspend_ == 0, "ripple::JobQueue::stop : no coros suspended");
        stopped_ = true;
    }
}

bool
JobQueue::isStopped() const
{
    return stopped_;
}

void
JobQueue::getNextJob(Job& job)
{
    ASSERT(!m_jobSet.empty(), "ripple::JobQueue::getNextJob : non-empty jobs");

    std::set<Job>::const_iterator iter;
    for (iter = m_jobSet.begin(); iter != m_jobSet.end(); ++iter)
    {
        JobType const type = iter->getType();
        ASSERT(
            type != jtINVALID, "ripple::JobQueue::getNextJob : valid job type");

        JobTypeData& data(getJobTypeData(type));
        ASSERT(
            data.running <= getJobLimit(type),
            "ripple::JobQueue::getNextJob : maximum jobs running");

        // Run this job if we're running below the limit.
        if (data.running < getJobLimit(data.type()))
        {
            ASSERT(
                data.waiting > 0,
                "ripple::JobQueue::getNextJob : positive data waiting");
            --data.waiting;
            ++data.running;
            break;
        }
    }

    ASSERT(
        iter != m_jobSet.end(),
        "ripple::JobQueue::getNextJob : found next job");
    job = *iter;
    m_jobSet.erase(iter);
}

void
JobQueue::finishJob(JobType type)
{
    ASSERT(
        type != jtINVALID,
        "ripple::JobQueue::finishJob : valid input job type");

    JobTypeData& data = getJobTypeData(type);

    // Queue a deferred task if possible
    if (data.deferred > 0)
    {
        ASSERT(
            data.running + data.waiting >= getJobLimit(type),
            "ripple::JobQueue::finishJob : job limit");

        --data.deferred;
        m_workers.addTask();
    }

    --data.running;
}

void
JobQueue::processTask(int instance)
{
    JobType type;

    {
        using namespace std::chrono;
        Job::clock_type::time_point const start_time(Job::clock_type::now());
        {
            Job job;
            {
                std::lock_guard lock(m_mutex);
                getNextJob(job);
                ++m_processCount;
            }
            type = job.getType();
            JobTypeData& data(getJobTypeData(type));
            JLOG(m_journal.trace()) << "Doing " << data.name() << "job";

            // The amount of time that the job was in the queue
            auto const q_time =
                ceil<microseconds>(start_time - job.queue_time());
            perfLog_.jobStart(type, q_time, start_time, instance);

            job.doJob();

            // The amount of time it took to execute the job
            auto const x_time =
                ceil<microseconds>(Job::clock_type::now() - start_time);

            if (x_time >= 10ms || q_time >= 10ms)
            {
                getJobTypeData(type).dequeue.notify(q_time);
                getJobTypeData(type).execute.notify(x_time);
            }
            perfLog_.jobFinish(type, x_time, instance);
        }
    }

    {
        std::lock_guard lock(m_mutex);
        // Job should be destroyed before stopping
        // otherwise destructors with side effects can access
        // parent objects that are already destroyed.
        finishJob(type);
        if (--m_processCount == 0 && m_jobSet.empty())
            cv_.notify_all();
    }

    // Note that when Job::~Job is called, the last reference
    // to the associated LoadEvent object (in the Job) may be destroyed.
}

int
JobQueue::getJobLimit(JobType type)
{
    JobTypeInfo const& j(JobTypes::instance().get(type));
    ASSERT(
        j.type() != jtINVALID,
        "ripple::JobQueue::getJobLimit : valid job type");

    return j.limit();
}

}  // namespace ripple
