//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

NodeStoreScheduler::NodeStoreScheduler (Stoppable& parent, JobQueue& jobQueue)
    : Stoppable ("NodeStoreScheduler", parent)
    , m_jobQueue (jobQueue)
    , m_taskCount (1) // start it off at 1
{
}

void NodeStoreScheduler::onStop ()
{
    if (--m_taskCount == 0)
        stopped();
}

void NodeStoreScheduler::onChildrenStopped ()
{
}

void NodeStoreScheduler::scheduleTask (NodeStore::Task& task)
{
    ++m_taskCount;
    m_jobQueue.addJob (
        jtWRITE,
        "NodeObject::store",
        BIND_TYPE (&NodeStoreScheduler::doTask,
            this, boost::ref(task), P_1));
}

void NodeStoreScheduler::doTask (NodeStore::Task& task, Job&)
{
    task.performScheduledTask ();
    if ((--m_taskCount == 0) && isStopping())
        stopped();
}
