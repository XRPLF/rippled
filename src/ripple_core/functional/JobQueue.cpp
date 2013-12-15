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

    // Statistics for each JobType
    //
    struct Count
    {
        Count () noexcept
            : type (jtINVALID)
            , waiting (0)
            , running (0)
            , deferred (0)
        {
        }

        Count (JobType type_) noexcept
            : type (type_)
            , waiting (0)
            , running (0)
            , deferred (0)
        {
        }

        JobType type;    // The type of Job these counts reflect
        int waiting;     // The number waiting
        int running;     // How many are running
        int deferred;    // Number of jobs we didn't signal due to limits
    };

    typedef std::set <Job> JobSet;
    typedef std::map <JobType, Count> MapType;
    typedef CriticalSection::ScopedLockType ScopedLock;

    Journal m_journal;
    Metrics m_metrics;
    CriticalSection m_mutex;
    uint64 m_lastJob;
    JobSet m_jobSet;
    MapType m_jobCounts;

    // The number of jobs running through processTask()
    int m_processCount;

    Workers m_workers;
    LoadMonitor m_loads [NUM_JOB_TYPES];
    CancelCallback m_cancelCallback;

    //--------------------------------------------------------------------------

    JobQueueImp (shared_ptr <insight::Collector> const& collector,
        Stoppable& parent, Journal journal)
        : JobQueue ("JobQueue", parent)
        , m_journal (journal)
        , m_lastJob (0)
        , m_processCount (0)
        , m_workers (*this, "JobQueue", 0)
        , m_cancelCallback (boost::bind (&Stoppable::isStopping, this))
    {
        m_metrics.hook = collector->make_hook (beast::bind (
            &JobQueueImp::collect, this));
        m_metrics.job_count = collector->make_gauge ("job_count");

        {
            ScopedLock lock (m_mutex);

            // Initialize the job counts.
            // The 'limit' field in particular will be set based on the limit
            for (int i = 0; i < NUM_JOB_TYPES; ++i)
            {
                JobType const type (static_cast <JobType> (i));
                m_jobCounts [type] = Count (type);
            }
        }

        m_loads [ jtPUBOLDLEDGER  ].setTargetLatency (10000, 15000);
        m_loads [ jtVALIDATION_ut ].setTargetLatency (2000, 5000);
        m_loads [ jtPROOFWORK     ].setTargetLatency (2000, 5000);
        m_loads [ jtTRANSACTION   ].setTargetLatency (250, 1000);
        m_loads [ jtPROPOSAL_ut   ].setTargetLatency (500, 1250);
        m_loads [ jtPUBLEDGER     ].setTargetLatency (3000, 4500);
        m_loads [ jtWAL           ].setTargetLatency (1000, 2500);
        m_loads [ jtVALIDATION_t  ].setTargetLatency (500, 1500);
        m_loads [ jtWRITE         ].setTargetLatency (1750, 2500);
        m_loads [ jtTRANSACTION_l ].setTargetLatency (100, 500);
        m_loads [ jtPROPOSAL_t    ].setTargetLatency (100, 500);

        m_loads [ jtCLIENT        ].setTargetLatency (2000, 5000);
        m_loads [ jtPEER          ].setTargetLatency (200, 2500);
        m_loads [ jtDISK          ].setTargetLatency (500, 1000);
        m_loads [ jtACCEPTLEDGER  ].setTargetLatency (1000, 2500);

        m_loads [ jtNETOP_CLUSTER ].setTargetLatency (9999, 9999);    // once per 10 seconds
        m_loads [ jtNETOP_TIMER   ].setTargetLatency (999, 999);      // once per second
    }

    ~JobQueueImp ()
    {
    }

    void collect ()
    {
        ScopedLock lock (m_mutex);
        m_metrics.job_count = m_jobSet.size ();
    }

    void addJob (JobType type, const std::string& name, const FUNCTION_TYPE<void (Job&)>& jobFunc)
    {
        bassert (type != jtINVALID);

        // FIXME: Workaround incorrect client shutdown ordering
        // do not add jobs to a queue with no threads
        bassert (type == jtCLIENT || m_workers.getNumberOfThreads () > 0);

        // If this goes off it means that a child didn't follow the Stoppable API rules.
        bassert (! isStopped() && ! areChildrenStopped());

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
            ScopedLock lock (m_mutex);

            std::pair< std::set <Job>::iterator, bool > it =
                m_jobSet.insert (Job (
                    type, name, ++m_lastJob, m_loads[type], jobFunc, m_cancelCallback));

            queueJob (*it.first, lock);
        }
    }

    int getJobCount (JobType t)
    {
        ScopedLock lock (m_mutex);

        MapType::const_iterator c = m_jobCounts.find (t);

        return (c == m_jobCounts.end ()) ? 0 : c->second.waiting;
    }

    int getJobCountTotal (JobType t)
    {
        ScopedLock lock (m_mutex);

        MapType::const_iterator c = m_jobCounts.find (t);

        return (c == m_jobCounts.end ()) ? 0 : (c->second.waiting + c->second.running);
    }

    int getJobCountGE (JobType t)
    {
        // return the number of jobs at this priority level or greater
        int ret = 0;

        ScopedLock lock (m_mutex);

        typedef MapType::value_type jt_int_pair;

        BOOST_FOREACH (jt_int_pair const& it, m_jobCounts)
        {
            if (it.first >= t)
                ret += it.second.waiting;
        }

        return ret;
    }

    std::vector< std::pair<JobType, std::pair<int, int> > > getJobCounts ()
    {
        // return all jobs at all priority levels
        std::vector< std::pair<JobType, std::pair<int, int> > > ret;

        ScopedLock lock (m_mutex);

        ret.reserve (m_jobCounts.size ());

        typedef MapType::value_type jt_int_pair;

        BOOST_FOREACH (const jt_int_pair & it, m_jobCounts)
        {
            ret.push_back (std::make_pair (it.second.type,
                std::make_pair (it.second.waiting, it.second.running)));
        }

        return ret;
    }

    // shut down the job queue without completing pending jobs
    //
    void shutdown ()
    {
        m_journal.info <<  "Job queue shutting down";

        m_workers.pauseAllThreadsAndWait ();
    }

    // set the number of thread serving the job queue to precisely this number
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

            m_journal.info <<  "Auto-tuning to " << c << " validation/transaction/proposal threads";
        }

        m_workers.setNumberOfThreads (c);
    }


    LoadEvent::pointer getLoadEvent (JobType t, const std::string& name)
    {
        return boost::make_shared<LoadEvent> (boost::ref (m_loads[t]), name, true);
    }

    LoadEvent::autoptr getLoadEventAP (JobType t, const std::string& name)
    {
        return LoadEvent::autoptr (new LoadEvent (m_loads[t], name, true));
    }

    bool isOverloaded ()
    {
        int count = 0;

        for (int i = 0; i < NUM_JOB_TYPES; ++i)
            if (m_loads[i].isOver ())
                ++count;

        return count > 0;
    }

    Json::Value getJson (int)
    {
        Json::Value ret (Json::objectValue);

        ret["threads"] = m_workers.getNumberOfThreads ();
        ret["cpu"] = String::fromNumber <int> (m_workers.getUtilization() * 100) + "%";

        Json::Value priorities = Json::arrayValue;

        ScopedLock lock (m_mutex);

        for (int i = 0; i < NUM_JOB_TYPES; ++i)
        {
            JobType const type (static_cast <JobType> (i));

            if (type == jtGENERIC)
                continue;

            LoadMonitor::Stats stats = m_loads [i].getStats ();
            int jobCount;
            int threadCount;

            MapType::const_iterator it = m_jobCounts.find (type);

            if (it == m_jobCounts.end ())
            {
                jobCount = 0;
                threadCount = 0;
            }
            else
            {
                jobCount = it->second.waiting;
                threadCount = it->second.running;
            }

            if ((stats.count != 0) || (jobCount != 0) ||
                (stats.latencyPeak != 0) || (threadCount != 0))
            {
                Json::Value& pri = priorities.append (Json::objectValue);

                pri["job_type"] = Job::toString (type);

                if (stats.isOverloaded)
                    pri["over_target"] = true;

                if (jobCount != 0)
                    pri["waiting"] = jobCount;

                if (stats.count != 0)
                    pri["per_second"] = static_cast<int> (stats.count);

                if (stats.latencyPeak != 0)
                    pri["peak_time"] = static_cast<int> (stats.latencyPeak);

                if (stats.latencyAvg != 0)
                    pri["avg_time"] = static_cast<int> (stats.latencyAvg);

                if (threadCount != 0)
                    pri["in_progress"] = threadCount;
            }
        }

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
        JobType const type (job.getType ());

        bassert (type != jtINVALID);
        bassert (m_jobSet.find (job) != m_jobSet.end ());

        Count& count (m_jobCounts [type]);

        if (count.waiting + count.running < getJobLimit (type))
        {
            m_workers.addTask ();
        }
        else
        {
            // defer the task until we go below the limit
            //
            ++count.deferred;
        }
        ++count.waiting;
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
    void getNextJob (Job& job, ScopedLock const& lock)
    {
        bassert (! m_jobSet.empty ());

        JobSet::const_iterator iter;
        for (iter = m_jobSet.begin (); iter != m_jobSet.end (); ++iter)
        {
            Count& count (m_jobCounts [iter->getType ()]);

            bassert (count.running <= getJobLimit (count.type));

            // Run this job if we're running below the limit.
            if (count.running < getJobLimit (count.type))
            {
                bassert (count.waiting > 0);
                break;
            }
        }

        bassert (iter != m_jobSet.end ());

        JobType const type = iter->getType ();
        Count& count (m_jobCounts [type]);

        bassert (type != jtINVALID);

        job = *iter;
        m_jobSet.erase (iter);

        --count.waiting;
        ++count.running;
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
    void finishJob (Job const& job, ScopedLock const& lock)
    {
        JobType const type = job.getType ();

        bassert (m_jobSet.find (job) == m_jobSet.end ());
        bassert (type != jtINVALID);

        Count& count (m_jobCounts [type]);

        // Queue a deferred task if possible
        if (count.deferred > 0)
        {
            bassert (count.running + count.waiting >= getJobLimit (type));

            --count.deferred;
            m_workers.addTask ();
        }

        --count.running;
    }

    //------------------------------------------------------------------------------
    //
    // Runs the next appropriate waiting Job.
    //
    // Pre-conditions:
    //  A RunnableJob must exist in the JobSet
    //
    // Post-conditions:
    //  The chosen RunnableJob will have Job::doJob() called.
    //
    // Invariants:
    //  <none>
    //
    void processTask ()
    {
        Job job;

        {
            ScopedLock lock (m_mutex);
            getNextJob (job, lock);
            ++m_processCount;
        }

        JobType const type (job.getType ());
        String const name (Job::toString (type));

        // Skip the job if we are stopping and the
        // skipOnStop flag is set for the job type
        //
        if (!isStopping() || !skipOnStop (type))
        {
            Thread::setCurrentThreadName (name);
            m_journal.trace << "Doing " << name << " job";
            job.doJob ();
        }
        else
        {
            m_journal.trace << "Skipping processTask ('" << name << "')";
        }

        {
            ScopedLock lock (m_mutex);
            finishJob (job, lock);
            --m_processCount;
            checkStopped (lock);
        }

        // Note that when Job::~Job is called, the last reference
        // to the associated LoadEvent object (in the Job) may be destroyed.
    }

    //------------------------------------------------------------------------------

    // Returns `true` if all jobs of this type should be skipped when
    // the JobQueue receives a stop notification. If the job type isn't
    // skipped, the Job will be called and the job must call Job::shouldCancel
    // to determine if a long running or non-mandatory operation should be canceled.
    static bool skipOnStop (JobType type)
    {
        switch (type)
        {
        // These are skipped when a stop notification is received
        case jtPACK:
        case jtPUBOLDLEDGER:
        case jtVALIDATION_ut:
        case jtPROOFWORK:
        case jtTRANSACTION_l:
        case jtPROPOSAL_ut:
        case jtLEDGER_DATA:
        case jtUPDATE_PF:
        case jtCLIENT:
        case jtTRANSACTION:
        case jtUNL:
        case jtADVANCE:
        case jtPUBLEDGER:
        case jtTXN_DATA:
        case jtVALIDATION_t:
        case jtPROPOSAL_t:
        case jtSWEEP:
        case jtNETOP_CLUSTER:
        case jtNETOP_TIMER:
        case jtADMIN:
            return true;

        default:
            bassertfalse;
        case jtWAL:
        case jtWRITE:
            break;
        }

        return false;
    }

    // Returns the limit of running jobs for the given job type.
    // For jobs with no limit, we return the largest int. Hopefully that
    // will be enough.
    //
    static int getJobLimit (JobType type)
    {
        int limit = std::numeric_limits <int>::max ();

        switch (type)
        {
        // These are not dispatched by JobQueue
        case jtPEER:
        case jtDISK:
        case jtACCEPTLEDGER:
        case jtTXN_PROC:
        case jtOB_SETUP:
        case jtPATH_FIND:
        case jtHO_READ:
        case jtHO_WRITE:
        case jtGENERIC:
            limit = 0;
            break;

        default:
            // Someone added a JobType but forgot to set a limit.
            // Did they also forget to add it to Job.cpp?
            bassertfalse;
            break;

        case jtVALIDATION_ut:
        case jtPROOFWORK:
        case jtTRANSACTION_l:
        case jtPROPOSAL_ut:
        case jtUPDATE_PF:
        case jtCLIENT:
        case jtRPC:
        case jtTRANSACTION:
        case jtPUBLEDGER:
        case jtADVANCE:
        case jtWAL:
        case jtVALIDATION_t:
        case jtWRITE:
        case jtPROPOSAL_t:
        case jtSWEEP:
        case jtADMIN:
            limit = std::numeric_limits <int>::max ();
            break;

        case jtLEDGER_DATA:         limit = 2; break;
        case jtPACK:                limit = 1; break;
        case jtPUBOLDLEDGER:        limit = 2; break;
        case jtTXN_DATA:            limit = 1; break;
        case jtUNL:                 limit = 1; break;

        // If either of the next two are processing so slowly
        // or we are so busy we have two of them at once, it
        // indicates a serious problem!
        //
        case jtNETOP_TIMER:
        case jtNETOP_CLUSTER:
            limit = 1;
            break;
        };

        return limit;
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

JobQueue* JobQueue::New (shared_ptr <insight::Collector> const& collector,
                         Stoppable& parent, Journal journal)
{
    return new JobQueueImp (collector, parent, journal);
}
