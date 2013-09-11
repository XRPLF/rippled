//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (JobQueue)

//------------------------------------------------------------------------------

JobQueue::Count::Count () noexcept
    : type (jtINVALID)
    , waiting (0)
    , running (0)
    , deferred (0)
{
}

JobQueue::Count::Count (JobType type_) noexcept
    : type (type_)
    , waiting (0)
    , running (0)
    , deferred (0)
{
}

//------------------------------------------------------------------------------

JobQueue::State::State ()
    : lastJob (0)
{
}

//------------------------------------------------------------------------------

JobQueue::JobQueue ()
    : m_workers (*this, "JobQueue", 0)
{
    {
        ScopedLock lock (m_mutex);

        // Initialize the job counts.
        // The 'limit' field in particular will be set based on the limit
        for (int i = 0; i < NUM_JOB_TYPES; ++i)
        {
            JobType const type (static_cast <JobType> (i));
            m_state.jobCounts [type] = Count (type);
        }
    }

    mJobLoads [ jtPUBOLDLEDGER  ].setTargetLatency (10000, 15000);
    mJobLoads [ jtVALIDATION_ut ].setTargetLatency (2000, 5000);
    mJobLoads [ jtPROOFWORK     ].setTargetLatency (2000, 5000);
    mJobLoads [ jtTRANSACTION   ].setTargetLatency (250, 1000);
    mJobLoads [ jtPROPOSAL_ut   ].setTargetLatency (500, 1250);
    mJobLoads [ jtPUBLEDGER     ].setTargetLatency (3000, 4500);
    mJobLoads [ jtWAL           ].setTargetLatency (1000, 2500);
    mJobLoads [ jtVALIDATION_t  ].setTargetLatency (500, 1500);
    mJobLoads [ jtWRITE         ].setTargetLatency (1750, 2500);
    mJobLoads [ jtTRANSACTION_l ].setTargetLatency (100, 500);
    mJobLoads [ jtPROPOSAL_t    ].setTargetLatency (100, 500);

    mJobLoads [ jtCLIENT        ].setTargetLatency (2000, 5000);
    mJobLoads [ jtPEER          ].setTargetLatency (200, 2500);
    mJobLoads [ jtDISK          ].setTargetLatency (500, 1000);
    mJobLoads [ jtACCEPTLEDGER  ].setTargetLatency (1000, 2500);

    mJobLoads [ jtNETOP_CLUSTER ].setTargetLatency (9999, 9999);    // once per 10 seconds
    mJobLoads [ jtNETOP_TIMER   ].setTargetLatency (999, 999);      // once per second
}

JobQueue::~JobQueue ()
{
}

void JobQueue::addJob (JobType type, const std::string& name, const FUNCTION_TYPE<void (Job&)>& jobFunc)
{
    bassert (type != jtINVALID);

    // FIXME: Workaround incorrect client shutdown ordering
    // do not add jobs to a queue with no threads
    bassert (type == jtCLIENT || m_workers.getNumberOfThreads () > 0);

    ScopedLock lock (m_mutex);

    std::pair< std::set <Job>::iterator, bool > it =
        m_state.jobSet.insert (Job (
            type, name, ++m_state.lastJob, mJobLoads[type], jobFunc));

    // start timing how long it stays in the queue
    it.first->peekEvent().start();

    queueJob (*it.first, lock);
}

int JobQueue::getJobCount (JobType t)
{
    ScopedLock lock (m_mutex);

    JobCounts::const_iterator c = m_state.jobCounts.find (t);

    return (c == m_state.jobCounts.end ()) ? 0 : c->second.waiting;
}

int JobQueue::getJobCountTotal (JobType t)
{
    ScopedLock lock (m_mutex);

    JobCounts::const_iterator c = m_state.jobCounts.find (t);

    return (c == m_state.jobCounts.end ()) ? 0 : (c->second.waiting + c->second.running);
}

int JobQueue::getJobCountGE (JobType t)
{
    // return the number of jobs at this priority level or greater
    int ret = 0;

    ScopedLock lock (m_mutex);

    typedef JobCounts::value_type jt_int_pair;

    BOOST_FOREACH (jt_int_pair const& it, m_state.jobCounts)
    {
        if (it.first >= t)
            ret += it.second.waiting;
    }

    return ret;
}

std::vector< std::pair<JobType, std::pair<int, int> > > JobQueue::getJobCounts ()
{
    // return all jobs at all priority levels
    std::vector< std::pair<JobType, std::pair<int, int> > > ret;

    ScopedLock lock (m_mutex);

    ret.reserve (m_state.jobCounts.size ());

    typedef JobCounts::value_type jt_int_pair;

    BOOST_FOREACH (const jt_int_pair & it, m_state.jobCounts)
    {
        ret.push_back (std::make_pair (it.second.type,
            std::make_pair (it.second.waiting, it.second.running)));
    }

    return ret;
}

