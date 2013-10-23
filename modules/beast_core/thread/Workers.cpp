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

Workers::Workers (Callback& callback, String const& threadNames, int numberOfThreads)
    : m_callback (callback)
    , m_threadNames (threadNames)
    , m_allPaused (true, true)
    , m_semaphore (0)
    , m_numberOfThreads (0)
{
    setNumberOfThreads (numberOfThreads);
}

Workers::~Workers ()
{
    pauseAllThreadsAndWait ();

    deleteWorkers (m_everyone);
}

int Workers::getNumberOfThreads () const noexcept
{
    return m_numberOfThreads;
}

// VFALCO NOTE if this function is called quickly to reduce then
//             increase the number of threads, it could result in
//             more paused threads being created than expected.
//
void Workers::setNumberOfThreads (int numberOfThreads)
{
    if (m_numberOfThreads != numberOfThreads)
    {
        if (numberOfThreads > m_numberOfThreads)
        {
            // Increasing the number of working threads

            int const amount = numberOfThreads - m_numberOfThreads;

            for (int i = 0; i < amount; ++i)
            {
                // See if we can reuse a paused worker
                Worker* worker = m_paused.pop_front ();

                if (worker != nullptr)
                {
                    // If we got here then the worker thread is at [1]
                    // This will unblock their call to wait()
                    //
                    worker->notify ();
                }
                else
                {
                    worker = new Worker (*this, m_threadNames);
                }

                m_everyone.push_front (worker);
            }
        }
        else if (numberOfThreads < m_numberOfThreads)
        {
            // Decreasing the number of working threads

            int const amount = m_numberOfThreads - numberOfThreads;

            for (int i = 0; i < amount; ++i)
            {
                ++m_pauseCount;

                // Pausing a thread counts as one "internal task"
                m_semaphore.signal ();
            }
        }

        m_numberOfThreads = numberOfThreads;
    }
}

void Workers::pauseAllThreadsAndWait ()
{
    setNumberOfThreads (0);

    m_allPaused.wait ();

    bassert (numberOfCurrentlyRunningTasks () == 0);
}

void Workers::addTask ()
{
    m_semaphore.signal ();
}

int Workers::numberOfCurrentlyRunningTasks () const noexcept
{
    return m_runningTaskCount.get ();
}

double Workers::getUtilization () const
{
    return m_usage.getUtilization();
}

void Workers::deleteWorkers (LockFreeStack <Worker>& stack)
{
    for (;;)
    {
        Worker* const worker = stack.pop_front ();

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

//------------------------------------------------------------------------------

Workers::Worker::Worker (Workers& workers, String const& threadName)
    : Thread (threadName)
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
        // Increment the count of active workers, and if
        // we are the first one then reset the "all paused" event
        //
        if (++m_workers.m_activeCount == 1)
            m_workers.m_allPaused.reset ();

        for (;;)
        {
            // Acquire a task or "internal task."
            //
            {
                CPUMeter::ScopedIdleTime elapsed (m_workers.m_usage);

                m_workers.m_semaphore.wait ();
            }

            {
                CPUMeter::ScopedActiveTime elapsed (m_workers.m_usage);

                // See if there's a pause request. This
                // counts as an "internal task."
                //
                int pauseCount = m_workers.m_pauseCount.get ();

                if (pauseCount > 0)
                {
                    // Try to decrement
                    pauseCount = --m_workers.m_pauseCount;

                    if (pauseCount >= 0)
                    {
                        // We got paused
                        break;
                    }
                    else
                    {
                        // Undo our decrement
                        ++m_workers.m_pauseCount;
                    }
                }

                // We couldn't pause so we must have gotten
                // unblocked in order to process a task.
                //
                ++m_workers.m_runningTaskCount;
                m_workers.m_callback.processTask ();
                --m_workers.m_runningTaskCount;
            }

            // Put the name back in case the callback changed it
            Thread::setCurrentThreadName (m_threadName);
        }

        // Any worker that goes into the paused list must
        // guarantee that it will eventually block on its
        // event object.
        //
        m_workers.m_paused.push_front (this);

        // Decrement the count of active workers, and if we
        // are the last one then signal the "all paused" event.
        //
        if (--m_workers.m_activeCount == 0)
            m_workers.m_allPaused.signal ();

        Thread::setCurrentThreadName (m_threadName + " (paused)");

        // [1] We will be here when the paused list is popped
        //
        // We block on our event object, a requirement of being
        // put into the paused list.
        //
        // This will get signaled on either a reactivate or a stopThread()
        //
        wait ();
    }
}

//------------------------------------------------------------------------------

class WorkersTests : public UnitTest
{
public:
    WorkersTests () : UnitTest ("Workers", "beast")
    {
    }

    struct TestCallback : Workers::Callback
    {
        explicit TestCallback (int count_)
            : finished (false, count_ == 0)
            , count (count_)
        {
        }

        void processTask ()
        {
            if (--count == 0)
                finished.signal ();
        }

        WaitableEvent finished;
        Atomic <int> count;
    };

    void testThreads (int const threadCount)
    {
        String s;
        s << "threadCount = " << String (threadCount);
        beginTestCase (s);

        TestCallback cb (threadCount);

        Workers w (cb, "Test", 0);
        expect (w.getNumberOfThreads () == 0);

        w.setNumberOfThreads (threadCount);
        expect (w.getNumberOfThreads () == threadCount);

        for (int i = 0; i < threadCount; ++i)
            w.addTask ();

        // 10 seconds should be enough to finish on any system
        //
        bool signaled = cb.finished.wait (10 * 1000);

        expect (signaled, "timed out");

        w.pauseAllThreadsAndWait ();

        int const count (cb.count.get ());

        expectEquals (count, 0);
    }

    void runTest ()
    {
        testThreads (0);
        testThreads (1);
        testThreads (2);
        testThreads (4);
        testThreads (16);
        testThreads (64);
    }
};

static WorkersTests workersTests;
