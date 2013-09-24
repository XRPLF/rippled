//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_TASK_H_INCLUDED
#define RIPPLE_NODESTORE_TASK_H_INCLUDED

namespace NodeStore
{

/** Derived classes perform scheduled tasks. */
struct Task
{
    virtual ~Task () { }

    /** Performs the task.
        The call may take place on a foreign thread.
    */
    virtual void performScheduledTask () = 0;
};

}

#endif