Json::Value JobQueue::getJson (int)
{
    Json::Value ret (Json::objectValue);

    ret["threads"] = m_workers.getNumberOfThreads ();

    Json::Value priorities = Json::arrayValue;

    ScopedLock lock (m_mutex);

    for (int i = 0; i < NUM_JOB_TYPES; ++i)
    {
        JobType const type (static_cast <JobType> (i));

        if (type == jtGENERIC)
            continue;

        uint64 count;
        uint64 latencyAvg;
        uint64 latencyPeak;
        int jobCount;
        int threadCount;
        bool isOver;

        mJobLoads [i].getCountAndLatency (count, latencyAvg, latencyPeak, isOver);

        JobCounts::const_iterator it = m_state.jobCounts.find (type);

        if (it == m_state.jobCounts.end ())
        {
            jobCount = 0;
            threadCount = 0;
        }
        else
        {
            jobCount = it->second.waiting;
            threadCount = it->second.running;
        }

        if ((count != 0) || (jobCount != 0) || (latencyPeak != 0) || (threadCount != 0))
        {
            Json::Value pri (Json::objectValue);

            if (isOver)
                pri["over_target"] = true;

            pri["job_type"] = Job::toString (type);

            if (jobCount != 0)
                pri["waiting"] = jobCount;

            if (count != 0)
                pri["per_second"] = static_cast<int> (count);

            if (latencyPeak != 0)
                pri["peak_time"] = static_cast<int> (latencyPeak);

            if (latencyAvg != 0)
                pri["avg_time"] = static_cast<int> (latencyAvg);

            if (threadCount != 0)
                pri["in_progress"] = threadCount;

            priorities.append (pri);
        }
    }

    ret["job_types"] = priorities;

    return ret;
}

bool JobQueue::isOverloaded ()
{
    int count = 0;

    for (int i = 0; i < NUM_JOB_TYPES; ++i)
        if (mJobLoads[i].isOver ())
            ++count;

    return count > 0;
}

// shut down the job queue without completing pending jobs
//
void JobQueue::shutdown ()
{
    WriteLog (lsINFO, JobQueue) << "Job queue shutting down";

    m_workers.pauseAllThreadsAndWait ();
}

// set the number of thread serving the job queue to precisely this number
void JobQueue::setThreadCount (int c, bool const standaloneMode)
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
        WriteLog (lsINFO, JobQueue) << "Auto-tuning to " << c << " validation/transaction/proposal threads";
    }

    m_workers.setNumberOfThreads (c);
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
void JobQueue::queueJob (Job const& job, ScopedLock const& lock)
{
    JobType const type (job.getType ());

    bassert (type != jtINVALID);
    bassert (m_state.jobSet.find (job) != m_state.jobSet.end ());

    Count& count (m_state.jobCounts [type]);

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
void JobQueue::getNextJob (Job& job, ScopedLock const& lock)
{
    bassert (! m_state.jobSet.empty ());

    JobSet::const_iterator iter;
    for (iter = m_state.jobSet.begin (); iter != m_state.jobSet.end (); ++iter)
    {
        Count& count (m_state.jobCounts [iter->getType ()]);

        bassert (count.running <= getJobLimit (count.type));

        // Run this job if we're running below the limit.
        if (count.running < getJobLimit (count.type))
        {
            bassert (count.waiting > 0);
            break;
        }
    }

    bassert (iter != m_state.jobSet.end ());

    JobType const type = iter->getType ();

    Count& count (m_state.jobCounts [type]);

    bassert (type != jtINVALID);

    job = *iter;

    m_state.jobSet.erase (iter);

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
void JobQueue::finishJob (Job const& job)
{
    JobType const type = job.getType ();

    {
        ScopedLock lock (m_mutex);

        bassert (m_state.jobSet.find (job) == m_state.jobSet.end ());
        bassert (type != jtINVALID);

        Count& count (m_state.jobCounts [type]);

        // Queue a deferred task if possible
        if (count.deferred > 0)
        {
            bassert (count.running + count.waiting >= getJobLimit (type));

            --count.deferred;
            m_workers.addTask ();
        }

        --count.running;
    }
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
void JobQueue::processTask ()
{
    Job job;

    {
        ScopedLock lock (m_mutex);

        getNextJob (job, lock);
    }

    JobType const type (job.getType ());

    String const name (Job::toString (type));

    Thread::setCurrentThreadName (name);

    WriteLog (lsTRACE, JobQueue) << "Doing " << name << " job";

    job.doJob ();

    finishJob (job);

    // Note that when Job::~Job is called, the last reference
    // to the associated LoadEvent object (in the Job) may be destroyed.
}

//------------------------------------------------------------------------------

// Returns the limit of running jobs for the given job type.
// For jobs with no limit, we return the largest int. Hopefully that
// will be enough.
//
int JobQueue::getJobLimit (JobType type)
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
        // Did they also forget to add it to ripple_Job.cpp?
        bassertfalse;
        break;

    case jtVALIDATION_ut:
    case jtPROOFWORK:
    case jtTRANSACTION_l:
    case jtPROPOSAL_ut:
    case jtUPDATE_PF:
    case jtCLIENT:
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
