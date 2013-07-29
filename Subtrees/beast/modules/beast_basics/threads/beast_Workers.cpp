//------------------------------------------------------------------------------
/*
    This file is part of Beast: https://github.com/vinniefalco/Beast
    Copyright 2013, Vinnie Falco <vinnie.falco@gmail.com>

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

//
// Workers::Worker
//

Workers::Worker::Worker (Workers& workers)
    : Thread ("Worker")
    , m_workers (workers)
{
    startThread ();
}

Workers::Worker::~Worker ()
{
    stopThread ();
}

void Workers::Worker::run ()
{
    while (! threadShouldExit ())
    {
        m_workers.m_allPaused.reset ();

        ++m_workers.m_activeCount;

        for (;;)
        {
            m_workers.m_semaphore.wait ();

            // See if we should pause
            int pauseCount = m_workers.m_pauseCount.get ();
                        
            if (pauseCount > 0)
            {
                // Try to decrement
                pauseCount = --m_workers.m_pauseCount;

                // Did we get paused>
                if (pauseCount >= 0)
                {
                    // Yes, so signal again if we need more threads to pause
                    if (pauseCount > 0)
                        m_workers.addTask ();

                    break;
                }
                else
                {
                    // Not paused, undo the decrement
                    ++m_workers.m_pauseCount;
                }
            }

            m_workers.m_callback.processTask ();
        }

        // must happen before decrementing m_activeCount
        m_workers.m_paused.push_front (this);

        if (--m_workers.m_activeCount == 0)
            m_workers.m_allPaused.signal ();

        // If we get here then this thread became sidelined via
        // a call to setNumberOfThreads. We block on the thread event
        // instead of exiting the thread, because it is bad form for
        // a server process to constantly create and destroy threads.
        //
        // The thread event is signaled either to make the thread
        // resume participating in tasks, or to make it exit.
        //

        wait ();
    }
}

//------------------------------------------------------------------------------

//
// Workers
//

Workers::Workers (Callback& callback, int numberOfThreads)
    : m_callback (callback)
    , m_semaphore (0)
    , m_numberOfThreads (0)
{
    setNumberOfThreads (numberOfThreads);
}

Workers::~Workers ()
{
    setNumberOfThreads (0);

    m_allPaused.wait ();

    deleteWorkers (m_active);
    deleteWorkers (m_paused);
}

void Workers::setNumberOfThreads (int numberOfThreads)
{
    if (numberOfThreads > m_numberOfThreads)
    {
        int const amount = numberOfThreads - m_numberOfThreads;

        for (int i = 0; i < amount; ++i)
        {
            Worker* worker = m_paused.pop_front ();

            if (worker != nullptr)
            {
                worker->notify ();
            }
            else
            {
                worker = new Worker (*this);
            }

            m_active.push_front (worker);
        }
    }
    else if (numberOfThreads < m_numberOfThreads)
    {
        int const amount = m_numberOfThreads - numberOfThreads;

        for (int i = 0; i < amount; ++i)
        {
            ++m_pauseCount;
        }

        // pausing threads counts as an "internal task"
        m_semaphore.signal ();
    }
}

void Workers::addTask ()
{
    m_semaphore.signal ();
}

void Workers::deleteWorkers (LockFreeStack <Worker>& stack)
{
    for (;;)
    {
        Worker* worker = stack.pop_front ();

        if (worker != nullptr)
        {
            // This call blocks until the thread orderly exits 
            delete worker;
        }
        else
        {
            break;
        }
    }
}
