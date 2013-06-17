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

ThreadWithCallQueue::ThreadWithCallQueue (String name)
    : CallQueue (name)
    , m_thread (name)
    , m_entryPoints (nullptr)
    , m_calledStart (false)
    , m_calledStop (false)
    , m_shouldStop (false)
{
}

ThreadWithCallQueue::~ThreadWithCallQueue ()
{
    stop (true);
}

void ThreadWithCallQueue::start (EntryPoints* const entryPoints)
{
    {
        // This is mostly for diagnostics
        // TODO: Atomic flag for this whole thing
        CriticalSection::ScopedLockType lock (m_mutex);

        // start() MUST be called.
        bassert (!m_calledStart);
        m_calledStart = true;
    }

    m_entryPoints = entryPoints;

    m_thread.start (this);
}

void ThreadWithCallQueue::stop (bool const wait)
{
    // can't call stop(true) from within a thread function
    bassert (!wait || !m_thread.isTheCurrentThread ());

    {
        CriticalSection::ScopedLockType lock (m_mutex);

        // start() MUST be called.
        bassert (m_calledStart);

        // TODO: Atomic for this
        if (!m_calledStop)
        {
            m_calledStop = true;

            {
                CriticalSection::ScopedUnlockType unlock (m_mutex); // getting fancy

                call (&ThreadWithCallQueue::doStop, this);

                // in theory something could slip in here

                close ();
            }
        }
    }

    if (wait)
        m_thread.join ();
}

// Should be called periodically by the idle function.
// There are three possible results:
//
// #1 Returns false. The idle function may continue or return.
// #2 Returns true. The idle function should return as soon as possible.
// #3 Throws a Thread::Interruption exception.
//
// If interruptionPoint returns true or throws, it must
// not be called again before the thread has the opportunity to reset.
//
bool ThreadWithCallQueue::interruptionPoint ()
{
    return m_thread.interruptionPoint ();
}

// Interrupts the idle function by queueing a call that does nothing.
void ThreadWithCallQueue::interrupt ()
{
    call (&ThreadWithCallQueue::doNothing);
}

void ThreadWithCallQueue::doNothing ()
{
    // Intentionally empty
}

void ThreadWithCallQueue::signal ()
{
    m_thread.interrupt ();
}

void ThreadWithCallQueue::reset ()
{
}

void ThreadWithCallQueue::doStop ()
{
    m_shouldStop = true;
}

void ThreadWithCallQueue::threadRun ()
{
    m_entryPoints->threadInit ();

    for (;;)
    {
        CallQueue::synchronize ();

        if (m_shouldStop)
            break;

        bool interrupted = m_entryPoints->threadIdle ();

        if (!interrupted)
            interrupted = interruptionPoint ();

        if (!interrupted)
            m_thread.wait ();
    }

    m_entryPoints->threadExit ();
}
