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
