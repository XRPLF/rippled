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

class JobQueueImp
    : public JobQueue
    , private Workers::Callback
{
public:
    struct Metrics
    {
        insight::Hook hook;
        insight::Gauge job_count;
    };

    typedef std::set <Job> JobSet;
    typedef CriticalSection::ScopedLockType ScopedLock;

    Journal m_journal;
    Metrics m_metrics;
    CriticalSection m_mutex;
    uint64 m_lastJob;
    JobSet m_jobSet;

    // The number of jobs running through processTask()
    int m_processCount;

    Workers m_workers;
    CancelCallback m_cancelCallback;

    //--------------------------------------------------------------------------

    JobQueueImp (insight::Collector::ptr const& collector,
        Stoppable& parent, Journal journal)
        : JobQueue ("JobQueue", parent)
        , m_journal (journal)
        , m_lastJob (0)
        , m_processCount (0)
        , m_workers (*this, "JobQueue", 0)
        , m_cancelCallback (boost::bind (&Stoppable::isStopping, this))
    {
        m_metrics.hook = collector->make_hook (std::bind (
            &JobQueueImp::collect, this));
        m_metrics.job_count = collector->make_gauge ("job_count");

        jtPUBOLDLEDGER.load.setTargetLatency (10000, 15000);
        jtVALIDATION_ut.load.setTargetLatency (2000, 5000);
        jtPROOFWORK.load.setTargetLatency (2000, 5000);
        jtTRANSACTION.load.setTargetLatency (250, 1000);
        jtPROPOSAL_ut.load.setTargetLatency (500, 1250);
        jtPUBLEDGER.load.setTargetLatency (3000, 4500);
        jtWAL.load.setTargetLatency (1000, 2500);
        jtVALIDATION_t.load.setTargetLatency (500, 1500);
        jtWRITE.load.setTargetLatency (1750, 2500);
        jtTRANSACTION_l.load.setTargetLatency (100, 500);
        jtPROPOSAL_t.load.setTargetLatency (100, 500);

        jtCLIENT.load.setTargetLatency (2000, 5000);
        jtPEER.load.setTargetLatency (200, 2500);
        jtDISK.load.setTargetLatency (500, 1000);

        jtNETOP_CLUSTER.load.setTargetLatency (9999, 9999);  // once per 10 seconds
        jtNETOP_TIMER.load.setTargetLatency (999, 999);      // once per second
    }

    ~JobQueueImp ()
    {
        // Must unhook before destroying
        m_metrics.hook = insight::Hook ();
    }

    void collect ()
    {
        ScopedLock lock (m_mutex);
        m_metrics.job_count = m_jobSet.size ();
    }

    void addJob (JobType& type, const std::string& name, 
        boost::function<void (Job&)> const& jobFunc)
    {
        // FIXME: Workaround incorrect client shutdown ordering
        // do not add jobs to a queue with no threads
        bassert (type == jtCLIENT || m_workers.getNumberOfThreads () > 0);

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
            ScopedLock lock (m_mutex);
            bassert (! isStopped() && (
                m_processCount>0 ||
                ! m_jobSet.empty () ||
                ! areChildrenStopped()));
        }

        // Don't even add it to the queue if we're stopping
        // and the job is of a type that will be skipped.
        //
        if (isStopping() && type.skip)
        {
            m_journal.debug <<
                "Skipping addJob ('" << name << "')";
            return;
        }

        {
            ScopedLock lock (m_mutex);

            std::pair <std::set <Job>::iterator, bool> it =
                m_jobSet.insert (Job (
                    type, name, ++m_lastJob, jobFunc, m_cancelCallback));

            queueJob (*it.first, lock);
        }
    }

    int getJobCount (JobType const& t)
    {
        ScopedLock lock (m_mutex);

        return t.waiting;
    }

    // NIKB TODO This function should take a predicate which can select
    //           jobs according to any criteria necessary. The current
    //           behavior is emulated using std::greater_equal. The predicate
    //           should be unary with the job type to compare against bound
    //           to it.
    int getJobCountGE (JobType const& t)
    {
        ScopedLock lock (m_mutex);

        int count = 0;

        std::for_each (JobType::Jobs.begin (), JobType::Jobs.end (),
            [&count, &t](std::pair <const int, std::reference_wrapper<ripple::JobType>>& entry)
            {
                JobType& type = entry.second;

                if (type >= t)
                    count += type.waiting;
            });

        return count;
    }

    int getJobCountTotal (JobType const& t)
    {
        ScopedLock lock (m_mutex);

        return t.waiting + t.running;
    }

    // shut down the job queue without completing pending jobs
    //
    void shutdown ()
    {
        m_journal.info <<  "Job queue shutting down";

        m_workers.pauseAllThreadsAndWait ();
    }

    // set the number of thread serving the job queue
    void setThreadCount (int c, bool const standaloneMode)
    {
        if (standaloneMode)
        {
            c = 1;
        }
        else if (c == 0)
        {
            c = SystemStats::getNumCpus ();

            // VFALCO NOTE According to boost, hardware_concurrency cannot return
            //             negative numbers/
            //
            if (c < 0)
                c = 2; // VFALCO NOTE Why 2?

            if (c > 4) // I/O will bottleneck
                c = 4;

            c += 2;

            m_journal.info <<  "Auto-tuning to " << c <<
                               " validation/transaction/proposal threads";
        }

        m_workers.setNumberOfThreads (c);
    }


    LoadEvent::pointer getLoadEvent (JobType& t, const std::string& name)
    {
        return boost::make_shared<LoadEvent> (boost::ref (t.load), name, true);
    }

    LoadEvent::autoptr getLoadEventAP (JobType& t, const std::string& name)
    {
        return LoadEvent::autoptr (new LoadEvent (t.load, name, true));
    }

    bool isOverloaded () const
    {
        auto count = std::count_if (JobType::Jobs.begin (), JobType::Jobs.end (),
            [](std::pair <const int, std::reference_wrapper<ripple::JobType>>& entry)
            {
                JobType& type = entry.second;
                return type.load.isOver ();
            });

        return count > 0;
    }

    Json::Value getJson (int)
    {
        Json::Value ret (Json::objectValue);

        ret["threads"] = m_workers.getNumberOfThreads ();
        ret["cpu"] = String::fromNumber <int> (m_workers.getUtilization() * 100) + "%";

        Json::Value priorities = Json::arrayValue;

        ScopedLock lock (m_mutex);

        std::for_each (JobType::Jobs.begin (), JobType::Jobs.end (),
            [&priorities](std::pair <const int, std::reference_wrapper<ripple::JobType>>& entry)
            {
                JobType& type = entry.second;

                LoadMonitor::Stats stats = type.load.getStats ();

                if ((stats.count != 0) || (type.waiting != 0) ||
                    (stats.latencyPeak != 0) || (type.running != 0))
                {
                    Json::Value& pri = priorities.append (Json::objectValue);

                    pri["job_type"] = type.name;

                    if (stats.isOverloaded)
                        pri["over_target"] = true;

                    if (type.waiting != 0)
                        pri["waiting"] = type.waiting;

                    if (stats.count != 0)
                        pri["per_second"] = static_cast<int> (stats.count);

                    if (stats.latencyPeak != 0)
                        pri["peak_time"] = static_cast<int> (stats.latencyPeak);

                    if (stats.latencyAvg != 0)
                        pri["avg_time"] = static_cast<int> (stats.latencyAvg);

                    if (type.running != 0)
                        pri["in_progress"] = type.running;
                }
            });

        ret["job_types"] = priorities;

        return ret;
    }

