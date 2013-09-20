//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_APP_NODESTORESCHEDULERSERVICE_H_INCLUDED
#define RIPPLE_APP_NODESTORESCHEDULERSERVICE_H_INCLUDED

/** A NodeStore::Scheduler which uses the JobQueue and implements the Service API. */
class NodeStoreSchedulerService
    : public NodeStore::Scheduler
    , public Service
{
public:
    NodeStoreSchedulerService (Service& parent, JobQueue& jobQueue);

    void onServiceStop ();
    void onServiceChildrenStopped ();
    void scheduleTask (NodeStore::Task& task);

private:
    void doTask (NodeStore::Task& task, Job&);

    JobQueue& m_jobQueue;
    Atomic <int> m_taskCount;
};


#endif
