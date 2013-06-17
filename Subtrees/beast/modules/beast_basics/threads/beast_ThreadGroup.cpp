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
