//------------------------------------------------------------------------------
/*
    Copyright (c) 2011-2013, OpenCoin, Inc.
*/
//==============================================================================

namespace NodeStore
{

BatchWriter::BatchWriter (Callback& callback, Scheduler& scheduler)
    : m_callback (callback)
    , m_scheduler (scheduler)
    , mWriteGeneration (0)
    , mWriteLoad (0)
    , mWritePending (false)
{
    mWriteSet.reserve (batchWritePreallocationSize);
}

BatchWriter::~BatchWriter ()
{
    waitForWriting ();
}

void BatchWriter::store (NodeObject::ref object)
{
    LockType::scoped_lock sl (mWriteMutex);

    mWriteSet.push_back (object);

    if (! mWritePending)
    {
        mWritePending = true;

        m_scheduler.scheduleTask (*this);
    }
}

int BatchWriter::getWriteLoad ()
{
    LockType::scoped_lock sl (mWriteMutex);

    return std::max (mWriteLoad, static_cast<int> (mWriteSet.size ()));
}

void BatchWriter::performScheduledTask ()
{
    writeBatch ();
}

void BatchWriter::writeBatch ()
{
    int setSize = 0;

    for (;;)
    {
        std::vector< boost::shared_ptr<NodeObject> > set;

        set.reserve (batchWritePreallocationSize);

        {
            LockType::scoped_lock sl (mWriteMutex);

            mWriteSet.swap (set);
            assert (mWriteSet.empty ());
            ++mWriteGeneration;
            mWriteCondition.notify_all ();

            if (set.empty ())
            {
                mWritePending = false;
                mWriteLoad = 0;

                // VFALCO NOTE Fix this function to not return from the middle
                return;
            }

            int newSetSize = mWriteSet.size();
            mWriteLoad = std::max (setSize, newSetSize);
            newSetSize = set.size ();
        }

        m_callback.writeBatch (set);
    }
}

void BatchWriter::waitForWriting ()
{
    LockType::scoped_lock sl (mWriteMutex);
    int gen = mWriteGeneration;

    while (mWritePending && (mWriteGeneration == gen))
        mWriteCondition.wait (sl);
}

}
