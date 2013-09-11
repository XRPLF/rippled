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

void ThreadGroup::QuitType::operator () (Worker* worker)
{
    worker->setShouldExit ();
}

//==============================================================================

ThreadGroup::Worker::Worker (String name, ThreadGroup& group)
    : Thread (name)
    , m_group (group)
    , m_shouldExit (false)
{
    startThread ();
}

ThreadGroup::Worker::~Worker ()
{
    // Make sure the thread is stopped.
    stopThread (-1);
}

void ThreadGroup::Worker::setShouldExit ()
{
    m_shouldExit = true;
}

void ThreadGroup::Worker::run ()
{
    do
    {
        m_group.m_semaphore.wait ();

        Work* work = m_group.m_queue.pop_front ();

        bassert (work != nullptr);

        work->operator () (this);

        delete work;
    }
    while (!m_shouldExit);
}

//==============================================================================

ThreadGroup::ThreadGroup (int numberOfThreads)
    : m_numberOfThreads (numberOfThreads)
    , m_semaphore (0)
{
    for (int i = 0; i++ < numberOfThreads; )
    {
        String s;
        s << "ThreadGroup (" << i << ")";

        m_threads.push_front (new Worker (s, *this));
    }
}

ThreadGroup::~ThreadGroup ()
{
    // Put one quit item in the queue for each worker to stop.
    for (int i = 0; i < m_numberOfThreads; ++i)
    {
        m_queue.push_front (new (getAllocator ()) QuitType);

        m_semaphore.signal ();
    }

    for (;;)
    {
        Worker* worker = m_threads.pop_front ();

        if (worker != nullptr)
            delete worker;
        else
            break;
    }

    // There must not be pending work!
    bassert (m_queue.pop_front () == nullptr);
}

int ThreadGroup::getNumberOfThreads () const
{
    return m_numberOfThreads;
}
