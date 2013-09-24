//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_SCHEDULER_H_INCLUDED
#define RIPPLE_NODESTORE_SCHEDULER_H_INCLUDED

namespace NodeStore
{

/** Scheduling for asynchronous backend activity
    
    For improved performance, a backend has the option of performing writes
    in batches. These writes can be scheduled using the provided scheduler
    object.

    @see BatchWriter
*/
class Scheduler
{
public:
    virtual ~Scheduler() { }

    /** Schedules a task.
        Depending on the implementation, the task may be invoked either on
        the current thread of execution, or an unspecified implementation-defined
        foreign thread.
    */
    virtual void scheduleTask (Task& task) = 0;
};

}

#endif
