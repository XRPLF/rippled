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
    m_thread.stopThread (-1);
}

bool InterruptibleThread::wait (int milliSeconds)
{
    // Can only be called from the corresponding thread of execution.
    //
    bassert (isTheCurrentThread ());

    bool interrupted = false;

    for (;;)
    {
        bassert (m_state != stateWait);

        // See if we are interrupted
        //
        if (m_state.tryChangeState (stateInterrupt, stateRun))
        {
            // We were interrupted, state is changed to Run. Caller must run now.
            //
            interrupted = true;
            break;
        }
        else if (m_state.tryChangeState (stateRun, stateWait) ||
                 m_state.tryChangeState (stateReturn, stateWait))
        {
            // Transitioned to wait. Caller must wait now.
            //
            interrupted = false;
            break;
        }
    }

    if (! interrupted)
    {
        bassert (m_state == stateWait);

        interrupted = m_thread.wait (milliSeconds);

        if (! interrupted)
        {
            // The wait timed out
            //
            if (m_state.tryChangeState (stateWait, stateRun))
            {
                interrupted = false;
            }
            else
            {
                bassert (m_state == stateInterrupt);

                interrupted = true;
            }
        }
        else
        {
            // The event became signalled, which can only
            // happen via m_event.notify() in interrupt()
            //
            bassert (m_state == stateRun);
        }
    }

    return interrupted;
}

void InterruptibleThread::interrupt ()
{
    for (;;)
    {
        int const state = m_state;

        if (state == stateInterrupt ||
                state == stateReturn ||
                m_state.tryChangeState (stateRun, stateInterrupt))
        {
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

bool InterruptibleThread::interruptionPoint ()
{
    // Can only be called from the thread of execution.
    //
    bassert (isTheCurrentThread ());

    if (m_state == stateWait)
    {
        // It is impossible for this function to be called while in the wait state.
        //
        Throw (Error ().fail (__FILE__, __LINE__));
    }
    else if (m_state == stateReturn)
    {
        // If this goes off it means the thread called the
        // interruption a second time after already getting interrupted.
        //
        Throw (Error ().fail (__FILE__, __LINE__));
    }

    bool const interrupted = m_state.tryChangeState (stateInterrupt, stateRun);

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
