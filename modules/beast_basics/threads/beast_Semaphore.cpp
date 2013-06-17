/*============================================================================*/
/*
  VFLib: https://github.com/vinniefalco/VFLib

  Copyright (C) 2008 by Vinnie Falco <vinnie.falco@gmail.com>

  This library contains portions of other open source products covered by
  separate licenses. Please see the corresponding source files for specific
  terms.

  VFLib is provided under the terms of The MIT License (MIT):

  Permission is hereby granted, free of charge, to any person obtaining a copy
  of this software and associated documentation files (the "Software"), to deal
  in the Software without restriction, including without limitation the rights
  to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
  copies of the Software, and to permit persons to whom the Software is
  furnished to do so, subject to the following conditions:

  The above copyright notice and this permission notice shall be included in
  all copies or substantial portions of the Software.

  THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
  IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
  FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
  AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
  LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING
  FROM, OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS
  IN THE SOFTWARE.
*/
/*============================================================================*/

Semaphore::WaitingThread::WaitingThread ()
    : m_event (false) // auto-reset
{
}

void Semaphore::WaitingThread::wait ()
{
    m_event.wait ();
}

void Semaphore::WaitingThread::signal ()
{
    m_event.signal ();
}

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

void Semaphore::wait ()
{
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
        waitingThread->wait ();

        // If the wait is satisfied, then we've been taken off the
        // waiting list so put waitingThread back in the delete list.
        //
        m_deleteList.push_front (waitingThread);
    }
}
