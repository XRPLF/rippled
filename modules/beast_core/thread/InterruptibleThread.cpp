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

InterruptibleThread::ThreadHelper::ThreadHelper (String name,
        InterruptibleThread* owner)
    : Thread (name)
    , m_owner (owner)
{
}

InterruptibleThread* InterruptibleThread::ThreadHelper::getOwner () const
{
    return m_owner;
}

void InterruptibleThread::ThreadHelper::run ()
{
    m_owner->run ();
}

//------------------------------------------------------------------------------

InterruptibleThread::InterruptibleThread (String name)
    : m_thread (name, this)
    , m_entryPoint (nullptr)
    , m_state (stateRun)
{
}

InterruptibleThread::~InterruptibleThread ()
{
    m_runEvent.signal ();

    join ();
}

void InterruptibleThread::start (EntryPoint* const entryPoint)
{
    m_entryPoint = entryPoint;

    m_thread.startThread ();

    // Prevent data race with member variables
    //
    m_runEvent.signal ();
}

void InterruptibleThread::join ()
{
    m_thread.signalThreadShouldExit();
    m_thread.notify();
    interrupt();
    m_thread.stopThread (-1);
}

// Block until there is an interruption.
// This counts as an interruption point.
//
void InterruptibleThread::wait ()
{
    // Can only be called from the thread of execution.
    bassert (isTheCurrentThread ());

    for (;;)
    {
        // Impossible for us to already be in the wait state.
        bassert (m_state != stateWait);

        // See if we are interrupted.
        if (m_state.tryChangeState (stateInterrupt, stateRun))
        {
            // We were interrupted, so the wait is satisfied.
            return;
        }
        else
        {
            // Try to get into the wait state.
            if (m_state.tryChangeState (stateRun, stateWait))
            {
                bassert (m_state == stateWait);

                // Got into the wait state so block until interrupt.
                m_thread.wait ();

                // Event is signalled means we were
                // interrupted, so the wait is satisfied.
                bassert (m_state != stateWait || m_thread.threadShouldExit ());
                return;
            }
        }
    }
}

void InterruptibleThread::interrupt ()
{
    for (;;)
    {
        int const state (m_state);

        if (state == stateInterrupt ||
            m_state.tryChangeState (stateRun, stateInterrupt))
        {
            // We got into the interrupt state, the thead
            // will see this at the next interruption point.
            //
            // Thread will see this at next interruption point.
            //
            break;
        }
        else if (m_state.tryChangeState (stateWait, stateRun))
        {
            m_thread.notify ();
            break;
        }
    }
}

// Returns true if the thead function should stop
// its activities as soon as possible and return.
//
bool InterruptibleThread::interruptionPoint ()
{
    // Can only be called from the thread of execution.
    bassert (isTheCurrentThread ());

    // Impossible for this to be called in the wait state.
    bassert (m_state != stateWait);

    bool const interrupted (m_state.tryChangeState (stateInterrupt, stateRun));

    return interrupted;
}

InterruptibleThread::id InterruptibleThread::getId () const
{
    return m_threadId;
}

bool InterruptibleThread::isTheCurrentThread () const
{
    return m_thread.getCurrentThreadId () == m_threadId;
}

void InterruptibleThread::setPriority (int priority)
{
    m_thread.setPriority (priority);
}

InterruptibleThread* InterruptibleThread::getCurrentThread ()
{
    InterruptibleThread* result = nullptr;

    Thread* const thread = Thread::getCurrentThread ();

    if (thread != nullptr)
    {
        ThreadHelper* const helper = dynamic_cast <ThreadHelper*> (thread);

        bassert (helper != nullptr);

        result = helper->getOwner ();
    }

    return result;
}

void InterruptibleThread::run ()
{
    m_threadId = m_thread.getThreadId ();

    m_runEvent.wait ();

    m_entryPoint->threadRun ();
}

//------------------------------------------------------------------------------

bool CurrentInterruptibleThread::interruptionPoint ()
{
    bool interrupted = false;

    InterruptibleThread* const interruptibleThread (InterruptibleThread::getCurrentThread ());

    bassert (interruptibleThread != nullptr);

    interrupted = interruptibleThread->interruptionPoint ();

    return interrupted;
}

//------------------------------------------------------------------------------

class InterruptibleThreadTests : public UnitTest
{
public:
    enum
    {
        callsPerThread = 100000
    };

    struct TestThread : InterruptibleThread::EntryPoint
    {
        explicit TestThread (int id)
            : m_thread ("#" + String::fromNumber (id))
        {
            m_thread.start (this);
        }

        void threadRun ()
        {
            while (! m_thread.peekThread().threadShouldExit())
            {
                String s;

                while (!m_thread.interruptionPoint ())
                {
                    s = s + String::fromNumber (m_random.nextInt ());

                    if (s.length () > 100)
                        s = String::empty;
                }
            }
        }

        Random m_random;
        InterruptibleThread m_thread;
    };
    
    void testThreads (std::size_t nThreads)
    {
        beginTestCase (String::fromNumber (nThreads) + " threads");

        OwnedArray <TestThread> threads;
        threads.ensureStorageAllocated (nThreads);

        for (std::size_t i = 0; i < nThreads; ++i)
            threads.add (new TestThread (i + 1));

        for (std::size_t i = 0; i < callsPerThread * nThreads; ++i)
        {
            int const n (random().nextInt (threads.size()));
            threads[n]->m_thread.interrupt();
        }

        pass ();
    }

    void runTest ()
    {
        testThreads (8);
        testThreads (64);
    }

    InterruptibleThreadTests () : UnitTest ("InterruptibleThread", "beast")
    {
    }
};

static InterruptibleThreadTests interruptibleThreadTests;
