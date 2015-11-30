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

#include <BeastConfig.h>
#include <ripple/core/JobQueue.h>
#include <ripple/core/JobTypes.h>
#include <ripple/core/JobTypeInfo.h>
#include <ripple/core/JobTypeData.h>
#include <beast/chrono/chrono_util.h>
#include <beast/module/core/thread/Workers.h>
#include <chrono>
#include <memory>
#include <mutex>
#include <set>
#include <thread>

namespace ripple {

JobQueue::JobQueue (beast::insight::Collector::ptr const& collector,
    Stoppable& parent, beast::Journal journal, Logs& logs)
    : Stoppable ("JobQueue", parent)
    , m_journal (journal)
    , m_lastJob (0)
    , m_invalidJobData (getJobTypes ().getInvalid (), collector, logs)
    , m_processCount (0)
    , m_workers (*this, "JobQueue", 0)
    , m_cancelCallback (std::bind (&Stoppable::isStopping, this))
    , m_collector (collector)
{
    hook = m_collector->make_hook (std::bind (&JobQueue::collect, this));
    job_count = m_collector->make_gauge ("job_count");

    {
        std::lock_guard <std::mutex> lock (m_mutex);

        for (auto const& x : getJobTypes ())
        {
            JobTypeInfo const& jt = x.second;

            // And create dynamic information for all jobs
            auto const result (m_jobData.emplace (std::piecewise_construct,
                std::forward_as_tuple (jt.type ()),
                std::forward_as_tuple (jt, m_collector, logs)));
            assert (result.second == true);
            (void) result.second;
        }
    }
}

JobQueue::~JobQueue ()
{
    // Must unhook before destroying
    hook = beast::insight::Hook ();
}

void
JobQueue::collect ()
{
    std::lock_guard <std::mutex> lock (m_mutex);
    job_count = m_jobSet.size ();
}

void
JobQueue::addJob (JobType type, std::string const& name,
    JobFunction const& func)
{
    assert (type != jtINVALID);

    auto iter (m_jobData.find (type));
    assert (iter != m_jobData.end ());
    if (iter == m_jobData.end ())
        return;

    JobTypeData& data (iter->second);

    // FIXME: Workaround incorrect client shutdown ordering
    // do not add jobs to a queue with no threads
    assert (type == jtCLIENT || m_workers.getNumberOfThreads () > 0);

    {
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
        std::lock_guard <std::mutex> lock (m_mutex);
        assert (! isStopped() && (
            m_processCount>0 ||
            ! m_jobSet.empty () ||
            ! areChildrenStopped()));
    }

    // Don't even add it to the queue if we're stopping
    // and the job type is marked for skipOnStop.
    //
    if (isStopping() && skipOnStop (type))
    {
        m_journal.debug <<
            "Skipping addJob ('" << name << "')";
        return;
    }

    {
        std::lock_guard <std::mutex> lock (m_mutex);

        std::pair <std::set <Job>::iterator, bool> result (
            m_jobSet.insert (Job (type, name, ++m_lastJob,
                data.load (), func, m_cancelCallback)));
        queueJob (*result.first, lock);
    }
}

int
JobQueue::getJobCount (JobType t) const
{
    std::lock_guard <std::mutex> lock (m_mutex);

    JobDataMap::const_iterator c = m_jobData.find (t);

    return (c == m_jobData.end ())
        ? 0
        : c->second.waiting;
}

int
JobQueue::getJobCountTotal (JobType t) const
{
    std::lock_guard <std::mutex> lock (m_mutex);

    JobDataMap::const_iterator c = m_jobData.find (t);

    return (c == m_jobData.end ())
        ? 0
        : (c->second.waiting + c->second.running);
}

int
JobQueue::getJobCountGE (JobType t) const
{
    // return the number of jobs at this priority level or greater
    int ret = 0;

    std::lock_guard <std::mutex> lock (m_mutex);

    for (auto const& x : m_jobData)
    {
        if (x.first >= t)
            ret += x.second.waiting;
    }

    return ret;
}

void
JobQueue::shutdown ()
{
    m_journal.info <<  "Job queue shutting down";

    m_workers.pauseAllThreadsAndWait ();
}

void
JobQueue::setThreadCount (int c, bool const standaloneMode)
{
    if (standaloneMode)
    {
        c = 1;
    }
    else if (c == 0)
    {
        c = static_cast<int>(std::thread::hardware_concurrency());
        c = 2 + std::min (c, 4); // I/O will bottleneck

        m_journal.info << "Auto-tuning to " << c <<
                            " validation/transaction/proposal threads";
    }

    m_workers.setNumberOfThreads (c);
}

LoadEvent::pointer
JobQueue::getLoadEvent (JobType t, std::string const& name)
{
    JobDataMap::iterator iter (m_jobData.find (t));
    assert (iter != m_jobData.end ());

    if (iter == m_jobData.end ())
        return std::shared_ptr<LoadEvent> ();

    return std::make_shared<LoadEvent> (
        std::ref (iter-> second.load ()), name, true);
}

LoadEvent::autoptr
JobQueue::getLoadEventAP (JobType t, std::string const& name)
{
    JobDataMap::iterator iter (m_jobData.find (t));
    assert (iter != m_jobData.end ());

    if (iter == m_jobData.end ())
        return {};

    return std::make_unique<LoadEvent> (iter-> second.load (), name, true);
}

void
JobQueue::addLoadEvents (JobType t, int count,
    std::chrono::milliseconds elapsed)
{
    JobDataMap::iterator iter (m_jobData.find (t));
    assert (iter != m_jobData.end ());
    iter->second.load().addSamples (count, elapsed);
}

bool
JobQueue::isOverloaded ()
{
    int count = 0;

    for (auto& x : m_jobData)
    {
        if (x.second.load ().isOver ())
            ++count;
    }

    return count > 0;
}

Json::Value
JobQueue::getJson (int c)
{
    Json::Value ret (Json::objectValue);

    ret["threads"] = m_workers.getNumberOfThreads ();

    Json::Value priorities = Json::arrayValue;

    std::lock_guard <std::mutex> lock (m_mutex);

    for (auto& x : m_jobData)
    {
        assert (x.first != jtINVALID);

        if (x.first == jtGENERIC)
            continue;

        JobTypeData& data (x.second);

        LoadMonitor::Stats stats (data.stats ());

        int waiting (data.waiting);
        int running (data.running);

        if ((stats.count != 0) || (waiting != 0) ||
            (stats.latencyPeak != 0) || (running != 0))
        {
            Json::Value& pri = priorities.append (Json::objectValue);

            pri["job_type"] = data.name ();

            if (stats.isOverloaded)
                pri["over_target"] = true;

            if (waiting != 0)
                pri["waiting"] = waiting;

            if (stats.count != 0)
                pri["per_second"] = static_cast<int> (stats.count);

            if (stats.latencyPeak != 0)
                pri["peak_time"] = static_cast<int> (stats.latencyPeak);

            if (stats.latencyAvg != 0)
                pri["avg_time"] = static_cast<int> (stats.latencyAvg);

            if (running != 0)
                pri["in_progress"] = running;
        }
    }

    ret["job_types"] = priorities;

    return ret;
}

Job*
JobQueue::getJobForThread (std::thread::id const& id) const
{
    auto tid = (id == std::thread::id()) ? std::this_thread::get_id() : id;

    std::lock_guard <std::mutex> lock (m_mutex);
    auto i = m_threadIds.find (tid);
    return (i == m_threadIds.end()) ? nullptr : i->second;
}

JobTypeData&
JobQueue::getJobTypeData (JobType type)
{
    JobDataMap::iterator c (m_jobData.find (type));
    assert (c != m_jobData.end ());

    // NIKB: This is ugly and I hate it. We must remove jtINVALID completely
    //       and use something sane.
    if (c == m_jobData.end ())
        return m_invalidJobData;

    return c->second;
}

void
JobQueue::checkStopped (std::lock_guard <std::mutex> const& lock)
{
    // We are stopped when all of the following are true:
    //
    //  1. A stop notification was received
    //  2. All Stoppable children have stopped
    //  3. There are no executing calls to processTask
    //  4. There are no remaining Jobs in the job set
    //
    if (isStopping() &&
        areChildrenStopped() &&
        (m_processCount == 0) &&
        m_jobSet.empty())
    {
        stopped();
    }
}

void
JobQueue::queueJob (Job const& job, std::lock_guard <std::mutex> const& lock)
{
    JobType const type (job.getType ());
    assert (type != jtINVALID);
    assert (m_jobSet.find (job) != m_jobSet.end ());

    JobTypeData& data (getJobTypeData (type));

    if (data.waiting + data.running < getJobLimit (type))
    {
        m_workers.addTask ();
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
JobQueue::getNextJob (Job& job)
{
    assert (! m_jobSet.empty ());

    std::set <Job>::const_iterator iter;
    for (iter = m_jobSet.begin (); iter != m_jobSet.end (); ++iter)
    {
        JobTypeData& data (getJobTypeData (iter->getType ()));

        assert (data.running <= getJobLimit (data.type ()));

        // Run this job if we're running below the limit.
        if (data.running < getJobLimit (data.type ()))
        {
            assert (data.waiting > 0);
            break;
        }
    }

    assert (iter != m_jobSet.end ());

    JobType const type = iter->getType ();
    JobTypeData& data (getJobTypeData (type));

    assert (type != jtINVALID);

    job = *iter;
    m_jobSet.erase (iter);

    m_threadIds[std::this_thread::get_id()] = &job;

    --data.waiting;
    ++data.running;
}

void
JobQueue::finishJob (Job const& job)
{
    JobType const type = job.getType ();

    assert (m_jobSet.find (job) == m_jobSet.end ());
    assert (type != jtINVALID);

    JobTypeData& data (getJobTypeData (type));

    // Queue a deferred task if possible
    if (data.deferred > 0)
    {
        assert (data.running + data.waiting >= getJobLimit (type));

        --data.deferred;
        m_workers.addTask ();
    }

    if (! m_threadIds.erase (std::this_thread::get_id()))
    {
        assert (false);
    }
    --data.running;
}

template <class Rep, class Period>
void JobQueue::on_dequeue (JobType type,
    std::chrono::duration <Rep, Period> const& value)
{
    auto const ms (ceil <std::chrono::milliseconds> (value));

    if (ms.count() >= 10)
        getJobTypeData (type).dequeue.notify (ms);
}

template <class Rep, class Period>
void JobQueue::on_execute (JobType type,
    std::chrono::duration <Rep, Period> const& value)
{
    auto const ms (ceil <std::chrono::milliseconds> (value));

    if (ms.count() >= 10)
        getJobTypeData (type).execute.notify (ms);
}

void
JobQueue::processTask ()
{
    Job job;

    {
        std::lock_guard <std::mutex> lock (m_mutex);
        getNextJob (job);
        ++m_processCount;
    }

    JobTypeData& data (getJobTypeData (job.getType ()));

    // Skip the job if we are stopping and the
    // skipOnStop flag is set for the job type
    //
    if (!isStopping() || !data.info.skip ())
    {
        beast::Thread::setCurrentThreadName (data.name ());
        m_journal.trace << "Doing " << data.name () << " job";

        Job::clock_type::time_point const start_time (
            Job::clock_type::now());

        on_dequeue (job.getType (), start_time - job.queue_time ());
        job.doJob ();
        on_execute (job.getType (), Job::clock_type::now() - start_time);
    }
    else
    {
        m_journal.trace << "Skipping processTask ('" << data.name () << "')";
    }

    {
        std::lock_guard <std::mutex> lock (m_mutex);
        finishJob (job);
        --m_processCount;
        checkStopped (lock);
    }

    // Note that when Job::~Job is called, the last reference
    // to the associated LoadEvent object (in the Job) may be destroyed.
}

bool
JobQueue::skipOnStop (JobType type)
{
    JobTypeInfo const& j (getJobTypes ().get (type));
    assert (j.type () != jtINVALID);

    return j.skip ();
}

int
JobQueue::getJobLimit (JobType type)
{
    JobTypeInfo const& j (getJobTypes ().get (type));
    assert (j.type () != jtINVALID);

    return j.limit ();
}

void
JobQueue::onStop ()
{
    // VFALCO NOTE I wanted to remove all the jobs that are skippable
    //             but then the Workers count of tasks to process
    //             goes wrong.

    /*
    {
        std::lock_guard <std::mutex> lock (m_mutex);

        // Remove all jobs whose type is skipOnStop
        using JobDataMap = hash_map <JobType, std::size_t>;
        JobDataMap counts;
        bool const report (m_journal.debug.active());

        for (std::set <Job>::const_iterator iter (m_jobSet.begin());
            iter != m_jobSet.end();)
        {
            if (skipOnStop (iter->getType()))
            {
                if (report)
                {
                    std::pair <JobDataMap::iterator, bool> result (
                        counts.insert (std::make_pair (iter->getType(), 1)));
                    if (! result.second)
                        ++(result.first->second);
                }

                iter = m_jobSet.erase (iter);
            }
            else
            {
                ++iter;
            }
        }

        if (report)
        {
            beast::Journal::ScopedStream s (m_journal.debug);

            for (JobDataMap::const_iterator iter (counts.begin());
                iter != counts.end(); ++iter)
            {
                s << std::endl <<
                    "Removed " << iter->second <<
                    " skiponStop jobs of type " << Job::toString (iter->first);
            }
        }
    }
    */
}

void
JobQueue::onChildrenStopped ()
{
    std::lock_guard <std::mutex> lock (m_mutex);
    checkStopped (lock);
}

}
