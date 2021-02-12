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
#include <ripple/core/JobQueue.h>

namespace ripple {

JobQueue::JobQueue(
    beast::insight::Collector::ptr const& collector,
    Stoppable& parent,
    beast::Journal journal,
    Logs& logs,
    perf::PerfLog& perfLog)
    : Stoppable("JobQueue", parent)
    , m_journal(journal)
    , m_lastJob(0)
    , m_invalidJobData(JobTypes::instance().getInvalid(), collector, logs)
    , m_processCount(0)
    , m_workers(*this, &perfLog, "JobQueue", 0)
    , m_cancelCallback(std::bind(&Stoppable::isStopping, this))
    , perfLog_(perfLog)
    , m_collector(collector)
{
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
            assert(result.second == true);
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
    assert(type != jtINVALID);

    auto iter(m_jobData.find(type));
    assert(iter != m_jobData.end());
    if (iter == m_jobData.end())
        return false;

    JLOGV(
        m_journal.debug(),
        "addRefCountedJob Adding job",
        jv("name", name),
        jv("type", type));
    JobTypeData& data(iter->second);

    // FIXME: Workaround incorrect client shutdown ordering
    // do not add jobs to a queue with no threads
    assert(type == jtCLIENT || m_workers.getNumberOfThreads() > 0);

    {
        std::lock_guard lock(m_mutex);

        // If this goes off it means that a child didn't follow
        // the Stoppable API rules. A job may only be added if:
        //
        //  - The JobQueue has NOT stopped
        //          AND
        //      * We are currently processing jobs
        //          OR
        //      * We have have pending jobs
        //          OR
        //      * Not all children are stopped
        //
        assert(
            !isStopped() &&
            (m_processCount > 0 || !m_jobSet.empty() || !areChildrenStopped()));

        std::pair<std::set<Job>::iterator, bool> result(m_jobSet.insert(
            Job(type, name, ++m_lastJob, data.load(), func, m_cancelCallback)));
        queueJob(*result.first, lock);
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

void
JobQueue::setThreadCount(int c, bool const standaloneMode)
{
    if (standaloneMode)
    {
        c = 1;
    }
    else if (c == 0)
    {
        c = static_cast<int>(std::thread::hardware_concurrency());
        c = 2 + std::min(c, 4);  // I/O will bottleneck
        JLOGV(
            m_journal.info(),
            "Auto-tuning validation/transaction/proposal threads",
            jv("numThreads", c));
    }
    else
    {
        JLOGV(
            m_journal.info(),
            "Configured validation/transaction/proposal threads",
            jv("numThreads", c));
    }

    m_workers.setNumberOfThreads(c);
}

std::unique_ptr<LoadEvent>
JobQueue::makeLoadEvent(JobType t, std::string const& name)
{
    JobDataMap::iterator iter(m_jobData.find(t));
    assert(iter != m_jobData.end());

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
    assert(iter != m_jobData.end());
    iter->second.load().addSamples(count, elapsed);
}

bool
JobQueue::isOverloaded()
{
    int count = 0;

    for (auto& x : m_jobData)
    {
        if (x.second.load().isOver())
            ++count;
    }

    return count > 0;
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
        assert(x.first != jtINVALID);

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
    cv_.wait(lock, [&] { return m_processCount == 0 && m_jobSet.empty(); });
}

JobTypeData&
JobQueue::getJobTypeData(JobType type)
{
    JobDataMap::iterator c(m_jobData.find(type));
    assert(c != m_jobData.end());

    // NIKB: This is ugly and I hate it. We must remove jtINVALID completely
    //       and use something sane.
    if (c == m_jobData.end())
        return m_invalidJobData;

    return c->second;
}

void
JobQueue::onStop()
{
    // onStop must be defined and empty here,
    // otherwise the base class will do the wrong thing.
}

void
JobQueue::checkStopped(std::lock_guard<std::mutex> const& lock)
{
    // We are stopped when all of the following are true:
    //
    //  1. A stop notification was received
    //  2. All Stoppable children have stopped
    //  3. There are no executing calls to processTask
    //  4. There are no remaining Jobs in the job set
    //  5. There are no suspended coroutines
    //
    if (isStopping() && areChildrenStopped() && (m_processCount == 0) &&
        m_jobSet.empty() && nSuspend_ == 0)
    {
        stopped();
    }
}

void
JobQueue::queueJob(Job const& job, std::lock_guard<std::mutex> const& lock)
{
    JobType const type(job.getType());
    assert(type != jtINVALID);
    assert(m_jobSet.find(job) != m_jobSet.end());
    perfLog_.jobQueue(type);

    JobTypeData& data(getJobTypeData(type));

    if (data.waiting + data.running < getJobLimit(type))
    {
        m_workers.addTask();
    }
    else
    {
        // defer the task until we go below the limit
        //
        ++data.deferred;
    }
    ++data.waiting;
}

void
JobQueue::getNextJob(Job& job)
{
    assert(!m_jobSet.empty());

    std::set<Job>::const_iterator iter;
    for (iter = m_jobSet.begin(); iter != m_jobSet.end(); ++iter)
    {
        JobTypeData& data(getJobTypeData(iter->getType()));

        assert(data.running <= getJobLimit(data.type()));

        // Run this job if we're running below the limit.
        if (data.running < getJobLimit(data.type()))
        {
            assert(data.waiting > 0);
            break;
        }
    }

    assert(iter != m_jobSet.end());

    JobType const type = iter->getType();
    JobTypeData& data(getJobTypeData(type));

    assert(type != jtINVALID);

    job = *iter;
    m_jobSet.erase(iter);

    --data.waiting;
    ++data.running;
}

void
JobQueue::finishJob(JobType type)
{
    assert(type != jtINVALID);

    JobTypeData& data = getJobTypeData(type);

    // Queue a deferred task if possible
    if (data.deferred > 0)
    {
        assert(data.running + data.waiting >= getJobLimit(type));

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
            JLOGV(m_journal.trace(), "doing job", jv("name", data.name()));

            // The amount of time that the job was in the queue
            auto const q_time =
                date::ceil<microseconds>(start_time - job.queue_time());
            perfLog_.jobStart(type, q_time, start_time, instance);

            job.doJob();

            // The amount of time it took to execute the job
            auto const x_time =
                date::ceil<microseconds>(Job::clock_type::now() - start_time);

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
        // Job should be destroyed before calling checkStopped
        // otherwise destructors with side effects can access
        // parent objects that are already destroyed.
        finishJob(type);
        if (--m_processCount == 0 && m_jobSet.empty())
            cv_.notify_all();
        checkStopped(lock);
    }

    // Note that when Job::~Job is called, the last reference
    // to the associated LoadEvent object (in the Job) may be destroyed.
}

int
JobQueue::getJobLimit(JobType type)
{
    JobTypeInfo const& j(JobTypes::instance().get(type));
    assert(j.type() != jtINVALID);

    return j.limit();
}

void
JobQueue::onChildrenStopped()
{
    std::lock_guard lock(m_mutex);
    checkStopped(lock);
}

}  // namespace ripple
