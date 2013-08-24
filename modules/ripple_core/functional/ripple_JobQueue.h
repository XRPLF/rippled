//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_JOBQUEUE_H_INCLUDED
#define RIPPLE_JOBQUEUE_H_INCLUDED

class JobQueue : private Workers::Callback
{
public:
    // Statistics on a particular JobType
    struct Count
    {
        Count () noexcept;
        explicit Count (JobType type) noexcept;

        JobType type;    // The type of Job these counts reflect
        int waiting;     // The number waiting
        int running;     // How many are running
        int deferred;    // Number of jobs we didn't signal due to limits
    };

    typedef std::map <JobType, Count> JobCounts;

    //--------------------------------------------------------------------------

    JobQueue ();

    ~JobQueue ();

    // VFALCO TODO make convenience functions that allow the caller to not 
    //             have to call bind.
    //
    void addJob (JobType type, const std::string& name, const FUNCTION_TYPE<void (Job&)>& job);

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
    typedef std::set <Job> JobSet;

    struct State
    {
        State ();

        uint64 lastJob;
        JobSet jobSet;
        JobCounts jobCounts;
    };

    typedef SharedData <State> SharedState;

    void queueJob (Job const& job, SharedState::WriteAccess& state);
    void getNextJob (Job& job, SharedState::WriteAccess& state);
    void finishJob (Job const& job);
    void processTask ();

    static int getJobLimit (JobType type);

private:
    SharedState m_state;
    Workers m_workers;
    LoadMonitor mJobLoads [NUM_JOB_TYPES];
};

#endif
