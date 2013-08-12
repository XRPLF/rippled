//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (JobQueue)

JobQueue::JobQueue ()
    : m_workers (*this, "JobQueue", 0)
    , mLastJob (0)
{
    for (int i = 0; i < NUM_JOB_TYPES; ++i)
        mJobCounts[static_cast<JobType>(i)] = std::make_pair<int, int>(0, 0);

    mJobLoads [ jtPUBOLDLEDGER  ].setTargetLatency (10000, 15000);
    mJobLoads [ jtVALIDATION_ut ].setTargetLatency (2000, 5000);
    mJobLoads [ jtPROOFWORK     ].setTargetLatency (2000, 5000);
    mJobLoads [ jtTRANSACTION   ].setTargetLatency (250, 1000);
    mJobLoads [ jtPROPOSAL_ut   ].setTargetLatency (500, 1250);
    mJobLoads [ jtPUBLEDGER     ].setTargetLatency (3000, 4500);
    mJobLoads [ jtWAL           ].setTargetLatency (1000, 2500);
    mJobLoads [ jtVALIDATION_t  ].setTargetLatency (500, 1500);
    mJobLoads [ jtWRITE         ].setTargetLatency (750, 1500);
    mJobLoads [ jtTRANSACTION_l ].setTargetLatency (100, 500);
    mJobLoads [ jtPROPOSAL_t    ].setTargetLatency (100, 500);

    mJobLoads [ jtCLIENT        ].setTargetLatency (2000, 5000);
    mJobLoads [ jtPEER          ].setTargetLatency (200, 2500);
    mJobLoads [ jtDISK          ].setTargetLatency (500, 1000);
    mJobLoads [ jtACCEPTLEDGER  ].setTargetLatency (1000, 2500);
}

JobQueue::~JobQueue ()
{
}

void JobQueue::addJob (JobType type, const std::string& name, const FUNCTION_TYPE<void (Job&)>& jobFunc)
{
    assert (type != jtINVALID);

    ScopedLockType lock (mJobLock);

    // FIXME: Workaround incorrect client shutdown ordering
    // do not add jobs to a queue with no threads
    bassert (type == jtCLIENT || m_workers.getNumberOfThreads () > 0);

    std::pair< std::set <Job>::iterator, bool > it =
        mJobSet.insert (Job (type, name, ++mLastJob, mJobLoads[type], jobFunc));

    it.first->peekEvent().start(); // start timing how long it stays in the queue

    queueJobForRunning (*it.first, lock);
}

int JobQueue::getJobCount (JobType t)
{
    ScopedLockType lock (mJobLock);

    JobCounts::const_iterator c = mJobCounts.find (t);

    return (c == mJobCounts.end ()) ? 0 : c->second.first;
}

int JobQueue::getJobCountTotal (JobType t)
{
    ScopedLockType lock (mJobLock);

    JobCounts::const_iterator c = mJobCounts.find (t);

    return (c == mJobCounts.end ()) ? 0 : (c->second.first + c->second.second);
}

int JobQueue::getJobCountGE (JobType t)
{
    // return the number of jobs at this priority level or greater
    int ret = 0;

    ScopedLockType lock (mJobLock);

    typedef JobCounts::value_type jt_int_pair;

    BOOST_FOREACH (jt_int_pair const& it, mJobCounts)
    {
        if (it.first >= t)
            ret += it.second.first;
    }

    return ret;
}

std::vector< std::pair<JobType, std::pair<int, int> > > JobQueue::getJobCounts ()
{
    // return all jobs at all priority levels
    std::vector< std::pair<JobType, std::pair<int, int> > > ret;

    ScopedLockType lock (mJobLock);

    ret.reserve (mJobCounts.size ());

    typedef JobCounts::value_type jt_int_pair;

    BOOST_FOREACH (const jt_int_pair & it, mJobCounts)
    {
        ret.push_back (it);
    }

    return ret;
}

