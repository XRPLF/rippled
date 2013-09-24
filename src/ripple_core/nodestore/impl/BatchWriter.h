//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

#ifndef RIPPLE_NODESTORE_BATCHWRITER_H_INCLUDED
#define RIPPLE_NODESTORE_BATCHWRITER_H_INCLUDED

namespace NodeStore
{

/** Batch-writing assist logic.

    The batch writes are performed with a scheduled task. Use of the
    class it not required. A backend can implement its own write batching,
    or skip write batching if doing so yields a performance benefit.

    @see Scheduler
*/
// VFALCO NOTE I'm not entirely happy having placed this here,
//             because whoever needs to use NodeStore certainly doesn't
//             need to see the implementation details of BatchWriter.
//
class BatchWriter : private Task
{
public:
    /** This callback does the actual writing. */
    struct Callback
    {
        virtual void writeBatch (Batch const& batch) = 0;
    };

    /** Create a batch writer. */
    BatchWriter (Callback& callback, Scheduler& scheduler);

    /** Destroy a batch writer.

        Anything pending in the batch is written out before this returns.
    */
    ~BatchWriter ();

    /** Store the object.

        This will add to the batch and initiate a scheduled task to
        write the batch out.
    */
    void store (NodeObject::Ptr const& object);

    /** Get an estimate of the amount of writing I/O pending. */
    int getWriteLoad ();

private:
    void performScheduledTask ();
    void writeBatch ();
    void waitForWriting ();

private:
    typedef boost::recursive_mutex LockType;
    typedef boost::condition_variable_any CondvarType;

    Callback& m_callback;
    Scheduler& m_scheduler;
    LockType mWriteMutex;
    CondvarType mWriteCondition;
    int mWriteGeneration;
    int mWriteLoad;
    bool mWritePending;
    Batch mWriteSet;
};

}

#endif
