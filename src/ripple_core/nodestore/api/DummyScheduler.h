//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_DUMMYSCHEDULER_H_INCLUDED
#define RIPPLE_NODESTORE_DUMMYSCHEDULER_H_INCLUDED

namespace NodeStore
{

// Simple Scheduler that just peforms the tasks synchronously.
class DummyScheduler : public Scheduler
{
public:
    DummyScheduler ();

    ~DummyScheduler ();

    void scheduleTask (Task& task);

    void scheduledTasksStopped ();
};

}

#endif
