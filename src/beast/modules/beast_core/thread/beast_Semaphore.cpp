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

Semaphore::Semaphore (int initialCount)
    : m_counter (initialCount)
{
}

Semaphore::~Semaphore ()
{
    // Can't delete the semaphore while threads are waiting on it!!
    bassert (m_waitingThreads.pop_front () == nullptr);

    for (;;)
    {
        WaitingThread* waitingThread = m_deleteList.pop_front ();

        if (waitingThread != nullptr)
            delete waitingThread;
        else
            break;
    }
}

void Semaphore::signal (int amount)
{
    bassert (amount > 0);

    while (amount--)
    {
        // Make counter and list operations atomic.
        LockType::ScopedLockType lock (m_mutex);

        if (++m_counter <= 0)
        {
            WaitingThread* waitingThread = m_waitingThreads.pop_front ();

            bassert (waitingThread != nullptr);

            waitingThread->signal ();
        }
    }
}

bool Semaphore::wait (int timeOutMilliseconds)
{
    bool signaled = true;

    // Always prepare the WaitingThread object first, either
    // from the delete list or through a new allocation.
    //
    WaitingThread* waitingThread = m_deleteList.pop_front ();

    if (waitingThread == nullptr)
        waitingThread = new WaitingThread;

    {
        // Make counter and list operations atomic.
        LockType::ScopedLockType lock (m_mutex);

        if (--m_counter >= 0)
        {
            // Acquired the resource so put waitingThread back.
            m_deleteList.push_front (waitingThread);

            waitingThread = nullptr;
        }
        else
        {
            // Out of resources, go on to the waiting list.
            m_waitingThreads.push_front (waitingThread);
        }
    }

    // Do we need to wait?
    if (waitingThread != nullptr)
    {
        // Yes so do it.
        signaled = waitingThread->wait (timeOutMilliseconds);

        // If the wait is satisfied, then we've been taken off the
        // waiting list so put waitingThread back in the delete list.
        //
        m_deleteList.push_front (waitingThread);
    }

    return signaled;
}

//------------------------------------------------------------------------------

Semaphore::WaitingThread::WaitingThread ()
    : m_event (false) // auto-reset
{
}

bool Semaphore::WaitingThread::wait (int timeOutMilliseconds)
{
    return m_event.wait (timeOutMilliseconds);
}

void Semaphore::WaitingThread::signal ()
{
    m_event.signal ();
}

//------------------------------------------------------------------------------

// VFALCO TODO Unit Tests!
