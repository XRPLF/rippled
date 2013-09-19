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

#ifndef BEAST_SEMAPHORE_H_INCLUDED
#define BEAST_SEMAPHORE_H_INCLUDED

/*============================================================================*/
/**
  A semaphore.

  This provides a traditional semaphore synchronization primitive. There is no
  upper limit on the number of signals.

  @note There is no tryWait() or timeout facility for acquiring a resource.

  @ingroup beast_core
*/
class BEAST_API Semaphore
{
public:
    /** Create a semaphore with the specified number of resources.

        @param initialCount The starting number of resources.
    */
    explicit Semaphore (int initialCount = 0);

    ~Semaphore ();

    /** Increase the number of available resources.

        @param amount The number of new resources available.
    */
    void signal (int amount = 1);

    /** Wait for a resource.

        A negative time-out value means that the method will wait indefinitely.

        @returns true if the event has been signalled, false if the timeout expires.
    */
    bool wait (int timeOutMilliseconds = -1);

private:
    class WaitingThread
        : public LockFreeStack <WaitingThread>::Node
        , LeakChecked <WaitingThread>
    {
    public:
        WaitingThread ();

        bool wait (int timeOutMilliseconds);
        void signal ();

    private:
        WaitableEvent m_event;
    };

    typedef SpinLock LockType;

    LockType m_mutex;
    Atomic <int> m_counter;
    LockFreeStack <WaitingThread> m_waitingThreads;
    LockFreeStack <WaitingThread> m_deleteList;
};

#endif