private:
    //------------------------------------------------------------------------------

    // Signals the service stopped if the stopped condition is met.
    //
    void checkStopped (ScopedLock const& lock)
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

    //------------------------------------------------------------------------------
    //
    // Signals an added Job for processing.
    //
    // Pre-conditions:
    //  The JobType must be valid.
    //  The Job must exist in mJobSet.
    //  The Job must not have previously been queued.
    //
    // Post-conditions:
    //  Count of waiting jobs of that type will be incremented.
    //  If JobQueue exists, and has at least one thread, Job will eventually run.
    //
    // Invariants:
    //  The calling thread owns the JobLock
    //  
    void queueJob (Job const& job, ScopedLock const& lock)
    {
        bassert (m_jobSet.find (job) != m_jobSet.end ());

        if (job.getType ().addTask ())
            m_workers.addTask ();
    }

    //------------------------------------------------------------------------------
    //
    // Returns the next Job we should run now.
    //
    // RunnableJob:
    //  A Job in the JobSet whose slots count for its type is greater than zero.
    //
    // Pre-conditions:
    //  mJobSet must not be empty.
    //  mJobSet holds at least one RunnableJob
    //
    // Post-conditions:
    //  job is a valid Job object.
    //  job is removed from mJobQueue.
    //  Waiting job count of it's type is decremented
    //  Running job count of it's type is incremented
    //
    // Invariants:
    //  The calling thread owns the JobLock
    //
    Job getNextJob (ScopedLock const&)
    {
        bassert (! m_jobSet.empty ());

        JobSet::const_iterator iter;

        for (iter = m_jobSet.begin (); iter != m_jobSet.end (); ++iter)
        {
            JobType& type = iter->getType ();

            bassert (type.running <= type.limit);

            // Run this job if we're running below the limit.
            if (type.running < type.limit)
            {
                bassert (type.waiting > 0);
                break;
            }
        }

        bassert (iter != m_jobSet.end ());

        Job job (*iter);

        m_jobSet.erase (iter);

        --job.getType ().waiting;
        ++job.getType ().running;

        ++m_processCount;

        return job;
    }

    //------------------------------------------------------------------------------
    //
    // Indicates that a running Job has completed its task.
    //
    // Pre-conditions:
    //  Job must not exist in mJobSet.
    //  The JobType must not be invalid.
    //
    // Post-conditions:
    //  The running count of that JobType is decremented
    //  A new task is signaled if there are more waiting Jobs than the limit, if any.
    //
    // Invariants:
    //  <none>
    //
    void finishJob (Job const& job, ScopedLock const&)
    {
        JobType& type = job.getType ();

        bassert (m_jobSet.find (job) == m_jobSet.end ());

        // Queue a deferred task if possible
        if (type.deferred > 0)
        {
            bassert (type.running + type.waiting >= type.limit);

            --type.deferred;
            m_workers.addTask ();
        }

        --type.running;

        --m_processCount;
    }

    //------------------------------------------------------------------------------
    //
    // Runs the next appropriate waiting Job.
    //
    // Pre-conditions:
    //  A RunnableJob must exist in the JobSet
    //
    // Post-conditions:
    //  The chosen RunnableJob will have Job::work() called.
    //
    // Invariants:
    //  <none>
    //
    void processTask ()
    {
        Job job (getNextJob (ScopedLock (m_mutex)));

        String const name (job.getType ().name);

        // Skip the job if we are stopping and the
        // job is of a type that will be skipped
        //
        if (!isStopping() || !job.getType ().skip)
        {
            Thread::setCurrentThreadName (name);
            m_journal.trace << "Doing " << name << " job";
            job.work ();
        }
        else
        {
            m_journal.trace << "Skipping processTask ('" << name << "')";
        }

        {
            ScopedLock lock (m_mutex);
            finishJob (job, lock);
            checkStopped (lock);
        }

        // Note that when Job::~Job is called, the last reference
        // to the associated LoadEvent object (in the Job) may be destroyed.
    }

    //--------------------------------------------------------------------------

    void onStop ()
    {
        // VFALCO NOTE I wanted to remove all the jobs that are skippable
        //             but then the Workers count of tasks to process
        //             goes wrong.

        /*
        {
            ScopedLock lock (m_mutex);

            // Remove all jobs whose type is skipOnStop
            typedef boost::unordered_map <JobType, std::size_t> MapType;
            MapType counts;
            bool const report (m_journal.debug.active());

            for (JobSet::const_iterator iter (m_jobSet.begin());
                iter != m_jobSet.end();)
            {
                if (skipOnStop (iter->getType()))
                {
                    if (report)
                    {
                        std::pair <MapType::iterator, bool> result (
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
                Journal::ScopedStream s (m_journal.debug);

                for (MapType::const_iterator iter (counts.begin());
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

    void onChildrenStopped ()
    {
        ScopedLock lock (m_mutex);

        checkStopped (lock);
    }
};

//------------------------------------------------------------------------------

JobQueue::JobQueue (char const* name, Stoppable& parent)
    : Stoppable (name, parent)
{
}

//------------------------------------------------------------------------------

JobQueue* JobQueue::New (insight::Collector::ptr const& collector,
                         Stoppable& parent, Journal journal)
{
    return new JobQueueImp (collector, parent, journal);
}
