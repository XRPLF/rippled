//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

SETUP_LOG (JobQueue)

JobQueue::JobQueue (boost::asio::io_service& svc)
    : mLastJob (0)
    , mThreadCount (0)
    , mShuttingDown (false)
    , mIOService (svc)
{
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
    mJobLoads [ jtPEER          ].setTargetLatency (200, 1250);
    mJobLoads [ jtDISK          ].setTargetLatency (500, 1000);
    mJobLoads [ jtACCEPTLEDGER  ].setTargetLatency (1000, 2500);
}

void JobQueue::addJob (JobType type, const std::string& name, const FUNCTION_TYPE<void (Job&)>& jobFunc)
{
    addLimitJob(type, name, 0, jobFunc);
}

void JobQueue::addLimitJob (JobType type, const std::string& name, int limit, const FUNCTION_TYPE<void (Job&)>& jobFunc)
{
    assert (type != jtINVALID);

    boost::mutex::scoped_lock sl (mJobLock);

    if (type != jtCLIENT) // FIXME: Workaround incorrect client shutdown ordering
        assert (mThreadCount != 0); // do not add jobs to a queue with no threads

    mJobSet.insert (Job (type, name, limit, ++mLastJob, mJobLoads[type], jobFunc));
    ++mJobCounts[type].first;
    mJobCond.notify_one ();
}

int JobQueue::getJobCount (JobType t)
{
    boost::mutex::scoped_lock sl (mJobLock);

    std::map< JobType, std::pair<int, int> >::iterator c = mJobCounts.find (t);
    return (c == mJobCounts.end ()) ? 0 : c->second.first;
}

int JobQueue::getJobCountTotal (JobType t)
{
    boost::mutex::scoped_lock sl (mJobLock);

    std::map< JobType, std::pair<int, int> >::iterator c = mJobCounts.find (t);
    return (c == mJobCounts.end ()) ? 0 : (c->second.first + c->second.second);
}

int JobQueue::getJobCountGE (JobType t)
{
    // return the number of jobs at this priority level or greater
    int ret = 0;

    boost::mutex::scoped_lock sl (mJobLock);

    typedef std::map< JobType, std::pair<int, int> >::value_type jt_int_pair;
    BOOST_FOREACH (const jt_int_pair & it, mJobCounts)

    if (it.first >= t)
        ret += it.second.first;

    return ret;
}

std::vector< std::pair<JobType, std::pair<int, int> > > JobQueue::getJobCounts ()
{
    // return all jobs at all priority levels
    std::vector< std::pair<JobType, std::pair<int, int> > > ret;

    boost::mutex::scoped_lock sl (mJobLock);
    ret.reserve (mJobCounts.size ());

    typedef std::map< JobType, std::pair<int, int> >::value_type jt_int_pair;
    BOOST_FOREACH (const jt_int_pair & it, mJobCounts)
    ret.push_back (it);

    return ret;
}

Json::Value JobQueue::getJson (int)
{
    Json::Value ret (Json::objectValue);
    boost::mutex::scoped_lock sl (mJobLock);

    ret["threads"] = mThreadCount;

    Json::Value priorities = Json::arrayValue;

    for (int i = 0; i < NUM_JOB_TYPES; ++i)
    {
        uint64 count, latencyAvg, latencyPeak;
        int jobCount, threadCount;
        bool isOver;
        mJobLoads[i].getCountAndLatency (count, latencyAvg, latencyPeak, isOver);
        std::map< JobType, std::pair<int, int> >::iterator it = mJobCounts.find (static_cast<JobType> (i));

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
    boost::mutex::scoped_lock sl (mJobLock);

    for (int i = 0; i < NUM_JOB_TYPES; ++i)
        if (mJobLoads[i].isOver ())
            ++count;

    return count > 0;
}

void JobQueue::shutdown ()
{
    // shut down the job queue without completing pending jobs
    WriteLog (lsINFO, JobQueue) << "Job queue shutting down";
    boost::mutex::scoped_lock sl (mJobLock);
    mShuttingDown = true;
    mJobCond.notify_all ();

    while (mThreadCount != 0)
        mJobCond.wait (sl);
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
        c = boost::thread::hardware_concurrency ();

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

    // VFALCO TODO Split the function up. The lower part actually does the "do",
    //             The part above this comment figures out the value for numThreads
    //
    boost::mutex::scoped_lock sl (mJobLock);

    while (mJobCounts[jtDEATH].first != 0)
    {
        mJobCond.wait (sl);
    }

    while (mThreadCount < c)
    {
        ++mThreadCount;
        boost::thread (BIND_TYPE (&JobQueue::threadEntry, this)).detach ();
    }

    while (mThreadCount > c)
    {
        if (mJobCounts[jtDEATH].first != 0)
        {
            mJobCond.wait (sl);
        }
        else
        {
            mJobSet.insert (Job (jtDEATH, 0));
            ++ (mJobCounts[jtDEATH].first);
        }
    }

    mJobCond.notify_one (); // in case we sucked up someone else's signal
}

bool JobQueue::getJob(Job& job)
{
    if (mJobSet.empty() || mShuttingDown)
        return false;

    std::set<Job>::iterator it = mJobSet.begin ();

    while (1)
    {
        // Are we out of jobs?
        if (it == mJobSet.end())
            return false;

        // Does this job have no limit?
        if (it->getLimit() == 0)
            break;

        // Is this job category below the limit?
        if (mJobCounts[it->getType()].second < it->getLimit())
            break;

        // Try the next job, if any
        ++it;
    }

    job = *it;
    mJobSet.erase (it);

    return true;
}

// do jobs until asked to stop
void JobQueue::threadEntry ()
{
    boost::mutex::scoped_lock sl (mJobLock);

    while (1)
    {
        JobType type;

        setCallingThreadName ("waiting");

        {
            Job job;
            while (!getJob(job))
            {
                if (mShuttingDown)
                {
                    --mThreadCount;
                    mJobCond.notify_all();
                    return;
                }
                mJobCond.wait (sl);
            }

            type = job.getType ();
            -- (mJobCounts[type].first);

            if (type == jtDEATH)
            {
                --mThreadCount;
                mJobCond.notify_all();
                return;
            }

            ++ (mJobCounts[type].second);
            sl.unlock ();
            setCallingThreadName (Job::toString (type));
            WriteLog (lsTRACE, JobQueue) << "Doing " << Job::toString (type) << " job";
            job.doJob ();
        } // must destroy job without holding lock

        sl.lock ();
        -- (mJobCounts[type].second);
    }
}

// vim:ts=4
