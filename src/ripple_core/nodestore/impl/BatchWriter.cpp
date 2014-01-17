//------------------------------------------------------------------------------
/*
    This file is part of rippled: https://github.com/ripple/rippled
    Copyright (c) 2012, 2013 Ripple Labs Inc.

    Permission to use, copy, modify, and/or distribute this software for any
    purpose  with  or without fee is hereby granted, provided that the above
    copyright notice and this permission notice appear in all copies.

    THE  SOFTWARE IS PROVIDED "AS IS" AND THE AUTHOR DISCLAIMS ALL WARRANTIES
    WITH  REGARD  TO  THIS  SOFTWARE  INCLUDING  ALL  IMPLIED  WARRANTIES  OF
    MERCHANTABILITY  AND  FITNESS. IN NO EVENT SHALL THE AUTHOR BE LIABLE FOR
    ANY  SPECIAL ,  DIRECT, INDIRECT, OR CONSEQUENTIAL DAMAGES OR ANY DAMAGES
    WHATSOEVER  RESULTING  FROM  LOSS  OF USE, DATA OR PROFITS, WHETHER IN AN
    ACTION  OF  CONTRACT, NEGLIGENCE OR OTHER TORTIOUS ACTION, ARISING OUT OF
    OR IN CONNECTION WITH THE USE OR PERFORMANCE OF THIS SOFTWARE.
*/
//==============================================================================

namespace ripple {
namespace NodeStore {

BatchWriter::BatchWriter (Callback& callback, Scheduler& scheduler)
    : m_callback (callback)
    , m_scheduler (scheduler)
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
    for (;;)
    {
        std::vector< boost::shared_ptr<NodeObject> > set;

        set.reserve (batchWritePreallocationSize);

        {
            LockType::scoped_lock sl (mWriteMutex);

            mWriteSet.swap (set);
            assert (mWriteSet.empty ());
            mWriteLoad = set.size ();

            if (set.empty ())
            {
                mWritePending = false;
                mWriteCondition.notify_all ();

                // VFALCO NOTE Fix this function to not return from the middle
                return;
            }

        }

        m_callback.writeBatch (set);
    }
}

void BatchWriter::waitForWriting ()
{
    LockType::scoped_lock sl (mWriteMutex);

    while (mWritePending)
        mWriteCondition.wait (sl);
}

}
}
