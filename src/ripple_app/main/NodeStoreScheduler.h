//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_APP_NODESTORESCHEDULER_H_INCLUDED
#define RIPPLE_APP_NODESTORESCHEDULER_H_INCLUDED

/** A NodeStore::Scheduler which uses the JobQueue and implements the Stoppable API. */
class NodeStoreScheduler
    : public NodeStore::Scheduler
    , public Stoppable
{
public:
    NodeStoreScheduler (Stoppable& parent, JobQueue& jobQueue);

    void onStop ();
    void onChildrenStopped ();
    void scheduleTask (NodeStore::Task& task);

private:
    void doTask (NodeStore::Task& task, Job&);

    JobQueue& m_jobQueue;
    Atomic <int> m_taskCount;
};


#endif
