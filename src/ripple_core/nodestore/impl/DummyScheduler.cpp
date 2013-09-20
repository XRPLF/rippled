//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace NodeStore
{

DummyScheduler::DummyScheduler ()
{
}

DummyScheduler::~DummyScheduler ()
{
}

void DummyScheduler::scheduleTask (Task& task)
{
    // Invoke the task synchronously.
    task.performScheduledTask();
}

void DummyScheduler::scheduledTasksStopped ()
{
}

}