Json::Value JobQueue::getJson (int)
{
    Json::Value ret (Json::objectValue);

    ScopedLockType lock (mJobLock);

    ret["threads"] = m_workers.getNumberOfThreads ();

    Json::Value priorities = Json::arrayValue;

    for (int i = 0; i < NUM_JOB_TYPES; ++i)
    {
        if (static_cast<JobType>(i) == jtGENERIC)
            continue;

        uint64 count, latencyAvg, latencyPeak;
        int jobCount, threadCount;
        bool isOver;
        mJobLoads[i].getCountAndLatency (count, latencyAvg, latencyPeak, isOver);
        JobCounts::const_iterator it = mJobCounts.find (static_cast<JobType> (i));

        if (it == mJobCounts.end ())
        {
            jobCount = 0;
            threadCount = 0;
        }
        else
        {
            jobCount = it->second.first;
            threadCount = it->second.second;
        }

        if ((count != 0) || (jobCount != 0) || (latencyPeak != 0) || (threadCount != 0))
        {
            Json::Value pri (Json::objectValue);

            if (isOver)
                pri["over_target"] = true;

            pri["job_type"] = Job::toString (static_cast<JobType> (i));

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

    ScopedLockType lock (mJobLock);

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
// Determines the number of free task slots for the given JobType.
//
// This can return a negative number.
//
// Pre-conditions:
//  <none>
//
// Post-conditions:
//  <none>
//
// Invariants:
//  The calling thread owns the JobLock
//
int JobQueue::freeTaskSlots (JobType type, ScopedLockType const&)
{
    int const limit = getJobLimit (type);
  
    if (limit != 0)
    {
        int waiting = mJobCounts [type].first;
        int running = mJobCounts [type].second;

        return (limit - running) - waiting;
    }

    // The actual number doesn't matter as long as its positive
    return 1;
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
void JobQueue::queueJobForRunning (Job const& job, ScopedLockType const& lock)
{
    JobType const type (job.getType ());

    check_precondition (type != jtINVALID);
    check_precondition (mJobSet.find (job) != mJobSet.end ());

    if (freeTaskSlots (type, lock) > 0)
    {
        m_workers.addTask ();
    }
    else
    {
        // Do nothing.
        // When the next job of this type finishes, it will
        // call addTask if there are more waiting than the limit
    }

    // This has to happen after the call to freeTaskSlots
    ++mJobCounts [type].first;
}

//------------------------------------------------------------------------------
//
// Returns the next Job we should run now.
//
// RunnableJob:
//  A Job with no limit for its JobType, or
//  The number of running Jobs for its JobType is below the limit.
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
void JobQueue::getNextJobToRun (Job& job, ScopedLockType const&)
{
    check_precondition (! mJobSet.empty ());

    JobSet::const_iterator iter;
    for (iter = mJobSet.begin (); iter != mJobSet.end (); ++iter)
    {
        // Check the requirements for RunnableJob
        if (getJobLimit (iter->getType ()) <= 0 ||
            (mJobCounts [iter->getType ()].second < getJobLimit (iter->getType ())))
        {
            break;
        }
    }

    check_precondition (iter != mJobSet.end ());

    JobType const type = iter->getType ();

    check_postcondition (type != JobType::jtINVALID);

    job = *iter;

    mJobSet.erase (iter);

    --(mJobCounts [type].first);
    ++(mJobCounts [type].second);
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
void JobQueue::setRunningJobFinished (Job const& job)
{
    JobType const type = job.getType ();

    ScopedLockType lock (mJobLock);

    check_precondition (mJobSet.find (job) == mJobSet.end ());
    check_precondition (type != JobType::jtINVALID);

    // If there were previously no free slots and we would
    // free one up, and there are any other jobs of this type
    // waiting, then go ahead and signal a task
    //
    if (freeTaskSlots (type, lock) == 0 && mJobCounts [type].first > 0)
    {
        m_workers.addTask ();
    }

    --(mJobCounts [type].second);
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

    getNextJobToRun (job, ScopedLockType (mJobLock));

    JobType const type (job.getType ());

    String const name (Job::toString (type));

    Thread::setCurrentThreadName (name);

    WriteLog (lsTRACE, JobQueue) << "Doing " << name << " job";

    job.doJob ();

    setRunningJobFinished (job);

    // Note that when Job::~Job is called, the last reference
    // to the associated LoadEvent object (in the Job) may be destroyed.
}

//------------------------------------------------------------------------------

// Returns the limit of running jobs for the given job type.
// A value of zero means no limit.
//
int JobQueue::getJobLimit (JobType type)
{
    int limit = 0;

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
    default:
        bassertfalse;
        limit = 0;
        break;

    case jtVALIDATION_ut:
    case jtPROOFWORK:
    case jtTRANSACTION_l:
    case jtPROPOSAL_ut:
    case jtUPDATE_PF:
    case jtCLIENT:
    case jtTRANSACTION:
    case jtPUBLEDGER:
    case jtWAL:
    case jtVALIDATION_t:
    case jtWRITE:
    case jtPROPOSAL_t:
    case jtSWEEP:
    case jtADMIN:
        limit = 0;
        break;

    case jtLEDGER_DATA:         limit = 2; break;
    case jtPACK:                limit = 1; break;
    case jtPUBOLDLEDGER:        limit = 2; break;
    case jtTXN_DATA:            limit = 1; break;
    };

    return limit;
}
