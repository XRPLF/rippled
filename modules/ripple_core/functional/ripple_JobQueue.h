//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_JOBQUEUE_H
#define RIPPLE_JOBQUEUE_H

class JobQueue
{
public:
    explicit JobQueue (boost::asio::io_service&);

    // VFALCO TODO make convenience functions that allow the caller to not 
    //             have to call bind.
    //
    void addJob (JobType type, const std::string& name, const FUNCTION_TYPE<void (Job&)>& job);
    void addLimitJob (JobType type, const std::string& name, int limit, const FUNCTION_TYPE<void (Job&)>& job);

    int getJobCount (JobType t);        // Jobs waiting at this priority
    int getJobCountTotal (JobType t);   // Jobs waiting plus running at this priority
    int getJobCountGE (JobType t);      // All waiting jobs at or greater than this priority
    std::vector< std::pair<JobType, std::pair<int, int> > > getJobCounts (); // jobs waiting, threads doing

    void shutdown ();
    void setThreadCount (int c, bool const standaloneMode);

    // VFALCO TODO Rename these to newLoadEventMeasurement or something similar
    //             since they create the object.
    //
    LoadEvent::pointer getLoadEvent (JobType t, const std::string& name)
    {
        return boost::make_shared<LoadEvent> (boost::ref (mJobLoads[t]), name, true);
    }

    // VFALCO TODO Why do we need two versions, one which returns a shared
    //             pointer and the other which returns an autoptr?
    //          
    LoadEvent::autoptr getLoadEventAP (JobType t, const std::string& name)
    {
        return LoadEvent::autoptr (new LoadEvent (mJobLoads[t], name, true));
    }

    bool isOverloaded ();
    Json::Value getJson (int c = 0);

private:
    void threadEntry ();

    boost::mutex                    mJobLock;
    boost::condition_variable       mJobCond;

    uint64                          mLastJob;
    std::set <Job>                  mJobSet;
    LoadMonitor                     mJobLoads [NUM_JOB_TYPES];
    int                             mThreadCount;
    bool                            mShuttingDown;

    boost::asio::io_service&        mIOService;

    std::map<JobType, std::pair<int, int > >    mJobCounts;

    bool getJob (Job& job);
};

#endif
