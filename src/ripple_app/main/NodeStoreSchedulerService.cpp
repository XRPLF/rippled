//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

NodeStoreSchedulerService::NodeStoreSchedulerService (Service& parent, JobQueue& jobQueue)
    : Service ("NodeStoreSchedulerService", parent)
    , m_jobQueue (jobQueue)
    , m_taskCount (1) // start it off at 1
{
}

void NodeStoreSchedulerService::onServiceStop ()
{
    if (--m_taskCount == 0)
        serviceStopped();
}

void NodeStoreSchedulerService::onServiceChildrenStopped ()
{
}

void NodeStoreSchedulerService::scheduleTask (NodeStore::Task& task)
{
    ++m_taskCount;
    m_jobQueue.addJob (
        jtWRITE,
        "NodeObject::store",
        BIND_TYPE (&NodeStoreSchedulerService::doTask,
            this, boost::ref(task), P_1));
}

void NodeStoreSchedulerService::doTask (NodeStore::Task& task, Job&)
{
    task.performScheduledTask ();
    if ((--m_taskCount == 0) && isServiceStopping())
        serviceStopped();
}
