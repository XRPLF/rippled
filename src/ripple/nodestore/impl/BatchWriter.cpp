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

#include <ripple/nodestore/impl/BatchWriter.h>

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

void
BatchWriter::store (std::shared_ptr<NodeObject> const& object)
{
    std::lock_guard<decltype(mWriteMutex)> sl (mWriteMutex);

    mWriteSet.push_back (object);

    if (! mWritePending)
    {
        mWritePending = true;

        m_scheduler.scheduleTask (*this);
    }
}

int
BatchWriter::getWriteLoad ()
{
    std::lock_guard<decltype(mWriteMutex)> sl (mWriteMutex);

    return std::max (mWriteLoad, static_cast<int> (mWriteSet.size ()));
}

void
BatchWriter::performScheduledTask ()
{
    writeBatch ();
}

void
BatchWriter::writeBatch ()
{
    for (;;)
    {
        std::vector< std::shared_ptr<NodeObject> > set;

        set.reserve (batchWritePreallocationSize);

        {
            std::lock_guard<decltype(mWriteMutex)> sl (mWriteMutex);

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

        BatchWriteReport report;
        report.writeCount = set.size();
        auto const before = std::chrono::steady_clock::now();

        m_callback.writeBatch (set);

        report.elapsed = std::chrono::duration_cast <std::chrono::milliseconds>
            (std::chrono::steady_clock::now() - before);

        m_scheduler.onBatchWrite (report);
    }
}

void
BatchWriter::waitForWriting ()
{
    std::unique_lock <decltype(mWriteMutex)> sl (mWriteMutex);

    while (mWritePending)
        mWriteCondition.wait (sl);
}

}
}
